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



#ifndef _SRPC
#define _SRPC

//  ****************
//  *  Simple RPC  *
//  ****************


// This class provides a simple RPC facility that emphasizes
// convenience rather than generality.  It handles reliable connection
// of caller and callee, but doesn't support general-purpose argument
// marshalling.  The underlying communication protocol is assumed to
// be TCP; this class depends on TCP_sock, although the user of this
// class can largely ignore that fact.

// A description of the use of this class appears at the end of this file.

// The implementation depends on the pthreads and reentrant C libraries
// SRPC does not depend upon the garbage collector.

#include <Basics.H>
#include "TCP_sock.H"
#include "int_seq.H"
#include "chars_seq.H"
#include "bytes_seq.H"

class SRPC_impl;

class SRPC {
  friend class SRPC_impl;
public:

  //
  // Generic failure type (for exception handling).
  //

  enum {
    // By convention, negative values represent generic problems.  Particular
    // interfaces use positive values to report specific problems if they
    // wish, or may use generic values (e.g., "protocol_violation") if they
    // prefer.
    unknown_host       = -1,
    unknown_interface  = -2,       //no exporter or bogus port number
    version_skew       = -3,       //start_call/await_call rules violation
    protocol_violation = -4,       //mismatch, e.g. send_X with recv_Y
    buffer_too_small   = -5,       //see recv_*_seq, recv_chars_here
    transport_failure  = -6,       //underlying transport (TCP) problem
    internal_trouble   = -7,       //bug in SRPC implementation
    invalid_parameter  = -8,       //probable caller bug
    partner_went_away  = -9,       //partner's SRPC destructor invoked
    not_implemented    = -10,
    read_timeout       = -11       // no data from peer for too long
    };
  struct failure {
    int r;
    Text msg;
    inline failure() : r(0) { };
    inline failure(int r, const Text &msg) {
      this->r = r; this->msg = msg; };
    inline failure(const failure &f) {
      this->r = f.r; this->msg = f.msg; };
    inline failure& operator=(const failure &f) {
      this->r = f.r; this->msg = f.msg; return *this; };
  };

  //
  // SRPC creation and destruction
  //

  enum which_end { caller, callee };

  SRPC(which_end me, const Text &interface, const Text &hostname = "")
    throw(failure);
    // If "which_end" is "caller", then the remaining parameters identify
    // the callee.  "interface" is a string identifying the exported service
    // to be called; "hostname", if present, specifies a particular exporter,
    // which may be identified by name or by an explicit IP address.
    // If "interface" is a decimal number, it is assumed to be a port number
    // on a socket defined by "hostname", which in this case cannot be
    // defaulted.
    //
    // If "which_end" is "callee", then "interface" defines the
    // service being exported by the local machine.  If it is a
    // decimal number, it is interpreted as a port number.  "hostname"
    // is ignored for callees.

  SRPC(which_end me, TCP_sock *sock) throw(failure);
    // If "which_end" is "caller", "sock" specifies a connected TCP socket.
    // If "which_end" is "callee", "sock" specifies a listener TCP socket.

  SRPC(TCP_sock *sock) throw(failure);
    // "sock" specifies a connected TCP socket.  This implicitly sets
    // the behavior to callee.

  ~SRPC() throw(failure);
    // The underlying transport layer's connection is shutdown and any
    // resources it consumes are released.  If this destructor is invoked
    // before send_end(...) has returned "true", "failure" is thrown on the
    // remote end.

  static Text this_host() throw(failure);
    // Returns a/the name for the local machine.  This is the name
    // that is used when the 'hostname' argument in the first
    // SRPC constructor, above, has its default value.

  static void split_name(const Text &host_and_port, Text &host, Text &port);
    // Divides 'host_and_port', which is expected to be of the form
    // <host>:<port>, into two Texts 'host' and 'port'.  If no colon is
    // present, the entire Text is returned as 'host' and 'port' is empty.

  //
  // Connection establishment (call identification)
  //

  enum {default_intf_version = -1};
  enum {default_proc_id = -1};
  void start_call(int proc_id = default_proc_id,
		  int intf_version = default_intf_version) throw(failure);
  void await_call(int &proc_id, int &intf_version) throw(failure);

  //
  // Connection information
  //

  bool previous_failure(SRPC::failure *f = NULL) throw();
    // If a failure has previously occurred on the SRPC connection, this
    // function returns true and, if 'f' is non-NULL, fills in *f with the
    // information associated with the previous failure.  Otherwise, this
    // function returns false and the value of 'f' is immaterial (that is,
    // *f is unaffected).

