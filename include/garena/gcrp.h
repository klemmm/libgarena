#ifndef GARENA_GCRP_H
#define GARENA_GCRP_H 1

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <garena/config.h>
#include <garena/garena.h>


#define GCRP_PORT 8687


#define GCRP_MAX_MSGSIZE 65536

/*#define GCRP_JOINCODE "\x00\x00\x00\x00\x3e\x00\x00\x00\x14\xda\x4f\xf3\x78\x9c\x9b\xd3\xa4\xc1\x58\x90\x58\x9a\x63\x68\x6c\x6c\x6e\xc4\x00\x01\x6e\x41\x0c\x0c\x8c\x6c\x8c\x0c\x31\x8d\xcb\x66\x1d\x58\xc1\xc8\x0f\x12\x63\x7d\xc9\xfa\x92\x01\x0b\xb8\xce\x83\x45\x90\x15\xa8\x1f\x48\x01\x00\xd7\x29\x0a\xf4\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x65\x34\x66\x34\x35\x64\x65\x30\x30\x32\x35\x34\x62\x37\x62\x30\x37\x62\x64\x62\x36\x36\x30\x30\x34\x30\x63\x30\x64\x32\x62\x35\x00\x00" */



#define GCRP_MSG_JOIN 0x22
#define GCRP_MSG_PART 0x23
#define GCRP_MSG_TALK 0x25
#define GCRP_MSG_MEMBERS 0x2c
#define GCRP_MSG_WELCOME 0x30
#define GCRP_MSG_STARTVPN 0x3a
#define GCRP_MSG_STOPVPN 0x39

#define GCRP_MSG_NUM 0x100

struct gcrp_hdr_s {
  uint32_t msglen;
  uint8_t msgtype;
} __attribute__ ((packed));

typedef struct gcrp_hdr_s gcrp_hdr_t;

struct gcrp_me_join_s {
  uint32_t room_id;
} __attribute__ ((packed));
typedef struct gcrp_me_join_s gcrp_me_join_t;

struct gcrp_welcome_s {
  uint32_t room_id;
  wchar_t text[0];
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


typedef int gcrp_fun_t(int type, void *payload, int length, void *privdata, void *roomdata);

typedef struct {
  gcrp_fun_t *fun;
  void *privdata;
} gcrp_handler_t;

typedef struct  {
  gcrp_handler_t gcrp_handlers[GCRP_MSG_NUM];
} gcrp_handtab_t;


int gcrp_read(int sock, char *buf, int length);

int gcrp_output(int sock, int type, char *payload, int length);
int gcrp_input(gcrp_handtab_t *,char *buf, int length, void *roomdata);

int gcrp_register_handler(gcrp_handtab_t *,int msgtype, gcrp_fun_t *fun, void *privdata);
int gcrp_unregister_handler(gcrp_handtab_t *, int msgtype);
void* gcrp_handler_privdata(gcrp_handtab_t *, int msgtype);
int gcrp_send_join(int sock, int room_id);
int gcrp_send_togglevpn(int sock, int user_id, int vpn);
int gcrp_send_part(int sock, int user_id);
int gcrp_send_talk(int sock, int room_id, int user_id, char *text);
gcrp_handtab_t *gcrp_alloc_handtab (void);



#endif
