#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <garena/config.h>
#include <garena/error.h>
#include <garena/gcrp.h>
#include <garena/gp2pp.h>
#include <garena/gsp.h>
#include <garena/garena.h>
#include <garena/ghl.h>
#include <garena/util.h> 
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <mhash.h>


static llist_t timers;
static int ghl_free_room(ghl_rh_t *rh);
static void member_extract(ghl_member_t *dst, gcrp_member_t *src);
static void insert_pkt(llist_t list, ghl_ch_pkt_t *pkt);
static void conn_free(ghl_ch_t *ch);
static void xmit_packet(ghl_ctx_t *ctx, ghl_ch_pkt_t *pkt);
static int ghl_signal_event(ghl_ctx_t *ctx, int event, void *eventparam);
static void myinfo_pack(gsp_myinfo_t *dst, ghl_myinfo_t *src);
static void myinfo_extract(ghl_myinfo_t *dst, gsp_myinfo_t *src);

static void send_hello(ghl_ctx_t *ctx, ghl_member_t *cur) {
  struct sockaddr_in remote;
  remote.sin_family = AF_INET;
    if (cur->conn_ok > 0) {
      remote.sin_addr = cur->effective_ip;
      remote.sin_port = htons(cur->effective_port);
      gp2pp_send_hello_request(ctx->peersock, ctx->my_info.user_id, &remote);
    } else {
      remote.sin_addr = cur->external_ip;
      remote.sin_port = htons(cur->external_port);
      gp2pp_send_hello_request(ctx->peersock, ctx->my_info.user_id, &remote);
      remote.sin_addr = cur->internal_ip;
      remote.sin_port = htons(cur->internal_port);
      gp2pp_send_hello_request(ctx->peersock, ctx->my_info.user_id, &remote);
      if (cur->external_port != GP2PP_PORT) {
        remote.sin_addr = cur->external_ip;
        remote.sin_port = htons(GP2PP_PORT);
        gp2pp_send_hello_request(ctx->peersock, ctx->my_info.user_id, &remote);
      }
    }

}

static void send_hello_to_all(ghl_ctx_t *ctx) {
  cell_t iter2;
  ghl_member_t *cur;
  ghl_rh_t *rh = ctx->room; 
  if (rh == NULL)
    return;
  
  
  for (iter2 = llist_iter(rh->members); iter2 ; iter2 = llist_next(iter2)) {
    cur = llist_val(iter2);
    if (cur->user_id == ctx->my_info.user_id)
      continue;
    send_hello(ctx, cur);
  }
}

static int handle_servconn_timeout(void *privdata) {
  ghl_ctx_t *ctx = privdata;
  ghl_servconn_t servconn;
  servconn.result = GHL_EV_RES_FAILURE;
  ghl_signal_event(ctx, GHL_EV_SERVCONN, &servconn);
  ctx->servconn_timeout = NULL; /* prevent ghl_free_ctx from freeing the timer */
  ctx->need_free = 1;
  return 0;
}

static int do_conn_retrans(void *privdata) {
  ghl_ctx_t *ctx = privdata;
  cell_t iter2;
  cell_t iter;
  ghl_ch_t *ch;
  ghl_conn_fin_t conn_fin_ev;
  ghl_ch_pkt_t *pkt;
  ghl_ch_t *todel = NULL;
  ghl_rh_t *rh = ctx->room;
  int now = time(NULL);
  int retrans;
  
  if (ctx->room == NULL) {
    ctx->conn_retrans_timer = ghl_new_timer(time(NULL) + GP2PP_CONN_RETRANS_CHECK, do_conn_retrans, privdata);
    return 0;
  }
  for (iter = llist_iter(rh->conns); iter; iter = llist_next(iter)) {
    if (todel) {
      llist_del_item(rh->conns, todel);
      conn_free(todel);
      todel = NULL;
    }

    ch = llist_val(iter);
    for (iter2 = llist_iter(ch->sendq); iter2; iter2 = llist_next(iter2)) {
      pkt = llist_val(iter2);
      if ((pkt->seq - ch->snd_una) > GP2PP_MAX_IN_TRANSIT) {
        fprintf(deb, "[Flow control] Congestion on connection %x\n", ch->conn_id);
        fflush(deb);
        break;
      }
      if ((pkt->xmit_ts + GP2PP_CONN_RETRANS_DELAY) <= now) {
        fprintf(deb, "[GHL] Retransmitting packet, seq=%u\n", pkt->seq);
        xmit_packet(ctx, pkt);
        pkt->did_fast_retrans = 0;
        retrans++;
      }
    }
    if (!llist_is_empty(ch->sendq) && ((ch->ts_ack + GP2PP_CONN_TIMEOUT) < now)) {
        fprintf(deb, "[GHL] Connection ID %x with user %s timed out.\n", ch->conn_id, ch->member->name);
        todel = ch;
        if (ch->cstate != GHL_CSTATE_CLOSING_OUT) {
          conn_fin_ev.ch = ch;
          ghl_signal_event(ctx, GHL_EV_CONN_FIN, &conn_fin_ev);
        }
        break;
    }

    if (llist_is_empty(ch->sendq) &&  (ch->cstate == GHL_CSTATE_CLOSING_OUT)) {
      todel = ch;
      continue;
    }
  }
  
  if (todel) {
    llist_del_item(rh->conns, todel);
    conn_free(todel);
    todel = NULL;
  }

  ctx->conn_retrans_timer = ghl_new_timer(time(NULL) + GP2PP_CONN_RETRANS_CHECK, do_conn_retrans, privdata);
  return 0;
}

static void do_fast_retrans(ghl_ctx_t *ctx, llist_t sendq, int up_to) {
  cell_t iter;
  ghl_ch_pkt_t *pkt;

  for (iter = llist_iter(sendq); iter; iter = llist_next(iter)) {
    pkt = llist_val(iter);
    if ((pkt->seq - up_to) >= 0)
      break;
    if (pkt->did_fast_retrans == 0) {
      xmit_packet(ctx, pkt);
        fprintf(deb, "[GHL] Fast-retransmitting packet, seq=%u\n", pkt->seq);
      
      pkt->did_fast_retrans = 1;
    }
  }
}

static int do_hello(void *privdata) {
  ghl_ctx_t *ctx = privdata;
  send_hello_to_all(ctx);  
  ctx->hello_timer = ghl_new_timer(time(NULL) + GP2PP_HELLO_INTERVAL, do_hello, privdata);
  return 0;
}

static int do_roominfo_query(void *privdata) {
  ghl_ctx_t *ctx = privdata;
  
  if (ctx && ctx->connected && gp2pp_request_roominfo(ctx->peersock, ctx->my_info.user_id, ctx->server_ip, ctx->gp2pp_port) == -1) {
    fprintf(deb, "[WARN/GHL] Room Info will not be available because the request failed.\n");
    fflush(deb); 
  }

  ctx->roominfo_timer = ghl_new_timer(time(NULL) + GHL_ROOMINFO_QUERY_INTERVAL, do_roominfo_query, privdata);
  return 0;
}


void ghl_fini(void) {
  if (timers != NULL)
    llist_free_val(timers);
}

int ghl_init(void) {
  timers = llist_alloc();
  if (timers == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    return -1;
  }
  return 0;
}

static int ghl_signal_event(ghl_ctx_t *ctx, int event, void *eventparam) {
  if (ctx->ghl_handlers[event].fun) {
    return ctx->ghl_handlers[event].fun(ctx, event, eventparam, ctx->ghl_handlers[event].privdata);
  } else {
    IFDEBUG(printf("[GHL/DEBUG] Event %x was ignored.\n", event));
    return 0;
  }
}

