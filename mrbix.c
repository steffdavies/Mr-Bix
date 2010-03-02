#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include <arpa/inet.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include<fcntl.h>

#define OUT 0
#define IN  1

#define CB_IN "in"
#define CB_OUT "out"
#define CB_OUT_NR "out_nr"

#define K64 (1024*64)

int chloe(unsigned short s_port, char *s_ip, int inout);
void levent_socket_attach(int sock, struct event_base *base, char *cb_inout);
void set_non_blocking(int fd);

int main(int argc, char **argv)
  {
  int sock;
  struct event_base *base;

// open listening socket
  sock=chloe(9000,"127.0.0.1", IN);
  base = event_base_new();
  levent_socket_attach(sock,base, CB_IN);
// open outgoing socket
  sock=chloe(9999,"127.0.0.1", OUT);
  levent_socket_attach(sock,base,CB_OUT);
  while(1)
    {
    event_base_dispatch(base);
    }
  return 0;
  }

void cb_func(evutil_socket_t fd, short what, void *arg)
  {
  const char *data = arg;
  void *buf;
  int bytes;
  struct sockaddr *addr;
  socklen_t *addrlen;
  int newfd;

  if((buf=malloc(K64))==NULL)
    {
    perror("malloc");
    exit(1);
    }
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
    bytes=recv(newfd, buf, K64, 0);
    if((bytes==-1))
      {
      perror("recv");
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
  

  


