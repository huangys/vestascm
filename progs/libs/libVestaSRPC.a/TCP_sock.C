// Copyright (C) 2001, Compaq Computer Corporation
// 
// This file is part of Vesta.
// 
// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

//  *******************************
//  *  TCP sockets for pseudo RPC *
//  *******************************

#include "SRPCConfig.h"

//  Standard definitions

#include <sys/types.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <Basics.H>
#include <OS.H>
#include <VestaConfig.H>

#if defined(HAVE_SYS_FILIO_H)
// We need this on Solaris and possibly other systems
#include <sys/filio.h>
#endif

#include "TCP_sock.H"

#if defined(__digital__)
// The sys/socket.h checked into Vesta for Tru64 is missing this POSIX
// typedef
typedef int socklen_t;

#ifndef NETDB_INTERNAL
// This should probably be removed after we update the Tru64 libc
// checked into Vesta.
#define NETDB_INTERNAL  -1      /* see errno */
#endif
#endif

//  *******************************
//  *  Miscellaneous definitions  *
//  *******************************

const char *TCP_NAME = "tcp";

//  ************************************************
//  *  Static variables defined in TCP_sock class  *
//  ************************************************

pthread_once_t TCP_sock::init_block = PTHREAD_ONCE_INIT;

// static class data
Text TCP_sock::my_hostname;
sockaddr_in TCP_sock::defaultsock;
in_addr TCP_sock::self_addr;
int TCP_sock::tcp_protolevel;
static unsigned int g_resolv_try_again_limit = 100;

#if defined(__linux__)
Basics::mutex TCP_sock::ndb_mutex;
#endif

//  ***************
//  *  Utilities  *
//  ***************

static void die(const Text &m) throw (TCP_sock::failure)
{
  throw TCP_sock::failure(TCP_sock::internal_trouble, m);
}

static void die_with_errno(int reason, const Text &m, int e = 0)
  throw (TCP_sock::failure)
{
  if (e == 0) e = errno;
  Text emsg = Basics::errno_Text(e);
  throw TCP_sock::failure(reason, m + " (" + emsg + ")", e);
}

extern "C"
{
  void TCP_sock_init()
  {
    TCP_sock::init();
  }
}

/* static */
void TCP_sock::ensure_init() throw (TCP_sock::failure)
{
  if (pthread_once(&init_block, TCP_sock_init) != 0)
    throw TCP_sock::failure(internal_trouble, "pthread_once failed!");
}

static bool close_fd(fd_t &fd) throw ()
{
  if (fd == NO_FD) return true;
  if (close(fd) == SYSERROR) return false;
  fd = NO_FD;
  return true;
}

const char *TCP_sock::state_str(TCP_sock::sock_state is)
{
  switch(is)
    {
    case TCP_sock::fresh: return "fresh";
    case TCP_sock::listening: return "listening";
    case TCP_sock::connected: return "connected";
    case TCP_sock::dead: return "dead";
    }
  return "<<INVALID STATE>>";
}

void TCP_sock::ensure_state(sock_state should_be)
  throw (TCP_sock::failure)
{
  if (this->state != should_be) {
    sock_state is = this->state;
    this->state = TCP_sock::dead;
    throw failure(TCP_sock::wrong_state,
		  Text("Socket in wrong state; is: ") + TCP_sock::state_str(is) +
		  ", should be: " + TCP_sock::state_str(should_be));
  }
}

void TCP_sock::ensure_sndbuffer() throw ()
{
  if (this->sndbuff != NULL) return;
  this->sndbuff = NEW_PTRFREE_ARRAY(char, this->sndbuff_size);
  this->sndbuff_len = 0;
}

void TCP_sock::ensure_rcvbuffer() throw ()
{
  if (this->rcvbuff != NULL) return;
  this->rcvbuff = NEW_PTRFREE_ARRAY(char, this->rcvbuff_size);
  this->rcvcurp = this->rcvend = this->rcvbuff;
}

void TCP_sock::configure_blocking() throw (TCP_sock::failure)
{
  // Establish blocking/non-blocking mode
  bool non_blocking = (this->alerts_enabled || this->read_timeout_enabled);
  if (this->non_blocking != non_blocking) {
    // Convert to int type needed for FIONBIO parameter.
    int nb_param = non_blocking ? 1 : 0;
    if (ioctl(this->fd, FIONBIO, &nb_param) == SYSERROR)
      die_with_errno(TCP_sock::internal_trouble, "ioctl(FIONBIO) failed");
    this->non_blocking = non_blocking;
  }
}

void TCP_sock::controlled_wait()
  throw (TCP_sock::alerted, TCP_sock::failure)
