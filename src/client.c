  #include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h> 
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <ncurses.h>
#include <garena/error.h>
#include <garena/garena.h>
#include <garena/gcrp.h>
#include <garena/gp2pp.h>
#include <garena/ghl.h>
#include <garena/util.h>


#define MAP(p) ((p) ^ 0xDEAD)

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define IP_OFFSET1 0x7D
#define IP_OFFSET2 29
#define IP_OFFSET3 33


#define MTU 1492

int tunmax;

typedef struct {
  int sock;
  int servsock;
#define SI_STATE_CONNECTING 0
#define SI_STATE_ACCEPTING 1
#define SI_STATE_ESTABLISHED 2
  int state;
  ghl_ch_t *ch;
} sockinfo_t;

char tun_name[IFNAMSIZ];
char fwdtun_name[IFNAMSIZ];
int tun_fd, fwdtun_fd;


unsigned int max_conn_pkt;
unsigned int routing_host=INADDR_NONE;

int quit = 0;
int need_free_ctx = 0;

typedef struct  {
  WINDOW *text;
  WINDOW *cmd;
  struct termios attr;
} screen_ctx_t;


ghl_ctx_t *ctx = NULL;
screen_ctx_t screen;

#define HASH_SIZE 256

typedef struct hash_s *hash_t;
typedef struct hashitem_s *hashitem_t;
typedef void *hash_keytype;

struct hashitem_s { 
  hash_keytype key;
  void *value;  
  struct hashitem_s *next;
};

struct hash_s {
  unsigned int size;
  struct hashitem_s **h;
};

static int hash_num(hash_t hash);
static int hash_put(hash_t hash, hash_keytype key, void *value);
static void *hash_get(hash_t hash, hash_keytype key);
static int hash_del(hash_t hash, hash_keytype key);
static void hash_free(hash_t hash);
static void hash_free_val(hash_t hash);
static hash_t hash_init();

hash_t ch2sock;
llist_t socklist;
llist_t roomlist;
typedef struct {
  char name[256];
  int ip;
  int id;
} room_t;

static llist_t read_roomlist() {
  char filename[256];
  char name[256];
  char ip[256];
  int num_ip;
  int id;
  room_t *room;
  FILE *f;
  roomlist = llist_alloc();
  snprintf(filename, sizeof(filename), "%s/.garenarc", getenv("HOME"));
  filename[sizeof(filename)-1] = 0;
  f = fopen(filename, "r");
  if (f == NULL)
    return roomlist;
  while (!feof(f)) {
    if (fscanf(f, "%s %s %d", name, ip, &id) != 3)
      continue;
      
    num_ip = inet_addr(ip);
    if (num_ip == INADDR_NONE)
      num_ip = atoi(ip);
    room = malloc(sizeof(room_t));
    strncpy(room->name, name, sizeof(room->name));
    room->ip = num_ip;
    room->id = id;
    llist_add_head(roomlist, room);
  }
  fclose(f);
  return roomlist;
}


static inline unsigned int hash_func(hash_keytype id) {
  int i = (int)id;
  return (i ^ (i >> 8) ^ (i >> 16) ^ (i >> 24));
} 

int hash_num(hash_t hash) {
  hashitem_t item;
  unsigned int i;
  int num = 0;
  
  for (i = 0; i < hash->size; i++) {
    for (item = hash->h[i]; item != NULL; item = item->next)
      num++;
  }
  return num;
  
}  

int hash_put(hash_t hash, hash_keytype key, void *value) {
  hashitem_t item;
  unsigned int hv = hash_func(key) % hash->size;

  item = malloc(sizeof(struct hashitem_s));
  if (item == NULL) {
    return -1;
  }
   
  item->key = key;
  item->value = value;

  item->next = hash->h[hv];
  hash->h[hv] = item;

  return 0;
}

void *hash_get(hash_t hash, hash_keytype key) {
  hashitem_t item;
  unsigned int hv = hash_func(key) % hash->size;

  for (item = hash->h[hv]; item != NULL; item = item->next) {
    if (key == item->key)
      return item->value;
  }
   
  return NULL;
}

int hash_del(hash_t hash, hash_keytype key) {
  hashitem_t item, *prev;
  unsigned int hv = hash_func(key) % hash->size;

  prev = &hash->h[hv];
  item = NULL;
  for (item = hash->h[hv]; item != NULL; item = item->next) {
    if (key == item->key)
    {
      *prev = item->next;
      free(item);
      return 0;  
    } else prev = &item->next;
  }
   
  return -1;
}

void hash_free(hash_t hash) {
  unsigned int i;
  hashitem_t item,old;
  
  for (i = 0 ; i < hash->size; i++) {
    item = hash->h[i];
    while(item != NULL) {
      old = item;
      item = item->next;
      free(old);
    }
  }  
  free(hash->h);
  free(hash);   
}

void hash_free_val(hash_t hash) {
  unsigned int i;
  hashitem_t item,old;
  
  for (i = 0 ; i < hash->size; i++) {
    item = hash->h[i];
    while(item != NULL) {
      old = item;
      item = item->next;
      free(old->value);
      free(old);
    }
  }  
  free(hash->h);
  free(hash);   
}

hash_t hash_init() {
  hash_t hash;
  int size = HASH_SIZE;
  
  hash = malloc(sizeof(struct hash_s));
  if(hash == NULL)
    return  NULL; 
  hash->h = malloc(size * sizeof(struct hashitem_s*));
  if (hash->h == NULL) {
    free(hash);
    return (NULL);
  }
  hash->size = size;
  memset(hash->h, 0, size*sizeof(struct hashitem_s*));
  return(hash);
}

void screen_init(screen_ctx_t *screen) {
  initscr();
  cbreak();
  noecho();
  screen->text = newwin(LINES-2, COLS, 0, 0);
  screen->cmd = newwin(2, COLS, LINES-2, 0);
  scrollok(screen->text, true);
  scrollok(screen->cmd, true);
  keypad(screen->cmd, true);

  mvwhline(screen->cmd, 0, 0, 0, COLS);
  mvwprintw(screen->cmd, 1, 0, "> ");
  wrefresh(screen->text);
  wrefresh(screen->cmd);
}



