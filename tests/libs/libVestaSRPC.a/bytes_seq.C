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

// Last modified on Fri Apr 22 13:17:30 EDT 2005 by irina.furman@intel.com 
//      modified on Tue Mar 22 14:47:05 EST 2005 by ken@xorian.net         
//      modified on Wed Jul 31 13:28:49 EDT 2002 by kcschalk@shr.intel.com 
//      modified on Tue Feb  8 11:55:15 PST 2000 by mann  
//      modified on Thu Jun 27 18:00:28 PDT 1996 by heydon
//      modified on Wed Mar  8 23:34:42 PST 1995 by levin

//  *********************************
//  *  Implementation of bytes_seq  *
//  *********************************

#include "bytes_seq.H"

typedef char *chars;
typedef bytes_seq_impl::rep rep;

//  **************
//  *  Utilities  *
//  ***************

inline int round_up(int v, int m) { return ((v + m - 1)/m)*m; }

rep *bytes_seq_impl::allocate_buffer(int len_hint, int bytes)
{
  // Allocate a buffer with room for "len_hint" byte-str's and "bytes"
  // characters of string bodies, ensuring that the buffer is aligned
  // for pointer storage.
  int size = bytes_seq::min_size(len_hint) + round_up(bytes, sizeof(chars));
  char* buffer = (char*) NEW_PTRFREE_ARRAY(chars, size/sizeof(chars));
  rep *p = (rep *) buffer;
  p->h.storage = grow;
  p->h.len = 0;
  p->h.bodies = p->h.limit = &buffer[size];
  return p;
}

void bytes_seq_impl::expand(bytes_seq_impl::rep *&p,
			    int len_hint,
			    int min_body_add)
{
  if (p->h.storage == manual)
    throw(bytes_seq::failure("Buffer has insufficient space."));
  // Expand storage.  If len_hint is non-negative, it specifies a new length
  // hint.  If min_body_add is non-negative, the string space either will be
  // doubled (compared to the presently used amount), or increased in size
  // by min_body_add, whichever produces a larger resulting buffer.
  int new_len = len_hint < 0 ? p->h.len : len_hint;
  int body_space =
    min_body_add >= 0 ? max(2*(p->h.limit - p->h.bodies), min_body_add) : 0;
  rep *new_p = allocate_buffer(new_len, body_space);

  new_p->h.storage = p->h.storage;
  new_p->h.bodies = new_p->h.limit - body_space;

  // Copy string bodies (if any) as a unit
  memcpy(new_p->h.bodies, p->h.bodies, body_space);

  // Copy byte_str's one at at time, relocating if necessary.  This code
  // isn't portable according to the ARM, but should work on any machine
  // with a flat memory architecture.
  byte_str *seq = &p->base;
  byte_str *new_seq = &new_p->base;
  int offset = new_p->h.bodies - p->h.bodies;
  for (int i = 0; i < p->h.len; i++) {
    assert(seq[i].p >= p->h.bodies && seq[i].p < p->h.limit);
    new_seq[i].p = seq[i].p + offset;
    new_seq[i].l = seq[i].l;
  }
  new_p->h.len = p->h.len;

  delete[] (chars *)p;
  p = new_p;

}


//  **********************************
//  *  Constructors and Destructors  *
//  **********************************

bytes_seq::bytes_seq(int len_hint, int bytes)
{
  if (len_hint == 0 && bytes == 0)
    // Defer allocation until needed.  The first allocation might
    // be via SRPC::recv_bytes_seq, which can allocate precisely the
    // right amount.
    p = NULL;
  else p = bytes_seq_impl::allocate_buffer(len_hint, bytes);
}

bytes_seq::bytes_seq(void **buffer, int bytes) throw (bytes_seq::failure)
{
  p = (rep *)buffer;
  p->h.storage = bytes_seq_impl::manual;
  p->h.len = 0;
  p->h.bodies = p->h.limit = (char *)buffer + bytes;
}

bytes_seq::~bytes_seq()
{
  if (p == NULL || p->h.storage == bytes_seq_impl::manual) return;
  delete[] (chars *)p;
}

/* static */ int bytes_seq::min_size(int len)
{
  // This calculation allows for the possibility that padding will be
  // inserted between 'h' and 'base'.  It does this by computing the
  // size of 'rep', which includes (in effect) a single-element sequence
  // (i.e., base), then adding additional space for len-1 additional
  // sequence elements (each a 'byte_str').
  return
    round_up(sizeof(bytes_seq_impl::rep) + (len-1)*(sizeof(byte_str)),
	    sizeof(chars));
}


//  ***************************
//  *  Sequence modification  *
//  ***************************

void bytes_seq::append(const byte_str &b) throw(bytes_seq::failure)
{
  if (p == NULL) p = bytes_seq_impl::allocate_buffer();
  int bytes_for_body = b.l;
  byte_str *seq = &p->base;    // base of pointer vector
  char *s_copy = p->h.bodies - bytes_for_body;

  // The following pointer comparison is portable, per ARM 5.9, since
  // both point into the same array (or one beyond its end).
  if (&seq[p->h.len+1].p > (chars *)s_copy) {
    // No space in buffer for string body and pointer.  Expand first.
    bytes_seq_impl::expand(p, 1+2*p->h.len, bytes_for_body);
    // Recompute pointers affected by 'expand'
    seq = &p->base;
    s_copy = p->h.bodies - bytes_for_body;
  }
  // Now there must be room, regardless of whether we expanded or not.
  assert(&seq[p->h.len+1].p <=  (chars *)s_copy);

  strcpy(s_copy, b.p);
  seq[p->h.len].p = p->h.bodies = s_copy;
  seq[p->h.len].l = bytes_for_body;
  p->h.len++;
}