static int handle_auth(int type, void *payload, unsigned int length, void *privdata) {
  gsp_login_reply_t *login_reply = payload;
  ghl_servconn_t servconn;
  
  ghl_ctx_t *ctx = privdata;
  switch(type) {
    case GSP_MSG_LOGIN_REPLY:
      myinfo_extract(&ctx->my_info, &login_reply->my_info);
      fprintf(deb, "[GHL] My user_id is %x\n", ctx->my_info.user_id);
      fflush(deb);
      ctx->auth_ok = 1;
      if (gp2pp_request_roominfo(ctx->peersock, ctx->my_info.user_id, ctx->server_ip, ctx->gp2pp_port) == -1) {
        fprintf(deb, "[WARN/GHL] Room Info will not be available because the request failed.\n");
        fflush(deb); 
      }

      if ((ctx->connected = ctx->lookup_ok)) {
        ghl_free_timer(ctx->servconn_timeout);
        ctx->servconn_timeout = NULL;
        servconn.result = GHL_EV_RES_SUCCESS;
        ghl_signal_event(ctx, GHL_EV_SERVCONN, &servconn);
      }
      break;
    case GSP_MSG_AUTH_FAIL:
      ghl_free_timer(ctx->servconn_timeout);
      ctx->servconn_timeout = NULL;
      servconn.result = GHL_EV_RES_FAILURE;
      ghl_signal_event(ctx, GHL_EV_SERVCONN, &servconn);
      ghl_free_ctx(ctx);
      break;
    default:
      garena_errno = GARENA_ERR_INVALID;
      return -1;
  }
  return 0;
}

static int handle_ip_lookup(int type, void *payload, unsigned int length, void *privdata, unsigned int user_id, struct sockaddr_in *remote) {
  ghl_servconn_t servconn;
  ghl_ctx_t *ctx = privdata;
  gp2pp_lookup_reply_t *lookup = payload;
  switch(type) {
    case GP2PP_MSG_IP_LOOKUP_REPLY:
      ctx->my_info.external_ip = lookup->my_external_ip;
      ctx->my_info.external_port = htons(lookup->my_external_port);
      ctx->lookup_ok = 1;
      if ((ctx->connected = ctx->auth_ok)) {
        servconn.result = GHL_EV_RES_SUCCESS;
        ghl_signal_event(ctx, GHL_EV_SERVCONN, &servconn);
      }
      break;
    default:
      garena_errno = GARENA_ERR_INVALID;
      return -1;
  }
  return 0;
}

static int handle_roominfo(int type, void *payload, unsigned int length, void *privdata, unsigned int user_id, struct sockaddr_in *remote) {
  gp2pp_roominfo_reply_t *roominfo = payload;
  unsigned int i;
  unsigned int *num_users;
  unsigned int room_id;
  ghl_ctx_t *ctx = privdata;
  
  for (i = 0; i < roominfo->num_rooms; i++) {
    room_id = (ghtonl(roominfo->prefix) << 8) | (ghtonl(roominfo->usernum[i].suffix) & 0xFF);
    num_users = ihash_get(ctx->roominfo, room_id);
    if (num_users == NULL) {
      num_users = malloc(sizeof(unsigned int));
      ihash_put(ctx->roominfo, room_id,num_users);
    } 
    *num_users = roominfo->usernum[i].num_users;
  }
  return 0;
}

