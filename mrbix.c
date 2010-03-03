#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include <arpa/inet.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>

#define OUT 0
#define IN  1

#define CB_IN "in"
#define CB_OUT "out"
#define CB_OUT_NR "out_nr"

#define K64 (1024*64)

struct conn_set{ 
  bool inuse;
  int c_in;
  int c_out;
  int c_out_nr;
};

struct event_base *gbase;
struct conn_set *gstate_table; 


int chloe(unsigned short s_port, char *s_ip, int inout);
void levent_socket_attach(int sock, struct event_base *base, char *cb_inout);
void set_non_blocking(int fd);
int find_unused_conn_set (void);

int main(int argc, char **argv)
  {
  int i;
  int sock;
  struct conn_set *state_table; 
  
  state_table=malloc(1000*sizeof(struct conn_set));
  if(state_table==NULL)
    {
    perror("malloc");
    exit(1);
    }
  for(i=0; i<1000; i++)
    {
    state_table[i].inuse=0;
    state_table[i].c_in=-1;
    state_table[i].c_out=-1;
    state_table[i].c_out_nr=-1;
    }
  gstate_table=state_table;

// open listening socket
  sock=chloe(9000,"127.0.0.1", IN);
  gbase = event_base_new();
  levent_socket_attach(sock,gbase, CB_IN);
  while(1)
    {
    event_base_dispatch(gbase);
    }
  return 0;
  }

void cb_func(evutil_socket_t fd, short what, void *arg)
  {
  const char *data = arg;
  int newfd;
  int sock;
  int my_conn_set;

  printf("Got an event on socket %d:%s%s%s%s [%s]\n",
  (int) fd,
    (what&EV_TIMEOUT) ? " timeout" : "",
    (what&EV_READ)    ? " read" : "",
    (what&EV_WRITE)   ? " write" : "",
    (what&EV_SIGNAL)   ? " signal" : "",
      data);
  if(!strcmp(data,CB_IN))
    {
    printf("Event was on our input socket %d\n", fd);
    newfd=accept(fd, NULL, NULL);
    if((newfd==-1))
      {
      perror("accept");
      }
    else    /*  We've got an incoming connection */
      {     /*  Now we can make our outgoing ones, set them up to receive events. */
      sock=chloe(9999,"127.0.0.1",OUT);
      levent_socket_attach(sock,gbase, CB_OUT); 
      my_conn_set=find_unused_conn_set();
      gstate_table[my_conn_set].inuse=1;
      gstate_table[my_conn_set].c_in=newfd;
      gstate_table[my_conn_set].c_out=sock;
      }
    }
  if(!strcmp(data,CB_OUT))
    {
    printf("Event was on our output socket %d\n", fd);
    }
   if(!strcmp(data,CB_OUT_NR))
    {
    printf("Event was on our non-return output socket %d\n", fd);
    }
  free(buf);
  return;
  }

int chloe(unsigned short s_port, char *s_ip, int inout)   /* "Chloe, patch me a (non-blocking) socket!" */
  {
  int sock;
  struct sockaddr_in s_addr;
  if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
    perror("socket");
    exit(1);
    }
  printf("Allocated socket: %d\n",sock);
  
  set_non_blocking(sock);
  
  memset(&s_addr, 0, sizeof(s_addr));     /* Zero out structure */
  s_addr.sin_family      = AF_INET;             /* Internet address family */
  s_addr.sin_addr.s_addr = inet_addr(s_ip);   /* Server IP address */
  s_addr.sin_port        = htons(s_port); /* Server port */

  
  if(inout==OUT) // outgoing connection
    {
    if (connect(sock, (struct sockaddr *) &s_addr, sizeof(s_addr)) < 0)
      {
      perror("connect");
      }
    }
  else // listening socket
    {
    if (bind(sock, (struct sockaddr *) &s_addr, sizeof(s_addr)) < 0)
      {
      perror("bind");
      exit(1);
      }
    if (listen(sock, 128) < 0)
      {
      perror("listen");
      exit(1);
      }
    }
  return sock;
  }

void set_non_blocking(int fd)
  {
  int flags;

  if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
      flags = 0;
  if(fcntl(fd, F_SETFL, flags | O_NONBLOCK))
    {
    perror("fcntl");
    exit(1);
    }
  return;
  }    

void levent_socket_attach(int sock, struct event_base *base, char * cb_inout)
  {
  struct event *ev;
  ev = event_new(base, sock, EV_READ|EV_WRITE, cb_func, cb_inout);
  event_add(ev, NULL);
  }

int find_unused_conn_set (void)
  {
  int i;
  int firstunused=-1;

  for (i=0; i<999; i++)
    {
    if(gstate_table[i].inuse==0)
      {
      firstunused=i;
      break;
      }
    }
  if(firstunused==-1)
    {
    printf("state table overflow!\n");
    exit(1);
    }
  return firstunused;   
  }
  
  

  


