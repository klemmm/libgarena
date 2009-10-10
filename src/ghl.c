
/**
 * @file
 *
 * This file provides a Garena High-Level abstraction to
 * the client, hiding details of the protocol implementation.
 * The function in this file uses functions from GSP, GP2PP
 * and GCRP to manage details of the protocol.
 */
 
/* includes */

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
#include <errno.h>

/* documentation */


/* static globals */

static llist_t timers;

/* static (private) functions declarations */
static int ghl_free_room(ghl_room_t *rh);
static int insert_pkt(llist_t list, ghl_ch_pkt_t *pkt);
static void conn_free(ghl_ch_t *ch);
static void xmit_packet(ghl_serv_t *serv, ghl_ch_pkt_t *pkt);
static void myinfo_pack(gsp_myinfo_t *dst, ghl_myinfo_t *src);
static void myinfo_extract(ghl_myinfo_t *dst, gsp_myinfo_t *src);
static void member_extract(ghl_member_t *dst, gcrp_member_t *src);
static void send_hello(ghl_serv_t *serv, ghl_member_t *cur);
static void send_hello_to_all(ghl_serv_t *serv);
static int handle_servconn_timeout(void *privdata);
static int do_conn_retrans(void *privdata);
static void do_fast_retrans(ghl_serv_t *serv, llist_t sendq, int up_to);
static int do_hello(void *privdata);
static int do_roominfo_query(void *privdata);
static int signal_event(ghl_serv_t *serv, int event, void *eventparam);
static int handle_auth(int type, void *payload, unsigned int length, void *privdata);
static int handle_ip_lookup(int type, void *payload, unsigned int length, void *privdata, unsigned int user_id, struct sockaddr_in *remote);
static int handle_roominfo(int type, void *payload, unsigned int length, void *privdata, unsigned int user_id, struct sockaddr_in *remote);
static int handle_initconn_msg(int type, void *payload, unsigned int length, void *privdata, unsigned int user_id, struct sockaddr_in *remote);
static void update_next(ghl_serv_t *serv, ghl_ch_t *ch);
static void try_deliver(ghl_serv_t *serv, ghl_ch_t *conn);
static int handle_conn_fin_msg(int subtype, void *payload, unsigned int length, void *privdata, unsigned int user_id, unsigned int conn_id, int seq1, int seq2, int ts_rel, struct sockaddr_in *remote);
static int handle_conn_ack_msg(int subtype, void *payload, unsigned int length, void *privdata, unsigned int user_id, unsigned int conn_id, int seq1, int seq2, int ts_rel, struct sockaddr_in *remote);
static int handle_conn_data_msg(int subtype, void *payload, unsigned int length, void *privdata, unsigned int user_id, unsigned int conn_id, int seq1, int seq2, int ts_rel, struct sockaddr_in *remote);
static int handle_peer_msg(int type, void *payload, unsigned int length, void *privdata, unsigned int user_id, struct sockaddr_in *remote);
static int handle_room_join_timeout(void *privdata);
static void send_hello_to_members(ghl_room_t *rh);
static int handle_room_join(int type, void *payload, unsigned int length, void *privdata, void *roomdata);
static int handle_room_activity(int type, void *payload, unsigned int length, void *privdata, void *roomdata);
static void update_rto(gtime_t rtt, ghl_ch_t *ch);


/* API (public) functions defitinions */


/**
 * Get the number of users on a room.
 *
 * @param serv The server handle
 * @param room_id The room ID
 * @return Number of users, or -1 for error.
 */
int ghl_num_members(ghl_serv_t *serv, unsigned int room_id) {
  int *num_members = ihash_get(serv->roominfo, room_id);
  if (num_members == NULL) {
    garena_errno = GARENA_ERR_NOTFOUND;
    return -1;
  }
  return *num_members;
}

/**
 * Free the GHL part of the library.
 * Called by garena_fini(), should not be called directly.
 */
void ghl_fini(void) {
  if (timers != NULL)
    llist_free_val(timers);
}

/**
 * Initializes the GHL part of the library.
 * It is called automatically by garena_init() and should not be called
   directly by the user.
 */

int ghl_init(void) {
  timers = llist_alloc();
  if (timers == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    return -1;
  }
  return 0;
}



/**
 * Connects to the garena server, and return a newly allocated handle to the server.
 * The server connect operation is non-blocking, a GHL_EV_SERVCONN event will be
 * sent to signal the end of the operation.
 *
 * @par Errors
 *
 * @li GARENA_ERR_NORESOURCE: A resource allocation failed.
 * @li GARENA_ERR_LIBC: A socket operation failed (consult errno for details)
 * 
 * @param name Account user name (case insensitive)
 * @param password Account password
 * @param server_ip Server IP address
 * @param server_port Server GSP port (if 0, defaults to 7456)
 * @param gp2pp_lport Server GP2PP local port (if 0, defaults to 1513)
 * @param gp2pp_rport Server GP2PP remote port (if 0, defaults to 1513)
 * @param mtu Link MTU (if 0, defaults to 1500)
 * @returns Pointer to the new server handle, or NULL in case of error
 */
 
