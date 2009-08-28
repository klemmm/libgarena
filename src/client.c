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
#include <ncurses.h>
#include <garena/error.h>
#include <garena/garena.h>
#include <garena/gcrp.h>
#include <garena/gp2pp.h>
#include <garena/ghl.h>
#include <garena/util.h>


#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define IP_OFFSET1 0x7D
#define IP_OFFSET2 29
#define IP_OFFSET3 33


int tun_fd;

int routing_host=INADDR_NONE;

int quit = 0;

typedef struct  {
  WINDOW *text;
  WINDOW *cmd;
  struct termios attr;
} screen_ctx_t;


ghl_ctx_t *ctx = NULL;
ghl_rh_t *rh = NULL;
screen_ctx_t screen;

void screen_init(screen_ctx_t *screen) {
  int x, y;
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
    int x,y;
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
  struct in_addr inaddr; 
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


int ip_chksum(char *buffer, int length)
{
  u_short *w = (u_short *)buffer;
  int sum    = 0;
  u_short pad_val;
  int pad = length % 2;
  if (pad) {
    pad_val = buffer[length - 1];
  }
   
  length >>= 1;
  while( length-- )
    sum += *w++;   
  
  if (pad)
    sum += pad_val;
  sum = (sum >> 16) + (sum & 0xffff);
  return(~((sum >> 16) + sum));
}


int handle_me_join(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_me_join_t *join = event_param;
  cell_t iter;
  char cmd[128];
  char buf[512];
  ghl_member_t *member;
  if (join->result == EXIT_SUCCESS) {
    screen_output(&screen, join->rh->welcome);
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

    snprintf(cmd, 128, "/sbin/ifconfig garena0 192.168.29.%u netmask 255.255.255.0", join->rh->me->virtual_suffix);
    system(cmd);
  } else {
    screen_output(&screen, "Room join failed (timeout)\n");
    rh = NULL;
  }
  return 0;
}

int handle_talk(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_talk_t *talk = event_param;
  char buf[512];
  snprintf(buf, 512, "%x <%s> %s\n", talk->rh->room_ID, talk->member->name, talk->text);
  screen_output(&screen, buf);
}

int handle_join(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_join_t *join = event_param;
  char buf[512];
  snprintf(buf, 512, "%x %s[%x] joined the room.\n", join->rh->room_ID, join->member->name, join->member->user_id);
  screen_output(&screen, buf);
}

int handle_part(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_part_t *part = event_param;
  char buf[512];
  snprintf(buf, 512, "%x %s left the room.\n", part->rh->room_ID, part->member->name);
  screen_output(&screen, buf);
}

int handle_udp_encap(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_udp_encap_t *udp_encap = event_param;
  struct ip *iph;
  struct udphdr *udph;
  ghl_rh_t *rh;
  char *buf;
  rh = llist_head(ctx->rooms);
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
  
}

int handle_togglevpn(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_togglevpn_t *togglevpn = event_param;
  char buf[512];
  snprintf(buf, 512, "%x %s %s a game.\n", togglevpn->rh->room_ID, togglevpn->member->name, togglevpn->vpn ? "started" : "stopped");
  screen_output(&screen, buf);
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
         strncpy(ifr.ifr_name, dev, IFNAMSIZ);

      err = ioctl(fd, TUNSETIFF, (void*) &ifr);
      if (err < 0) {
         close(fd);
         return err;
      } 
      strcpy(dev, ifr.ifr_name);
      return fd;
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

void handle_tunnel(ghl_ctx_t *ctx, ghl_rh_t *rh) {
  char buf[65536];
  struct ip *iph;
  struct udphdr *udph;
  cell_t iter;
  int suffix;
  ghl_member_t *member;
  ghl_member_t *cur;
  
  int r;
  if ((r = read(tun_fd, buf, sizeof(buf))) <= 0)
    return;
  if (r < (sizeof(struct ip) + sizeof(struct udphdr)))
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

#define MAX_CMDS 10
#define MAX_PARAMS 16

typedef int cmdfun_t(screen_ctx_t *screen, int parc, char **parv);
typedef struct {
  cmdfun_t *fun;
  char *str;
} cmd_t;


int handle_cmd_connect(screen_ctx_t *screen, int parc, char **parv) {
  if (parc != 2) {
    screen_output(screen, "Usage: /CONNECT <your nick>");
    return -1;
  }
  if (ctx != NULL) {
    screen_output(screen, "You are already connected\n");
    return -1;
  }
  ctx = ghl_new_ctx(parv[1], "tamere", 0x128829c, inet_addr("74.55.122.122"), 0);
  ghl_register_handler(ctx, GHL_EV_ME_JOIN, handle_me_join, NULL);
  ghl_register_handler(ctx, GHL_EV_TALK, handle_talk, NULL);
  ghl_register_handler(ctx, GHL_EV_JOIN, handle_join, NULL);
  ghl_register_handler(ctx, GHL_EV_PART, handle_part, NULL);
  ghl_register_handler(ctx, GHL_EV_TOGGLEVPN, handle_togglevpn, NULL);
  ghl_register_handler(ctx, GHL_EV_UDP_ENCAP, handle_udp_encap, NULL);
  return 0;
}

int handle_cmd_join(screen_ctx_t *screen, int parc, char **parv) {
  int serv_ip;
  if (parc != 3) {
    screen_output(screen, "Usage: /JOIN <room server IP> <room ID>\n");
    return -1;
  }
  serv_ip = inet_addr(parv[1]);
  if (rh != NULL) {
    if (rh->joined) {
      screen_output(screen, "You are already in a room, leave first\n");
    } else {
      screen_output(screen, "A join is already in progress, please wait\n");
    }
    return -1;
  }
  if (ctx == NULL) {
    screen_output(screen, "You are not connected to a server\n");
    return -1;
  }
  if (serv_ip == INADDR_NONE)
    serv_ip = atoi(parv[1]);
  rh = ghl_join_room(ctx, serv_ip, 8687, atoi(parv[2]));
  return 0;
}

int handle_cmd_part(screen_ctx_t *screen, int parc, char **parv) {
  if (rh && rh->joined) {
    ghl_leave_room(rh);
    rh = NULL;
    return 0;
  } else screen_output(screen, "You are not in a room\n");
  return -1;
}

int handle_cmd_startgame(screen_ctx_t *screen, int parc, char **parv) {
  if (rh && rh->joined) {
    ghl_togglevpn(rh, 1);
    return 0;
  } else screen_output(screen, "You are not in a room\n");
  return -1;
}

int handle_cmd_stopgame(screen_ctx_t *screen, int parc, char **parv) {
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
          (ghtonl(member->user_id) == strtol(parv[1], NULL, 16))) {
           snprintf(buf, 512, "Member name: %s\nUser ID: %x\nCountry: %s\nLevel: %u\nIn game: %s\nVirtual IP: 192.168.29.%u\n", member->name, member->user_id, member->country, member->level, member->vpn ? "yes" : "no", member->virtual_suffix);
          screen_output(screen, buf);
          snprintf(buf, 512, "External ip/port: %s:%u\n", inet_ntoa(member->external_ip), htons(member->external_port));
          screen_output(screen, buf);
          snprintf(buf, 512, "Internal ip/port: %s:%u\n", inet_ntoa(member->internal_ip), htons(member->internal_port));
          screen_output(screen, buf);

          return 0;
      }
  }
  screen_output(screen, "User not found.\n");
  return -1;

}

int handle_cmd_who(screen_ctx_t *screen, int parc, char **parv) {
    char buf[512];
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
      }
    }
    screen_output(screen, "\n");
    screen_output(screen, "Room members [playing]: ");
    for (iter = llist_iter(rh->members); iter; iter = llist_next(iter)) {
      member = llist_val(iter);
      if (member->vpn) {
        snprintf(buf, 512, "%s[%x] ", member->name, member->user_id);
        screen_output(screen, buf);
      }
    }
    screen_output(screen, "\n");
    return 0;

}

