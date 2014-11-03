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


//  *************************
//  *  SRPC implementation  *
//  *************************

#include <Basics.H>
#include <VestaConfig.H>

#include "SRPC.H"
#include "SRPC_impl.H"
#include "chars_seq_private.H"
#include "int_seq_private.H"

//  *************************************
//  *  Connection setup (constructors)  *
//  *************************************

SRPC::SRPC(SRPC::which_end me, const Text &interface, const Text &hostname)
  throw (SRPC::failure)
{
  try {
    p = NEW_CONSTR(SRPC_impl, (me == caller));
  } catch (TCP_sock::failure f) { throw(convert_TCP_failure(f)); }

  try {
    try {
      if (p->is_caller) {
	if (hostname.Empty())
	  p->throw_failure(not_implemented,
			   "hostname may not be defaulted", true);
	p->s = NEW(TCP_sock);
	p->s->connect_to(hostname, interface);
	// Enable the TCP keep-alive timer
	p->s->enable_keepalive();
      } else {
	// For callee, create a listener and defer socket acquisition
	// and connection until first recv operation.
	u_short port = TCP_sock::parse_port(interface);
	p->listener = SRPC_listener::create(port);
      }
    } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }
  } catch (SRPC::failure f) { delete p; throw(f); }
}

SRPC::SRPC(SRPC::which_end me, TCP_sock *sock) throw (SRPC::failure)
{
  try {
    p = NEW_CONSTR(SRPC_impl, (me == caller));
  } catch (TCP_sock::failure f) { throw(convert_TCP_failure(f)); }

  try {
    try {
      if (p->is_caller)
	p->s = sock;
      else {
	// For callee, create a listener and defer socket acquisition
	// and connection until first recv operation.
	sockaddr_in addr;
	sock->get_local_addr(addr);
	p->listener = SRPC_listener::create(addr.sin_port, sock);
      }
    } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }
  } catch (SRPC::failure f) { delete p; throw(f); }
}

SRPC::SRPC(TCP_sock *sock) throw (SRPC::failure)
{
  try {
    p = NEW_CONSTR(SRPC_impl, (false));
    p->s = sock;
  } catch (TCP_sock::failure f) { throw(convert_TCP_failure(f)); }
}

SRPC::~SRPC() throw(SRPC::failure)
{
  if (!(p->state == SRPC_impl::initial || p->state == SRPC_impl::failed)) {
    try {
      send_failure(partner_went_away, "Partner destroyed its SRPC object");
    } catch (...) { /*SKIP*/ }
  }
  delete p;
}

Text SRPC::this_host() throw(SRPC::failure)
{
  try {
    return TCP_sock::this_host();
  } catch (TCP_sock::failure f) { throw(convert_TCP_failure(f)); }

}

void SRPC::split_name(const Text &host_and_port, Text &host, Text &port)
{
  int i = host_and_port.FindCharR(':');
  if (i < 0) { host = host_and_port; port = ""; }
  else { host = host_and_port.Sub(0, i); port = host_and_port.Sub(i+1); }
}


//  ****************************
//  *  Connection information  *
//  ****************************

bool SRPC::previous_failure(SRPC::failure *f) throw()
{
  if (p->state != SRPC_impl::failed) return false;
  if (f != NULL) *f = p->f;
  return true;
}

bool SRPC::alive(SRPC::failure *f) throw()
{
  if (previous_failure(f)) return false;
  try {
    return socket()->alive();
  } catch (TCP_sock::failure why) {
    if (f != NULL) *f = SRPC::convert_TCP_failure(why);
    return false;
  };
}

TCP_sock *SRPC::socket() throw(SRPC::failure) {
  return p->s;
}

Text SRPC::local_socket() throw(SRPC::failure)
{
  TCP_sock *sock = socket();
  sockaddr_in me;
  Text t;
  char a[15+1+5+1];
  if (sock == NULL) return "";
  try {
    sock->get_local_addr(me);
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }
  t = inet_ntoa_r(me.sin_addr);
  sprintf(a, "%s:%d", t.cchars(), ntohs(me.sin_port));
  return Text(a);
}