/* This procedure uses poll(2) to wait until one of four things happens:

   1. 'poll' returns indicating that data is available to be read
      on the file descriptor 'fd'.  In this case, the procedure
      simply returns.  There may also be data available to be read on
      'pipe_rd', but, if so, it is left untouched.  The procedure returns
      normally.

   2. 'poll' returns indicating that data is available to be read
      solely on the file descriptor 'pipe_rd'.  This means there may
      be a pending alert, represented by 'alert_flag'.  All available
      data is read from 'pipe_rd'.  Then, if 'alert_flag' is true,
      it is set to false and 'alerted' is thrown.  If 'alert_flag' is
      is false, 'poll' is retried.

   3. 'poll' returns 0, indicating that the timeout has elapsed.  If
   'read_timeout_enabled' is true, throw 'failure' with the reason
   'read_timeout'.

   4. None of the above.  The procedure throws 'failure'.

   Not all of these cases can occur in all configurations.  Case 1 is
   always possible.  Case 2 can arise only if 'alerts_enabled' is
   true.  Case 3 can arise only if 'read_timeout_enabled' is true.
   Case 4, of course, can always occur.

*/
{
  // By default, wait indefinitely.
  int timeout = -1;
  time_t wait_start;

  // If read timeouts are enabled, use a positive timeout with poll.
  if(this->read_timeout_enabled)
    {
      // Convert to milliseconds
      timeout = this->read_timeout_secs * 1000;

      // If we have a time limit, remember when we start waiting in case
      // we get interrupted to try and avoid waiting indefinitely.  (We
      // could still be in trouble if we get interrupted after less than
      // a second.)
      wait_start = time(NULL);
    }

  while (true) {
    struct pollfd poll_data[2];
    unsigned int nfds = 1;
    poll_data[0].fd = this->fd;
    poll_data[0].events = POLLIN;
    if (this->alerts_enabled) {
      nfds = 2;
      poll_data[1].fd = this->pipe_rd;
      poll_data[1].events = POLLIN;
    }

    int nready = poll(poll_data, nfds, timeout);

    if (nready == SYSERROR) {
      // Case 4: error.  Deal with transient error cases.
      if (errno == EAGAIN)
	// Insufficient resources to perform the 'poll' (whatever
	// that means...).  Wait a while (with 'sleep', hoping that
	// it uses different resources!) and try again.
	sleep(1);
      else if (errno != EINTR) {
	this->state = TCP_sock::dead;
	die_with_errno(TCP_sock::internal_trouble, "Error on 'poll'");
      }
      // Loop and retry 'poll' if EINTR or EAGAIN.
    } else if(poll_data[0].revents & POLLNVAL) {
      // Case 4:  error
      this->state = TCP_sock::dead;
      die("Error on socket input");
    } else if(this->alerts_enabled && 
	      (poll_data[1].revents & (POLLERR | POLLNVAL))) {
      // Case 4:  error
      this->state = TCP_sock::dead;
      die("Error on alert channel pipe");
    } else if(poll_data[0].revents & (POLLIN | POLLERR)) {
      // Case 1: data available on sock_rd.  (Note: we treat POLLERR
      // as readable so that we can get the error status on a
      // subsequent read.  This is also the way select(2) treats
      // errors.)
      break;
    } else if (this->alerts_enabled && (poll_data[1].revents & POLLIN)) {
      // Case 2:  data available on pipe_rd
      this->m.lock();
      // Drain the pipe by reading 'bytes_in_pipe' bytes of data
      // from it.  In principle, this number can be arbitrarily large;
      // in practice, it is 1.  Thus, the following inefficient code
      // is ok; it avoids allocating a buffer.
      while (this->bytes_in_pipe > 0) {
	char c;
	if (read(this->pipe_rd, &c, 1) == SYSERROR) {
	  this->state = TCP_sock::dead;
	  this->m.unlock();
	  die_with_errno(TCP_sock::internal_trouble, "Error reading pipe");
	}
	this->bytes_in_pipe--;
      }
      if (this->alert_flag) {
	this->alert_flag = false;
	this->m.unlock();
	throw TCP_sock::alerted();
      }
      this->m.unlock();
    } else if(this->read_timeout_enabled && (nready == 0)) {
      // Case 3: no data from peer within the timeout period.
      throw
	TCP_sock::failure(TCP_sock::read_timeout,
			  "read timeout (no data from peer)");
    }
    // Go around the loop again, retrying the 'poll'.

    if(this->read_timeout_enabled)
      {
	// Check to see how much time there is left until we reach our
	// limit waiting for the peer.
	time_t now = time(NULL);

	// Technically, time can go backwards (thanks to things like
	// ntp), or we could get here less than a second after we
	// start waiting.
	if(now > wait_start)
	  {
	    if((now - wait_start) < this->read_timeout_secs)
	      {
		// Still some time left: decrease the timeout used with poll.
		timeout = (this->read_timeout_secs - (now - wait_start)) * 1000;
	      }
	    else
	      {
		// Technically the timeout has elapsed, but maybe we
		// got EINTR or EAGAIN, so give it one more chance
		// with a minimal timeout.
		timeout = 1;
	      }
	  }
      }

  }
}

//  ********************
//  *  Initialization  *
//  ********************

/* static */
void TCP_sock::init() throw (TCP_sock::failure)
{
    {
      // If set, [TCP_sock]resolv_try_again_limit specifies the
      // maximum number of times we'll retry resolving a hostname with
      // the non-fatal TRY_AGAIN error.
      Text try_again_limit;
      if(VestaConfig::get("TCP_sock", "resolv_try_again_limit",
			  try_again_limit))
	{
	  g_resolv_try_again_limit = atoi(try_again_limit.cchars());
	}
    }

    // Who am I?  Save the local host name.  (This is used in some
    // cases where one program needs to tell another "call me back".)
    char temp_hostname[MAXHOSTNAMELEN+1];
    if (gethostname(temp_hostname, MAXHOSTNAMELEN) == SYSERROR)
      die_with_errno(TCP_sock::internal_trouble,
		     "initial gethostname failed!");
    TCP_sock::my_hostname = Text(temp_hostname);

    // Default values for connections.  Use the wildcard address by
    // default, which let the OS chose the right SRC IP.
    memset((char *)(&(TCP_sock::defaultsock)), 0,
	   sizeof(TCP_sock::defaultsock));
    TCP_sock::defaultsock.sin_family = AF_INET;
    TCP_sock::defaultsock.sin_addr.s_addr = INADDR_ANY;

    // Look up our hostname's IP address and remember it.
    try
      {
	TCP_sock::self_addr = TCP_sock::host_to_addr(TCP_sock::my_hostname);
      }
    catch(TCP_sock::failure f)
      {
	// If we have trouble, add a little text to emphasize that
	// this happened while trying to resolve our own hostname.
	f.msg += " (resolving our own hostname!)";
	throw f;
      }

    // Determine TCP protocol number.

    // This code used to use getprotobyname_r, but that's not standard
    // between platforms, and some don't even have it.  We could use
    // getprotobyname, but the TCP protocol number is available as a
    // constant, so we'll just use that.

    TCP_sock::tcp_protolevel = IPPROTO_TCP;
    endprotoent();
}

//  ******************
//  *  Construction  *
//  ******************

TCP_sock::TCP_sock(u_short port) throw (TCP_sock::failure)
{
  TCP_sock::ensure_init();
  this->instance_init();
  this->new_socket(port);
}

TCP_sock::TCP_sock(const empty_sock_marker &p_ignored)
  throw (TCP_sock::failure)
{
  TCP_sock::ensure_init();

  this->instance_init();

  // No file descriptor here.
  this->fd = SYSERROR;
}

/* static */
void TCP_sock::set_no_delay(int fd) throw (TCP_sock::failure)
/* Tell kernel "send" operations on "fd" to send without delay, since
   we do our own output buffering. This disables Nagle's algorithm. */
{
  int no_delay = 1;
  if (setsockopt(fd, TCP_sock::tcp_protolevel, TCP_NODELAY,
      (char *)(&no_delay), sizeof(no_delay)) == SYSERROR) {
    die_with_errno(TCP_sock::internal_trouble,
      "setsocketopt failed on TCP_NODELAY");
  }
}

void TCP_sock::instance_init() throw()
{
  memset(&this->me, 0, sizeof(this->me));

  // Initialize other fields.
  this->sndbuff = (char *)NULL;
  this->sndbuff_len = 0;

  this->rcvbuff = (char *)NULL;
  this->rcvcurp = (char *)NULL;
  this->rcvend = (char *)NULL;

  this->sndbuff_size = TCP_sock::default_sndbuffer_size;
  this->rcvbuff_size = TCP_sock::default_rcvbuffer_size;

  this->non_blocking = false;

  this->alerts_enabled = false;

  this->read_timeout_enabled = false;

  this->state = TCP_sock::fresh;
}

