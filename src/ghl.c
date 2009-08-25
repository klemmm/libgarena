#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <garena/config.h>
#include <garena/error.h>
#include <garena/gcrp.h>
#include <garena/gp2pp.h>
#include <garena/garena.h>
#include <garena/ghl.h>
#include <garena/util.h> 

int handle_join(int type, void *payload, int length, void *privdata) {
}


ghl_ctx_t *ghl_new_ctx(char *name, char *password, int server_ip, int server_port) {
  ghl_ctx_t *ctx = malloc(sizeof(ghl_ctx_t));
  if (ctx == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    return NULL;
  }  
  ctx->servsock = socket(PF_INET, SOCK_STREAM, 0);
  if (ctx->servsock == -1) {
    garena_errno = GARENA_ERR_LIBC;
    free(ctx);
    return NULL;
  }
  strncpy(ctx->myname, name, sizeof(ctx->myname)-1);
  ctx->rooms = llist_alloc();
  if (ctx->rooms == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    close(ctx->servsock);
    free(ctx);
    return NULL;
  }
  return ctx;
}

ghl_rh_t *ghl_join_room(ghl_ctx_t *ctx, int room_ip, int room_port, int room_id) {
  struct sockaddr_in fsocket;
  
  ghl_rh_t *rh = malloc(sizeof(ghl_rh_t));
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
  if (connect(rh->roomsock, (struct sockaddr *) &fsocket, sizeof(fsocket)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    close(rh->roomsock);
    free(rh);
    return NULL;
  }
  
  if (gcrp_send_join(rh->roomsock, room_id) == -1) {
    close(rh->roomsock);
    free(rh);
    return NULL;
  }
  
  rh->ctx = ctx;
  
  
  rh->members = llist_alloc();
  if (rh->members == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    close(rh->roomsock);
    free(rh);
    return NULL;
  }
  
  gcrp_register_handler(GCRP_MSG_MEMBERS, handle_join, rh);
  gcrp_register_handler(GCRP_MSG_WELCOME, handle_join, rh);
}


int ghl_leave_room(ghl_rh_t *rh) {
}

int ghl_toggle_vpn(ghl_rh_t *rh, int vpn) {
}

int ghl_talk(ghl_rh_t *rh, char *text) {
}

int ghl_udp_encap(ghl_rh_t *rh, int sport, int dport, char *payload, int length) {
}

int ghl_fill_fds(ghl_ctx_t *ctx, fd_set *fds) {
}

int ghl_process(ghl_ctx_t *ctx, fd_set *fds) {
}

int ghl_register_handler(int event, ghl_funptr_t *fun, void *privdata) {
}

int ghl_unregister_handler(int event) {
}

void* ghl_handler_privdata(int event) {
}

