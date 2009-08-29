#ifndef GARENA_GHL_H
#define GARENA_GHL_H 1
#include <stdint.h>
#include <garena/gcrp.h>
#include <garena/util.h>


#define GHL_JOIN_WAIT 10


#define GHL_EV_ME_JOIN 0
#define GHL_EV_JOIN 1
#define GHL_EV_PART 2
#define GHL_EV_TOGGLEVPN 3
#define GHL_EV_TALK 5
#define GHL_EV_UDP_ENCAP 6
#define GHL_EV_CONN_INCOMING 7
#define GHL_EV_CONN_RECV 8
#define GHL_EV_CONN_FIN 9
#define GHL_EV_NUM 10


typedef int ghl_timerfun_t(void *privdata);

typedef struct {
  ghl_timerfun_t *fun;
  void *privdata;
  int when;
} ghl_timer_t;


struct ghl_ctx_s;

typedef int ghl_fun_t(struct ghl_ctx_s *ctx, int event, void *event_data, void *privdata);

typedef struct {
  ghl_fun_t *fun;
  void *privdata;
} ghl_handler_t;

typedef struct ghl_ctx_s {
  int servsock;
  int peersock;
  char myname[17];
  uint32_t my_id;
  llist_t rooms;
  ghl_handler_t ghl_handlers[GHL_EV_NUM];
  gp2pp_handtab_t *gp2pp_htab;
  gcrp_handtab_t *gcrp_htab; 
  ghl_timer_t *hello_timer;
  ghl_timer_t *conn_retrans_timer;
  llist_t conns;
} ghl_ctx_t;



typedef struct {
  int roomsock;
  int room_id;
  gcrp_member_t *me;
  ghl_ctx_t *ctx;
  llist_t members;
  int got_welcome, got_members;
  ghl_timer_t *timeout;
  int joined;
  char welcome[GCRP_MAX_MSGSIZE];
} ghl_rh_t;

typedef gcrp_member_t ghl_member_t;

/* event structs */
typedef struct {
  int result;
  ghl_rh_t *rh;
} ghl_me_join_t;


typedef struct {
  ghl_member_t *member;
  ghl_rh_t *rh;
} ghl_join_t;

typedef struct {
  ghl_member_t *member;
  ghl_rh_t *rh;
} ghl_part_t;

typedef struct {
  ghl_member_t *member;
  ghl_rh_t *rh;
  char *text;
} ghl_talk_t;

typedef struct {
  ghl_member_t *member;
  ghl_rh_t *rh;
  int vpn;
} ghl_togglevpn_t;

typedef struct {
  ghl_member_t *member;
  int sport;
  int dport;
  int length;
  char *payload;
} ghl_udp_encap_t;

typedef struct {
  int conn_id;
  int ts_base;
  int snd_una, snd_next, rcv_next;
  llist_t sendq;
  llist_t recvq;
#define GHL_CSTATE_IDLE 0
#define GHL_CSTATE_LISTEN 1
#define GHL_CSTATE_ESTABLISHED 2
  int cstate;
  ghl_ctx_t *ctx;
  ghl_member_t *member;
} ghl_ch_t;   

typedef struct {
  ghl_ch_t *ch;
  int ts_rel;
  int length;
  int seq;
  char *payload;
} ghl_ch_pkt_t;

typedef struct {
  ghl_ch_t *ch;
  int dport;
} ghl_conn_incoming_t;

typedef struct {
  ghl_ch_t *ch;
  int length;
  char *payload;
} ghl_conn_recv_t;





ghl_ctx_t *ghl_new_ctx(char *name, char *password, int my_id, int server_ip, int server_port);
ghl_rh_t *ghl_join_room(ghl_ctx_t *ctx, int room_ip, int room_port, int room_id);
int ghl_leave_room(ghl_rh_t *rh);
ghl_member_t *ghl_member_from_id(ghl_rh_t *rh, int user_id);
ghl_member_t *ghl_global_find_member(ghl_ctx_t *ctx, int user_id);
int ghl_togglevpn(ghl_rh_t *rh, int vpn);
int ghl_talk(ghl_rh_t *rh, char *text);
int ghl_udp_encap(ghl_ctx_t *ctx, ghl_member_t *member, int sport, int dport, char *payload, int length);
int ghl_fill_fds(ghl_ctx_t *ctx, fd_set *fds);
int ghl_process(ghl_ctx_t *ctx, fd_set *fds);
int ghl_register_handler(ghl_ctx_t *ctx, int event, ghl_fun_t *fun, void *privdata);
int ghl_unregister_handler(ghl_ctx_t *ctx, int event);
void* ghl_handler_privdata(ghl_ctx_t *ctx, int event);
ghl_rh_t *ghl_room_from_id(ghl_ctx_t *ctx, int room_id);
ghl_timer_t * ghl_new_timer(int when, ghl_timerfun_t *fun, void *privdata);
void ghl_free_timer(ghl_timer_t *timer);
void ghl_free_ctx(ghl_ctx_t *ctx);
int ghl_next_timer(struct timeval *tv);
ghl_ch_t *ghl_conn_connect(ghl_ctx_t *ctx, ghl_member_t *member, int port);
void ghl_conn_close(ghl_ch_t *ch);
int ghl_conn_send(ghl_ch_t *ch, char *payload, int length);
ghl_ch_t *ghl_conn_from_id(ghl_ctx_t *ctx, int conn_id);

#endif