void TCP_sock::new_socket(u_short port)
  throw (TCP_sock::failure)
{
  // Initialize to default values.  This includes using the wildcard
  // address (INADDR_ANY) for the IP.
  this->me = TCP_sock::defaultsock;

  // Use the specified port
  this->me.sin_port = htons(port);

  // First, create an Internet domain socket for TCP.
  this->fd = socket(AF_INET, SOCK_STREAM, TCP_sock::tcp_protolevel);
  if (this->fd == SYSERROR) {
    int code = (errno == ENOBUFS || errno == ENOMEM)
      ? TCP_sock::environment_problem : TCP_sock::internal_trouble;
    die_with_errno(code, "couldn't create TCP socket");
  }

  // We allow reuse of a previous socket name to allow listeners to come
  // back at a well-known address following a crash.
  int reuseaddr = 1;
  if (setsockopt(this->fd, SOL_SOCKET, SO_REUSEADDR,
      (char *)(&reuseaddr), sizeof(reuseaddr)) == SYSERROR)
    die_with_errno(TCP_sock::internal_trouble, "setsockopt(reuseaddr) failed");

  // Next, bind the specified address/port to the socket.
  if (bind(this->fd, (sockaddr *)(&me), sizeof(me)) == SYSERROR) {
    int code = (errno == EADDRINUSE) ? TCP_sock::environment_problem :
      (errno == EADDRNOTAVAIL || errno == EACCES)
      ? TCP_sock::invalid_parameter : TCP_sock::internal_trouble;
    Text addr_t = inet_ntoa_r(this->me.sin_addr);
    die_with_errno(code,
		   Text::printf("bind socket to %s:%d failed!",
				addr_t.cchars(), (int) port));
  }

  // The socket is now has a name.  If the port was omitted, fill it in.
  if (this->me.sin_port == 0) {
    socklen_t size = sizeof(this->me);
    if (getsockname(this->fd, (sockaddr *)(&(this->me)), &size) == SYSERROR) {
      int code = (errno == ENOBUFS)
        ? TCP_sock::environment_problem : TCP_sock::internal_trouble;
      die_with_errno(code,
	"getsockname failed on " + inet_ntoa_r(this->me.sin_addr));
    }
  };

  // Disable Nagle's algorithm
  TCP_sock::set_no_delay(this->fd);
}

TCP_sock::~TCP_sock() throw (TCP_sock::failure)
{
  if (this->fd != SYSERROR) (void)close_fd(this->fd);
  if (this->alerts_enabled) {
    if (!close_fd(this->pipe_wr))
      die_with_errno(TCP_sock::internal_trouble, "close of pipe_wr failed!");
    if (!close_fd(this->pipe_rd))
      die_with_errno(TCP_sock::internal_trouble, "close of pipe_rd failed!");
    this->alerts_enabled = false;
  }
  if (this->sndbuff != (char *)NULL) delete[] this->sndbuff;
  if (this->rcvbuff != (char *)NULL) delete[] this->rcvbuff;
}

//  ****************************
//  *  Optional configuration  *
//  ****************************

void TCP_sock::set_output_buffer_size(int bytes)
  throw (TCP_sock::failure)
{
  if (bytes <= 0)
    throw(TCP_sock::failure(TCP_sock::invalid_parameter,
      "Illegal buffer size"));
  this->m.lock();
  try {
    if (this->sndbuff != (char *)NULL) {
      throw(TCP_sock::failure(TCP_sock::wrong_state,
        "Can't set output buffer size now"));
    }
    this->sndbuff_size = bytes;
  } catch(TCP_sock::failure &f) { this->m.unlock(); throw; }
  this->m.unlock();
}

void TCP_sock::set_input_buffer_size(int bytes)
  throw (TCP_sock::failure)
{
  if (bytes <= 0)
    throw(TCP_sock::failure(TCP_sock::invalid_parameter,
      "Illegal buffer size"));
  this->m.lock();
  try {
    if (this->rcvbuff != (char *)NULL) {
      throw(TCP_sock::failure(TCP_sock::wrong_state,
        "Can't set input buffer size now"));
    }
    this->rcvbuff_size = bytes;
  } catch(failure &f) { this->m.unlock(); throw; }
  this->m.unlock();
}

void TCP_sock::enable_alerts() throw (TCP_sock::failure)
{
  this->m.lock();
  try {
    if (!(this->alerts_enabled)) {
      fd_t pipe_fds[2];
      if (pipe(pipe_fds) == SYSERROR) {
	int code = (errno == ENFILE || errno == ENOMEM)
          ? TCP_sock::environment_problem : TCP_sock::internal_trouble;
	die_with_errno(code, "pipe call failed!");
      }
      this->pipe_rd = pipe_fds[0];
      this->pipe_wr = pipe_fds[1];
      this->bytes_in_pipe = 0;
      this->alert_flag = false;
      this->alerts_enabled = true;
      this->configure_blocking();
    }
  } catch(...) { this->m.unlock(); throw; }
  this->m.unlock();
}

void TCP_sock::disable_alerts() throw (TCP_sock::failure)
{
  this->m.lock();
  try {
    if (this->alerts_enabled) {
      if (!close_fd(this->pipe_wr))
	die_with_errno(TCP_sock::internal_trouble, "close of pipe_wr failed!");
      if (!close_fd(this->pipe_rd))
	die_with_errno(TCP_sock::internal_trouble, "close of pipe_rd failed!");
      this->alerts_enabled = false;
      this->configure_blocking();
    }
  } catch(...) { this->m.unlock(); throw; }
  this->m.unlock();
}

void TCP_sock::alert() throw (TCP_sock::failure)
{
  this->m.lock();
  try {
    if (!(this->alerts_enabled)) {
      throw(TCP_sock::failure(TCP_sock::wrong_state,
        "alerts aren't enabled on this socket"));
    }
    if (!(this->alert_flag)) {
      char c = '!';
      if (write(this->pipe_wr, &c, 1) == SYSERROR)
	die_with_errno(TCP_sock::internal_trouble, "write to pipe failed!");
      this->bytes_in_pipe++;
      this->alert_flag = true;
    }
  } catch(...) { this->m.unlock(); throw; }
  this->m.unlock();
}

bool TCP_sock::test_alert() throw(TCP_sock::failure)
{
  bool res;
  this->m.lock();
  try {
    if (!(this->alerts_enabled))
      throw failure(TCP_sock::wrong_state,
        "alerts aren't enabled on this socket.");
    res = this->alert_flag;
    this->alert_flag = false;
  } catch(...) { this->m.unlock(); throw; }
  this->m.unlock();
  return res;
}

void TCP_sock::set_keepalive(bool state)  throw (failure)
{
  int keepalive = state ? 1 : 0;

  this->m.lock();
  int result = setsockopt(this->fd, SOL_SOCKET, SO_KEEPALIVE,
			  (char *)(&keepalive), sizeof(keepalive));
  this->m.unlock();

  if(result == SYSERROR)
    {
      die_with_errno(TCP_sock::internal_trouble,
		     "setsocketopt failed on SO_KEEPALIVE");
    }
}

