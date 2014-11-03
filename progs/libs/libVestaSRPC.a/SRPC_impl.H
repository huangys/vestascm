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

#include <Basics.H>  // for threads

//  **********************************
//  *  Implementation of class SRPC  *
//  **********************************

class SRPC_listener;    // forward declaration

class SRPC_impl {
  friend class SRPC;
public:
  //  ***************************
  //  *  Data type definitions  *
  //  ***************************

  //  SRPC wire protocol:
  //  -------------------
  //
  //  Each direction on the wire is treated as a reliable stream of
  //  self-describing entities ("items").  Each item begins with a
  //  single byte code and may be followed by zero or more integers or
  //  byte strings.  The item_code specifies what integers/strings, if
  //  any, follow it in the stream.

  //  An integer is represented as a binary, 4-byte value in network
  //  byte order.  A byte string is represented by a binary,
  //  non-negative, 4-byte value (call it L) specifying the length of
  //  the string, followed by a sequence of L bytes that forms the
  //  body of the string.  Thus, a byte string may contain any byte values,
  //  including the null character.

  //  The following table defines the item codes used by SRPC.  The
  //  comments indicate what numbers or byte sequences, if any, are
  //  sent.

  enum item_code {
    ic_null =           0,        // internal use only
    ic_hello =         10,        // (proto_version: bs)
    ic_start_call =    11,        // (proc: int32, intf: int32)
    ic_end_call =      12,        // (and_close: int)
    ic_end_ack =       13,        // (and_close: int)
    ic_seq_start =     20,        // (n: int32, bytes: int32)
    ic_seq_end =       21,        // ()
    ic_int16 =         28,        // (i: int16)
    ic_int16_array =   29,        // (n: int32, vals[0..n): int16)
    ic_int32 =         30,        // (i: int32)
    ic_int32_seq =     31,        // (n: int, values[0..n): int)
    ic_chars =         32,        // (body: bs)
    ic_chars_seq =     33,        // (n: int32, bodies: bs, offs[0..n): int32)
    ic_bytes =         34,        // (body: bs)
    ic_bytes_seq =     35,        // (n: int32, bodies: bs, lens[0..n): int32)
    ic_text =          36,        // (body: bs)
    ic_socket =        40,        // (ip_addr_and_port: bs)

    // Added later:
    ic_int64 =         41,        // (i: int64)
    ic_int32_array =   42,        // (n: int32, vals[0..n): int32)
    ic_int64_array =   43,        // (n: int32, vals[0..n): int64)

    ic_bool_true     = 44,        // Boolean piggy-backed in the item code
    ic_bool_false    = 45,

    ic_failure =      255         // (code: int32, msg: bs)
  };

  // Definitions for state machine

  enum rpc_state {
    initial = 0,                    // start of call (includes reused conn.)
    ready = 1,                      // start of directional transmission
    data_out = 2,                   // sending top-level args or results
    seq_out = 3,                    // sending a sequence, arg or result
    data_in = 4,                    // recving top-level args or results
    seq_in = 5,                     // recving a sequence, arg or result
    failed = 6
  };

  enum ops {
    op_start_call      	=  0,
    op_await_call      	=  1,
    op_send_int32      	=  2,
    op_recv_int32      	=  3,
    op_send_chars      	=  4,
    op_recv_chars      	=  5,
    op_recv_chars_here 	=  6,
    op_send_bytes      	=  7,
    op_recv_bytes      	=  8,
    op_recv_bytes_here 	=  9,
    op_send_text       	= 10,
    op_recv_text       	= 11,
    op_send_socket     	= 12,
    op_recv_socket     	= 13,
    op_send_int32_seq  	= 14,
    op_recv_int32_seq  	= 15,
    op_send_chars_seq  	= 16,
    op_recv_chars_seq  	= 17,
    op_send_bytes_seq  	= 18,
    op_recv_bytes_seq  	= 19,
    op_send_seq_start  	= 20,
    op_recv_seq_start  	= 21,
    op_send_seq_end    	= 22,
    op_recv_seq_end    	= 23,
    op_send_end        	= 24,
    op_recv_end        	= 25,
    op_send_failure    	= 26,

    // Added later:
    op_send_int16      	= 27,
    op_recv_int16      	= 28,
    op_send_int16_array	= 29,
    op_recv_int16_array	= 30,