static int handle_initconn_msg(int type, void *payload, unsigned int length, void *privdata, unsigned int user_id, struct sockaddr_in *remote) {
  gp2pp_initconn_t *initconn = payload;
  ghl_conn_incoming_t conn_incoming_ev;
  ghl_ctx_t *ctx = privdata;
  ghl_rh_t *rh = ctx->room;
  if (rh == NULL) {
    fprintf(deb,"Received INITCONN, but we are not in a room.\n");
    fflush(deb);  
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  fprintf(deb, "Received INITCONN message from %s:%u\n", inet_ntoa(remote->sin_addr), htons(remote->sin_port));
  fflush(deb);
  conn_incoming_ev.ch = malloc(sizeof(ghl_ch_t));
  conn_incoming_ev.ch->member = ghl_member_from_id(rh, user_id);
  if (conn_incoming_ev.ch->member == NULL) {
    free(conn_incoming_ev.ch);
    fprintf(deb, "Received INITCONN from unknown user %x\n", user_id);
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  conn_incoming_ev.ch->ts_base = gp2pp_get_tsnow();
  conn_incoming_ev.ch->sendq = llist_alloc();
  conn_incoming_ev.ch->recvq = llist_alloc();
  conn_incoming_ev.ch->snd_una = 0;
  conn_incoming_ev.ch->ctx = ctx;
  conn_incoming_ev.ch->snd_next = 0;
  conn_incoming_ev.ch->rcv_next = 0;
  conn_incoming_ev.ch->rcv_next_deliver = 0;
  conn_incoming_ev.ch->ts_ack = 0;
  conn_incoming_ev.ch->cstate = GHL_CSTATE_ESTABLISHED;
  conn_incoming_ev.ch->conn_id = ghtonl(initconn->conn_id);
  conn_incoming_ev.dport = ghtons(initconn->dport);
  llist_add_head(rh->conns, conn_incoming_ev.ch);
  ghl_signal_event(ctx, GHL_EV_CONN_INCOMING, &conn_incoming_ev);
  return 0;
}

static void update_next(ghl_ctx_t *ctx, ghl_ch_t *ch) {
  cell_t iter;
  ghl_conn_recv_t conn_recv_ev;
  ghl_conn_fin_t conn_fin_ev;
  ghl_ch_pkt_t *pkt;
  
  if (llist_is_empty(ch->recvq))
    return;
   
  for (iter = llist_iter(ch->recvq); iter; iter = llist_next(iter)) {
    pkt = llist_val(iter);
    if (pkt->seq > ch->rcv_next)
      break;
    if (pkt->seq == ch->rcv_next)
      ch->rcv_next++;
  }
}

static void try_deliver_one(ghl_ctx_t *ctx) {
  int seq;
  cell_t iter;
  cell_t c_iter;
  ghl_conn_recv_t conn_recv_ev;
  ghl_conn_fin_t conn_fin_ev;
  ghl_ch_pkt_t *pkt;
  ghl_ch_pkt_t *todel = NULL;
  ghl_ch_t *ch = NULL;
  int r;
  
  if (ctx->room == NULL)
    return;
    
  if (llist_is_empty(ctx->room->conns))
    return;
    
   
  for (c_iter = llist_iter(ctx->room->conns); c_iter; c_iter = llist_next(c_iter)) {
    ch = llist_val(c_iter);
    if (llist_is_empty(ch->recvq))
      continue;
    seq = ch->rcv_next_deliver;
    pkt = llist_head(ch->recvq);
    if (pkt->seq == ch->rcv_next_deliver) {
      if (pkt->length > 0) {
        conn_recv_ev.ch = ch;
        conn_recv_ev.payload = pkt->payload;
        conn_recv_ev.length = pkt->length;
        r = pkt->length;
        if (ch->cstate != GHL_CSTATE_CLOSING_OUT) {
           r = ghl_signal_event(ctx, GHL_EV_CONN_RECV, &conn_recv_ev);
        }
        if (r == pkt->length) {
          llist_del_item(ch->recvq, pkt);
          free(pkt->payload);
          free(pkt);
          ch->rcv_next_deliver++;
        } else if (r == -1) {
          fprintf(deb, "receive error\n");
        } else {
          fprintf(deb, "partial receive\n");
          memmove(pkt->payload, pkt->payload + r, pkt->length - r);
          pkt->length -= r;
        }
      } else {
        conn_fin_ev.ch = ch;
        if (ch->cstate != GHL_CSTATE_CLOSING_OUT) {
          ch->cstate = GHL_CSTATE_CLOSING_OUT;
          ghl_signal_event(ctx, GHL_EV_CONN_FIN, &conn_fin_ev);
        }
        llist_del_item(ch->recvq, pkt);
        free(pkt->payload);
        free(pkt);
        ch->rcv_next_deliver++;
      }
      break;
    }
  }
}


static int can_deliver_one(ghl_ctx_t *ctx) {
  int seq;
  cell_t iter;
  cell_t c_iter;
  ghl_conn_recv_t conn_recv_ev;
  ghl_conn_fin_t conn_fin_ev;
  ghl_ch_pkt_t *pkt;
  ghl_ch_pkt_t *todel = NULL;
  ghl_ch_t *ch = NULL;

  if (ctx->room == NULL)
    return 0;
    
  if (llist_is_empty(ctx->room->conns))
    return 0;
   
  for (c_iter = llist_iter(ctx->room->conns); c_iter; c_iter = llist_next(c_iter)) {
    ch = llist_val(c_iter);
    if (llist_is_empty(ch->recvq))
      continue;

    seq = ch->rcv_next_deliver;
    pkt = llist_head(ch->recvq);
    if (pkt->seq == ch->rcv_next_deliver)
      return 1;
    
  }
  return 0;
}

static int handle_conn_fin_msg(int subtype, void *payload, unsigned int length, void *privdata, unsigned int user_id, unsigned int conn_id, int seq1, int seq2, int ts_rel, struct sockaddr_in *remote) { 
  ghl_ctx_t *ctx = privdata;
  ghl_ch_pkt_t *pkt;
  ghl_ch_t *ch;
  ghl_rh_t *rh = ctx->room;
  
  if (rh == NULL) {
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  
  ch = ghl_conn_from_id(rh, conn_id);
  if (ch == NULL) {
    fprintf(deb, "Alien conn: %x\n", conn_id);
    fflush(deb);
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  if ((ch->cstate == GHL_CSTATE_CLOSING_OUT) || (ch->cstate == GHL_CSTATE_CLOSING_IN))
    return 0;
  ch->cstate = GHL_CSTATE_CLOSING_IN;

  pkt = malloc(sizeof(ghl_ch_pkt_t));
  pkt->length = 0;
  pkt->did_fast_retrans = 0;
  pkt->xmit_ts = 0;
  pkt->partial = 0;
  pkt->payload = NULL;
  pkt->seq = ch->rcv_next; /* wtf is this crappy protocol, the FIN packet does not have a sequence number */
  pkt->ts_rel = ts_rel;
  pkt->ch = ch;
  insert_pkt(ch->recvq, pkt);
    update_next(ctx, ch);
  ch->finseq = seq1;
  return 0;
}

static int handle_conn_ack_msg(int subtype, void *payload, unsigned int length, void *privdata, unsigned int user_id, unsigned int conn_id, int seq1, int seq2, int ts_rel, struct sockaddr_in *remote) {
  ghl_ctx_t *ctx = privdata;
  ghl_rh_t *rh = ctx->room;
  ghl_ch_t *ch;
  cell_t iter;
  ghl_ch_pkt_t *pkt;
  ghl_ch_pkt_t *todel = NULL;
  
  if (rh == NULL) {
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  fprintf(deb, "[%x] ACK, this_ack=%u next_expected=%u\n", conn_id, seq1, seq2);
  ch = ghl_conn_from_id(rh, conn_id);
  if (ch == NULL) {
    fprintf(deb, "Alien conn: %x\n", conn_id);
    fflush(deb);
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  if ((seq2 - ch->snd_una) > 0) {
    ch->snd_una = seq2;
    ch->ts_ack = time(NULL);
  } else {
    fprintf(deb, "Duplicate ack %u on connex %x\n", seq2, conn_id);
  }
  
  
  for (iter=llist_iter(ch->sendq); iter; iter = llist_next(iter)) {
    if (todel != NULL) { 
      llist_del_item(ch->sendq, todel); 
      free(todel->payload);
      free(todel);
      todel = NULL;
    }
    pkt = llist_val(iter);
    if ((pkt->seq == seq1) || ((ch->snd_una - pkt->seq) >= 1)) {
      todel = pkt;
    }
    if ((pkt->xmit_ts == 0) && ((pkt->seq - ch->snd_una) < GP2PP_MAX_IN_TRANSIT)) 
     
      xmit_packet(ctx, pkt);
  }
  if (todel != NULL) {
      llist_del_item(ch->sendq, todel);
      free(todel->payload);
      free(todel);
      todel = NULL;
  }
  do_fast_retrans(ctx, ch->sendq, seq1);
  return 0;
}

static int handle_conn_data_msg(int subtype, void *payload, unsigned int length, void *privdata, unsigned int user_id, unsigned int conn_id, int seq1, int seq2, int ts_rel, struct sockaddr_in *remote) {
  ghl_ctx_t *ctx = privdata;
  ghl_ch_t *ch;
  ghl_rh_t *rh = ctx->room;
  ghl_ch_pkt_t *pkt;
  cell_t iter;
  ghl_ch_pkt_t *todel = NULL;
  if (rh == NULL) {
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  ch = ghl_conn_from_id(rh, conn_id);
  fprintf(deb, "[%x] DATA, this_seq=%u next_expected=%u\n", conn_id, seq1, seq2);
    
  if (ch == NULL) {
    fprintf(deb, "Alien conn: %x\n", conn_id);
    fflush(deb);
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  if (length == 0) {
    return 0;
  }
    
  if (ch->cstate == GHL_CSTATE_CLOSING_OUT)
    return 0;
  if ((seq2 - ch->snd_una) > 0) {
    ch->snd_una = seq2;
  } 

  for (iter=llist_iter(ch->sendq); iter; iter = llist_next(iter)) {
    if (todel != NULL) { 
      llist_del_item(ch->sendq, todel); 
      free(todel->payload);
      free(todel);
      todel = NULL;
    }
    pkt = llist_val(iter);
    if ((ch->snd_una - pkt->seq) >= 1) {
      todel = pkt;
    }
  }
  if (todel != NULL) {
      llist_del_item(ch->sendq, todel);
      free(todel->payload);
      free(todel);
      todel = NULL;
  }

  if ((ch->cstate == GHL_CSTATE_CLOSING_IN) && ((seq1 - ch->finseq) > 0)) {
    return 0;
  }
  fprintf(deb, "Received CONN DATA message from %s:%u\n", inet_ntoa(remote->sin_addr), htons(remote->sin_port));
  fflush(deb);
  
  pkt = malloc(sizeof(ghl_ch_pkt_t));
  pkt->length = length;
  pkt->payload = malloc(length);
  pkt->seq = seq1;
  pkt->ts_rel = ts_rel;
  pkt->ch = ch;
  pkt->xmit_ts = 0;
  pkt->partial = 0;
  pkt->did_fast_retrans = 0;
  memcpy(pkt->payload, payload, length);
  if (((seq1 - ch->rcv_next) >= 0) && ((ch->rcv_next - ch->rcv_next_delivered) < GP2PP_MAX_UNDELIVERED) && ((seq1 - ch->rcv_next) < GP2PP_MAX_IN_TRANSIT)) {
    insert_pkt(ch->recvq, pkt);
    update_next(ctx, ch); 
  }
  
  remote->sin_port = htons(ch->member->external_port);
  gp2pp_output_conn(ctx->peersock, GP2PP_CONN_MSG_ACK, NULL, 0, ctx->my_info.user_id, conn_id, seq1, ch->rcv_next, 0, remote);
  
  return 0;
}

static int handle_peer_msg(int type, void *payload, unsigned int length, void *privdata, unsigned int user_id, struct sockaddr_in *remote) {
  gp2pp_udp_encap_t *udp_encap = payload;
  ghl_ctx_t *ctx = privdata;
  ghl_rh_t *rh = ctx->room;
  ghl_udp_encap_t udp_encap_ev;
  ghl_member_t *member;
  if (rh == NULL) {
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  member = ghl_member_from_id(rh, user_id);
  if (member == NULL){
    fprintf(deb, "[GHL/ERR] Received GP2PP message from unknown user_id %x\n", user_id);
    fflush(deb);
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  switch(type) {
    case GP2PP_MSG_HELLO_REQ:
      IFDEBUG(printf("[GHL/DEBUG] Received HELLO request from %s\n", member->name));
      if (member->conn_ok == 0)
        member->conn_ok = 1;
      member->effective_ip = remote->sin_addr;
      member->effective_port = htons(remote->sin_port);
      gp2pp_send_hello_reply(ctx->peersock, ctx->my_info.user_id, user_id, remote);
      break;
    case GP2PP_MSG_HELLO_REP:
      IFDEBUG(printf("[GHL/DEBUG] Received HELLO reply from %s\n", member->name));
      member->effective_ip = remote->sin_addr;
      member->effective_port = htons(remote->sin_port);
      member->conn_ok = 2;
      break;
    case GP2PP_MSG_UDP_ENCAP:
      udp_encap_ev.member = member;
      udp_encap_ev.sport = htons(udp_encap->sport);
      udp_encap_ev.dport = htons(udp_encap->dport);
      udp_encap_ev.length = length - sizeof(gp2pp_udp_encap_t);
      udp_encap_ev.payload = udp_encap->payload;
      ghl_signal_event(ctx, GHL_EV_UDP_ENCAP, &udp_encap_ev);
      IFDEBUG(printf("[GHL/DEBUG] Received UDP_ENCAP from %s\n", member->name));
      
      break;
    default:
      garena_errno = GARENA_ERR_INVALID;
      return -1;
  }
  return 0;
}

static int handle_room_join_timeout(void *privdata) {
  int err = 0;
  ghl_me_join_t join;
  ghl_rh_t *rh = privdata;
/*  printf("Room %x join timeouted.\n", rh->room_id); */
  join.result = GHL_EV_RES_FAILURE;
  join.rh = rh;
  err |= (ghl_signal_event(rh->ctx, GHL_EV_ME_JOIN, &join) == -1);
  rh->timeout = NULL; /* prevent ghl_free_room from deleting the timer we are currently handling */
  err |= (ghl_free_room(rh) == -1);
  return err ? -1 : 0;
}


static void send_hello_to_members(ghl_rh_t *rh) {
  cell_t iter2;
  ghl_ctx_t *ctx = rh->ctx;
  ghl_member_t *cur;
  struct sockaddr_in remote;
  
  remote.sin_family = AF_INET;
  
  for (iter2 = llist_iter(rh->members); iter2 ; iter2 = llist_next(iter2)) {
    cur = llist_val(iter2);
    if (cur->user_id == ctx->my_info.user_id)
      continue;
    
    send_hello(ctx, cur);
  }
  
}

static int handle_room_join(int type, void *payload, unsigned int length, void *privdata, void *roomdata) {
  gcrp_welcome_t *welcome = payload;
  gcrp_memberlist_t *memberlist = payload;
  ghl_me_join_t join;
  ghl_rh_t *rh = NULL;
  ghl_ctx_t *ctx = privdata;
  int err = 0;
  ghl_member_t *member;
  unsigned int i;
  
  switch(type) {
    case GCRP_MSG_WELCOME:
      rh = ctx->room;
      if ((rh == NULL) || (rh->room_id != ghtonl(welcome->room_id))) {
        fprintf(stderr, "[GHL/WARN] Joined a room that we didn't ask to join (?!?) id=%x\n", ghtonl(welcome->room_id));
        return 0;
      }
      if (gcrp_tochar(rh->welcome, welcome->text, GCRP_MAX_MSGSIZE-1) == -1)
        fprintf(stderr, "[GHL/WARN] Welcome message decode failed\n");
      rh->got_welcome = 1;
      break;
    case GCRP_MSG_MEMBERS:
      IFDEBUG(printf("[GHL] Received members (%u members).\n", ghtonl(memberlist->num_members)));
      rh = ctx->room;
      
      if ((rh == NULL) || (rh->room_id != ghtonl(memberlist->room_id))) {
        fprintf(stderr, "[GHL/WARN] Joined a room that we didn't ask to join (?!?)\n");
        return 0;
      }
  
      for (i = 0; i < ghtonl(memberlist->num_members); i++) {
        member = malloc(sizeof(ghl_member_t));
        member_extract(member, memberlist->members + i);
        llist_add_head(rh->members, member);

        IFDEBUG(printf("[GHL] Room member: %s\n", member->name));
        
/*        printf("member %s PORT=%u ID=%x\n", member->name, htons(member->external_port), ghtonl(member->user_id)); */
        if (strcmp(ctx->my_info.name, member->name) == 0)
          rh->me = member;
      }
      
      if (rh->me == NULL) {
        fprintf(deb, "[GHL/ERROR] Joined a room, but we are not in the member list. Leaving now.\n");
        garena_errno = GARENA_ERR_PROTOCOL;
        llist_empty_val(rh->members);
        return -1;
      }
      ctx->my_info.user_id = rh->me->user_id;
      rh->got_members = 1;
      break;  
    case GCRP_MSG_JOIN_FAILED:

      rh = ctx->room;
      if (rh == NULL) {
        fprintf(stderr, "[GHL/WARN] Failed to join a room that we didn't ask to join\n");
        return 0;
      }

      join.result = GHL_EV_RES_FAILURE;
      join.rh = rh;
      ghl_free_timer(rh->timeout);
      rh->timeout = NULL;
      err |= (ghl_signal_event(rh->ctx, GHL_EV_ME_JOIN, &join) == -1);
      rh->timeout = NULL; /* prevent ghl_free_room from deleting the timer we are currently handling */
      err |= (ghl_free_room(rh) == -1);
      return err ? -1 : 0;
      
    default:  
      garena_errno = GARENA_ERR_INVALID;
      return -1;
  }
  
  if (rh && rh->got_welcome && rh->got_members) {
    join.result = GHL_EV_RES_SUCCESS;
    join.rh = rh;
    ghl_free_timer(rh->timeout);
    rh->timeout = NULL;
    send_hello_to_members(rh);
    rh->joined = 1;
    return ghl_signal_event(ctx, GHL_EV_ME_JOIN, &join);
  }
  
  return 0;
}


static int handle_room_activity(int type, void *payload, unsigned int length, void *privdata, void *roomdata) {
  static char buf[GCRP_MAX_MSGSIZE];
  gcrp_join_t *join = payload;
  gcrp_part_t *part = payload;
  gcrp_talk_t *talk = payload;
  gcrp_togglevpn_t *togglevpn = payload;
  ghl_rh_t *rh = roomdata;
  ghl_ctx_t *ctx = privdata;
  ghl_member_t *member;
  ghl_talk_t talk_ev;
  ghl_part_t part_ev;
  cell_t iter;
  ghl_conn_fin_t conn_fin_ev;
  ghl_ch_t *todel;
  ghl_ch_t *conn;
  ghl_join_t join_ev;
  ghl_togglevpn_t togglevpn_ev;
  
  switch(type) {
    case GCRP_MSG_JOIN:
      member = malloc(sizeof(ghl_member_t));
      member_extract(member, join);
      llist_add_head(rh->members, member);
      join_ev.rh = rh;
      join_ev.member = member;
      ghl_signal_event(ctx, GHL_EV_JOIN, &join_ev);
      if (member->user_id != ctx->my_info.user_id) {
        send_hello(ctx, member);
      }

      break;
    case GCRP_MSG_TALK:
      if (gcrp_tochar(buf, talk->text, (talk->length >> 1) + 1) == -1) {
        fprintf(stderr, "Failed to convert user message.\n");
      } else {
        member = ghl_member_from_id(rh, ghtonl(talk->user_id));
        if (member == NULL) {
          fprintf(deb, "[GHL/WARN] Received message from an user not on the room (%x)\n", ghtonl(togglevpn->user_id)); 
          fflush(deb);
          garena_errno = GARENA_ERR_PROTOCOL;
          return -1;
        }
        talk_ev.rh = rh;
        talk_ev.member = member;
        talk_ev.text = buf;
        ghl_signal_event(ctx, GHL_EV_TALK, &talk_ev);
      }
      break;
    case GCRP_MSG_STARTVPN:
        member = ghl_member_from_id(rh, ghtonl(togglevpn->user_id));
        if (member == NULL) {
          fprintf(deb, "[GHL/WARN] Received startvpn from an user not on the room (%x)\n", ghtonl(togglevpn->user_id)); 
          fflush(deb);
          garena_errno = GARENA_ERR_PROTOCOL;
          return -1;
        }
        togglevpn_ev.rh = rh;
        togglevpn_ev.member = member;
        togglevpn_ev.vpn = 1;
        member->vpn = 1;
        ghl_signal_event(ctx, GHL_EV_TOGGLEVPN, &togglevpn_ev);
      break;
    case GCRP_MSG_STOPVPN:
        member = ghl_member_from_id(rh, ghtonl(togglevpn->user_id));
        if (member == NULL) {
          fprintf(deb, "[GHL/WARN] Received stopvpn from an user not on the room (%x)\n", ghtonl(togglevpn->user_id)); 
          fflush(deb);
          garena_errno = GARENA_ERR_PROTOCOL;
          return -1;
        }
        
        togglevpn_ev.rh = rh;
        togglevpn_ev.member = member;
        togglevpn_ev.vpn = 0;
        member->vpn = 0;
        ghl_signal_event(ctx, GHL_EV_TOGGLEVPN, &togglevpn_ev);
      break;
    case GCRP_MSG_PART:
      member = ghl_member_from_id(rh, ghtonl(part->user_id));
      if (member == NULL) {
        fprintf(deb, "[GHL/WARN] Received PART message from an user, but that user was not in the room (%x).\n", ghtonl(togglevpn->user_id)); 
        fflush(deb);
        garena_errno = GARENA_ERR_PROTOCOL;
        return -1;
      } 
      part_ev.member = member;
      part_ev.rh = rh;
      ghl_signal_event(ctx, GHL_EV_PART, &part_ev);
      llist_del_item(rh->members, member);
      todel = NULL;
      for (iter = llist_iter(rh->conns); iter; iter = llist_next(iter)) {
        if (todel) {
          llist_del_item(rh->conns, conn);
          conn_free(conn);
          todel = NULL;
        }

        conn = llist_val(iter);
        if (conn->member == member) {
           if (conn->cstate != GHL_CSTATE_CLOSING_OUT) {
             conn_fin_ev.ch = conn;
             ghl_signal_event(ctx, GHL_EV_CONN_FIN, &conn_fin_ev);
           }
           todel = conn;
        }
      }
      if (todel) {
        llist_del_item(rh->conns, conn);
        conn_free(conn);
        todel = NULL;
      }
      
      free(member);
      break;
    default:  
      garena_errno = GARENA_ERR_INVALID;
      return -1;
      break;
  }
  return 0;
}


ghl_ctx_t *ghl_new_ctx(char *name, char *password, int server_ip, int server_port, int gp2pp_port) {
  int i;
  unsigned int local_len = sizeof(struct sockaddr_in);
  MHASH mh;
  struct sockaddr_in local;
  struct sockaddr_in fsocket;
  ghl_ctx_t *ctx = malloc(sizeof(ghl_ctx_t));
  if (ctx == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    return NULL;
  }
  ctx->room = NULL;
  ctx->gp2pp_htab = NULL;
  ctx->gcrp_htab = NULL;
  ctx->gsp_htab = NULL;
  ctx->peersock = -1;
  ctx->gp2pp_port = gp2pp_port ? gp2pp_port : GP2PP_PORT;
  ctx->hello_timer = NULL;
  ctx->roominfo_timer = NULL;
  ctx->conn_retrans_timer = NULL;
  ctx->auth_ok = 0;
  ctx->need_free = 0;
  ctx->lookup_ok = 0;
  ctx->connected = 0;
  ctx->server_ip = server_ip;
  ctx->roominfo = NULL;  
  ctx->servsock = socket(PF_INET, SOCK_STREAM, 0);
  memset(&ctx->my_info, 0, sizeof(ctx->my_info));
  ctx->roominfo = ihash_init();
  if (ctx->roominfo == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    goto err;
  }
  
  if (ctx->servsock == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }

  ctx->peersock = socket(PF_INET, SOCK_DGRAM, 0);
  if (ctx->peersock == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }
  local.sin_family = AF_INET;
  local.sin_port = htons(GP2PP_PORT); 
  local.sin_addr.s_addr = INADDR_ANY;
  
  if (bind(ctx->peersock, (struct sockaddr *) &local, sizeof(local)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }

  fsocket.sin_family = AF_INET;
  fsocket.sin_port = htons(ctx->gp2pp_port);
  fsocket.sin_addr.s_addr = server_ip;
  if (connect(ctx->peersock, (struct sockaddr *) &fsocket, sizeof(fsocket)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }
  if (getsockname(ctx->peersock, (struct sockaddr *) &local, &local_len) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }
  ctx->my_info.internal_ip = local.sin_addr;
  ctx->my_info.internal_port = htons(local.sin_port);
  
  close(ctx->peersock);
  ctx->peersock = socket(PF_INET, SOCK_DGRAM, 0);
  if (ctx->peersock == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }
  
  local.sin_family = AF_INET;
  local.sin_port = htons(GP2PP_PORT); 
  local.sin_addr.s_addr = INADDR_ANY;
  
  if (bind(ctx->peersock, (struct sockaddr *) &local, sizeof(local)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }

  fsocket.sin_family = AF_INET;
  fsocket.sin_port = server_port ? htons(server_port) : htons(GSP_PORT);
  fsocket.sin_addr.s_addr = server_ip;
  if (connect(ctx->servsock, (struct sockaddr *) &fsocket, sizeof(fsocket)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }
  if (gsp_open_session(ctx->servsock, ctx->session_key, ctx->session_iv) == -1)
    goto err;
  if (gsp_send_hello(ctx->servsock, ctx->session_key, ctx->session_iv) == -1)
    goto err;
  if (gp2pp_do_ip_lookup(ctx->peersock, ctx->server_ip, ctx->gp2pp_port) == -1)
    goto err;

  mh = mhash_init(MHASH_MD5);
  if (mh == NULL)
    goto err;
  
  mhash(mh, password, strlen(password));
  mhash_deinit(mh, &ctx->md5pass);
  
  if (gsp_send_login(ctx->servsock, name, ctx->md5pass, ctx->session_key, ctx->session_iv, ctx->my_info.internal_ip.s_addr, ctx->my_info.internal_port) == -1)
    goto err;
  
  ctx->room = NULL;
  
  for (i = 0 ; i < GHL_EV_NUM; i++) {
    ctx->ghl_handlers[i].fun = NULL;
    ctx->ghl_handlers[i].privdata = NULL;
  }
  ctx->gcrp_htab = gcrp_alloc_handtab();
  if (ctx->gcrp_htab == NULL)
    goto err;
  ctx->gp2pp_htab = gp2pp_alloc_handtab();
  if (ctx->gp2pp_htab == NULL)
    goto err;
  ctx->gsp_htab = gsp_alloc_handtab();
  if (ctx->gsp_htab == NULL)
    goto err;
  
  /* GSP handlers */
  if (gsp_register_handler(ctx->gsp_htab, GSP_MSG_LOGIN_REPLY, handle_auth, ctx) == -1)
    goto err;
  if (gsp_register_handler(ctx->gsp_htab, GSP_MSG_AUTH_FAIL, handle_auth, ctx) == -1)
    goto err;
  
  /* GCRP handlers */
  if (gcrp_register_handler(ctx->gcrp_htab, GCRP_MSG_WELCOME, handle_room_join, ctx) == -1)
    goto err;
  if (gcrp_register_handler(ctx->gcrp_htab, GCRP_MSG_MEMBERS, handle_room_join, ctx) == -1)
    goto err;
  if (gcrp_register_handler(ctx->gcrp_htab, GCRP_MSG_JOIN, handle_room_activity, ctx) == -1)
    goto err;
  if (gcrp_register_handler(ctx->gcrp_htab, GCRP_MSG_PART, handle_room_activity, ctx) == -1)
    goto err;
  if (gcrp_register_handler(ctx->gcrp_htab, GCRP_MSG_TALK, handle_room_activity, ctx) == -1)
    goto err;
  if (gcrp_register_handler(ctx->gcrp_htab, GCRP_MSG_STARTVPN, handle_room_activity, ctx) == -1)
    goto err;
  if (gcrp_register_handler(ctx->gcrp_htab, GCRP_MSG_STOPVPN, handle_room_activity, ctx) == -1)
    goto err;
  if (gcrp_register_handler(ctx->gcrp_htab, GCRP_MSG_JOIN_FAILED, handle_room_join, ctx) == -1)
    goto err;
    
    /* GP2PP handlers */
  if (gp2pp_register_handler(ctx->gp2pp_htab, GP2PP_MSG_UDP_ENCAP, handle_peer_msg, ctx) == -1)
    goto err;
  if (gp2pp_register_handler(ctx->gp2pp_htab, GP2PP_MSG_HELLO_REQ, handle_peer_msg, ctx) == -1)
    goto err;
  if (gp2pp_register_handler(ctx->gp2pp_htab, GP2PP_MSG_HELLO_REP, handle_peer_msg, ctx) == -1)
    goto err;
  if (gp2pp_register_handler(ctx->gp2pp_htab, GP2PP_MSG_INITCONN, handle_initconn_msg, ctx) == -1)
    goto err;
  if (gp2pp_register_handler(ctx->gp2pp_htab, GP2PP_MSG_ROOMINFO_REPLY, handle_roominfo, ctx) == -1)
    goto err;
  if (gp2pp_register_handler(ctx->gp2pp_htab, GP2PP_MSG_IP_LOOKUP_REPLY, handle_ip_lookup, ctx) == -1)
    goto err;

  /* GP2PP CONN handlers */

  if (gp2pp_register_conn_handler(ctx->gp2pp_htab, GP2PP_CONN_MSG_DATA, handle_conn_data_msg, ctx) == -1)
    goto err;
  if (gp2pp_register_conn_handler(ctx->gp2pp_htab, GP2PP_CONN_MSG_FIN, handle_conn_fin_msg, ctx) == -1)
    goto err;
  if (gp2pp_register_conn_handler(ctx->gp2pp_htab, GP2PP_CONN_MSG_ACK, handle_conn_ack_msg, ctx) == -1)
    goto err;

  /* timers handlers */
  if ((ctx->hello_timer = ghl_new_timer(time(NULL) + GP2PP_HELLO_INTERVAL, do_hello, ctx)) == NULL)
    goto err;
  if ((ctx->conn_retrans_timer = ghl_new_timer(time(NULL) + GP2PP_CONN_RETRANS_CHECK, do_conn_retrans, ctx)) == NULL)
    goto err;
  if ((ctx->roominfo_timer = ghl_new_timer(time(NULL) + GHL_ROOMINFO_QUERY_INTERVAL, do_roominfo_query, ctx)) == NULL)
    goto err;
  if ((ctx->servconn_timeout = ghl_new_timer(time(NULL) + GHL_SERVCONN_TIMEOUT, handle_servconn_timeout, ctx)) == NULL)
    goto err;
    
  return ctx;

err:
  if (ctx->gp2pp_htab)
    free(ctx->gp2pp_htab);
  if (ctx->gcrp_htab)
    free(ctx->gcrp_htab);
  if (ctx->gsp_htab)
    free(ctx->gsp_htab);
  if (ctx->servsock != -1)
    close(ctx->servsock);
  if (ctx->peersock != -1)
    close(ctx->peersock);
  if (ctx->hello_timer)
    ghl_free_timer(ctx->hello_timer);
  if (ctx->conn_retrans_timer)
    ghl_free_timer(ctx->conn_retrans_timer);
  if (ctx->roominfo_timer)
    ghl_free_timer(ctx->roominfo_timer);
  if (ctx->servconn_timeout)
    ghl_free_timer(ctx->servconn_timeout);
  if (ctx->roominfo)
    ihash_free_val(ctx->roominfo);
  free(ctx);
  return NULL;
}

ghl_rh_t *ghl_join_room(ghl_ctx_t *ctx, int room_ip, int room_port, unsigned int room_id) {
  struct sockaddr_in fsocket;
  gcrp_join_block_t join_block;
  ghl_rh_t *rh;
  
  if (ctx->connected == 0) {
    garena_errno = GARENA_ERR_INVALID;
    return NULL;
  }
  
  if (ctx->room != NULL) {
    garena_errno = GARENA_ERR_INUSE;
    return NULL;
  }
  rh = malloc(sizeof(ghl_rh_t));
  if (rh == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    return NULL;
  }
  
  rh->roomsock = socket(PF_INET, SOCK_STREAM, 0);
  if (rh->roomsock == -1) {
    garena_errno = GARENA_ERR_LIBC;
    free(rh);
    return NULL;
  }
  
  fsocket.sin_family = AF_INET;
  fsocket.sin_addr.s_addr = room_ip;
  fsocket.sin_port = htons(room_port);
  
  if (connect(rh->roomsock, (struct sockaddr *) &fsocket, sizeof(fsocket)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    close(rh->roomsock);
    free(rh);
    return NULL;
  }
  
  myinfo_pack(&join_block, &ctx->my_info);
  if (gcrp_send_join(rh->roomsock, room_id, &join_block, ctx->md5pass) == -1) {
    close(rh->roomsock);
    free(rh);
    return NULL;
  }

  rh->ctx = ctx;
  rh->joined = 0;
  rh->room_id = room_id;
  rh->me = NULL;
  
  rh->members = llist_alloc();
  if (rh->members == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    close(rh->roomsock);
    free(rh);
    return NULL;
  }
  rh->conns = llist_alloc();
  if (rh->conns == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    close(rh->roomsock);
    free(rh->members);
    free(rh);
    return NULL;
  }
  rh->got_welcome = 0;
  rh->got_members = 0;
  ctx->room = rh;
  fflush(deb);
  rh->timeout = ghl_new_timer(time(NULL) + GHL_JOIN_TIMEOUT, handle_room_join_timeout, rh);
  return(rh);
}


ghl_member_t *ghl_member_from_id(ghl_rh_t *rh, unsigned int user_id) {
  cell_t iter;
  ghl_member_t *cur;
  for (iter = llist_iter(rh->members); iter ; iter = llist_next(iter)) {
    cur = llist_val(iter);
    if (cur->user_id == user_id)
      return cur;
  }
  garena_errno = GARENA_ERR_NOTFOUND;
  return NULL;
}


ghl_ch_t *ghl_conn_from_id(ghl_rh_t *rh, unsigned int conn_id) {
  cell_t iter;
  ghl_ch_t *cur;
  for (iter=llist_iter(rh->conns); iter; iter = llist_next(iter)) {
    cur = llist_val(iter);
    if (cur->conn_id == conn_id)
      return cur;
  }
  garena_errno = GARENA_ERR_NOTFOUND;
  return NULL;
}

 
static int ghl_free_room(ghl_rh_t *rh) {
  cell_t iter;
  ghl_ch_t *ch;
  close(rh->roomsock);
  
  for (iter = llist_iter(rh->members); iter; iter = llist_next(iter)) {
    free(llist_val(iter));
  }

  for (iter = llist_iter(rh->conns); iter; iter = llist_next(iter)) {
    ch = llist_val(iter);
    conn_free(ch);
    
  }
  
  llist_free(rh->members);
  llist_free(rh->conns);
  if (rh->timeout)
    ghl_free_timer(rh->timeout);
  rh->ctx->room = NULL;
  free(rh);
  return 0;    
}

int ghl_leave_room(ghl_rh_t *rh) {
  if (gcrp_send_part(rh->roomsock, rh->ctx->my_info.user_id) == -1) {
    return -1;
  }
  return ghl_free_room(rh);
}

int ghl_togglevpn(ghl_rh_t *rh, int vpn) {
  return gcrp_send_togglevpn(rh->roomsock, rh->ctx->my_info.user_id, vpn);
}

int ghl_talk(ghl_rh_t *rh, char *text) {
  return gcrp_send_talk(rh->roomsock, rh->room_id, rh->ctx->my_info.user_id, text);  
}

int ghl_udp_encap(ghl_ctx_t *ctx, ghl_member_t *member, int sport, int dport, char *payload, unsigned int length) {
  struct sockaddr_in fsocket;
  
  if (member->conn_ok == 0) {
    garena_errno = GARENA_ERR_AGAIN;
    return -1;
  }  
  
  fsocket.sin_family = AF_INET;
  fsocket.sin_port = htons(member->effective_port);
  fsocket.sin_addr = member->effective_ip;
  
  return gp2pp_send_udp_encap(ctx->peersock, ctx->my_info.user_id, sport, dport, payload, length, &fsocket);
}

int ghl_fill_fds(ghl_ctx_t *ctx, fd_set *fds) {
  int max = -1;
  
  if (ctx->room) {
    FD_SET(ctx->room->roomsock, fds);
    if (ctx->room->roomsock > max)
      max = ctx->room->roomsock;
  }

  FD_SET(ctx->peersock, fds);
  if (ctx->peersock > max)
    max = ctx->peersock;
  if (ctx->servsock != -1) {
    FD_SET(ctx->servsock, fds);
    if (ctx->servsock > max)
      max = ctx->servsock;
  }
  return max;
}


/**
 * Fills tv with the time remaining until next timer event
 *
 * @param tv struct timeval to fill
 * @return 0 if no next timer is found, 1 otherwise
 */
 
int ghl_fill_tv(ghl_ctx_t *ctx, struct timeval *tv) {
  ghl_timer_t *next;
  int now = time(NULL);
  tv->tv_sec = 0;
  tv->tv_usec = 0;
  if (can_deliver_one(ctx))
    return 1;
  next = llist_head(timers);
  if (next) {
    tv->tv_sec = (next->when > now) ? ((next->when) - now) : 0;
    return 1;
  }
  return 0;
}

int ghl_process(ghl_ctx_t *ctx, fd_set *fds) {
  char buf[GCRP_MAX_MSGSIZE];
  int r;
  fd_set myfds;
  struct sockaddr_in remote;
  cell_t iter;
  struct timeval tv;
  int stuff_to_do; 
  int now = time(NULL);
  ghl_timer_t *cur;
  ghl_room_disc_t room_disc_ev;
  
  /* process timers */
  do {
    stuff_to_do = 0;
    for (iter = llist_iter(timers); iter; iter = llist_next(iter)) {
      cur = llist_val(iter);
      if (now >= cur->when) {
        if (cur->fun(cur->privdata) == -1) {
          perror("[GHL/ERR] a timer was not handled correctly");
        }
        ghl_free_timer(cur);
        if (ctx->need_free) {
          garena_errno = GARENA_ERR_PROTOCOL;
          ghl_free_ctx(ctx);
          return -1;
        }
        stuff_to_do=1;
        /* XXX FIXME */
        break;
      }
    }
  } while (stuff_to_do);
  
  if (can_deliver_one(ctx))  
    try_deliver_one(ctx);

  /* process network activity */
  if (fds == NULL) {
    fds = &myfds;
    FD_ZERO(&myfds);
    if ((r = ghl_fill_fds(ctx, &myfds)) == -1) {
      /* it would be stupid to use select() on an empty fd_set, so this situation is an error */
      garena_errno = GARENA_ERR_INVALID;
      return -1;
    }
    if (ghl_fill_tv(ctx, &tv)) {
      IFDEBUG(printf("[GHL/DEBUG] Going to sleep, wake-up on network activity or at next timer (%u secs)\n", tv.tv_sec));
      r = select(r+1, &myfds, NULL, NULL, &tv);
    } else { 
      IFDEBUG(printf("[GHL/DEBUG] Going to sleep, wake-up on network activity\n"));
      r = select(r+1, &myfds, NULL, NULL, NULL);
    }
    if (r == 0) {
      IFDEBUG(printf("[GHL/DEBUG] Wake-up due to timer\n"));
    } else {
      IFDEBUG(printf("[GHL/DEBUG] Wake-up due to network activity\n"));
    }
  }
  
  
  if (ctx->room && FD_ISSET(ctx->room->roomsock, fds)) {
    r = gcrp_read(ctx->room->roomsock, buf, GCRP_MAX_MSGSIZE);
    if (r != -1) {
      gcrp_input(ctx->gcrp_htab, buf, r, ctx->room);
    } else {
      room_disc_ev.rh = ctx->room;
      ghl_signal_event(ctx, GHL_EV_ROOM_DISC, &room_disc_ev);
      ghl_free_room(ctx->room);
      ctx->room = NULL;
      
    }
  }


  if (FD_ISSET(ctx->peersock, fds)) {
    r = gp2pp_read(ctx->peersock, buf, GCRP_MAX_MSGSIZE, &remote);
    if (r != -1) {
      gp2pp_input(ctx->gp2pp_htab, buf, r, &remote);
    } 
  }
  if (FD_ISSET(ctx->servsock, fds)) {
    r = gsp_read(ctx->servsock, buf, GSP_MAX_MSGSIZE);
    if (r != -1) {
      gsp_input(ctx->gsp_htab, buf, r, ctx->session_key, ctx->session_iv);
    } else {
      fprintf(deb, "[WARN/GHL] Disconnected from main server, but we don't care\n");
      fflush(deb);
      close(ctx->servsock);
      ctx->servsock = -1;
    }
  }
  return 0;
}



/**
 * Register a handler to be called on the specified event
 *
 * @param msgtype The message type for which we define a handler
 * @param fun Pointer to handler function
 * @param privdata Pointer to private data (anything you want) that will be supplied to the called function
 * @return 0 for success, -1 for failure
 */
 
int ghl_register_handler(ghl_ctx_t *ctx, int event, ghl_fun_t *fun, void *privdata) {
  if ((event < 0) || (event >= GHL_EV_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  
  if (ctx->ghl_handlers[event].fun != NULL) {
    garena_errno = GARENA_ERR_INUSE;
    return -1;
  }
  ctx->ghl_handlers[event].fun = fun;
  ctx->ghl_handlers[event].privdata = privdata;
  return 0;
}

/**
 * Add timer to the list
 *
 */
ghl_timer_t * ghl_new_timer(int when, ghl_timerfun_t *fun, void *privdata) {
  ghl_timer_t *tmp = malloc(sizeof(ghl_timer_t));
  ghl_timer_t *cur;
  cell_t iter;
  
  if (tmp == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    return NULL;
  }
  tmp->fun = fun;
  tmp->privdata = privdata;
  tmp->when = when;
  for (iter = llist_iter(timers); iter; iter = llist_next(iter)) {
    cur = llist_val(iter);
    if (cur->when >= tmp->when)
      break;
  }
  if (iter) {
    llist_add_before(timers, cur, tmp);
  } else {
    llist_add_tail(timers, tmp);
  }
  return tmp;
}

static void insert_pkt(llist_t list, ghl_ch_pkt_t *pkt) {
  ghl_ch_pkt_t *cur;
  cell_t iter;
  
  for (iter = llist_iter(list); iter; iter = llist_next(iter)) {
    cur = llist_val(iter);
    if (cur->seq == pkt->seq)
      return; /* we already have this packet */
    if ((cur->seq - pkt->seq) >= 0)
      break;
  }
  if (iter) {
    llist_add_before(list, cur, pkt);
  } else {
    llist_add_tail(list, pkt);
  }
}

/**
 * Unregisters a handler associated with the specified event
 *
 * @param event The message type for which we delete the handler
 * @return 0 for success, -1 for failure
 */
int ghl_unregister_handler(ghl_ctx_t *ctx, int event) {
  if ((event < 0) || (event >= GHL_EV_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  if (ctx->ghl_handlers[event].fun == NULL) {
    garena_errno = GARENA_ERR_NOTFOUND;
    return -1;
  }
  ctx->ghl_handlers[event].fun = NULL;
  ctx->ghl_handlers[event].privdata = NULL;  
  return 0;
}


/**
 * Get the privdata associated with a handler.
 *
 * @param event The message type of the handler we wish to retrieve the privdata
 * @return The privdata, or NULL if there is an error
 */
void* ghl_handler_privdata(ghl_ctx_t *ctx, int event) {
  if ((event < 0) || (event >= GHL_EV_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return NULL;
  }
  if (ctx->ghl_handlers[event].fun == NULL) {
    garena_errno = GARENA_ERR_NOTFOUND;
    return NULL;
  }
 return ctx->ghl_handlers[event].privdata;
}


void ghl_free_timer(ghl_timer_t *timer) {
  if (timer == NULL)
    return;
  llist_del_item(timers, timer);
  free(timer);
}


void ghl_free_ctx(ghl_ctx_t *ctx) {
  if (!ctx)
    return;
  /* free all rooms */
  if (ctx->room)
    ghl_free_room(ctx->room);
  close(ctx->peersock);
  if (ctx->servsock != -1)
    close(ctx->servsock);
  free(ctx->gcrp_htab);
  free(ctx->gp2pp_htab);
  free(ctx->gsp_htab);
  if (ctx->hello_timer)
    ghl_free_timer(ctx->hello_timer);
  if (ctx->conn_retrans_timer)
    ghl_free_timer(ctx->conn_retrans_timer);
  if (ctx->roominfo_timer)
    ghl_free_timer(ctx->roominfo_timer);
  if (ctx->servconn_timeout)
    ghl_free_timer(ctx->servconn_timeout);
  if (ctx->roominfo)
    ihash_free_val(ctx->roominfo);
  free(ctx);
}


ghl_ch_t *ghl_conn_connect(ghl_ctx_t *ctx, ghl_member_t *member, int port) {
  struct sockaddr_in remote;
  ghl_ch_t *ch;
  ghl_rh_t *rh = ctx->room;
  
  if (member->conn_ok == 0) {
    garena_errno = GARENA_ERR_AGAIN;
    return NULL;
  
  }
  if (rh == NULL) {
    garena_errno = GARENA_ERR_INVALID;
    return NULL;
  }
  
  ch = malloc(sizeof(ghl_ch_t));
  if (ch == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    return NULL;
  }
  
  ch->cstate = GHL_CSTATE_ESTABLISHED;
  ch->member = member;
  ch->ts_base = gp2pp_get_tsnow();
  ch->sendq = llist_alloc();
  ch->recvq = llist_alloc();
  ch->ctx = ctx;
  ch->snd_una = 0;
  ch->snd_next = 0;
  ch->rcv_next = 0;
  ch->rcv_next_deliver = 0;
  ch->ts_ack = 0;
  ch->conn_id = gp2pp_new_conn_id();
  llist_add_head(rh->conns, ch);
  

  remote.sin_family = AF_INET;
  remote.sin_addr = ch->member->effective_ip;
  remote.sin_port = htons(ch->member->effective_port);
  if (gp2pp_send_initconn(ctx->peersock, ctx->my_info.user_id, ch->conn_id, port, GP2PP_MAGIC_LOCALIP, &remote) == -1) {
    llist_free(ch->sendq);
    llist_free(ch->recvq);
    llist_del_item(rh->conns, ch);
    free(ch);
    return NULL;
  } else return ch;
}

static void conn_free(ghl_ch_t *ch) {
  cell_t iter;
  ghl_ch_pkt_t *pkt;
  for (iter = llist_iter(ch->sendq); iter; iter = llist_next(iter)) {
    pkt = llist_val(iter);
    free(pkt->payload);
  }
  for (iter = llist_iter(ch->recvq); iter; iter = llist_next(iter)) {
    pkt = llist_val(iter);
    free(pkt->payload);
  }
  llist_free_val(ch->sendq);
  llist_free_val(ch->recvq);
  free(ch);
}


void ghl_conn_close(ghl_ctx_t *ctx, ghl_ch_t *ch) {
  struct sockaddr_in remote;
  int i;
  if (ch->cstate == GHL_CSTATE_CLOSING_OUT)
    return;
  remote.sin_family = AF_INET;
  remote.sin_addr = ch->member->effective_ip;
  remote.sin_port = htons(ch->member->effective_port);
  for (i = 0; i < 4; i++) 
    gp2pp_output_conn(ctx->peersock, GP2PP_CONN_MSG_FIN, NULL, 0, ctx->my_info.user_id, ch->conn_id, ch->rcv_next, ch->rcv_next, 0, &remote);
  ch->cstate = GHL_CSTATE_CLOSING_OUT;

}

static void xmit_packet(ghl_ctx_t *ctx, ghl_ch_pkt_t *pkt) {
  struct sockaddr_in remote;
  remote.sin_family = AF_INET;
  remote.sin_addr = pkt->ch->member->effective_ip;
  remote.sin_port = htons(pkt->ch->member->effective_port);
  pkt->xmit_ts = time(NULL);
  gp2pp_output_conn(ctx->peersock, GP2PP_CONN_MSG_DATA, pkt->payload, pkt->length, ctx->my_info.user_id, pkt->ch->conn_id, pkt->seq, pkt->ch->rcv_next, pkt->ts_rel, &remote);
}

static void myinfo_pack(gsp_myinfo_t *dst, ghl_myinfo_t *src) {
  memset(dst, 0, sizeof(gsp_myinfo_t));
  dst->user_id = ghtonl(src->user_id);
  memcpy(dst->name, src->name, sizeof(dst->name));
  memcpy(dst->country, src->country, sizeof(dst->country));
  dst->name[sizeof(dst->name) - 1] = 0; /* not sure if it's necessary, don't know if the server expects a null-terminated string */
  dst->level = src->level;
  dst->external_ip = src->external_ip;
  dst->external_port = htons(src->external_port);
  dst->internal_ip = src->internal_ip;
  dst->internal_port = htons(src->internal_port);

  dst->unknown1 = src->unknown1;
  dst->unknown2 = src->unknown2;
  dst->unknown3 = src->unknown3;
  dst->unknown4 = src->unknown4;
  
}
static void myinfo_extract(ghl_myinfo_t *dst, gsp_myinfo_t *src) {
  dst->user_id = ghtonl(src->user_id);
  memcpy(dst->name, src->name, sizeof(src->name));
  memcpy(dst->country, src->country, sizeof(src->country));
  dst->name[sizeof(src->name)] = 0;
  dst->country[sizeof(src->country)] = 0;
  dst->level = src->level;
  /* 
   * ignore the src->internal_ip and src->internal_port
   * sent by the server, because it is computed locally
   */
  /* 
   * ignore src->external_ip and src->external_port
   * because it is computed more reliably by
   * the ip lookup.
   */
  
  dst->unknown1 = src->unknown1;
  dst->unknown2 = src->unknown2;
  dst->unknown3 = src->unknown3;
  dst->unknown4 = src->unknown4;
}

static void member_extract(ghl_member_t *dst, gcrp_member_t *src) {
  dst->user_id = ghtonl(src->user_id);
  memcpy(dst->name, src->name, sizeof(src->name));
  memcpy(dst->country, src->country, sizeof(src->country));
  dst->name[sizeof(src->name)] = 0;
  dst->country[sizeof(src->country)] = 0;
  dst->level = src->level;
  dst->vpn = src->vpn;
  dst->external_ip = src->external_ip;
  dst->internal_ip = src->internal_ip;
  dst->external_port = htons(src->external_port);
  dst->internal_port = htons(src->internal_port);
  dst->virtual_suffix = src->virtual_suffix;
  dst->effective_ip.s_addr = INADDR_NONE;
  dst->effective_port = 0;
  dst->conn_ok = 0;
}


int ghl_conn_send(ghl_ctx_t *ctx, ghl_ch_t *ch, char *payload, unsigned int length) {
  ghl_ch_pkt_t *pkt;
  int now = gp2pp_get_tsnow();
  if (ch->cstate == GHL_CSTATE_CLOSING_OUT) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  
  if ((ch->snd_next - ch->snd_una) >= GP2PP_MAX_SENDQ) {
    garena_errno = GARENA_ERR_AGAIN;
    return -1;
  }
  
  pkt = malloc(sizeof(ghl_ch_pkt_t));
  pkt->length = length;
  pkt->payload = malloc(length);
  pkt->seq = ch->snd_next;
  pkt->ts_rel = (now - ch->ts_base);
  pkt->ch = ch;
  pkt->xmit_ts = 0;
  pkt->partial = 0;
  pkt->did_fast_retrans = 0;
  memcpy(pkt->payload, payload, length);
  
  ch->snd_next++;
  ch->ts_base = now;
  if (!llist_is_empty(ch->sendq)) {
    ch->ts_ack = time(NULL);
  }
  insert_pkt(ch->sendq, pkt);
  if ((pkt->seq - ch->snd_una) < GP2PP_MAX_IN_TRANSIT) {
    xmit_packet(ctx, pkt);
    fprintf(deb, "[GHL] Initial packet transmit: seq=%u\n", pkt->seq);
  }
  return 0;
}

inline unsigned int ghl_max_conn_pkt(unsigned int mtu) {
  /* FIXME: should query path-MTU */
  return (mtu - sizeof(struct ip) - sizeof(struct udphdr) - sizeof(gp2pp_conn_hdr_t));
}