void screen_output(screen_ctx_t *screen, char *buf) {
    wprintw(screen->text, "%s", buf);
    wrefresh(screen->text);
    wrefresh(screen->cmd);
}

int screen_input(screen_ctx_t *screen, char *buf, int len) {
    static int cur = 0;
    int x,y;
    int tmp;
    int r;
    
    if (cur == 0) {
      r = mvwgetch(screen->cmd, 1, 2);
          
    } else r = wgetch(screen->cmd);
    
/*    mvwhline(screen->cmd, 0, 0, 0, COLS);
    mvwprintw(screen->cmd, 1, 0, "> ");
*/

    wrefresh(screen->text);
    wrefresh(screen->cmd);
    
    if (r == ERR)
      return -1;
    
    if (r == KEY_RESIZE) {
      wresize(screen->text, LINES-2, COLS);
      wresize(screen->cmd, 2, COLS);
      delwin(screen->cmd);
      screen->cmd = newwin(2, COLS, LINES-2, 0);
      scrollok(screen->cmd, true);
      keypad(screen->cmd, true);
      
      mvwhline(screen->cmd, 0, 0, 0, COLS);
      mvwprintw(screen->cmd, 1, 0, "> ");

      wrefresh(screen->text);
      wrefresh(screen->cmd);
      cur = 0;
      
      return 0;
    }
    if (r == KEY_BACKSPACE) {
      if (cur > 0) {
        getyx(screen->cmd, y,x);
        mvwprintw(screen->cmd, y, x-1, " ");
        mvwprintw(screen->cmd, y, x-1, "");
        wrefresh(screen->cmd);
        cur--;
      }
      return 0;
    }
          
    if ((cur == (len-1)) || (r == '\n')) {
      buf[cur] = 0;
      tmp = cur;
      cur = 0;
      mvwhline(screen->cmd, 1, 0, ' ', COLS);
      mvwprintw(screen->cmd, 1, 0, "> ");
      return tmp;
    } else {
      wprintw(screen->cmd, "%c", r);
      wrefresh(screen->cmd);
      buf[cur] = r;
      cur++;
      return 0;
    }
}


int resolve(char *addr) {
  if (inet_addr(addr) == INADDR_NONE) {
      struct hostent *he;
      fflush(stdout);
      he=gethostbyname(addr);
      if(he==NULL) {
        return INADDR_NONE;
      }
      return (*(int *)he->h_addr);
  }
  return(inet_addr(addr));
}     


int ip_chksum(char *buffer, int l)
{
  u_short *w = (u_short *)buffer;
  int sum    = 0;
  u_short pad_val;
  int pad = l % 2;
  if (pad) {
    pad_val = buffer[l - 1];
  }
   
  l >>= 1;
  while( l-- )
    sum += *w++;   
  
  if (pad)
    sum += pad_val;
  sum = (sum >> 16) + (sum & 0xffff);
  return(~((sum >> 16) + sum));
}



static int set_nonblock(int sock) {
  int flags;
  
  flags = fcntl(sock, F_GETFL, 0);
  if (flags == -1)
    return -1;
  return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

int handle_conn_incoming(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  int sock;
  int r;
  ghl_conn_incoming_t *conn_incoming = event_param;
  struct sockaddr_in fsocket;
  sockinfo_t *si;
  r = -1;
  sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock != -1) {
    fsocket.sin_family = AF_INET;
    fsocket.sin_addr.s_addr = inet_addr(FWD_NETWORK) | (conn_incoming->ch->member->virtual_suffix << 24); 
    fsocket.sin_port = htons(conn_incoming->dport);
    r = set_nonblock(sock);
    if (r != -1)
    r = connect(sock, (struct sockaddr *) &fsocket, sizeof(fsocket));
    if ((r != -1) || (errno == EINPROGRESS)) {
      si = malloc(sizeof(sockinfo_t));
      si->sock = sock;
      si->servsock = -1;
      si->state = (r == -1) ? SI_STATE_CONNECTING : SI_STATE_ESTABLISHED;
      si->ch = conn_incoming->ch;
      hash_put(ch2sock, conn_incoming->ch, si);
      fprintf(deb,"Adding SI for: %x (connect)\n", si->ch->conn_id);
      fflush(deb);
      
      llist_add_tail(socklist, si);
      return 0;
    } else {
      close(sock);
    }
  }
  
  ghl_conn_close(ctx, conn_incoming->ch);
  return 0;
}

int handle_conn_fin(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  sockinfo_t *si;
  ghl_conn_fin_t *conn_fin = event_param;
  ghl_ch_t *ch = conn_fin->ch;
  si = hash_get(ch2sock, ch);
  fprintf(deb, "Received FIN for conn %x\n", ch->conn_id);
  fflush(deb);
  if (si->sock != -1)
    close(si->sock);	
  if (si->servsock != -1)
    close(si->servsock);
  hash_del(ch2sock, ch);
  llist_del_item(socklist, si);
  fprintf(deb, "freeing SI associated w/ %x\n", si->ch->conn_id);
  fflush(deb);
  free(si);
  return 0;
}

int handle_conn_recv(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  char *buf;
  int r;
  ghl_conn_recv_t *conn_recv = event_param;
  ghl_ch_t *ch = conn_recv->ch;
  int sock;
  sockinfo_t *si;
  buf = malloc(conn_recv->length);
  memcpy(buf, conn_recv->payload, conn_recv->length );
  si = hash_get(ch2sock, ch);
  sock = si->sock;
  r= write(sock, buf, conn_recv->length);
  free(buf);
  return r;
}

int handle_servconn(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  char buf[256];
  ghl_servconn_t *servconn = event_param;
  if (servconn->result == GHL_EV_RES_SUCCESS) {
    snprintf(buf, sizeof(buf), "Connected to server (my external ip/port is %s:%u).\n", inet_ntoa(ctx->my_info.external_ip), ctx->my_info.external_port);
    screen_output(&screen, buf);
  } else {
    screen_output(&screen, "Connection to server failed.\n");
    need_free_ctx = 1;
  }
  return 0;
}