Text SRPC::remote_socket() throw(SRPC::failure)
{
  TCP_sock *sock = socket();
  sockaddr_in him;
  Text t;
  char a[15+1+5+1];
  if (sock == NULL) return "";
  try {
    sock->get_remote_addr(him);
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }
  t = inet_ntoa_r(him.sin_addr);
  sprintf(a, "%s:%d", t.cchars(), ntohs(him.sin_port));
  return Text(a);
}

bool SRPC::start_call_ok() throw()
{
  return p->state_ok_for(SRPC_impl::op_start_call);
}

//  *******************
//  *  Configuration  *
//  *******************

void SRPC::enable_read_timeout(unsigned int seconds,
			       bool use_between_calls)
  throw ()
{
  p->use_read_timeout = true;
  p->use_read_timeout_between_calls = use_between_calls;
  p->read_timeout_secs = seconds;
}

void SRPC::disable_read_timeout() throw ()
{
  p->use_read_timeout = false;
}

bool SRPC::get_read_timeout(/*OUT*/ unsigned int &seconds,
			    /*OUT*/ bool &use_between_calls) throw()
{
  if(p->use_read_timeout)
    {
      seconds = p->read_timeout_secs;
      use_between_calls = p->use_read_timeout_between_calls;
    }
  return p->use_read_timeout;
}

//  *********************
//  *  Call initiation  *
//  *********************

void SRPC::start_call(int proc_id, int intf_version) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_start_call);

  try {
    p->send_item_code(SRPC_impl::ic_start_call);
    p->send_int32(proc_id);
    p->send_int32(intf_version);
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->state = SRPC_impl::ready;
}

void SRPC::await_call(int &proc_id, int &intf_version) throw(SRPC::failure)
{
  int remote_proc_id;
  int remote_intf_version;

  p->check_state(SRPC_impl::op_await_call);

  try {
    item_code ic;
    ic = p->read_item_code();
    if (ic != SRPC_impl::ic_start_call) {
      // Assume start_call was defaulted.
      p->buffered_ic = ic;                   // put back for next time
      remote_proc_id = default_proc_id;
      remote_intf_version = default_intf_version;
    } else {
      remote_proc_id = p->recv_int32();
      remote_intf_version = p->recv_int32();
    }
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  // Check for acceptable intf_version
  if (intf_version == default_intf_version)
    intf_version = remote_intf_version;
  else if (intf_version != remote_intf_version)
    p->throw_failure(version_skew,
		     "start_call/await_call disagree on intf_version");

  // Check for acceptable proc_id
  if (proc_id == default_proc_id)
    proc_id = remote_proc_id;
  else if (proc_id != remote_proc_id)
    p->throw_failure(version_skew,
		     "start_call/await_call disagree on proc_id");

  p->state = SRPC_impl::ready;
}


//  **************************
//  *  Integer transmission  *
//  **************************

void SRPC::send_int32(int i) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_int32);

  try {
    p->send_item_code(SRPC_impl::ic_int32);
    p->send_int32(i);
  } catch(TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_sending_state();
}

int SRPC::recv_int32(bool *got_end) throw(SRPC::failure)
{
  int result;
  p->check_state(SRPC_impl::op_recv_int32);

  try {
    p->recv_item_code(SRPC_impl::ic_int32, got_end);
    result = (got_end != NULL && *got_end) ? 0 : p->recv_int32();
  } catch(TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();

  return result;
}

void SRPC::send_int16(Basics::int16 i) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_int16);

  try {
    p->send_item_code(SRPC_impl::ic_int16);
    p->send_int16(i);
  } catch(TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_sending_state();
}

Basics::int16 SRPC::recv_int16(bool *got_end) throw(SRPC::failure)
{
  Basics::int16 result;
  p->check_state(SRPC_impl::op_recv_int16);

  try {
    p->recv_item_code(SRPC_impl::ic_int16, got_end);
    result = (got_end != NULL && *got_end) ? 0 : p->recv_int16();
  } catch(TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();

  return result;
}

void SRPC::send_int64(Basics::int64 i) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_int64);

  try {
    p->send_item_code(SRPC_impl::ic_int64);
    p->send_int64(i);
  } catch(TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_sending_state();
}

