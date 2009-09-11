#ifndef GARENA_GP2PP_H
#define GARENA_GP2PP_H 1

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <garena/config.h>
#include <garena/garena.h>


#define GP2PP_PORT 1513
#define GP2PP_MAX_SENDQ 65536
#define GP2PP_MAX_IN_TRANSIT 1024

#define GP2PP_MAGIC_LOCALIP (inet_addr("127.0.0.1"))

#define GP2PP_CONN_TS_DIVISOR 256
#define GP2PP_HELLO_INTERVAL 30
#define GP2PP_CONN_RETRANS_DELAY 5
#define GP2PP_CONN_RETRANS_CHECK 1
#define GP2PP_CLOSE_WAIT 10
#define GP2PP_CONN_TIMEOUT 30

#define GP2PP_MAX_MSGSIZE 65536

#define GP2PP_MSG_UDP_ENCAP 0x01
#define GP2PP_MSG_HELLO_REQ 0x02
#define GP2PP_MSG_IP_LOOKUP_REPLY 0x06
#define GP2PP_MSG_INITCONN 0x0b
#define GP2PP_MSG_CONN_PKT 0x0d
#define GP2PP_MSG_HELLO_REP 0x0F
#define GP2PP_MSG_ROOMINFO_REPLY 0x3F
#define GP2PP_MSG_NUM 0x40

#define GP2PP_CONN_MSG_FIN 0x01
#define GP2PP_CONN_MSG_ACK 0x0E
#define GP2PP_CONN_MSG_DATA 0x14
#define GP2PP_CONN_MSG_NUM 0x15

struct gp2pp_hdr_s {
  uint8_t msgtype;
  char unknown[3];
  uint32_t user_id;
} __attribute__ ((packed));
typedef struct gp2pp_hdr_s gp2pp_hdr_t;


struct gp2pp_conn_hdr_s {
  uint8_t msgtype;
  uint8_t msgsubtype;
  uint16_t ts_rel;
  uint32_t conn_id;
  uint32_t user_id;
  uint32_t seq1;
  uint32_t seq2;
  char payload[0];
} __attribute__ ((packed));
typedef struct gp2pp_conn_hdr_s gp2pp_conn_hdr_t;


struct gp2pp_initconn_s {
  uint32_t conn_id;
  uint32_t sip; /* always set to 127.0.0.1 apparently */
  uint16_t dport; /* !!! LITTLE ENDIAN !!! */
  uint16_t mbz;
} __attribute__ ((packed));
typedef struct gp2pp_initconn_s gp2pp_initconn_t;


struct gp2pp_udp_encap_s {
  uint16_t sport; /* !!! BIGENDIAN !!! */
  uint16_t mbz;
  uint16_t dport; /* !!! BIGENDIAN !!! */
  uint16_t mbz2;
  char payload[0];
} __attribute__ ((packed));
typedef struct gp2pp_udp_encap_s gp2pp_udp_encap_t;


struct gp2pp_hello_rep_s {
  uint32_t mbz;
  uint32_t user_id;
} __attribute__ ((packed));
typedef struct gp2pp_hello_rep_s gp2pp_hello_rep_t;

struct gp2pp_hello_req_s {
  uint32_t mbz;
  uint32_t mbz2;
} __attribute__ ((packed));
typedef struct gp2pp_hello_req_s gp2pp_hello_req_t;

struct gp2pp_roominfo_reply_s {
} __attribute__ ((packed));
typedef struct gp2pp_roominfo_reply_s gp2pp_roominfo_reply_t;

struct gp2pp_lookup_reply_s {
  char mbz[7];
  struct in_addr my_external_ip;
  uint16_t my_external_port;
  uint16_t mbz2;
} __attribute__ ((packed));

typedef struct gp2pp_lookup_reply_s gp2pp_lookup_reply_t;
typedef int gp2pp_fun_t(int type, void *payload, unsigned int length, void *privdata, unsigned int user_id, struct sockaddr_in *remote);
typedef int gp2pp_conn_fun_t(int subtype, void *payload, unsigned int length, void *privdata, unsigned int user_id, unsigned int conn_id, int seq1, int seq2, int ts_rel, struct sockaddr_in *remote);

typedef struct {
  gp2pp_fun_t *fun;
  void *privdata;
} gp2pp_handler_t;

typedef struct {
  gp2pp_conn_fun_t *fun;
  void *privdata;
} gp2pp_conn_handler_t;


typedef struct  {
  gp2pp_handler_t gp2pp_handlers[GP2PP_MSG_NUM];
  gp2pp_conn_handler_t gp2pp_conn_handlers[GP2PP_CONN_MSG_NUM];
} gp2pp_handtab_t;



int gp2pp_read(int sock, char *buf, unsigned int length, struct sockaddr_in *remote);

int gp2pp_output(int sock, int type, char *payload, unsigned int length, int user_id, struct sockaddr_in *remote);
int gp2pp_output_conn(int sock, int subtype, char *payload, unsigned int length, int user_id, unsigned int conn_id, int seq1, int seq2, int ts_rel, struct sockaddr_in *remote);
int gp2pp_input(gp2pp_handtab_t *tab, char *buf, unsigned int length, struct sockaddr_in *remote);

int gp2pp_send_initconn(int sock, int from_ID, unsigned int conn_id, int dport, int sip, struct sockaddr_in *remote);
int gp2pp_send_hello_reply(int sock, int from_ID, int to_ID, struct sockaddr_in *remote);
int gp2pp_send_hello_request(int sock, int from_ID, struct sockaddr_in *remote);
int gp2pp_send_udp_encap(int sock, int from_ID, int sport, int dport, char *payload, unsigned int length, struct sockaddr_in *remote);
int gp2pp_request_roominfo(int sock, int my_id, int server_ip, int server_port);

int gp2pp_get_tsnow();
int gp2pp_do_ip_lookup(int sock, int server_ip, int server_port);
int gp2pp_register_handler(gp2pp_handtab_t *tab,int msgtype, gp2pp_fun_t *fun, void *privdata);
int gp2pp_unregister_handler(gp2pp_handtab_t *tab,int msgtype);
void* gp2pp_handler_privdata(gp2pp_handtab_t *tab, int msgtype);
int gp2pp_register_conn_handler(gp2pp_handtab_t *tab,int msgtype, gp2pp_conn_fun_t *fun, void *privdata);
int gp2pp_unregister_conn_handler(gp2pp_handtab_t *tab,int msgtype);
void* gp2pp_conn_handler_privdata(gp2pp_handtab_t *tab, int msgtype);
gp2pp_handtab_t *gp2pp_alloc_handtab (void);
int gp2pp_init();
void gp2pp_fini();
int gp2pp_new_conn_id(void);

#endif