int handle_me_join(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_me_join_t *join = event_param;
  cell_t iter;
  char cmd[128];
  char buf[512];
  ghl_member_t *member;
  if (join->result == GHL_EV_RES_SUCCESS) {
    screen_output(&screen, join->rh->welcome);
    screen_output(&screen, "\n");
    screen_output(&screen, "Room members [not playing]: ");
    for (iter = llist_iter(join->rh->members); iter; iter = llist_next(iter)) {
      member = llist_val(iter);
      if (!member->vpn) {
        snprintf(buf, 512, "%s[%x] ", member->name, member->user_id);
        screen_output(&screen, buf);
      }
    }
    screen_output(&screen, "\n");
    screen_output(&screen, "Room members [playing]: ");
    for (iter = llist_iter(join->rh->members); iter; iter = llist_next(iter)) {
      member = llist_val(iter);
      if (member->vpn) {
        snprintf(buf, 512, "%s[%x] ", member->name, member->user_id);
        screen_output(&screen, buf);
      }
    }
    screen_output(&screen, "\n");

    snprintf(cmd, 128, "sudo ifconfig %s 192.168.29.%u netmask 255.255.255.0", tun_name, join->rh->me->virtual_suffix);
    system(cmd);
    snprintf(cmd, 128, "sudo ifconfig %s 192.168.28.%u netmask 255.255.255.0", fwdtun_name, join->rh->me->virtual_suffix);
    system(cmd);
  } else {
    screen_output(&screen, "Room join failed\n");
  }
  return 0;
}

int handle_talk(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_talk_t *talk = event_param;
  char buf[512];
  snprintf(buf, 512, "%x <%s> %s\n", talk->rh->room_id, talk->member->name, talk->text);
  screen_output(&screen, buf);
  return 0;
}

int handle_join(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_join_t *join = event_param;
  char buf[512];
  snprintf(buf, 512, "%x %s[%x] joined the room.\n", join->rh->room_id, join->member->name, join->member->user_id);
  screen_output(&screen, buf);
  return 0;
}

int handle_part(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_part_t *part = event_param;
  char buf[512];
  snprintf(buf, 512, "%x %s left the room.\n", part->rh->room_id, part->member->name);
  screen_output(&screen, buf);
  return 0;
}

int handle_disc(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  screen_output(&screen, "Lost connection to room server.\n");
  return 0;
}