  bool alive(SRPC::failure *f = NULL) throw();
    // Returns true if previous_failure(f) is false and if, upon inspection,
    // the SRPC connection seems to be healthy.  In this case, the value
    // of 'f' is immaterial.  Otherwise, alive(f) returns false and, if 'f'
    // is non-NULL, *f is filled in with the reason that the connection is
    // no longer considered usable.

  TCP_sock *socket() throw(failure);
    // Returns the underlying TCP_socket for the SRPC connection.  If no
    // connection has yet been established (as is the case for the callee
    // side immediately after construction), NULL is returned.

  Text local_socket() throw(failure);
    // s.local_socket() is semantically equivalent to:
    //   TCP_sock *sock = s.socket();
    //   if (sock == NULL) return NULL;
    //   sockaddr_in me;
    //   sock->get_local_addr(me);
    //   char a[22];
    //   sprintf(a, "%s:%d", inet_ntoa(me.sin_addr), me.sin_port);
    //   return Text(a);

  Text remote_socket() throw(failure);
    // s.remote_socket() is semantically equivalent to:
    //   TCP_sock *sock = s.socket();
    //   if (sock == NULL) return NULL;
    //   sockaddr_in him;
    //   sock->get_remote_addr(him);
    //   char a[22];
    //   sprintf(a, "%s:%d", inet_ntoa(him.sin_addr), him.sin_port);
    //   return Text(a);

  // Is this conneciton in a state where start_call is allowed?
  bool start_call_ok() throw();

  //
  // Configuration
  //
  
  void enable_read_timeout(unsigned int seconds,
			   bool use_between_calls = false) throw ();
  void disable_read_timeout() throw ();
  inline bool get_read_timeout(/*OUT*/ unsigned int &seconds,
			       /*OUT*/ bool &use_between_calls) throw();
    // Enable, disable, or check the setting of the read timeout
    // feature.  This makes it possible to set an upper limit on the
    // amount of time spent waiting for data from a peer.  This
    // feature can be used by servers to prevent holding a resource
    // for an indefinite amount of time due to suspended or
    // misbehaving clients.  Normally the timeout is only used during
    // each call and is disabled between calls.

  //
  // Single argument/result transmission
  //

  void send_int32(int i) throw(failure);
  int recv_int32(bool *got_end = NULL) throw(failure);

  inline void send_int(int i) throw(failure)
  {
    send_int32(i);
  }
  inline int recv_int(bool *got_end = NULL) throw(failure)
  {
    return recv_int32(got_end);
  }

  void send_int16(Basics::int16 i) throw(failure);
  Basics::int16 recv_int16(bool *got_end = NULL) throw(failure);

  inline void send_short(Basics::int16 i) throw(failure)
  {
    send_int16(i);
  }
  inline Basics::int16 recv_short(bool *got_end = NULL) throw(failure)
  {
    return recv_int16(got_end);
  }

  void send_int64(Basics::int64 i) throw(failure);
  Basics::int64 recv_int64(bool *got_end = NULL) throw(failure);

  // Send and receive single precision fp numbers.
  // Used for sending load averages around.
  // assumes sizeof(float) == sizeof(int),
  // which I think is as safe as recv_int32 assuming sizeof(int) == sizeof(int32).
  inline void send_float(float f) throw(failure)
  {
    union { int i; float f; } u;
    u.f = f;
    send_int32(u.i);
  }
  inline float recv_float(bool *got_end = NULL) throw(failure)
  {
    union { int i; float f; } u;
    u.i = recv_int32(got_end);
    return u.f;
  }

  // Send and receive booleans.  Intended for use with optional
  // parameters.
  void send_bool(bool b) throw (failure);
  bool recv_bool(bool *got_end = NULL) throw(failure);

