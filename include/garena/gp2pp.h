#ifndef GARENA_GP2PP_H
#define GARENA_GP2PP_H 1

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <garena/config.h>
#include <garena/garena.h>



#define GP2PP_MAX_MSGSIZE 65536

#define GP2PP_MSG_UDP_ENCAP 0x01
#define GP2PP_MSG_HELLO_REQ 0x02
#define GP2PP_MSG_HELLO_REP 0x0F
#define GP2PP_MSG_NUM 0x10

struct gp2pp_hdr_s {
  uint8_t msgtype;
  char unknown[3];
  uint32_t user_ID;
} __attribute__ ((packed));
typedef struct gp2pp_hdr_s gp2pp_hdr_t;

struct gp2pp_udp_encap_s {
  uint16_t sport; /* !!! BIGENDIAN !!! */
  uint16_t mbz;
  uint16_t dport; /* !!! BIGENDIAN !!! */
  uint16_t mbz2;
} __attribute__ ((packed));
typedef struct gp2pp_udp_encap_s gp2pp_udp_encap_t;


struct gp2pp_hello_rep_s {
  uint32_t mbz;
  uint32_t user_ID;
} __attribute__ ((packed));
typedef struct gp2pp_hello_rep_s gp2pp_hello_rep_t;

typedef int gp2pp_funptr_t(int type, void *payload, int length, void *privdata, int user_ID, struct sockaddr_in *remote);

typedef struct {
  gp2pp_funptr_t *fun;
  void *privdata;
} gp2pp_handler_t;

static gp2pp_handler_t gp2pp_handlers[GP2PP_MSG_NUM]; 


int gp2pp_read(int sock, char *buf, int length, struct sockaddr_in *remote);

int gp2pp_output(int sock, int type, char *payload, int length, int user_ID, struct sockaddr_in *remote);
int gp2pp_input(char *buf, int length, struct sockaddr_in *remote);

int gp2pp_send_hello_reply(int sock, int from_ID, int to_ID, struct sockaddr_in *remote);


int gp2pp_register_handler(int msgtype, gp2pp_funptr_t *fun, void *privdata);
int gp2pp_unregister_handler(int msgtype);
void* gp2pp_handler_privdata(int msgtype);

#endif
