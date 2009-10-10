
/**
 * @file ghl.h
 * 
 * The header for the Garena High-Level module.
 *
 */


#ifndef GARENA_GHL_H
#define GARENA_GHL_H 1
#include <stdint.h>
#include <garena/garena.h>
#include <garena/gcrp.h>
#include <garena/gp2pp.h>
#include <garena/gsp.h>
#include <garena/util.h>
#include <sys/select.h>


/**
 * Default time (in seconds) to wait before join room timeout.
 */ 
#define GHL_JOIN_TIMEOUT 600


/**
 * Event received when a join room operation (initiated by you) is completed (or has failed). 
 */
#define GHL_EV_ME_JOIN 0
/**
 * Event received when someone joins a room you are on.
 * The associated event data type is @ref ghl_me_join_t
 */
#define GHL_EV_JOIN 1
/**
 * Event received when someone leaves a room you are on. The member structure will be
 * removed and free()'d immediately after your handler returns. 
 * The associated event data type is @ref ghl_join_t
 */
#define GHL_EV_PART 2
/**
 * Event received when someone on the room starts or stop a game.
 * The associated event data type is @ref ghl_part_t
 */
#define GHL_EV_TOGGLEVPN 3
/**
 * Event received when someone talks on the room.
 * The associated event data type is @ref ghl_talk_t
 */
#define GHL_EV_TALK 5
/**
 * Event received when someone sends you a virtual UDP packet on the VPN.
 * The associated event data type is @ref ghl_udp_encap_t
 */
#define GHL_EV_UDP_ENCAP 6
/**
 * Event received when there is an incoming virtual connection on the VPN from someone. 
 * When you receive this event, the connection is already accepted and established.
 * Since there is no way in Garena to reject an incoming connection, if you don't
 * want it, the only way is to close it with @ref ghl_close_conn.
 * The associated event data type is @ref ghl_conn_incoming_t
 */
#define GHL_EV_CONN_INCOMING 7
/**
 * Event received when there is incoming data on a (already established) virtual connection. 
 * Your handler functions need to returns the number of bytes accepted. If you return
 * a number that is less than the incoming data size, another @ref GHL_EV_CONN_INCOMING event
 * will be generated later to let you handle the remaining data. You should try to not use
 * this behavior, because it is not very efficient. Instead, try to call @ref ghl_process only
 * if you are prepared to handle any event. 
 * The associated event data type is @ref ghl_conn_recv_t
 */
#define GHL_EV_CONN_RECV 8
/**
 * Event received when the peer closes a virtual connection. The connection handle will be 
 * eventually free()'d by the garena library after your handler returns. 
 * The associated event data type is @ref ghl_conn_fin_t
 */
#define GHL_EV_CONN_FIN 9
/**
 * Event received when the connection to the room server is lost.
 * This means that you are not in the room anymore. Room handle will be free()'d immediately
 * after your handler returns, and all virtual connections will be closed.
 * The associated event data type is @ref ghl_room_disc_t
 */
#define GHL_EV_ROOM_DISC 10

/**
 * Event received when the connection to the main server is completed (or has failed)
 * The associated event data type is @ref ghl_servconn_t
 */
#define GHL_EV_SERVCONN 11
/**
 * The number of events.
 */
#define GHL_EV_NUM 12

/**
 * The number of seconds to wait for main server connection
 */
#define GHL_SERVCONN_TIMEOUT 600

/**
 * Failure
 */
#define GHL_EV_RES_SUCCESS 0
/** 
 * Success
 */
#define GHL_EV_RES_FAILURE -1

/** 
 * The interval (seconds) between query for room member count
 */
#define GHL_ROOMINFO_QUERY_INTERVAL 300

/**
 * The type for timer handler functions
 *
 * @param privdata The privdata given at handler installation
 * @return 0 for success, -1 for failure
 */
typedef int ghl_timerfun_t(void *privdata);

/**
 * 
 * Timer structure
 * 
 */
