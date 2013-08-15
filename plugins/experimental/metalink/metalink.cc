/** @file

    Implement the Metalink protocol to "dedup" cache entries for
    equivalent content. This can for example improve the cache hit
    ratio for content with many different (unique) URLs.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one
    or more contributor license agreements.  See the NOTICE file
    distributed with this work for additional information
    regarding copyright ownership.  The ASF licenses this file
    to you under the Apache License, Version 2.0 (the
    "License"); you may not use this file except in compliance
    with the License.  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/


/*
  This plugin was originally developed by Jack Bates during his Google
  Summer of Code 2012 project for Metalinker.
*/


#include <stdio.h>
#include <string.h>

#include <openssl/sha.h>

#include "ts/ts.h"
#include "ink_defs.h"

#define PLUGIN_NAME "metalink"
#include "ts/debug.h"

typedef struct {
  TSVConn connp;
  TSIOBuffer bufp;

} WriteData;

typedef struct {
  TSHttpTxn txnp;

  /* Null transform */
  TSIOBuffer bufp;
  TSVIO viop;

  /* Message digest handle */
  SHA256_CTX c;

  TSCacheKey key;

} TransformData;

typedef struct {
  TSHttpTxn txnp;

  TSMBuffer resp_bufp;
  TSMLoc hdr_loc;

  /* "Location: ..." header */
  TSMLoc location_loc;

  /* Cache key */
  TSMLoc url_loc;
  TSCacheKey key;

  /* "Digest: SHA-256=..." header */
  TSMLoc digest_loc;

  /* Digest header field value index */
  int idx;

  TSIOBuffer read_bufp;

} SendData;

static int
write_vconn_write_complete(TSCont contp, void * /* edata ATS_UNUSED */)
{
  WriteData *data = (WriteData *) TSContDataGet(contp);
  TSContDestroy(contp);

  /* The object is not committed to the cache until the vconnection is closed.
   * When all data has been transferred, the user (contp) must do a
   * TSVConnClose() */
  TSVConnClose(data->connp);

  TSIOBufferDestroy(data->bufp);
  TSfree(data);

  return 0;
}

static int
write_handler(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    return write_vconn_write_complete(contp, edata);

  default:
    TSAssert(!"Unexpected event");
  }

  return 0;
}