cmd_t cmdtab[MAX_CMDS] = {
 {handle_cmd_connect, "CONNECT"},
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
  if (strlen(buf) == 0)
    return;
  if (rh && rh->joined) {
    ghl_talk(rh, buf);
  } else screen_output(screen, "You are not in a room\n");
}

int main(int argc, char **argv) {
  char tun_name[] = "garena0";
  fd_set fds;
  char buf[512];
  struct timeval tv;
  int r;
  int input = 0;
  
  tun_fd = tun_alloc(tun_name);
  if (tun_fd == -1) {
    perror("tun_alloc");
    exit(-1);
  }
  garena_init();
  
  screen_init(&screen);
  while(!quit) {
    FD_ZERO(&fds);
    r = ctx ? ghl_fill_fds(ctx, &fds) : 0;
    FD_SET(tun_fd, &fds);
    FD_SET(0, &fds);
    if (ghl_next_timer(&tv)) {
      r = select(MAX(r,tun_fd)+1, &fds, NULL, NULL, &tv);
    } else {
      r = select(MAX(r,tun_fd)+1, &fds, NULL, NULL, NULL);
    }

    if (r == -1) {
      if ((errno == EINTR) || (errno == EAGAIN))
        continue;
      fprintf(deb, "select: %s", strerror(errno));
      fflush(deb);
      exit(-1);
    }
    if (FD_ISSET(0, &fds)) {
      r = screen_input(&screen, buf, sizeof(buf));
      if (r > 0) {
        if (buf[0] == '/') {
          handle_command(&screen, buf + 1);
        } else {
          handle_text(&screen, buf);
        }
      }
    }
    if (FD_ISSET(tun_fd, &fds)) {
      handle_tunnel(ctx, rh);
    }
    if (ctx) 
      ghl_process(ctx, &fds);

  }
  
  endwin();
  ghl_free_ctx(ctx);  
  printf("bye...\n");
  return 0;
}
