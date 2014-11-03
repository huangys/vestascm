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


//  ***************************************
//  *  Implementation of SRPC_impl class  *
//  ***************************************

#include <Basics.H>
#include "SRPC.H"
#include "SRPC_impl.H"

#if defined(__linux__)
// Linux lacks these typedefs.
typedef        unsigned short  in_port_t;
typedef        unsigned int    in_addr_t;
#endif

// ------------------------------------------------------------
// SRPC protocol changes:

// V1.4:

//       Replaced the pinger thread mechanism with the OS-supplied TCP
//       keep-alive timer (set by the SO_KEEPALIVE socket option).
//       This includes the removal of the call-back port from the
//       initial "hello" message (as it's no longer needed).

// ------------------------------------------------------------
const char *SRPC_VERSION = "SRPC V1.4";

const int INITIAL_WAITERS_KLUDGE = 5;

//  **********************************
//  *  Constructors and Destructors  *
//  **********************************

SRPC_impl::SRPC_impl(bool i_am_caller)
: is_caller(i_am_caller), is_sender(i_am_caller),
  version_sent(false), version_checked(false),
  state(SRPC_impl::initial),
  buffered_ic(SRPC_impl::ic_null), s((TCP_sock *)NULL),
  listener((SRPC_listener *)NULL),
  use_read_timeout(false), use_read_timeout_between_calls(false),
  read_timeout_secs(0)
{
}

SRPC_impl::~SRPC_impl() throw (SRPC::failure)
{
  if (this->listener != (SRPC_listener *)NULL) {
    if (!SRPC_listener::destroy(this->listener))
      throw_failure(SRPC::internal_trouble, "listeners list scrambled.");
  }
  if (this->s != (TCP_sock *)NULL) delete this->s;
}

//  ***********************
//  *  Failure reporting  *
//  ***********************

void SRPC_impl::throw_failure(int reason, const Text &msg, bool local_only)
  throw(SRPC::failure)
{
  if (!(local_only || this->s == (TCP_sock *)NULL)) {
    try {
      Text rmsg = "[from other end] " + msg;
      send_item_code(SRPC_impl::ic_failure);
      send_int32(reason);
      send_bytes(rmsg.chars(), rmsg.Length(), /*flush=*/ true);
    } catch (TCP_sock::failure) {
      // Well, we tried...
    }
  }
  this->state = SRPC_impl::failed;
  this->f = SRPC::failure(reason, msg);
  throw(this->f);
}

void SRPC_impl::report_TCP_failure(TCP_sock::failure &tf)
  throw (SRPC::failure)
{
  SRPC::failure f = SRPC::convert_TCP_failure(tf);
  throw_failure(f.r, f.msg, /*local_only=*/ true);
}

//  *******************
//  *  State machine  *
//  *******************

chars op_names[] = {
  "start_call",       "await_call",
  "send_int32",       "recv_int32",
  "send_chars",       "recv_chars",      "recv_chars_here", 
  "send_bytes",       "recv_bytes",      "recv_bytes_here",
  "send_text",        "recv_text",
  "send_socket",      "recv_socket",
  "send_int32_seq",   "recv_int32_seq",
  "send_chars_seq",   "recv_chars_seq",
  "send_bytes_seq",   "recv_bytes_seq",
  "send_seq_start",   "recv_seq_start", "send_seq_end",    "recv_seq_end",
  "send_end",         "recv_end",
  "send_failure",

  // Added later:
  "send_int16",       "recv_int16",
  "send_int16_array", "recv_int16_array",
  "send_int64",	      "recv_int64",

  "send_int32_array", "recv_int32_array",
  "send_int64_array", "recv_int64_array",

  "send_bool",        "recv_bool"
};

chars state_names[] = {
  "initial", "ready", "data_out", "seq_out", "data_in", "seq_in", "failed"
};

enum {
  rs_pos = 0,
  recver_pos = 0, sender_pos = 1,
  cc_pos = 2,
  callee_pos = 2, caller_pos = 3,
  state0_pos = 4
};