static int
cache_open_write(TSCont contp, void *edata)
{
  TSMBuffer bufp;

  TSMLoc hdr_loc;
  TSMLoc url_loc;

  const char *value;
  int length;

  TransformData *transform_data = (TransformData *) TSContDataGet(contp);

  TSCacheKeyDestroy(transform_data->key);

  WriteData *write_data = (WriteData *) TSmalloc(sizeof(WriteData));
  write_data->connp = (TSVConn) edata;

  contp = TSContCreate(write_handler, NULL);
  TSContDataSet(contp, write_data);

  write_data->bufp = TSIOBufferCreate();
  TSIOBufferReader readerp = TSIOBufferReaderAlloc(write_data->bufp);

  if (TSHttpTxnClientReqGet(transform_data->txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSLogError("Couldn't retrieve client request header");

    return 0;
  }

  if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

    return 0;
  }

  value = TSUrlStringGet(bufp, url_loc, &length);
  if (!value) {
    TSHandleMLocRelease(bufp, hdr_loc, url_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

    return 0;
  }

  TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

  int nbytes = TSIOBufferWrite(write_data->bufp, value, length);

  TSVConnWrite(write_data->connp, contp, readerp, nbytes);

  return 0;
}

static int
cache_open_write_failed(TSCont contp, void * /* edata ATS_UNUSED */)
{
  TransformData *data = (TransformData *) TSContDataGet(contp);

  TSCacheKeyDestroy(data->key);

  return 0;
}

static int
vconn_write_ready(TSCont contp, void * /* edata ATS_UNUSED */)
{
  const char *value;
  int64_t length;
  char digest[32];
  TransformData *data = (TransformData *) TSContDataGet(contp);

  /* Can't TSVConnWrite() before TS_HTTP_RESPONSE_TRANSFORM_HOOK */
  if (!data->bufp) {
    TSVConn connp = TSTransformOutputVConnGet(contp);

    data->bufp = TSIOBufferCreate();
    TSIOBufferReader readerp = TSIOBufferReaderAlloc(data->bufp);

    data->viop = TSVConnWrite(connp, contp, readerp, INT64_MAX);

    SHA256_Init(&data->c);
  }

  TSVIO viop = TSVConnWriteVIOGet(contp);
  TSIOBuffer bufp = TSVIOBufferGet(viop);

  if (!bufp) {
    int ndone = TSVIONDoneGet(viop);
    TSVIONBytesSet(data->viop, ndone);

    TSVIOReenable(data->viop);

    return 0;
  }

  TSIOBufferReader readerp = TSVIOReaderGet(viop);
  int avail = TSIOBufferReaderAvail(readerp);

  if (avail > 0) {
    TSIOBufferCopy(data->bufp, readerp, avail, 0);

    /* Feed content to message digest */
    TSIOBufferBlock blockp = TSIOBufferReaderStart(readerp);
    while (blockp) {

      value = TSIOBufferBlockReadStart(blockp, readerp, &length);
      SHA256_Update(&data->c, value, length);

      blockp = TSIOBufferBlockNext(blockp);
    }

    TSIOBufferReaderConsume(readerp, avail);

    int ndone = TSVIONDoneGet(viop);
    TSVIONDoneSet(viop, ndone + avail);
  }

  /* If not finished and we copied some content */
  int ntodo = TSVIONTodoGet(viop);

  if (ntodo > 0) {
    if (avail > 0) {
      TSContCall(TSVIOContGet(viop), TS_EVENT_VCONN_WRITE_READY, viop);

      TSVIOReenable(data->viop);
    }
  /* If finished */
  } else {
    TSContCall(TSVIOContGet(viop), TS_EVENT_VCONN_WRITE_COMPLETE, viop);

    int ndone = TSVIONDoneGet(viop);
    TSVIONBytesSet(data->viop, ndone);

    TSVIOReenable(data->viop);

    SHA256_Final((unsigned char *) digest, &data->c);

    data->key = TSCacheKeyCreate();
    if (TSCacheKeyDigestSet(data->key, digest, sizeof(digest)) != TS_SUCCESS) {
      return 0;
    }

    TSCacheWrite(contp, data->key);
  }

  return 0;
}

static int
transform_vconn_write_complete(TSCont contp, void * /* edata ATS_UNUSED */)
{
  TransformData *data = (TransformData *) TSContDataGet(contp);

  TSVConn connp = TSTransformOutputVConnGet(contp);
  TSVConnShutdown(connp, 0, 1);

  TSIOBufferDestroy(data->bufp);
  TSfree(data);

  TSContDestroy(contp);

  return 0;
}

static int
transform_handler(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_CACHE_OPEN_WRITE:
    return cache_open_write(contp, edata);

  case TS_EVENT_CACHE_OPEN_WRITE_FAILED:
    return cache_open_write_failed(contp, edata);

  case TS_EVENT_IMMEDIATE:
  case TS_EVENT_VCONN_WRITE_READY:
    return vconn_write_ready(contp, edata);

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    return transform_vconn_write_complete(contp, edata);

  default:
    TSAssert(!"Unexpected event");
  }

  return 0;
}

static int
rewrite_handler(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  const char *value;
  int length;

  SendData *data = (SendData *) TSContDataGet(contp);
  TSContDestroy(contp);

  switch (event) {

  /* Yes: Rewrite "Location: ..." header and reenable response */
  case TS_EVENT_CACHE_OPEN_READ:
    value = TSUrlStringGet(data->resp_bufp, data->url_loc, &length);
    TSMimeHdrFieldValuesClear(data->resp_bufp, data->hdr_loc, data->location_loc);
    TSMimeHdrFieldValueStringInsert(data->resp_bufp, data->hdr_loc, data->location_loc, -1, value, length);
    break;

  case TS_EVENT_CACHE_OPEN_READ_FAILED:
    break;

  default:
    TSAssert(!"Unexpected event");
  }

  TSCacheKeyDestroy(data->key);

  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
  TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

  TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
  TSfree(data);

  return 0;
}