int handle_udp_encap(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_udp_encap_t *udp_encap = event_param;
  struct ip *iph;
  struct udphdr *udph;
  ghl_rh_t *rh;
  char *buf;
  rh = ctx ? ctx->room : NULL;
  if (rh == NULL) {
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  buf = malloc(udp_encap->length + sizeof(struct ip) + sizeof(struct udphdr));
  if (buf == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    return -1;
  }
  iph = (struct ip*) buf;
  udph = (struct udphdr*) (buf + sizeof(struct ip));

  iph->ip_v = 4;
  iph->ip_hl = 5;
  iph->ip_tos = 0;
  iph->ip_id = 0;
  iph->ip_len = htons(sizeof(struct ip) + sizeof(struct udphdr) + udp_encap->length);
  iph->ip_p = IPPROTO_UDP;
  iph->ip_off = 0;
  iph->ip_ttl = 0xFF;
  iph->ip_sum = 0;
  iph->ip_src.s_addr = (udp_encap->member->virtual_suffix << 24) | inet_addr(GARENA_NETWORK);
  iph->ip_dst.s_addr = (routing_host != INADDR_NONE) ? routing_host : ((rh->me->virtual_suffix << 24) | inet_addr(GARENA_NETWORK));
  iph->ip_sum = ip_chksum(buf, sizeof(struct ip));
  
  udph->source = htons(udp_encap->sport);
  udph->dest = htons(udp_encap->dport);
  udph->check = 0;
  udph->len = htons(sizeof(struct udphdr) + udp_encap->length);
  memcpy(buf + sizeof(struct ip) + sizeof(struct udphdr), udp_encap->payload, udp_encap->length);
  if (write(tun_fd, buf, udp_encap->length + sizeof(struct ip) + sizeof(struct udphdr)) == -1) {
    perror("write to tunnel");
    garena_errno = GARENA_ERR_LIBC;
    free(buf);
    return -1;
  }
  free(buf);
  return 0;
}

int handle_togglevpn(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_togglevpn_t *togglevpn = event_param;
  char buf[512];
  snprintf(buf, 512, "%x %s %s a game.\n", togglevpn->rh->room_id, togglevpn->member->name, togglevpn->vpn ? "started" : "stopped");
  screen_output(&screen, buf);
  return 0;
}



int tun_alloc(char *dev)
{
      struct ifreq ifr;
      int fd, err;

      if( (fd = open("/dev/net/tun", O_RDWR)) < 0 ) {
         return -1;
      }

      memset(&ifr, 0, sizeof(ifr));

      ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
      if( *dev )
         strncpy(ifr.ifr_name, dev, sizeof(ifr.ifr_name));

      err = ioctl(fd, TUNSETIFF, (void*) &ifr);
      if (err < 0) {
         close(fd);
         return err;
      } 
      strncpy(dev, ifr.ifr_name, IFNAMSIZ);
      return fd;
}

static void drop_privileges() {
  
  if (setuid(getuid()) == -1) {
    fprintf(stderr, "failed to drop privileges\n");
    exit (-1);
  }
  
  /* better safe than sorry */
  if (setuid(0) != -1) {
    fprintf(stderr, "failed to drop privileges\n");
    exit (-1);
  }
}

void l4d_patchpkt(int virtual_suffix, char *buf, int length) {
  int myip = (virtual_suffix  << 24) | inet_addr(GARENA_NETWORK);
  int myip_r = htonl(myip);
  int routing_host_r = htonl(routing_host);
  if (length < (IP_OFFSET1 + 4))
    return;

          /* try to translate server IP in announce packet */
          if (memcmp(buf + IP_OFFSET1, &routing_host, 4) == 0) {
            IFDEBUG(printf("[NET] Translated announcement packet.(1)\n"));
            memcpy(buf + IP_OFFSET1, &myip, 4);
          }
          if (memcmp(buf + IP_OFFSET2, &routing_host_r, 4) == 0) {
            IFDEBUG(printf("[NET] Translated announcement packet(2).\n"));
            memcpy(buf + IP_OFFSET2, &myip_r, 4);
          }
          if (memcmp(buf + IP_OFFSET3, &routing_host_r, 4) == 0) {
            IFDEBUG(printf("[NET] Translated announcement packet(3).\n"));
            memcpy(buf + IP_OFFSET3, &myip_r, 4);
          }
}

int tcp_chksum(char *buffer, int length) {
  struct pseudo_hdr {  /* rfc 793 tcp pseudo-header */
         unsigned int saddr, daddr;
         char mbz;
         char ptcl;
         unsigned short tcpl;
  };
  char checkbuf[65536];

  struct pseudo_hdr *ph = (void*) (checkbuf + (sizeof(struct ip) - sizeof(struct pseudo_hdr)));

  struct ip ip_backup;
  struct ip *ip_header = (void*) checkbuf;
  int sum;

  memset(checkbuf, 0, 65536);
  memcpy(checkbuf, buffer, length);
  memcpy(&ip_backup, ip_header, sizeof(struct iphdr));
  memset(ph, 0, sizeof(struct pseudo_hdr));

 /* fill RFC793 TCP pseudo-header for the checksum */
  ph->saddr=ip_backup.ip_src.s_addr;
  ph->daddr=ip_backup.ip_dst.s_addr;
  ph->mbz=0;
  ph->ptcl=IPPROTO_TCP;
  ph->tcpl=htons(length - sizeof(struct iphdr));

  if ((length % 2) != 0)
    length++;
  sum = ip_chksum((char*) ph, length - ((char*)ph - (char*) ip_header));
  memcpy(ip_header, &ip_backup,sizeof(struct iphdr));
  return(sum);
}

#define FWDTUN_TO_TUN 0
#define TUN_TO_FWDTUN 1
int mangle_packet(ghl_rh_t *rh, char *buf, unsigned int size, int direction) {
  struct ip *iph;
  int r;
  struct sockaddr_in remote;
  unsigned int remotelen;
  sockinfo_t *si;
  int optval;
  int sock;
  ghl_member_t *cur;
  ghl_member_t *member;
  cell_t iter;
  struct sockaddr_in local;
  struct tcphdr *tcph;
  unsigned int network_source = (direction == FWDTUN_TO_TUN) ? inet_addr(FWD_NETWORK) : inet_addr(GARENA_NETWORK);
  unsigned int network_dest = (direction == FWDTUN_TO_TUN) ? inet_addr(GARENA_NETWORK) : inet_addr(FWD_NETWORK);
    
  if (size < (sizeof(struct ip) + sizeof(struct tcphdr)))
    return 0;
  iph = (struct ip *) buf;
  tcph = (struct tcphdr *) (buf + sizeof(struct ip));
  if (iph->ip_p != IPPROTO_TCP)
    return 0;

  if ((direction == TUN_TO_FWDTUN) && (routing_host != INADDR_NONE)) {
    if (iph->ip_src.s_addr != routing_host)
      return 0;
  } else {
    if (iph->ip_src.s_addr != ((rh->me->virtual_suffix << 24) | network_source))
      return 0;
  }
    
  if ((iph->ip_dst.s_addr & 0xFFFFFF) != network_source)
    return 0;
  
  iph->ip_src.s_addr = (iph->ip_dst.s_addr & 0xFF000000) | network_dest;
  if ((direction == FWDTUN_TO_TUN) && (routing_host != INADDR_NONE)) {
    iph->ip_dst.s_addr = routing_host;
  } else iph->ip_dst.s_addr = ((rh->me->virtual_suffix << 24) | network_dest);

  if (direction == FWDTUN_TO_TUN) {
    tcph->source = MAP(tcph->source);
  } else {
    tcph->dest = MAP(tcph->dest);
  }
  iph->ip_sum = 0;
  iph->ip_sum = ip_chksum((char*) buf, sizeof(struct ip));
  tcph->check = 0;
  tcph->check = tcp_chksum(buf, size);
  if ((direction == TUN_TO_FWDTUN) && (tcph->syn) && (!tcph->ack) && ((sock = socket(PF_INET, SOCK_STREAM, 0)) != -1)) {
    member = NULL;
    for (iter = llist_iter(rh->members) ; iter ; iter = llist_next(iter)) {
      cur = llist_val(iter);
      if (cur->virtual_suffix == (iph->ip_src.s_addr >> 24)) {
        member = cur;
        break;
      }
    }
    if (member == NULL) {
      return 0;
    }

    local.sin_family = PF_INET;
    local.sin_port = tcph->dest;
    local.sin_addr.s_addr = network_dest | (rh->me->virtual_suffix << 24);
    optval = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if ((bind(sock, (struct sockaddr *)&local, sizeof(local)) != -1) && (listen(sock,5) != -1)) {
      set_nonblock(sock);
      remotelen = sizeof(remote);
      r = accept(sock, (struct sockaddr *) &remote, &remotelen);
      
      if ((r != -1) || (errno == EWOULDBLOCK)) {
        si = malloc(sizeof(sockinfo_t));
        si->sock = r;
        si->state = (r == -1) ? SI_STATE_ACCEPTING : SI_STATE_ESTABLISHED;
        si->servsock = sock;
        if (r != -1) {
          close(si->servsock);
          si->servsock = -1;
        }
  
        si->ch = ghl_conn_connect(rh->ctx, member, htons(MAP(tcph->dest)));
        if (si->ch != NULL) {
          hash_put(ch2sock, si->ch, si);
      fprintf(deb,"Adding SI for: %x (accept)\n", si->ch->conn_id);
      fflush(deb);
          
          llist_add_tail(socklist, si);
          return 1;
        }
        fprintf(deb, "freeing SI associated w/ no conn\n");
        fflush(deb);

        free(si);
        if (r != -1)
          close(r);
      }
    } 
    close(sock);
    return 0;
    
  }
  return 1;  
}

void handle_fwd_tunnel(ghl_rh_t *rh) {
  char buf[65536];
  int r;
  if (rh == NULL)
    return;
  if ((r = read(fwdtun_fd, buf, sizeof(buf))) <= 0)
    return;

  if (mangle_packet(rh, buf, r, FWDTUN_TO_TUN))
    write(tun_fd, buf, r);
}


void handle_tunnel(ghl_ctx_t *ctx, ghl_rh_t *rh) {
  char buf[65536];
  struct ip *iph;
  struct udphdr *udph;
  cell_t iter;
  int suffix;
  ghl_member_t *member;
  ghl_member_t *cur;
  
  int r;
  if (rh == NULL)
    return;
  if ((r = read(tun_fd, buf, sizeof(buf))) <= 0)
    return;
  if (mangle_packet(rh, buf, r, TUN_TO_FWDTUN)) {
    write(fwdtun_fd, buf, r);
    return;
  }
  
  if ((unsigned)r < (sizeof(struct ip) + sizeof(struct udphdr)))
    return;
  iph = (struct ip*) buf;
  udph = (struct udphdr*) (buf + sizeof(struct ip));
  
  if (iph->ip_p != IPPROTO_UDP)
    return;
  
  if (routing_host == INADDR_NONE) {
    if (iph->ip_src.s_addr != ((rh->me->virtual_suffix << 24) | inet_addr(GARENA_NETWORK )))
      return;
  } else {
    if (iph->ip_src.s_addr != routing_host)
      return;
  }
  
  
  if ((iph->ip_dst.s_addr == 0xFFFFFFFF) || (iph->ip_dst.s_addr == (inet_addr(GARENA_NETWORK) | 0xFF000000))) {
    /* broadcast packet */
    for (iter = llist_iter(rh->members) ; iter ; iter = llist_next(iter)) {
      cur = llist_val(iter);
      l4d_patchpkt(rh->me->virtual_suffix, buf + sizeof(struct ip) + sizeof(struct udphdr), r - sizeof(struct ip) - sizeof(struct udphdr));
      ghl_udp_encap(ctx, cur, htons(udph->source), htons(udph->dest), buf + sizeof(struct ip) + sizeof(struct udphdr), r - sizeof(struct ip) - sizeof(struct udphdr));
    }
    return;
  } else {
    /* unicast packet */
    
  
    if ((iph->ip_dst.s_addr & 0xFFFFFF) != inet_addr(GARENA_NETWORK))
      return;
  
    suffix = iph->ip_dst.s_addr >> 24;

    /* try to find the member to send the packet */
    member = NULL;
    for (iter = llist_iter(rh->members) ; iter ; iter = llist_next(iter)) {
      cur = llist_val(iter);
      if (cur->virtual_suffix == suffix) {
        member = cur;
        break;
      }
    }
    if (member == NULL) {
      return;
    }
    l4d_patchpkt(rh->me->virtual_suffix, buf + sizeof(struct ip) + sizeof(struct udphdr), r - sizeof(struct ip) - sizeof(struct udphdr));
    ghl_udp_encap(ctx, member, htons(udph->source), htons(udph->dest), buf + sizeof(struct ip) + sizeof(struct udphdr), r - sizeof(struct ip) - sizeof(struct udphdr));
    return;
  }
}

#define MAX_CMDS 12
#define MAX_PARAMS 16

typedef int cmdfun_t(screen_ctx_t *screen, int parc, char **parv);
typedef struct {
  cmdfun_t *fun;
  char *str;
} cmd_t;


int handle_cmd_connect(screen_ctx_t *screen, int parc, char **parv) {
  if (parc != 3) {
    screen_output(screen, "Usage: /CONNECT <your nick> <your pass>");
    return -1;
  }
  if (ctx != NULL) {
    screen_output(screen, "You are already connected\n");
    return -1;
  }
  ctx = ghl_new_ctx(parv[1], parv[2], inet_addr("74.55.122.122"), 0, 0);
  if (ctx == NULL) {
    screen_output(screen, "Context creation failed\n");
    return -1;
  }
  
  ghl_register_handler(ctx, GHL_EV_SERVCONN, handle_servconn, NULL);
  ghl_register_handler(ctx, GHL_EV_ME_JOIN, handle_me_join, NULL);
  ghl_register_handler(ctx, GHL_EV_TALK, handle_talk, NULL);
  ghl_register_handler(ctx, GHL_EV_JOIN, handle_join, NULL);
  ghl_register_handler(ctx, GHL_EV_PART, handle_part, NULL);
  ghl_register_handler(ctx, GHL_EV_ROOM_DISC, handle_disc, NULL);
  ghl_register_handler(ctx, GHL_EV_TOGGLEVPN, handle_togglevpn, NULL);
  ghl_register_handler(ctx, GHL_EV_UDP_ENCAP, handle_udp_encap, NULL);

  ghl_register_handler(ctx, GHL_EV_CONN_INCOMING, handle_conn_incoming, NULL);
  ghl_register_handler(ctx, GHL_EV_CONN_RECV, handle_conn_recv, NULL);
  ghl_register_handler(ctx, GHL_EV_CONN_FIN, handle_conn_fin, NULL);
  return 0;
}

int handle_cmd_roominfo(screen_ctx_t *screen, int parc, char **parv) {
  unsigned int *num_users;
  char buf[256];
  if (parc != 2) {
    screen_output(screen, "Usage: /ROOMINFO <room ID>\n");
    return -1;
  }
  if ((ctx == NULL) || (ctx->connected == 0)){
    screen_output(screen, "You are not connected to a server\n");
    return -1;
  }
  num_users = ihash_get(ctx->roominfo, atoi(parv[1]));
  if (num_users == NULL) {
    screen_output(screen, "I haven't any information on this room\n");
  } else {
    snprintf(buf, sizeof(buf), "The room has %u users.\n", *num_users);
    screen_output(screen, buf);
  }

}

int handle_cmd_list(screen_ctx_t *screen, int parc, char **parv) {
  unsigned int *num_users;
  cell_t iter;
  room_t *room;
  char buf[256];
  
  for (iter = llist_iter(roomlist); iter; iter = llist_next(iter)) {
    room = llist_val(iter);
    num_users = NULL;
    if (ctx)
      num_users = ihash_get(ctx->roominfo, room->id);
    if (num_users) {
      snprintf(buf, 256, "%s (%u users)\n", room->name, *num_users);
    } else snprintf(buf, 256, "%s\n", room->name);

    screen_output(screen, buf);
  }
}





int handle_cmd_join(screen_ctx_t *screen, int parc, char **parv) {
  unsigned int serv_ip;
  int room_id;
  room_t *room; 
  cell_t iter;
  ghl_rh_t *rh = ctx ? ctx->room : NULL;
  if ((parc != 3) && (parc != 2)) {
    screen_output(screen, "Usage: /JOIN <room server IP> <room ID>\n");
    screen_output(screen, "Or: /JOIN <room alias>\n");
    return -1;
  }
  
  if (parc == 2) {
    serv_ip = INADDR_NONE;
    for (iter = llist_iter(roomlist); iter; iter = llist_next(iter)) {
      room = llist_val(iter);
      if (strcasecmp(room->name, parv[1]) == 0) {
        serv_ip = room->ip;
        room_id = room->id;
        break;
      }
    }
    if (serv_ip == INADDR_NONE) {
      screen_output(screen, "No such room alias.\n");
      return -1;
    }
  } else {
    serv_ip = inet_addr(parv[1]);
    if (serv_ip == INADDR_NONE)
      serv_ip = atoi(parv[1]);
    room_id = atoi(parv[2]);
  }
  if (serv_ip < (0x80000000))
    serv_ip = htonl(serv_ip);
  if (rh != NULL) {
    if (rh->joined) {
      screen_output(screen, "You are already in a room, leave first\n");
    } else {
      screen_output(screen, "A join is already in progress, please wait\n");
    }
    return -1;
  }
  if ((ctx == NULL) || (ctx->connected == 0)){
    screen_output(screen, "You are not connected to a server\n");
    return -1;
  }
  rh = ghl_join_room(ctx, serv_ip, 8687, room_id);
  if (rh == NULL) {
    screen_output(screen, "error\n");
    screen_output(screen, garena_strerror());
    return -1;
  }
  return 0;
}

int handle_cmd_part(screen_ctx_t *screen, int parc, char **parv) {
  ghl_rh_t *rh = ctx ? ctx->room : NULL;
  if (rh && rh->joined) {
    ghl_leave_room(rh);
    rh = NULL;
    return 0;
  } else screen_output(screen, "You are not in a room\n");
  return -1;
}

int handle_cmd_startgame(screen_ctx_t *screen, int parc, char **parv) {
  ghl_rh_t *rh = ctx ? ctx->room : NULL;
  if (rh && rh->joined) {
    ghl_togglevpn(rh, 1);
    return 0;
  } else screen_output(screen, "You are not in a room\n");
  return -1;
}

int handle_cmd_stopgame(screen_ctx_t *screen, int parc, char **parv) {
  ghl_rh_t *rh = ctx ? ctx->room : NULL;
  if (rh && rh->joined) {
    ghl_togglevpn(rh, 0);
    return 0;
  } else screen_output(screen, "You are not in a room\n");
  return -1;
}

int handle_cmd_quit(screen_ctx_t *screen, int parc, char **parv) {
  quit = 1;
  return 0;
}

int handle_cmd_routing(screen_ctx_t *screen, int parc, char **parv) {
  char buf[512];
  if (parc == 1) {
    screen_output(screen, "Routing is now disabled\n");
    routing_host = INADDR_NONE;
  } else {
    routing_host = resolve(parv[1]);
    if (routing_host == INADDR_NONE) {
      snprintf(buf, 512, "Routing host %s is invalid\n", parv[1]);
    } else snprintf(buf, 512, "Now routing to host: %s\n", parv[1]);
    screen_output(screen, buf);
  }
  return 0;
}

int handle_cmd_whois(screen_ctx_t *screen, int parc, char **parv) {
  ghl_rh_t *rh = ctx ? ctx->room : NULL;
  char buf[512];
  ghl_member_t *member;
  cell_t iter;
  if (parc != 2) {
    screen_output(screen, "Usage: /WHOIS <name|IP|ID>\n");
    return -1;
  }
  if (!rh || !rh->joined) {
      screen_output(screen, "You are not in a room\n");
      return - 1;
  }
  for (iter = llist_iter(rh->members); iter; iter = llist_next(iter)) {
      member = llist_val(iter);
      
      if  ((strcasecmp(member->name, parv[1]) == 0) ||
          ( ((member->virtual_suffix << 24) | inet_addr(GARENA_NETWORK)) == inet_addr(parv[1]) ) ||
          (member->external_ip.s_addr == inet_addr(parv[1])) ||
          (member->internal_ip.s_addr == inet_addr(parv[1])) ||
          (ghtonl(member->user_id) == strtoul(parv[1], NULL, 16))) {
           snprintf(buf, 512, "Member name: %s\nUser ID: %x\nCountry: %s\nLevel: %u\nIn game: %s\nVirtual IP: 192.168.29.%u\n", member->name, member->user_id, member->country, member->level, member->vpn ? "yes" : "no", member->virtual_suffix);
          screen_output(screen, buf);
          snprintf(buf, 512, "External ip/port: %s:%u\n", inet_ntoa(member->external_ip), member->external_port);
          screen_output(screen, buf);
          snprintf(buf, 512, "Internal ip/port: %s:%u\n", inet_ntoa(member->internal_ip), member->internal_port);
          screen_output(screen, buf);
          if (member->conn_ok) {
            screen_output(screen, "Connex: OK\n");
          } else screen_output(screen, "Connex: KO\n");

          return 0;
      }
  }
  screen_output(screen, "User not found.\n");
  return -1;

}

int handle_cmd_who(screen_ctx_t *screen, int parc, char **parv) {
    char buf[512];
    int num = 0;
    ghl_rh_t *rh = ctx ? ctx->room : NULL;
    int total = 0;
    cell_t iter;
    ghl_member_t *member;
    if (!rh || !rh->joined) {
      screen_output(screen, "You are not in a room\n");
      return - 1;
    }
    screen_output(screen, "Room members [not playing]: ");
    for (iter = llist_iter(rh->members); iter; iter = llist_next(iter)) {
      member = llist_val(iter);
      if (!member->vpn) {
        snprintf(buf, 512, "%s[%x] ", member->name, member->user_id);
        screen_output(screen, buf);
        num += member->conn_ok ? 1 : 0;
        total++;
      }
    }
    screen_output(screen, "\n");
    screen_output(screen, "Room members [playing]: ");
    for (iter = llist_iter(rh->members); iter; iter = llist_next(iter)) {
      member = llist_val(iter);
      if (member->vpn) {
        snprintf(buf, 512, "%s[%x] ", member->name, member->user_id);
        screen_output(screen, buf);
        num += member->conn_ok ? 1 : 0;
        total++;
      }
    }
    screen_output(screen, "\n");
    num++; /* add myself */
    snprintf(buf, sizeof(buf), "Connection OK with %u on %u players\n", num, total);
    screen_output(screen, buf);
    return 0;

}

cmd_t cmdtab[MAX_CMDS] = {
 {handle_cmd_list, "LIST"},
 {handle_cmd_connect, "CONNECT"},
 {handle_cmd_roominfo, "ROOMINFO"},
 {handle_cmd_join, "JOIN"},
 {handle_cmd_part, "PART"},
 {handle_cmd_quit, "QUIT"},
 {handle_cmd_startgame, "STARTGAME"},
 {handle_cmd_stopgame, "STOPGAME"},
 {handle_cmd_who, "WHO"},
 {handle_cmd_routing, "ROUTING"},
 {handle_cmd_whois, "WHOIS"},
 {NULL, NULL}
};
 
void handle_command(screen_ctx_t *screen, char *buf) {
  char tmp[512];
  int i;
  int state = 0;
  int parc;
  int len = strlen(buf);
  char *parv[MAX_PARAMS];
  for (i = 0, parc = 0; i < len; i++) {
    if (state == 0) {
      if (buf[i] != ' ') {
        state = 1;
        parv[parc] = buf + i;
        parc++;
        if (parc == MAX_PARAMS)
          return;
      } else buf[i] = '\0';
    } else {
      if (buf[i] == ' ') {
        state = 0;
        buf[i] = '\0';
      }
    }
  }
  
  if (parc > 0) {
    for (i = 0 ; i < MAX_CMDS; i++) {
      if ((cmdtab[i].fun != NULL) && (strcasecmp(parv[0], cmdtab[i].str) == 0)) {
        if (cmdtab[i].fun(screen, parc, parv) == 0)
          screen_output(screen, "OK\n");
        break;
      }
    }
    if (i == MAX_CMDS) {
      snprintf(tmp, 512, "Unknown command: %s\n", parv[0]);
      screen_output(screen, tmp);
    }
  }
}


void handle_text(screen_ctx_t *screen, char *buf) {
  ghl_rh_t *rh = ctx ? ctx->room : NULL;
  if (strlen(buf) == 0)
    return;
  if (rh && rh->joined) {
    ghl_talk(rh, buf);
  } else screen_output(screen, "You are not in a room\n");
}



int fill_fds_if_needed(ghl_ctx_t *ctx, fd_set *fds) {
  fd_set wfds;
  cell_t iter;
  struct timeval tv;
  sockinfo_t *si;
  ghl_ch_t *ch;
  int num_fd;
  int r; 
    if (ctx) {
      if (ctx->room) {
        num_fd = 0;
        r = 0;
        FD_ZERO(&wfds);
        for (iter = llist_iter(ctx->room->conns); iter; iter = llist_next(iter)) {
          ch = llist_val(iter);
          if (ch->cstate == GHL_CSTATE_ESTABLISHED) {
            si = hash_get(ch2sock, ch);
            if (si->state == SI_STATE_ESTABLISHED) {
              FD_SET(si->sock, &wfds);
              num_fd++;
              if (r < si->sock)
                r = si->sock;
            }
          }
        }
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        r = select(r+1, NULL, &wfds, NULL, &tv);
        if (r == num_fd) {
          r = ghl_fill_fds(ctx, fds);
        } else {
          r = 0;
        }
      } else r = ghl_fill_fds(ctx, fds);
    } else r = 0;
  return r;   
}

int fill_conn_fds(ghl_ctx_t *ctx, fd_set *fds, fd_set *wfds, int r) {
  cell_t iter;
  ghl_ch_t *ch;
  sockinfo_t *si;
    if (ctx && ctx->room)
      for (iter = llist_iter(ctx->room->conns); iter; iter = llist_next(iter)) {
        ch = llist_val(iter);
        if (ch->cstate == GHL_CSTATE_ESTABLISHED) {
          si = hash_get(ch2sock, ch);
          if (si->state == SI_STATE_CONNECTING) {
            FD_SET(si->sock, wfds);
            if (si->sock > r)
              r = si->sock;

          } else if (si->state == SI_STATE_ESTABLISHED) {
            FD_SET(si->sock, fds);
            if (si->sock > r)
              r = si->sock;

          } else if (si->state == SI_STATE_ACCEPTING) {
            FD_SET(si->servsock, fds);
            if (si->servsock > r)
              r = si->servsock;

          }
        }
      }
  return r;
}
int handle_connections(ghl_ctx_t *ctx, fd_set *fds, fd_set *wfds) {

  cell_t iter;
  ghl_ch_t *ch;
  sockinfo_t *si;
  int dummy_size;
  struct sockaddr_in dummy;
  char buf[4096];
  int r;
  
    if (ctx && ctx->room)
      for (iter = llist_iter(ctx->room->conns); iter; iter = llist_next(iter)) {
        ch = llist_val(iter);
        if (ch->cstate == GHL_CSTATE_ESTABLISHED) {
          si = hash_get(ch2sock, ch);
          if (si->state == SI_STATE_ACCEPTING) {
            if (FD_ISSET(si->servsock, fds)) {
              dummy_size = sizeof(dummy);
              if ((r = accept(si->servsock, (struct sockaddr *)&dummy, &dummy_size)) == -1) {
                /* the accept failed */
  fprintf(deb, "freeing SI because the accept failed, connid:  %x\n", ch->conn_id);
  fflush(deb);
                
                ghl_conn_close(ctx, ch);
                if (si->sock != -1)
                  close(si->sock);
                if (si->servsock != -1)
                  close(si->servsock);
                llist_del_item(socklist, si);
                hash_del(ch2sock, ch);
  fprintf(deb, "freeing SI associated w/ %x\n", si->ch->conn_id);
  fflush(deb);
                
                free(si);
              } else {
                si->sock = r;
                set_nonblock(si->sock);
                si->state = SI_STATE_ESTABLISHED;
                if (si->servsock != -1) {
                  close(si->servsock);
                  si->servsock = -1;
                }
              }
            }
          } else if (si->state == SI_STATE_CONNECTING) {
            if (FD_ISSET(si->sock, wfds)) {
              dummy_size = sizeof(dummy);
              if (getpeername(si->sock, (struct sockaddr *)&dummy, &dummy_size) == -1) {
                /* close */
                fprintf(deb, "freeing SI because the connect failed,: %x\n", si->ch->conn_id);
                fflush(deb);
                ghl_conn_close(ctx, ch);
                if (si->sock != -1)
                  close(si->sock);
                if (si->servsock != -1)
                  close(si->servsock);
                llist_del_item(socklist, si);
                hash_del(ch2sock, ch);
  fprintf(deb, "freeing SI associated w/ %x\n", si->ch->conn_id);
  fflush(deb);
              
                free(si);
              } else {
                /* ok */
                si->state = SI_STATE_ESTABLISHED;
              }
            }
          } else if (si->state == SI_STATE_ESTABLISHED) {
            if (FD_ISSET(si->sock, fds)) {
              r = read(si->sock, buf, max_conn_pkt);
              if (r > 0) {
                ghl_conn_send(ctx, ch, buf, r);
              } else {
              fprintf(deb, "freeing SI because normal close: %x\n", si->ch->conn_id);
              fflush(deb);
                ghl_conn_close(ctx, ch);
                if (si->sock != -1)
                  close(si->sock);
                if (si->servsock != -1)
                  close(si->servsock);
                llist_del_item(socklist, si);
                hash_del(ch2sock, ch);
  fprintf(deb, "freeing SI associated w/ %x\n", si->ch->conn_id);
  fflush(deb);
                
                free(si);
              }
            }
          }
        }
      }
  
}



int prepare_fds(fd_set *fds, fd_set *wfds) {
  int r;
    
    r = fill_fds_if_needed(ctx, fds);
    
    r = fill_conn_fds(ctx, fds, wfds, r);

   return r; 

}


int process_input(fd_set *fds, fd_set *wfds) {
  int r;
  int has_timer;
  struct timeval tv;
  char buf[4096];
  int filled_fds;
  
    
    r = prepare_fds(fds, wfds);
    has_timer = ctx ? ghl_fill_tv(ctx, &tv) : 0;
    
    if (has_timer) {
      r = select(MAX(r,tunmax)+1, fds, wfds, NULL, &tv);
    } else {
      r = select(MAX(r,tunmax)+1, fds, wfds, NULL, NULL);
    }
  return r;
}

void process_output(fd_set *fds, fd_set *wfds) {
  int r;
  static char buf[4096];
  if (FD_ISSET(0, fds)) {
      r = screen_input(&screen, buf, sizeof(buf));
      if (r > 0) {
        if (buf[0] == '/') {
          handle_command(&screen, buf + 1);
        } else {
          handle_text(&screen, buf);
        }
      }
  }

  handle_connections(ctx, fds, wfds);


  if (FD_ISSET(tun_fd, fds)) {
      ghl_rh_t *rh = ctx ? ctx->room : NULL;
      handle_tunnel(ctx, rh);
  }
  if (FD_ISSET(fwdtun_fd, fds)) {
      ghl_rh_t *rh = ctx ? ctx->room : NULL;
      handle_fwd_tunnel(rh);
  }
    
  if (ctx) {
      ghl_process(ctx, fds);
  }

}

void client_loop() {
  int r;
  fd_set fds, wfds;
  fd_set i_fds, i_wfds;
  tunmax = MAX(tun_fd, fwdtun_fd);
  FD_ZERO(&i_fds);
  FD_ZERO(&i_wfds);
  FD_SET(tun_fd, &i_fds);
  FD_SET(fwdtun_fd, &i_fds);
  FD_SET(0, &i_fds);

  while(!quit) {
  if (need_free_ctx) {
      ctx = NULL;
      need_free_ctx = 0;
    }
    fds = i_fds;
    wfds = i_wfds;
    r = process_input(&fds, &wfds);
    
    if (r == -1) {
      if ((errno == EINTR) || (errno == EAGAIN))
        continue;
      fprintf(deb, "select: %s", strerror(errno));
      fflush(deb);
      exit(-1);
    }
    process_output(&fds, &wfds);
  }
}

int main(int argc, char **argv) {

  int as_root = 0;
  ghl_ch_t *ch;
  cell_t iter;
  
  if (argc != 2) {
    printf("usage: %s <tunnel interface to use>\n", argv[0]);
    exit(-1);
  }

  strncpy(tun_name, argv[1], IFNAMSIZ);
  tun_name[IFNAMSIZ-1] = 0;
  tun_fd = tun_alloc(tun_name);
  if (tun_fd == -1) {
    perror("tun_alloc");
    exit(-1);
  }
  
  snprintf(fwdtun_name, IFNAMSIZ, "%s-ctrl", argv[1]);
  fwdtun_name[IFNAMSIZ-1] = 0;
  fwdtun_fd = tun_alloc(fwdtun_name);
  if (fwdtun_fd == -1) {
    perror("fwdtun_alloc");
    exit(-1);
  }
  /* fin du code execute en root */
  
  if (getuid() != 0) {
    drop_privileges();
  } else as_root = 1;

  roomlist = read_roomlist();
  if (garena_init() == -1) {
    garena_perror("garena_init");
    exit(-1);
  }
  
  max_conn_pkt = ghl_max_conn_pkt(MTU);
  screen_init(&screen);

  if (as_root)
    screen_output(&screen, "WARNING: Running garena client as root. This may be a security risk.\n");
  ch2sock = hash_init();
  socklist = llist_alloc();
  
  client_loop();
  
  llist_free_val(socklist);  
  hash_free(ch2sock);
  ghl_free_ctx(ctx);  
  garena_fini();
  endwin();
  printf("bye...\n");
  return 0;
}
