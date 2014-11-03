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

// See end of this file for an overview.

#ifndef _TCP_sock
#define _TCP_sock

#include <netdb.h>  // network host database access
#include <Basics.H>
#include <OS.H>

extern "C" void TCP_sock_init();

class TCP_sock {
public:

  // Error reporting
  enum {
    wrong_state          =  -1,   // inappropriate time to attempt function
    internal_trouble     =  -2,   // probable bug in TCP_sock implementation
    environment_problem  =  -3,   // unexpected external environment state
    unknown_host         =  -4,
    unknown_port         =  -5,
    invalid_parameter    =  -6,
    partner_went_away    =  -7,
    read_timeout         =  -8,    // no data from peer for too long
    end_of_file =         partner_went_away
    };
  struct failure {
    int reason;
    int err_no;
    Text msg;
    inline failure(int r, const Text &msg, int err_no = 0) {
      this->reason = r; this->msg = msg; this->err_no = err_no; };
    inline failure(const failure &f) {
      this->reason = f.reason; this->msg = f.msg; this->err_no = f.err_no; };
    inline failure& operator=(const failure &f) {
      this->reason = f.reason; this->msg = f.msg; this->err_no = f.err_no;
      return *this; };
  };

  struct alerted { };
    // optionally thrown by wait_for_connect and recv_data

  //
  // Construction
  //

  TCP_sock(u_short port = 0) throw (failure);

  ~TCP_sock() throw (failure);

  //
  // Optional configuration
  //

  enum {default_sndbuffer_size = 8192, default_rcvbuffer_size = 8192};

  void set_output_buffer_size(int bytes = default_sndbuffer_size)
    throw (failure);
    // By default, a buffer size of "default_sndbuffer_size" is used for
    // output.  However, this number can be adjusted by calling this 
    // procedure before the first "send_data" operation.

  void set_input_buffer_size(int bytes = default_rcvbuffer_size)
    throw (failure);
    // By default, a buffer size of "default_buffer_size" is used for
    // output.  However, this number can be adjusted by calling this 
    // procedure before the first "recv_data" operation.

  void enable_alerts() throw (failure);
  void disable_alerts() throw (failure);
    // These operations enable/disable the alert mechanism, which is
    // used to terminate an otherwise unbounded wait in recv_data and
    // wait_for_connect.

  void alert() throw (failure);
    // If alerts have not been enabled (see "enable_alerts", above), this
    // operation throws failure.  Otherwise, it sets the alert flag in the
    // TCP_sock.  Additionally, if another thread is presently waiting in
    // recv_data or wait_for_connect, the wait is terminated and "alerted"
    // is thrown by the operation.

  bool test_alert() throw (failure);
    // If alerts have not been enabled (see "enable_alerts", above), this
    // operation throws failure.  Otherwise, it returns the present value
    // of the alert flag and sets the flag to false.

  void set_keepalive(bool state)  throw (failure);
  inline void enable_keepalive() throw (failure)
    { set_keepalive(true); }
  void disable_keepalive() throw (failure)
    { set_keepalive(false); }
  bool get_keepalive() throw (failure);
    // Enable, disable, or check for the use of the TCP keep-alive
    // timer (set by the SO_KEEPALIVE socket option).  This provides
    // for detection of a peer whose system has crashed, rebooted, or
    // which is no longer reachable.  (See the setsockopt(2) and/or
    // socket(7) man pages for more information.)

  void enable_read_timeout(unsigned int seconds) throw (failure);
  void disable_read_timeout() throw (failure);
  inline bool get_read_timeout(/*OUT*/ unsigned int &seconds) throw()
    {
      if(read_timeout_enabled)
	seconds = read_timeout_secs;
      return read_timeout_enabled;
    }
    // Enable, disable, or check the setting of the read timeout
    // feature.  This makes it possible to set an upper limit on the
    // amount of time spent waiting for data from a peer.  This
    // feature can be used by servers to prevent holding a resource
    // for an indefinite amount of time due to suspended or
    // misbehaving clients.
  
  //
  // Interrogation
  //

  static Text this_host() throw (failure);
    // returns a/the name of this host.

  void get_local_addr(/*OUT*/ sockaddr_in &addr) throw (failure);
    // 'addr' is filled in with the local socket address (in network byte
    // order) of this socket.

  void get_remote_addr(/*OUT*/ sockaddr_in &addr) throw (failure);
    // 'addr' is filled in with the remote (i.e., connected) socket address
    // (in network byte order), if any.  If the socket is not connected,
    // failure is thrown.