#define M(x) (1<<(state0_pos+SRPC_impl::x))
#define ANY_STATE (-1)
#define IS_SENDER (1<<sender_pos)
#define IS_RECVER (1<<recver_pos)
#define IS_CALLER (1<<caller_pos)
#define IS_CALLEE (1<<callee_pos)
#define EITHER_END (IS_CALLER | IS_CALLEE)
#define FRESH_SENDER    EITHER_END | IS_SENDER | M(initial) | M(ready)
#define FRESH_RECVER    EITHER_END | IS_RECVER | M(initial) | M(ready)

int legal_states[] = {
  /* start_call       */ IS_CALLER | IS_SENDER | M(initial),
  /* await_call       */ IS_CALLEE | IS_RECVER | M(initial),
  /* send_int32       */ FRESH_SENDER | M(data_out) | M(seq_out),
  /* recv_int32       */ FRESH_RECVER | M(data_in)  | M(seq_in),
  /* send_chars       */ FRESH_SENDER | M(data_out) | M(seq_out),
  /* recv_chars       */ FRESH_RECVER | M(data_in)  | M(seq_in),
  /* recv_chars_here  */ FRESH_RECVER | M(data_in)  | M(seq_in),
  /* send_bytes       */ FRESH_SENDER | M(data_out) | M(seq_out),
  /* recv_bytes       */ FRESH_RECVER | M(data_in)  | M(seq_in),
  /* recv_bytes_here  */ FRESH_RECVER | M(data_in)  | M(seq_in),
  /* send_text        */ FRESH_SENDER | M(data_out) | M(seq_out),
  /* recv_text        */ FRESH_RECVER | M(data_in)  | M(seq_in),
  /* send_socket      */ FRESH_SENDER | M(data_out) | M(seq_out),
  /* recv_socket      */ FRESH_RECVER | M(data_in)  | M(seq_in),
  /* send_int32_seq   */ FRESH_SENDER | M(data_out) | M(seq_out),
  /* recv_int32_seq   */ FRESH_RECVER | M(data_in)  | M(seq_in),
  /* send_chars_seq   */ FRESH_SENDER | M(data_out) | M(seq_out),
  /* recv_chars_seq   */ FRESH_RECVER | M(data_in)  | M(seq_in),
  /* send_bytes_seq   */ FRESH_SENDER | M(data_out) | M(seq_out),
  /* recv_bytes_seq   */ FRESH_RECVER | M(data_in)  | M(seq_in),
  /* send_seq_start   */ FRESH_SENDER | M(data_out),
  /* recv_seq_start   */ FRESH_RECVER | M(data_in),
  /* send_seq_end     */ EITHER_END | IS_SENDER | M(seq_out),
  /* recv_seq_end     */ EITHER_END | IS_RECVER | M(seq_in),
  /* send_end         */ FRESH_SENDER | M(data_out),
  /* recv_end         */ FRESH_RECVER | M(data_in),
  /* send_failure     */ ANY_STATE,

  // Added later:
  /* send_int16       */ FRESH_SENDER | M(data_out) | M(seq_out),
  /* recv_int16       */ FRESH_RECVER | M(data_in)  | M(seq_in),
  /* send_int16_array */ FRESH_SENDER | M(data_out) | M(seq_out),
  /* recv_int16_array */ FRESH_RECVER | M(data_in)  | M(seq_in),
  /* send_int64       */ FRESH_SENDER | M(data_out) | M(seq_out),
  /* recv_int64       */ FRESH_RECVER | M(data_in)  | M(seq_in),
  /* send_int32_array */ FRESH_SENDER | M(data_out) | M(seq_out),
  /* recv_int32_array */ FRESH_RECVER | M(data_in)  | M(seq_in),
  /* send_int64_array */ FRESH_SENDER | M(data_out) | M(seq_out),
  /* recv_int64_array */ FRESH_RECVER | M(data_in)  | M(seq_in),

  /* send_bool */        FRESH_SENDER | M(data_out) | M(seq_out),
  /* recv_bool */        FRESH_RECVER | M(data_in)  | M(seq_in)
};

