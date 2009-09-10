 /**
  * @file
  * File implementing the Garena Server Protocol (GSP)
  */
  
#include <sys/types.h>
#include <sys/socket.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <garena/garena.h>
#include <garena/gsp.h>
#include <garena/error.h>
#include <garena/util.h>

int gsp_init(void) {
  return 0;
}

gsp_handtab_t *gsp_alloc_handtab (void) {
  int i;
  gsp_handtab_t *htab = malloc(sizeof(gsp_handtab_t));
  if (htab == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    return NULL;
  }
  for (i = 0; i < GSP_MSG_NUM; i++) {
    htab->gsp_handlers[i].fun = NULL;
    htab->gsp_handlers[i].privdata = NULL;
  }
  return htab;
}

int gsp_read(int sock, char *buf, int length) {
  int toread;
  uint32_t *size;
  int r;
  
  size = (uint32_t *) buf;
  /* XXX FIXME handle partial reads */
  /* read header */
  if ((r = recv(sock, buf, sizeof(uint32_t), MSG_WAITALL) != sizeof(uint32_t))) {
    garena_errno = GARENA_ERR_LIBC;
    return -1;
  }
  toread = ghtonl(*size) & 0xFFFFFF;

  if (toread + sizeof(uint32_t) > GSP_MAX_MSGSIZE) {
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  if (toread & 0xF) {
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  
  /* read message body */
  if ((r = recv(sock, buf + sizeof(uint32_t), toread, MSG_WAITALL) != toread)) {
    garena_errno = GARENA_ERR_LIBC;
    return -1;
  }
  return (toread + sizeof(uint32_t)); 
}

/**
 * Processes a received GSP message and calls any
 * registered callback to handle the message.
 *
 * @param buf The message
 * @param length Length of the message (including size field)
 * @return 0 for success, -1 for failure
 */
 
int gsp_input(gsp_handtab_t *htab, char *buf, int length) {
  uint32_t *size = (uint32_t *) buf;
  if (length < sizeof(uint32_t)) {
    garena_errno = GARENA_ERR_PROTOCOL;
    IFDEBUG(fprintf(stderr, "[DEBUG/GSP] Dropped short message.\n"));
    return -1;
  }
  if ((length - sizeof(uint32_t)) != ghtonl(*size)) {
    IFDEBUG(fprintf(stderr, "[DEBUG/GSP] Dropped malformed message."));
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  if ((length - sizeof(uint32_t)) & 0xF) {
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  /*
  if ((hdr->msgtype < 0) || (hdr->msgtype >= GSP_MSG_NUM) || (htab->gsp_handlers[hdr->msgtype].fun == NULL)) {
    fprintf(deb, "[DEBUG/GSP] Unhandled message of type: %x (payload size = %x)\n", hdr->msgtype, hdr->msglen - 1);
    fflush(deb);
  } else {
    
    if (htab->gsp_handlers[hdr->msgtype].fun(hdr->msgtype, buf + sizeof(gsp_hdr_t), length - sizeof(gsp_hdr_t), htab->gsp_handlers[hdr->msgtype].privdata, roomdata) == -1) {
      garena_perror("[WARN/GSP] Error while handling message");
    }
  }
  */
}


/**
  * Builds and send a GSP message over a socket. 
  *
  * @param sock Socket used to send the GSP message.
  * @param type Type of the message
  * @param payload Data contained in the message
  * @param length Length of the data (in bytes) 
  * @return 0 for success, -1 for failure
  */
int gsp_output(int sock, int type, char *payload, int length) {
  static char buf[GSP_MAX_MSGSIZE];
  uint32_t *size = (uint32_t *) buf;

  if (length + sizeof(uint32_t) + sizeof(gsp_hdr_t) > GSP_MAX_MSGSIZE) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  
  /*
  hdr->msglen = ghtonl(length + 1); 
  hdr->msgtype = type;
  memcpy(buf + sizeof(gsp_hdr_t), payload, length);
  if (write(sock, buf, length + sizeof(gsp_hdr_t)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    return -1;
  }
  IFDEBUG(fprintf(stderr, "[DEBUG/GSP] Sent a message of type %x (payload length = %x)\n", type, length));
  */
  return 0;
}



/**
 * Register a handler to be called on incoming messages of type "msgtype".
 *
 * @param msgtype The message type for which we define a handler
 * @param fun Pointer to handler function
 * @param privdata Pointer to private data (anything you want) that will be supplied to the called function
 * @return 0 for success, -1 for failure
 */
 
int gsp_register_handler(gsp_handtab_t *htab, int msgtype, gsp_fun_t *fun, void *privdata) {
  if ((msgtype < 0) || (msgtype >= GSP_MSG_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  
  if (htab->gsp_handlers[msgtype].fun != NULL) {
    garena_errno = GARENA_ERR_INUSE;
    return -1;
  }
  htab->gsp_handlers[msgtype].fun = fun;
  htab->gsp_handlers[msgtype].privdata = privdata;
  return 0;
}

/**
 * Unregisters a handler associated with the specified message type.
 *
 * @param msgtype The message type for which we delete the handler
 * @return 0 for success, -1 for failure
 */
int gsp_unregister_handler(gsp_handtab_t *htab, int msgtype) {
  if ((msgtype < 0) || (msgtype >= GSP_MSG_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  if (htab->gsp_handlers[msgtype].fun == NULL) {
    garena_errno = GARENA_ERR_NOTFOUND;
    return -1;
  }
  htab->gsp_handlers[msgtype].fun = NULL;
  htab->gsp_handlers[msgtype].privdata = NULL;  
  return 0;
}


/**
 * Get the privdata associated with a handler.
 *
 * @param msgtype The message type of the handler we wish to retrieve the privdata
 * @return The privdata, or NULL if there is an error
 */
void* gsp_handler_privdata(gsp_handtab_t *htab, int msgtype) {
  if ((msgtype < 0) || (msgtype >= GSP_MSG_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return NULL;
  }
  if (htab->gsp_handlers[msgtype].fun == NULL) {
    garena_errno = GARENA_ERR_NOTFOUND;
    return NULL;
  }
 return htab->gsp_handlers[msgtype].privdata;
}


int gsp_send_login(int sock, char *login, char *md5pass) {
}
