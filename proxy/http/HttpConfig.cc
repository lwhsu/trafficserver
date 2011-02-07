/** @file

  A brief file description

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

#include "ink_config.h"
#include <ctype.h>
#include <string.h>
#include "HttpConfig.h"
#include "HTTP.h"
#include "ProcessManager.h"
#include "ProxyConfig.h"
#include "ICPProcessor.h"
#include "P_Net.h"
#include "P_RecUtils.h"

#ifndef min
#define         min(a,b)        ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define         max(a,b)        ((a) > (b) ? (a) : (b))
#endif

#define HttpEstablishStaticConfigStringAlloc(_ix,_n) \
  REC_EstablishStaticConfigStringAlloc(_ix,_n); \
  REC_RegisterConfigUpdateFunc(_n, http_config_cb, NULL)

#define HttpEstablishStaticConfigLongLong(_ix,_n) \
  REC_EstablishStaticConfigInteger(_ix,_n); \
  REC_RegisterConfigUpdateFunc(_n, http_config_cb, NULL)

#define HttpEstablishStaticConfigFloat(_ix,_n) \
  REC_EstablishStaticConfigFloat(_ix,_n); \
  REC_RegisterConfigUpdateFunc(_n, http_config_cb, NULL)


RecRawStatBlock *http_rsb;
#define HTTP_CLEAR_DYN_STAT(x) \
do { \
	RecSetRawStatSum(http_rsb, x, 0); \
	RecSetRawStatCount(http_rsb, x, 0); \
} while (0);



class HttpConfigCont:public Continuation
{
public:
  HttpConfigCont();
  int handle_event(int event, void *edata);
};


////////////////////////////////////////////////////////////////
//
//  static variables
//
////////////////////////////////////////////////////////////////
int HttpConfig::m_id = 0;
HttpConfigParams HttpConfig::m_master;
HttpUserAgent_RegxEntry *HttpConfig::user_agent_list = NULL;

static volatile int http_config_changes = 1;
static HttpConfigCont *http_config_cont = NULL;


HttpConfigCont::HttpConfigCont()
  : Continuation(new_ProxyMutex())
{
  SET_HANDLER(&HttpConfigCont::handle_event);
}

int
HttpConfigCont::handle_event(int event, void *edata)
{
  NOWARN_UNUSED(event);
  NOWARN_UNUSED(edata);
  if (ink_atomic_increment((int *) &http_config_changes, -1) == 1) {
    HttpConfig::reconfigure();
  }
  return 0;
}


static int
http_config_cb(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  NOWARN_UNUSED(name);
  NOWARN_UNUSED(data_type);
  NOWARN_UNUSED(data);
  NOWARN_UNUSED(cookie);
  ink_atomic_increment((int *) &http_config_changes, 1);

  INK_MEMORY_BARRIER;

  eventProcessor.schedule_in(http_config_cont, HRTIME_SECONDS(1), ET_CALL);
  return 0;
}


void
register_configs()
{
}

void
register_stat_callbacks()
{

  // Dynamic stats

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.background_fill_current_count",
                     RECD_INT, RECP_NON_PERSISTENT, (int) http_background_fill_current_count_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_background_fill_current_count_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.current_client_connections",
                     RECD_INT, RECP_NON_PERSISTENT, (int) http_current_client_connections_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_client_connections_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.current_active_client_connections",
                     RECD_INT, RECP_NON_PERSISTENT,
                     (int) http_current_active_client_connections_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_active_client_connections_stat);
  // Current Transaction Stats
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.current_client_transactions",
                     RECD_INT, RECP_NON_PERSISTENT, (int) http_current_client_transactions_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_client_transactions_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.current_parent_proxy_transactions",
                     RECD_INT, RECP_NON_PERSISTENT,
                     (int) http_current_parent_proxy_transactions_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_parent_proxy_transactions_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.current_icp_transactions",
                     RECD_INT, RECP_NON_PERSISTENT, (int) http_current_icp_transactions_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_icp_transactions_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.current_server_transactions",
                     RECD_INT, RECP_NON_PERSISTENT, (int) http_current_server_transactions_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_server_transactions_stat);
  // Current Transaction (Raw) Stats
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.current_parent_proxy_raw_transactions",
                     RECD_INT, RECP_NON_PERSISTENT,
                     (int) http_current_parent_proxy_raw_transactions_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_parent_proxy_raw_transactions_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.current_icp_raw_transactions",
                     RECD_INT, RECP_NON_PERSISTENT, (int) http_current_icp_raw_transactions_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_icp_raw_transactions_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.current_server_raw_transactions",
                     RECD_INT, RECP_NON_PERSISTENT, (int) http_current_server_raw_transactions_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_server_raw_transactions_stat);
  // Total connections stats

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.completed_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_completed_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.total_incoming_connections",
                     RECD_COUNTER, RECP_NULL, (int) http_total_incoming_connections_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.total_client_connections",
                     RECD_COUNTER, RECP_NULL, (int) http_total_client_connections_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.total_client_connections_ipv4",
                     RECD_COUNTER, RECP_NULL, (int) http_total_client_connections_ipv4_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.total_client_connections_ipv6",
                     RECD_COUNTER, RECP_NULL, (int) http_total_client_connections_ipv6_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.total_server_connections",
                     RECD_COUNTER, RECP_NULL, (int) http_total_server_connections_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.total_parent_proxy_connections",
                     RECD_COUNTER, RECP_NULL, (int) http_total_parent_proxy_connections_stat, RecRawStatSyncCount);

  // Upstream current connections stats
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.current_parent_proxy_connections",
                     RECD_INT, RECP_NON_PERSISTENT,
                     (int) http_current_parent_proxy_connections_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_parent_proxy_connections_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.current_server_connections",
                     RECD_INT, RECP_NON_PERSISTENT, (int) http_current_server_connections_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_server_connections_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.current_cache_connections",
                     RECD_INT, RECP_NON_PERSISTENT, (int) http_current_cache_connections_stat, RecRawStatSyncSum);
  HTTP_CLEAR_DYN_STAT(http_current_cache_connections_stat);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.avg_transactions_per_client_connection",
                     RECD_FLOAT, RECP_NULL, (int) http_transactions_per_client_con, RecRawStatSyncAvg);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.avg_transactions_per_server_connection",
                     RECD_FLOAT, RECP_NULL, (int) http_transactions_per_server_con, RecRawStatSyncAvg);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.avg_transactions_per_parent_connection",
                     RECD_FLOAT, RECP_NULL, (int) http_transactions_per_parent_con, RecRawStatSyncAvg);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.client_connection_time",
                     RECD_INT, RECP_NULL, (int) http_client_connection_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.parent_proxy_connection_time",
                     RECD_INT, RECP_NULL, (int) http_parent_proxy_connection_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.server_connection_time",
                     RECD_INT, RECP_NULL, (int) http_server_connection_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_connection_time",
                     RECD_INT, RECP_NULL, (int) http_cache_connection_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_counts.errors.pre_accept_hangups",
                     RECD_COUNTER, RECP_NULL,
                     (int) http_ua_msecs_counts_errors_pre_accept_hangups_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_totaltime.errors.pre_accept_hangups",
                     RECD_FLOAT, RECP_NULL,
                     (int) http_ua_msecs_counts_errors_pre_accept_hangups_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_counts.errors.empty_hangups",
                     RECD_COUNTER, RECP_NULL,
                     (int) http_ua_msecs_counts_errors_empty_hangups_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_totaltime.errors.empty_hangups",
                     RECD_FLOAT, RECP_NULL, (int) http_ua_msecs_counts_errors_empty_hangups_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_counts.errors.early_hangups",
                     RECD_COUNTER, RECP_NULL,
                     (int) http_ua_msecs_counts_errors_early_hangups_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_totaltime.errors.early_hangups",
                     RECD_FLOAT, RECP_NULL, (int) http_ua_msecs_counts_errors_early_hangups_stat, RecRawStatSyncCount);



  // Transactional stats

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.incoming_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_incoming_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.outgoing_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_outgoing_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.incoming_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_incoming_responses_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.invalid_client_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_invalid_client_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.missing_host_hdr",
                     RECD_COUNTER, RECP_NULL, (int) http_missing_host_hdr_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.get_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_get_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.head_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_head_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.trace_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_trace_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.options_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_options_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.post_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_post_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.put_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_put_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.push_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_push_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.delete_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_delete_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.purge_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_purge_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.connect_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_connect_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.extension_method_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_extension_method_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.client_no_cache_requests",
                     RECD_COUNTER, RECP_NULL, (int) http_client_no_cache_requests_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.broken_server_connections",
                     RECD_COUNTER, RECP_NULL, (int) http_broken_server_connections_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_lookups",
                     RECD_COUNTER, RECP_NULL, (int) http_cache_lookups_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_writes",
                     RECD_COUNTER, RECP_NULL, (int) http_cache_writes_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_updates",
                     RECD_COUNTER, RECP_NULL, (int) http_cache_updates_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_deletes",
                     RECD_COUNTER, RECP_NULL, (int) http_cache_deletes_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tunnels",
                     RECD_COUNTER, RECP_NULL, (int) http_tunnels_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.throttled_proxy_only",
                     RECD_COUNTER, RECP_NULL, (int) http_throttled_proxy_only_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.request_taxonomy.i0_n0_m0",
                     RECD_COUNTER, RECP_NULL, (int) http_request_taxonomy_i0_n0_m0_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.request_taxonomy.i1_n0_m0",
                     RECD_COUNTER, RECP_NULL, (int) http_request_taxonomy_i1_n0_m0_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.request_taxonomy.i0_n1_m0",
                     RECD_COUNTER, RECP_NULL, (int) http_request_taxonomy_i0_n1_m0_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.request_taxonomy.i1_n1_m0",
                     RECD_COUNTER, RECP_NULL, (int) http_request_taxonomy_i1_n1_m0_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.request_taxonomy.i0_n0_m1",
                     RECD_COUNTER, RECP_NULL, (int) http_request_taxonomy_i0_n0_m1_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.request_taxonomy.i1_n0_m1",
                     RECD_COUNTER, RECP_NULL, (int) http_request_taxonomy_i1_n0_m1_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.request_taxonomy.i0_n1_m1",
                     RECD_COUNTER, RECP_NULL, (int) http_request_taxonomy_i0_n1_m1_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.request_taxonomy.i1_n1_m1",
                     RECD_COUNTER, RECP_NULL, (int) http_request_taxonomy_i1_n1_m1_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.icp_suggested_lookups",
                     RECD_COUNTER, RECP_NULL, (int) http_icp_suggested_lookups_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.client_transaction_time",
                     RECD_INT, RECP_NULL, (int) http_client_transaction_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.client_write_time",
                     RECD_INT, RECP_NULL, (int) http_client_write_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.server_read_time",
                     RECD_INT, RECP_NULL, (int) http_server_read_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.icp_transaction_time",
                     RECD_INT, RECP_NULL, (int) http_icp_transaction_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.icp_raw_transaction_time",
                     RECD_INT, RECP_NULL, (int) http_icp_raw_transaction_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.parent_proxy_transaction_time",
                     RECD_INT, RECP_NULL, (int) http_parent_proxy_transaction_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.parent_proxy_raw_transaction_time",
                     RECD_INT, RECP_NULL, (int) http_parent_proxy_raw_transaction_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.server_transaction_time",
                     RECD_INT, RECP_NULL, (int) http_server_transaction_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.server_raw_transaction_time",
                     RECD_INT, RECP_NULL, (int) http_server_raw_transaction_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.user_agent_request_header_total_size",
                     RECD_INT, RECP_NULL, (int) http_user_agent_request_header_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.user_agent_response_header_total_size",
                     RECD_INT, RECP_NULL, (int) http_user_agent_response_header_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.user_agent_request_document_total_size",
                     RECD_INT, RECP_NULL, (int) http_user_agent_request_document_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.user_agent_response_document_total_size",
                     RECD_INT, RECP_NULL, (int) http_user_agent_response_document_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.origin_server_request_header_total_size",
                     RECD_INT, RECP_NULL, (int) http_origin_server_request_header_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.origin_server_response_header_total_size",
                     RECD_INT, RECP_NULL, (int) http_origin_server_response_header_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.origin_server_request_document_total_size",
                     RECD_INT, RECP_NULL, (int) http_origin_server_request_document_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.origin_server_response_document_total_size",
                     RECD_INT, RECP_NULL,
                     (int) http_origin_server_response_document_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.parent_proxy_request_total_bytes",
                     RECD_INT, RECP_NULL, (int) http_parent_proxy_request_total_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.parent_proxy_response_total_bytes",
                     RECD_INT, RECP_NULL, (int) http_parent_proxy_response_total_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.pushed_response_header_total_size",
                     RECD_INT, RECP_NULL, (int) http_pushed_response_header_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.pushed_document_total_size",
                     RECD_INT, RECP_NULL, (int) http_pushed_document_total_size_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.response_document_size_100",
                     RECD_COUNTER, RECP_NULL, (int) http_response_document_size_100_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.response_document_size_1K",
                     RECD_COUNTER, RECP_NULL, (int) http_response_document_size_1K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.response_document_size_3K",
                     RECD_COUNTER, RECP_NULL, (int) http_response_document_size_3K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.response_document_size_5K",
                     RECD_COUNTER, RECP_NULL, (int) http_response_document_size_5K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.response_document_size_10K",
                     RECD_COUNTER, RECP_NULL, (int) http_response_document_size_10K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.response_document_size_1M",
                     RECD_COUNTER, RECP_NULL, (int) http_response_document_size_1M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.response_document_size_inf",
                     RECD_COUNTER, RECP_NULL, (int) http_response_document_size_inf_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.request_document_size_100",
                     RECD_COUNTER, RECP_NULL, (int) http_request_document_size_100_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.request_document_size_1K",
                     RECD_COUNTER, RECP_NULL, (int) http_request_document_size_1K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.request_document_size_3K",
                     RECD_COUNTER, RECP_NULL, (int) http_request_document_size_3K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.request_document_size_5K",
                     RECD_COUNTER, RECP_NULL, (int) http_request_document_size_5K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.request_document_size_10K",
                     RECD_COUNTER, RECP_NULL, (int) http_request_document_size_10K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.request_document_size_1M",
                     RECD_COUNTER, RECP_NULL, (int) http_request_document_size_1M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.request_document_size_inf",
                     RECD_COUNTER, RECP_NULL, (int) http_request_document_size_inf_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.user_agent_speed_bytes_per_sec_100",
                     RECD_COUNTER, RECP_NULL, (int) http_user_agent_speed_bytes_per_sec_100_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.user_agent_speed_bytes_per_sec_1K",
                     RECD_COUNTER, RECP_NULL, (int) http_user_agent_speed_bytes_per_sec_1K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.user_agent_speed_bytes_per_sec_10K",
                     RECD_COUNTER, RECP_NULL, (int) http_user_agent_speed_bytes_per_sec_10K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.user_agent_speed_bytes_per_sec_100K",
                     RECD_COUNTER, RECP_NULL, (int) http_user_agent_speed_bytes_per_sec_100K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.user_agent_speed_bytes_per_sec_1M",
                     RECD_COUNTER, RECP_NULL, (int) http_user_agent_speed_bytes_per_sec_1M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.user_agent_speed_bytes_per_sec_10M",
                     RECD_COUNTER, RECP_NULL, (int) http_user_agent_speed_bytes_per_sec_10M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.user_agent_speed_bytes_per_sec_100M",
                     RECD_COUNTER, RECP_NULL, (int) http_user_agent_speed_bytes_per_sec_100M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.origin_server_speed_bytes_per_sec_100",
                     RECD_COUNTER, RECP_NULL,
                     (int) http_origin_server_speed_bytes_per_sec_100_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.origin_server_speed_bytes_per_sec_1K",
                     RECD_COUNTER, RECP_NULL,
                     (int) http_origin_server_speed_bytes_per_sec_1K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.origin_server_speed_bytes_per_sec_10K",
                     RECD_COUNTER, RECP_NULL,
                     (int) http_origin_server_speed_bytes_per_sec_10K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.origin_server_speed_bytes_per_sec_100K",
                     RECD_COUNTER, RECP_NULL,
                     (int) http_origin_server_speed_bytes_per_sec_100K_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.origin_server_speed_bytes_per_sec_1M",
                     RECD_COUNTER, RECP_NULL,
                     (int) http_origin_server_speed_bytes_per_sec_1M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.origin_server_speed_bytes_per_sec_10M",
                     RECD_COUNTER, RECP_NULL,
                     (int) http_origin_server_speed_bytes_per_sec_10M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.origin_server_speed_bytes_per_sec_100M",
                     RECD_COUNTER, RECP_NULL,
                     (int) http_origin_server_speed_bytes_per_sec_100M_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.total_transactions_time",
                     RECD_INT, RECP_NULL, (int) http_total_transactions_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.total_transactions_think_time",
                     RECD_INT, RECP_NULL, (int) http_total_transactions_think_time_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_hit_fresh",
                     RECD_COUNTER, RECP_NULL, (int) http_cache_hit_fresh_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_hit_revalidated",
                     RECD_COUNTER, RECP_NULL, (int) http_cache_hit_reval_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_hit_ims",
                     RECD_COUNTER, RECP_NULL, (int) http_cache_hit_ims_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_hit_stale_served",
                     RECD_COUNTER, RECP_NULL, (int) http_cache_hit_stale_served_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_miss_cold",
                     RECD_COUNTER, RECP_NULL, (int) http_cache_miss_cold_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_miss_changed",
                     RECD_COUNTER, RECP_NULL, (int) http_cache_miss_changed_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_miss_client_no_cache",
                     RECD_COUNTER, RECP_NULL, (int) http_cache_miss_client_no_cache_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_miss_client_not_cacheable",
                     RECD_COUNTER, RECP_NULL, (int) http_cache_miss_uncacheable_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_miss_ims",
                     RECD_COUNTER, RECP_NULL, (int) http_cache_miss_ims_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_read_error",
                     RECD_COUNTER, RECP_NULL, (int) http_cache_read_error_stat, RecRawStatSyncCount);

  /////////////////////////////////////////
  // Bandwidth Savings Transaction Stats //
  /////////////////////////////////////////

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_hit_count_stat",
                     RECD_COUNTER, RECP_NULL, (int) http_tcp_hit_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_hit_user_agent_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_hit_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_hit_origin_server_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_hit_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_miss_count_stat",
                     RECD_COUNTER, RECP_NULL, (int) http_tcp_miss_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_miss_user_agent_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_miss_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_miss_origin_server_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_miss_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_expired_miss_count_stat",
                     RECD_COUNTER, RECP_NULL, (int) http_tcp_expired_miss_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_expired_miss_user_agent_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_expired_miss_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_expired_miss_origin_server_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_expired_miss_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_refresh_hit_count_stat",
                     RECD_COUNTER, RECP_NULL, (int) http_tcp_refresh_hit_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_refresh_hit_user_agent_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_refresh_hit_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_refresh_hit_origin_server_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_refresh_hit_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_refresh_miss_count_stat",
                     RECD_COUNTER, RECP_NULL, (int) http_tcp_refresh_miss_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_refresh_miss_user_agent_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_refresh_miss_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_refresh_miss_origin_server_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_refresh_miss_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_client_refresh_count_stat",
                     RECD_COUNTER, RECP_NULL, (int) http_tcp_client_refresh_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_client_refresh_user_agent_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_client_refresh_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_client_refresh_origin_server_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_client_refresh_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_ims_hit_count_stat",
                     RECD_COUNTER, RECP_NULL, (int) http_tcp_ims_hit_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_ims_hit_user_agent_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_ims_hit_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_ims_hit_origin_server_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_ims_hit_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_ims_miss_count_stat",
                     RECD_COUNTER, RECP_NULL, (int) http_tcp_ims_miss_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_ims_miss_user_agent_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_ims_miss_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.tcp_ims_miss_origin_server_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_tcp_ims_miss_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.err_client_abort_count_stat",
                     RECD_COUNTER, RECP_NULL, (int) http_err_client_abort_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.err_client_abort_user_agent_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_err_client_abort_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.err_client_abort_origin_server_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_err_client_abort_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.err_connect_fail_count_stat",
                     RECD_COUNTER, RECP_NULL, (int) http_err_connect_fail_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.err_connect_fail_user_agent_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_err_connect_fail_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.err_connect_fail_origin_server_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_err_connect_fail_origin_server_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.misc_count_stat",
                     RECD_COUNTER, RECP_NULL, (int) http_misc_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.misc_user_agent_bytes_stat",
                     RECD_INT, RECP_NULL, (int) http_misc_user_agent_bytes_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.background_fill_bytes_aborted_stat",
                     RECD_INT, RECP_NULL, (int) http_background_fill_bytes_aborted_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.background_fill_bytes_completed_stat",
                     RECD_INT, RECP_NULL, (int) http_background_fill_bytes_completed_stat, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_write_errors",
                     RECD_INT, RECP_NULL, (int) http_cache_write_errors, RecRawStatSyncSum);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.cache_read_errors",
                     RECD_INT, RECP_NULL, (int) http_cache_read_errors, RecRawStatSyncSum);

  ////////////////////////////////////////////////////////////////////////////////
  // status code counts
  ////////////////////////////////////////////////////////////////////////////////

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.100_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_100_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.101_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_101_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.1xx_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_1xx_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.200_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_200_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.201_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_201_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.202_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_202_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.203_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_203_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.204_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_204_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.205_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_205_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.206_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_206_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.2xx_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_2xx_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.300_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_300_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.301_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_301_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.302_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_302_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.303_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_303_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.304_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_304_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.305_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_305_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.307_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_307_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.3xx_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_3xx_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.400_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_400_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.401_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_401_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.402_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_402_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.403_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_403_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.404_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_404_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.405_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_405_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.406_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_406_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.407_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_407_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.408_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_408_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.409_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_409_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.410_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_410_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.411_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_411_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.412_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_412_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.413_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_413_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.414_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_414_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.415_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_415_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.416_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_416_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.4xx_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_4xx_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.500_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_500_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.501_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_501_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.502_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_502_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.503_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_503_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.504_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_504_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.505_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_505_count_stat, RecRawStatSyncCount);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.5xx_responses",
                     RECD_COUNTER, RECP_NULL, (int) http_response_status_5xx_count_stat, RecRawStatSyncCount);


  ////////////////////////////////////////////////////////////////////////////////
  // http - time and count of transactions classified by client's point of view //
  //  the internal stat is in msecs, the output time is float seconds           //
  ////////////////////////////////////////////////////////////////////////////////

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_counts.hit_fresh",
                     RECD_COUNTER, RECP_NULL, (int) http_ua_msecs_counts_hit_fresh_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_totaltime.hit_fresh",
                     RECD_FLOAT, RECP_NULL,
                     (int) http_ua_msecs_counts_hit_fresh_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_counts.hit_fresh.process",
                     RECD_COUNTER, RECP_NULL, (int) http_ua_msecs_counts_hit_fresh_process_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_totaltime.hit_fresh.process",
                     RECD_FLOAT, RECP_NULL,
                     (int) http_ua_msecs_counts_hit_fresh_process_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_counts.hit_revalidated",
                     RECD_COUNTER, RECP_NULL, (int) http_ua_msecs_counts_hit_reval_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_totaltime.hit_revalidated",
                     RECD_FLOAT, RECP_NULL,
                     (int) http_ua_msecs_counts_hit_reval_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_counts.miss_cold",
                     RECD_COUNTER, RECP_NULL, (int) http_ua_msecs_counts_miss_cold_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_totaltime.miss_cold",
                     RECD_FLOAT, RECP_NULL,
                     (int) http_ua_msecs_counts_miss_cold_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_counts.miss_not_cacheable",
                     RECD_COUNTER, RECP_NULL, (int) http_ua_msecs_counts_miss_uncacheable_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_totaltime.miss_not_cacheable",
                     RECD_FLOAT, RECP_NULL,
                     (int) http_ua_msecs_counts_miss_uncacheable_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_counts.miss_changed",
                     RECD_COUNTER, RECP_NULL, (int) http_ua_msecs_counts_miss_changed_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_totaltime.miss_changed",
                     RECD_FLOAT, RECP_NULL,
                     (int) http_ua_msecs_counts_miss_changed_stat, RecRawStatSyncIntMsecsToFloatSeconds);


  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_counts.miss_client_no_cache",
                     RECD_COUNTER, RECP_NULL,
                     (int) http_ua_msecs_counts_miss_client_no_cache_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_totaltime.miss_client_no_cache",
                     RECD_FLOAT, RECP_NULL,
                     (int) http_ua_msecs_counts_miss_client_no_cache_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_counts.errors.aborts",
                     RECD_COUNTER, RECP_NULL, (int) http_ua_msecs_counts_errors_aborts_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_totaltime.errors.aborts",
                     RECD_FLOAT, RECP_NULL,
                     (int) http_ua_msecs_counts_errors_aborts_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_counts.errors.possible_aborts",
                     RECD_COUNTER, RECP_NULL,
                     (int) http_ua_msecs_counts_errors_possible_aborts_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_totaltime.errors.possible_aborts",
                     RECD_FLOAT, RECP_NULL,
                     (int) http_ua_msecs_counts_errors_possible_aborts_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_counts.errors.connect_failed",
                     RECD_COUNTER, RECP_NULL,
                     (int) http_ua_msecs_counts_errors_connect_failed_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_totaltime.errors.connect_failed",
                     RECD_FLOAT, RECP_NULL,
                     (int) http_ua_msecs_counts_errors_connect_failed_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_counts.errors.other",
                     RECD_COUNTER, RECP_NULL, (int) http_ua_msecs_counts_errors_other_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_totaltime.errors.other",
                     RECD_FLOAT, RECP_NULL,
                     (int) http_ua_msecs_counts_errors_other_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_counts.other.unclassified",
                     RECD_COUNTER, RECP_NULL, (int) http_ua_msecs_counts_other_unclassified_stat, RecRawStatSyncCount);
  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.transaction_totaltime.other.unclassified",
                     RECD_FLOAT, RECP_NULL,
                     (int) http_ua_msecs_counts_other_unclassified_stat, RecRawStatSyncIntMsecsToFloatSeconds);

  RecRegisterRawStat(http_rsb, RECT_PROCESS,
                     "proxy.process.http.total_x_redirect_count",
                     RECD_COUNTER, RECP_NULL,
                     (int) http_total_x_redirect_stat, RecRawStatSyncCount);

}


////////////////////////////////////////////////////////////////
//
//  HttpConfig::startup()
//
////////////////////////////////////////////////////////////////
void
HttpConfig::startup()
{

  http_rsb = RecAllocateRawStatBlock((int) http_stat_count);
  register_configs();
  register_stat_callbacks();

  HttpConfigParams &c = m_master;

  http_config_cont = NEW(new HttpConfigCont);

  HttpEstablishStaticConfigStringAlloc(c.proxy_hostname, "proxy.config.proxy_name");
  c.proxy_hostname_len = -1;

  if (c.proxy_hostname == NULL) {
    c.proxy_hostname = (char *) xmalloc(sizeof(char));
    c.proxy_hostname[0] = '\0';
  }

  RecGetRecordString_Xmalloc("proxy.local.incoming_ip_to_bind", &(c.incoming_ip_to_bind));

  if (c.incoming_ip_to_bind) {
    Debug("ip_binding", "incoming_ip_to_bind: %s", c.incoming_ip_to_bind);
    c.incoming_ip_to_bind_saddr = inet_addr(c.incoming_ip_to_bind);
  }

  RecGetRecordString_Xmalloc("proxy.local.outgoing_ip_to_bind", &(c.outgoing_ip_to_bind));

  if (c.outgoing_ip_to_bind) {
    Debug("ip_binding", "outgoing_ip_to_bind: %s", c.outgoing_ip_to_bind);
    c.outgoing_ip_to_bind_saddr = inet_addr(c.outgoing_ip_to_bind);
  }

  HttpEstablishStaticConfigLongLong(c.server_max_connections, "proxy.config.http.server_max_connections");

  HttpEstablishStaticConfigLongLong(c.oride.origin_max_connections, "proxy.config.http.origin_max_connections");

  HttpEstablishStaticConfigLongLong(c.origin_min_keep_alive_connections, "proxy.config.http.origin_min_keep_alive_connections");

  HttpEstablishStaticConfigLongLong(c.parent_proxy_routing_enable, "proxy.config.http.parent_proxy_routing_enable");

  // Wank me.
  REC_ReadConfigInteger(c.disable_ssl_parenting, "proxy.local.http.parent_proxy.disable_connect_tunneling");
  HttpEstablishStaticConfigLongLong(c.no_dns_forward_to_parent, "proxy.config.http.no_dns_just_forward_to_parent");
  HttpEstablishStaticConfigLongLong(c.uncacheable_requests_bypass_parent,
                                    "proxy.config.http.uncacheable_requests_bypass_parent");
  HttpEstablishStaticConfigLongLong(c.no_origin_server_dns, "proxy.config.http.no_origin_server_dns");
  HttpEstablishStaticConfigLongLong(c.use_client_target_addr, "proxy.config.http.use_client_target_addr");
  HttpEstablishStaticConfigLongLong(c.oride.maintain_pristine_host_hdr, "proxy.config.url_remap.pristine_host_hdr");

  HttpEstablishStaticConfigLongLong(c.snarf_username_from_authorization,
                                    "proxy.config.http.snarf_username_from_authorization");

  HttpEstablishStaticConfigLongLong(c.enable_url_expandomatic, "proxy.config.http.enable_url_expandomatic");

  HttpEstablishStaticConfigLongLong(c.oride.insert_request_via_string, "proxy.config.http.insert_request_via_str");
  HttpEstablishStaticConfigLongLong(c.oride.insert_response_via_string, "proxy.config.http.insert_response_via_str");
  HttpEstablishStaticConfigLongLong(c.verbose_via_string, "proxy.config.http.verbose_via_str");

  HttpEstablishStaticConfigStringAlloc(c.proxy_request_via_string, "proxy.config.http.request_via_str");
  c.proxy_request_via_string_len = -1;
  HttpEstablishStaticConfigStringAlloc(c.proxy_response_via_string, "proxy.config.http.response_via_str");
  c.proxy_response_via_string_len = -1;

  HttpEstablishStaticConfigLongLong(c.wuts_enabled, "proxy.config.http.wuts_enabled");
  HttpEstablishStaticConfigLongLong(c.log_spider_codes, "proxy.config.http.log_spider_codes");

  HttpEstablishStaticConfigStringAlloc(c.url_expansions_string, "proxy.config.dns.url_expansions");
  HttpEstablishStaticConfigLongLong(c.proxy_server_port, "proxy.config.http.server_port");
  HttpEstablishStaticConfigStringAlloc(c.proxy_server_other_ports, "proxy.config.http.server_other_ports");
  HttpEstablishStaticConfigLongLong(c.oride.keep_alive_enabled, "proxy.config.http.keep_alive_enabled");
  HttpEstablishStaticConfigLongLong(c.oride.chunking_enabled, "proxy.config.http.chunking_enabled");
  HttpEstablishStaticConfigLongLong(c.session_auth_cache_keep_alive_enabled,
                                   "proxy.config.http.session_auth_cache_keep_alive_enabled");
  HttpEstablishStaticConfigLongLong(c.origin_server_pipeline, "proxy.config.http.origin_server_pipeline");
  HttpEstablishStaticConfigLongLong(c.user_agent_pipeline, "proxy.config.http.user_agent_pipeline");
  HttpEstablishStaticConfigLongLong(c.share_server_sessions, "proxy.config.http.share_server_sessions");
  HttpEstablishStaticConfigLongLong(c.oride.keep_alive_post_out, "proxy.config.http.keep_alive_post_out");

  HttpEstablishStaticConfigLongLong(c.oride.keep_alive_no_activity_timeout_in,
                                    "proxy.config.http.keep_alive_no_activity_timeout_in");
  HttpEstablishStaticConfigLongLong(c.keep_alive_no_activity_timeout_out,
                                    "proxy.config.http.keep_alive_no_activity_timeout_out");
  HttpEstablishStaticConfigLongLong(c.oride.transaction_no_activity_timeout_in,
                                    "proxy.config.http.transaction_no_activity_timeout_in");
  HttpEstablishStaticConfigLongLong(c.oride.transaction_no_activity_timeout_out,
                                    "proxy.config.http.transaction_no_activity_timeout_out");
  HttpEstablishStaticConfigLongLong(c.transaction_active_timeout_in, "proxy.config.http.transaction_active_timeout_in");
  HttpEstablishStaticConfigLongLong(c.oride.transaction_active_timeout_out,
                                    "proxy.config.http.transaction_active_timeout_out");
  HttpEstablishStaticConfigLongLong(c.accept_no_activity_timeout, "proxy.config.http.accept_no_activity_timeout");

  HttpEstablishStaticConfigLongLong(c.background_fill_active_timeout,
                                    "proxy.config.http.background_fill_active_timeout");
  HttpEstablishStaticConfigFloat(c.background_fill_threshold, "proxy.config.http.background_fill_completed_threshold");

  HttpEstablishStaticConfigLongLong(c.oride.connect_attempts_max_retries, "proxy.config.http.connect_attempts_max_retries");
  HttpEstablishStaticConfigLongLong(c.oride.connect_attempts_max_retries_dead_server,
                                    "proxy.config.http.connect_attempts_max_retries_dead_server");

  HttpEstablishStaticConfigLongLong(c.oride.connect_attempts_rr_retries, "proxy.config.http.connect_attempts_rr_retries");
  HttpEstablishStaticConfigLongLong(c.oride.connect_attempts_timeout, "proxy.config.http.connect_attempts_timeout");
  HttpEstablishStaticConfigLongLong(c.oride.post_connect_attempts_timeout, "proxy.config.http.post_connect_attempts_timeout");
  HttpEstablishStaticConfigLongLong(c.parent_connect_attempts, "proxy.config.http.parent_proxy.total_connect_attempts");
  HttpEstablishStaticConfigLongLong(c.per_parent_connect_attempts,
                                    "proxy.config.http.parent_proxy.per_parent_connect_attempts");
  HttpEstablishStaticConfigLongLong(c.parent_connect_timeout,
                                    "proxy.config.http.parent_proxy.connect_attempts_timeout");

  HttpEstablishStaticConfigLongLong(c.oride.sock_recv_buffer_size_out, "proxy.config.net.sock_recv_buffer_size_out");
  HttpEstablishStaticConfigLongLong(c.oride.sock_send_buffer_size_out, "proxy.config.net.sock_send_buffer_size_out");
  HttpEstablishStaticConfigLongLong(c.oride.sock_option_flag_out, "proxy.config.net.sock_option_flag_out");

  c.fwd_proxy_auth_to_parent = 0;

  HttpEstablishStaticConfigLongLong(c.oride.anonymize_remove_from, "proxy.config.http.anonymize_remove_from");
  HttpEstablishStaticConfigLongLong(c.oride.anonymize_remove_referer, "proxy.config.http.anonymize_remove_referer");
  HttpEstablishStaticConfigLongLong(c.oride.anonymize_remove_user_agent, "proxy.config.http.anonymize_remove_user_agent");
  HttpEstablishStaticConfigLongLong(c.oride.anonymize_remove_cookie, "proxy.config.http.anonymize_remove_cookie");
  HttpEstablishStaticConfigLongLong(c.oride.anonymize_remove_client_ip, "proxy.config.http.anonymize_remove_client_ip");
  HttpEstablishStaticConfigLongLong(c.oride.anonymize_insert_client_ip, "proxy.config.http.anonymize_insert_client_ip");
  HttpEstablishStaticConfigLongLong(c.oride.append_xforwards_header, "proxy.config.http.append_xforwards_header");
  HttpEstablishStaticConfigStringAlloc(c.anonymize_other_header_list, "proxy.config.http.anonymize_other_header_list");
  HttpEstablishStaticConfigStringAlloc(c.global_user_agent_header, "proxy.config.http.global_user_agent_header");
  c.global_user_agent_header_size = c.global_user_agent_header ? strlen(c.global_user_agent_header) : 0;

  HttpEstablishStaticConfigLongLong(c.oride.proxy_response_server_enabled, "proxy.config.http.response_server_enabled");
  HttpEstablishStaticConfigStringAlloc(c.oride.proxy_response_server_string, "proxy.config.http.response_server_str");
  c.oride.proxy_response_server_string_len = c.oride.proxy_response_server_string ?
    strlen(c.oride.proxy_response_server_string) : 0;

  HttpEstablishStaticConfigLongLong(c.oride.insert_squid_x_forwarded_for, "proxy.config.http.insert_squid_x_forwarded_for");


  HttpEstablishStaticConfigLongLong(c.insert_age_in_response, "proxy.config.http.insert_age_in_response");

  HttpEstablishStaticConfigLongLong(c.avoid_content_spoofing, "proxy.config.http.avoid_content_spoofing");

  HttpEstablishStaticConfigLongLong(c.enable_http_stats, "proxy.config.http.enable_http_stats");

  HttpEstablishStaticConfigLongLong(c.normalize_ae_gzip, "proxy.config.http.normalize_ae_gzip");

  HttpEstablishStaticConfigLongLong(c.icp_enabled, "proxy.config.icp.enabled");
  HttpEstablishStaticConfigLongLong(c.stale_icp_enabled, "proxy.config.icp.stale_icp_enabled");

  HttpEstablishStaticConfigLongLong(c.oride.cache_heuristic_min_lifetime, "proxy.config.http.cache.heuristic_min_lifetime");
  HttpEstablishStaticConfigLongLong(c.oride.cache_heuristic_max_lifetime, "proxy.config.http.cache.heuristic_max_lifetime");
  HttpEstablishStaticConfigFloat(c.oride.cache_heuristic_lm_factor, "proxy.config.http.cache.heuristic_lm_factor");

  HttpEstablishStaticConfigLongLong(c.oride.cache_guaranteed_min_lifetime, "proxy.config.http.cache.guaranteed_min_lifetime");
  HttpEstablishStaticConfigLongLong(c.oride.cache_guaranteed_max_lifetime, "proxy.config.http.cache.guaranteed_max_lifetime");

  HttpEstablishStaticConfigLongLong(c.oride.cache_max_stale_age, "proxy.config.http.cache.max_stale_age");

  HttpEstablishStaticConfigLongLong(c.oride.freshness_fuzz_time, "proxy.config.http.cache.fuzz.time");
  HttpEstablishStaticConfigLongLong(c.oride.freshness_fuzz_min_time, "proxy.config.http.cache.fuzz.min_time");
  HttpEstablishStaticConfigFloat(c.oride.freshness_fuzz_prob, "proxy.config.http.cache.fuzz.probability");

  HttpEstablishStaticConfigStringAlloc(c.cache_vary_default_text, "proxy.config.http.cache.vary_default_text");
  HttpEstablishStaticConfigStringAlloc(c.cache_vary_default_images, "proxy.config.http.cache.vary_default_images");
  HttpEstablishStaticConfigStringAlloc(c.cache_vary_default_other, "proxy.config.http.cache.vary_default_other");

  // open read failure retries
  HttpEstablishStaticConfigLongLong(c.oride.max_cache_open_read_retries, "proxy.config.http.cache.max_open_read_retries");
  HttpEstablishStaticConfigLongLong(c.oride.cache_open_read_retry_time, "proxy.config.http.cache.open_read_retry_time");

  // open write failure retries
  HttpEstablishStaticConfigLongLong(c.max_cache_open_write_retries, "proxy.config.http.cache.max_open_write_retries");

  HttpEstablishStaticConfigLongLong(c.oride.cache_http, "proxy.config.http.cache.http");
  HttpEstablishStaticConfigLongLong(c.oride.cache_ignore_client_no_cache, "proxy.config.http.cache.ignore_client_no_cache");
  HttpEstablishStaticConfigLongLong(c.oride.cache_ignore_client_cc_max_age,
                                    "proxy.config.http.cache.ignore_client_cc_max_age");
  HttpEstablishStaticConfigLongLong(c.oride.cache_ims_on_client_no_cache, "proxy.config.http.cache.ims_on_client_no_cache");
  HttpEstablishStaticConfigLongLong(c.oride.cache_ignore_server_no_cache, "proxy.config.http.cache.ignore_server_no_cache");
  HttpEstablishStaticConfigLongLong(c.oride.cache_responses_to_cookies, "proxy.config.http.cache.cache_responses_to_cookies");

  HttpEstablishStaticConfigLongLong(c.oride.cache_ignore_auth, "proxy.config.http.cache.ignore_authentication");
  HttpEstablishStaticConfigLongLong(c.oride.cache_urls_that_look_dynamic,
                                    "proxy.config.http.cache.cache_urls_that_look_dynamic");
  HttpEstablishStaticConfigLongLong(c.cache_enable_default_vary_headers,
                                    "proxy.config.http.cache.enable_default_vary_headers");

  HttpEstablishStaticConfigLongLong(c.ignore_accept_mismatch, "proxy.config.http.cache.ignore_accept_mismatch");
  HttpEstablishStaticConfigLongLong(c.ignore_accept_language_mismatch,
                                    "proxy.config.http.cache.ignore_accept_language_mismatch");
  HttpEstablishStaticConfigLongLong(c.ignore_accept_encoding_mismatch,
                                    "proxy.config.http.cache.ignore_accept_encoding_mismatch");
  HttpEstablishStaticConfigLongLong(c.ignore_accept_charset_mismatch,
                                    "proxy.config.http.cache.ignore_accept_charset_mismatch");

  HttpEstablishStaticConfigLongLong(c.oride.cache_when_to_revalidate, "proxy.config.http.cache.when_to_revalidate");
  HttpEstablishStaticConfigLongLong(c.cache_when_to_add_no_cache_to_msie_requests,
                                    "proxy.config.http.cache.when_to_add_no_cache_to_msie_requests");
  HttpEstablishStaticConfigLongLong(c.oride.cache_required_headers, "proxy.config.http.cache.required_headers");
  HttpEstablishStaticConfigLongLong(c.cache_range_lookup, "proxy.config.http.cache.range.lookup");

  HttpEstablishStaticConfigStringAlloc(c.connect_ports_string, "proxy.config.http.connect_ports");

  HttpEstablishStaticConfigLongLong(c.request_hdr_max_size, "proxy.config.http.request_header_max_size");

  HttpEstablishStaticConfigLongLong(c.response_hdr_max_size, "proxy.config.http.response_header_max_size");

  HttpEstablishStaticConfigLongLong(c.push_method_enabled, "proxy.config.http.push_method_enabled");

  HttpEstablishStaticConfigLongLong(c.reverse_proxy_enabled, "proxy.config.reverse_proxy.enabled");
  HttpEstablishStaticConfigLongLong(c.url_remap_required, "proxy.config.url_remap.remap_required");

  HttpEstablishStaticConfigStringAlloc(c.reverse_proxy_no_host_redirect,
                                       "proxy.config.header.parse.no_host_url_redirect");
  c.reverse_proxy_no_host_redirect_len = -1;

  HttpEstablishStaticConfigLongLong(c.errors_log_error_pages, "proxy.config.http.errors.log_error_pages");

  HttpEstablishStaticConfigLongLong(c.slow_log_threshold, "proxy.config.http.slow.log.threshold");

  HttpEstablishStaticConfigLongLong(c.record_cop_page, "proxy.config.http.record_heartbeat");

  HttpEstablishStaticConfigLongLong(c.record_tcp_mem_hit, "proxy.config.http.record_tcp_mem_hit");

  HttpEstablishStaticConfigLongLong(c.oride.send_http11_requests, "proxy.config.http.send_http11_requests");

  // HTTP Referer Filtering
  HttpEstablishStaticConfigLongLong(c.referer_filter_enabled, "proxy.config.http.referer_filter");
  HttpEstablishStaticConfigLongLong(c.referer_format_redirect, "proxy.config.http.referer_format_redirect");

  // HTTP Accept_Encoding filtering (depends on User-Agent)
  HttpEstablishStaticConfigLongLong(c.accept_encoding_filter_enabled, "proxy.config.http.accept_encoding_filter_enabled");

  // HTTP Quick filter
  HttpEstablishStaticConfigLongLong(c.quick_filter_mask, "proxy.config.http.quick_filter.mask");

  // Negative caching
  HttpEstablishStaticConfigLongLong(c.oride.down_server_timeout, "proxy.config.http.down_server.cache_time");
  HttpEstablishStaticConfigLongLong(c.oride.client_abort_threshold, "proxy.config.http.down_server.abort_threshold");

  // Negative revalidating
  HttpEstablishStaticConfigLongLong(c.negative_revalidating_enabled, "proxy.config.http.negative_revalidating_enabled");
  HttpEstablishStaticConfigLongLong(c.negative_revalidating_lifetime, "proxy.config.http.negative_revalidating_lifetime");

  // Negative response caching
  HttpEstablishStaticConfigLongLong(c.oride.negative_caching_enabled, "proxy.config.http.negative_caching_enabled");
  HttpEstablishStaticConfigLongLong(c.oride.negative_caching_lifetime, "proxy.config.http.negative_caching_lifetime");

  // Buffer size
  HttpEstablishStaticConfigLongLong(c.default_buffer_size_index, "proxy.config.http.default_buffer_size");

  // Buffer water mark
  HttpEstablishStaticConfigLongLong(c.default_buffer_water_mark, "proxy.config.http.default_buffer_water_mark");

  // Stat Page Info
  HttpEstablishStaticConfigLongLong(c.enable_http_info, "proxy.config.http.enable_http_info");


  ///////////////////////////////////////////////////////////////////////////
  //   Added by YTS Team, yamsat                                                //
  //   Connection collapsing Configuration parameters                      //
  // 1. hashtable_enabled: if set to 1, requests will first search the     //
  //    hashtable to see if another similar request is already being served//
  // 2. rww_wait_time: read-while-write wait time: While read while write  //
  //    is enabled, the secondary clients will wait this amount of time    //
  //    after which cache lookup is retried                                //
  // 3. revaildate_window_period: while revaidation of a cached object is  //
  //    being done, the secondary clients for the same url will serve the  //
  //    stale object for this amount of time, after the revalidation had   //
  //    started                                                            //
  ///////////////////////////////////////////////////////////////////////////

  HttpEstablishStaticConfigLongLong(c.hashtable_enabled, "proxy.config.connection_collapsing.hashtable_enabled");

  HttpEstablishStaticConfigLongLong(c.rww_wait_time, "proxy.config.connection_collapsing.rww_wait_time");

  HttpEstablishStaticConfigLongLong(c.revalidate_window_period,
                                    "proxy.config.connection_collapsing.revalidate_window_period");

  HttpEstablishStaticConfigLongLong(c.srv_enabled, "proxy.config.srv_enabled");

  //##############################################################################
  //#
  //# Redirection
  //#
  //# 1. redirection_enabled: if set to 1, redirection is enabled.
  //# 2. number_of_redirections: The maximum number of redirections YTS permits
  //# 3. post_copy_size: The maximum POST data size YTS permits to copy
  //#
  //##############################################################################

  HttpEstablishStaticConfigLongLong(c.redirection_enabled, "proxy.config.http.redirection_enabled");

  HttpEstablishStaticConfigLongLong(c.number_of_redirections, "proxy.config.http.number_of_redirections");

  HttpEstablishStaticConfigLongLong(c.post_copy_size, "proxy.config.http.post_copy_size");

  // Transparency flag.
  char buffer[10];
  if (REC_ERR_OKAY ==  RecGetRecordString("proxy.config.http.transparent",
					  buffer, sizeof(buffer))) {
    if (0 == strcasecmp("both", buffer) ||
	0 == strcasecmp("on", buffer) ||
	0 == strcasecmp("enable", buffer)) {
      c.client_transparency_enabled = true;
      c.server_transparency_enabled = true;
    } else if (0 == strcasecmp("server", buffer)) {
      c.server_transparency_enabled = true;
      c.client_transparency_enabled = false;
    } else if (0 == strcasecmp("client", buffer)) {
      c.server_transparency_enabled = false;
      c.client_transparency_enabled = true;
    } else {
      c.server_transparency_enabled = false;
      c.client_transparency_enabled = false;
    }
  }

  // Cluster time delta gets it own callback since it needs
  //  to use ink_atomic_swap
  c.cluster_time_delta = 0;
  RegisterMgmtCallback(MGMT_EVENT_HTTP_CLUSTER_DELTA, cluster_delta_cb, NULL);

  http_config_cont->handleEvent(EVENT_NONE, NULL);

  return;
}

////////////////////////////////////////////////////////////////
//
//  HttpConfig::reconfigure()
//
////////////////////////////////////////////////////////////////
void
HttpConfig::reconfigure()
{
#define INT_TO_BOOL(i) ((i) ? 1 : 0);

  HttpConfigParams *params;

  params = NEW(new HttpConfigParams);

  params->incoming_ip_to_bind_saddr = m_master.incoming_ip_to_bind_saddr;
  params->outgoing_ip_to_bind_saddr = m_master.outgoing_ip_to_bind_saddr;
  params->proxy_hostname = xstrdup(m_master.proxy_hostname);
  params->proxy_hostname_len = (params->proxy_hostname) ? strlen(params->proxy_hostname) : 0;
  params->no_dns_forward_to_parent = INT_TO_BOOL(m_master.no_dns_forward_to_parent);
  params->uncacheable_requests_bypass_parent = INT_TO_BOOL(m_master.uncacheable_requests_bypass_parent);
  params->no_origin_server_dns = INT_TO_BOOL(m_master.no_origin_server_dns);
  params->use_client_target_addr = INT_TO_BOOL(m_master.use_client_target_addr);
  params->oride.maintain_pristine_host_hdr = INT_TO_BOOL(m_master.oride.maintain_pristine_host_hdr);

  params->snarf_username_from_authorization = INT_TO_BOOL(m_master.snarf_username_from_authorization);

  params->disable_ssl_parenting = m_master.disable_ssl_parenting;

  params->server_max_connections = m_master.server_max_connections;
  params->oride.origin_max_connections = m_master.oride.origin_max_connections;
  params->origin_min_keep_alive_connections = m_master.origin_min_keep_alive_connections;

  if (params->oride.origin_max_connections &&
      params->oride.origin_max_connections < params->origin_min_keep_alive_connections ) {
    Warning("origin_max_connections < origin_min_keep_alive_connections, setting min=max , please correct your records.config");
    params->origin_min_keep_alive_connections = params->oride.origin_max_connections;
  }

  params->parent_proxy_routing_enable = INT_TO_BOOL(m_master.parent_proxy_routing_enable);
  params->fwd_proxy_auth_to_parent = 0;
  params->enable_url_expandomatic = INT_TO_BOOL(m_master.enable_url_expandomatic);

  params->oride.insert_request_via_string = INT_TO_BOOL(m_master.oride.insert_request_via_string);
  params->oride.insert_response_via_string = INT_TO_BOOL(m_master.oride.insert_response_via_string);
  params->verbose_via_string = m_master.verbose_via_string;
  params->proxy_request_via_string = xstrdup(m_master.proxy_request_via_string);
  params->proxy_request_via_string_len = (params->proxy_request_via_string) ? strlen(params->proxy_request_via_string) : 0;
  params->proxy_response_via_string = xstrdup(m_master.proxy_response_via_string);
  params->proxy_response_via_string_len = (params->proxy_response_via_string) ? strlen(params->proxy_response_via_string) : 0;

  params->wuts_enabled = INT_TO_BOOL(m_master.wuts_enabled);
  params->log_spider_codes = INT_TO_BOOL(m_master.log_spider_codes);

  params->url_expansions_string = xstrdup(m_master.url_expansions_string);
  params->url_expansions = parse_url_expansions(params->url_expansions_string, &params->num_url_expansions);

  params->proxy_server_port = m_master.proxy_server_port;
  params->proxy_server_other_ports = xstrdup(m_master.proxy_server_other_ports);
  params->oride.keep_alive_enabled = INT_TO_BOOL(m_master.oride.keep_alive_enabled);
  params->oride.chunking_enabled = INT_TO_BOOL(m_master.oride.chunking_enabled);
  params->session_auth_cache_keep_alive_enabled = INT_TO_BOOL(m_master.session_auth_cache_keep_alive_enabled);
  params->origin_server_pipeline = m_master.origin_server_pipeline;
  params->user_agent_pipeline = m_master.user_agent_pipeline;
  params->share_server_sessions = INT_TO_BOOL(m_master.share_server_sessions);
  params->oride.keep_alive_post_out = m_master.oride.keep_alive_post_out;

  params->oride.keep_alive_no_activity_timeout_in = m_master.oride.keep_alive_no_activity_timeout_in;
  params->keep_alive_no_activity_timeout_out = m_master.keep_alive_no_activity_timeout_out;
  params->oride.transaction_no_activity_timeout_in = m_master.oride.transaction_no_activity_timeout_in;
  params->oride.transaction_no_activity_timeout_out = m_master.oride.transaction_no_activity_timeout_out;
  params->transaction_active_timeout_in = m_master.transaction_active_timeout_in;
  params->oride.transaction_active_timeout_out = m_master.oride.transaction_active_timeout_out;
  params->accept_no_activity_timeout = m_master.accept_no_activity_timeout;
  params->background_fill_active_timeout = m_master.background_fill_active_timeout;
  params->background_fill_threshold = m_master.background_fill_threshold;

  params->oride.connect_attempts_max_retries = m_master.oride.connect_attempts_max_retries;
  params->oride.connect_attempts_max_retries_dead_server = m_master.oride.connect_attempts_max_retries_dead_server;
  params->oride.connect_attempts_rr_retries = m_master.oride.connect_attempts_rr_retries;
  params->oride.connect_attempts_timeout = m_master.oride.connect_attempts_timeout;
  params->oride.post_connect_attempts_timeout = m_master.oride.post_connect_attempts_timeout;
  params->parent_connect_attempts = m_master.parent_connect_attempts;
  params->per_parent_connect_attempts = m_master.per_parent_connect_attempts;
  params->parent_connect_timeout = m_master.parent_connect_timeout;

  params->oride.sock_recv_buffer_size_out = m_master.oride.sock_recv_buffer_size_out;
  params->oride.sock_send_buffer_size_out = m_master.oride.sock_send_buffer_size_out;
  params->oride.sock_option_flag_out = m_master.oride.sock_option_flag_out;

  params->oride.anonymize_remove_from = INT_TO_BOOL(m_master.oride.anonymize_remove_from);
  params->oride.anonymize_remove_referer = INT_TO_BOOL(m_master.oride.anonymize_remove_referer);
  params->oride.anonymize_remove_user_agent = INT_TO_BOOL(m_master.oride.anonymize_remove_user_agent);
  params->oride.anonymize_remove_cookie = INT_TO_BOOL(m_master.oride.anonymize_remove_cookie);
  params->oride.anonymize_remove_client_ip = INT_TO_BOOL(m_master.oride.anonymize_remove_client_ip);
  params->oride.anonymize_insert_client_ip = INT_TO_BOOL(m_master.oride.anonymize_insert_client_ip);
  params->oride.append_xforwards_header = INT_TO_BOOL(m_master.oride.append_xforwards_header);
  params->anonymize_other_header_list = xstrdup(m_master.anonymize_other_header_list);

  params->global_user_agent_header = xstrdup(m_master.global_user_agent_header);
  params->global_user_agent_header_size = params->global_user_agent_header ?
    strlen(params->global_user_agent_header) : 0;

  params->oride.proxy_response_server_string = xstrdup(m_master.oride.proxy_response_server_string);
  params->oride.proxy_response_server_string_len = params->oride.proxy_response_server_string ?
    strlen(params->oride.proxy_response_server_string) : 0;
  params->oride.proxy_response_server_enabled = m_master.oride.proxy_response_server_enabled;

  params->oride.insert_squid_x_forwarded_for = INT_TO_BOOL(m_master.oride.insert_squid_x_forwarded_for);
  params->insert_age_in_response = INT_TO_BOOL(m_master.insert_age_in_response);
  params->avoid_content_spoofing = INT_TO_BOOL(m_master.avoid_content_spoofing);
  params->enable_http_stats = INT_TO_BOOL(m_master.enable_http_stats);
  params->normalize_ae_gzip = INT_TO_BOOL(m_master.normalize_ae_gzip);

  params->icp_enabled = (m_master.icp_enabled == ICP_MODE_SEND_RECEIVE ? 1 : 0);
  params->stale_icp_enabled = m_master.stale_icp_enabled;

  params->oride.cache_heuristic_min_lifetime = m_master.oride.cache_heuristic_min_lifetime;
  params->oride.cache_heuristic_max_lifetime = m_master.oride.cache_heuristic_max_lifetime;
  params->oride.cache_heuristic_lm_factor = min(max(m_master.oride.cache_heuristic_lm_factor, 0), 1);

  params->oride.cache_guaranteed_min_lifetime = m_master.oride.cache_guaranteed_min_lifetime;
  params->oride.cache_guaranteed_max_lifetime = m_master.oride.cache_guaranteed_max_lifetime;

  params->oride.cache_max_stale_age = m_master.oride.cache_max_stale_age;
  params->oride.freshness_fuzz_time = m_master.oride.freshness_fuzz_time;
  params->oride.freshness_fuzz_min_time = m_master.oride.freshness_fuzz_min_time;
  params->oride.freshness_fuzz_prob = m_master.oride.freshness_fuzz_prob;

  params->cache_vary_default_text = xstrdup(m_master.cache_vary_default_text);
  params->cache_vary_default_images = xstrdup(m_master.cache_vary_default_images);
  params->cache_vary_default_other = xstrdup(m_master.cache_vary_default_other);

  // open read failure retries
  params->oride.max_cache_open_read_retries = m_master.oride.max_cache_open_read_retries;
  params->oride.cache_open_read_retry_time = m_master.oride.cache_open_read_retry_time;

  // open write failure retries
  params->max_cache_open_write_retries = m_master.max_cache_open_write_retries;

  params->oride.cache_http = INT_TO_BOOL(m_master.oride.cache_http);
  params->oride.cache_ignore_client_no_cache = INT_TO_BOOL(m_master.oride.cache_ignore_client_no_cache);
  params->oride.cache_ignore_client_cc_max_age = INT_TO_BOOL(m_master.oride.cache_ignore_client_cc_max_age);
  params->oride.cache_ims_on_client_no_cache = INT_TO_BOOL(m_master.oride.cache_ims_on_client_no_cache);
  params->oride.cache_ignore_server_no_cache = INT_TO_BOOL(m_master.oride.cache_ignore_server_no_cache);
  params->oride.cache_responses_to_cookies = m_master.oride.cache_responses_to_cookies;
  params->oride.cache_ignore_auth = INT_TO_BOOL(m_master.oride.cache_ignore_auth);
  params->oride.cache_urls_that_look_dynamic = INT_TO_BOOL(m_master.oride.cache_urls_that_look_dynamic);
  params->cache_enable_default_vary_headers = INT_TO_BOOL(m_master.cache_enable_default_vary_headers);

  params->ignore_accept_mismatch = INT_TO_BOOL(m_master.ignore_accept_mismatch);
  params->ignore_accept_language_mismatch = INT_TO_BOOL(m_master.ignore_accept_language_mismatch);
  params->ignore_accept_encoding_mismatch = INT_TO_BOOL(m_master.ignore_accept_encoding_mismatch);
  params->ignore_accept_charset_mismatch = INT_TO_BOOL(m_master.ignore_accept_charset_mismatch);

  params->oride.cache_when_to_revalidate = m_master.oride.cache_when_to_revalidate;
  params->cache_when_to_add_no_cache_to_msie_requests = m_master.cache_when_to_add_no_cache_to_msie_requests;

  params->oride.cache_required_headers = m_master.oride.cache_required_headers;
  params->cache_range_lookup = INT_TO_BOOL(m_master.cache_range_lookup);

  params->connect_ports_string = xstrdup(m_master.connect_ports_string);
  params->connect_ports = parse_ports_list(params->connect_ports_string);

  params->request_hdr_max_size = m_master.request_hdr_max_size;
  params->response_hdr_max_size = m_master.response_hdr_max_size;
  params->push_method_enabled = m_master.push_method_enabled;

  params->reverse_proxy_enabled = INT_TO_BOOL(m_master.reverse_proxy_enabled);
  params->url_remap_required = INT_TO_BOOL(m_master.url_remap_required);
  params->errors_log_error_pages = INT_TO_BOOL(m_master.errors_log_error_pages);
  params->slow_log_threshold = m_master.slow_log_threshold;
  params->record_cop_page = INT_TO_BOOL(m_master.record_cop_page);
  params->record_tcp_mem_hit = INT_TO_BOOL(m_master.record_tcp_mem_hit);
  params->oride.send_http11_requests = m_master.oride.send_http11_requests;
  params->doc_in_cache_skip_dns = m_master.doc_in_cache_skip_dns;
  params->default_buffer_size_index = m_master.default_buffer_size_index;
  params->default_buffer_water_mark = m_master.default_buffer_water_mark;
  params->enable_http_info = INT_TO_BOOL(m_master.enable_http_info);
  params->reverse_proxy_no_host_redirect = xstrdup(m_master.reverse_proxy_no_host_redirect);
  params->reverse_proxy_no_host_redirect_len =
    params->reverse_proxy_no_host_redirect ? strlen(params->reverse_proxy_no_host_redirect) : 0;

  params->referer_filter_enabled = m_master.referer_filter_enabled;
  params->referer_format_redirect = m_master.referer_format_redirect;

  params->accept_encoding_filter_enabled = m_master.accept_encoding_filter_enabled;

  params->quick_filter_mask = m_master.quick_filter_mask;

  params->oride.down_server_timeout = m_master.oride.down_server_timeout;
  params->oride.client_abort_threshold = m_master.oride.client_abort_threshold;

  params->negative_revalidating_enabled = m_master.negative_revalidating_enabled;
  params->negative_revalidating_lifetime = m_master.negative_revalidating_lifetime;

  params->oride.negative_caching_enabled = m_master.oride.negative_caching_enabled;
  params->oride.negative_caching_lifetime = m_master.oride.negative_caching_lifetime;

  ///////////////////////////////////////////////////////////////////////////
  //  Added by YTS Team, yamsat                                                 //
  //   Connection collapsing Configuration parameters                      //
  // 1. hashtable_enabled: if set to 1, requests will first search the     //
  //    hashtable to see if another similar request is already being served//
  // 2. rww_wait_time: read-while-write wait time: While read while write  //
  //    is enabled, the secondary clients will wait this amount of time    //
  //    after which cache lookup is retried                                //
  // 3. revaildate_window_period: while revaidation of a cached object is  //
  //    being done, the secondary clients for the same url will serve the  //
  //    stale object for this amount of time, after the revalidation had   //
  //    started                                                            //
  ///////////////////////////////////////////////////////////////////////////

  params->hashtable_enabled = INT_TO_BOOL(m_master.hashtable_enabled);
  params->rww_wait_time = m_master.rww_wait_time;
  params->revalidate_window_period = m_master.revalidate_window_period;

  //##############################################################################
  //#
  //# Redirection
  //#
  //# 1. redirection_enabled: if set to 1, redirection is enabled.
  //# 2. number_of_redirections: The maximum number of redirections YTS permits
  //# 3. post_copy_size: The maximum POST data size YTS permits to copy
  //#
  //##############################################################################

  params->redirection_enabled = INT_TO_BOOL(m_master.redirection_enabled);
  params->number_of_redirections = m_master.number_of_redirections;
  params->post_copy_size = m_master.post_copy_size;

  m_id = configProcessor.set(m_id, params);

#undef INT_TO_BOOL

// Connection collapsing debug statements
  Debug("http_init", "proxy.config.connection_collapsing.hashtable_enabled = %d", params->hashtable_enabled);
  Debug("http_init", "proxy.config.connection_collapsing.rww_wait_time = %d", params->rww_wait_time);
  Debug("http_init", "proxy.config.connection_collapsing.revalidate_window_period = %d",
        params->revalidate_window_period);

// Redirection debug statements
  Debug("http_init", "proxy.config.http.redirection_enabled = %d", params->redirection_enabled);
  Debug("http_init", "proxy.config.http.number_of_redirections = %d", params->number_of_redirections);

  Debug("http_init", "proxy.config.http.post_copy_size = %d", params->post_copy_size);
}

////////////////////////////////////////////////////////////////
//
//  HttpConfig::acquire()
//
////////////////////////////////////////////////////////////////
HttpConfigParams *
HttpConfig::acquire()
{
  if (m_id != 0) {
    return (HttpConfigParams *) configProcessor.get(m_id);
  } else {
    return NULL;
  }
}

////////////////////////////////////////////////////////////////
//
//  HttpConfig::release()
//
////////////////////////////////////////////////////////////////
void
HttpConfig::release(HttpConfigParams * params)
{
  configProcessor.release(m_id, params);
}

/*
  Static Accept-Encoding/User-Agent filtering table
  The format of this table is compatible with ae_ua.config file
  */