#undef M
#undef ANY_STATE
#undef IS_SENDER
#undef IS_RECVER
#undef IS_CALLER
#undef IS_CALLEE
#undef EITHER_END
#undef FRESH_SENDER
#undef FRESH_RECVER

bool SRPC_impl::state_ok_for(int op) throw ()
{
  int m =
    (1<<(this->is_sender+rs_pos)) | (1<<(this->is_caller+cc_pos)) |
    (1<<(this->state+state0_pos));
  return ((m & legal_states[op]) == m);
}

void SRPC_impl::check_state(int op) throw (SRPC::failure)
{
  if (!this->state_ok_for(op)) {
    throw_failure(SRPC::protocol_violation,
      Text(op_names[op]) + " isn't legal now (in state '" +
      state_names[this->state] + "')");
  }
}

void SRPC_impl::ensure_sending_state() throw ()
{
  switch (this->state) {
  case SRPC_impl::data_out:
  case SRPC_impl::seq_out:
    break;
  default:
    this->state = SRPC_impl::data_out;
  }
}

void SRPC_impl::ensure_recving_state() throw ()
{
  switch (this->state) {
  case SRPC_impl::data_in:
  case SRPC_impl::seq_in:
    break;
  default:
    this->state = SRPC_impl::data_in;
  }
}

//  ********************************
//  *  Byte-sequence transmission  *
//  ********************************

void SRPC_impl::send_bytes(const char *bs, int len, bool flush)
  throw(TCP_sock::failure)
{
  send_int32(len);
  s->send_data(bs, len, flush);
}

void SRPC_impl::recv_bytes(char *bs, int len)
  throw(TCP_sock::failure, SRPC::failure)
{
  // Set or clear read timeout before trying to read data from the
  // peer.  Enable the read timeout if it's turned on and we're either
  // not in the initial state (i.e. between calls) or we're supposed
  // to use it between calls.
  if(this->use_read_timeout &&
     ((!this->state == SRPC_impl::initial) ||
      this->use_read_timeout_between_calls))
    {
      this->s->enable_read_timeout(this->read_timeout_secs);
    }
  else
    {
      this->s->disable_read_timeout();
    }

  int curr = 0;
  while (len > 0) {
    int bytes_read;
    bytes_read = this->s->recv_data(&bs[curr], len);
    curr += bytes_read;
    len -= bytes_read;
  }
}

//  **************************
//  *  Integer transmission  *
//  **************************

void SRPC_impl::send_int16(Basics::int16 i, bool flush)
  throw (TCP_sock::failure)
{
  i = Basics::hton16(i);
  this->s->send_data((const char *) &i, sizeof(i), flush);
}
  
Basics::int16 SRPC_impl::recv_int16()
  throw (TCP_sock::failure)
{
  Basics::int16 x;
  recv_bytes((char *) &x, sizeof(x));
  return Basics::ntoh16(x);
}

void SRPC_impl::send_int32(Basics::int32 i, bool flush)
  throw (TCP_sock::failure)
{
  i = Basics::hton32(i);
  this->s->send_data((const char *) &i, sizeof(i), flush);
}
  
Basics::int32 SRPC_impl::recv_int32()
  throw (TCP_sock::failure)
{
  Basics::int32 x;
  recv_bytes((char *) &x, sizeof(x));
  return Basics::ntoh32(x);
}

void SRPC_impl::send_int64(Basics::int64 i, bool flush)
  throw (TCP_sock::failure)
{
  i = Basics::hton64(i);
  this->s->send_data((const char *) &i, sizeof(i), flush);
}
  
Basics::int64 SRPC_impl::recv_int64()
  throw (TCP_sock::failure)
{
  Basics::int64 x;
  recv_bytes((char *) &x, sizeof(x));
  return Basics::ntoh64(x);
}

//  ****************************
//  *  Item code transmission  *
//  ****************************

