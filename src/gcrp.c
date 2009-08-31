 /**
  * @file
  * File implementing the Garena ChatRoom Protocol (GCRP)
  */
  
#include <sys/types.h>
#include <sys/socket.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <garena/garena.h>
#include <garena/gcrp.h>
#include <garena/error.h>
#include <garena/util.h>


char GCRP_JOINCODE[] = {
0x00, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x00, 
0x00, 0xef, 0x77, 0xb6, 0x08, 0x78, 0x9c, 0xeb, 
0xf9, 0x27, 0xc9, 0x98, 0x9d, 0x93, 0x9a, 0x9b, 
0x6b, 0x68, 0x64, 0x6c, 0xc2, 0x00, 0x01, 0x6e, 
0x41, 0x0c, 0x0c, 0x8c, 0x6c, 0x8c, 0x0c, 0x31, 
0x93, 0x6e, 0xeb, 0x1f, 0x58, 0xc1, 0xc8, 0x0f, 
0x12, 0x63, 0x7d, 0xc9, 0xfa, 0x92, 0x01, 0x0b, 
0x50, 0xe3, 0xc1, 0x22, 0xc8, 0x0a, 0xd4, 0x0f, 
0xa4, 0x00, 0xeb, 0x6f, 0x0a, 0xa9, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x65, 0x34, 0x66, 
0x34, 0x35, 0x64, 0x65, 0x30, 0x30, 0x32, 0x35, 
0x34, 0x62, 0x37, 0x62, 0x30, 0x37, 0x62, 0x64, 
0x62, 0x36, 0x36, 0x30, 0x30, 0x34, 0x30, 0x63, 
0x30, 0x64, 0x32, 0x62, 0x35, 0x00, 0x00 };

/*
char GCRP_JOINCODE[] = {
0x00, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x00, 
0x00, 0xf1, 0x03, 0x55, 0x7f, 0x78, 0x9c, 0xeb, 
0xf9, 0x27, 0xc9, 0x98, 0x9d, 0x93, 0x9a, 0x9b, 
0x6b, 0x68, 0x64, 0x6c, 0xc2, 0x00, 0x01, 0x6e, 
0x41, 0x0c, 0x0c, 0x8c, 0x6c, 0x8c, 0x0c, 0x31, 
0x93, 0x6e, 0xeb, 0x1f, 0x58, 0xc1, 0xc8, 0x0f, 
0x12, 0x63, 0x61, 0x64, 0x7d, 0xc9, 0x80, 0x05, 
0xa8, 0xf1, 0x60, 0x11, 0x64, 0x05, 0xea, 0x07, 
0x52, 0x00, 0xbd, 0x03, 0x09, 0xc0, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x65, 0x34, 0x66, 
0x34, 0x35, 0x64, 0x65, 0x30, 0x30, 0x32, 0x35, 
0x34, 0x62, 0x37, 0x62, 0x30, 0x37, 0x62, 0x64, 
0x62, 0x36, 0x36, 0x30, 0x30, 0x34, 0x30, 0x63, 
0x30, 0x64, 0x32, 0x62, 0x35, 0x00, 0x00 };
*/
/*

char GCRP_JOINCODE[] = {
0x00, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x00, 
0x00, 

0xdc, 0x83, 0x7f, 0x81, 


0x78, 0x9c, 0xeb, 0xf9,
0x27, 0xc9, 0x98, 0x9d, 0x93, 0x9a, 0x9b, 
0x6b, 0x68, 0x64, 0x6c, 0xc2, 0x00, 0x01, 0x6e, 
0x41, 0x0c, 0x0c, 0x8c, 0xac, 0x8c, 0x0c, 0x31, 


0x8d, 0xcb, 0x2c, 0x0e, 0xac, 0x60, 0xe4, 0x07, 
0x89, 0xb1, 0x30, 0xb0, 
0xbe, 0x64, 
0xc0, 0x02, 
0xb4, 0xb8, 
0xb0, 0x08, 
0xb2, 0x02, 
0xf5, 0x03, 
0x29, 0x00, 
0xad, 0xe7, 0x09, 0x83, 


0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x65, 0x34, 0x66, 
0x34, 0x35, 0x64, 0x65, 0x30, 0x30, 0x32, 0x35, 
0x34, 0x62, 0x37, 0x62, 0x30, 0x37, 0x62, 0x64, 
0x62, 0x36, 0x36, 0x30, 0x30, 0x34, 0x30, 0x63, 
0x30, 0x64, 0x32, 0x62, 0x35, 0x00, 0x00 };

*/

int gcrp_init(void) {
  return 0;
}

gcrp_handtab_t *gcrp_alloc_handtab (void) {
  int i;
  gcrp_handtab_t *htab = malloc(sizeof(gcrp_handtab_t));
  if (htab == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    return NULL;
  }
  for (i = 0; i < GCRP_MSG_NUM; i++) {
    htab->gcrp_handlers[i].fun = NULL;
    htab->gcrp_handlers[i].privdata = NULL;
  }
  return htab;
}




