#ifndef GARENA_GSP_H
#define GARENA_GSP_H 1

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <garena/config.h>
#include <garena/garena.h>


#define GSP_PORT 7456


#define GSP_MAX_MSGSIZE 65536
#define GSP_MSG_LOGIN 0x1F
#define GSP_MSG_AUTH_FAIL 0x2E
#define GSP_MSG_MYINFO 0x45
#define GSP_MSG_SESSION_INIT 0xAD
#define GSP_MSG_SESSION_INIT_REPLY 0xAE
#define GSP_MSG_HELLO 0xD3
#define GSP_MSG_NUM 0xDF

#define GSP_KEYSIZE 32
#define GSP_IVSIZE 16
#define GSP_PWHASHSIZE 32

#define GSP_SESSION_MAGIC 0xF00F
#define GSP_SESSION_MAGIC2 0x00AD
#define GSP_HELLO_MAGIC 0x00000782

#define GSP_BLOCKSIZE 16
#define GSP_BLOCKMASK (GSP_BLOCKSIZE - 1)
#define GSP_BLOCK_ROUND(n) (((n) & GSP_BLOCKMASK) ? (((n) & (~GSP_BLOCKMASK)) + GSP_BLOCKSIZE) : (n))

struct gsp_sessionhdr_s {
  uint32_t size;
  uint16_t magic;
} __attribute__ ((packed));
typedef struct gsp_sessionhdr_s gsp_sessionhdr_t;

struct gsp_hdr_s {
  uint8_t msgtype;
} __attribute__ ((packed));
typedef struct gsp_hdr_s gsp_hdr_t;

struct gsp_hello_s {
  char country[2];
  uint32_t magic;
} __attribute__ ((packed));
typedef struct gsp_hello_s gsp_hello_t;

struct gsp_myinfo_s {
  uint32_t mbz;
  uint32_t unknown1;
  uint32_t user_id;
  char name[16];
  char country[2];
  uint16_t mbz2;
  char unknown2;
  char level;
  char unknown3;
  char mbz3;
  struct in_addr external_ip;
  uint32_t mbz4;
  uint32_t mbz5;
  uint16_t external_port; /* BIG ENDIAN */
  char mbz6[22];
  uint16_t unknown4;
  char mbz7[19];
  char unknown5[3];
} __attribute__ ((packed));

typedef struct gsp_myinfo_s gsp_myinfo_t;

struct gsp_login_s {
  uint32_t mbz;
  char name[16];
  uint32_t mbz2;
  uint32_t pwhash_size;
  char pwhash[GSP_PWHASHSIZE];
  uint8_t mbz3;
  struct in_addr internal_ip;
  uint16_t internal_port; /* BIG ENDIAN */
  uint8_t mbz4;
} __attribute__ ((packed));
typedef struct gsp_login_s gsp_login_t;


typedef int gsp_fun_t(int type, void *payload, int length, void *privdata);

typedef struct {
  gsp_fun_t *fun;
  void *privdata;
} gsp_handler_t;

typedef struct  {
  gsp_handler_t gsp_handlers[GSP_MSG_NUM];
} gsp_handtab_t;


int gsp_open_session(int sock, char *key, char *iv); 

int gsp_read(int sock, char *buf, int length);
int gsp_output(int sock, int type, char *payload, int length, char *key, char *iv);
int gsp_input(gsp_handtab_t *,char *buf, int length, char *key, char *iv);
int gsp_register_handler(gsp_handtab_t *,int msgtype, gsp_fun_t *fun, void *privdata);
int gsp_unregister_handler(gsp_handtab_t *, int msgtype);
void* gsp_handler_privdata(gsp_handtab_t *, int msgtype);
int gsp_send_login(int sock, char *login, char *md5pass, char *key, char *iv);
gsp_handtab_t *gsp_alloc_handtab (void);

#endif
