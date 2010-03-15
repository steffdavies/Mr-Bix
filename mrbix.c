#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <arpa/inet.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

int g_out_ip,g_out_nr_ip;
int g_out_port,g_out_nr_port;

struct conn_tuple
{
  struct bufferevent *bev_listening;
  struct bufferevent *bev_out;
  struct bufferevent *bev_out_nr;
};

void
read_cb_listener (struct bufferevent *bev, void *ctx)
{
  struct conn_tuple *connections = ctx;
  /* This callback is invoked when there is data to read on the listening socket. */
  struct evbuffer *input = bufferevent_get_input (bev);
  struct evbuffer *output = bufferevent_get_output (connections->bev_out);
  struct evbuffer *output_nr;
  if(connections->bev_out_nr)
    {
      output_nr = bufferevent_get_output (connections->bev_out_nr);
    }
  else
    {
      printf("Listening bev %p would have written to nr output, but it was already closed\n",bev);
    }
  size_t len;
  void *scratch;

  len = evbuffer_get_length (input);
  scratch = malloc (len);
  evbuffer_remove (input, scratch, len);

  /* Copy all the data from the input buffer to the output buffers. */
  evbuffer_add (output, scratch, len);
  if(connections->bev_out_nr)
    {
      evbuffer_add (output_nr, scratch, len);
    }
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
}

void
error_cb_nr (struct bufferevent *bev, short events, void *ctx)
{
  struct conn_tuple *connections = ctx;
  if (events & BEV_EVENT_ERROR)
    {
      if(bev==connections->bev_out_nr)
        {
          printf ("ERROR: %s on bev_out_nr: %p\n",strerror(errno), bev);
        }
      else
        {
          printf("ERROR: %s on UNKNOWN bev: %p - this should not happen\n",strerror(errno), bev);
        }
    }
  if ((events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
      || (events & BEV_EVENT_ERROR))
    {
      bufferevent_free (connections->bev_out_nr);
      printf("Freed outgoing nr bev: %p\n",connections->bev_out_nr);
      connections->bev_out_nr = 0;
    }
}

void
error_cb (struct bufferevent *bev, short events, void *ctx)
{
  struct conn_tuple *connections = ctx;
  if (events & BEV_EVENT_ERROR)
    {
      if(bev==connections->bev_out)
        {
          printf ("ERROR: %s on bev_out: %p\n",strerror(errno), bev);
        }
      else if(bev==connections->bev_listening)
        {
          printf ("ERROR: %s on bev_listening: %p\n",strerror(errno), bev);
        }
      else
        {
          printf("ERROR: %s on UNKNOWN bev: %p - this should not happen\n",strerror(errno), bev);
        }
    }
  if ((events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
      || (events & BEV_EVENT_ERROR))
    {
      bufferevent_free (connections->bev_listening);
      printf("Freed listening bev: %p\n",connections->bev_listening);
      bufferevent_free (connections->bev_out);
      printf("Freed outgoing bev: %p\n",connections->bev_out);
      if (connections->bev_out_nr != 0)
	      {
	        bufferevent_free (connections->bev_out_nr);
          printf("Freed outgoing nr bev: %p\n",connections->bev_out_nr);
	      }
      free (connections);
      printf("Freed: %p\n",connections);
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
  bev_listening = bufferevent_socket_new (base, fd, BEV_OPT_CLOSE_ON_FREE);
  connections->bev_listening = bev_listening;
  printf("Created listening bev: %p\n",bev_listening);
  bufferevent_setcb (bev_listening, read_cb_listener, NULL, error_cb,
		     connections);
  bufferevent_enable (bev_listening, EV_READ | EV_WRITE);

/*  Set up outgoing socket */
  bev_out = bufferevent_socket_new (base, -1, BEV_OPT_CLOSE_ON_FREE);
  connections->bev_out = bev_out;
  printf("Created outgoing bev: %p\n",bev_out);
  bufferevent_setcb (bev_out, read_cb_out, NULL, error_cb, connections);
  bufferevent_enable (bev_out, EV_READ);

  memset (&sockaddr_outgoing, 0, sizeof (sockaddr_outgoing));
  sockaddr_outgoing.sin_family = AF_INET;
  sockaddr_outgoing.sin_addr.s_addr = g_out_ip;
  sockaddr_outgoing.sin_port = g_out_port;

  if (bufferevent_socket_connect
      (bev_out, (struct sockaddr *) &sockaddr_outgoing,
       sizeof (sockaddr_outgoing)) != 0)
    {
      printf("ERROR: outgoing socket connect failed: %s\n",strerror(errno));
    }

/*  Set up outgoing non-return socket */
  bev_out_nr = bufferevent_socket_new (base, -1, BEV_OPT_CLOSE_ON_FREE);
  connections->bev_out_nr = bev_out_nr;
  printf("Created outgoing nr bev: %p\n",bev_out_nr);
  bufferevent_setcb (bev_out_nr, read_cb_out_nr, NULL, error_cb_nr,
		     connections);
  bufferevent_enable (bev_out_nr, EV_READ);

  memset (&sockaddr_outgoing_nr, 0, sizeof (sockaddr_outgoing_nr));
  sockaddr_outgoing_nr.sin_family = AF_INET;
  sockaddr_outgoing_nr.sin_addr.s_addr = g_out_nr_ip;
  sockaddr_outgoing_nr.sin_port = g_out_nr_port;

  if (bufferevent_socket_connect
      (bev_out_nr, (struct sockaddr *) &sockaddr_outgoing_nr,
       sizeof (sockaddr_outgoing_nr)) != 0)
    {
      printf("ERROR: outgoing nr socket connect failed: %s\n",strerror(errno));
    }


}

int
main (int argc, char **argv)
{
  struct event_base *base;
  struct evconnlistener *listener;
  struct sockaddr_in sin;
  int listening_port;  
  if(argc != 6)
    {
      printf("Usage:\nmrbix <listeningport> <ip_known_good> <port_known_good> <ip_under_test> <port_under_test>\n");
      exit(1);
    }
  listening_port = atoi (argv[1]);
  if (listening_port <= 0 || listening_port > 65535)
    {
      puts ("Invalid port");
      return 1;
    }

  g_out_ip=inet_addr(argv[2]);
  g_out_port=htons(atoi(argv[3]));
  g_out_nr_ip=inet_addr(argv[4]);
  g_out_nr_port=htons(atoi(argv[5]));

  base = event_base_new ();
  if (!base)
    {
      puts ("Couldn't open event base");
      return 1;
    }

  memset (&sin, 0, sizeof (sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl (0);
  sin.sin_port = htons (listening_port);

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
