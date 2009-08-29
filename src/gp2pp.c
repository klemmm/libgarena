 /**
  * @file
  * File implementing the Garena Peer2Peer Protocol (GP2PP)
  */
  
#include <sys/types.h>
#include <sys/socket.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <garena/garena.h>
#include <garena/gp2pp.h>
#include <garena/error.h>
#include <garena/util.h>



int gp2pp_init(void) {
  return 0;
}  

gp2pp_handtab_t *gp2pp_alloc_handtab (void) {
  int i;
  gp2pp_handtab_t *htab = malloc(sizeof(gp2pp_handtab_t));
  if (htab == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    return NULL;
  }
  for (i = 0; i < GP2PP_MSG_NUM; i++) {
    htab->gp2pp_handlers[i].fun = NULL;
    htab->gp2pp_handlers[i].privdata = NULL;
  }
  for (i = 0; i < GP2PP_CONN_MSG_NUM; i++) {
    htab->gp2pp_conn_handlers[i].fun = NULL;
    htab->gp2pp_conn_handlers[i].privdata = NULL;
  }
  return htab; 
}  





/**
 * Attempt to read the socket to get a GP2PP message.
 * The read will be blocking if the socket is blocking.
 *
 * @param sock The socket from which we read the message
 * @param buf The buffer in which we will store the message
 * @param length The allocated buffer space, in bytes
 * @param remote Pointer to sockaddr_in for receiving the sender's address
 * @return Size of the message (including GP2PP header), or -1 for failure
 */