void SRPC_impl::send_item_code(item_code ic, bool flush)
  throw (TCP_sock::failure)
{
  char c;
  if (!(this->version_sent)) {
    c = SRPC_impl::ic_hello;
    this->s->send_data(&c, 1);
    send_bytes(SRPC_VERSION, strlen(SRPC_VERSION)+1);
    this->version_sent = true;
  }
  c = ic;
  this->s->send_data(&c, 1, flush);
}

item_code SRPC_impl::read_item_code()
  throw (TCP_sock::failure, SRPC::failure)
{
  char c;
  item_code ic;

  // Make sure there's a connection to the other end.  This is the point at
  // which a callee thread will block waiting for a caller.
  if (this->s == (TCP_sock *)NULL && !(this->is_caller)) {
    this->s = this->listener->sock->wait_for_connect();
  }

  // Read an item code.

  if (this->buffered_ic == SRPC_impl::ic_null) {
    this->recv_bytes(&c, 1);
    ic = (item_code) (unsigned char) c;
  } else {
    ic = this->buffered_ic;
    this->buffered_ic = SRPC_impl::ic_null;
  }

  if (!(this->version_checked)) {
    if (ic != SRPC_impl::ic_hello) {
      this->throw_failure(SRPC::protocol_violation,
	"Other end didn't say hello.", true);
    }
    // Read other end's version identification
    int len = this->recv_int32();
    char *ver = NEW_PTRFREE_ARRAY(char, len+1);
    this->recv_bytes(ver, len);
    ver[len] = '\0';

    if (strcmp(ver, SRPC_VERSION) != 0) {
      delete[] ver;
      this->throw_failure(SRPC::protocol_violation,
	"Client/server SRPC version mismatch", true);
    }
    delete[] ver;

    // Protocol versions match.  We can communicate.
    // We can now accept incoming items.
    this->version_checked = true;

    // Read an item code.
    this->recv_bytes(&c, 1);
    ic = (item_code) (unsigned char) c;
  }

  if (ic == SRPC_impl::ic_failure) {
    // Other side reports a failure.  Throw it.
    int code = this->recv_int32();
    int len = this->recv_int32();
    char *str = NEW_PTRFREE_ARRAY(char, len);
    this->recv_bytes(str, len);
    Text msg(str, len);
    delete[] str;
    this->throw_failure(code, msg, /*local_only=*/ true);
  }

  return ic;
}

void SRPC_impl::recv_item_code(item_code expected, bool *got_end)
  throw (TCP_sock::failure, SRPC::failure)
{
  item_code ic = this->read_item_code();

  if (got_end != (bool *)NULL) *got_end = false;
  if (expected == ic) return;
  if (this->state == SRPC_impl::seq_in && ic == SRPC_impl::ic_seq_end
      && got_end != (bool *)NULL) {
    // Put item code back and report to caller
    this->buffered_ic = ic;
    *got_end = true;
    return;
  }

  char msg[50];
  sprintf(msg, "Mix-up: expected item code %d, got %d", expected, ic);
  this->throw_failure(SRPC::protocol_violation, msg);
}

item_code SRPC_impl::recv_item_code(item_code expected1, item_code expected2,
				    bool *got_end)
  throw (TCP_sock::failure, SRPC::failure)
{
  item_code ic = this->read_item_code();

  if (got_end != (bool *)NULL) *got_end = false;
  if ((expected1 == ic) || (expected2 == ic)) return ic;
  if (this->state == SRPC_impl::seq_in && ic == SRPC_impl::ic_seq_end
      && got_end != (bool *)NULL) {
    // Put item code back and report to caller
    this->buffered_ic = ic;
    *got_end = true;
    return SRPC_impl::ic_seq_end;
  }

  char msg[60];
  sprintf(msg, "Mix-up: expected item code %d or %d, got %d",
	  expected1, expected2, ic);
  this->throw_failure(SRPC::protocol_violation, msg);

  // Unreachable, but the compiler doesn't know that.
  return SRPC_impl::ic_null;
}

// ====================================================================

//  ****************************************
//  *  SRPC_listener class implementation  *
//  ****************************************

//  **********************
//  *  Static variables  *
//  **********************