typedef struct {
  ghl_timerfun_t *fun; /**< Handler function */
  void *privdata; /**< Private data */
  int when; /**< When (in seconds after the Epoch) the timer must activate */
} ghl_timer_t;


struct ghl_serv_s;
struct ghl_rh_s;
struct ghl_member_s;
struct ghl_ch_s;

/**
 *
 * The type for event handling function.
 *
 * @param serv The server handle
 * @param event The event type (GHL_EV_.....) 
 * @param event_data The event data (need to cast to ghl_..._t*) 
 * @param privdata The private data
 * @return -1 means error, but any other value is event-specific.
 */
typedef int ghl_fun_t(struct ghl_serv_s *serv, int event, void *event_data, void *privdata);

/**
 * Handler structure
 */
typedef struct {
  ghl_fun_t *fun; /**< Handler function */
  void *privdata; /**< Private data */
} ghl_handler_t;

/**
 * Structure describing my (the client) informations on Garena
 */
typedef struct {
  uint32_t unknown1; 
  unsigned int user_id; /**< User ID */
  char name[17]; /**< Name */
  char country[3]; /**< Country code */
  char unknown2;  
  char level; /**< Experience level */
  char unknown3;
  struct in_addr external_ip; /**< External IP */
  int external_port; /**< External port */
  struct in_addr internal_ip; /**< Internal (LAN) IP */
  int internal_port; /**< Internal (before NAT) port */
  uint16_t unknown4; 
  char unknown5[3];
} ghl_myinfo_t;


/**
 * Server handle structure
 */
typedef struct ghl_serv_s {
  int servsock; /**< Socket to talk to main server (GSP, tcp port 7456) */
  int peersock; /**< Socket to talk to other clients (GP2PP, udp port 1513) */
  int gp2pp_rport; /**< GP2PP remote port, usually 1513 but configurable */ 
  int gp2pp_lport; /**< GP2PP local port, usually 1513 but configurable */ 
  int server_ip; /**< Main server IP */
  unsigned char session_key[GSP_KEYSIZE]; /**< AES KEY for GSP session with main server */
  unsigned char session_iv[GSP_IVSIZE]; /**< AES IV for GSP session with main server */
  int auth_ok; /**< Did we complete authentication yet? */
  int lookup_ok; /**< Did we complete IP/port lookup yet? */
  int connected; /**< Are we fully connected (auth_ok & lookup_ok) to the server yet? */
  int need_free; /**< Do we need to free this handle (because the server connection failed) ? */
  char md5pass[GSP_PWHASHSIZE >> 1]; /**< Garena account password, hashed in MD5 */
  ghl_myinfo_t my_info; /**< My (this client) informations */
  struct ghl_rh_s *room; /**< Pointer to room handle if we are in a room, or NULL otherwise */
  ghl_handler_t ghl_handlers[GHL_EV_NUM]; /**< Array of GHL event handlers associated with this server */
  gp2pp_handtab_t *gp2pp_htab; /**< For GP2PP events that needs to be processed by GHL */
  gcrp_handtab_t *gcrp_htab;  /**< For GCRP events that needs to be processed by GHL */
  gsp_handtab_t *gsp_htab; /**< For GSP events that needs to be processed by GHL */
  ghl_timer_t *hello_timer; /**< Timer to send periodic GP2PP HELLO message to room members */
  ghl_timer_t *conn_retrans_timer; /**< Timer to try retransmission of lost virtual connection segments, and manage virtual connection timeout and cleanup */
  ghl_timer_t *roominfo_timer; /**< Timer to send queries for room usage count */
  ghl_timer_t *servconn_timeout; /**< Timer to handle server connection timeout */
  ihash_t roominfo; /**< Hashtable (key=room id, value=pointer to integer) to know the room usage count */
  int mtu;
} ghl_serv_t;



/**
 * Room handle structure
 */