  void send_chars(const char *s) throw(failure);
    // The sequence of characters beginning at "s" and terminated by
    // a null byte (binary zero) is transmitted.  Passing NULL for "s"
    // is equivalent to passing a zero-length string.
  char *recv_chars(bool *got_end = NULL) throw(failure);
    // The incoming string is stored in dynamic storage; a pointer to it
    // is returned.  The client is responsible for deallocating the storage.
  char *recv_chars(char *buff, int buff_len, bool *got_end = NULL)
    throw(failure);
    // If the incoming string (plus its null terminator) is at most "buff_len",
    // then the string and its null terminator are read into "buff" and "buff"
    // is returned. Otherwise, dynamic storage is allocated to hold the
    // incoming string (and its null terminator), and a pointer to that buffer
    // is returned. In the latter case, the client is responsible for de-
    // allocating the storage.
  void recv_chars_here(char *buffer, int &len, bool *got_end = NULL)
    throw(failure);
    // "buffer" and "len" define a storage area into which the incoming
    // string is placed.  If space permits, the string is stored (with a
    // null terminator) and "len" is updated to reflect its length (without
    // the terminator).  If the string is too long to fit in the space
    // provided, "failure(buffer_too_small)" is thrown.

  void send_Text(const Text &t) throw(failure);
    // The Text 't' is transmitted.
  void recv_Text(/*OUT*/ Text &t, bool *got_end = NULL) throw(failure);
    // Set "t" to the incoming text.

  void send_bytes(const char *buffer, int len) throw(failure);
    // The sequence of characters beginning at "buffer" and of length "len"
    // is transmitted.
  char *recv_bytes(int &len, bool *got_end = NULL) throw(failure);
    // An incoming sequence of bytes is stored in dynamically allocated
    // storage, whose address is returned as the result of this function.
    // Additionally, the "len" parameter is set to the length of the
    // received sequence.  The client is responsible for deallocating
    // the storage used by the sequence.
  void recv_bytes_here(char *buffer, int &len, bool *got_end = NULL)
    throw(failure);
    // "buffer" and "len" define a storage area into which the incoming
    // sequence of bytes is placed. If space permits, the bytes are stored
    // in a prefix of "buffer" and "len" is set to the number of bytes
    // received. If the number of incoming bytes is greater than the initial
    // value of "len", then "failure(buffer_too_small)" is thrown.

  void send_socket(sockaddr_in &sock) throw(failure);
    // "sock" is an Internet socket address containing an IP address and
    // port stored (as is customary in Unix) in network byte order.
  sockaddr_in recv_socket(bool *got_end = NULL) throw(failure);
    // Returns an Internet socket address containing an IP address and port
    // stored in network byte order.  The remaining fields of the resulting
    // "sockaddr_in" are initialized appropriately, so that the address can
    // be used immediately, e.g., as a parameter to "TCP_sock::connect_to".

  //
  // Sequence of argument/result transmission: special cases
  //

  void send_int16_array(const Basics::int16 *seq, int len)
    throw (SRPC::failure);
    // Send the array of 16-bit integers in "seq", which must have
    // length "len".
  Basics::int16* recv_int16_array(/*OUT*/ int &len) throw (SRPC::failure);
    // Receive an array of 16-bit integers into a newly allocated
    // array. The new array is returned, and "len" is set to its
    // length.

  inline void send_short_array(const Basics::int16 *seq, int len)
    throw (SRPC::failure)
  {
    send_int16_array(seq, len);
  }
  inline Basics::int16* recv_short_array(/*OUT*/ int &len)
    throw (SRPC::failure)
  {
    return recv_int16_array(len);
  }

  void send_int32_array(const Basics::int32 *seq, int len)
    throw (SRPC::failure);
    // Send the array of 32-bit integers in "seq", which must have
    // length "len".
  Basics::int32* recv_int32_array(/*OUT*/ int &len) throw (SRPC::failure);
    // Receive an array of 32-bit integers into a newly allocated
    // array. The new array is returned, and "len" is set to its
    // length.

  void send_int64_array(const Basics::int64 *seq, int len)
    throw (SRPC::failure);
    // Send the array of 64-bit integers in "seq", which must have
    // length "len".
  Basics::int64* recv_int64_array(/*OUT*/ int &len) throw (SRPC::failure);
    // Receive an array of 64-bit integers into a newly allocated
    // array. The new array is returned, and "len" is set to its
    // length.