Basics::mutex SRPC_listener::m;         // initialized by constructor
SRPC_listener *SRPC_listener::listeners = NULL;

//  **********************
//  *  Member functions  *
//  **********************

/* static */
SRPC_listener *SRPC_listener::create(u_short port, TCP_sock *ls)
  throw (TCP_sock::failure)
{
  SRPC_listener *s;
  SRPC_listener::m.lock();
  try {
    s = SRPC_listener::listeners;
    while (s != (SRPC_listener *)NULL) {
      if (s->port == port) break;
      s = s->next;
    }
    if (s == (SRPC_listener *)NULL) {
      s = NEW(SRPC_listener);
      s->next = SRPC_listener::listeners;
      if (s->my_sock = (ls == (TCP_sock *)NULL)) {
	s->sock = NEW_CONSTR(TCP_sock, (port));
	s->sock->set_waiters(INITIAL_WAITERS_KLUDGE);
	// Enable the TCP keep-alive timer now, so that it gets
	// inherited by accepted connections.
	s->sock->enable_keepalive();
      }
      else s->sock = ls;
      s->port = port;
      s->n = 0;
      SRPC_listener::listeners = s;
    }
    s->n++;
  } catch (...) { SRPC_listener::m.unlock(); throw; }
  SRPC_listener::m.unlock();
  return s;
}

/* static */
bool SRPC_listener::destroy(SRPC_listener *listener)
  throw (TCP_sock::failure)
{
  bool result = true;
  SRPC_listener::m.lock();
  try {
    if (--(listener->n) == 0) {
      // Remove listener from list
      SRPC_listener *l = SRPC_listener::listeners;
      SRPC_listener *prev = (SRPC_listener *)NULL;
      while (result = (l != (SRPC_listener *)NULL)) {
  	if (l->sock == listener->sock) {
	  if (prev == (SRPC_listener *)NULL) {
  	    SRPC_listener::listeners = l->next;
  	  } else {
  	    prev->next = l->next;
  	  }
	  if (listener->my_sock) {
	    // destroy TCP socket
	    delete listener->sock;
  	  }
	  delete listener;
	  break;
  	}
  	prev = l;
  	l = l->next;
      }
    }
  } catch (...) { SRPC_listener::m.unlock(); throw; }
  SRPC_listener::m.unlock();
  return result;
}

// ======================================================================

/* Socket usage in SRPC and SRPC_impl:
   ------------------------------------

   A connected pair of TCP_sock objects are used for the transmission
   of arguments and results between the caller and callee.

   The connection is established by having the callee (the server)
   create a listener socket and wait for a connection to be made by a
   caller (the client).  The listener socket corresponds to an
   instance of RPC interface being "exported" by the server, and many
   threads may be listening on the socket.  Accordingly, the
   implementation maintains a list of listener sockets; when a callee
   is constructed (i.e., SRPC(callee) is invoked), the appropriate
   socket is found on this list (or inserted, if one didn't previously
   exist).  However, the callee doesn't actually wait for a connection
   until await_call or recv_X is invoked, at which time
   TCP_sock::wait_for_connection is used to obtain a unique socket for
   the connection.  This wait is an indefinite one, unlike most of the
   other waits in SRPC.

   The caller side of the main connection is more straightforward.  As
   part of the SRPC(caller) constructor invocation, a connection is
   attempted to a specified server.  The SRPC machinery does not
   explicitly time out this connection attempt, but the underlying OS
   socket machinery does.

   When the caller begins sending arguments on the main connection,
   the SRPC implementation prefixes a synchronizing message containing
   a version identifier for the SRPC wire protocol.  The callee is
   expected to read the version message and, if the version is
   acceptable, proceed with reading the first call.  (If the version
   is unacceptable, an exception is thrown and sent to the peer, which
   normally results in tremination of the connection.)

   Many calls using the underlying TCP_sock can block for an unbounded
   amount of time.  To protect against indefinitely tying up system
   resources in the event of a peer system crashing, rebooting, or
   becoming unreachable, the TCP keep-alive mechanism is turned on for
   both the callee and caller ends.
*/
