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


#define MAX(a,b) ((a) > (b) ? (a) : (b))


int handle_hello(int type, void *payload, int length, void *privdata, int user_ID, struct sockaddr_in *remote) {
  gp2pp_udp_encap_t *udp_encap = payload;
  switch(type) {
    case GP2PP_MSG_HELLO_REQ:
/*     printf("Received Hello from %s:%u (user_ID=%x)\n", inet_ntoa(remote->sin_addr), htons(remote->sin_port), user_ID);  */
     break;
    case GP2PP_MSG_HELLO_REP:
     break;
    case GP2PP_MSG_UDP_ENCAP:
/*     printf("Received tunnelled UDP packet, sport=%u dport=%u\n", htons(udp_encap->sport), htons(udp_encap->dport)); */
     break;
  }
}

int handle_traffic(int type, void *payload, int length, void *privdata) {
  static char buf[GCRP_MAX_MSGSIZE];
  gcrp_talk_t *talk = payload;
  gcrp_togglevpn_t *toggle = payload;
  gcrp_join_t *join = payload;
  gcrp_leave_t *leave = payload;
  
  switch(type) {
    case GCRP_MSG_TALK:
    
      if (gcrp_tochar(buf, talk->text, (talk->length >> 1) + 1) == -1) {
        fprintf(stderr, "Failed to convert user message.\n");
      } else {
        printf("Room %x <%x> %s\n",ghtonl(talk->room_id), ghtonl(talk->user_id), buf); 
      }
      break;
    case GCRP_MSG_STARTVPN:
      printf("User %x started a game.\n", ghtonl(toggle->user_id));
      
      break;
    case GCRP_MSG_STOPVPN:
      printf("User %x stopped a game.\n", ghtonl(toggle->user_id));
      break;
      
    case GCRP_MSG_JOIN:
      printf("User %x joined the room.\n", ghtonl(join->user_id));
      printf("New Member: Name=%s ID=%x Country=%s ExternalIP=%s", join->name, join->user_id, join->country, inet_ntoa(join->external_ip));
      printf(" InternalIP=%s VirtualIP=192.168.29.%u\n", inet_ntoa(join->internal_ip), join->virtual_suffix);

      break;
    case GCRP_MSG_LEAVE:
      printf("User %x left the room.\n", ghtonl(leave->user_id));
      break;
    default:
      abort();
      break;
  }
  
  return 0;
}

int handle_join(int type, void *payload, int length, void *privdata) {
  static char buf[GCRP_MAX_MSGSIZE];
  gcrp_welcome_t *welcome = payload;
  gcrp_memberlist_t *memberlist = payload;
  int i;
  
  switch(type) {
    case GCRP_MSG_WELCOME:
        printf("Welcome on Chat Room %x\n", ghtonl(welcome->room_id));
      if (gcrp_tochar(buf, welcome->text, GCRP_MAX_MSGSIZE-1) == -1) {
        fprintf(stderr, "Failed to convert welcome message.\n");
      } else {
        printf("Chat Room Welcome Message: [%s]\n", buf);
      }
      break;
    case GCRP_MSG_MEMBERS:
      printf("Received member list on the Chat Room %x (%u members)\n", ghtonl(memberlist->room_id), ghtonl(memberlist->num_members));
      for (i = 0; i < ghtonl(memberlist->num_members); i++) {
        printf("Member %u: Name=%s ID=%x Country=%s ExternalIP=%s", i, memberlist->members[i].name, memberlist->members[i].user_id, memberlist->members[i].country, inet_ntoa(memberlist->members[i].external_ip));
        printf(" InternalIP=%s VirtualIP=192.168.29.%u\n", inet_ntoa(memberlist->members[i].internal_ip), memberlist->members[i].virtual_suffix);
        
      }
      break;
    default:
      abort();
      break;
  }
  return 0;  
}



int main(void) {
  int s, s2;
  struct sockaddr_in fsocket;
  struct sockaddr_in udplocal, udpremote;
  
  static char buf[GCRP_MAX_MSGSIZE];
  fd_set fds;
  int r;
  
  printf("Exemple client using libgarena\n");
  if (garena_init() == -1) {
    garena_perror("garena_init");
    exit(-1);
  }
  s = socket(PF_INET, SOCK_STREAM, 0);
  if (s == -1) {
    perror("gcrp socket");
    exit(-1);
  }
  s2 = socket(PF_INET, SOCK_DGRAM, 0);
  if (s2 == -1) {
    perror("gp2pp socket");
    exit(-1);
  }
  udplocal.sin_family = AF_INET;
  udplocal.sin_addr.s_addr = INADDR_ANY;
  udplocal.sin_port = htons(1513);
  if (bind(s2, (struct sockaddr*) &udplocal, sizeof(udplocal)) == -1) {
    perror("bind");
    exit(-1);
  }
  
  fsocket.sin_family = AF_INET;
  fsocket.sin_addr.s_addr = inet_addr("74.86.170.188");
  fsocket.sin_port = htons(8687);
  if (connect(s, (struct sockaddr *) &fsocket, sizeof(fsocket)) == -1) {
    perror("connect");
    exit(-1);
  }
  
  if (gcrp_send_join(s, 0x090009) == -1) {
    garena_perror("gcrp_send_join");
    exit(-1);
  }

  gcrp_register_handler(GCRP_MSG_MEMBERS, handle_join, NULL);
  gcrp_register_handler(GCRP_MSG_WELCOME, handle_join, NULL);
  gcrp_register_handler(GCRP_MSG_TALK, handle_traffic, NULL);
  gcrp_register_handler(GCRP_MSG_STARTVPN, handle_traffic, NULL);
  gcrp_register_handler(GCRP_MSG_STOPVPN, handle_traffic, NULL);
  gcrp_register_handler(GCRP_MSG_JOIN, handle_traffic, NULL);
  gcrp_register_handler(GCRP_MSG_LEAVE, handle_traffic, NULL);
  

  gp2pp_register_handler(GP2PP_MSG_HELLO_REQ, handle_hello, NULL);
  gp2pp_register_handler(GP2PP_MSG_HELLO_REP, handle_hello, NULL);
  gp2pp_register_handler(GP2PP_MSG_UDP_ENCAP, handle_hello, NULL);
  sleep(1);
  gcrp_send_togglevpn(s, 0x128829c, 1);
  
  sleep(1);  
  while(1) {
    FD_ZERO(&fds);
    
    FD_SET(s, &fds);
    FD_SET(s2, &fds);
    
    r = select(MAX(s,s2)+1, &fds, NULL, NULL, NULL);
    if (FD_ISSET(s, &fds)) {
      r = gcrp_read(s, buf, sizeof(buf));
      if (r == -1) {
        garena_perror("gcrp_read");
        exit(-1);
      }
      gcrp_input(buf, r);
    }
    if (FD_ISSET(s2, &fds)) {
      r = gp2pp_read(s2, buf, sizeof(buf), &udpremote);
      if (r == -1) {
        garena_perror("gp2pp_read");
        exit(-1);
      }
      gp2pp_input(buf, r, &udpremote);
    }
    
  }
  close(s);
  
}
