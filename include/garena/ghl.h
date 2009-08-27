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
#define GHL_EV_NUM 7


typedef int ghl_timerfun_t(void *privdata);

typedef struct {
  ghl_timerfun_t *fun;
  void *privdata;
  int when;
} ghl_timer_t;



typedef int ghl_funptr_t(int event, void *event_data, void *privdata);

typedef struct {
  ghl_funptr_t *fun;
  void *privdata;
} ghl_handler_t;

typedef struct {
  int servsock;
  int peersock;
  char myname[17];
  uint32_t my_ID;
  llist_t rooms;
  ghl_handler_t ghl_handlers[GHL_EV_NUM];
  gp2pp_handtab_t *gp2pp_htab;
  gcrp_handtab_t *gcrp_htab; 
  ghl_timer_t *hello_timer;
} ghl_ctx_t;

typedef struct {
  int roomsock;
  int room_ID;
  gcrp_member_t *me;
  ghl_ctx_t *ctx;
  llist_t members;
  int got_welcome, got_members;
  ghl_timer_t *timeout;
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
  char *payload;
} ghl_udp_encap_t;





ghl_ctx_t *ghl_new_ctx(char *name, char *password, int my_id, int server_ip, int server_port);
ghl_rh_t *ghl_join_room(ghl_ctx_t *ctx, int room_ip, int room_port, int room_id);
int ghl_leave_room(ghl_rh_t *rh);
ghl_member_t *ghl_member_from_id(ghl_rh_t *rh, int user_ID);
ghl_member_t *ghl_global_find_member(ghl_ctx_t *ctx, int user_ID);
int ghl_togglevpn(ghl_rh_t *rh, int vpn);
int ghl_talk(ghl_rh_t *rh, char *text);
int ghl_udp_encap(ghl_rh_t *rh, int sport, int dport, char *payload, int length);
int ghl_fill_fds(ghl_ctx_t *ctx, fd_set *fds);
int ghl_process(ghl_ctx_t *ctx, fd_set *fds);
int ghl_register_handler(ghl_ctx_t *ctx, int event, ghl_funptr_t *fun, void *privdata);
int ghl_unregister_handler(ghl_ctx_t *ctx, int event);
void* ghl_handler_privdata(ghl_ctx_t *ctx, int event);
ghl_rh_t *ghl_room_from_id(ghl_ctx_t *ctx, int room_ID);
ghl_timer_t * ghl_new_timer(int when, ghl_timerfun_t *fun, void *privdata);
void ghl_free_timer(ghl_timer_t *timer);
void ghl_free_ctx(ghl_ctx_t *ctx);

#endif
