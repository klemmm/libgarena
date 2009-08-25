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
  int i;
  for (i = 0; i < GP2PP_MSG_NUM; i++) {
    gp2pp_handlers[i].fun = NULL;
    gp2pp_handlers[i].privdata = NULL;
  }
  return 0;
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

/**
 * Processes a received GP2PP message and calls any
 * registered callback to handle the message.
 *
 * @param buf The message
 * @param length Length of the message (including GP2PP header)
 * @param remote The addr from which the message was received.
 * @return 0 for success, -1 for failure
 */
 
int gp2pp_input(char *buf, int length, struct sockaddr_in *remote) {
  gp2pp_hdr_t *hdr = (gp2pp_hdr_t *) buf;
  if (length < sizeof(gp2pp_hdr_t)) {
    garena_errno = GARENA_ERR_MALFORMED;
    IFDEBUG(fprintf(stderr, "[DEBUG/GP2PP] Dropped short message.\n"));
    return -1;
  }

  if (gp2pp_handlers[hdr->msgtype].fun == NULL) {
    IFDEBUG(fprintf(stderr, "[DEBUG/GP2PP] Unhandled message of type: %x\n", hdr->msgtype));
  } else {
    
    if (gp2pp_handlers[hdr->msgtype].fun(hdr->msgtype, buf + sizeof(gp2pp_hdr_t), length - sizeof(gp2pp_hdr_t), gp2pp_handlers[hdr->msgtype].privdata, ghtonl(hdr->user_ID), remote) == -1) {
      garena_perror("[WARN/GP2PP] Error while handling message");
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
int gp2pp_output(int sock, int type, char *payload, int length, int user_ID, struct sockaddr_in *remote) {
  static char buf[GP2PP_MAX_MSGSIZE];
  gp2pp_hdr_t *hdr = (gp2pp_hdr_t *) buf;
  if (length + sizeof(gp2pp_hdr_t) > GP2PP_MAX_MSGSIZE) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  hdr->msgtype = type;
  hdr->user_ID = ghtonl(user_ID);
  
  memcpy(buf + sizeof(gp2pp_hdr_t), payload, length);
  if (sendto(sock, buf, length + sizeof(gp2pp_hdr_t), 0, (struct sockaddr *) remote, sizeof(struct sockaddr_in)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    return -1;
  }
  IFDEBUG(fprintf(stderr, "[DEBUG/GP2PP] Sent a message of type %x (payload length = %x)\n", type, length));
  return 0;
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
  hello_rep->user_ID = to_ID;
  return gp2pp_output(sock, GP2PP_MSG_HELLO_REP, buf, sizeof(gp2pp_hello_rep_t), from_ID, remote);
}

/**
 * Register a handler to be called on incoming messages of type "msgtype".
 *
 * @param msgtype The message type for which we define a handler
 * @param fun Pointer to handler function
 * @param privdata Pointer to private data (anything you want) that will be supplied to the called function
 * @return 0 for success, -1 for failure
 */
 
int gp2pp_register_handler(int msgtype, gp2pp_funptr_t *fun, void *privdata) {
  if ((msgtype < 0) || (msgtype >= GP2PP_MSG_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  
  if (gp2pp_handlers[msgtype].fun != NULL) {
    garena_errno = GARENA_ERR_INUSE;
    return -1;
  }
  gp2pp_handlers[msgtype].fun = fun;
  gp2pp_handlers[msgtype].privdata = privdata;
  return 0;
}

/**
 * Unregisters a handler associated with the specified message type.
 *
 * @param msgtype The message type for which we delete the handler
 * @return 0 for success, -1 for failure
 */
int gp2pp_unregister_handler(int msgtype) {
  if ((msgtype < 0) || (msgtype >= GP2PP_MSG_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  if (gp2pp_handlers[msgtype].fun == NULL) {
    garena_errno = GARENA_ERR_NOTFOUND;
    return -1;
  }
  gp2pp_handlers[msgtype].fun = NULL;
  gp2pp_handlers[msgtype].privdata = NULL;  
  return 0;
}


/**
 * Get the privdata associated with a handler.
 *
 * @param msgtype The message type of the handler we wish to retrieve the privdata
 * @return The privdata, or NULL if there is an error
 */
void* gp2pp_handler_privdata(int msgtype) {
  if ((msgtype < 0) || (msgtype >= GP2PP_MSG_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return NULL;
  }
  if (gp2pp_handlers[msgtype].fun == NULL) {
    garena_errno = GARENA_ERR_NOTFOUND;
    return NULL;
  }
 return gp2pp_handlers[msgtype].privdata;
}

