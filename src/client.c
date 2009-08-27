#include <linux/if_tun.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h> 
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <garena/error.h>
#include <garena/garena.h>
#include <garena/gcrp.h>
#include <garena/gp2pp.h>
#include <garena/ghl.h>
#include <garena/util.h>


#define MAX(a,b) ((a) > (b) ? (a) : (b))


int tun_fd;

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
  ghl_member_t *member;
  if (join->result == EXIT_SUCCESS) {
    printf("Room %x joined.\n", join->rh->room_ID);
    printf("%s\n", join->rh->welcome);
    printf("Room members [not playing]: ");
    for (iter = llist_iter(join->rh->members); iter; iter = llist_next(iter)) {
      member = llist_val(iter);
      if (!member->vpn)
        printf("%s[%x] ", member->name, member->user_id);
    }
    printf("\n");
    printf("Room members [playing]: ");
    for (iter = llist_iter(join->rh->members); iter; iter = llist_next(iter)) {
      member = llist_val(iter);
      if (member->vpn)
        printf("%s[%x] ", member->name, member->user_id);
    }
    printf("\n");
    ghl_togglevpn(join->rh, 1);
    printf("My virtual IP is 192.168.29.%u, configuring VPN interface\n", join->rh->me->virtual_suffix);
    snprintf(cmd, 128, "/sbin/ifconfig garena0 192.168.29.%u netmask 255.255.255.0", join->rh->me->virtual_suffix);
    system(cmd);
  } else {
    printf("Room %x join failed.\n", join->rh->room_ID);
  }
  return 0;
}

int handle_talk(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_talk_t *talk = event_param;
  
  printf("%x <%s> %s\n", talk->rh->room_ID, talk->member->name, talk->text);
}

int handle_join(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_join_t *join = event_param;
  
  printf("%x %s[%x] joined the room.\n", join->rh->room_ID, join->member->name, join->member->user_id);
}\

int handle_part(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_part_t *part = event_param;
  
  printf("%x %s left the room.\n", part->rh->room_ID, part->member->name);
}

int handle_udp_encap(ghl_ctx_t *ctx, int event, void *event_param, void *privdata) {
  ghl_udp_encap_t *udp_encap = event_param;
  struct ip *iph;
  struct udphdr *udph;
  ghl_rh_t *rh;
  char *buf;
  rh = llist_head(ctx->rooms);
  if (rh == NULL) {
    printf("Received UDP packet, but we are not in a room so we can't know our virtual IP. Packet ignored.\n");
    garena_errno = GARENA_ERR_PROTOCOL;
    return -1;
  }
  buf = malloc(udp_encap->length + sizeof(struct ip) + sizeof(struct udphdr));
  if (buf == NULL) {
    garena_errno = GARENA_ERR_NORESOURCE;
    return -1;
  }
  printf("%s sent an UDP packet, sport=%u, dport=%u\n", udp_encap->member->name, udp_encap->sport, udp_encap->dport);
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
  iph->ip_dst.s_addr = (rh->me->virtual_suffix << 24) | inet_addr(GARENA_NETWORK);
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
  
  printf("%x %s %s a game.\n", togglevpn->rh->room_ID, togglevpn->member->name, togglevpn->vpn ? "started" : "stopped");
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
  
  
  if (iph->ip_src.s_addr != ((rh->me->virtual_suffix << 24) | inet_addr(GARENA_NETWORK )))
    return;
  
  
  
  if ((iph->ip_dst.s_addr == 0xFFFFFFFF) || (iph->ip_dst.s_addr == (inet_addr(GARENA_NETWORK) | 0xFF000000))) {
    /* broadcast packet */
    printf("Outgoing broadcast packet.\n");
    for (iter = llist_iter(rh->members) ; iter ; iter = llist_next(iter)) {
      cur = llist_val(iter);
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
        printf("Outgoing packet for %s\n", member->name);
        break;
      }
    }
    if (member == NULL) {
      printf("This virtual IP belongs to no one...\n");
      return;
    }
    ghl_udp_encap(ctx, member, htons(udph->source), htons(udph->dest), buf + sizeof(struct ip) + sizeof(struct udphdr), r - sizeof(struct ip) - sizeof(struct udphdr));
    return;
  }
}


int main(void) {
  char tun_name[] = "garena0";
  fd_set fds;
  struct timeval tv;
  int quit = 0;
  int r;
  
  tun_fd = tun_alloc(tun_name);
  if (tun_fd == -1) {
    perror("tun_alloc");
    exit(-1);
  }
  garena_init();
/*
rooms L4D

 Europe Room 01|-1163241910|262234|1029
  Europe Room 06|-986305584|459098|1029
   Europe Room 05|-986305584|459099|1029
    Europe Room 04|-986305584|459100|1029
     Europe Room 03|-986305584|459101|1029
      Europe Room 02|-1129687478|589833|1029
      
*/
/*
rooms DotA
 Europe DotA Room 01|-986305584|458866
 Europe DotA Room 02|-986305584|458867
 Europe DotA Room 03|-986305584|458868
 Europe DotA Room 04|-986305584|458869
 Europe DotA Room 05|-986305584|458870
 Europe DotA Room 06|-986305584|458871
 Europe DotA Room 11|-986305584|458913
 Europe DotA Room 10|-986305584|458960
 Europe DotA Room 09|-986305584|458961
 Europe DotA Room 08|-986305584|458962
 Europe DotA Room 07|-986305584|458963

*/

  ghl_ctx_t *ctx = ghl_new_ctx("paul13372", "tamere", 0x128829c, inet_addr("74.55.122.122"), 0);
  ghl_register_handler(ctx, GHL_EV_ME_JOIN, handle_me_join, NULL);
  ghl_register_handler(ctx, GHL_EV_TALK, handle_talk, NULL);
  ghl_register_handler(ctx, GHL_EV_JOIN, handle_join, NULL);
  ghl_register_handler(ctx, GHL_EV_PART, handle_part, NULL);
  ghl_register_handler(ctx, GHL_EV_TOGGLEVPN, handle_togglevpn, NULL);
  ghl_register_handler(ctx, GHL_EV_UDP_ENCAP, handle_udp_encap, NULL);
  sleep(3);
  printf("join now\n");
  ghl_rh_t *rh = ghl_join_room(ctx, -1129687478, 8687, 589833);
  
  while (!quit) {

    FD_ZERO(&fds);
    r = ghl_fill_fds(ctx, &fds);
    FD_SET(tun_fd, &fds);
    if (ghl_next_timer(&tv)) {
      r = select(r+1, &fds, NULL, NULL, &tv);
    } else {
      r = select(r+1, &fds, NULL, NULL, NULL);
    }
    
    
    if (FD_ISSET(tun_fd, &fds)) {
      handle_tunnel(ctx, rh);
    }

    quit = (ghl_process(ctx, &fds) == -1);
    
  
  }
    
  ghl_free_ctx(ctx);  
}