static int
cache_open_read(TSCont contp, void *edata)
{
  SendData *data = (SendData *) TSContDataGet(contp);
  TSVConn connp = (TSVConn) edata;

  data->read_bufp = TSIOBufferCreate();
  TSVConnRead(connp, contp, data->read_bufp, INT64_MAX);

  return 0;
}

static int
cache_open_read_failed(TSCont contp, void * /* edata ATS_UNUSED */)
{
  SendData *data = (SendData *) TSContDataGet(contp);
  TSContDestroy(contp);

  TSCacheKeyDestroy(data->key);

  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
  TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

  TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
  TSfree(data);

  return 0;
}

static int
vconn_read_ready(TSCont contp, void * /* edata ATS_UNUSED */)
{
  const char *value;
  int64_t length;
  SendData *data = (SendData *) TSContDataGet(contp);

  TSContDestroy(contp);

  TSIOBufferReader readerp = TSIOBufferReaderAlloc(data->read_bufp);
  TSIOBufferBlock blockp = TSIOBufferReaderStart(readerp);

  value = TSIOBufferBlockReadStart(blockp, readerp, &length);
  if (TSUrlParse(data->resp_bufp, data->url_loc, &value, value + length) != TS_PARSE_DONE) {
    TSIOBufferDestroy(data->read_bufp);

    TSCacheKeyDestroy(data->key);

    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
    TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  TSIOBufferDestroy(data->read_bufp);

  if (TSCacheKeyDigestFromUrlSet(data->key, data->url_loc) != TS_SUCCESS) {
    TSCacheKeyDestroy(data->key);

    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
    TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  contp = TSContCreate(rewrite_handler, NULL);
  TSContDataSet(contp, data);

  TSCacheRead(contp, data->key);

  return 0;
}

/* Check if "Digest: SHA-256=..." digest already exist in cache */

static int
digest_handler(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_CACHE_OPEN_READ:
    return cache_open_read(contp, edata);

  case TS_EVENT_CACHE_OPEN_READ_FAILED:
    return cache_open_read_failed(contp, edata);

  case TS_EVENT_VCONN_READ_READY:
    return vconn_read_ready(contp, edata);

  default:
    TSAssert(!"Unexpected event");
  }

  return 0;
}

/* Check if "Location: ..." URL already exist in cache */

static int
location_handler(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  SendData *data = (SendData *) TSContDataGet(contp);
  TSContDestroy(contp);

  switch (event) {
  /* Yes: Do nothing, just reenable response */
  case TS_EVENT_CACHE_OPEN_READ:
    break;

  /* No: Check "Digest: SHA-256=..." digest */
  case TS_EVENT_CACHE_OPEN_READ_FAILED:
    {
      const char *value;
      int length;

      /* ATS_BASE64_DECODE_DSTLEN() */
      char digest[33];

      value = TSMimeHdrFieldValueStringGet(data->resp_bufp, data->hdr_loc, data->digest_loc, data->idx, &length);
      if (TSBase64Decode(value + 8, length - 8, (unsigned char *) digest, sizeof(digest), NULL) != TS_SUCCESS
          || TSCacheKeyDigestSet(data->key, digest, 32 /* SHA-256 */ ) != TS_SUCCESS) {
        break;
      }

      contp = TSContCreate(digest_handler, NULL);
      TSContDataSet(contp, data);

      TSCacheRead(contp, data->key);
      TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->digest_loc);

      return 0;
    }
    break;

  default:
    TSAssert(!"Unexpected event");
  }

  TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->digest_loc);

  TSCacheKeyDestroy(data->key);

  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
  TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

  TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
  TSfree(data);

  return 0;
}

/* Compute SHA-256 digest, write to cache, and store there the request URL */

