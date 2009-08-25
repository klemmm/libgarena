#ifndef GARENA_GHL_H
#define GARENA_GHL_H 1
#include <stdint.h>
#include <garena/gcrp.h>
#include <garena/util.h>


#define GHL_EV_ME_JOIN 0
#define GHL_EV_JOIN 1
#define GHL_EV_PART 2
#define GHL_EV_STARTVPN 3
#define GHL_EV_STOPVPN 4
#define GHL_EV_TALK 5
#define GHL_EV_UDP_ENCAP 6
#define GHL_EV_NUM 7

typedef struct {
  int result;
} ghl_me_join_t;


typedef struct {
  int servsock;
  char myname[16];
  uint32_t my_ID;
  llist_t rooms;
} ghl_ctx_t;

typedef int ghl_funptr_t(int event, void *event_data, void *privdata);
typedef struct {
  ghl_funptr_t *fun;
  void *privdata;
} ghl_handler_t;
    
typedef struct {
  int roomsock;
  gcrp_member_t *me;
  ghl_ctx_t *ctx;
  llist_t members;
} ghl_rh_t;

static ghl_handler_t ghl_handlers[GHL_EV_NUM]; 

ghl_ctx_t *ghl_new_ctx(char *name, char *password, int server_ip, int server_port);
ghl_rh_t *ghl_join_room(ghl_ctx_t *ctx, int room_ip, int room_port);
int ghl_leave_room(ghl_rh_t *rh);
int ghl_toggle_vpn(ghl_rh_t *rh, int vpn);
int ghl_talk(ghl_rh_t *rh, char *text);
int ghl_udp_encap(ghl_rh_t *rh, int sport, int dport, char *payload, int length);
int ghl_fill_fds(ghl_ctx_t *ctx, fd_set *fds);
int ghl_process(ghl_ctx_t *ctx, fd_set *fds);
int ghl_register_handler(int event, ghl_funptr_t *fun, void *privdata);
int ghl_unregister_handler(int event);
void* ghl_handler_privdata(int event);


#endif