Basics::int64 SRPC::recv_int64(bool *got_end) throw(SRPC::failure)
{
  Basics::int64 result;
  p->check_state(SRPC_impl::op_recv_int64);

  try {
    p->recv_item_code(SRPC_impl::ic_int64, got_end);
    result = (got_end != NULL && *got_end) ? 0 : p->recv_int64();
  } catch(TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();

  return result;
}

void SRPC::send_bool(bool b) throw (SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_bool);

  try {
    p->send_item_code(b ? SRPC_impl::ic_bool_true : SRPC_impl::ic_bool_false);
  } catch(TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_sending_state();
}

bool SRPC::recv_bool(bool *got_end) throw(SRPC::failure)
{
  bool result = false;
  p->check_state(SRPC_impl::op_recv_bool);

  try {
    SRPC_impl::item_code ic = p->recv_item_code(SRPC_impl::ic_bool_true,
						SRPC_impl::ic_bool_false,
						got_end);
    if(got_end != NULL && *got_end)
      {
	result = false;
      }
    else if(ic == SRPC_impl::ic_bool_true)
      {
	result = true;
      }
    else if(ic == SRPC_impl::ic_bool_false)
      {
	result = false;
      }
  } catch(TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();

  return result;
}

//  ***********************************
//  *  Character string transmission  *
//  ***********************************

void SRPC::send_chars(const char *s) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_chars);

  try {
    p->send_item_code(SRPC_impl::ic_chars);
    p->send_bytes(s, s == NULL ? 0 : strlen(s));
  } catch(TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_sending_state();
}

char *SRPC::recv_chars(bool *got_end) throw(SRPC::failure)
{
  int len;
  char *str;

  p->check_state(SRPC_impl::op_recv_chars);

  try {
    p->recv_item_code(SRPC_impl::ic_chars, got_end);
    if (got_end != NULL && *got_end) str = NULL;
    else {
      len = p->recv_int32();

      if (len < 0)
	p->throw_failure(internal_trouble, "Malformed string length");

      str = NEW_PTRFREE_ARRAY(char, len+1);
      p->recv_bytes(str, len);
      str[len] = '\0';
    }

  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();
  return str;
}

char *SRPC::recv_chars(char *buff, int buff_len, bool *got_end)
  throw(SRPC::failure)
{
  int len;
  char *str;

  p->check_state(SRPC_impl::op_recv_chars);

  try {
    p->recv_item_code(SRPC_impl::ic_chars, got_end);
    if (got_end != NULL && *got_end) str = NULL;
    else {
      len = p->recv_int32();

      if (len < 0)
	p->throw_failure(internal_trouble, "Malformed string length");

      if (len < buff_len)
	str = buff;            // fast path, use client-supplied buffer
      else
	// slow path, allocate buffer on heap
	str = NEW_PTRFREE_ARRAY(char, len+1);
      p->recv_bytes(str, len);
      str[len] = '\0';
    }

  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();
  return str;
}

void SRPC::recv_chars_here(char *buffer, int &len, bool *got_end)
    throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_recv_chars_here);

  try {
    int incoming_len;

    p->recv_item_code(SRPC_impl::ic_chars, got_end);
    if (got_end != NULL && *got_end) len = 0;
    else {
      incoming_len = p->recv_int32();
      if (incoming_len + 1 > len)
	p->throw_failure(buffer_too_small, "Buffer too small");

      p->recv_bytes(buffer, incoming_len);
      buffer[incoming_len] = '\0';
      len = incoming_len;
    }
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();
}

//  ***********************
//  *  Text transmission  *
//  ***********************

void SRPC::send_Text(const Text &t) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_text);

  try {
    p->send_item_code(SRPC_impl::ic_text);
    char *s = t.chars();              // gets underlying pointer
    p->send_bytes(s, strlen(s));
  } catch(TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_sending_state();
}

void SRPC::recv_Text(/*OUT*/ Text &t, bool *got_end) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_recv_text);

  try {
    p->recv_item_code(SRPC_impl::ic_text, got_end);
    if (got_end != NULL && *got_end) {
      t.Init("");
    } else {
      int len = p->recv_int32();
      if (len < 0) p->throw_failure(internal_trouble, "Malformed Text length");
      char *str = NEW_PTRFREE_ARRAY(char, len+1);
      p->recv_bytes(str, len);
      str[len] = '\0';
      t.Init(str);
    }

  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();
}