    // Operations for specific bit sizes of integers
    op_send_int64      	= 31,
    op_recv_int64      	= 32,

    // Arrays of larger integers
    op_send_int32_array	= 33,
    op_recv_int32_array	= 34,
    op_send_int64_array	= 35,
    op_recv_int64_array	= 36,

    // Booleans
    op_send_bool        = 37,
    op_recv_bool        = 38,

  };


  //  **********************
  //  *  Member functions  *
  //  **********************

  // Constructors and Destructors

  SRPC_impl(bool i_am_caller);
  ~SRPC_impl() throw (SRPC::failure);

  // Item code transmission

  void send_item_code(item_code ic, bool flush = false)
    throw (TCP_sock::failure);
  item_code read_item_code()
    throw (TCP_sock::failure, SRPC::failure);
    // Reads item code from stream, but doesn't check it against
    // any expectation of the caller.
  void recv_item_code(item_code expected, bool *got_end = NULL)
    throw (TCP_sock::failure, SRPC::failure);
    // If the item_code read from the stream matches "expected", the
    // procedure returns normally.  Otherwise, if the received
    // item_code is "ic_seq_end" and "got_end" is non-NULL, set
    // "got_end" to true.  Otherwise, throw failure.
  item_code recv_item_code(item_code expected1, item_code expected2,
			   bool *got_end)
    throw (TCP_sock::failure, SRPC::failure);
    // Like recv_item_code, but either expected1 or expected2 is
    // allowed.  If either of these is read, it is returned.  If the
    // received item_code is "ic_seq_end" and "got_end" is non-NULL,
    // set "got_end" to true and return "ic_seq_end".

  // Integer transmission: send and recieve integers of different
  // sizes, dealing with network byte ordering.

  void send_int16(Basics::int16 i, bool flush = false)
    throw (TCP_sock::failure);
  Basics::int16 recv_int16() throw (TCP_sock::failure);

  void send_int32(Basics::int32 i, bool flush = false)
    throw (TCP_sock::failure);
  Basics::int32 recv_int32() throw (TCP_sock::failure);

  void send_int64(Basics::int64 i, bool flush = false)
    throw (TCP_sock::failure);
  Basics::int64 recv_int64() throw (TCP_sock::failure);

  // Byte-sequence transmission

  void send_bytes(const char *bs, int len, bool flush = false)
    throw (TCP_sock::failure);
  void recv_bytes(char *bs, int len)
    throw (TCP_sock::failure, SRPC::failure);
    // Reads exactly "len" bytes from the underlying stream, waiting as
    // necessary.

  // State machine

  bool state_ok_for(int op) throw ();
  void check_state(int op) throw (SRPC::failure);
  void ensure_sending_state() throw ();
  void ensure_recving_state() throw ();

  // Failure reporting

  void throw_failure(int reason, const Text &msg, bool local_only = false)
    throw (SRPC::failure);
  void report_TCP_failure(TCP_sock::failure &tf)
    throw (SRPC::failure);

  //  *****************
  //  *  Member data  *
  //  *****************

  rpc_state state;
  SRPC::failure f;                  // set if state == failed

  bool is_caller;                   // true <=> this is the caller
  bool is_sender;                   // true <=> this end is allowed to send
  bool version_sent;                // true <=> "hello" sent
  bool version_checked;             // true <=> "hello" matches

  item_code buffered_ic;

  TCP_sock *s;                       // local socket
  SRPC_listener *listener;           // (for callee only)

  bool use_read_timeout;
  bool use_read_timeout_between_calls;
  unsigned int read_timeout_secs;
};

//  *************************
//  *  SRPC_listener class  *
//  *************************

class SRPC_listener {
public:
  TCP_sock *sock;                 // TCP_sock listener
  bool my_sock;                   // true <=> we created sock
  u_short port;                   // port we're listening on

  // Static member functions
  static SRPC_listener *create(u_short port, TCP_sock *ls = NULL)
    throw (TCP_sock::failure);

  static bool destroy(SRPC_listener *listener)
    throw (TCP_sock::failure);

private:
  SRPC_listener *next;
  int n;                          // reference count

  // static member data
  static Basics::mutex m;          // mutex for:
  static SRPC_listener *listeners; // list of known listeners
};

//  Convenience declarations

typedef SRPC_impl::item_code item_code;
typedef char *chars;