static char *static_aeua_filter_array[] = {
//    ".substring Mozilla/4.",
  NULL
};

static int
read_string(FILE * fp, char *buf, int size)
{
  int i, retsize = (-1);
  if (fp && --size > 0 && buf) {
    for (buf[(retsize = 0)] = 0; (i = fgetc(fp)) != EOF;) {
      if (i == '\n' || i == '\r')
        break;
      if ((i == ' ' || i == '\t') && !retsize)
        continue;
      if (retsize < size)
        buf[retsize++] = (char) i;
    }
    buf[retsize] = 0;
    if (i == EOF && !retsize)
      retsize = (-1);           /* i == EOF && retsize == 0 */
  }
  return retsize;
}

static bool
store_error_message(char *err_msg_buf, int buf_size, const char *fmt, ...)
{
  if (likely(err_msg_buf && buf_size > 0)) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    (void) vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    buf[sizeof(buf) - 1] = 0;
    err_msg_buf[0] = 0;
    strncpy(err_msg_buf, buf, buf_size - 1);
    err_msg_buf[buf_size - 1] = 0;
    va_end(ap);
  }
  return false;
}

////////////////////////////////////////////////////////////////
//
//  HttpConfig::init_aeua_filter()
//
////////////////////////////////////////////////////////////////
int
HttpConfig::init_aeua_filter(char *config_fname)
{
  char errmsgbuf[1024], line[2048], *c;
  HttpUserAgent_RegxEntry *ua, **uaa, *u;
  FILE *fp;
  int i, size;
  int retcount = 0;

  Debug("http_aeua", "[HttpConfig::init_aeua_filter] - Config: \"%s\"", config_fname ? config_fname : "<NULL>");

  for (uaa = &HttpConfig::user_agent_list, i = 0; static_aeua_filter_array[i]; i++) {
    memset(errmsgbuf, 0, sizeof(errmsgbuf));
    ua = NEW(new HttpUserAgent_RegxEntry);
    if (!ua->create(static_aeua_filter_array[i], errmsgbuf, sizeof(errmsgbuf))) {
      ink_error("[HttpConfig::init_aeua_filter] - internal list - %s - %s",
                static_aeua_filter_array[i], errmsgbuf[0] ? errmsgbuf : "Unknown error");
      delete ua;
      ua = 0;
    } else {
      *uaa = ua;
      uaa = &(ua->next);
      retcount++;
    }
    Debug("http_aeua", "[HttpConfig::init_aeua_filter] - Add \"%s\" filter - %s",
          static_aeua_filter_array[i], ua ? "Success" : "Error");
  }
  if (config_fname && config_fname[0]) {
    Debug("http_aeua", "[HttpConfig::init_aeua_filter] - Opening config \"%s\"", config_fname);
    if ((fp = fopen(config_fname, "r")) != NULL) {
      while ((i = read_string(fp, line, (int) sizeof(line))) >= 0) {
        if (!i)
          continue;
        for (c = line; *c == ' ' || *c == '\t'; c++);
        if (*c == '#' || (size = strlen(c)) <= 0)
          continue;
        while (size > 0 && (c[size - 1] == ' ' || c[size - 1] == '\t' || c[size - 1] == '\n' || c[size - 1] == '\r'))
          c[--size] = 0;
        if (size <= 0)
          continue;
        Debug("http_aeua", "[HttpConfig::init_aeua_filter] - \"%s\"", c);
        for (u = HttpConfig::user_agent_list; u; u = u->next) {
          if (u->user_agent_str_size && u->user_agent_str && !strcmp(u->user_agent_str, c))
            break;
        }
        if (!u) {
          ua = NEW(new HttpUserAgent_RegxEntry);
          if (!ua->create(c, errmsgbuf, sizeof(errmsgbuf))) {
            ink_error("[HttpConfig::init_aeua_filter] - config list - %s - %s", c,
                      errmsgbuf[0] ? errmsgbuf : "Unknown error");
            delete ua;
            ua = 0;
          } else {
            *uaa = ua;
            uaa = &(ua->next);
            retcount++;
          }
          Debug("http_aeua", "[HttpConfig::init_aeua_filter] - Add \"%s\" filter - %s", c, ua ? "Success" : "Error");
        } else {
          Debug("http_aeua", "[HttpConfig::init_aeua_filter] - Duplicate record \"%s\"", c);
        }
      }
      fclose(fp);
    } else {
      ink_error("[HttpConfig::init_aeua_filter] - Can't open \"%s\"", config_fname);
    }
  }
  Debug("http_aeua", "[HttpConfig::init_aeua_filter] - Added %d REGEXP filters", retcount);
  return retcount;
}