static int
http_read_response_hdr(TSCont /* contp ATS_UNUSED */, void *edata)
{
  TransformData *data = (TransformData *) TSmalloc(sizeof(TransformData));
  data->txnp = (TSHttpTxn) edata;

  /* Can't TSVConnWrite() before TS_HTTP_RESPONSE_TRANSFORM_HOOK */
  data->bufp = NULL;

  TSVConn connp = TSTransformCreate(transform_handler, data->txnp);
  TSContDataSet(connp, data);

  TSHttpTxnHookAdd(data->txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);

  TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

static int
http_send_response_hdr(TSCont contp, void *edata)
{
  const char *value;
  int length;

  SendData *data = (SendData *) TSmalloc(sizeof(SendData));

  data->txnp = (TSHttpTxn) edata;
  if (TSHttpTxnClientRespGet(data->txnp, &data->resp_bufp, &data->hdr_loc) != TS_SUCCESS) {
    TSLogError("Couldn't retrieve client response header");

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  /* If Instance Digests are not provided by the Metalink servers, the Link
   * header fields pertaining to this specification MUST be ignored */

  /* Metalinks contain whole file hashes as described in Section 6, and MUST
   * include SHA-256, as specified in [FIPS-180-3] */

  /* Assumption: Want to minimize cache read, so check first that:
   *
   *   1. response has "Location: ..." header
   *   2. response has "Digest: SHA-256=..." header
   *
   * Then scan if URL or digest already exist in cache */

  /* If response has "Location: ..." header */
  data->location_loc = TSMimeHdrFieldFind(data->resp_bufp, data->hdr_loc, TS_MIME_FIELD_LOCATION, TS_MIME_LEN_LOCATION);
  if (!data->location_loc) {
    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  TSUrlCreate(data->resp_bufp, &data->url_loc);

  /* If can't parse or lookup "Location: ..." URL, should still check if
   * response has "Digest: SHA-256=..." header? No: Can't parse or lookup URL
   * in "Location: ..." header is error */
  value = TSMimeHdrFieldValueStringGet(data->resp_bufp, data->hdr_loc, data->location_loc, 0, &length);
  if (TSUrlParse(data->resp_bufp, data->url_loc, &value, value + length) != TS_PARSE_DONE) {

    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
    TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  data->key = TSCacheKeyCreate();
  if (TSCacheKeyDigestFromUrlSet(data->key, data->url_loc) != TS_SUCCESS) {
    TSCacheKeyDestroy(data->key);

    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
    TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  /* ... and "Digest: SHA-256=..." header */
  data->digest_loc = TSMimeHdrFieldFind(data->resp_bufp, data->hdr_loc, "Digest", 6);
  while (data->digest_loc) {

    int count = TSMimeHdrFieldValuesCount(data->resp_bufp, data->hdr_loc, data->digest_loc);
    for (data->idx = 0; data->idx < count; data->idx += 1) {

      value = TSMimeHdrFieldValueStringGet(data->resp_bufp, data->hdr_loc, data->digest_loc, data->idx, &length);
      if (length < 8 + 44 /* 32 bytes, Base64 */ || strncasecmp(value, "SHA-256=", 8)) {
        continue;
      }

      contp = TSContCreate(location_handler, NULL);
      TSContDataSet(contp, data);

      TSCacheRead(contp, data->key);

      return 0;
    }

    TSMLoc next_loc = TSMimeHdrFieldNextDup(data->resp_bufp, data->hdr_loc, data->digest_loc);

    TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->digest_loc);

    data->digest_loc = next_loc;
  }

  TSCacheKeyDestroy(data->key);

  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
  TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

  TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
  TSfree(data);

  return 0;
}

static int
handler(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    return http_read_response_hdr(contp, edata);

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    return http_send_response_hdr(contp, edata);

  default:
    TSAssert(!"Unexpected event");
  }

  return 0;
}

void
TSPluginInit(int /* argc ATS_UNUSED */, const char * /* argv ATS_UNUSED */[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name = const_cast<char*>("metalink");
  info.vendor_name = const_cast<char*>("Jack Bates");
  info.support_email = const_cast<char*>("jack@nottheoilrig.com");

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
    TSLogError("Plugin registration failed");
  }

  TSCont contp = TSContCreate(handler, NULL);

  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
  TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
}
