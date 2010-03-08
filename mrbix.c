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

int
free_if_bevs_freed (struct conn_tuple *connections)
{
  if ((connections->bev_listening == 0) && (connections->bev_out == 0)
      && (connections->bev_out_nr == 0))
    {
      printf ("about to free %p\n", connections);
      free (connections);
      return 1;
    }
  else
    {
      return 0;
    }
}

void
read_cb_listener (struct bufferevent *bev, void *ctx)
{
  struct conn_tuple *connections = ctx;
  /* This callback is invoked when there is data to read on the listening socket. */
  struct evbuffer *input = bufferevent_get_input (bev);
  struct evbuffer *output = bufferevent_get_output (connections->bev_out);
  struct evbuffer *output_nr =
    bufferevent_get_output (connections->bev_out_nr);
  size_t len;
  void *scratch;

  len = evbuffer_get_length (input);
  scratch = malloc (len);
  evbuffer_remove (input, scratch, len);

  /* Copy all the data from the input buffer to the output buffers. */
  evbuffer_add (output, scratch, len);
  evbuffer_add (output_nr, scratch, len);
  free (scratch);
}

void
read_cb_out (struct bufferevent *bev, void *ctx)
{
  struct conn_tuple *connections = ctx;
  /* This callback is invoked when there is data to read on outgoing connection. */
  struct evbuffer *input = bufferevent_get_input (bev);
  struct evbuffer *output =
    bufferevent_get_output (connections->bev_listening);

  /* Copy all the data from the input buffer to the output buffer. */
  evbuffer_add_buffer (output, input);
}

void
read_cb_out_nr (struct bufferevent *bev, void *ctx)
{
  /* This callback is invoked when there is data to read on outgoing connection without return leg. */
  size_t len;
  struct evbuffer *input;

/* In this instance, we want to throw the data away, not copy it */
  input = bufferevent_get_input (bev);
  len = evbuffer_get_length (input);
  evbuffer_drain (input, len);
  printf ("Drained %lu bytes\n", (unsigned long) len);
}

void
error_cb (struct bufferevent *bev, short events, void *ctx)
{
  struct conn_tuple *connections = ctx;
  if (events & BEV_EVENT_ERROR)
    perror ("Error from bufferevent");
  if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
    {
      bufferevent_free (connections->bev_listening);
      connections->bev_listening = 0;
      bufferevent_free (connections->bev_out);
      connections->bev_out = 0;
      free_if_bevs_freed (connections);
    }
}

void
accept_conn_cb (struct evconnlistener *listener,
		evutil_socket_t fd, struct sockaddr *address, int socklen,
		void *ctx)
{
  /*  We got a new connection! Set up a bufferevent for it, make our 
     outgoing connections and set up their bufferevents. */
  struct event_base *base = evconnlistener_get_base (listener);
  struct bufferevent *bev_listening;
  struct bufferevent *bev_out;
  struct bufferevent *bev_out_nr;
  struct sockaddr_in sockaddr_outgoing;
  struct sockaddr_in sockaddr_outgoing_nr;
  struct conn_tuple *connections;

/*  Set up listening socket */
  connections = malloc ((sizeof (struct conn_tuple)));
  printf ("allocated %lx bytes at %p\n", sizeof (struct conn_tuple),
	  connections);
  bev_listening = bufferevent_socket_new (base, fd, BEV_OPT_CLOSE_ON_FREE);
  connections->bev_listening = bev_listening;
  bufferevent_setcb (bev_listening, read_cb_listener, NULL, error_cb,
		     connections);
  bufferevent_enable (bev_listening, EV_READ | EV_WRITE);

/*  Set up outgoing socket */
  bev_out = bufferevent_socket_new (base, -1, BEV_OPT_CLOSE_ON_FREE);
  connections->bev_out = bev_out;
  bufferevent_setcb (bev_out, read_cb_out, NULL, error_cb, connections);
  bufferevent_enable (bev_out, EV_READ);

  memset (&sockaddr_outgoing, 0, sizeof (sockaddr_outgoing));
  sockaddr_outgoing.sin_family = AF_INET;
  sockaddr_outgoing.sin_addr.s_addr = htonl (0x7f000001);
  sockaddr_outgoing.sin_port = htons (9000);

  if (bufferevent_socket_connect
      (bev_out, (struct sockaddr *) &sockaddr_outgoing,
       sizeof (sockaddr_outgoing)) != 0)
    {
      perror ("Outgoing connection");
    }

/*  Set up outgoing non-return socket */
  bev_out_nr = bufferevent_socket_new (base, -1, BEV_OPT_CLOSE_ON_FREE);
  connections->bev_out_nr = bev_out_nr;
  bufferevent_setcb (bev_out_nr, read_cb_out_nr, NULL, error_cb, connections);
  bufferevent_enable (bev_out_nr, EV_READ);

  memset (&sockaddr_outgoing_nr, 0, sizeof (sockaddr_outgoing_nr));
  sockaddr_outgoing_nr.sin_family = AF_INET;
  sockaddr_outgoing_nr.sin_addr.s_addr = htonl (0x7f000001);
  sockaddr_outgoing_nr.sin_port = htons (9001);

  if (bufferevent_socket_connect
      (bev_out_nr, (struct sockaddr *) &sockaddr_outgoing_nr,
       sizeof (sockaddr_outgoing_nr)) != 0)
    {
      perror ("Outgoing connection");
    }


}

int
main (int argc, char **argv)
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