int gp2pp_read(int sock, char *buf, int length, struct sockaddr_in *remote) {
  int r;
  int fromlen = sizeof(struct sockaddr_in);
  if ((r = recvfrom(sock, buf, length, 0, (struct sockaddr*) remote, &fromlen)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    return -1;
  }
  return r;
}


static int gp2pp_handle_conn_pkt(gp2pp_handtab_t *htab, char *buf, int length, struct sockaddr_in *remote) {
  gp2pp_conn_hdr_t *pkt = (gp2pp_conn_hdr_t*) buf;
  if (length < sizeof(gp2pp_conn_hdr_t)) {
    garena_errno = GARENA_ERR_PROTOCOL;
    IFDEBUG(fprintf(stderr, "[DEBUG/GP2PP] Dropped short message.\n"));
    return -1;
  }
  if ((pkt->msgsubtype < 0) || (pkt->msgsubtype > GP2PP_CONN_MSG_NUM) || (htab->gp2pp_conn_handlers[pkt->msgsubtype].fun == NULL)) {
    fprintf(deb, "[DEBUG/GP2PP] Unhandled CONN message of subtype: %x\n", pkt->msgsubtype);
    fflush(deb);
  } else {
    if (htab->gp2pp_conn_handlers[pkt->msgsubtype].fun(pkt->msgsubtype, buf + sizeof(gp2pp_conn_hdr_t), length - sizeof(gp2pp_conn_hdr_t), htab->gp2pp_conn_handlers[pkt->msgsubtype].privdata,ghtonl(pkt->user_id), ghtonl(pkt->conn_id), ghtonl(pkt->seq1), ghtonl(pkt->seq2), ghtons(pkt->ts_rel), remote) == -1) {
/*      garena_perror("[WARN/GP2PP] Error while handling message"); */
    }

  }
}
/**
 * Processes a received GP2PP message and calls any
 * registered callback to handle the message.
 *
 * @param buf The message
 * @param length Length of the message (including GP2PP header)
 * @param remote The addr from which the message was received.
 * @return 0 for success, -1 for failure
 */
 
int gp2pp_input(gp2pp_handtab_t *htab, char *buf, int length, struct sockaddr_in *remote) {
  gp2pp_hdr_t *hdr = (gp2pp_hdr_t *) buf;
  fprintf(deb, "Received %x\n", hdr->msgtype);
  fflush(deb);
  if (length < sizeof(gp2pp_hdr_t)) {
    garena_errno = GARENA_ERR_PROTOCOL;
    IFDEBUG(fprintf(stderr, "[DEBUG/GP2PP] Dropped short message.\n"));
    return -1;
  }

  /*
   * Need special handling for type 0x0D packets, since they do not conform to the 
   * regular format. Who invented this shitty protocol? 
   */
  if (hdr->msgtype == GP2PP_MSG_CONN_PKT)
    return gp2pp_handle_conn_pkt(htab, buf, length, remote);
    
  if ((hdr->msgtype < 0) || (hdr->msgtype >= GP2PP_MSG_NUM) || (htab->gp2pp_handlers[hdr->msgtype].fun == NULL)) {
    fprintf(deb, "[DEBUG/GP2PP] Unhandled message of type: %x\n", hdr->msgtype);
    fflush(deb);
  } else {
    
    if (htab->gp2pp_handlers[hdr->msgtype].fun(hdr->msgtype, buf + sizeof(gp2pp_hdr_t), length - sizeof(gp2pp_hdr_t), htab->gp2pp_handlers[hdr->msgtype].privdata, ghtonl(hdr->user_id), remote) == -1) {
/*      garena_perror("[WARN/GP2PP] Error while handling message"); */
    }
  }
}



/**
  * Builds and send a GP2PP message over a socket. 
  *
  * @param sock Socket used to send the GP2PP message.
  * @param type Type of the message
  * @param payload Data contained in the message
  * @param length Length of the data (in bytes) 
  * @param remote The destination address of the message
  * @return 0 for success, -1 for failure
  */
int gp2pp_output(int sock, int type, char *payload, int length, int user_id, struct sockaddr_in *remote) {
  static char buf[GP2PP_MAX_MSGSIZE];
  gp2pp_hdr_t *hdr = (gp2pp_hdr_t *) buf;
  int hdrsize = sizeof(gp2pp_hdr_t);
    
  if (length + hdrsize > GP2PP_MAX_MSGSIZE) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  
  hdr->msgtype = type;
  hdr->user_id = ghtonl(user_id);
  
  memcpy(buf + hdrsize, payload, length);
  if (sendto(sock, buf, length + hdrsize, 0, (struct sockaddr *) remote, sizeof(struct sockaddr_in)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    return -1;
  }
  IFDEBUG(fprintf(stderr, "[DEBUG/GP2PP] Sent a message of type %x (payload length = %x)\n", type, length));
  return 0;
}

int gp2pp_output_conn(int sock, int subtype, char *payload, int length, int user_id, int conn_id, int seq1, int seq2, int ts_rel, struct sockaddr_in *remote) {
  static char buf[GP2PP_MAX_MSGSIZE];
  int type = GP2PP_MSG_CONN_PKT;
  gp2pp_conn_hdr_t *conn_hdr = (gp2pp_conn_hdr_t *) buf;
  int hdrsize = sizeof(gp2pp_conn_hdr_t);
    
  if (length + hdrsize > GP2PP_MAX_MSGSIZE) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  
  conn_hdr->msgtype = type;
  conn_hdr->msgsubtype = subtype;
  conn_hdr->user_id = ghtonl(user_id);
  conn_hdr->conn_id = ghtonl(conn_id);
  conn_hdr->seq1 = ghtonl(seq1);
  conn_hdr->seq2 = ghtonl(seq2);
  conn_hdr->ts_rel = ghtons(ts_rel); 
  
  memcpy(buf + hdrsize, payload, length);
  if (sendto(sock, buf, length + hdrsize, 0, (struct sockaddr *) remote, sizeof(struct sockaddr_in)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    return -1;
  }
  IFDEBUG(fprintf(stderr, "[DEBUG/GP2PP] Sent a message of type %x (payload length = %x)\n", type, length));
  return 0;
}

int gp2pp_send_initconn(int sock, int from_ID, int conn_id, int dport, int sip, struct sockaddr_in *remote) {
  static char buf[GP2PP_MAX_MSGSIZE];
  gp2pp_initconn_t *initconn = (gp2pp_initconn_t *) buf;
  initconn->mbz = 0;
  initconn->conn_id = ghtonl(conn_id);
  initconn->dport = ghtons(dport);
  initconn->sip = sip; 
  return gp2pp_output(sock, GP2PP_MSG_INITCONN, buf, sizeof(gp2pp_initconn_t), from_ID, remote);
}

/**
 * Send a GP2PP HELLO REPLY message.
 *
 * @param sock The socket for sending.
 * @param from_ID the originating user ID 
 * @param to_ID The destination user ID
 * @param remote The remote address
 * @return 0 for success, -1 for failure
 */
 
int gp2pp_send_hello_reply(int sock, int from_ID, int to_ID, struct sockaddr_in *remote) {
  static char buf[GP2PP_MAX_MSGSIZE];
  gp2pp_hello_rep_t *hello_rep = (gp2pp_hello_rep_t *) buf;
  hello_rep->user_id = to_ID;
  hello_rep->mbz = 0;
  return gp2pp_output(sock, GP2PP_MSG_HELLO_REP, buf, sizeof(gp2pp_hello_rep_t), from_ID, remote);
}

int gp2pp_send_hello_request(int sock, int from_ID, struct sockaddr_in *remote) {
  static char buf[GP2PP_MAX_MSGSIZE];
  gp2pp_hello_req_t *hello_req = (gp2pp_hello_req_t *) buf;
  hello_req->mbz = 0;
  hello_req->mbz2 = 0;
  return gp2pp_output(sock, GP2PP_MSG_HELLO_REQ, buf, sizeof(gp2pp_hello_req_t), from_ID, remote);
}

int gp2pp_send_udp_encap(int sock, int from_ID, int sport, int dport, char *payload, int length, struct sockaddr_in *remote) {
  static char buf[GP2PP_MAX_MSGSIZE];
  gp2pp_udp_encap_t *udp_encap = (gp2pp_udp_encap_t *) buf;
  if (length + sizeof(gp2pp_udp_encap_t) > sizeof(buf)) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  udp_encap->mbz = 0;
  udp_encap->mbz2 = 0;
  udp_encap->sport = htons(sport);
  udp_encap->dport = htons(dport);
  memcpy(buf + sizeof(gp2pp_udp_encap_t), payload, length);
  return gp2pp_output(sock, GP2PP_MSG_UDP_ENCAP, buf, sizeof(gp2pp_udp_encap_t) + length, from_ID, remote);
}

/**
 * Register a handler to be called on incoming messages of type "msgtype".
 *
 * @param msgtype The message type for which we define a handler
 * @param fun Pointer to handler function
 * @param privdata Pointer to private data (anything you want) that will be supplied to the called function
 * @return 0 for success, -1 for failure
 */
 
int gp2pp_register_handler(gp2pp_handtab_t *htab, int msgtype, gp2pp_fun_t *fun, void *privdata) {
  if ((msgtype < 0) || (msgtype >= GP2PP_MSG_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  
  if (htab->gp2pp_handlers[msgtype].fun != NULL) {
    garena_errno = GARENA_ERR_INUSE;
    return -1;
  }
  htab->gp2pp_handlers[msgtype].fun = fun;
  htab->gp2pp_handlers[msgtype].privdata = privdata;
  return 0;
}

/**
 * Unregisters a handler associated with the specified message type.
 *
 * @param msgtype The message type for which we delete the handler
 * @return 0 for success, -1 for failure
 */
int gp2pp_unregister_handler(gp2pp_handtab_t *htab, int msgtype) {
  if ((msgtype < 0) || (msgtype >= GP2PP_MSG_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  if (htab->gp2pp_handlers[msgtype].fun == NULL) {
    garena_errno = GARENA_ERR_NOTFOUND;
    return -1;
  }
  htab->gp2pp_handlers[msgtype].fun = NULL;
  htab->gp2pp_handlers[msgtype].privdata = NULL;  
  return 0;
}


/**
 * Get the privdata associated with a handler.
 *
 * @param msgtype The message type of the handler we wish to retrieve the privdata
 * @return The privdata, or NULL if there is an error
 */
void* gp2pp_handler_privdata(gp2pp_handtab_t *htab, int msgtype) {
  if ((msgtype < 0) || (msgtype >= GP2PP_MSG_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return NULL;
  }
  if (htab->gp2pp_handlers[msgtype].fun == NULL) {
    garena_errno = GARENA_ERR_NOTFOUND;
    return NULL;
  }
 return htab->gp2pp_handlers[msgtype].privdata;
}


/**
 * Register a handler to be called on incoming messages of type "msgtype".
 *
 * @param msgtype The message type for which we define a handler
 * @param fun Pointer to handler function
 * @param privdata Pointer to private data (anything you want) that will be supplied to the called function
 * @return 0 for success, -1 for failure
 */
 
int gp2pp_register_conn_handler(gp2pp_handtab_t *htab, int msgtype, gp2pp_conn_fun_t *fun, void *privdata) {
  if ((msgtype < 0) || (msgtype >= GP2PP_CONN_MSG_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  
  if (htab->gp2pp_conn_handlers[msgtype].fun != NULL) {
    garena_errno = GARENA_ERR_INUSE;
    return -1;
  }
  htab->gp2pp_conn_handlers[msgtype].fun = fun;
  htab->gp2pp_conn_handlers[msgtype].privdata = privdata;
  return 0;
}

/**
 * Unregisters a handler associated with the specified message type.
 *
 * @param msgtype The message type for which we delete the handler
 * @return 0 for success, -1 for failure
 */
int gp2pp_unregister_conn_handler(gp2pp_handtab_t *htab, int msgtype) {
  if ((msgtype < 0) || (msgtype >= GP2PP_CONN_MSG_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  if (htab->gp2pp_conn_handlers[msgtype].fun == NULL) {
    garena_errno = GARENA_ERR_NOTFOUND;
    return -1;
  }
  htab->gp2pp_conn_handlers[msgtype].fun = NULL;
  htab->gp2pp_conn_handlers[msgtype].privdata = NULL;  
  return 0;
}


/**
 * Get the privdata associated with a handler.
 *
 * @param msgtype The message type of the handler we wish to retrieve the privdata
 * @return The privdata, or NULL if there is an error
 */
void* gp2pp_conn_handler_privdata(gp2pp_handtab_t *htab, int msgtype) {
  if ((msgtype < 0) || (msgtype >= GP2PP_CONN_MSG_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return NULL;
  }
  if (htab->gp2pp_conn_handlers[msgtype].fun == NULL) {
    garena_errno = GARENA_ERR_NOTFOUND;
    return NULL;
  }
 return htab->gp2pp_conn_handlers[msgtype].privdata;
}


int gp2pp_do_ip_lookup(int sock, int my_id, int server_ip, int server_port) {
  char buf[32];
  uint32_t *id = (uint32_t*) &buf[1];
  struct sockaddr_in fsocket;
  return 0;
  

  /* test */
  struct sockaddr_in local;
  local.sin_family = PF_INET;
  local.sin_addr.s_addr = INADDR_ANY;
  local.sin_port = htons(0xDEAD);
  sock = socket(PF_INET, SOCK_DGRAM, 0);  
  bind(sock, (struct sockaddr *) &local, sizeof(local));
  
  fsocket.sin_family = PF_INET;
  fsocket.sin_addr.s_addr = server_ip;
  fsocket.sin_port = htons(server_port);
   
  memset(buf, 0, sizeof(buf));
  buf[0] = 5;
  sendto(sock, buf, 9, 0, (struct sockaddr *) &fsocket, sizeof(fsocket));

  sleep(1);

  memset(buf, 0, sizeof(buf));
  buf[0] = 2;
  *id = ghtonl(my_id);
  sendto(sock, buf, 5, 0, (struct sockaddr *) &fsocket, sizeof(fsocket));
  
  sleep(1);
  
  memset(buf, 0, sizeof(buf));
  buf[0] = 2;
  sendto(sock, buf, 5, 0, (struct sockaddr *) &fsocket, sizeof(fsocket));
  sendto(sock, buf, 5, 0, (struct sockaddr *) &fsocket, sizeof(fsocket));
  
}

int gp2pp_get_tsnow() {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    return -1;
  }
  return (tv.tv_usec/GP2PP_CONN_TS_DIVISOR);
}