bool TCP_sock::get_keepalive() throw (failure)
{
  int keepalive = 0;
  socklen_t keepalive_size = sizeof(keepalive);

  this->m.lock();
  int result = getsockopt(this->fd, SOL_SOCKET, SO_KEEPALIVE,
			  (char *)(&keepalive), &keepalive_size);
  this->m.unlock();

  if(result == SYSERROR)
    {
      die_with_errno(TCP_sock::internal_trouble,
		     "getsocketopt failed on SO_KEEPALIVE");
    }

  return (keepalive != 0);
}

void TCP_sock::enable_read_timeout(unsigned int seconds)
  throw (TCP_sock::failure)
{
  read_timeout_enabled = true;
  read_timeout_secs = seconds;

  // The read timeout enables non-blocking I/O, so we may need to turn
  // that on.
  configure_blocking();
}

void TCP_sock::disable_read_timeout() throw (TCP_sock::failure)
{
  read_timeout_enabled = false;

  // The read timeout enables non-blocking I/O, so we may need to turn
  // that off.
  configure_blocking();
}


//  *******************
//  *  Interrogation  *
//  *******************

Text TCP_sock::this_host() throw (TCP_sock::failure)
{
  TCP_sock::ensure_init();
  return TCP_sock::my_hostname;
}

void TCP_sock::get_local_addr(/*OUT*/ sockaddr_in &addr)
  throw (TCP_sock::failure)
{
  addr = this->me;

  // If we're a listener or not yet connected, we probably have the
  // wildcard address for our end.  In this case, return the IP
  // address that our hostname resolves to.
  if(addr.sin_addr.s_addr == INADDR_ANY)
    {
      addr.sin_addr = TCP_sock::self_addr;
    }
}

void TCP_sock::get_remote_addr(/*OUT*/ sockaddr_in &addr)
  throw (TCP_sock::failure)
{
  if (this->state != TCP_sock::dead) {
    // Permit either connected or dead, though exception message will 
    // refer only to connected.
    this->ensure_state(TCP_sock::connected);
  }
  addr = this->him;
}

bool TCP_sock::alive() throw (TCP_sock::failure)
{
  bool result = true;
  this->m.lock();
  if((this->state == TCP_sock::fresh) || (this->state == TCP_sock::dead))
    // Not yet connected or previously declared dead
    result = false;
  else
    {
      try {
	// Use 'poll' with a short timeout to determine the state of
	// the socket.  If 'poll' returns indicating data available or
	// timeout, assume the socket is happy.
	while (true) {
	  struct pollfd poll_data;
	  poll_data.fd = this->fd;
	  poll_data.events = POLLIN;
	  int nready = poll(&poll_data, 1, 0);

	  if (nready == 0) break;      // timeout; assume healthy
	  else if (nready > 0) {
	    if (poll_data.revents & (POLLERR | POLLNVAL)) {
	      result = false;  // unhappy
	    } else if (poll_data.revents & POLLIN) {
	      if(this->state == TCP_sock::listening)
		// Listening socket: assume all is well
		result = true;
	      else if(this->state == TCP_sock::connected)
		// Connected socket: check the count of bytes
		// available to be read
		{
		  int bytes_to_read;
		  if (ioctl(this->fd, FIONREAD, &bytes_to_read) == SYSERROR)
		    die_with_errno(TCP_sock::internal_trouble, "Error on 'ioctl'");
		  result = (bytes_to_read != 0);
		}
	    } else {
	      assert(false);
	    }
	    break;
	  } else if (nready == SYSERROR) {
	    // Deal with transient error cases.
	    if (errno == EAGAIN) {
	      // Insufficient resources to perform the 'poll' (whatever
	      // that means...).  Wait a while (with 'sleep', hoping that
	      // it uses different resources!) and try again.
	      sleep(1);
	      // Go around the loop again, retrying the 'poll'.
	    } else if (errno != EINTR) {
	      this->state = TCP_sock::dead;
	      die_with_errno(TCP_sock::internal_trouble, "Error on 'poll'");
	    }
	    // Loop and retry 'poll' if EINTR/EAGAIN.
	  } else {
	    assert(false);
	  }
	}
      } catch (failure &f) { this->m.unlock(); throw; }
    }
  this->m.unlock();
  return result;
}

u_short TCP_sock::parse_port(const Text &port)
  throw (TCP_sock::failure)
{
  u_short l_result = 0;

  // Parse port number or look it up (as service name) in network database.
  if (isdigit(port[0]))
    {
      l_result = atoi(port.cchars());
    }
  else
    {
      // getaddrinfo is preferable, so use it if available.
#if defined(USE_GETADDRINFO)
      struct addrinfo hints, *res = 0;
      memset(&hints, 0, sizeof(hints));
      hints.ai_family = PF_INET;
      int err = getaddrinfo(0, port.cchars(), &hints, &res);
      if(err != 0)
	{
	  throw(TCP_sock::failure(TCP_sock::unknown_port,
				  "unknown port: " + port + ": " +
				  gai_strerror(err)));
	}
      assert(res != 0);

      bool found = false;
      for(struct addrinfo *cur = res;
	  (cur != 0) && !found;
	  cur = cur->ai_next)
	{
	  assert(cur->ai_addr != 0);
	  if(cur->ai_addr->sa_family == AF_INET)
	    {
	      l_result = ((sockaddr_in *) cur->ai_addr)->sin_port;
	      found = true;
	    }
	}

      freeaddrinfo(res);

      if(!found)
	{
	  throw(TCP_sock::failure(TCP_sock::unknown_port,
				  "unknown port: " + port +
				  " (getaddrinfo indicates success, "
				  "but no usable result found)"));
	}
#elif defined(__digital__) || defined(__linux__)
      // This old code using getservbyname_r can probably be discarded
      // except for supporting old Tru64 systems.

      // Data structures for getservbyname_r
      struct servent sp;
# if defined(__digital__)
      struct servent_data sd;
# elif defined(__linux__)
      struct servent *sp_result;
      // getservbyname_r uses this storage when filling entries in the
      // struct servent.  The size of this array was an arbitrary
      // choice.  There may be better or worse values.  Somebody should
      // look into it.
      char sp_buf[1024];
# endif

# if defined(__digital__)
      memset((char *)(&sd), 0, sizeof(sd));
# elif defined(__linux__)
      memset(sp_buf, 0, sizeof(sp_buf));
# endif

# if defined(__digital__)
      if (getservbyname_r(port.cchars(), TCP_NAME, &sp, &sd) == SYSOK)
# elif defined(__linux__)
      ndb_mutex.lock(); 
      if (getservbyname_r(port.cchars(), TCP_NAME,
                             &sp,
                             sp_buf, sizeof(sp_buf),
                             &sp_result) == SYSOK)
# else
# error Do not know how to do getservbyname_r on this platform!
# endif
	{
# if defined(__linux__)
          ndb_mutex.unlock(); 
# endif
	  l_result = ntohs(sp.s_port);  // returned in network byte order
	  endservent();
	} else {
# if defined(__linux__)
          ndb_mutex.unlock(); 
# endif
	  throw(TCP_sock::failure(TCP_sock::unknown_port,
				  "unknown port: " + port));
	}
#else
#error Need re-entrant service name to port conversion
#endif
    }

  // Return the port number.
  return l_result;
}

