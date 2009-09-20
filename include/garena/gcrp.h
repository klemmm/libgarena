#ifndef GARENA_GCRP_H
#define GARENA_GCRP_H 1

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <garena/config.h>
#include <garena/gsp.h>
#include <garena/garena.h>


#define GCRP_PORT 8687


#define GCRP_MAX_MSGSIZE 8192

#define GCRP_MSG_JOIN 0x22
#define GCRP_MSG_PART 0x23
#define GCRP_MSG_TALK 0x25
#define GCRP_MSG_MEMBERS 0x2c
#define GCRP_MSG_WELCOME 0x30
#define GCRP_MSG_JOIN_FAILED 0x36
#define GCRP_MSG_STARTVPN 0x3a
#define GCRP_MSG_STOPVPN 0x39

#define GCRP_MSG_NUM 0x3b

#define GCRP_PWHASHSIZE GSP_PWHASHSIZE

struct gcrp_hdr_s {
  uint32_t msglen;
  uint8_t msgtype;
} __attribute__ ((packed));
typedef struct gcrp_hdr_s gcrp_hdr_t;

struct gcrp_me_join_s {
  uint32_t room_id;
  uint32_t mbz;
  uint32_t infolen;
  uint32_t infocrc;
} __attribute__ ((packed));
typedef struct gcrp_me_join_s gcrp_me_join_t;

typedef gsp_myinfo_t gcrp_join_block_t;
struct gcrp_me_join_suffix_s {
  char mbz[15];
  char pwhash[GCRP_PWHASHSIZE];
  uint16_t mbz2;
} __attribute__ ((packed));
typedef struct gcrp_me_join_suffix_s gcrp_me_join_suffix_t;

struct gcrp_welcome_s {
  uint32_t room_id;
  char text[0];
} __attribute__ ((packed));
typedef struct gcrp_welcome_s gcrp_welcome_t;

struct gcrp_member_s {
  uint32_t user_id;
  char name[16];
  char country[2];
  uint16_t mbz;
  char unknown1;
  char level;
  char unknown2;
  char vpn;
  struct in_addr external_ip, internal_ip;
  uint32_t mbz2;
  uint16_t external_port; /* BIG ENDIAN */
  uint16_t internal_port; /* BIG ENDIAN */
  uint8_t virtual_suffix;
  char unknown3[19];
  
} __attribute__ ((packed));
typedef struct gcrp_member_s gcrp_member_t;
typedef struct gcrp_member_s gcrp_join_t;

struct gcrp_talk_s {
  uint32_t room_id;
  uint32_t user_id;
  uint32_t length;
  char text[0];
} __attribute__ ((packed));
typedef struct gcrp_talk_s gcrp_talk_t;

struct gcrp_togglevpn_s {
  uint32_t user_id;
} __attribute__ ((packed));
typedef struct gcrp_togglevpn_s gcrp_togglevpn_t;

struct gcrp_part_s {
  uint32_t user_id;
} __attribute__ ((packed));
typedef struct gcrp_part_s gcrp_part_t;

struct gcrp_memberlist_s {
  uint32_t room_id;
  uint32_t num_members;
  gcrp_member_t members[0];
} __attribute__ ((packed));
typedef struct gcrp_memberlist_s gcrp_memberlist_t;


typedef int gcrp_fun_t(int type, void *payload, unsigned int length, void *privdata, void *roomdata);

typedef struct {
  gcrp_fun_t *fun;
  void *privdata;
} gcrp_handler_t;

typedef struct  {
  gcrp_handler_t gcrp_handlers[GCRP_MSG_NUM];
} gcrp_handtab_t;


int gcrp_read(int sock, char *buf, unsigned int length);

int gcrp_output(int sock, int type, char *payload, unsigned int length);
int gcrp_input(gcrp_handtab_t *,char *buf, unsigned int length, void *roomdata);

int gcrp_register_handler(gcrp_handtab_t *,int msgtype, gcrp_fun_t *fun, void *privdata);
int gcrp_unregister_handler(gcrp_handtab_t *, int msgtype);
void* gcrp_handler_privdata(gcrp_handtab_t *, int msgtype);
int gcrp_send_join(int sock, unsigned int room_id, gcrp_join_block_t *join_block, char *pwhash);
int gcrp_send_togglevpn(int sock, int user_id, int vpn);
int gcrp_send_part(int sock, int user_id);
int gcrp_send_talk(int sock, unsigned int room_id, int user_id, char *text);
gcrp_handtab_t *gcrp_alloc_handtab (void);
int gcrp_tochar(char *dst, char *src, size_t size);
int gcrp_fromchar(char *dst, char *src, size_t size);

#endif
