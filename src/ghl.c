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


static llist_t timers;
static int ghl_free_room(ghl_rh_t *rh);

static int send_hello_to_all(ghl_ctx_t *ctx) {
  cell_t iter, iter2;
  ghl_member_t *cur;
  ghl_rh_t *rh; 
  struct sockaddr_in remote;
  
  remote.sin_family = AF_INET;
  
  for (iter = llist_iter(ctx->rooms); iter; iter = llist_next(iter)) {
    rh = llist_val(iter);
    for (iter2 = llist_iter(rh->members); iter2 ; iter2 = llist_next(iter2)) {
      cur = llist_val(iter2);
      if (cur->user_id == ctx->my_ID)
        continue;
      remote.sin_addr = cur->external_ip;
      remote.sin_port = cur->external_port;
      gp2pp_send_hello_request(ctx->peersock, ctx->my_ID, &remote);
    }
  }
}


static int do_hello(void *privdata) {
  ghl_ctx_t *ctx = privdata;
  send_hello_to_all(ctx);  
  ctx->hello_timer = ghl_new_timer(time(NULL) + GP2PP_HELLO_INTERVAL, do_hello, privdata);
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

static int handle_peer_msg(int type, void *payload, int length, void *privdata, int user_id, struct sockaddr_in *remote) {
  gp2pp_hello_req_t *hello_req = payload;
  gp2pp_hello_rep_t *hello_rep = payload;
  gp2pp_udp_encap_t *udp_encap = payload;
  ghl_ctx_t *ctx = privdata;
  ghl_udp_encap_t udp_encap_ev;
  ghl_member_t *member = ghl_global_find_member(ctx, user_id);
  if (member == NULL) {
    printf("[GHL/ERR] Received message from unknown user_id %x\n", user_id);
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  switch(type) {
    case GP2PP_MSG_HELLO_REQ:
      IFDEBUG(printf("[GHL/DEBUG] Received HELLO request from %s\n", member->name));
      remote->sin_port = member->external_port;
      gp2pp_send_hello_reply(ctx->peersock, ctx->my_ID, user_id, remote);
      
      break;
    case GP2PP_MSG_HELLO_REP:
      IFDEBUG(printf("[GHL/DEBUG] Received HELLO reply from %s\n", member->name));
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
  printf("Room %x join timeouted.\n", rh->room_ID);
  join.result = -1;
  join.rh = rh;
  err |= (ghl_signal_event(rh->ctx, GHL_EV_ME_JOIN, &join) == -1);
  rh->timeout = NULL; /* prevent ghl_free_room from deleting the timer we are currently handling */
  err |= (ghl_free_room(rh) == -1);
  return err ? -1 : 0;
}


static int send_hello_to_members(ghl_rh_t *rh) {
  cell_t iter2;
  ghl_ctx_t *ctx = rh->ctx;
  ghl_member_t *cur;
  struct sockaddr_in remote;
  
  remote.sin_family = AF_INET;
  
  for (iter2 = llist_iter(rh->members); iter2 ; iter2 = llist_next(iter2)) {
    cur = llist_val(iter2);
    if (cur->user_id == ctx->my_ID)
      continue;
    remote.sin_addr = cur->external_ip;
    remote.sin_port = cur->external_port;
    gp2pp_send_hello_request(ctx->peersock, ctx->my_ID, &remote);
  }
}

static int handle_room_join(int type, void *payload, int length, void *privdata, void *roomdata) {
  gcrp_welcome_t *welcome = payload;
  gcrp_memberlist_t *memberlist = payload;
  ghl_me_join_t join;
  ghl_rh_t *rh = NULL;
  ghl_ctx_t *ctx = privdata;
  gcrp_member_t *member;
  int k;
  
  int i;
  
  switch(type) {
    case GCRP_MSG_WELCOME:
      rh = ghl_room_from_id(ctx, ghtonl(welcome->room_id));
      if (rh == NULL) {
        fprintf(stderr, "[GHL/WARN] Joined a room that we didn't ask to join (?!?) id=%x\n", ghtonl(welcome->room_id));
        return 0;
      }
      if (gcrp_tochar(rh->welcome, welcome->text, GCRP_MAX_MSGSIZE-1) == -1)
        fprintf(stderr, "[GHL/WARN] Welcome message decode failed\n");
      rh->got_welcome = 1;
      break;
    case GCRP_MSG_MEMBERS:
      IFDEBUG(printf("[GHL] Received members (%u members).\n", ghtonl(memberlist->num_members)));
      
      rh = ghl_room_from_id(ctx, ghtonl(memberlist->room_id));
      if (rh == NULL) {
        fprintf(stderr, "[GHL/WARN] Joined a room that we didn't ask to join (?!?)\n");
        return 0;
      }
  
      for (i = 0; i < ghtonl(memberlist->num_members); i++) {
        member = malloc(sizeof(gcrp_member_t)); 
        memcpy(member, memberlist->members + i, sizeof(gcrp_member_t));
        llist_add_head(rh->members, member);

        IFDEBUG(printf("[GHL] Room member: %s\n", member->name));
        
/*        printf("member %s PORT=%u ID=%x\n", member->name, htons(member->external_port), ghtonl(member->user_id)); */
        if (strcmp(ctx->myname, member->name) == 0)
          rh->me = member;
      }
      
      if (rh->me == NULL) {
        fprintf(stderr, "[GHL/ERROR] Joined a room, but we are not in the member list. Leaving now.\n");
        garena_errno = GARENA_ERR_PROTOCOL;
        llist_empty_val(rh->members);
        return -1;
      }
      ctx->my_ID = rh->me->user_id;
      rh->got_members = 1;
      break;  

    default:  
      garena_errno = GARENA_ERR_INVALID;
      return -1;
      break;
  }
  
  if (rh && rh->got_welcome && rh->got_members) {
    join.result = 0;
    join.rh = rh;
    ghl_free_timer(rh->timeout);
    send_hello_to_members(rh);
    return ghl_signal_event(ctx, GHL_EV_ME_JOIN, &join);
  }
  
  return 0;
}


static int handle_room_activity(int type, void *payload, int length, void *privdata, void *roomdata) {
  static char buf[GCRP_MAX_MSGSIZE];
  gcrp_join_t *join = payload;
  gcrp_part_t *part = payload;
  gcrp_talk_t *talk = payload;
  gcrp_togglevpn_t *togglevpn = payload;
  ghl_rh_t *rh = roomdata;
  ghl_ctx_t *ctx = privdata;
  gcrp_member_t *member;
  ghl_talk_t talk_ev;
  ghl_part_t part_ev;
  ghl_join_t join_ev;
  ghl_togglevpn_t togglevpn_ev;
  struct sockaddr_in remote;
  

  int i;
  
  switch(type) {
    case GCRP_MSG_JOIN:
      member = malloc(sizeof(gcrp_member_t));
      memcpy(member, join, sizeof(ghl_member_t));
      llist_add_head(rh->members, member);
      join_ev.rh = rh;
      join_ev.member = member;
      ghl_signal_event(ctx, GHL_EV_JOIN, &join_ev);
      if (member->user_id != ctx->my_ID) {
        remote.sin_family = AF_INET;
        remote.sin_port = member->external_port;
        remote.sin_addr = member->external_ip;
        gp2pp_send_hello_request(ctx->peersock, ctx->my_ID, &remote);
      }

      break;
    case GCRP_MSG_TALK:
      if (gcrp_tochar(buf, talk->text, (talk->length >> 1) + 1) == -1) {
        fprintf(stderr, "Failed to convert user message.\n");
      } else {
        member = ghl_member_from_id(rh, ghtonl(talk->user_id));
        if (member == NULL) {
          fprintf(stderr, "[GHL/WARN] Received message from an user not on the room\n");
        }
        talk_ev.rh = rh;
        talk_ev.member = member;
        talk_ev.text = buf;
        ghl_signal_event(ctx, GHL_EV_TALK, &talk_ev);
      }
;
      break;
    case GCRP_MSG_STARTVPN:
        member = ghl_member_from_id(rh, ghtonl(togglevpn->user_id));
        togglevpn_ev.rh = rh;
        togglevpn_ev.member = member;
        togglevpn_ev.vpn = 1;
        member->vpn = 1;
        ghl_signal_event(ctx, GHL_EV_TOGGLEVPN, &togglevpn_ev);
      break;
    case GCRP_MSG_STOPVPN:
        member = ghl_member_from_id(rh, ghtonl(togglevpn->user_id));
        togglevpn_ev.rh = rh;
        togglevpn_ev.member = member;
        togglevpn_ev.vpn = 0;
        member->vpn = 0;
        ghl_signal_event(ctx, GHL_EV_TOGGLEVPN, &togglevpn_ev);
      break;
    case GCRP_MSG_PART:
      member = ghl_member_from_id(rh, ghtonl(part->user_id));
      if (member == NULL) {
        fprintf(stderr, "[GHL/WARN] Received PART message from an user, but that user was not in the room.\n");
      } else {
        part_ev.member = member;
        part_ev.rh = rh;
        ghl_signal_event(ctx, GHL_EV_PART, &part_ev);
        llist_del_item(rh->members, member);
        free(member);
      }
      break;
    default:  
      garena_errno = GARENA_ERR_INVALID;
      return -1;
      break;
  }
  return 0;
}


ghl_ctx_t *ghl_new_ctx(char *name, char *password, int my_id, int server_ip, int server_port) {
  int i;
  struct sockaddr_in local;
  ghl_ctx_t *ctx = malloc(sizeof(ghl_ctx_t));
  if (ctx == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    return NULL;
  }
  ctx->rooms = NULL;
  ctx->gp2pp_htab = NULL;
  ctx->gcrp_htab = NULL;
  ctx->peersock = -1;
  ctx->my_ID = my_id;
  ctx->hello_timer = NULL;
    
  ctx->servsock = socket(PF_INET, SOCK_STREAM, 0);
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
  
  gp2pp_do_ip_lookup(ctx->peersock, my_id, server_ip, GP2PP_PORT);
  strncpy(ctx->myname, name, sizeof(ctx->myname)-1);
  ctx->rooms = llist_alloc();
  if (ctx->rooms == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    goto err;
  }
  
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
    
  if (gp2pp_register_handler(ctx->gp2pp_htab, GP2PP_MSG_UDP_ENCAP, handle_peer_msg, ctx) == -1)
    goto err;
  if (gp2pp_register_handler(ctx->gp2pp_htab, GP2PP_MSG_HELLO_REQ, handle_peer_msg, ctx) == -1)
    goto err;
  if (gp2pp_register_handler(ctx->gp2pp_htab, GP2PP_MSG_HELLO_REP, handle_peer_msg, ctx) == -1)
    goto err;

  if ((ctx->hello_timer = ghl_new_timer(time(NULL) + GP2PP_HELLO_INTERVAL, do_hello, ctx)) == NULL)
    goto err;

  return ctx;

err:
  if (ctx->gp2pp_htab)
    free(ctx->gp2pp_htab);
  if (ctx->gcrp_htab)
    free(ctx->gcrp_htab);
  if (ctx->servsock != -1)
    close(ctx->servsock);
  if (ctx->peersock != -1)
    close(ctx->peersock);
  if (ctx->rooms)
    llist_free(ctx->rooms);
  if (ctx->hello_timer)
    ghl_free_timer(ctx->hello_timer);
  free(ctx);
  return NULL;
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
  
  fsocket.sin_family = AF_INET;
  fsocket.sin_addr.s_addr = room_ip;
  fsocket.sin_port = htons(room_port);
  
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
  rh->room_ID = room_id;
  rh->me = NULL;
  
  rh->members = llist_alloc();
  if (rh->members == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    close(rh->roomsock);
    free(rh);
    return NULL;
  }
  rh->got_welcome = 0;
  rh->got_members = 0;
  llist_add_head(ctx->rooms, rh);
  rh->timeout = ghl_new_timer(time(NULL) + GHL_JOIN_WAIT, handle_room_join_timeout, rh);
  return(rh);
}

ghl_rh_t *ghl_room_from_id(ghl_ctx_t *ctx, int room_ID) {
  cell_t iter;
  ghl_rh_t *cur;
  for (iter = llist_iter(ctx->rooms); iter ; iter = llist_next(iter)) {
    cur = llist_val(iter);
    if (cur->room_ID == room_ID)
      return cur;
  }
  garena_errno = GARENA_ERR_NOTFOUND;
  return NULL;
}


ghl_member_t *ghl_member_from_id(ghl_rh_t *rh, int user_ID) {
  cell_t iter;
  ghl_member_t *cur;
  for (iter = llist_iter(rh->members); iter ; iter = llist_next(iter)) {
    cur = llist_val(iter);
    if (cur->user_id == user_ID)
      return cur;
  }
  garena_errno = GARENA_ERR_NOTFOUND;
  return NULL;
}


ghl_member_t *ghl_global_find_member(ghl_ctx_t *ctx, int user_ID) {
  cell_t iter, iter2;
  ghl_member_t *cur;
  ghl_rh_t *rh; 
  
  for (iter = llist_iter(ctx->rooms); iter; iter = llist_next(iter)) {
    rh = llist_val(iter);
    for (iter2 = llist_iter(rh->members); iter2 ; iter2 = llist_next(iter2)) {
      cur = llist_val(iter2);
      if (cur->user_id == user_ID)
        return cur;
    }
  }
  garena_errno = GARENA_ERR_NOTFOUND;
  return NULL;
}

 
static int ghl_free_room(ghl_rh_t *rh) {
  cell_t iter;
  close(rh->roomsock);
  
  for (iter = llist_iter(rh->members); iter; iter = llist_next(iter)) {
    free(llist_val(iter));
  }
  
  llist_free(rh->members);
  llist_del_item(rh->ctx->rooms, rh);
  ghl_free_timer(rh->timeout);
  free(rh);
  return 0;    
}

int ghl_leave_room(ghl_rh_t *rh) {
  if (gcrp_send_part(rh->roomsock, rh->ctx->my_ID) == -1) {
    return -1;
  }
  return ghl_free_room(rh);
}

int ghl_togglevpn(ghl_rh_t *rh, int vpn) {
  return gcrp_send_togglevpn(rh->roomsock, rh->ctx->my_ID, vpn);
}

int ghl_talk(ghl_rh_t *rh, char *text) {
  return gcrp_send_talk(rh->roomsock, rh->room_ID, rh->ctx->my_ID, text);  
}

int ghl_udp_encap(ghl_ctx_t *ctx, ghl_member_t *member, int sport, int dport, char *payload, int length) {
  struct sockaddr_in fsocket;
  
  fsocket.sin_family = AF_INET;
  fsocket.sin_port = member->external_port;
  fsocket.sin_addr = member->external_ip;
  
  gp2pp_send_udp_encap(ctx->peersock, ctx->my_ID, sport, dport, payload, length, &fsocket);
}

int ghl_fill_fds(ghl_ctx_t *ctx, fd_set *fds) {
  cell_t iter;
  cell_t iter2;
  int max = -1;
  for (iter = llist_iter(ctx->rooms); iter ; iter = llist_next(iter)) {
    ghl_rh_t *rh = llist_val(iter);
    FD_SET(rh->roomsock, fds);
    if (rh->roomsock > max)
      max = rh->roomsock;
  }
  FD_SET(ctx->peersock, fds);
  if (ctx->peersock > max)
    max = ctx->peersock;
  /* FD_SET(ctx->servsock, fds); */
  return max;
}


/**
 * Fills tv with the time remaining until next timer event
 *
 * @param tv struct timeval to fill
 * @return 0 if no next timer is found, 1 otherwise
 */
 
int ghl_next_timer(struct timeval *tv) {
  ghl_timer_t *next;
  int now = time(NULL);
  tv->tv_sec = 0;
  tv->tv_usec = 0;
  next = llist_head(timers);
  if (next) {
    tv->tv_sec = (next->when) - now;
    return 1;
  }
  printf("there is no timers!\n");
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
  
  /* process timers */
  do {
    stuff_to_do = 0;
    for (iter = llist_iter(timers); iter; iter = llist_next(iter)) {
      cur = llist_val(iter);
      if (now >= cur->when) {
        if (cur->fun(cur->privdata) == -1) {
          perror("[GHL/ERR] a timer was not handled correctly");
        }
        llist_del_item(timers, cur);
        free(cur);
        stuff_to_do=1;
        break;
      }
    }
  } while (stuff_to_do);
  
  /* process network activity */
  if (fds == NULL) {
    fds = &myfds;
    FD_ZERO(&myfds);
    if ((r = ghl_fill_fds(ctx, &myfds)) == -1) {
      /* it would be stupid to use select() on an empty fd_set, so this situation is an error */
      garena_errno = GARENA_ERR_INVALID;
      return -1;
    }
    if (ghl_next_timer(&tv)) {
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
  
  for (iter = llist_iter(ctx->rooms); iter ; iter = llist_next(iter)) {
    ghl_rh_t *rh = llist_val(iter);
    if (FD_ISSET(rh->roomsock, fds)) {
      r = gcrp_read(rh->roomsock, buf, GCRP_MAX_MSGSIZE);
      if (r != -1) {
        gcrp_input(ctx->gcrp_htab, buf, r, rh);
      }
    }
  }
  if (FD_ISSET(ctx->peersock, fds)) {
    r = gp2pp_read(ctx->peersock, buf, GCRP_MAX_MSGSIZE, &remote);
    if (r != -1) {
      gp2pp_input(ctx->gp2pp_htab, buf, r, &remote);
    }
  }
  /* FD_SET(ctx->servsock, fds); */
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
 
int ghl_register_handler(ghl_ctx_t *ctx, int event, ghl_funptr_t *fun, void *privdata) {
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
  llist_add_before(timers, cur, tmp);
  return tmp;
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
  llist_del_item(timers, timer);
  free(timer);
}


void ghl_free_ctx(ghl_ctx_t *ctx) {
  if (!ctx)
    return;
  /* free all rooms */
  while (!llist_is_empty(ctx->rooms)) {
    ghl_free_room(llist_head(ctx->rooms));
  }
  llist_free(ctx->rooms);
  free(ctx->gcrp_htab);
  free(ctx->gp2pp_htab);
  free(ctx);
}