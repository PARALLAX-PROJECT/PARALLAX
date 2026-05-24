#include "network_thread_stop.h"
#include "network_thread_common.h"
void network_thread_request_stop(net_state_t *st) { if (st) st->running = 0; }
void network_thread_stop(net_state_t *st)
{ if (!st) return; if (st->sockfd > 0) close(st->sockfd); mq_close(st->mq_in); mq_close(st->mq_out); mq_unlink(Q_IN); mq_unlink(Q_OUT); log_info("stopped"); }