in_addr TCP_sock::host_to_addr(const Text &hostname)
  throw (TCP_sock::failure)
{
  in_addr l_result;

  // Parse host name or look it up in network database.
  int dummy;
  if(isdigit(hostname[0]) &&
     (sscanf(hostname.chars(), "%d.%d.%d.%d",
	     &dummy, &dummy, &dummy, &dummy) == 4))
    {
      l_result.s_addr = inet_addr(hostname.chars());
    }
  else
    {
      // getaddrinfo is preferable, so use it if available.
#if defined(USE_GETADDRINFO)
      // Used to count the number of times we've gotten "TRY_AGAIN".
      unsigned int try_again_count = 0;
      while(1)
	{
	  // If we've been spinning with err == EAI_AGAIN, the
	  // name resolution setup is probably misconfigured.
	  if(try_again_count > g_resolv_try_again_limit)
	    {
	      throw
		TCP_sock::failure(TCP_sock::unknown_host,
				  Text("misconfigured name resolution "
				       "(spinning on non-fatal error) "
				       "resolving host: ") +
				  hostname);
	      
	    }

	  struct addrinfo hints, *res = 0;
	  memset(&hints, 0, sizeof(hints));
	  hints.ai_family = PF_INET;
	  int err = getaddrinfo(hostname.cchars(), 0, &hints, &res);
	  if(err != 0)
	    {
	      switch (err)
		{
		case EAI_AGAIN:
		  // TRY_AGAIN means there was a non-fatal problem
		  // with resolving this name.  Loop around and give
		  // it another shot.  Count the number of times this
		  // happens.
		  try_again_count++;
		  break;
		case EAI_SYSTEM:
		  // An internal error, refer to errno.
		  die_with_errno(TCP_sock::unknown_host,
				 Text("error resolving host: ") + hostname);
		case EAI_NONAME:
		  // No such host.
#ifdef EAI_NODATA
		case EAI_NODATA:
		  // No A record for host.
#endif
		  throw(TCP_sock::failure(TCP_sock::unknown_host,
					  Text("unknown host: ") + hostname));
		case EAI_FAIL:
		  // Unrecoverable error in host lookup.
		default:
		  // There shouldn't be any other cases that'll get us
		  // here, so stick with the generic message.
		  throw(TCP_sock::failure(TCP_sock::unknown_host,
					  Text("error resolving host: ") +
					  hostname + ": " +
					  gai_strerror(err)));
		}
	    }
	  else
	    {
	      assert(res != 0);

	      bool found = false;
	      for(struct addrinfo *cur = res;
		  (cur != 0) && !found;
		  cur = cur->ai_next)
		{
		  assert(cur->ai_addr != 0);
		  if(cur->ai_addr->sa_family == AF_INET)
		    {
		      l_result = ((sockaddr_in *) cur->ai_addr)->sin_addr;
		      found = true;
		    }
		}

	      freeaddrinfo(res);

	      if(!found)
		{
		  throw(TCP_sock::failure(TCP_sock::unknown_host,
					  Text("error resolving host: ") +
					  hostname +
					  " (getaddrinfo indicates success, "
					  "but no usable result found)"));
		}

	      break;
	    }
	}
#elif defined(__digital__) || defined(__linux__)
      // This old code using gethostbyname_r can probably be discarded
      // except for supporting old Tru64 systems.

      // Data structures for gethostbyname_r
      struct hostent hp;
# if defined(__digital__)
      struct hostent_data hd;
# elif defined(__linux__)
      struct hostent *hp_result;
      // gethostbyname_r uses this storage when filling entries in the
      // struct hostent.  The size of this array was an arbitrary
      // choice.  There may be better or worse values.  Somebody
      // should look into it.
      char hp_buf[1024];
# endif
      int h_errno_r;

      memset((char *)&l_result, 0, sizeof(l_result));
# if defined(__digital__)
      memset((char *)(&hd), 0, sizeof(hd));
# elif defined(__linux__)
      memset(hp_buf, 0, sizeof(hp_buf));
# endif

      // Used to count the number of times we've gotten "TRY_AGAIN".
      unsigned int try_again_count = 0;
      while(1)
	{
	  // If we've been spinning with h_errno == TRY_AGAIN, the
	  // name resolution setup is probably misconfigured.
	  if(try_again_count > g_resolv_try_again_limit)
	    {
	      throw
		TCP_sock::failure(TCP_sock::unknown_host,
				  Text("misconfigured name resolution "
				       "(spinning on non-fatal error) "
				       "resolving host: ") +
				  hostname);
	      
	    }
# if defined(__digital__)
	  if (gethostbyname_r(hostname.chars(), &hp, &hd) == SYSERROR)
# elif defined(__linux__)
	  ndb_mutex.lock(); 	    
	  if (gethostbyname_r(hostname.chars(), &hp,
			      hp_buf, sizeof(hp_buf),
			      &hp_result,
			      &h_errno_r) != SYSOK)
# else
# error Do not know how to do gethostbyname_r on this platform!
# endif
	    {
# if defined(__digital__)
	      h_errno_r = h_errno; 
# elif defined(__linux__)
	      ndb_mutex.unlock(); 
# endif
	      switch (h_errno_r)
		{
		case TRY_AGAIN:
		  // TRY_AGAIN means there was a non-fatal problem
		  // with resolving this name.  Loop around and give
		  // it another shot.  Count the number of times this
		  // happens.
		  try_again_count++;
		  break;
		case NETDB_INTERNAL:
		  // An internal error, refer to errno.
		  die_with_errno(TCP_sock::unknown_host,
				 Text("error resolving host: ") + hostname);
		case HOST_NOT_FOUND:
		  // No such host.
		case NO_DATA:
		  // No A record for host.
		  throw(TCP_sock::failure(TCP_sock::unknown_host,
					  Text("unknown host: ") + hostname));
		case NO_RECOVERY:
		  // Unrecoverable error in host lookup.
		default:
		  // There shouldn't be any other cases that'll get us
		  // here, so stick with the generic message.
		  throw(TCP_sock::failure(TCP_sock::unknown_host,
					  Text("error resolving host: ") +
					  hostname));
		}
	    }
	  else
	    {
# if defined(__linux__)
	      ndb_mutex.unlock(); 
# endif

	      // At this time, we're not ready for IPv6 or any other
	      // address family type.  At least TCP_sock.C and
	      // probably the rest of the SRPC library would need a
	      // significant audit before being ready for that.
	      if(hp.h_addrtype != AF_INET)
		{
		  throw(TCP_sock::failure(TCP_sock::unknown_host,
					  Text("non-IPv4 host: ") + hostname));
		}

	      // I've seen this happen (and cause a crash) in some
	      // cases.  (Why is gethostbyname_r so broken?)  Treat
	      // this like TRY_AGAIN.
	      if(hp.h_addr == 0)
		{
		  try_again_count++;
		  continue;
		}

	      memcpy((char *)(&l_result), hp.h_addr, hp.h_length);
	      endhostent();
	      break;
	    }
	}
#else
#error Need re-entrant host name to address conversion
#endif
    }

  return l_result;
}