typedef struct ghl_rh_s {
  int roomsock; /**< Room to talk to the room server (GCRP, tcp port 8687) */
  unsigned int room_id; /**< Room ID */
  struct ghl_member_s *me; /**< Pointer to the room member that is our client */
  ghl_serv_t *serv; /**< Pointer to server handle */
  ihash_t members; /**< Hashtable (key=user id, value= pointer to @ref ghl_member_t) to get room members */
  int got_welcome; /**< Did we receive welcome message yet? */
  int got_members; /**< Did we receive room member list yet? */
  ghl_timer_t *timeout; /**< Timer to handle room join timeout */
  int joined; /**< Did we fully join the room yet? */
  char welcome[GCRP_MAX_MSGSIZE]; /**< Welcome message of the room. */
  ihash_t conns; /**< Hashtable(key=conn id, value pointer to @ref ghl_ch_t) to get virtual connections on the VPN associated with this room */
} ghl_room_t;

/**
 * Member structure. 
 */
typedef struct ghl_member_s {
  uint32_t user_id; /**< User ID */
  char name[17];  /**< Member name */
  char country[3]; /**< Member Country code */
  uint16_t mbz; 
  char level; /**< Member experience level */
  char vpn; /**< Is the member currently playing (VPN enabled)? */
  struct in_addr external_ip; /**< External IP of the member */
  struct in_addr internal_ip; /**< Internal IP of the member */
  struct in_addr effective_ip; /**< Effective IP (the IP from which we actually receive packets) of the member */
  uint32_t mbz2; 
  uint16_t external_port; /**< External port of the member */
  uint16_t internal_port; /**< Internal port of the member */
  uint16_t effective_port; /**< Effective port of the member */
  uint8_t virtual_suffix; /**< Virtual suffix (i.e. the virtual IP of the member is 192.168.29.virtual_suffix)  */
  int conn_ok; /**< Do we have direct bidirectionnal communication with the member? (else VPN communication with the member won't work) */
} ghl_member_t;

/**
 * @ref GHL_EV_ME_JOIN event data structure.
 */
typedef struct {
  int result; /**< Join outcome (0 for success, -1 for failure) */
  ghl_room_t *rh; /**< Room handle */
} ghl_me_join_t;

/**
 * @ref GHL_EV_JOIN_T event data structure.
 */

typedef struct {
  ghl_member_t *member; /**< The member that joined */
  ghl_room_t *rh; /**< Room handle */
} ghl_join_t;

/**
 * @ref GHL_EV_PART event data structure.
 */

typedef struct {
  ghl_member_t *member; /**< The member that left */
  ghl_room_t *rh; /**< Room handle */
} ghl_part_t;
/**
 * @ref GHL_EV_TALK event data structure.
 */

typedef struct {
  ghl_member_t *member; /**< The member that talked */
  ghl_room_t *rh; /**< Room handle */
  char *text; /**< The text */
} ghl_talk_t;

/**
 * @ref GHL_EV_TOGGLEVPN event data structure.
 */

typedef struct {
  ghl_member_t *member; /**< The member that started/stopped a game */
  ghl_room_t *rh; /**< Room handle */
  int vpn; /**< 0 if stopped a game, 1 if started */
} ghl_togglevpn_t;
/**
 * @ref GHL_EV_UDP_ENCAP event data structure.
 */
typedef struct {
  ghl_member_t *member; /**< The member that sent the packet */
  int sport; /**< UDP source port */
  int dport; /**< UDP destination port */
  unsigned int length; /**< UDP payload length */
  char *payload; /**< Pointer to payload */
} ghl_udp_encap_t;


/**
 * Virtual connection handle structure
 */
typedef struct ghl_ch_s {
  unsigned int conn_id; /**< Connection ID */
  int ts_base;
  int snd_una, snd_next, rcv_next, rcv_next_deliver;
  llist_t sendq;
  llist_t recvq;
#define GHL_CSTATE_ESTABLISHED 2
#define GHL_CSTATE_CLOSING_IN 3
#define GHL_CSTATE_CLOSING_OUT 4
  int cstate;
  int ts_ack;
  gtime_t rto;
  gtime_t srtt;
  gtime_t last_xmit;
  unsigned int flightsize;
  unsigned int cwnd;
  unsigned int ssthresh; 
  ghl_serv_t *serv; /**< Server handle */
  ghl_member_t *member; /**< The peer */
  int finseq; 
} ghl_ch_t;   