//  ********************************
//  *  Byte-sequence transmission  *
//  ********************************

void SRPC::send_bytes(const char *buffer, int len) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_bytes);

  try {
    p->send_item_code(SRPC_impl::ic_bytes);
    p->send_bytes(buffer, len);
  } catch(TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_sending_state();
}

char *SRPC::recv_bytes(int &len, bool *got_end) throw(SRPC::failure)
{
  char *buffer;

  p->check_state(SRPC_impl::op_recv_chars);

  try {
    p->recv_item_code(SRPC_impl::ic_bytes, got_end);
    if (got_end != NULL && *got_end) buffer = NULL;
    else {
      len = p->recv_int32();

      if (len < 0)
	p->throw_failure(internal_trouble, "Malformed string length");

      buffer = NEW_PTRFREE_ARRAY(char, len);
      p->recv_bytes(buffer, len);
    }
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();
  return buffer;
}

void SRPC::recv_bytes_here(char *buffer, int &len, bool *got_end)
    throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_recv_bytes_here);

  try {
    int incoming_len;

    p->recv_item_code(SRPC_impl::ic_bytes, got_end);
    if (got_end != NULL && *got_end) len = 0;
    else {
      incoming_len = p->recv_int32();
      if (incoming_len > len)
	p->throw_failure(buffer_too_small, "Buffer too small");

      p->recv_bytes(buffer, incoming_len);
      len = incoming_len;
    }
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();
}

//  *********************************
//  *  Socket address transmission  *
//  *********************************

// Care is required here to make sure that a machine-independent
// representation of socket addresses is used.  The relevant fields of
// a socket address are already in network byte order, and their lengths
// are machine-independent (4 bytes for IP address, 2 for port).  Therefore,
// it suffices to assemble them in a single byte array without any
// other change of representation.

static sockaddr_in dummy_sock;
const int bytes_for_socket =
            sizeof(dummy_sock.sin_addr)+sizeof(dummy_sock.sin_port);