////////////////////////////////////////////////////////////////
//
//  HttpUserAgent_RegxEntry::HttpUserAgent_RegxEntry()
//
////////////////////////////////////////////////////////////////
HttpUserAgent_RegxEntry::HttpUserAgent_RegxEntry()
{
  next = 0;
  user_agent_str_size = 0;
  user_agent_str = 0;
  regx_valid = false;
  stype = STRTYPE_UNKNOWN;
  memset(&regx, 0, sizeof(regx));
}

////////////////////////////////////////////////////////////////
//
//  HttpUserAgent_RegxEntry::~HttpUserAgent_RegxEntry()
//
////////////////////////////////////////////////////////////////
HttpUserAgent_RegxEntry::~HttpUserAgent_RegxEntry()
{
  (void) create();              /* just for clean up */
}

////////////////////////////////////////////////////////////////
//
//  HttpUserAgent_RegxEntry::create()
//
////////////////////////////////////////////////////////////////
bool
HttpUserAgent_RegxEntry::create(char *_refexp_str, char *errmsgbuf, int errmsgbuf_size)
{
  char *c, *refexp_str, refexp_str_buf[2048];
  bool retcode = false;

  user_agent_str = (char *) xfree_null(user_agent_str);
  user_agent_str_size = 0;
  stype = STRTYPE_UNKNOWN;
  if (regx_valid) {
    pcre_free(regx);
    regx_valid = false;
  }
  if (errmsgbuf && errmsgbuf_size > 0)
    errmsgbuf[0] = 0;


  if (_refexp_str && *_refexp_str) {
    strncpy(refexp_str_buf, _refexp_str, sizeof(refexp_str_buf) - 1);
    refexp_str_buf[sizeof(refexp_str_buf) - 1] = 0;
    refexp_str = &refexp_str_buf[0];

    Debug("http_aeua", "[HttpUserAgent_RegxEntry::create] - \"%s\"", refexp_str);
    while (*refexp_str && (*refexp_str == ' ' || *refexp_str == '\t'))
      refexp_str++;
    if (*refexp_str == '.') {
      for (c = refexp_str; *refexp_str && *refexp_str != ' ' && *refexp_str != '\t'; refexp_str++);
      while (*refexp_str && (*refexp_str == ' ' || *refexp_str == '\t'))
        *refexp_str++ = 0;
      if (*refexp_str) {
        if (!strcasecmp(c, ".substring") || !strcasecmp(c, ".string"))
          stype = STRTYPE_SUBSTR_CASE;
        else if (!strcasecmp(c, ".substring_ncase") || !strcasecmp(c, ".string_ncase"))
          stype = STRTYPE_SUBSTR_NCASE;
        else if (!strcasecmp(c, ".regexp") || !strcasecmp(c, ".regex"))
          stype = STRTYPE_REGEXP;
        else
          return store_error_message(errmsgbuf, errmsgbuf_size, "Unknown string type \"%s\"", c);
      } else
        return store_error_message(errmsgbuf, errmsgbuf_size, "Empty string with \"%s\" string type", c);
    } else
      return store_error_message(errmsgbuf, errmsgbuf_size, "Incorrect string type - must start with '.'");

    if ((user_agent_str = xstrdup(refexp_str)) != NULL) {
      retcode = true;
      if (stype == STRTYPE_REGEXP) {
        const char* error;
        int erroffset;

        regx = pcre_compile((const char *) user_agent_str, PCRE_CASELESS, &error, &erroffset, NULL);
        if (regx == NULL) {
          if (errmsgbuf && (errmsgbuf_size - 1) > 0)
            ink_strncpy(errmsgbuf, error, errmsgbuf_size - 1);
          user_agent_str = (char *) xfree_null(user_agent_str);
          retcode = false;
        } else
          regx_valid = true;
      }
      user_agent_str_size = user_agent_str ? strlen(user_agent_str) : 0;
    } else
      return store_error_message(errmsgbuf, errmsgbuf_size, "Memory allocation error (xstrdup)");
  }
  return retcode;
}