bool TCP_sock::addr_to_host(in_addr addr, /*OUT*/ Text &name)
  throw (TCP_sock::failure)
{
  unsigned int try_again_count = 0;
  int err;
#if defined(USE_GETNAMEINFO)
  struct sockaddr_in sock_addr;
  char hname_buf[1024];
  memset(&sock_addr, 0, sizeof(sock_addr));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_addr.s_addr = addr.s_addr;
# if defined(SOCKADDR_IN_HAS_SIN_LEN)
  sock_addr.sin_len = sizeof(sock_addr);
# endif
#elif defined(__digital__)
  struct hostent hp, *h = &hp;
  struct hostent_data hd;
  memset((char *)(&hd), 0, sizeof(hd));
  int my_h_errno;
#elif defined(__linux__)
  struct hostent hp, *h = 0;
  char hp_buf[1024];
  memset(hp_buf, 0, sizeof(hp_buf));
  int my_h_errno;
#endif

  bool try_again;
  do
    {
#if defined(USE_GETNAMEINFO)
      // We use getnameinfo on any platform where it's available.
      err = getnameinfo((struct sockaddr *) &sock_addr, sizeof(sock_addr),
			hname_buf, sizeof(hname_buf),
			(char *) 0, 0, NI_NAMEREQD);
      try_again = (err == EAI_AGAIN);
#elif defined(__digital__)
      err = gethostbyaddr_r((const char*)&addr, 
			    sizeof(addr), AF_INET,
			    &hp, &hd);
      my_h_errno = h_errno;
      try_again = (err != 0) && (my_h_errno == TRY_AGAIN);
#elif defined(__linux__)
      err = gethostbyaddr_r((const char*)&addr, 
			    sizeof(addr), AF_INET,
			    &hp,
			    hp_buf, sizeof(hp_buf),
			    &h, &my_h_errno);
      try_again = (err != 0) && (my_h_errno == TRY_AGAIN);
#else
#error Need re-entrant host address to name conversion
#endif
    }
  // Repeast on h_errno == TRY_AGAIN, but only so many times.  (Some
  // broken name resolution configurations can cause infinite looping
  // with TRY_AGAIN).
  while(try_again &&
	(try_again_count++ < g_resolv_try_again_limit));

#if defined(USE_GETNAMEINFO)
  if(err != 0)
    {
      switch (err)
	{
	case EAI_AGAIN:
	  // Exceeded limit on retrying the request
	  throw
	    TCP_sock::failure(TCP_sock::unknown_host,
			      Text("misconfigured name resolution "
				   "(spinning on non-fatal error) "
				   "trying to get hostname for address: ") +
			      inet_ntoa_r(addr));
	  break;
	case EAI_SYSTEM:
	  // An internal error, refer to errno.
	  die_with_errno(TCP_sock::unknown_host,
			 Text("error trying to get hostname for address: ") +
			 inet_ntoa_r(addr));
	  break;
	case EAI_NONAME:
	  // No such host.
#ifdef EAI_NODATA
	case EAI_NODATA:
	  // No A record for host.
#endif
	  // We return false rather than throwing an exception in this
	  // case.
	  return false;
	case EAI_FAIL:
	  // Unrecoverable error in host lookup.
	default:
	  // There shouldn't be any other cases that'll get us
	  // here, so stick with the generic message.
	  throw(TCP_sock::failure(TCP_sock::unknown_host,
				  Text("error getting name for address: ") +
				  inet_ntoa_r(addr) + ": " +
				  gai_strerror(err)));
	}
    }
  // In an error, the above will either return or throw an exception.
  assert(err == 0);

  name = hname_buf;
#elif defined(__digital__) || defined(__linux__)
  if((err != SYSOK) || (h == 0))
    {
      switch (my_h_errno)
	{
	case TRY_AGAIN:
	  // Exceeded limit on retrying the request
	  throw
	    TCP_sock::failure(TCP_sock::unknown_host,
			      Text("misconfigured name resolution "
				   "(spinning on non-fatal error) "
				   "trying to get hostname for address: ") +
			      inet_ntoa_r(addr));
	  break;
	case NETDB_INTERNAL:
	  // An internal error, refer to errno.
	  die_with_errno(TCP_sock::unknown_host,
			 Text("error trying to get hostname for address: ") +
			 inet_ntoa_r(addr));
	  break;
	case HOST_NOT_FOUND:
	  // No such host.
	case NO_DATA:
	  // No A record for host.

	  // We return false rather than throwing an exception in this
	  // case.
	  return false;
	case NO_RECOVERY:
	  // Unrecoverable error in host lookup.
	default:
	  // There shouldn't be any other cases that'll get us
	  // here, so stick with the generic message.
	  throw(TCP_sock::failure(TCP_sock::unknown_host,
				  Text("error getting name for address: ") +
				  inet_ntoa_r(addr)));
	}
    }
  // In an error, the above will either return or throw an exception.
  assert(h != 0);

  name = h->h_name;
#endif

  // We only fall through to here if we succeeded.
  return true;
}

/* static */
void TCP_sock::name_to_sockaddr(const Text &hostname, 
  const Text &port, /*OUT*/ sockaddr_in &s) throw (TCP_sock::failure)
{
  // Make sure we've initialized static members.
  TCP_sock::ensure_init();

  // Start by clearing the structure we are to fill in.
  memset((char *)(&s), 0, sizeof(s));

  // Get the address of this hostname.  If no hostname was provided
  // default to the hostname of this machine.
  s.sin_addr = TCP_sock::host_to_addr(hostname.Empty()
				      ? TCP_sock::my_hostname
				      : hostname);

  // We assume AF_INET.  (If it were something else,
  // TCP_sock::host_to_addr would throw a failure anyway.)
  s.sin_family = AF_INET;

  // Parse port number or look it up (as service name) in network database.
  s.sin_port = htons(TCP_sock::parse_port(port));
}

/* static */
void TCP_sock::name_to_sockaddr(const Text &host_and_port,
  /*OUT*/ sockaddr_in &s) throw(TCP_sock::failure)
{
    int i = host_and_port.FindCharR(':');
    if (i < 1) throw(TCP_sock::failure(TCP_sock::invalid_parameter,
      "No colon separating hostname and port number"));
    name_to_sockaddr(host_and_port.Sub(0, i),
      host_and_port.Sub(i+1), /*OUT*/ s);
}

//  ********************
//  *  Client support  *
//  ********************