void SRPC::send_socket(sockaddr_in &sock) throw(SRPC::failure)
{
  char buff[bytes_for_socket];
  p->check_state(SRPC_impl::op_send_socket);

  memcpy(buff, (char *) &sock.sin_addr, sizeof(sock.sin_addr));
  memcpy(buff + sizeof(sock.sin_addr), (char *) &sock.sin_port,
	 sizeof(sock.sin_port));
  try {
    p->send_item_code(SRPC_impl::ic_socket);
    p->send_bytes(buff, sizeof(buff));
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_sending_state();
}

sockaddr_in SRPC::recv_socket(bool *got_end) throw(SRPC::failure)
{
  char buff[bytes_for_socket];
  sockaddr_in sock;

  p->check_state(SRPC_impl::op_recv_socket);

  memset((char *)&sock, 0, sizeof(sock));
  try {
    p->recv_item_code(SRPC_impl::ic_socket, got_end);
    if (got_end == NULL || !*got_end) {
      int incoming_len = p->recv_int32();

      if (incoming_len != bytes_for_socket)
	p->throw_failure(internal_trouble, "Malformed socket");

      p->recv_bytes(buff, incoming_len);

      sock.sin_family = AF_INET;
      memcpy((char *) &sock.sin_addr, buff, sizeof(sock.sin_addr));
      memcpy((char *) &sock.sin_port, buff + sizeof(sock.sin_addr),
	     sizeof(sock.sin_port));
    }
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();
  return sock;
}


//  ***********************************
//  *  Integer sequence transmission  *
//  ***********************************

void SRPC::send_int_seq(int_seq &is) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_int32_seq);

  try {
    // If buffer allocation for 'is' was deferred, do it now.
    if (is.p == NULL) is.p = int_seq_impl::allocate_buffer();

    int n = is.length();
    p->send_item_code(SRPC_impl::ic_int32_seq);
    p->send_int32(n);
    for (int i = 0; i < n; i++) p->send_int32(is[i]);
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_sending_state();
}

void SRPC::recv_int_seq(int_seq &is) throw (SRPC::failure)
{
  p->check_state(SRPC_impl::op_recv_int32_seq);

  try {
    int n;
    int *seq;

    p->recv_item_code(SRPC_impl::ic_int32_seq);
    n = p->recv_int32();
    // If buffer allocation for 'is' was deferred, do it now.
    // Otherwise, ensure buffer is large enough for incoming sequence.
    if (is.p == NULL) is.p = int_seq_impl::allocate_buffer(n);
    else is.lengthen(n);
    seq = &is.p->base;
    is.p->h.len = n;
    for (int i = 0; i < n; i++) seq[i] = p->recv_int32();

  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();
}

//  ******************************
//  *  Short array transmission  *
//  ******************************

void SRPC::send_int16_array(const Basics::int16* seq, int len)
  throw (SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_int16_array);

  try {
    p->send_item_code(SRPC_impl::ic_int16_array);
    p->send_int32(len);
    for (int i = 0; i < len; i++) p->send_int16(seq[i]);
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_sending_state();
}

Basics::int16* SRPC::recv_int16_array(/*OUT*/ int &len)
  throw (SRPC::failure)
{
  p->check_state(SRPC_impl::op_recv_int16_array);

  Basics::int16 *seq;
  try {
    p->recv_item_code(SRPC_impl::ic_int16_array);
    len = p->recv_int32();
    seq = NEW_PTRFREE_ARRAY(Basics::int16, len);
    for (int i = 0; i < len; i++) seq[i] = p->recv_int16();
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();
  return seq;
}

//  ***************************************
//  *  32-bit integer array transmission  *
//  ***************************************

void SRPC::send_int32_array(const Basics::int32* seq, int len)
  throw (SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_int32_array);

  try {
    p->send_item_code(SRPC_impl::ic_int32_array);
    p->send_int32(len);
    for (int i = 0; i < len; i++) p->send_int32(seq[i]);
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_sending_state();
}

Basics::int32* SRPC::recv_int32_array(/*OUT*/ int &len)
  throw (SRPC::failure)
{
  p->check_state(SRPC_impl::op_recv_int32_array);

  Basics::int32 *seq;
  try {
    p->recv_item_code(SRPC_impl::ic_int32_array);
    len = p->recv_int32();
    seq = NEW_PTRFREE_ARRAY(Basics::int32, len);
    for (int i = 0; i < len; i++) seq[i] = p->recv_int32();
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();
  return seq;
}

//  ***************************************
//  *  64-bit integer array transmission  *
//  ***************************************

void SRPC::send_int64_array(const Basics::int64* seq, int len)
  throw (SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_int64_array);

  try {
    p->send_item_code(SRPC_impl::ic_int64_array);
    p->send_int32(len);
    for (int i = 0; i < len; i++) p->send_int64(seq[i]);
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_sending_state();
}

Basics::int64* SRPC::recv_int64_array(/*OUT*/ int &len)
  throw (SRPC::failure)
{
  p->check_state(SRPC_impl::op_recv_int64_array);

  Basics::int64 *seq;
  try {
    p->recv_item_code(SRPC_impl::ic_int64_array);
    len = p->recv_int32();
    seq = NEW_PTRFREE_ARRAY(Basics::int64, len);
    for (int i = 0; i < len; i++) seq[i] = p->recv_int64();
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();
  return seq;
}

//  ********************************************
//  *  Character string sequence transmission  *
//  ********************************************

void SRPC::send_chars_seq(chars_seq &cs) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_chars_seq);

  try {
    // If buffer allocation for 'cs' was deferred, do it now.
    if (cs.p == NULL) cs.p = chars_seq_impl::allocate_buffer();

    // We send the sequence as a single item, which requires that we
    // know the aggregate length of the string bodies.  We reach inside
    // the representation to make this efficient.
    int i;
    int n = cs.p->h.len;
    char **seq = &cs.p->base;
    p->send_item_code(SRPC_impl::ic_chars_seq);
    p->send_int32(n);

    int tot_len = cs.p->h.limit - cs.p->h.bodies;
    p->send_bytes(cs.p->h.bodies, tot_len);
    for (i = 0; i < n; i++)
      {
	assert(seq[i] >= cs.p->h.bodies && seq[i] < cs.p->h.limit);
	p->send_int32(seq[i] - cs.p->h.bodies); 
      }

  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_sending_state();
}

void SRPC::recv_chars_seq(chars_seq &cs) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_recv_chars_seq);
  if (cs.p != NULL) {
    if (cs.p->h.storage == chars_seq_impl::full)
      p->throw_failure(invalid_parameter, "Wrong kind of chars_seq");
    if (cs.p->h.len != 0)
      p->throw_failure(invalid_parameter, "Chars_seq must be empty");
  }

  try {
    int n;
    int body_bytes;
    char **seq;
    p->recv_item_code(SRPC_impl::ic_chars_seq);
    n = p->recv_int32();
    body_bytes = p->recv_int32();
    // If buffer allocation for 'cs' was deferred, do it now.
    if (cs.p == NULL) cs.p = chars_seq_impl::allocate_buffer(n, body_bytes);
    if (cs.p->h.limit - (char *)cs.p < cs.min_size(n) + body_bytes) {
      // 'cs' isn't big enough to hold incoming sequence.
      if (cs.p->h.storage == chars_seq_impl::manual)
	p->throw_failure(buffer_too_small, "buffer too small");
      else chars_seq_impl::expand(cs.p, n, body_bytes);
    }
    seq = &cs.p->base;
    // Receive all string bodies in a lump
    cs.p->h.bodies = cs.p->h.limit - body_bytes;
    p->recv_bytes(cs.p->h.bodies, body_bytes);
    // Receive offsets into bodies; set sequence pointers
    for (int i = 0; i < n; i++)
      seq[i] = cs.p->h.bodies + p->recv_int32();
    cs.p->h.len = n;
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();
}

//  ***************************************
//  *  Byte-string sequence transmission  *
//  ***************************************

void SRPC::send_bytes_seq(bytes_seq &bs) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_bytes_seq);

  try {
    // If buffer allocation for 'bs' was deferred, do it now.
    if (bs.p == NULL) bs.p = bytes_seq_impl::allocate_buffer();

    // We send the sequence as a single item, which requires that we
    // know the aggregate length of the string bodies.  We reach inside
    // the representation to make this efficient.
    int i;
    int n = bs.p->h.len;
    byte_str *seq = &bs.p->base;
    p->send_item_code(SRPC_impl::ic_bytes_seq);
    p->send_int32(n);

    int tot_len = bs.p->h.limit - bs.p->h.bodies;
    p->send_bytes(bs.p->h.bodies, tot_len);
    for (i = 0; i < n; i++)
      {
	assert(seq[i].p >= bs.p->h.bodies && seq[i].p < bs.p->h.limit);
	p->send_int32(seq[i].l); 
      }

  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_sending_state();
}

void SRPC::recv_bytes_seq(bytes_seq &bs) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_recv_bytes_seq);
  if (bs.p != NULL) {
    if (bs.p->h.storage == bytes_seq_impl::full)
      p->throw_failure(invalid_parameter, "Wrong kind of bytes_seq");
    if (bs.p->h.len != 0)
      p->throw_failure(invalid_parameter, "Bytes_seq must be empty");
  }

  try {
    int n;
    int body_bytes;
    byte_str *seq;
    p->recv_item_code(SRPC_impl::ic_bytes_seq);
    n = p->recv_int32();
    body_bytes = p->recv_int32();
    // If buffer allocation for 'bs' was deferred, do it now.
    if (bs.p == NULL) bs.p = bytes_seq_impl::allocate_buffer(n, body_bytes);
    if (bs.p->h.limit - (char *)bs.p < bs.min_size(n) + body_bytes) {
      // 'bs' isn't big enough to hold incoming sequence.
      if (bs.p->h.storage == bytes_seq_impl::manual)
	p->throw_failure(buffer_too_small, "buffer too small");
      else bytes_seq_impl::expand(bs.p, n, body_bytes);
    }
    seq = &bs.p->base;
    // Receive all string bodies in a lump
    bs.p->h.bodies = bs.p->h.limit - body_bytes;
    p->recv_bytes(bs.p->h.bodies, body_bytes);
    // Receive byte-string lengths; compute pointers
    int offset = 0;
    for (int i = 0; i < n; i++) {
      int len = p->recv_int32();
      seq[i].l = len;
      seq[i].p = bs.p->h.bodies + offset;
      offset += len;
    }
    bs.p->h.len = n;
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->ensure_recving_state();
}


//  ******************************************
//  *  Sequence transmission (general case)  *
//  ******************************************

void SRPC::send_seq_start(int len, int bytes) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_seq_start);

  try {
    p->send_item_code(SRPC_impl::ic_seq_start);
    p->send_int32(len);
    p->send_int32(bytes);
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->state = SRPC_impl::seq_out;
}

