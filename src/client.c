#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>


#include <garena/error.h>
#include <garena/garena.h>
#include <garena/gcrp.h>
#include <garena/gp2pp.h>
#include <garena/ghl.h>

#define MAX(a,b) ((a) > (b) ? (a) : (b))


int handle_me_join(int event, void *event_param, void *privdata) {
  ghl_me_join_t *join = event_param;
  ghl_ctx_t *ctx = privdata;
  cell_t iter;
  ghl_member_t *member;
  if (join->result == EXIT_SUCCESS) {
    printf("Room %x joined.\n", join->rh->room_ID);
    printf("%s\n", join->rh->welcome);
    printf("Room members [not playing]: ");
    for (iter = llist_iter(join->rh->members); iter; iter = llist_next(iter)) {
      member = llist_val(iter);
      if (!member->vpn)
        printf("%s ", member->name);
    }
    printf("\n");
    printf("Room members [playing]: ");
    for (iter = llist_iter(join->rh->members); iter; iter = llist_next(iter)) {
      member = llist_val(iter);
      if (member->vpn)
        printf("%s ", member->name);
    }
    printf("\n");
    ghl_togglevpn(join->rh, 1);
  } else {
    printf("Room %x join failed.\n", join->rh->room_ID);
  }
  return 0;
}

int handle_talk(int event, void *event_param, void *privdata) {
  ghl_talk_t *talk = event_param;
  
  printf("%x <%s> %s\n", talk->rh->room_ID, talk->member->name, talk->text);
}

int handle_join(int event, void *event_param, void *privdata) {
  ghl_join_t *join = event_param;
  
  printf("%x %s joined the room.\n", join->rh->room_ID, join->member->name);
}

int handle_part(int event, void *event_param, void *privdata) {
  ghl_part_t *part = event_param;
  
  printf("%x %s left the room.\n", part->rh->room_ID, part->member->name);
}

int handle_udp_encap(int event, void *event_param, void *privdata) {
  ghl_udp_encap_t *udp_encap = event_param;
  printf("%s sent an UDP packet, sport=%u, dport=%u\n", udp_encap->member->name, udp_encap->sport, udp_encap->dport);
}

int handle_togglevpn(int event, void *event_param, void *privdata) {
  ghl_togglevpn_t *togglevpn = event_param;
  
  printf("%x %s %s a game.\n", togglevpn->rh->room_ID, togglevpn->member->name, togglevpn->vpn ? "started" : "stopped");
}

int main(void) {

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
  ghl_ctx_t *ctx = ghl_new_ctx("paul13372", "tamere", 0x128829c, inet_addr("74.55.122.122"), 0);
  ghl_register_handler(ctx, GHL_EV_ME_JOIN, handle_me_join, NULL);
  ghl_register_handler(ctx, GHL_EV_TALK, handle_talk, NULL);
  ghl_register_handler(ctx, GHL_EV_JOIN, handle_join, NULL);
  ghl_register_handler(ctx, GHL_EV_PART, handle_part, NULL);
  ghl_register_handler(ctx, GHL_EV_TOGGLEVPN, handle_togglevpn, NULL);
  ghl_register_handler(ctx, GHL_EV_UDP_ENCAP, handle_udp_encap, NULL);
  ghl_rh_t *rh = ghl_join_room(ctx, -1129687478, 8687, 589833);
  
  while(ghl_process(ctx, NULL) != -1);

  ghl_free_ctx(ctx);  
}