void TCP_sock::connect_to(const Text &hostname, const Text &port)
  throw (TCP_sock::failure)
{
  sockaddr_in addr;
  try {
    TCP_sock::name_to_sockaddr(hostname, port, /*OUT*/ addr);
  } catch (TCP_sock::failure &f) { this->state = TCP_sock::dead; throw; };
  this->connect_to(addr);
}

void TCP_sock::connect_to(const Text &host_and_port)
  throw (TCP_sock::failure)
{
  sockaddr_in addr;
  try {
    TCP_sock::name_to_sockaddr(host_and_port, /*OUT*/ addr);
  } catch (TCP_sock::failure &f) { this->state = TCP_sock::dead; throw; };
  this->connect_to(addr);
}

void TCP_sock::connect_to(sockaddr_in &addr)
  throw (TCP_sock::failure)
{
    this->ensure_state(TCP_sock::fresh);

    while (connect(this->fd, (sockaddr *)(&addr), sizeof(addr)) < 0) {
        // Capture errno in a local to make sure another system call
        // doesn't interfere.
        int l_errno = errno;
        if (l_errno == EISCONN) {
	    // got it!
	    break;
	} else if (l_errno == EINTR) {
	    /* The case where connect(2) was interrupted is not fatal;
               just go around the loop and try again. */
	} else if ((l_errno == EWOULDBLOCK) || (l_errno == EINPROGRESS) ||
		   (l_errno == EALREADY) || (l_errno == EAGAIN)) {
	    /* Wait for connection to finish (fd to become writable) */
	    struct pollfd flist[1];
	    flist[0].fd = this->fd;
	    flist[0].events = POLLOUT;
	    flist[0].revents = 0;
	    poll(flist, 1, -1);
 	} else if (l_errno == EADDRINUSE) {
	    /* This should be a transient condition.  We can check for
	       it going away by either executing a read that succeeds,
	       or by retrying connect. */
	    char b;
	    if (read(fd, &b, 0) == 0) {
		break;
	    }
	    /* Read failed: retry */
	} else {
	    // Otherwise, the error is fatal
	    this->state = TCP_sock::dead;
	    int code = TCP_sock::internal_trouble; 
	    switch (l_errno) {
	    case ETIMEDOUT:
	    case ENETUNREACH:
	      code = TCP_sock::unknown_host;
	      break;
	    case ECONNREFUSED:
	      code = TCP_sock::unknown_port;
	      break;
	    case EADDRNOTAVAIL:
	      code = TCP_sock::invalid_parameter;
	      break;
	      // Note: default value already set above
	    }

	    Text l_msg = "Can't connect to ";
	    l_msg += inet_ntoa_r(addr.sin_addr);
	    l_msg += ":";
	    
	    char l_port_txt[6];
	    sprintf(l_port_txt, "%d", ntohs(addr.sin_port));

	    l_msg += l_port_txt;

	    die_with_errno(code, l_msg, l_errno);
	}
    }
    this->him = addr;
    this->state = TCP_sock::connected;
}

//  **********************
//  *  Listener support  *
//  **********************

void TCP_sock::set_waiters(int waiters) throw (TCP_sock::failure)
{
    this->ensure_state(TCP_sock::fresh);
    if (listen(this->fd, waiters) == SYSERROR)
	die_with_errno(TCP_sock::internal_trouble,
          "can't set_waiters ('listen' failed)");
    this->state = TCP_sock::listening;
}

TCP_sock *TCP_sock::wait_for_connect()
  throw(TCP_sock::alerted, TCP_sock::failure)
{
  socklen_t len;
  TCP_sock *ns = 0;

  try {
    if (this->state == TCP_sock::fresh) this->set_waiters();

    this->ensure_state(TCP_sock::listening);

    // Create a new socket object but not a socket (as that's handled
    // by accept below).
    ns = NEW_CONSTR(TCP_sock, (empty_sock_marker()));

    while (true) {
	len = sizeof(ns->him);
	ns->fd = accept(this->fd, (sockaddr *)(&(ns->him)), &len);
	if (ns->fd != SYSERROR) break;
	// Accept failed.
	if (errno == EWOULDBLOCK) {
	    // Must use poll to wait (for possible alert)
	    this->controlled_wait();
	    // Loop and retry 'accept', which generally shouldn't fail.
	} else if (errno == EINTR) {
	    /*SKIP*/   // go around the loop again.
	} else {
	    this->state = TCP_sock::dead;
	    int code = (errno == ENOMEM)
              ? TCP_sock::environment_problem : TCP_sock::internal_trouble;
	    die_with_errno(code,
	      "accept failed on socket " + inet_ntoa_r(ns->me.sin_addr));
	}
    }

    // Disable Nagle's algorithm
    TCP_sock::set_no_delay(ns->fd);

    // Fill in complete socket name (mostly for cleanliness)
    len = sizeof(ns->me);
    if (getsockname(ns->fd, (sockaddr *)(&(ns->me)), &len) == SYSERROR) {
	this->state = TCP_sock::dead;
	int code = (errno == ENOBUFS)
          ? TCP_sock::environment_problem : TCP_sock::internal_trouble;
	die_with_errno(code,
          "getsockname failed on " + inet_ntoa_r(ns->me.sin_addr));
    }
    ns->state = TCP_sock::connected;
  } catch(...) {
    // Don't leak memory if we fail
    delete ns;
    throw;
  }
  return ns;
}

//  ***********************
//  *  Data transmission  *
//  ***********************

