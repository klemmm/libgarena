 /**
  * @file
  * File implementing the Garena Server Protocol (GSP)
  */
  
#include <sys/types.h>
#include <sys/socket.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/aes.h>

#include <garena/garena.h>
#include <garena/gsp.h>
#include <garena/error.h>
#include <garena/util.h>


static char gsp_rsa_private[] =
"-----BEGIN RSA PRIVATE KEY-----\n"
"MIIEogIBAAKCAQEA3XK9BWuIHIS3R6za4WU/mQ0WlsPD/ErtzSTw2ZmbhI0lyKcQ\n"
"Ugk0aRIOaq4vTE+EpRtI6hvhH4AIm+15sWPqxpfuNR0Dvigse+BhuypFsqI+AWiL\n"
"dj5RrPSzrLcqWgjE5zSjUG4OmxS4NJJRY9UMNaEhtqsrgrFFj4iMX07bz6Joyp85\n"
"CHpGJhmFjPwU60OlUkGKwvs6TeQXUZlH9ypzXkNAhF4uDchTgEX7A/8yrqHzPx7/\n"
"r2T0Lww7kp106ACdy9wXTpq5v3tmfNZbZ7K0bEB4g8Ez43Hew1P5b/tabUV4pZL0\n"
"LkvDCA78ll8FHeuJjZA3+DKlEgyA2EWTs98VTQIDAQABAoIBAC65evCd08ZQqmtR\n"
"KY3NUzHz9QQyojOli69xT/BZ3NqG/aXsuiDVGF3jFW+k+Q3c6Vv8+dGLuGBxH1/n\n"
"J3oqXuswO26xhIym5Vvt6DEZpkMewH6DlImKdKlNqGuU6ja9Cu7NyHe8ARDvuj49\n"
"cTbjSQQ3z2k/jJqy1L6ITTX+6ZpRgZd9m/Ng5O0GBcoSiUjysfLgs5m5lHWCojL+\n"
"ppxqhsWXDM2ejIFGncGok798NNps+OkAM9EwEHcEI7qBo/UEsgXwnmlUvsyBvtq3\n"
"7NS/znsJlOT/PfbS3i0gIac6AmA0qh86zN+uC5yl44aY+WpwPqBua6eeKkpk3xAo\n"
"LrCRxHECgYEA/689gaRf0ihJ5WpD/cq6XLFwxuu4/CmmNjYpTwol2S3lGnq03RLZ\n"
"FhklvMKIkhfuaOLyrHgUWaYZVr2KBUU81qwHTVEZeN6rWPeXTsfgBnpShIYYXqBN\n"
"ePyqVDuISs44Lsi74fhSNrqai6ow6GQYlZewcdjS2zVc35G1of/cWNMCgYEA3biv\n"
"L49okrATQfBbdl5L6hueqNc8pfrv6EKYcw5SE48fFeHCToorKpaf4kf7GemITldD\n"
"29FFwukhyt1rJJI9Kvj6jKN49QZr3xS1d8QY0lOHnRRRLIg3x+VaD7RYOWuHbqs1\n"
"MKyzgeKkpWq6EkuaW2ZEQwL6cvzqGsbo1CRqBV8CgYBMNqEf1q5VR3sXbkCMEvTQ\n"
"EngqYzNFvuhzelt/2ueDQCHtbawhxa993csY4+evnICNNTDe5gAy5MbiyyasAYJr\n"
"/uVCT61HESCEKXEpo3yMkcOtCweSlTfim3XuG7y5h5TJpT4T0mA3PhI5FWb0rnmB\n"
"hbCrjtTzUIm5foZkno7AzwKBgD2PTXSTCKHRqUchiQNwYvt497BBMmGTLpD6DIHF\n"
"dBxiHGti5yQPULTeZT3aZmlnYaT+raSWkhvvxqYgm+Lnh3wq7MWnjanaQpEJmujJ\n"
"1WpwLrL6NR98IqCpmTvLAsPOiye6+WWuTZi+aKBU5Zy2yQCfgExqw0ax2f3dRD/C\n"
"bH1ZAoGAOJ/pLNpetFyE/aaD0jBfMA6UACdutjWT4vFGmk/GwBh3/sHoMbON2c/P\n"
"OeEM/N3/ZODOZHzXB1ALgWIjeoP2TegBfbniHf2d+j1/VRMTiYEMv3ws06YiWMLJ\n"
"ioX2ZNntCCPlIti48TeFs0etqcHQgQ5rSLblyde3RIuRcqatQko=\n"
"-----END RSA PRIVATE KEY-----\n";