/**
 * Convert a string from UTF-16
 * FIXME: need to do a real conversion :) 
 *
 * @param dst The destination buffer
 * @param src The source buffer
 * @param size The max number of chars to convert (including terminating null byte)
 */
 
int gcrp_tochar(char *dst, char *src, size_t size) {
  int i;
  for (i = 0; (i < (size-1)) && (src[i << 1] != 0); i++) {
    dst[i] = src[i << 1];
  }
  dst[i] = 0;
}

/**
 * Convert a string to UTF-16
 * FIXME: need to do a real conversion :) 
 *
 * @param dst The destination buffer
 * @param src The source buffer
 * @param size The max number of chars to convert (including terminating null byte)
 */
 
int gcrp_fromchar(char *dst, char *src, size_t size) {
  int i;
  for (i = 0; (i < (size-1)) && (src[i] != 0); i++) {
    dst[i << 1] = src[i];
    dst[(i << 1) + 1] = 0;
  }
  dst[i << 1] = 0;
  dst[(i << 1) + 1] = 0;
}


/**
 * Attempt to read the socket to get a GCRP message.
 * The read will be blocking if the socket is blocking.
 *
 * @param sock The socket from which we read the message
 * @param buf The buffer in which we will store the message
 * @param length The allocated buffer space, in bytes
 * @return Size of the message (including GCRP header), or -1 for failure
 */

