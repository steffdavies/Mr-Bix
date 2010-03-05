#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <arpa/inet.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>

struct conn_tuple
  {
  struct bufferevent *bev_listening;
  struct bufferevent *bev_out;
  struct bufferevent *bev_out_nr;
  };
  
static void read_cb_listener (struct bufferevent *bev, void *ctx)
{
  struct conn_tuple *connections=ctx;
  /* This callback is invoked when there is data to read on bev. */
  struct evbuffer *input = bufferevent_get_input (bev);
  struct evbuffer *output = bufferevent_get_output (connections->bev_out);

  /* Copy all the data from the input buffer to the output buffer. */
  evbuffer_add_buffer (output, input);
}

static void read_cb_out (struct bufferevent *bev, void *ctx)
{
  struct conn_tuple *connections=ctx;
  /* This callback is invoked when there is data to read on bev. */
  struct evbuffer *input = bufferevent_get_input (bev);
  struct evbuffer *output = bufferevent_get_output (connections->bev_listening);

  /* Copy all the data from the input buffer to the output buffer. */
  evbuffer_add_buffer (output, input);
}

static void error_cb (struct bufferevent *bev, short events, void *ctx)
{
  struct conn_tuple *connections=ctx;
  if (events & BEV_EVENT_ERROR)
    perror ("Error from bufferevent");
  if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
    {
    bufferevent_free(bev);
    bufferevent_free(connections->bev_out);
    printf("about to free %p\n",connections);
    free(connections); 
    }
  
}

static void accept_conn_cb (struct evconnlistener *listener,
		evutil_socket_t fd, struct sockaddr *address, int socklen,
		void *ctx)
{
  /*  We got a new connection! Set up a bufferevent for it, make our 
      outgoing connection and set up its bufferevent. */
  struct event_base *base = evconnlistener_get_base (listener);
  struct bufferevent *bev_listening = bufferevent_socket_new (base, fd, BEV_OPT_CLOSE_ON_FREE);
  struct bufferevent *bev_out = bufferevent_socket_new (base, fd, BEV_OPT_CLOSE_ON_FREE);
  struct sockaddr_in sockaddr_outgoing;
  struct conn_tuple *connections;

  connections=malloc((sizeof(struct conn_tuple)));
  printf("allocated %lx bytes at %p\n",sizeof(struct conn_tuple),connections);
  connections->bev_listening=bev_listening;
  bufferevent_setcb (bev_listening, read_cb_listener, NULL, error_cb, connections);
  bufferevent_enable (bev_listening, EV_READ | EV_WRITE);

  bev_out = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
  connections->bev_out=bev_out;
  bufferevent_setcb(bev_out, read_cb_out, NULL, error_cb, connections);
  bufferevent_enable(bev_out, EV_READ);

  memset(&sockaddr_outgoing, 0, sizeof(sockaddr_outgoing));
  sockaddr_outgoing.sin_family=AF_INET;
  sockaddr_outgoing.sin_addr.s_addr = htonl(0x7f000001);
  sockaddr_outgoing.sin_port = htons(9000);

  if(bufferevent_socket_connect(bev_out, (struct sockaddr *)&sockaddr_outgoing, sizeof(sockaddr_outgoing))!=0)
    {
    perror("Outgoing connection");
    }
}

int main (int argc, char **argv)
{
  struct event_base *base;
  struct evconnlistener *listener;
  struct sockaddr_in sin;

  int port = 9876;

  if (argc > 1)
    {
    port = atoi (argv[1]);
    }
  if (port <= 0 || port > 65535)
    {
    puts ("Invalid port");
    return 1;
    }

  base = event_base_new ();
  if (!base)
    {
    puts ("Couldn't open event base");
    return 1;
    }

  memset (&sin, 0, sizeof (sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl (0);
  sin.sin_port = htons (port);

  listener = evconnlistener_new_bind (base, accept_conn_cb, NULL,
				      LEV_OPT_CLOSE_ON_FREE |
				      LEV_OPT_REUSEABLE, -1,
				      (struct sockaddr *) &sin, sizeof (sin));
  if (!listener)
    {
    perror ("Couldn't create listener");
    return 1;
    }

  event_base_dispatch (base);
  return 0;
}