void gsp_fini(void) {
}

int gsp_init(void) {
  return 0;
}

inline static char gsp_pad(int type) {
  switch(type) {
    case GSP_MSG_HELLO:
      return 0x09;
    case GSP_MSG_LOGIN:
      return 0x0A;
    default:
      return 0;
  }
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

int gsp_read(int sock, char *buf, unsigned int length) {
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
 
int gsp_input(gsp_handtab_t *htab, char *buf, unsigned int length, unsigned char *key, unsigned char *iv) {
  AES_KEY aeskey;
  unsigned char tmp_iv[GSP_IVSIZE];
  unsigned char plaintext[GSP_MAX_MSGSIZE];
  uint32_t *size = (uint32_t *) buf;
  gsp_hdr_t *hdr = (gsp_hdr_t *) plaintext;
  
  if (length < sizeof(uint32_t)) {
    garena_errno = GARENA_ERR_PROTOCOL;
    IFDEBUG(fprintf(stderr, "[DEBUG/GSP] Dropped short message.\n"));
    return -1;
  }
  if ((length - sizeof(uint32_t)) != (ghtonl(*size) & 0xFFFFFF)) {
    IFDEBUG(fprintf(stderr, "[DEBUG/GSP] Dropped malformed message."));
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  if ((length - sizeof(uint32_t)) & 0xF) {
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  
  AES_set_decrypt_key(key, GSP_KEYSIZE << 3, &aeskey);
  memcpy(tmp_iv, iv, sizeof(tmp_iv));
  
  AES_cbc_encrypt((unsigned char*)buf + sizeof(uint32_t), plaintext, length - sizeof(uint32_t), &aeskey, tmp_iv, AES_DECRYPT);
  
  if ((hdr->msgtype >= GSP_MSG_NUM) || (htab->gsp_handlers[hdr->msgtype].fun == NULL)) {
    fprintf(deb, "[DEBUG/GSP] Unhandled message of type: %x (payload size = %x)\n", hdr->msgtype, ghtonl(*size & 0xFFFFFF));
    fflush(deb);
  } else {
/*    fprintf(deb, "[DEBUG/GSP] Handled message of type: %x (payload size = %x)\n", hdr->msgtype, ghtonl(*size & 0xFFFFFF)); */
    fflush(deb);
    
    if (htab->gsp_handlers[hdr->msgtype].fun(hdr->msgtype, plaintext + sizeof(gsp_hdr_t), length - sizeof(gsp_hdr_t), htab->gsp_handlers[hdr->msgtype].privdata) == -1) {
/*       garena_perror("[WARN/GSP] Error while handling message"); */
    }
  }
  return 0;
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
int gsp_output(int sock, int type, char *payload, unsigned int length, unsigned char *key, unsigned char *iv) {
  unsigned char plaintext[GSP_MAX_MSGSIZE];
  unsigned char ciphertext[GSP_MAX_MSGSIZE];
  unsigned char tmp_iv[GSP_IVSIZE];
  gsp_hdr_t *hdr = (gsp_hdr_t *) plaintext;
  AES_KEY aeskey;
  uint32_t *size = (uint32_t *) ciphertext;

  if (sizeof(uint32_t) + GSP_BLOCK_ROUND(length + sizeof(gsp_hdr_t)) > GSP_MAX_MSGSIZE) {
    garena_errno = GARENA_ERR_INVALID;
    return -1;
  }
  
  AES_set_encrypt_key(key, GSP_KEYSIZE << 3, &aeskey);
  memcpy(tmp_iv, iv, sizeof(tmp_iv));
  
  *size = ghtonl(GSP_BLOCK_ROUND(length + sizeof(gsp_hdr_t)) | 0x01000000);
  if (*size % 16) abort();
  memset(plaintext, gsp_pad(type), sizeof(plaintext));
  hdr->msgtype = type;
  memcpy(plaintext + sizeof(gsp_hdr_t), payload, length);
  AES_cbc_encrypt(plaintext, ciphertext + sizeof(uint32_t), GSP_BLOCK_ROUND(length + sizeof(gsp_hdr_t)), &aeskey, tmp_iv, AES_ENCRYPT);
  if (write(sock, ciphertext, sizeof(uint32_t) + GSP_BLOCK_ROUND(length + sizeof(gsp_hdr_t))) == -1) {
    garena_errno = GARENA_ERR_LIBC;
    return -1;
  }
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


int gsp_send_login(int sock, const char *login, char *md5pass, unsigned char *key, unsigned char *iv, unsigned int internal_ip, int internal_port) {
  gsp_login_t msg;
  static char *hex_digit = "0123456789abcdef";
  int i,j;
  
  memset(&msg, 0, sizeof(msg));
  strncpy(msg.name, login, 16);
  msg.name[15] = 0;
  msg.pwhash_size = GSP_PWHASHSIZE;
  msg.internal_ip.s_addr = internal_ip;
  msg.internal_port = htons(internal_port);
  for (i = 0, j = 0; i < GSP_PWHASHSIZE; i += 2, j++) {
    msg.pwhash[i] = hex_digit[(md5pass[j] >> 4) & 0xF];
    msg.pwhash[i+1] = hex_digit[md5pass[j] & 0xF];
  }
  return gsp_output(sock, GSP_MSG_LOGIN, (char*) &msg, sizeof(msg), key, iv);
}

int gsp_send_hello(int sock, unsigned char *key, unsigned char *iv) {
  gsp_hello_t msg;
  memcpy(msg.country, "EN", 2);
  msg.magic = ghtonl(GSP_CLIENT_VERSION);
  return gsp_output(sock, GSP_MSG_HELLO, (char*) &msg, sizeof(msg), key, iv);  
}

int gsp_open_session(int sock, unsigned char *key, unsigned char *iv) {
  RSA *rsa = NULL;
  BIO *bio = NULL;
  unsigned char *ciphertext = NULL;
  int signsize;
  int rcode = -1;
  unsigned char plaintext[GSP_IVSIZE + GSP_KEYSIZE + sizeof(uint16_t)];
  uint16_t *magic;
  gsp_sessionhdr_t hdr;
  
  bio = BIO_new(BIO_s_mem());
  
  if (bio == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    goto out;
  }
  
  BIO_puts(bio, gsp_rsa_private);

  rsa = PEM_read_bio_RSAPrivateKey(bio, NULL, NULL, NULL);
  if (rsa == NULL) {
    garena_errno = GARENA_ERR_UNKNOWN;
    fprintf(deb, "[GSP] Failed to import RSA private key\n");
    goto out;
  }
  ciphertext = malloc(RSA_size(rsa));
  if (ciphertext == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    goto out;
  }
  
  if (RAND_pseudo_bytes(key, GSP_KEYSIZE) == 0)
    fprintf(deb, "[WARN/GSP] Session key may be weak\n");
  if (RAND_pseudo_bytes(iv, GSP_IVSIZE) == 0)
    fprintf(deb, "[WARN/GSP] Session IV may be weak\n");
  
  memcpy(plaintext, key, GSP_KEYSIZE);
  memcpy(plaintext + GSP_KEYSIZE, iv, GSP_IVSIZE);
  magic = (uint16_t *) (plaintext + GSP_KEYSIZE + GSP_IVSIZE);
  *magic = GSP_SESSION_MAGIC;
  memset(ciphertext, RSA_size(rsa), 0);
  signsize = RSA_private_encrypt(sizeof(plaintext), plaintext, ciphertext, rsa, RSA_PKCS1_PADDING);   
  if (signsize == -1) {
    garena_errno = GARENA_ERR_UNKNOWN;
    goto out;
  }
  
  hdr.size = ghtonl(signsize + sizeof(hdr.magic));
  hdr.magic = GSP_SESSION_MAGIC2;
  if (write(sock, &hdr, sizeof(hdr)) != sizeof(hdr)) {
    garena_errno = GARENA_ERR_LIBC;
    goto out;
  }
  if (write(sock, ciphertext, signsize) != signsize) {
    garena_errno = GARENA_ERR_LIBC;
    goto out;
  }
  
  rcode = 0;
  
  out:
   if (ciphertext)
     free(ciphertext);
   if (rsa) 
     RSA_free(rsa);
   if (bio)
     BIO_free(bio);
   return rcode;
}