  void send_int_seq(int_seq &is) throw(failure);
    // "send_int_seq" transmits the sequence of integers specified by "is".
    // It is legal to send an integer sequence as a component of a general-case
    // sequence (i.e., between calls of "send_seq_start" and "send_seq_end").
    // Except for this proviso, the semantics of "send_int_seq" are
    // equivalent to:
    //   send_seq_start(cs.length());
    //   for (int i = 0; i < is.length(); i++) send_int(is[i]);
    //   send_seq_end();
  void recv_int_seq(int_seq &is) throw (failure);
    // "recv_int_seq" receives a sequence of integers into storage defined
    // by "is".  It is legal to receive an integer sequence as a component
    // of a general-case sequence (i.e., between calls of "recv_seq_start"
    // and "recv_seq_end").  Except for this proviso, the semantics of
    // "recv_int_seq" are equivalent to:
    //   recv_seq_start();
    //   while (true) {
    //     bool got_end;
    //     int i = recv_int(&got_end);
    //     if (got_end) break;
    //     is.append(i);
    //   };
    //   recv_seq_end();

  void send_chars_seq(chars_seq &cs) throw(failure);
    // "send_chars_seq" transmits a sequence of character strings.  The
    // chars_seq class provides various ways to construct such a sequence.
    // It is legal to send a string sequence as a component of a general-case
    // sequence (i.e., between calls of "send_seq_start" and "send_seq_end").
    // Except for this proviso, the semantics of "send_chars_seq" are
    // equivalent to:
    //   send_seq_start(cs.length());
    //   for (int i = 0; i < cs.length(); i++) send_chars(cs[i]);
    //   send_seq_end();
  void recv_chars_seq(chars_seq &cs) throw(failure);
    // "recv_chars_seq" receives a sequence of character strings.  The
    // "cs" parameter will generally be a freshly constructed chars_seq;
    // see the description of the chars_seq class, below, for various
    // construction options.  If "cs" is incapable of accommodating the
    // incoming sequence, "failure(buffer_too_small)" is thrown.
    // It is legal to receive a character string sequence as a component
    // of a general-case sequence (i.e., between calls of "recv_seq_start"
    // and "recv_seq_end").  Except for this proviso, the semantics of
    // "recv_chars_seq" are equivalent to:
    //   recv_seq_start();
    //   while (true) {
    //     bool got_end;
    //     char *s = recv_chars(&got_end);
    //     if (got_end) break;
    //     cs.append(s);
    //   };
    //   recv_seq_end();

  void send_bytes_seq(bytes_seq &bs) throw(failure);
    // "send_bytes_seq" transmits a sequence of byte strings.  The
    // bytes_seq class provides various ways to construct such a sequence.
    // It is legal to send a byte-string sequence as a component of a
    // general-case sequence (i.e., between calls of "send_seq_start" and
    // "send_seq_end").  Except for this proviso, the semantics of
    // "send_bytes_seq" are equivalent to:
    //   send_seq_start(bs.length());
    //   for (int i = 0; i < bs.length(); i++) send_bytes(bs[i]);
    //   send_seq_end();
  void recv_bytes_seq(bytes_seq &bs) throw(failure);
    // "recv_bytes_seq" receives a sequence of byte strings.  The
    // "bs" parameter will generally be a freshly constructed bytes_seq;
    // see the description of the bytes_seq class, below, for various
    // construction options.  If "bs" is incapable of accommodating the
    // incoming sequence, "failure(buffer_too_small)" is thrown.
    // It is legal to receive a byte-string sequence as a component
    // of a general-case sequence (i.e., between calls of "recv_seq_start"
    // and "recv_seq_end").  Except for this proviso, the semantics of
    // "recv_bytes_seq" are equivalent to:
    //   recv_seq_start();
    //   while (true) {
    //     bool got_end;
    //     int len;
    //     char *s = recv_bytes(len, &got_end);
    //     if (got_end) break;
    //     bs.append(byte_str(s, len));
    //   };
    //   recv_seq_end();


  //
  // Sequence of argument/result transmission: general case
  //

  enum {any = -1 };

  void send_seq_start(int len = any, int bytes = any) throw(failure);
    // If the "len" parameter is not "any", it specifies the number
    // of elements in the sequence.  If the "bytes" parameter is not
    // "any", it specifies the total storage required for the sequence.
    // Any argument other than another general-case sequence may be
    // transmitted as part of the general-case sequence.
  void recv_seq_start(int *len = NULL, int *bytes = NULL) throw(failure);
    // The "len" and "bytes" parameters supplied by send_seq_start are
    // returned (provided the corresponding parameter pointers are non-NULL).
    // These can be used by the receiver to allocate appropriate storage for
    // the sequence.  It is the responsibility of sender and receiver to
    // communicate and use these quantities properly; the SRPC implementation
    // neither interprets nor verifies them.
    // Any argument other than another general-case sequence may be transmitted
    // as part of the general-case sequence.
  void send_seq_end() throw(failure);
    // This function is called to inform the receiver that all members of
    // the sequence have been transmitted.  It is required, even if
    // "send_seq_start" was called with an explicit "len" parameter.
  void recv_seq_end() throw(failure);
    // This function is called by the receiver when it believes it has
    // received all elements of the sequence.  That belief may be based
    // on interpretation of the "len" value returned by "recv_seq_start", by
    // the inherent structure of the sequence (e.g., an agreed-upon marker),
    // or by a true value for the "got_end" result parameter to a preceding
    // call of "recv_X".