////////////////////////////////////////////////////////////////
//
//  HttpConfig::parse_ports_list()
//
////////////////////////////////////////////////////////////////
HttpConfigPortRange *
HttpConfig::parse_ports_list(char *ports_string)
{
  HttpConfigPortRange *ports_list = 0;

  if (!ports_string)
    return (0);

  if (strchr(ports_string, '*')) {
    ports_list = NEW(new HttpConfigPortRange);
    ports_list->low = -1;
    ports_list->high = -1;
    ports_list->next = NULL;
  } else {
    HttpConfigPortRange *pr, *prev;
    char *start;
    char *end;

    pr = NULL;
    prev = NULL;

    start = ports_string;

    while (1) {                 // eat whitespace
      while ((start[0] != '\0') && ParseRules::is_space(start[0]))
        start++;

      // locate the end of the next number
      end = start;
      while ((end[0] != '\0') && ParseRules::is_digit(end[0]))
        end++;

      // if there is no next number we're done
      if (start == end)
        break;

      pr = NEW(new HttpConfigPortRange);
      pr->low = atoi(start);
      pr->high = pr->low;
      pr->next = NULL;

      if (prev)
        prev->next = pr;
      else
        ports_list = pr;
      prev = pr;

      // if the next character after the current port
      //  number is a dash then we are parsing a range
      if (end[0] == '-') {
        start = end + 1;
        while ((start[0] != '\0') && ParseRules::is_space(start[0]))
          start++;

        end = start;
        while ((end[0] != '\0') && ParseRules::is_digit(end[0]))
          end++;

        if (start == end)
          break;

        pr->high = atoi(start);
      }

      start = end;

      ink_release_assert(pr->low <= pr->high);
    }
  }
  return (ports_list);
}