void SRPC::recv_seq_start(int *len, int *bytes) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_recv_seq_start);

  try {
    p->recv_item_code(SRPC_impl::ic_seq_start);
    int remote_len = p->recv_int32();
    if (len != NULL) *len = remote_len;
    int remote_bytes = p->recv_int32();
    if (bytes != NULL) *bytes = remote_bytes;
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->state = SRPC_impl::seq_in;
}

void SRPC::send_seq_end() throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_seq_end);

  try {
    p->send_item_code(SRPC_impl::ic_seq_end);
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->state = SRPC_impl::data_out;
}

void SRPC::recv_seq_end() throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_recv_seq_end);

  try {
    p->recv_item_code(SRPC_impl::ic_seq_end);
  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }

  p->state = SRPC_impl::data_in;
}


//  **************************************
//  *  End argument/result transmission  *
//  **************************************

void SRPC::send_end() throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_end);

  try {
    p->send_item_code(SRPC_impl::ic_end_call, /*flush=*/ true);

    // If this is the callee, verify that other end is happy.
    // This ensures that any problem at the caller side while receiving
    // the results is reported to the callee before the callee finishes.
    // Otherwise, the callee will assume all is well, and an error reported
    // from the other end will be delivered on the *next* call, which
    // will confuse everyone.
    // Note:  if we are the caller, this handshake can be omitted, since
    // we're about to turn around and wait for the callee to send the
    // results anyway, at which time any error will be reported.

    if (!p->is_caller) {
      p->recv_item_code(SRPC_impl::ic_end_ack);
      p->state = SRPC_impl::initial;
    }
    else
      p->state =  SRPC_impl::ready;

    p->is_sender = false;

  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }
}