void TCP_sock::send_data(const char *buffer, int len, bool flush)
  throw (TCP_sock::failure)
{
  int i = 0;

  this->ensure_state(TCP_sock::connected);
  this->ensure_sndbuffer();

  while (len > 0) {
    int n = min(len, this->sndbuff_size - this->sndbuff_len);
    memcpy(this->sndbuff + this->sndbuff_len, buffer + i, n);
    this->sndbuff_len += n;
    i += n; len -= n;
    if (len > 0 || flush) {
      int unsent = this->sndbuff_len, j = 0, sent;
      while (true) {
	sent = write(this->fd, sndbuff + j, unsent);
	if (sent == unsent) break;
	if (sent == SYSERROR) {
          if (errno == EINTR || errno == EWOULDBLOCK || errno == ENOBUFS) {
            //
            // A transient send error occurred due to lack of space
            // on the outgoing socket buffer or to a transient 
            // interruption. Just wait until it is ok to send using
            // poll, and retry the write.
            while (true) {
	      struct pollfd poll_data[2];
	      unsigned int nfds = 1;
	      // Wait for our socket to be writable
	      poll_data[0].fd = this->fd;
	      poll_data[0].events = POLLOUT;
	      if (this->alerts_enabled) {
		nfds = 2;
		poll_data[1].fd = this->pipe_rd;
		// We're only interested in exceptions on this
		// channel.
		poll_data[1].events = 0;
	      }
	      // Use poll with a short timeout (1s)
	      int nready = poll(poll_data, nfds, 1000);
	      
              if (nready == 0) {
                // Timeout. Retry the write.
                break;
              } else if (nready == SYSERROR) {
                // Deal with transient error cases.
                if (errno == EAGAIN) {
                  // Insufficient resources to perform the 'poll'
                  // (whatever that means...).  Wait a while
                  // (with 'sleep', hoping that it uses different
                  // resources!) and try the write again.
                  sleep(1);
                  break;
                } else if (errno != EINTR) {
                  this->state = TCP_sock::dead;
                  die_with_errno(TCP_sock::internal_trouble,
                    "Error during 'poll'");
                }
                /* Loop and retry 'poll' if EINTR. Strictly speaking,
                   we should decrement the waiting time by the amount
                   of time already lapsed, but it's not worth the trouble.
                */
              } else if (poll_data[0].revents & POLLNVAL) {
                // Error
                this->state = TCP_sock::dead;
                die("Error on socket output");
              } else if (this->alerts_enabled &&
			 (poll_data[1].revents & (POLLERR | POLLNVAL))) {
                // Error
                this->state = TCP_sock::dead;
                die("Error on alert channel pipe");
              } else if (poll_data[0].revents & (POLLOUT |
						 POLLERR | POLLHUP)) {
                // Case 1: possible to send data.  (Note: POLLHUP or
                // POLLERR means that we *can't* write, but attempting
                // to will get us an error that indicates what
                // happened, so we treat it as writable.  This is also
                // the way select(2) treats errors.)
                break;
              } 
            }
          } else {
            this->state = TCP_sock::dead;
            int code = (errno == ENOBUFS)
              ? TCP_sock::environment_problem : TCP_sock::internal_trouble;
            die_with_errno(code, "Unable to send data");
          }
        } else {
          j += sent;
          unsent -= sent;
        }
      }
      this->sndbuff_len = 0;
    }
  }
}

int TCP_sock::fill_rcvbuffer()
  throw(TCP_sock::alerted, TCP_sock::failure)
{
  int bytes_read;
  int debug_errno = 0;

  if (this->non_blocking) {
    // Read with bounded delay
    while (true) {
      /* Socket is in non-blocking mode, so the following read won't
         block.  However, there is a possibility that the kernel will
       	 deliver a signal and abort the read, even though it isn't
       	 really waiting.  This is foolish, since the kernel could just
       	 as well return 0, but we guard against it anyway with a loop.
      */
      do {
        bytes_read = read(this->fd, this->rcvbuff,
          TCP_sock::default_rcvbuffer_size);
      } while (bytes_read == SYSERROR && errno == EINTR);

      debug_errno = errno;

      /* There are four possibilities now:

       	   (1) bytes_read > 0, implying data was read;

       	   (2a) bytes_read == 0, implying end-of-file (i.e., the
       		remote end closed its socket);

       	   (2b) bytes_read == SYSERROR and errno == ECONNRESET, which
       		sometimes occurs in situations in which one would expect
       		case 2a;

       	   (2c) bytes_read == SYSERROR and errno == ETIMEDOUT or
		EHOSTUNREACH; these errors can occur when the TCP
		keep-alive mechanism is enabled, and indicates that
		the keep-alive time has elapsed and that the remote
		host is either down or no longer reachable by the
		network.

       	   (3) bytes_read == SYSERROR and errno == EWOULDBLOCK, implying
       	       no data available at the moment; or

       	   (4) bytes_read == SYSERROR and errno has some other value,
       	       implying a real error.

       	 If case (1), (2a), (2b), (2c), or (4), the outcome of
       	 recv_data is determined (i.e., success or failure), so we
       	 exit the loop.  If case (3), we need to wait using 'poll'
       	 for data to arrive or a condition to arise that terminates
       	 the waiting (an alert).
      */
      if (bytes_read > 0) {
        return bytes_read; // case (1)
      } else if (bytes_read == 0) {
        break; // case (2a)
      } else if (bytes_read == SYSERROR && errno != EWOULDBLOCK) {
        break; // case (2b) or case (4)
      }

      // Case (3):
      assert(bytes_read == SYSERROR && errno == EWOULDBLOCK);
      this->controlled_wait();

      // Loop and retry the read, which should immediately return with
      // a positive value of bytes_read and thus exit the loop.
    } /* while */
  } else {
    // Read with potentially indefinite wait
    do {
      bytes_read = read(fd, rcvbuff, TCP_sock::default_rcvbuffer_size);
    } while (bytes_read == SYSERROR && errno == EINTR);
    debug_errno = errno;
  } /* if */

  // Control comes here when data has been read or a fatal error occurred.
  if (bytes_read > 0) {
    // Case (1)
    return bytes_read;
  } else if (bytes_read == 0) {
    // case (2a)
    this->state = TCP_sock::dead;
    throw(TCP_sock::failure(TCP_sock::partner_went_away,
      "connection closed by peer"));
  } else if (bytes_read == SYSERROR && errno == ECONNRESET) { 
    // case (2b)
    this->state = TCP_sock::dead;
    throw(TCP_sock::failure(TCP_sock::partner_went_away,
      "connection reset by peer"));
  } else if (bytes_read == SYSERROR && (errno == ETIMEDOUT ||
					errno == EHOSTUNREACH)) { 
    // case (2c)
    this->state = TCP_sock::dead;
    throw(TCP_sock::failure(TCP_sock::partner_went_away,
			    (errno == ETIMEDOUT)
			    ? "connection timed out"
			    : "peer unreachable"));
  } else if (bytes_read == SYSERROR) {
    // case (4)
    this->state = TCP_sock::dead;
    die_with_errno(TCP_sock::internal_trouble, "Error receiving data");
  }
  return -1; //not reached
}

int TCP_sock::recv_data(char *buffer, int len)
  throw (TCP_sock::alerted, TCP_sock::failure)
{
  int nread, bytes_read;
  this->ensure_state(TCP_sock::connected);
  this->ensure_rcvbuffer();

  if (this->rcvcurp >= this->rcvend) {
    assert(this->rcvcurp == this->rcvend);
    // The input buffer is empty, ask the kernel for more data.
    nread = this->fill_rcvbuffer();
    this->rcvcurp = this->rcvbuff;
    this->rcvend = this->rcvbuff + nread;
  }
  bytes_read = min(len, this->rcvend - this->rcvcurp);
  memcpy(buffer, this->rcvcurp, bytes_read);
  this->rcvcurp += bytes_read;
  return bytes_read;
}

bool TCP_sock::recv_data_ready() throw()
{
  if((rcvcurp != 0) && (rcvend - rcvcurp))
    {
      // We already have buffered data available.
      return true;
    }

  // See if there's data that the OS has received but we haven't read
  // yet.
  struct pollfd poll_data;
  poll_data.fd = this->fd;
  poll_data.events = POLLIN;
  poll_data.revents = 0;
  int nready = poll(&poll_data, 1, 0);

  return ((nready > 0) && (poll_data.revents & POLLIN));
}