////////////////////////////////////////////////////////////////
//
//  HttpConfig::parse_url_expansions()
//
////////////////////////////////////////////////////////////////
char **
HttpConfig::parse_url_expansions(char *url_expansions_str, int *num_expansions)
{
  char **expansions = NULL;
  int count = 0, i;

  if (!url_expansions_str) {
    *num_expansions = count;
    return expansions;
  }
  // First count the number of URL expansions in the string
  char *start = url_expansions_str, *end;
  while (1) {
    // Skip whitespace
    while (isspace(*start))
      start++;
    if (*start == '\0')
      break;
    count++;
    end = start + 1;

    // Find end of expansion
    while (!isspace(*end) && *end != '\0')
      end++;
    start = end;
  }

  // Now extract the URL expansions
  if (count) {
    expansions = (char **) xmalloc(count * sizeof(char *));
    start = url_expansions_str;
    for (i = 0; i < count; i++) {
      // Skip whitespace
      while (isspace(*start))
        start++;
      expansions[i] = start;
      end = start + 1;

      // Find end of expansion
      while (!isspace(*end) && *end != '\0')
        end++;
      *end = '\0';
      if (i < (count - 1))
        start = end + 1;

    }
  }

  *num_expansions = count;
  return expansions;
}


////////////////////////////////////////////////////////////////
//
//  HttpConfig::cluster_delta_cb
//
////////////////////////////////////////////////////////////////
void *
HttpConfig::cluster_delta_cb(void *opaque_token, char *data_raw, int data_len)
{
  NOWARN_UNUSED(opaque_token);
  NOWARN_UNUSED(data_len);
  int32_t delta32 = (int32_t) atoi(data_raw);
  int32_t old;

  // Using ink_atomic_swap is mostly paranoia since a thirty bit write
  //  really ought to atomic.  However, any risk of bogus time is
  //  too ugly for me to contemplate
  old = ink_atomic_swap(&HttpConfig::m_master.cluster_time_delta, delta32);
  Debug("http_trans", "Cluster time delta moving from %d to %d", old, delta32);

  return NULL;

}

volatile int32_t icp_dynamic_enabled;
