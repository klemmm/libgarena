 /**
  * @file
  * File implementing the Garena ChatRoom Protocol (GCRP)
  */
  
#include <sys/types.h>
#include <sys/socket.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>

#include <garena/garena.h>
#include <garena/gcrp.h>
#include <garena/error.h>
#include <garena/util.h>

#include <mhash.h>

void gcrp_fini(void) {
}

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
  unsigned int i;
  for (i = 0; (i < (size-1)) && (src[i << 1] != 0); i++) {
    dst[i] = src[i << 1];
  }
  dst[i] = 0;
  return 0;
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
  unsigned int i;
  for (i = 0; (i < (size-1)) && (src[i] != 0); i++) {
    dst[i << 1] = src[i];
    dst[(i << 1) + 1] = 0;
  }
  dst[i << 1] = 0;
  dst[(i << 1) + 1] = 0;
  return 0;
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

int gcrp_read(int sock, char *buf, unsigned int length) {
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
 
int gcrp_input(gcrp_handtab_t *htab, char *buf, unsigned int length, void *roomdata) {
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
  if ((hdr->msgtype >= GCRP_MSG_NUM) || (htab->gcrp_handlers[hdr->msgtype].fun == NULL)) {
    fprintf(deb, "[DEBUG/GCRP] Unhandled message of type: %x (payload size = %x)\n", hdr->msgtype, hdr->msglen - 1);
    fflush(deb);
  } else {
    
    if (htab->gcrp_handlers[hdr->msgtype].fun(hdr->msgtype, buf + sizeof(gcrp_hdr_t), length - sizeof(gcrp_hdr_t), htab->gcrp_handlers[hdr->msgtype].privdata, roomdata) == -1) {
      garena_perror("[WARN/GCRP] Error while handling message");
      return -1;
    }
  }
  return 0;
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
int gcrp_output(int sock, int type, char *payload, unsigned int length) {
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
int gcrp_send_join(int sock, unsigned int room_id, gcrp_join_block_t *join_block, char *md5pass) {
  static char buf[GCRP_MAX_MSGSIZE];
  static char hex_digit[] = "0123456789abcdef";
  static char pwhash[GCRP_PWHASHSIZE];
  int r;
  int i,j;
  unsigned int compressed_size;
  gcrp_me_join_t *join = (gcrp_me_join_t *) buf;
  gcrp_me_join_suffix_t *suffix;
  MHASH mh;
  z_stream strm;
            
  strm.zalloc = Z_NULL; 
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;  
  
  r = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
  if (r != Z_OK) {
    garena_errno = GARENA_ERR_NORESOURCE;
    return -1;
  }
  
  strm.avail_in = sizeof(gcrp_join_block_t);
  strm.next_in = (char*)join_block;
  strm.avail_out = sizeof(buf) - sizeof(gcrp_me_join_t) - sizeof(gcrp_me_join_suffix_t);
  strm.next_out = buf + sizeof(gcrp_me_join_t);
  
  deflate(&strm, Z_FINISH);
  deflateEnd(&strm);
  compressed_size = sizeof(buf) - sizeof(gcrp_me_join_t) - sizeof(gcrp_me_join_suffix_t) - strm.avail_out;
  suffix = (gcrp_me_join_suffix_t *) (buf + sizeof(gcrp_me_join_t) + compressed_size);
  
  memset(join, 0, sizeof(gcrp_me_join_t));
  memset(suffix, 0, sizeof(gcrp_me_join_suffix_t));
  join->room_id = ghtonl(room_id);      
  join->infolen = sizeof(join->infocrc) + compressed_size;
  for (i = 0, j = 0; i < GSP_PWHASHSIZE; i += 2, j++) {
      suffix->pwhash[i] = hex_digit[(md5pass[j] >> 4) & 0xF];
      suffix->pwhash[i+1] = hex_digit[md5pass[j] & 0xF];
  }
  mh = mhash_init(MHASH_CRC32B);
  mhash(mh, buf + sizeof(gcrp_me_join_t), compressed_size);
  mhash_deinit(mh, &join->infocrc);

  return gcrp_output(sock, GCRP_MSG_JOIN, buf, sizeof(gcrp_me_join_t) + compressed_size + sizeof(gcrp_me_join_suffix_t));
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
int gcrp_send_talk(int sock, unsigned int room_id, int user_id, char *text) {
  static char buf[GCRP_MAX_MSGSIZE];
  gcrp_talk_t *talk = (gcrp_talk_t *) buf;
  talk->room_id = ghtonl(room_id);
  talk->user_id = ghtonl(user_id);
  talk->length = ghtonl(strlen(text) << 1);
  gcrp_fromchar(talk->text, text, sizeof(buf) - sizeof(gcrp_talk_t));
  return gcrp_output(sock, GCRP_MSG_TALK, buf, sizeof(gcrp_talk_t) + talk->length);
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
  return gcrp_output(sock, vpn ? GCRP_MSG_STARTVPN : GCRP_MSG_STOPVPN, buf, sizeof(gcrp_togglevpn_t));
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
  return gcrp_output(sock, GCRP_MSG_PART, buf, sizeof(gcrp_part_t));
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