int gcrp_read(int sock, char *buf, int length) {
  int toread;
  int r;
  
  gcrp_hdr_t *hdr = (gcrp_hdr_t *) buf;
  /* XXX FIXME handle partial reads */
  /* read header */
  if ((r = recv(sock, buf, sizeof(gcrp_hdr_t), MSG_WAITALL) != sizeof(gcrp_hdr_t))) {
    garena_errno = GARENA_ERR_LIBC;
    return -1;
  }
  toread = ghtonl(hdr->msglen) - 1; 
  if (toread + sizeof(gcrp_hdr_t) > GCRP_MAX_MSGSIZE) {
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  /* read message body */
  if ((r = recv(sock, buf + sizeof(gcrp_hdr_t), toread, MSG_WAITALL) != toread)) {
    garena_errno = GARENA_ERR_LIBC;
    return -1;
  }
  return (toread + sizeof(gcrp_hdr_t)); 
}

/**
 * Processes a received GCRP message and calls any
 * registered callback to handle the message.
 *
 * @param buf The message
 * @param length Length of the message (including GCRP header)
 * @return 0 for success, -1 for failure
 */
 
int gcrp_input(gcrp_handtab_t *htab, char *buf, int length, void *roomdata) {
  gcrp_hdr_t *hdr = (gcrp_hdr_t *) buf;
  if (length < sizeof(gcrp_hdr_t)) {
    garena_errno = GARENA_ERR_PROTOCOL;
    IFDEBUG(fprintf(stderr, "[DEBUG/GCRP] Dropped short message.\n"));
    return -1;
  }
  if ((length - sizeof(gcrp_hdr_t) + 1) != ghtonl(hdr->msglen)) {
    IFDEBUG(fprintf(stderr, "[DEBUG/GCRP] Dropped malformed message."));
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  if ((hdr->msgtype < 0) || (hdr->msgtype >= GCRP_MSG_NUM) || (htab->gcrp_handlers[hdr->msgtype].fun == NULL)) {
    fprintf(deb, "[DEBUG/GCRP] Unhandled message of type: %x (payload size = %x)\n", hdr->msgtype, hdr->msglen - 1);
    fflush(deb);
  } else {
    
    if (htab->gcrp_handlers[hdr->msgtype].fun(hdr->msgtype, buf + sizeof(gcrp_hdr_t), length - sizeof(gcrp_hdr_t), htab->gcrp_handlers[hdr->msgtype].privdata, roomdata) == -1) {
      garena_perror("[WARN/GCRP] Error while handling message");
    }
  }
}


/**
  * Builds and send a GCRP message over a socket. 
  *
  * @param sock Socket used to send the GCRP message.
  * @param type Type of the message
  * @param payload Data contained in the message
  * @param length Length of the data (in bytes) 
  * @return 0 for success, -1 for failure
  */
int gcrp_output(int sock, int type, char *payload, int length) {
  static char buf[GCRP_MAX_MSGSIZE];
  gcrp_hdr_t *hdr = (gcrp_hdr_t *) buf;
  if (length + sizeof(gcrp_hdr_t) > GCRP_MAX_MSGSIZE) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  hdr->msglen = ghtonl(length + 1); /* the msgtype byte is counted in the msglen, as well as the payload */
  hdr->msgtype = type;
  memcpy(buf + sizeof(gcrp_hdr_t), payload, length);
  if (write(sock, buf, length + sizeof(gcrp_hdr_t)) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    return -1;
  }
  IFDEBUG(fprintf(stderr, "[DEBUG/GCRP] Sent a message of type %x (payload length = %x)\n", type, length));
  return 0;
}

/**
 * Builds and send a GCRP JOIN message over a socket
 *
 * @param sock Socket used to send the message
 * @param room_id The ROOM ID of the room to join
 * @return 0 for succes, -1 for failure
 */
int gcrp_send_join(int sock, int room_id) {
  static char buf[GCRP_MAX_MSGSIZE];
  gcrp_me_join_t *join = (gcrp_me_join_t *) buf;
  join->room_id = ghtonl(room_id);
  memcpy(buf + sizeof(gcrp_me_join_t), GCRP_JOINCODE, sizeof(GCRP_JOINCODE)-1);
  if (gcrp_output(sock, GCRP_MSG_JOIN, buf, sizeof(GCRP_JOINCODE) + sizeof(gcrp_me_join_t) - 1) == -1) {
    return -1;
  }
}

/**
 * Builds and send a GCRP TALK message over a socket
 *
 * @param sock Socket used to send the message
 * @param room_id The ROOM ID of the room to join
 * @param user_id The sender's user ID
 * @param text The text (null terminated)
 * @return 0 for succes, -1 for failure
 */
int gcrp_send_talk(int sock, int room_id, int user_id, char *text) {
  static char buf[GCRP_MAX_MSGSIZE];
  gcrp_talk_t *talk = (gcrp_talk_t *) buf;
  talk->room_id = ghtonl(room_id);
  talk->user_id = ghtonl(user_id);
  talk->length = ghtonl(strlen(text) << 1);
  gcrp_fromchar(talk->text, text, sizeof(buf) - sizeof(gcrp_talk_t));
  if (gcrp_output(sock, GCRP_MSG_TALK, buf, sizeof(gcrp_talk_t) + talk->length) == -1) {
    return -1;
  }
}

/**
 * Builds and send a GCRP STARTVPN or STOPVPN message over a socket
 *
 * @param sock Socket used to send the message
 * @param user_id The sender's user ID
 * @param vpn Set to 0 to disable VPN, set to any other value to enable
 * @return 0 for succes, -1 for failure
 */
int gcrp_send_togglevpn(int sock, int user_id, int vpn) {
  static char buf[GCRP_MAX_MSGSIZE];
  gcrp_togglevpn_t *toggle = (gcrp_togglevpn_t *) buf;
  toggle->user_id = ghtonl(user_id);
  if (gcrp_output(sock, vpn ? GCRP_MSG_STARTVPN : GCRP_MSG_STOPVPN, buf, sizeof(gcrp_togglevpn_t)) == -1) {
    return -1;
  }
}

/**
 * Builds and send a GCRP PART message over a socket
 *
 * @param sock Socket used to send the message
 * @param user_id The sender's user ID
 * @return 0 for succes, -1 for failure
 */
int gcrp_send_part(int sock, int user_id) {
  static char buf[GCRP_MAX_MSGSIZE];
  gcrp_part_t *part = (gcrp_part_t *) buf;
  part->user_id = ghtonl(user_id);
  if (gcrp_output(sock, GCRP_MSG_PART, buf, sizeof(gcrp_part_t)) == -1) {
    return -1;
  }
}


/**
 * Register a handler to be called on incoming messages of type "msgtype".
 *
 * @param msgtype The message type for which we define a handler
 * @param fun Pointer to handler function
 * @param privdata Pointer to private data (anything you want) that will be supplied to the called function
 * @return 0 for success, -1 for failure
 */
 
int gcrp_register_handler(gcrp_handtab_t *htab, int msgtype, gcrp_fun_t *fun, void *privdata) {
  if ((msgtype < 0) || (msgtype >= GCRP_MSG_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  
  if (htab->gcrp_handlers[msgtype].fun != NULL) {
    garena_errno = GARENA_ERR_INUSE;
    return -1;
  }
  htab->gcrp_handlers[msgtype].fun = fun;
  htab->gcrp_handlers[msgtype].privdata = privdata;
  return 0;
}

/**
 * Unregisters a handler associated with the specified message type.
 *
 * @param msgtype The message type for which we delete the handler
 * @return 0 for success, -1 for failure
 */
int gcrp_unregister_handler(gcrp_handtab_t *htab, int msgtype) {
  if ((msgtype < 0) || (msgtype >= GCRP_MSG_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  if (htab->gcrp_handlers[msgtype].fun == NULL) {
    garena_errno = GARENA_ERR_NOTFOUND;
    return -1;
  }
  htab->gcrp_handlers[msgtype].fun = NULL;
  htab->gcrp_handlers[msgtype].privdata = NULL;  
  return 0;
}


/**
 * Get the privdata associated with a handler.
 *
 * @param msgtype The message type of the handler we wish to retrieve the privdata
 * @return The privdata, or NULL if there is an error
 */
void* gcrp_handler_privdata(gcrp_handtab_t *htab, int msgtype) {
  if ((msgtype < 0) || (msgtype >= GCRP_MSG_NUM)) {
    garena_errno = GARENA_ERR_INVALID;
    return NULL;
  }
  if (htab->gcrp_handlers[msgtype].fun == NULL) {
    garena_errno = GARENA_ERR_NOTFOUND;
    return NULL;
  }
 return htab->gcrp_handlers[msgtype].privdata;
}