void SRPC::recv_end() throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_recv_end);

  try {
    p->recv_item_code(SRPC_impl::ic_end_call);

    if (p->is_caller) {
      // Tell the other end we're happy.  See comment in send_end.
      p->send_item_code(SRPC_impl::ic_end_ack, /*flush=*/ true);
      p->state = SRPC_impl::initial;
    }
    else
      p->state = SRPC_impl::ready;

    p->is_sender = true;

  } catch (TCP_sock::failure f) { p->SRPC_impl::report_TCP_failure(f); }
}


//  ***********************
//  *  Failure reporting  *
//  ***********************

void SRPC::send_failure(int r,
			const Text &msg,
			bool remote_only) throw(SRPC::failure)
{
  p->check_state(SRPC_impl::op_send_failure);
  p->state = SRPC_impl::failed;
  p->f = failure(r, msg);

  try {
    p->send_item_code(SRPC_impl::ic_failure);
    p->send_int32(r);
    p->send_bytes(msg.cchars(), msg.Length(), true);
  } catch(TCP_sock::failure tf) {
    throw(failure(transport_failure, tf.msg));
  }
  if (!remote_only) throw(p->f);
}

SRPC::failure SRPC::convert_TCP_failure(TCP_sock::failure tf)
{
  int r;
  Text msg = tf.msg;
  switch (tf.reason) {
  case TCP_sock::unknown_host: r = SRPC::unknown_host; break;
  case TCP_sock::unknown_port: r = SRPC::unknown_interface; break;
  case TCP_sock::partner_went_away: r = SRPC::partner_went_away; break;
  case TCP_sock::read_timeout: r = SRPC::read_timeout; break;
  case TCP_sock::environment_problem:
    r = SRPC::transport_failure;
    msg = "TCP trouble: " + tf.msg;
    break;
  default:
    r = SRPC::internal_trouble;
    msg = "Internal error (TCP): " + tf.msg;
    break;
  }
  return failure(r, msg);
}