  bool alive() throw (failure);
    // This function should be used only for a connected socket (i.e.,
    // a socket returned by 'wait_for_connect' or one on which 'connect_to'
    // has been successfully executed.)  It attempts to determine if the
    // connection is intact.  A 'false' result means the connection is
    // definitely broken; a 'true' return strongly suggests, but does not
    // guarantee, that a subsequent 'recv_data' or 'send_data' will
    // succeed.

  static u_short parse_port(const Text &port) throw (failure);
    // This is a utility routine that translates parses a text string
    // specifying a port.  If it can be parsed as a decimal number, it
    // is taken to be an explicit port number.  Otherwise, 'port' is
    // looked up as a service in the network database; if it can't be
    // found there, 'failure' is thrown.  Note that the port number is
    // returned in host byte order, not network byte order.

  static in_addr host_to_addr(const Text &hostname) throw (failure);
    // This is a utility routine that translates a hostname to an IPv4
    // Internet address.  If 'hostname' can be parsed as four decimal
    // numbers separated by periods, it is taken to be an explicit
    // (numeric) IP address.  Otherwise, 'hostname' is looked up in
    // the network naming database; if it can't be found there,
    // 'failure' is thrown.  If 'hostname' resolves to a non-IPv4
    // address, failure is thrown.  (IPv6 is not yet supported by
    // TCP_sock.)

  static bool addr_to_host(in_addr addr, /*OUT*/ Text &name) throw (failure);
    // This is a utility routine that translates an IPv4 Internet
    // address to a hostname (i.e. reverse DNS).  If a hostname for
    // the address can be found, it will be stored in "name" and the
    // trurn value is true.  If no hostname can be found for the
    // address, false is returned.  (Note that we don't use an
    // excpetion in this case because it's a more common outcome than
    // an error.)  In the event of an error trying to find a hostname,
    // 'failure' is thrown.

  static void name_to_sockaddr(const Text &hostname,
    const Text &port, /*OUT*/ sockaddr_in &s) throw (failure);
    // This is a utility routine that translates a hostname and port
    // to a socket address.  If the translation is successful, the
    // socket address 's' is returned in network byte order.
    // If 'hostname' is empty, the local host's name is used.  If 'hostname'
    // can be parsed as four decimal numbers separated by periods, it is
    // taken to be an explicit (numeric) IP address.  Otherwise, 'hostname'
    // is looked up in the network naming database; if it can't be found
    // there, 'failure' is thrown.
    // 'port' is interpreted as follows.  If it can be parsed as a
    // decimal number, it is taken to be an explicit port number.
    // Otherwise, 'port' is looked up as a service in the network
    // database; if it can't be found there, 'failure' is thrown.

  static void name_to_sockaddr(const Text &host_and_port,
    /*OUT*/ sockaddr_in &s) throw (failure);
    // This is an alternative form of the preceding procedure.  It is
    // semantically equivalent to:
    //   int i = host_and_port.FindCharR(':');
    //   if (i < 1) throw (failure(...));
    //   name_to_sockaddr(host_and_port.Sub(0, i), host_and_port.Sub(i+1), s);

  fd_t get_fd() throw () { return fd; };
    // returns the Unix file descriptor associated with the socket

  //
  // Client
  //

  void connect_to(const Text &hostname, const Text &port) throw (failure);
    // "hostname" may be "abc" or "w.x.y.z".  "port" may be either
    // a service name or a decimal number.

  void connect_to(const Text &host_and_port) throw (failure);
    // Semantically equivalent to:
    //   int i = host_and_port.FindCharR(':');
    //   if (i < 1) throw (failure(...));
    //   connect_to(host_and_port.Sub(0, i), host_and_port.Sub(i+1));

  void connect_to(sockaddr_in &addr) throw (failure);
    // 'addr' contains a socket address in network byte order.

  //
  // Listener
  //

  void set_waiters(int waiters = 1) throw (failure);
    // See description under overview, below.

  TCP_sock *wait_for_connect() throw (alerted, failure);
    // See description under overview, below.

  //
  // Data transmission
  //

  void send_data(const char *buffer, int len, bool flush = false)
    throw (failure);

  inline void flush() throw (failure) { send_data(NULL, 0, true); };

  int recv_data(char *buffer, int len) throw (alerted, failure);
    // Waits until at least one byte of data is available, then places
    // at most "len" bytes in the buffer defined by "buffer".  The number
    // of bytes stored is returned as the result, which will never be zero.
    // Note that "end of file" is reported as a specific failure reason.
    // If the alert flag associated with the TCP_sock is set either at
    // entry or while recv_data is blocked, "alerted" is raised.  See
    // description of "alert".

  unsigned int recv_buff_bytes() throw()
      {
        return ((rcvcurp == 0)
		? 0
		: (rcvend - rcvcurp));
      }
    // return the number of buffered bytes read from the socket