  //
  // Argument/result transmission completion
  //

  void send_end() throw(failure);
    // If send_end returns normally, the invoker is assured that the preceding
    // arguments/results were accepted by the other end.
  void recv_end() throw(failure);
    // If recv_end returns normally, the invoker can assume that the stream
    // of arguments/results ended as expected and the other end has been
    // informed that all values were properly received.


  //
  // Failure reporting
  //

  void send_failure(int r,
		    const Text &msg,
		    bool remote_only = false) throw(failure);
    // The exception "failure" is transmitted to the other end of the
    // SRPC connection and thrown at the next opportunity.  This
    // function returns normally if and only if "remote_only" is true
    // and the (remote) failure notification occurs without trouble;
    // otherwise, "failure" is thrown (locally).  Regardless of whether
    // "failure" is thrown locally, the SRPC object is useless for
    // further operations; a subsequent attempt to use it will throw
    // "failure(protocol_violation)".

  static failure convert_TCP_failure(TCP_sock::failure tf);
    // This utility function converts a failure reported by the TCP_sock
    // machinery to a suitable SRPC::failure.

private:
  SRPC_impl *p;
  static pthread_once_t init_block;
  static void init() throw (failure);

  // An SRPC object should neither be copied nor assigned.
  SRPC(SRPC &);
  void operator=(SRPC &);
};


//  ***************************
//  *  Use of the SRPC class  *
//  ***************************