ghl_serv_t *ghl_new_serv(char *name, char *password, int server_ip, int server_port, int gp2pp_lport, int gp2pp_rport, int mtu) {
  int i;
  unsigned int local_len = sizeof(struct sockaddr_in);
  MHASH mh;
  struct sockaddr_in local;
  struct sockaddr_in fsocket;
  ghl_serv_t *serv = malloc(sizeof(ghl_serv_t));
  if (serv == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    return NULL;
  }
  serv->room = NULL;
  serv->gp2pp_htab = NULL;
  serv->gcrp_htab = NULL;
  serv->gsp_htab = NULL;
  serv->peersock = -1;
  serv->gp2pp_lport = gp2pp_lport ? gp2pp_lport : GP2PP_PORT;
  serv->gp2pp_rport = gp2pp_rport ? gp2pp_rport : GP2PP_PORT;
  serv->mtu = mtu ? mtu : GP2PP_DEFAULT_MTU;
  serv->hello_timer = NULL;
  serv->roominfo_timer = NULL;
  serv->conn_retrans_timer = NULL;
  serv->auth_ok = 0;
  serv->need_free = 0;
  serv->lookup_ok = 0;
  serv->connected = 0;
  serv->server_ip = server_ip;
  serv->roominfo = NULL;  
  serv->servsock = socket(PF_INET, SOCK_STREAM, 0);
  memset(&serv->my_info, 0, sizeof(serv->my_info));
  serv->roominfo = ihash_init();
  if (serv->roominfo == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    goto err;
  }
  
  if (serv->servsock == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }

  serv->peersock = socket(PF_INET, SOCK_DGRAM, 0);
  if (serv->peersock == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }
  local.sin_family = AF_INET;
  local.sin_port = htons(serv->gp2pp_lport); 
  local.sin_addr.s_addr = INADDR_ANY;
  
  if (bind(serv->peersock, (struct sockaddr *) &local, sizeof(local)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }

  fsocket.sin_family = AF_INET;
  fsocket.sin_port = htons(serv->gp2pp_rport);
  fsocket.sin_addr.s_addr = server_ip;
  if (connect(serv->peersock, (struct sockaddr *) &fsocket, sizeof(fsocket)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }
  if (getsockname(serv->peersock, (struct sockaddr *) &local, &local_len) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }
  serv->my_info.internal_ip = local.sin_addr;
  serv->my_info.internal_port = htons(local.sin_port);
  
  close(serv->peersock);
  serv->peersock = socket(PF_INET, SOCK_DGRAM, 0);
  if (serv->peersock == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }
  
  local.sin_family = AF_INET;
  local.sin_port = htons(serv->gp2pp_lport); 
  local.sin_addr.s_addr = INADDR_ANY;
  
  if (bind(serv->peersock, (struct sockaddr *) &local, sizeof(local)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }

  fsocket.sin_family = AF_INET;
  fsocket.sin_port = server_port ? htons(server_port) : htons(GSP_PORT);
  fsocket.sin_addr.s_addr = server_ip;
  if (connect(serv->servsock, (struct sockaddr *) &fsocket, sizeof(fsocket)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    goto err;
  }
  if (gsp_open_session(serv->servsock, serv->session_key, serv->session_iv) == -1)
    goto err;
  if (gsp_send_hello(serv->servsock, serv->session_key, serv->session_iv) == -1)
    goto err;
  if (gp2pp_do_ip_lookup(serv->peersock, serv->server_ip, GP2PP_PORT) == -1)
    goto err;

  mh = mhash_init(MHASH_MD5);
  if (mh == NULL)
    goto err;
  
  mhash(mh, password, strlen(password));
  mhash_deinit(mh, &serv->md5pass);
  
  if (gsp_send_login(serv->servsock, name, serv->md5pass, serv->session_key, serv->session_iv, serv->my_info.internal_ip.s_addr, serv->my_info.internal_port) == -1)
    goto err;
  
  serv->room = NULL;
  
  for (i = 0 ; i < GHL_EV_NUM; i++) {
    serv->ghl_handlers[i].fun = NULL;
    serv->ghl_handlers[i].privdata = NULL;
  }
  serv->gcrp_htab = gcrp_alloc_handtab();
  if (serv->gcrp_htab == NULL)
    goto err;
  serv->gp2pp_htab = gp2pp_alloc_handtab();
  if (serv->gp2pp_htab == NULL)
    goto err;
  serv->gsp_htab = gsp_alloc_handtab();
  if (serv->gsp_htab == NULL)
    goto err;
  
  /* GSP handlers */
  if (gsp_register_handler(serv->gsp_htab, GSP_MSG_LOGIN_REPLY, handle_auth, serv) == -1)
    goto err;
  if (gsp_register_handler(serv->gsp_htab, GSP_MSG_AUTH_FAIL, handle_auth, serv) == -1)
    goto err;
  
  /* GCRP handlers */
  if (gcrp_register_handler(serv->gcrp_htab, GCRP_MSG_WELCOME, handle_room_join, serv) == -1)
    goto err;
  if (gcrp_register_handler(serv->gcrp_htab, GCRP_MSG_MEMBERS, handle_room_join, serv) == -1)
    goto err;
  if (gcrp_register_handler(serv->gcrp_htab, GCRP_MSG_JOIN, handle_room_activity, serv) == -1)
    goto err;
  if (gcrp_register_handler(serv->gcrp_htab, GCRP_MSG_PART, handle_room_activity, serv) == -1)
    goto err;
  if (gcrp_register_handler(serv->gcrp_htab, GCRP_MSG_TALK, handle_room_activity, serv) == -1)
    goto err;
  if (gcrp_register_handler(serv->gcrp_htab, GCRP_MSG_STARTVPN, handle_room_activity, serv) == -1)
    goto err;
  if (gcrp_register_handler(serv->gcrp_htab, GCRP_MSG_STOPVPN, handle_room_activity, serv) == -1)
    goto err;
  if (gcrp_register_handler(serv->gcrp_htab, GCRP_MSG_JOIN_FAILED, handle_room_join, serv) == -1)
    goto err;
    
    /* GP2PP handlers */
  if (gp2pp_register_handler(serv->gp2pp_htab, GP2PP_MSG_UDP_ENCAP, handle_peer_msg, serv) == -1)
    goto err;
  if (gp2pp_register_handler(serv->gp2pp_htab, GP2PP_MSG_HELLO_REQ, handle_peer_msg, serv) == -1)
    goto err;
  if (gp2pp_register_handler(serv->gp2pp_htab, GP2PP_MSG_HELLO_REP, handle_peer_msg, serv) == -1)
    goto err;
  if (gp2pp_register_handler(serv->gp2pp_htab, GP2PP_MSG_INITCONN, handle_initconn_msg, serv) == -1)
    goto err;
  if (gp2pp_register_handler(serv->gp2pp_htab, GP2PP_MSG_ROOMINFO_REPLY, handle_roominfo, serv) == -1)
    goto err;
  if (gp2pp_register_handler(serv->gp2pp_htab, GP2PP_MSG_IP_LOOKUP_REPLY, handle_ip_lookup, serv) == -1)
    goto err;

  /* GP2PP CONN handlers */

  if (gp2pp_register_conn_handler(serv->gp2pp_htab, GP2PP_CONN_MSG_DATA, handle_conn_data_msg, serv) == -1)
    goto err;
  if (gp2pp_register_conn_handler(serv->gp2pp_htab, GP2PP_CONN_MSG_FIN, handle_conn_fin_msg, serv) == -1)
    goto err;
  if (gp2pp_register_conn_handler(serv->gp2pp_htab, GP2PP_CONN_MSG_ACK, handle_conn_ack_msg, serv) == -1)
    goto err;

  /* timers handlers */
  if ((serv->hello_timer = ghl_new_timer(garena_now() + GP2PP_HELLO_INTERVAL, do_hello, serv)) == NULL)
    goto err;
  if ((serv->conn_retrans_timer = ghl_new_timer(garena_now() + GP2PP_CONN_RETRANS_CHECK, do_conn_retrans, serv)) == NULL)
    goto err;
  if ((serv->roominfo_timer = ghl_new_timer(garena_now() + GHL_ROOMINFO_QUERY_INTERVAL, do_roominfo_query, serv)) == NULL)
    goto err;
  if ((serv->servconn_timeout = ghl_new_timer(garena_now() + GHL_SERVCONN_TIMEOUT, handle_servconn_timeout, serv)) == NULL)
    goto err;
    
  return serv;

err:
  if (serv->gp2pp_htab)
    free(serv->gp2pp_htab);
  if (serv->gcrp_htab)
    free(serv->gcrp_htab);
  if (serv->gsp_htab)
    free(serv->gsp_htab);
  if (serv->servsock != -1)
    close(serv->servsock);
  if (serv->peersock != -1)
    close(serv->peersock);
  if (serv->hello_timer)
    ghl_free_timer(serv->hello_timer);
  if (serv->conn_retrans_timer)
    ghl_free_timer(serv->conn_retrans_timer);
  if (serv->roominfo_timer)
    ghl_free_timer(serv->roominfo_timer);
  if (serv->servconn_timeout)
    ghl_free_timer(serv->servconn_timeout);
  if (serv->roominfo)
    ihash_free_val(serv->roominfo);
  free(serv);
  return NULL;
}


/**
 * Joins a garena room, and returns a newly allocated handle for this room.
 * The room join operation is non-blocking, a GHL_EV_ME_JOIN event will
 * be sent to signal the end of the operation.
 *
 * @par Errors
 *
 * @li GHL_ERR_INVALID: The server handle is not connected to the Garena server
 * @li GHL_ERR_INUSE: Already in a room (you can be in only one room at a time)
 * @li GHL_ERR_NORESOURCE: A resource allocation failed
 * @li GHL_ERR_LIBC: A socket operation failed (consult errno for details)
 *
 * @param serv The handle to the garena server. 
 * @param room_ip The IP of the room server.
 * @param room_port The GCRP port of the room server (if 0, defaults to 8687)
 * @param room_id The numerical ID of the room.
 * @return Pointer to the room handle, or NULL in case of error.
 */
ghl_room_t *ghl_join_room(ghl_serv_t *serv, int room_ip, int room_port, unsigned int room_id) {
  struct sockaddr_in fsocket;
  gcrp_join_block_t join_block;
  ghl_room_t *rh;
  
  if (room_port == 0)
    room_port = GCRP_PORT;  
  if (serv->connected == 0) {
    garena_errno = GARENA_ERR_INVALID;
    return NULL;
  }
  
  if (serv->room != NULL) {
    garena_errno = GARENA_ERR_INUSE;
    return NULL;
  }
  rh = malloc(sizeof(ghl_room_t));
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
  
  myinfo_pack(&join_block, &serv->my_info);
  if (gcrp_send_join(rh->roomsock, room_id, &join_block, serv->md5pass) == -1) {
    close(rh->roomsock);
    free(rh);
    return NULL;
  }

  rh->serv = serv;
  rh->joined = 0;
  rh->room_id = room_id;
  rh->me = NULL;
  
  rh->members = ihash_init();
  if (rh->members == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    close(rh->roomsock);
    free(rh);
    return NULL;
  }
  rh->conns = ihash_init();
  if (rh->conns == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    close(rh->roomsock);
    free(rh->members);
    free(rh);
    return NULL;
  }
  rh->got_welcome = 0;
  rh->got_members = 0;
  serv->room = rh;
  fflush(deb);
  rh->timeout = ghl_new_timer(garena_now() + GHL_JOIN_TIMEOUT, handle_room_join_timeout, rh);
  return(rh);
}

/**
 * Search a member in the room, given his user ID.
 *
 * @par Errors
 *
 * @li GARENA_ERR_NOTFOUND: The member was not found
 *
 * @param rh The handle of the room we are searching in. 
 * @param user_id The user ID
 * @return Pointer to the member object, or NULL in case of error.
 */
ghl_member_t *ghl_member_from_id(ghl_room_t *rh, unsigned int user_id) {
  ghl_member_t *member;
  member = ihash_get(rh->members, user_id);
  if (member == NULL)
    garena_errno = GARENA_ERR_NOTFOUND;
  return member;
}

/**
 * Find a virtual connection from the connection ID.
 *
 * @par Errors
 *
 * @li GARENA_ERR_NOTFOUND: No connection exists with such ID
 *
 * @param rh The handle of the room to which the connection belongs
 * @param conn_id The connection ID
 * @param Pointer to the connection handle, or NULL in case of error.
 */
ghl_ch_t *ghl_conn_from_id(ghl_room_t *rh, unsigned int conn_id) {
  ghl_ch_t *conn;
  conn = ihash_get(rh->conns, conn_id);
  if (conn == NULL)
    garena_errno = GARENA_ERR_NOTFOUND;
  return conn;
}

 
/**
 * Leave a garena room.
 *
 * @par Errors
 *
 * @li GARENA_ERR_LIBC: A socket error occured (use errno for details)
 *
 * @param rh The handle of the room to leave
 * @return 0 for success, -1 for failure
 */
int ghl_leave_room(ghl_room_t *rh) {
  if (gcrp_send_part(rh->roomsock, rh->serv->my_info.user_id) == -1) {
    return -1;
  }
  return ghl_free_room(rh);
}

/**
 *
 * Enable or disable the VPN.
 *
 * @par Errors
 *
 * @li GARENA_ERR_LIBC: A socket error occured (use errno for details)
 *
 * @param rh The handle of the room associated with the VPN
 * @param vpn Set to 1 for enabling the VPN, set to 0 for disabling.
 * @return 0 for success, -1 for failure.
 */

int ghl_togglevpn(ghl_room_t *rh, int vpn) {
  return gcrp_send_togglevpn(rh->roomsock, rh->serv->my_info.user_id, vpn);
}

/**
 *
 * Send a text message to a room.
 *
 * @par Errors
 *
 * @li GARENA_ERR_LIBC: A socket error occured (use errno for details)
 *
 * @param rh The handle of the room
 * @param text The text to send
 * @return 0 for success, -1 for failure.
 */
int ghl_talk(ghl_room_t *rh, char *text) {
  return gcrp_send_talk(rh->roomsock, rh->room_id, rh->serv->my_info.user_id, text);  
}

/**
 *
 * Send a virtual UDP packet to another member.
 *
 * @par Errors
 *
 * @li GARENA_ERR_LIBC: A socket error occured (use errno for details)
 *
 * @param serv The server handle
 * @param member The destination member
 * @param sport The UDP source port
 * @param dport The UDP destination port
 * @param payload The UDP packet payload
 * @param length The UDP packet payload size
 * @return 0 for success, -1 for failure.
 */

int ghl_udp_encap(ghl_serv_t *serv, ghl_member_t *member, int sport, int dport, char *payload, unsigned int length) {
  struct sockaddr_in fsocket;
  
  if (member->conn_ok == 0) {
    garena_errno = GARENA_ERR_AGAIN;
    return -1;
  }  
  
  fsocket.sin_family = AF_INET;
  fsocket.sin_port = htons(member->effective_port);
  fsocket.sin_addr = member->effective_ip;
  
  return gp2pp_send_udp_encap(serv->peersock, serv->my_info.user_id, sport, dport, payload, length, &fsocket);
}

/**
 *
 * Get the list of file descriptors used by the garena library, 
 * and that needs to be monitored for activity.
 *
 * @param serv The server handle
 * @param fds The fd_set in which we want to store the file descriptor list
 * @return 0 for success, -1 for failure
 */
int ghl_fill_fds(ghl_serv_t *serv, fd_set *fds) {
  int max = -1;
  
  if (serv->room) {
    FD_SET(serv->room->roomsock, fds);
    if (serv->room->roomsock > max)
      max = serv->room->roomsock;
  }

  FD_SET(serv->peersock, fds);
  if (serv->peersock > max)
    max = serv->peersock;
  if (serv->servsock != -1) {
    FD_SET(serv->servsock, fds);
    if (serv->servsock > max)
      max = serv->servsock;
  }
  return max;
}


/**
 * Fills tv with the time remaining until next timer event
 *
 * @param tv struct timeval to fill
 * @return 0 if no next timer is found, 1 otherwise
 */
 
int ghl_fill_tv(ghl_serv_t *serv, struct timeval *tv) {
  ghl_timer_t *next;
  int now = garena_now();
  tv->tv_sec = 0;
  tv->tv_usec = 0;
  next = llist_head(timers);
  if (next) {
    tv->tv_sec = (next->when > now) ? ((next->when) - now) : 0;
    return 1;
  }
  return 0;
}

/**
 * Process the next garena event (timer expiration or network activity, whatever comes first) 
 * This function exists in two modes, blocking and non-blocking. The mode of operation depends on the value of the fds parameter.
 * If fds is NULL, ghl_process() will operate in blocking mode. It will block until one or more garena events occurs, process these events, and return.
 * If fds is not NULL, ghl_process() will read from the file descriptors specified in fds, then process the occuring garena events, and then returns.
 * In the non-blocking mode, all the file descriptors specified in fds must be available for reading. Otherwise, the behavior of ghl_process() is not determined. 
 *
 * @par Errors
 * 
 * @li GARENA_ERR_PROTOCOL: A protocol error occured
 *
 * @param serv The server handle.
 * @param fds The file descriptor set. 
 * @return 0 for success, -1 for failure.
 */
int ghl_process(ghl_serv_t *serv, fd_set *fds) {
  char buf[GCRP_MAX_MSGSIZE];
  int r;
  fd_set myfds;
  struct sockaddr_in remote;
  cell_t iter;
  struct timeval tv;
  int stuff_to_do; 
  int now = garena_now();
  ghl_timer_t *cur;
  ghl_room_disc_t room_disc_ev;
  ghl_me_join_t join;  
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
        if (serv->need_free) {
          garena_errno = GARENA_ERR_PROTOCOL;
          ghl_free_serv(serv);
          return -1;
        }
        stuff_to_do=1;
        /* XXX FIXME */
        break;
      }
    }
  } while (stuff_to_do);
  

  /* process network activity */
  if (fds == NULL) {
    fds = &myfds;
    FD_ZERO(&myfds);
    if (ghl_fill_tv(serv, &tv)) {
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
  
  
  if (serv->room && FD_ISSET(serv->room->roomsock, fds)) {
    r = gcrp_read(serv->room->roomsock, buf, GCRP_MAX_MSGSIZE);
    if (r != -1) {
      gcrp_input(serv->gcrp_htab, buf, r, serv->room);
    } else if ((errno != EINTR) && (errno != EAGAIN)) {
      if (serv->room->joined) {
        room_disc_ev.rh = serv->room;
        signal_event(serv, GHL_EV_ROOM_DISC, &room_disc_ev);
        ghl_free_room(serv->room);
        serv->room = NULL;
      } else {
        join.result = GHL_EV_RES_FAILURE;
        join.rh = serv->room;
        ghl_free_timer(serv->room->timeout);
        serv->room->timeout = NULL;
        signal_event(serv, GHL_EV_ME_JOIN, &join);
        ghl_free_room(serv->room);
      }
    }
  }


  if (FD_ISSET(serv->peersock, fds)) {
    r = gp2pp_read(serv->peersock, buf, GCRP_MAX_MSGSIZE, &remote);
    if (r != -1) {
      gp2pp_input(serv->gp2pp_htab, buf, r, &remote);
    } 
  }
  if ((serv->servsock != -1) && FD_ISSET(serv->servsock, fds)) {
    r = gsp_read(serv->servsock, buf, GSP_MAX_MSGSIZE);
    if (r != -1) {
      gsp_input(serv->gsp_htab, buf, r, serv->session_key, serv->session_iv);
    } else {
      fprintf(deb, "[WARN/GHL] Disconnected from main server, but we don't care\n");
      fflush(deb);
      close(serv->servsock);
      serv->servsock = -1;
    }
  }

  return 0;
}



/**
 * Register a handler to be called on the specified event
 *
 * @par Errors
 *
 * @li GARENA_ERR_INVALID: You attempted to set a handler for an invalid event.
 * @li GARENA_ERR_INUSE: The event has already a handler.
 *
 * @param msgtype The event for which we define a handler
 * @param fun Pointer to handler function
 * @param privdata Pointer to private data (anything you want) that will be supplied to the called function
 * @return 0 for success, -1 for failure
 */
 
int ghl_register_handler(ghl_serv_t *serv, int event, ghl_fun_t *fun, void *privdata) {
  if ((event < 0) || (event >= GHL_EV_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  
  if (serv->ghl_handlers[event].fun != NULL) {
    garena_errno = GARENA_ERR_INUSE;
    return -1;
  }
  serv->ghl_handlers[event].fun = fun;
  serv->ghl_handlers[event].privdata = privdata;
  return 0;
}

/**
 * Add a new timer.
 *
 * @par Errors
 *
 * @li GARENA_ERR_NORESOURCE: A resource allocation faileD.
 *
 * @param when When the timer should expire (in seconds since the Epoch)
 * @param fun Pointer to the function to call on timer expiration
 * @param privdata Pointer to private data to pass to the handler function.
 * @return Pointer to the newly allocated timer, or NULL for error.
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


/**
 * Unregisters a handler associated with the specified event
 *
 * @par Errors
 *
 * @li GARENA_ERR_INVALID: The specified event is not valid.
 * @li GARENA_ERR_NOTFOUND: The specified event has no handler.
 *
 * @param event The message type for which we delete the handler
 * @return 0 for success, -1 for failure
 */
int ghl_unregister_handler(ghl_serv_t *serv, int event) {
  if ((event < 0) || (event >= GHL_EV_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  if (serv->ghl_handlers[event].fun == NULL) {
    garena_errno = GARENA_ERR_NOTFOUND;
    return -1;
  }
  serv->ghl_handlers[event].fun = NULL;
  serv->ghl_handlers[event].privdata = NULL;  
  return 0;
}


/**
 * Get the privdata associated with a handler.
 *
 * @par Errors
 *
 * @li GARENA_ERR_INVALID: The specified event is not valid.
 * @li GARENA_ERR_NOTFOUND: The specified event has no handler.
 *
 * @param event The message type of the handler we wish to retrieve the privdata
 * @return The privdata, or NULL if there is an error
 */
void* ghl_handler_privdata(ghl_serv_t *serv, int event) {
  if ((event < 0) || (event >= GHL_EV_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return NULL;
  }
  if (serv->ghl_handlers[event].fun == NULL) {
    garena_errno = GARENA_ERR_NOTFOUND;
    return NULL;
  }
 return serv->ghl_handlers[event].privdata;
}


/**
 *
 * Free (and disable) a timer. This is useful only for timer that have not expired yet.
 * Expired timers are free'd automatically.
 *
 * @param timer The timer to free.
 *
 */
void ghl_free_timer(ghl_timer_t *timer) {
  if (timer == NULL)
    return;
  llist_del_item(timers, timer);
  free(timer);
}

/**
 *
 * Free a server handle, and disconnect from the server.
 *
 * @param serv The server handle to free.
 */
void ghl_free_serv(ghl_serv_t *serv) {
  if (!serv)
    return;
  /* free all rooms */
  if (serv->room)
    ghl_free_room(serv->room);
  close(serv->peersock);
  if (serv->servsock != -1)
    close(serv->servsock);
  free(serv->gcrp_htab);
  free(serv->gp2pp_htab);
  free(serv->gsp_htab);
  if (serv->hello_timer)
    ghl_free_timer(serv->hello_timer);
  if (serv->conn_retrans_timer)
    ghl_free_timer(serv->conn_retrans_timer);
  if (serv->roominfo_timer)
    ghl_free_timer(serv->roominfo_timer);
  if (serv->servconn_timeout)
    ghl_free_timer(serv->servconn_timeout);
  if (serv->roominfo)
    ihash_free_val(serv->roominfo);
  free(serv);
}

/**
 *
 * Opens a virtual connection with a member.
 * The source port is not preserved since there is no way to send this information to the peer.
 * Opening a connection is an unconfirmed operation in the garena protocol.
 *
 * @par Errors
 *
 * @li GARENA_ERR_AGAIN: The GP2PP connection with the member is not yet established, try again later.
 * @li GARENA_ERR_INVALID: You are not in a room (this is required to open a connection)
 * @li GARENA_ERR_NORESOURCE: A resource allocation failed.
 *
 * @param serv The server handle.
 * @param member The destination member.
 * @param port The destination port.
 * @return Pointer to the connection handle, or NULL for error.
 */
ghl_ch_t *ghl_conn_connect(ghl_serv_t *serv, ghl_member_t *member, int port) {
  struct sockaddr_in remote;
  ghl_ch_t *ch;
  ghl_room_t *rh = serv->room;
  
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
  ch->ts_base = garena_now();
  ch->sendq = llist_alloc();
  ch->recvq = llist_alloc();
  ch->serv = serv;
  ch->snd_una = 0;
  ch->snd_next = 0;
  ch->rcv_next = 0;
  ch->rto = GP2PP_INIT_RTO;
  ch->srtt = 0;
  ch->flightsize = 0;
  ch->ssthresh = GP2PP_INIT_SSTHRESH;
  ch->cwnd = (ghl_max_conn_pkt(serv) << 1); 
  ch->rcv_next_deliver = 0;
  ch->ts_ack = garena_now();
  ch->conn_id = gp2pp_new_conn_id();
  ihash_put(rh->conns, ch->conn_id, ch);
  

  remote.sin_family = AF_INET;
  remote.sin_addr = ch->member->effective_ip;
  remote.sin_port = htons(ch->member->effective_port);
  if (gp2pp_send_initconn(serv->peersock, serv->my_info.user_id, ch->conn_id, port, GP2PP_MAGIC_LOCALIP, &remote) == -1) {
    llist_free(ch->sendq);
    llist_free(ch->recvq);
    ihash_del(rh->conns, ch->conn_id);
    free(ch);
    return NULL;
  } else return ch;
}


/**
 * Close a virtual connection. The packets in the send queue will still be retransmitted, and
 * then the resources will be free'd.
 * 
 * @param serv The server handle
 * @param ch The connection handle to close
 */
void ghl_conn_close(ghl_serv_t *serv, ghl_ch_t *ch) {
  struct sockaddr_in remote;
  int i;
  if (ch->cstate == GHL_CSTATE_CLOSING_OUT)
    return;
  remote.sin_family = AF_INET;
  remote.sin_addr = ch->member->effective_ip;
  remote.sin_port = htons(ch->member->effective_port);
  for (i = 0; i < 4; i++) 
    gp2pp_output_conn(serv->peersock, GP2PP_CONN_MSG_FIN, NULL, 0, serv->my_info.user_id, ch->conn_id, ch->rcv_next, ch->rcv_next, 0, &remote);
  ch->cstate = GHL_CSTATE_CLOSING_OUT;

}

/**
 *
 * Sends a data segment on a virtual connection.
 *
 * @par Errors
 *
 * @li GARENA_ERR_INVALID: The connection is in closing state and will soon be destroyed, you may not send data through it
 * @li GARENA_ERR_AGAIN: The send queue is too large (usually due to network congestion), try again later
 * 
 * @parm serv The server handle
 * @param ch The connection handle
 * @param payload The segment payload
 * @param length The segment payload length
 * @return 0 for success, -1 for failure.
 */
int ghl_conn_send(ghl_serv_t *serv, ghl_ch_t *ch, char *payload, unsigned int length) {
  ghl_ch_pkt_t *pkt;
  int now = garena_now();
  if (ch->cstate == GHL_CSTATE_CLOSING_OUT) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  
  if ((ch->last_xmit + ch->rto) < now) {
    fprintf(deb, "[CC] Restart after idle...\n");
    fprintf(deb, "[CC] Mode: SLOW START\n");
  }
  
  fprintf(deb, "[CC] Flight size: %u\n", ch->flightsize);
  fflush(deb);
  
  if ((ch->snd_next - ch->snd_una) >= GP2PP_MAX_SENDQ) {
    garena_errno = GARENA_ERR_AGAIN;
    return -1;
  }
  
  pkt = malloc(sizeof(ghl_ch_pkt_t));
  pkt->length = length;
  pkt->payload = malloc(length);
  pkt->seq = ch->snd_next;
  pkt->ts_rel = (now - ch->ts_base)*40;
  pkt->ch = ch;
  pkt->xmit_ts = 0;
  pkt->partial = 0;
  pkt->retrans = 0;
  pkt->did_fast_retrans = 0;
  memcpy(pkt->payload, payload, length);
  
  ch->snd_next++;
  ch->ts_base = now;
  if (!llist_is_empty(ch->sendq)) {
    ch->ts_ack = garena_now();
  }
  insert_pkt(ch->sendq, pkt);
  if ((pkt->seq - ch->snd_una) < GP2PP_MAX_IN_TRANSIT) {
    /* initial transmit */
    pkt->rto = ch->rto;
    pkt->xmit_ts = garena_now();
    xmit_packet(serv, pkt);
    pkt->first_trans = garena_now();
    fprintf(deb, "[GHL] Initial packet transmit: seq=%x snd_una=%x\n", pkt->seq, ch->snd_una); 
    ch->flightsize += pkt->length;
  }
  return 0;
}

/**
 * Given the link MTU, returns the maximum size of virtual connection segments which may be sent 
 * without needing fragmentation. Depending on the network configuration, trying to send larget
 * segments may lead to lag, packet loss, disconnects, etc.
 *
 * @param serv The server handle
 * @return Maximum segment size.
 */
inline unsigned int ghl_max_conn_pkt(ghl_serv_t *serv) {
  /* FIXME: should query path-MTU */
  return (serv->mtu - sizeof(struct ip) - sizeof(struct udphdr) - sizeof(gp2pp_conn_hdr_t));
}


/* Static HELPER FUNCTIONS */


static void xmit_packet(ghl_serv_t *serv, ghl_ch_pkt_t *pkt) {
  struct sockaddr_in remote;
  remote.sin_family = AF_INET;
  remote.sin_addr = pkt->ch->member->effective_ip;
  remote.sin_port = htons(pkt->ch->member->effective_port);
  pkt->ch->last_xmit = garena_now();
  gp2pp_output_conn(serv->peersock, GP2PP_CONN_MSG_DATA, pkt->payload, pkt->length, serv->my_info.user_id, pkt->ch->conn_id, pkt->seq, pkt->ch->rcv_next, pkt->ts_rel, &remote);
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



static int ghl_free_room(ghl_room_t *rh) {
  ihashitem_t iter;
  ghl_ch_t *ch;
  ghl_conn_fin_t conn_fin_ev;
  close(rh->roomsock);
  
  for (iter = ihash_iter(rh->members); iter; iter = ihash_next(rh->members, iter)) {
    free(ihash_val(iter));
  }

  for (iter = ihash_iter(rh->conns); iter; iter = ihash_next(rh->conns, iter)) {
    ch = ihash_val(iter);
    if (ch->cstate != GHL_CSTATE_CLOSING_OUT) {
      conn_fin_ev.ch = ch;
      ch->cstate = GHL_CSTATE_CLOSING_OUT;
      signal_event(rh->serv, GHL_EV_CONN_FIN, &conn_fin_ev);
    }

    conn_free(ch);
    
  }
  
  ihash_free(rh->members);
  ihash_free(rh->conns);
  if (rh->timeout)
    ghl_free_timer(rh->timeout);
  rh->serv->room = NULL;
  free(rh);
  return 0;    
}

static void send_hello(ghl_serv_t *serv, ghl_member_t *cur) {
  struct sockaddr_in remote;
  remote.sin_family = AF_INET;
    if (cur->conn_ok > 0) {
      remote.sin_addr = cur->effective_ip;
      remote.sin_port = htons(cur->effective_port);
      gp2pp_send_hello_request(serv->peersock, serv->my_info.user_id, &remote);
    } else {
      remote.sin_addr = cur->external_ip;
      remote.sin_port = htons(cur->external_port);
      gp2pp_send_hello_request(serv->peersock, serv->my_info.user_id, &remote);
      remote.sin_addr = cur->internal_ip;
      remote.sin_port = htons(cur->internal_port);
      gp2pp_send_hello_request(serv->peersock, serv->my_info.user_id, &remote);
      if (cur->external_port != GP2PP_PORT) {
        remote.sin_addr = cur->external_ip;
        remote.sin_port = htons(GP2PP_PORT);
        gp2pp_send_hello_request(serv->peersock, serv->my_info.user_id, &remote);
      }
    }

}

static void send_hello_to_all(ghl_serv_t *serv) {
  ihashitem_t iter2;
  ghl_member_t *cur;
  ghl_room_t *rh = serv->room; 
  if (rh == NULL)
    return;
  
  
  for (iter2 = ihash_iter(rh->members); iter2 ; iter2 = ihash_next(rh->members, iter2)) {
    cur = ihash_val(iter2);
    if (cur->user_id == serv->my_info.user_id)
      continue;
    send_hello(serv, cur);
  }
}



static int signal_event(ghl_serv_t *serv, int event, void *eventparam) {
  if (serv->ghl_handlers[event].fun) {
    return serv->ghl_handlers[event].fun(serv, event, eventparam, serv->ghl_handlers[event].privdata);
  } else {
    IFDEBUG(printf("[GHL/DEBUG] Event %x was ignored.\n", event));
    return 0;
  }
}


static void update_next(ghl_serv_t *serv, ghl_ch_t *ch) {
  cell_t iter;
  ghl_ch_pkt_t *pkt;
  
  if (llist_is_empty(ch->recvq))
    return;
   
  for (iter = llist_iter(ch->recvq); iter; iter = llist_next(iter)) {
    pkt = llist_val(iter);
    if (pkt->seq > ch->rcv_next)
      break;
    if (pkt->seq == ch->rcv_next) {
      ch->rcv_next++;
    }
  }
}

static void try_deliver(ghl_serv_t *serv, ghl_ch_t *ch) {
  cell_t iter;
  ihashitem_t c_iter;
  ghl_conn_recv_t conn_recv_ev;
  ghl_conn_fin_t conn_fin_ev;
  ghl_ch_pkt_t *pkt;
  ghl_ch_pkt_t *todel = NULL;
  int r;
  

  if (llist_is_empty(ch->recvq))
    return;

  for (iter = llist_iter(ch->recvq); iter ; iter = llist_next(iter)) {
    pkt = llist_val(iter);
    if (todel) {
      llist_del_item(ch->recvq, todel);
      free(todel->payload);
      free(todel);
      todel = NULL;
    }
      
    if (pkt->seq != ch->rcv_next_deliver)
      break;
        
    if (pkt->length > 0) {
      conn_recv_ev.ch = ch;
      conn_recv_ev.payload = pkt->payload;
      conn_recv_ev.length = pkt->length;
      r = pkt->length;
      if (ch->cstate != GHL_CSTATE_CLOSING_OUT) {
        r = signal_event(serv, GHL_EV_CONN_RECV, &conn_recv_ev);
      }
            
      if (r == pkt->length) {
        todel = pkt;
        ch->rcv_next_deliver++;
      } else if (r != -1) {
        memmove(pkt->payload, pkt->payload + r, pkt->length - r);
        pkt->length -= r;
      }
    } else {
      conn_fin_ev.ch = ch;
      if (ch->cstate != GHL_CSTATE_CLOSING_OUT) {
        ch->cstate = GHL_CSTATE_CLOSING_OUT;
        signal_event(serv, GHL_EV_CONN_FIN, &conn_fin_ev);
      }
      todel = pkt;
      ch->rcv_next_deliver++;
    }
  }
  if (todel) {
    llist_del_item(ch->recvq, todel);
    free(todel->payload);
    free(todel);
    todel = NULL;
  }
  
}



static void send_hello_to_members(ghl_room_t *rh) {
  ihashitem_t iter2;
  ghl_serv_t *serv = rh->serv;
  ghl_member_t *cur;
  struct sockaddr_in remote;
  
  remote.sin_family = AF_INET;
  
  for (iter2 = ihash_iter(rh->members); iter2 ; iter2 = ihash_next(rh->members, iter2)) {
    cur = ihash_val(iter2);
    if (cur->user_id == serv->my_info.user_id)
      continue;
    
    send_hello(serv, cur);
  }
  
}

static int insert_pkt(llist_t list, ghl_ch_pkt_t *pkt) {
  ghl_ch_pkt_t *cur;
  cell_t iter;
  
  for (iter = llist_iter(list); iter; iter = llist_next(iter)) {
    cur = llist_val(iter);
    if (cur->seq == pkt->seq)
      return 0; /* we already have this packet */
    if ((cur->seq - pkt->seq) >= 0)
      break;
  }
  if (iter) {
    llist_add_before(list, cur, pkt);
  } else {
    llist_add_tail(list, pkt);
  }
  return 1;
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

static void do_fast_retrans(ghl_serv_t *serv, llist_t sendq, int up_to) {
  cell_t iter;
  ghl_ch_pkt_t *pkt;

  for (iter = llist_iter(sendq); iter; iter = llist_next(iter)) {
    pkt = llist_val(iter);
    if ((pkt->seq - up_to) >= 0)
      break;
    if (pkt->did_fast_retrans == 0) {
      /* fast retransmit */
      pkt->retrans = 1;
      pkt->xmit_ts = garena_now();
      xmit_packet(serv, pkt);
        fprintf(deb, "[GHL] Fast-retransmitting packet, seq=%x\n", pkt->seq); 
      
      pkt->did_fast_retrans = 1;
    }
  }
}


/* Static HANDLER FUNCTIONS */

static int handle_servconn_timeout(void *privdata) {
  ghl_serv_t *serv = privdata;
  ghl_servconn_t servconn;
  servconn.result = GHL_EV_RES_FAILURE;
  signal_event(serv, GHL_EV_SERVCONN, &servconn);
  serv->servconn_timeout = NULL; /* prevent ghl_free_serv from freeing the timer */
  serv->need_free = 1;
  return 0;
}

static int do_conn_retrans(void *privdata) {
  ghl_serv_t *serv = privdata;
  cell_t iter2;
  ihashitem_t iter;
  ghl_ch_t *ch;
  ghl_conn_fin_t conn_fin_ev;
  ghl_ch_pkt_t *pkt;
  ghl_ch_t *todel = NULL;
  ghl_room_t *rh = serv->room;
  int now = garena_now();
  int retrans;
  
  if (serv->room == NULL) {
    serv->conn_retrans_timer = ghl_new_timer(garena_now() + GP2PP_CONN_RETRANS_CHECK, do_conn_retrans, privdata);
    return 0;
  }
  for (iter = ihash_iter(rh->conns); iter; iter = ihash_next(rh->conns, iter)) {
    if (todel) {
      ihash_del(rh->conns, todel->conn_id);
      conn_free(todel);
      todel = NULL;
    }

    ch = ihash_val(iter);
    for (iter2 = llist_iter(ch->sendq); iter2; iter2 = llist_next(iter2)) {
      pkt = llist_val(iter2);
      if ((pkt->seq - ch->snd_una) > GP2PP_MAX_IN_TRANSIT) {
        fprintf(deb, "[Flow control] Congestion on connection %x\n", ch->conn_id);
        fflush(deb);
        break;
      }
      if ((pkt->xmit_ts != 0) && ((pkt->xmit_ts + pkt->rto) <= now)) {
        fprintf(deb, "[GHL] Retransmitting packet, seq=%x after RTO of %u\n", pkt->seq, pkt->rto); 
        pkt->rto <<= 1; /* exponential backoff */
        if (ch->rto < pkt->rto)
          ch->rto = pkt->rto;
        pkt->xmit_ts = garena_now();
        pkt->retrans = 1;
        xmit_packet(serv, pkt);
        pkt->did_fast_retrans = 0;
        retrans++;
      }
    }
    if (!llist_is_empty(ch->sendq) && ((ch->ts_ack + GP2PP_CONN_TIMEOUT) < now)) {
        fprintf(deb, "[GHL] Connection ID %x with user %s timed out.\n", ch->conn_id, ch->member->name);
        todel = ch;
        if (ch->cstate != GHL_CSTATE_CLOSING_OUT) {
          conn_fin_ev.ch = ch;
          signal_event(serv, GHL_EV_CONN_FIN, &conn_fin_ev);
        }
        break;
    }

    if (llist_is_empty(ch->sendq) &&  (ch->cstate == GHL_CSTATE_CLOSING_OUT)) {
      todel = ch;
      continue;
    }
  }
  
  if (todel) {
    ihash_del(rh->conns, todel->conn_id);
    conn_free(todel);
    todel = NULL;
  }

  serv->conn_retrans_timer = ghl_new_timer(garena_now() + GP2PP_CONN_RETRANS_CHECK, do_conn_retrans, privdata);
  return 0;
}


static int do_hello(void *privdata) {
  ghl_serv_t *serv = privdata;
  send_hello_to_all(serv);  
  serv->hello_timer = ghl_new_timer(garena_now() + GP2PP_HELLO_INTERVAL, do_hello, privdata);
  return 0;
}

static int do_roominfo_query(void *privdata) {
  ghl_serv_t *serv = privdata;
  
  if (serv && serv->connected && gp2pp_request_roominfo(serv->peersock, serv->my_info.user_id, serv->server_ip, GP2PP_PORT) == -1) {
    fprintf(deb, "[WARN/GHL] Room Info will not be available because the request failed.\n");
    fflush(deb); 
  }

  serv->roominfo_timer = ghl_new_timer(garena_now() + GHL_ROOMINFO_QUERY_INTERVAL, do_roominfo_query, privdata);
  return 0;
}

static int handle_auth(int type, void *payload, unsigned int length, void *privdata) {
  gsp_login_reply_t *login_reply = payload;
  ghl_servconn_t servconn;
  
  ghl_serv_t *serv = privdata;
  switch(type) {
    case GSP_MSG_LOGIN_REPLY:
      myinfo_extract(&serv->my_info, &login_reply->my_info);
      fprintf(deb, "[GHL] My user_id is %x\n", serv->my_info.user_id);
      fflush(deb);
      serv->auth_ok = 1;
      if (gp2pp_request_roominfo(serv->peersock, serv->my_info.user_id, serv->server_ip, GP2PP_PORT) == -1) {
        fprintf(deb, "[WARN/GHL] Room Info will not be available because the request failed.\n");
        fflush(deb); 
      }

      if ((serv->connected = serv->lookup_ok)) {
        ghl_free_timer(serv->servconn_timeout);
        serv->servconn_timeout = NULL;
        servconn.result = GHL_EV_RES_SUCCESS;
        signal_event(serv, GHL_EV_SERVCONN, &servconn);
      }
      break;
    case GSP_MSG_AUTH_FAIL:
      if (serv->connected) {
        fprintf(deb, "[WARN/GHL] Received main server AUTH FAIL but we are already connected. Closing connection to main server, but trying to maintain normal operation.\n");
        fflush(deb);
        close(serv->servsock);
        serv->servsock = -1;
        return -1;
      }
      ghl_free_timer(serv->servconn_timeout);
      serv->servconn_timeout = NULL;
      servconn.result = GHL_EV_RES_FAILURE;
      signal_event(serv, GHL_EV_SERVCONN, &servconn);
      ghl_free_serv(serv);
      break;
    default:
      garena_errno = GARENA_ERR_INVALID;
      return -1;
  }
  return 0;
}

static int handle_ip_lookup(int type, void *payload, unsigned int length, void *privdata, unsigned int user_id, struct sockaddr_in *remote) {
  ghl_servconn_t servconn;
  ghl_serv_t *serv = privdata;
  gp2pp_lookup_reply_t *lookup = payload;
  switch(type) {
    case GP2PP_MSG_IP_LOOKUP_REPLY:
      serv->my_info.external_ip = lookup->my_external_ip;
      serv->my_info.external_port = htons(lookup->my_external_port);
      serv->lookup_ok = 1;
      if ((serv->connected = serv->auth_ok)) {
        servconn.result = GHL_EV_RES_SUCCESS;
        signal_event(serv, GHL_EV_SERVCONN, &servconn);
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
  ghl_serv_t *serv = privdata;
  
  for (i = 0; i < roominfo->num_rooms; i++) {
    room_id = (ghtonl(roominfo->prefix) << 8) | (ghtonl(roominfo->usernum[i].suffix) & 0xFF);
    num_users = ihash_get(serv->roominfo, room_id);
    if (num_users == NULL) {
      num_users = malloc(sizeof(unsigned int));
      ihash_put(serv->roominfo, room_id,num_users);
    } 
    *num_users = roominfo->usernum[i].num_users;
  }
  return 0;
}

static int handle_initconn_msg(int type, void *payload, unsigned int length, void *privdata, unsigned int user_id, struct sockaddr_in *remote) {
  gp2pp_initconn_t *initconn = payload;
  ghl_conn_incoming_t conn_incoming_ev;
  ghl_serv_t *serv = privdata;
  ghl_room_t *rh = serv->room;
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
  conn_incoming_ev.ch->ts_base = garena_now();
  conn_incoming_ev.ch->sendq = llist_alloc();
  conn_incoming_ev.ch->recvq = llist_alloc();
  conn_incoming_ev.ch->snd_una = 0;
  conn_incoming_ev.ch->serv = serv;
  conn_incoming_ev.ch->snd_next = 0;
  conn_incoming_ev.ch->rcv_next = 0;
  conn_incoming_ev.ch->srtt = 0;
  conn_incoming_ev.ch->flightsize = 0;
  conn_incoming_ev.ch->ssthresh = GP2PP_INIT_SSTHRESH;
  conn_incoming_ev.ch->cwnd = (ghl_max_conn_pkt(serv)) << 1;
  conn_incoming_ev.ch->last_xmit = 0;
  conn_incoming_ev.ch->rto = GP2PP_INIT_RTO;
  conn_incoming_ev.ch->rcv_next_deliver = 0;
  conn_incoming_ev.ch->ts_ack = garena_now();
  conn_incoming_ev.ch->cstate = GHL_CSTATE_ESTABLISHED;
  conn_incoming_ev.ch->conn_id = ghtonl(initconn->conn_id);
  conn_incoming_ev.dport = ghtons(initconn->dport);
  ihash_put(rh->conns, conn_incoming_ev.ch->conn_id, conn_incoming_ev.ch);
  signal_event(serv, GHL_EV_CONN_INCOMING, &conn_incoming_ev);
  return 0;
}

static int handle_conn_fin_msg(int subtype, void *payload, unsigned int length, void *privdata, unsigned int user_id, unsigned int conn_id, int seq1, int seq2, int ts_rel, struct sockaddr_in *remote) { 
  ghl_serv_t *serv = privdata;
  ghl_ch_pkt_t *pkt;
  ghl_ch_t *ch;
  ghl_room_t *rh = serv->room;
  
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
  /* WTF: CLOSING_IN and CLOSING_OUT states are basically the same, because the FIN packet does not carry a sequence number
   * so the FIN sequence number is set to recv_next, so try_deliver() will set state to CLOSING_OUT immediately.
   * This may change in the future (the linux client should set the SEQ correctly on FIN packet) */
  pkt = malloc(sizeof(ghl_ch_pkt_t));
  pkt->length = 0;
  pkt->did_fast_retrans = 0;
  pkt->xmit_ts = 0;
  pkt->partial = 0;
  pkt->retrans = 0;
  pkt->payload = NULL;
  pkt->seq = ch->rcv_next; /* wtf is this crappy protocol, the FIN packet does not have a sequence number */
  pkt->ts_rel = ts_rel;
  pkt->ch = ch;
  insert_pkt(ch->recvq, pkt);
  update_next(serv, ch);
  try_deliver(serv, ch);
  ch->finseq = seq1;
  return 0;
}

static int handle_conn_ack_msg(int subtype, void *payload, unsigned int length, void *privdata, unsigned int user_id, unsigned int conn_id, int seq1, int seq2, int ts_rel, struct sockaddr_in *remote) {
  ghl_serv_t *serv = privdata;
  ghl_room_t *rh = serv->room;
  ghl_ch_t *ch;
  cell_t iter;
  int now = garena_now();
  gtime_t rtt;
  
  ghl_ch_pkt_t *pkt;
  ghl_ch_pkt_t *todel = NULL;
  
  if (rh == NULL) {
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  fprintf(deb, "[%x] ACK, this_ack=%u next_expected=%u\n", conn_id, seq1, seq2); 
  fflush(deb);
  ch = ghl_conn_from_id(rh, conn_id);
  if (ch == NULL) {
    fprintf(deb, "Alien conn: %x\n", conn_id);
    fflush(deb);
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  if ((seq2 - ch->snd_una) > 0) {
    ch->snd_una = seq2;
    ch->ts_ack = garena_now();
  } else {
    if ((seq2 - ch->snd_una) < 0)
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
      fprintf(deb, "Packet seq %x of conn %x was transmitted after %u msec\n", pkt->seq, ch->conn_id, (now - pkt->first_trans));
      fflush(deb);
      ch->flightsize -= pkt->length;
      if (pkt->retrans == 0) {
        rtt = now - pkt->first_trans;
        update_rto(rtt, ch);
      }
    }
    if ((pkt->xmit_ts == 0) && ((pkt->seq - ch->snd_una) < GP2PP_MAX_IN_TRANSIT)) {
     
     /* initial transmit (after flow control) */
      pkt->rto = ch->rto;
      pkt->xmit_ts = garena_now();
      xmit_packet(serv, pkt);
      ch->flightsize += pkt->length;
    }
  }
  if (todel != NULL) {
      llist_del_item(ch->sendq, todel);
      free(todel->payload);
      free(todel);
      todel = NULL;
  }
  do_fast_retrans(serv, ch->sendq, seq1);
  return 0;
}

static void update_rto(gtime_t rtt, ghl_ch_t *ch) {
  
  if (rtt == 0)
    rtt++;
  
  if (ch->srtt == 0) {
    ch->srtt = rtt;
  } else {
    ch->srtt = ((ch->srtt * GP2PP_ALPHA) + rtt*(1024-GP2PP_ALPHA)) >> 10;
  }
  ch->rto = (GP2PP_BETA*ch->srtt) >> 10; 
  if (ch->rto > GP2PP_UBOUND)
    ch->rto = GP2PP_UBOUND;
  if (ch->rto < GP2PP_LBOUND)
    ch->rto = GP2PP_LBOUND;
}

static int handle_conn_data_msg(int subtype, void *payload, unsigned int length, void *privdata, unsigned int user_id, unsigned int conn_id, int seq1, int seq2, int ts_rel, struct sockaddr_in *remote) {
  ghl_serv_t *serv = privdata;
  ghl_ch_t *ch;
  ghl_room_t *rh = serv->room;
  ghl_ch_pkt_t *pkt;
  cell_t iter;
  gtime_t rtt;
  gtime_t now = garena_now();
  ghl_ch_pkt_t *todel = NULL;
  if (rh == NULL) {
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  ch = ghl_conn_from_id(rh, conn_id);
/*  fprintf(deb, "[%x] DATA, this_seq=%u next_expected=%u\n", conn_id, seq1, seq2); */
    
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
      fprintf(deb, "Packet seq %x of conn %x was transmitted after %u msec\n", pkt->seq, ch->conn_id, (now - pkt->first_trans));
      fflush(deb);
      ch->flightsize -= pkt->length;
      if (pkt->retrans == 0) {
        rtt = now - pkt->first_trans;
        update_rto(rtt, ch);
      }
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
/*  fprintf(deb, "Received CONN DATA message from %s:%u\n", inet_ntoa(remote->sin_addr), htons(remote->sin_port)); */ 
  fflush(deb);
  
  pkt = malloc(sizeof(ghl_ch_pkt_t));
  pkt->length = length;
  pkt->payload = malloc(length);
  pkt->seq = seq1;
  pkt->ts_rel = ts_rel;
  pkt->ch = ch;
  pkt->xmit_ts = 0;
  pkt->partial = 0;
  pkt->retrans = 0;
  pkt->did_fast_retrans = 0;
  memcpy(pkt->payload, payload, length);
  if (((seq1 - ch->rcv_next) >= 0) && ((ch->rcv_next - ch->rcv_next_deliver) < GP2PP_MAX_UNDELIVERED) && ((seq1 - ch->rcv_next) < GP2PP_MAX_IN_TRANSIT)) {
    insert_pkt(ch->recvq, pkt);
    remote->sin_port = htons(ch->member->external_port);
    update_next(serv, ch); 
    gp2pp_output_conn(serv->peersock, GP2PP_CONN_MSG_ACK, NULL, 0, serv->my_info.user_id, conn_id, seq1, ch->rcv_next, 0, remote);
    try_deliver(serv, ch);
  }
  
  
  return 0;
}

static int handle_peer_msg(int type, void *payload, unsigned int length, void *privdata, unsigned int user_id, struct sockaddr_in *remote) {
  gp2pp_udp_encap_t *udp_encap = payload;
  ghl_serv_t *serv = privdata;
  ghl_room_t *rh = serv->room;
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
      gp2pp_send_hello_reply(serv->peersock, serv->my_info.user_id, user_id, remote);
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
      signal_event(serv, GHL_EV_UDP_ENCAP, &udp_encap_ev);
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
  ghl_room_t *rh = privdata;
/*  printf("Room %x join timeouted.\n", rh->room_id); */
  join.result = GHL_EV_RES_FAILURE;
  join.rh = rh;
  err |= (signal_event(rh->serv, GHL_EV_ME_JOIN, &join) == -1);
  rh->timeout = NULL; /* prevent ghl_free_room from deleting the timer we are currently handling */
  err |= (ghl_free_room(rh) == -1);
  return err ? -1 : 0;
}


static int handle_room_join(int type, void *payload, unsigned int length, void *privdata, void *roomdata) {
  gcrp_welcome_t *welcome = payload;
  gcrp_memberlist_t *memberlist = payload;
  ghl_me_join_t join;
  ghl_room_t *rh = NULL;
  ghl_serv_t *serv = privdata;
  int err = 0;
  ghl_member_t *member;
  unsigned int i;
  
  switch(type) {
    case GCRP_MSG_WELCOME:
      rh = serv->room;
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
      rh = serv->room;
      
      if ((rh == NULL) || (rh->room_id != ghtonl(memberlist->room_id))) {
        fprintf(stderr, "[GHL/WARN] Joined a room that we didn't ask to join (?!?)\n");
        return 0;
      }
  
      for (i = 0; i < ghtonl(memberlist->num_members); i++) {
        member = malloc(sizeof(ghl_member_t));
        member_extract(member, memberlist->members + i);
        ihash_put(rh->members, member->user_id, member);

        IFDEBUG(printf("[GHL] Room member: %s\n", member->name));
        
/*        printf("member %s PORT=%u ID=%x\n", member->name, htons(member->external_port), ghtonl(member->user_id)); */
        if (strcmp(serv->my_info.name, member->name) == 0)
          rh->me = member;
      }
      
      if (rh->me == NULL) {
        fprintf(deb, "[GHL/ERROR] Joined a room, but we are not in the member list.\n");
        garena_errno = GARENA_ERR_PROTOCOL;
        join.result = GHL_EV_RES_FAILURE;
         join.rh = rh;
         ghl_free_timer(rh->timeout);
         rh->timeout = NULL;
         err |= (signal_event(rh->serv, GHL_EV_ME_JOIN, &join) == -1);
         err |= (ghl_free_room(rh) == -1);
         return err ? -1 : 0;
      }
      serv->my_info.user_id = rh->me->user_id;
      rh->got_members = 1;
      break;  
    case GCRP_MSG_JOIN_FAILED:

      rh = serv->room;
      if (rh == NULL) {
        fprintf(stderr, "[GHL/WARN] Failed to join a room that we didn't ask to join\n");
        return 0;
      }

      join.result = GHL_EV_RES_FAILURE;
      join.rh = rh;
      ghl_free_timer(rh->timeout);
      rh->timeout = NULL;
      err |= (signal_event(rh->serv, GHL_EV_ME_JOIN, &join) == -1);
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
    return signal_event(serv, GHL_EV_ME_JOIN, &join);
  }
  
  return 0;
}


static int handle_room_activity(int type, void *payload, unsigned int length, void *privdata, void *roomdata) {
  char buf[GCRP_MAX_MSGSIZE];
  gcrp_join_t *join = payload;
  gcrp_part_t *part = payload;
  gcrp_talk_t *talk = payload;
  gcrp_togglevpn_t *togglevpn = payload;
  ghl_room_t *rh = roomdata;
  ghl_serv_t *serv = privdata;
  ghl_member_t *member;
  ghl_talk_t talk_ev;
  ghl_part_t part_ev;
  ihashitem_t iter;
  ghl_conn_fin_t conn_fin_ev;
  ghl_ch_t *todel;
  ghl_ch_t *conn;
  ghl_join_t join_ev;
  ghl_togglevpn_t togglevpn_ev;
  
  switch(type) {
    case GCRP_MSG_JOIN:
      member = malloc(sizeof(ghl_member_t));
      member_extract(member, join);
      ihash_put(rh->members, member->user_id, member);
      join_ev.rh = rh;
      join_ev.member = member;
      signal_event(serv, GHL_EV_JOIN, &join_ev);
      if (member->user_id != serv->my_info.user_id) {
        send_hello(serv, member);
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
        signal_event(serv, GHL_EV_TALK, &talk_ev);
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
        signal_event(serv, GHL_EV_TOGGLEVPN, &togglevpn_ev);
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
        signal_event(serv, GHL_EV_TOGGLEVPN, &togglevpn_ev);
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
      signal_event(serv, GHL_EV_PART, &part_ev);
      ihash_del(rh->members, member->user_id);
      todel = NULL;
      for (iter = ihash_iter(rh->conns); iter; iter = ihash_next(rh->conns, iter)) {
        if (todel) {
          ihash_del(rh->conns, conn->conn_id);
          conn_free(conn);
          todel = NULL;
        }

        conn = ihash_val(iter);
        if (conn->member == member) {
           if (conn->cstate != GHL_CSTATE_CLOSING_OUT) {
             conn_fin_ev.ch = conn;
             signal_event(serv, GHL_EV_CONN_FIN, &conn_fin_ev);
           }
           todel = conn;
        }
      }
      if (todel) {
        ihash_del(rh->conns, conn->conn_id);
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
