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

#define GSP_MSG_MYINFO 0xDE

#define GSP_MSG_NUM 0x100

struct gsp_hdr_s {
  uint8_t msgtype;
} __attribute__ ((packed));

typedef struct gsp_hdr_s gsp_hdr_t;

struct gsp_myinfo_s {
} __attribute__ ((packed));
typedef struct gsp_myinfo_s gsp_myinfo_t;

struct gsp_login_s {
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


int gsp_read(int sock, char *buf, int length);

int gsp_output(int sock, int type, char *payload, int length);

int gsp_input(gsp_handtab_t *,char *buf, int length);

int gsp_register_handler(gsp_handtab_t *,int msgtype, gsp_fun_t *fun, void *privdata);
int gsp_unregister_handler(gsp_handtab_t *, int msgtype);
void* gsp_handler_privdata(gsp_handtab_t *, int msgtype);
int gsp_send_login(int sock, char *login, char *md5pass);
gsp_handtab_t *gsp_alloc_handtab (void);

#endif