/*

An SRPC object represents one end of a "connection" that is used for a
remote procedure call.  The actual connection, however, is essentially
invisible to the client (which gives the implementation freedom in
choosing the underlying transport protocol -- today, it's TCP).
Because the connection is being used for a procedure call and not
arbitrary bidirectional communication, the semantics of SRPC member
functions are tuned to this purpose.  This is done to reduce the need
for handshaking between the ends and thereby improve performance.

The possibility of a remote call begins when a server (who will be the
callee) advertises its availability.  It does so by using one of the
SRPC constructors and specifying the first parameter as "callee".  The
other parameters define the service (e.g., by name or well-known
socket/port).  This produces an SRPC object, but does not block
awaiting a connection from a client.  However, the creation of the
SRPC object by the callee causes the underlying machinery to "listen"
for client requests.  A server may create multiple SRPC objects for
the same service; they would typically be handled subsequently by
separate threads.

[Aside: Note that the private data of the SRPC class consists of a
single pointer; the actual data storage space is allocated on the heap.
Clients of SRPC therefore should not feel the need to allocate
pointers to SRPC objects; they may be freely passed around without
overhead.]

A call is actually initiated by a client (the caller), which invokes
one of the SRPC constructors specifying the first parameter as
"caller".  The other parameters specify the callee by naming the
service it provides (e.g., symbolically, or as a well-known
socket/port).  The SRPC implementation verifies that the service is
known to exist (i.e., has been advertised by some server), and
initiates contact, but does not block until an underlying connection
is established.

The caller then optionally calls start_call, which allows a
specification of the "procedure" to be called and the version of the
"interface" being used.  The callee discovers these specifications by
invoking await_call.  In face, these specifications are simply integer
values whose meanings are agreed upon by the two parties.  To simplify
common cases, however, SRPC performs a few tests that serve to detect
common cases of version skew.  Here are the rules for using start_call and
await_call:

  * If the caller invokes start_call, the callee must invoke await_call.

  * If the caller omits start_call, the callee may omit await_call.
    If the callee invokes await_call, the semantics are the same as if
    the caller had invoked start_call with default values for its
    parameters.  (See below.)

  * If await_call is invoked, SRPC compares the values supplied for
    its parameters with the values supplied for the corresponding
    parameters in the invocation of start_call.  If await_call
    supplies the default value for a parameter, SRPC replaces it with
    the value given by start_call (which may or may not be the default
    value).  If await_call supplies a non-default value, it must be
    equal to the value supplied by start_call.

  * If the preceding requirements are not met, the exception "failure"
    is thrown for both caller and callee at the earliest opportunity.

These rules allow a server (the callee) to accomodate clients
(callers) that expect different versions of an interface.  Each caller
specifies particular values for the interface and/or procedure it
wants to call; the callee specifies the default value and receives the
caller's specification, which it can then act upon.  A strict server
can specify a non-default value, thereby causing SRPC to throw
"failure" for all attempted calls that don't match that value.

Once caller and callee have agreed on the procedure to be performed,
the caller begins transmitting arguments, using the "send_*"
functions, finishing with a call of send_end.  The callee executes a
corresponding sequence of "recv_*" functions, ending with a call of
"recv_end".  (The consequences of a mismatch in the sequence of "send_*" and
"recv_*" calls are discussed below.)

SRPC handles only a limited collection of argument types; there is no
general-purpose marshalling facility.  Only simple types are supported
(integers, character and byte strings), as well as homogeneous
sequences of these these simple types.

The transmission of simple types is straightforward.  The sender
invokes send_X(val), where X is a simple type, and val has type X. The
receiver invokes a correspondingly named recv_X function, receiving a
value of type X as a result.  In the case of recv_chars or recv_bytes,
the storage to hold the string is allocated dynamically; the caller is
responsible for disposing of it.

There are several ways to transmit sequences:

 1. If the sender knows the length of the sequence, it can invoke
    send_X_seq, passing an object that embodies the entire sequence.
    The receiver invokes recv_X_seq and obtains an object that embodies
    the whole sequence.  Storage management at the receiving end is
    established by the way the sequence object is created.  See the
    comments for the various constructors in X_seq classes, above.

 2. If the sender does not know the length of the sequence, it invokes
    send_seq_start, followed by a series of calls on send_X, and
    concluding with a call of send_seq_end.  The receiver invokes
    recv_seq_start, followed by a series of calls on recv_X(&got_end),
    concluding with a call of recv_seq_end.  The result parameter got_end
    informs the receiver when the end of the sequence has been seen; if
    it is true, the return value of recv_X is not meaningful.

 3. The sender and receiver may agree to the transmission of a
    heterogeneously typed values by observing the above protocol but
    varying X in the series of calls of send_X and recv_X.  Of
    course, the two ends must vary X consistently to avoid error.

Once argument transmission is complete (i.e., when the callee's call
of recv_end returns normally), the callee begins execution of the
agreed-upon procedure.  After execution is complete, the callee begins
result transmission by calling a sequence of "send_*" procedures,
ending with "send_end".  The caller executes a corresponding sequence
of "recv_*" procedures, ending with "recv_end".  If the caller's call
of "recv_end" returns normally, the remote call is complete.  (See
below for reuse of the SRPC object.)

Either side may invoke "send_failure" to cause the other side to
receive a failure notification.  The type "failure" includes an
integer code describing the failure and a character string value.
There are some conventional values for the code included in the
definition of the SRPC class; caller and callee may use other
non-conflicting values as they wish.  For the conventional values, the
accompanying string gives more details on the failure; it is intended
for human consumption.

Once a failure is thrown for any reason, the SRPC connection is no longer
useful.

The implementation doesn't guarantee that a failure will be reported
as soon as possible (which would require failure notifications to be
sent out-of-band and significantly complicate the implementation).
Instead, the clients of this interface should surround the entire
sequence of calls to send_X or recv_X in a "try ... catch (failure)".
The implementation does promise to report any prior failure no later
than the return from send_end and recv_end; that is, if these
procedures return normally, the caller can assume that the actions of
all preceding calls were acceptable to the other side of the
connection.  Notice that this implies that the caller may not discover
that a server is not responding until it has transmitted (i.e., called
send_X for) all of its arguments.

As long as neither side throws "failure", the implicit connection can
be reused for subsequent calls, beginning with the (optional) calls of
start_call and await_call.  That is, following a normal return from
send_end (by the callee) and recv_end (by the caller), a new call may
be initiated without destroying and recreating new SRPC objects.  The
underlying resources of the transport machinery are retained until the
SRPC object is destroyed.

An SRPC object is an inherently sequential facility.  Multi-threaded use
of an SRPC object by a client must be synchronized by the client.

*/

#endif  /* _SRPC */