/**
 * Structure for an individual packet in a virtual connection
 */
 
typedef struct {
  ghl_ch_t *ch; /**< Connection where this packet belongs */
  int ts_rel;
  unsigned int length;
  int seq;
  int did_fast_retrans;
  gtime_t xmit_ts;
  gtime_t rto;
  int retrans;
  unsigned int partial;
  gtime_t first_trans;
  char *payload;
} ghl_ch_pkt_t;

/**
 * @ref GHL_EV_CONN_INCOMING event data structure.
 */

typedef struct {
  ghl_ch_t *ch; /**< The connection handle associated with this new connection */
  int dport; /**< The destination (local) port */
} ghl_conn_incoming_t;
/**
 * @ref GHL_EV_ROOM_DISC event data structure.
 */

typedef struct {
  ghl_room_t *rh; /**< The room handle for which the connection was lost */
} ghl_room_disc_t;
/**
 * @ref GHL_EV_CONN_RECV event data structure.
 */

typedef struct { 
  ghl_ch_t *ch; /**< The connection handle for which we received data */
  unsigned int length; /**< payload length */
  char *payload; /**< payload */
} ghl_conn_recv_t;

/**
 * @ref GHL_EV_CONN_FIN event data structure.
 */

typedef struct {
  ghl_ch_t *ch; /**< The connection handle that is terminated */
} ghl_conn_fin_t;
/**
 * @ref GHL_EV_SERVCONN event data structure.
 */
typedef struct {
  int result; /**< Server connection result (0 for success, -1 for failure) */
} ghl_servconn_t;




ghl_serv_t *ghl_new_serv(char *name, char *password, int server_ip, int server_port, int gp2pp_lport, int gp2pp_rport, int mtu);
void ghl_free_serv(ghl_serv_t *serv);

ghl_room_t *ghl_join_room(ghl_serv_t *serv, int room_ip, int room_port, unsigned int room_id);
int ghl_leave_room(ghl_room_t *rh);

ghl_member_t *ghl_member_from_id(ghl_room_t *rh, unsigned int user_id);
ghl_member_t *ghl_global_find_member(ghl_serv_t *serv, unsigned int user_id);

int ghl_togglevpn(ghl_room_t *rh, int vpn);

int ghl_talk(ghl_room_t *rh, char *text);

int ghl_udp_encap(ghl_serv_t *serv, ghl_member_t *member, int sport, int dport, char *payload, unsigned int length);

int ghl_fill_fds(ghl_serv_t *serv, fd_set *fds);
int ghl_process(ghl_serv_t *serv, fd_set *fds);

int ghl_register_handler(ghl_serv_t *serv, int event, ghl_fun_t *fun, void *privdata);
int ghl_unregister_handler(ghl_serv_t *serv, int event);
void* ghl_handler_privdata(ghl_serv_t *serv, int event);

ghl_room_t *ghl_room_from_id(ghl_serv_t *serv, unsigned int room_id);

ghl_timer_t * ghl_new_timer(int when, ghl_timerfun_t *fun, void *privdata);
void ghl_free_timer(ghl_timer_t *timer);

int ghl_fill_tv(ghl_serv_t *, struct timeval *tv);
ghl_ch_t *ghl_conn_connect(ghl_serv_t *serv, ghl_member_t *member, int port);
void ghl_conn_close(ghl_serv_t *serv, ghl_ch_t *ch);
int ghl_conn_send(ghl_serv_t *serv, ghl_ch_t *ch, char *payload, unsigned int length);
ghl_ch_t *ghl_conn_from_id(ghl_room_t *rh, unsigned int conn_id);
inline unsigned int ghl_max_conn_pkt(ghl_serv_t *serv);
int ghl_num_members(ghl_serv_t *serv, unsigned int room_id);

#endif