  bool recv_data_ready() throw();
    // Is there at least one byte of data that can be read without
    // blocking?

private:

  // A TCP_sock should neither be copied nor assigned.

  TCP_sock(TCP_sock&);

  void operator=(TCP_sock&);

  // Constructor for creating a TCP_sock without an underlying socket
  // (used when accepting connections).  We use an otherwise unused
  // non-integer type to disambiguate calls of this constructor from
  // the default (declared above).
  struct empty_sock_marker { empty_sock_marker() { } };
  TCP_sock(const empty_sock_marker &) throw (TCP_sock::failure);

  // data fields
  Basics::mutex m;              // protects most of the following fields

  enum sock_state { fresh, listening, connected, dead };
  static const char *state_str(sock_state);
  sock_state state;
                                // These are in network byte order:
  sockaddr_in me;               // local IP address/port
  sockaddr_in him;              // remote IP address/port, if connected
  fd_t fd;                      // file descriptor
  char *sndbuff;                // output buffer
  int sndbuff_len;              // current number of bytes
  int sndbuff_size;             // allocated size
  char *rcvbuff;                // input buffer
  char *rcvcurp;                // current read location
  char *rcvend;                 // end of valid data in the read buffer
  int rcvbuff_size;             // input buffer size

  bool non_blocking;

  bool alerts_enabled;
  bool alert_flag;
  fd_t pipe_wr;                 // pipe fd's used to implement alert channel
  fd_t pipe_rd;
  int bytes_in_pipe;

  // Optional timeout on reading data from the peer.
  bool read_timeout_enabled;
  unsigned int read_timeout_secs;

  // member functions

  static void init() throw (failure);
  friend void TCP_sock_init();
  static void ensure_init() throw (failure);
  static void set_no_delay(int fd) throw (failure);

  void new_socket(u_short port) throw (failure);
  void instance_init() throw();
  void ensure_state(sock_state should_be) throw (failure);
  void ensure_sndbuffer() throw ();
  void ensure_rcvbuffer() throw ();
  void configure_blocking() throw (failure);
  void controlled_wait() throw (alerted, failure);
  int  fill_rcvbuffer() throw (alerted, failure);

  // static data; filled in by "init"
  static pthread_once_t init_block;
  static Text my_hostname;
  static sockaddr_in defaultsock;  // default values for sockect creation
  static in_addr self_addr;        // address of our hostname
  static int tcp_protolevel;

#if defined(__linux__)
  // glibc 2.1... and 2.2... have a problem with gethostbyname_r and
  // perhaps getservbyname_r  -- they aren't really all that re-entrant.
  // so we mutex them for linux. 
  static Basics::mutex ndb_mutex;
#endif
};

/*
Overview of TCP_sock
--------------------

This class provides TCP-based sockets intended specifically to support
simple RPC's (SRPC).  It makes no attempt to provide comprehensive
TCP functionality.

A TCP_sock is constructed by optionally supplying an IP address and/or
port number.  If the IP address is omitted, the primary address of the
local host machine is used.  If the port is 0, the implementation
chooses a suitable value.  In either case, the resulting object is
assigned a file descriptor (used internally) which is bound, in the
sense of bind(2), to the specified IP address and port.

A newly-created TCP_sock may be either used to connect to a specified
address or to listen for connection requests.  For the former purpose,
the client calls connect_to, specifying the socket address (IP address and
port) to which the connection is to be made.

To listen on a TCP_sock, a client optionally invokes set_waiters,
specifying the number of incoming requests that will be buffered
(rather than rejected) while the client is busy with other things.  To
receive an incoming request, the client calls wait_for_connect, which
waits for a request to arrive (if necessary), creates a new TCP_sock
object, and connects it to the IP address/port provided by the
requestor.  This object is returned by wait_for_connect.

Data transmission is buffered.  To send data, the client invokes send_data,
specifying an address and byte count.  The data is taken from the specified
location and buffered.  The buffer is flushed when it becomes full or when
send_data is called with its third parameter set to true.

By default, data reception is synchronous; that is the recv_data function
blocks until data arrives.  However, the client may optionally enable either
or both of two mechanisms to prevent recv_data from blocking indefinitely.
First, a TCP_sock may be marked as alertable, using the enable_alerts
function.  On such a socket recv_data blocks until either data arrives or
until another thread invokes the alert function.  In the latter case, recv_data
throws 'alerted'.

Wherever socket addresses are visible through this interface, their fields
are maintained in network byte-order.  This follows the conventions used
by the network database routines, but can lead to surprises if clients
construct socket addresses piecemeal.

A TCP_sock is inherently a sequentially-used object.  Multi-threaded access
to a TCP_sock by a  client must be synchronized by the client.


*/

#endif  /* _TCP_sock */
