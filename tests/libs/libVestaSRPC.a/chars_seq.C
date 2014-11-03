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

// Last modified on Fri Apr 22 13:38:08 EDT 2005 by irina.furman@intel.com 
//      modified on Tue Mar 22 14:26:53 EST 2005 by ken@xorian.net         
//      modified on Wed Jul 31 13:28:32 EDT 2002 by kcschalk@shr.intel.com 
//      modified on Tue Feb  8 11:56:21 PST 2000 by mann  
//      modified on Thu Jun 27 18:00:28 PDT 1996 by heydon
//      modified on Thu Jul 20 12:26:06 PDT 1995 by levin

//  *********************************
//  *  Implementation of chars_seq  *
//  *********************************

#include "chars_seq.H"

typedef chars_seq::chars chars;
typedef chars_seq_impl::rep rep;


//  **************
//  *  Utilities  *
//  ***************


// Return the smallest multiple of 'm' at least as large as 'v'. This is
// equivalent to 'ceiling((float)v/(float)m) * m'.
inline int round_up(int v, int m) { return (((v + m - 1) / m) * m); }

rep *chars_seq_impl::allocate_buffer(int len_hint, int bytes)
{
  // Allocate a buffer with room for "len_hint" pointers and "bytes"
  // characters of string bodies (excluding terminators), ensuring that
  // the buffer is aligned for pointer storage.
  int size = chars_seq::min_size(len_hint) + round_up(bytes, sizeof(chars));
  char* buffer = (char *) NEW_PTRFREE_ARRAY(chars, size/sizeof(chars));
  rep *p = (rep *) buffer;
  p->h.storage = grow;
  p->h.len = 0;
  p->h.bodies = p->h.limit = &buffer[size];
  return p;
}

void chars_seq_impl::expand(rep *&p, int len_hint, int min_body_add)
{
  if (p->h.storage == manual)
    throw(chars_seq::failure("Buffer has insufficient space."));
  // Expand storage.  If len_hint is non-negative, it specifies a new length
  // hint.  If min_body_add is non-negative, the string space either will be
  // doubled (compared to the presently used amount), or increased in size
  // by min_body_add, whichever produces a larger resulting buffer.
  int new_len = len_hint < 0 ? p->h.len : len_hint;
  int old_body_sz = p->h.limit - p->h.bodies;
  int old_char_cnt = old_body_sz - p->h.len; // subtract off NULL terminators
  int new_char_cnt = old_char_cnt +
    ((min_body_add >= 0) ? max(old_char_cnt, min_body_add) : 0);

  // allocate a new buffer, subtracting out terminators of existing strings
  rep *new_p = allocate_buffer(new_len, new_char_cnt);

  new_p->h.storage = p->h.storage;
  new_p->h.len = p->h.len;
  new_p->h.bodies -= old_body_sz;

  // Copy string bodies (if any) as a unit
  memcpy(new_p->h.bodies, p->h.bodies, old_body_sz);

  // Copy pointers one at a time, relocating if necessary.
  chars *seq = &p->base;
  chars *new_seq = &new_p->base;
  for (int i = 0; i < p->h.len; i++) {
    assert(seq[i] >= p->h.bodies && seq[i] < p->h.limit);

    // Compute its offset from the end of the original block.
    int offset = p->h.limit - seq[i];
    // Compute the new pointer based on the offset and the end of
    // the new block.
    new_seq[i] = new_p->h.limit - offset;

    // Additional paranoia, probably warranted given the pointer
    // math.
    assert((new_seq[i] >= new_p->h.bodies) &&
	   (new_seq[i] < new_p->h.limit));

    seq[i] = (char *)NULL; // help garbage collector
  }
  delete[] (chars *)p;
  p = new_p;
}


//  **********************************
//  *  Constructors and Destructors  *
//  **********************************

chars_seq::chars_seq(int len_hint, int bytes)
{
  if (len_hint == 0 && bytes == 0)
    // Defer allocation until needed.  The first allocation might
    // be via SRPC::recv_chars_seq, which can allocate precisely the
    // right amount.
    p = NULL;
  else p = chars_seq_impl::allocate_buffer(len_hint, bytes);
}

chars_seq::chars_seq(void **buffer, int bytes) throw (chars_seq::failure)
{
  p = (rep *) buffer;
  p->h.storage = chars_seq_impl::manual;
  p->h.len = 0;
  p->h.bodies = p->h.limit = (char *)buffer + bytes;
}

chars_seq::chars_seq(const char **seq, int len) throw(chars_seq::failure)
{
  int total_len = 0;
  for (int i = 0; i < len; i++) total_len += strlen(seq[i]);
  p = chars_seq_impl::allocate_buffer(len, total_len);
  for (int i = 0; i < len; i++) append(seq[i]);
}

chars_seq::~chars_seq()
{
  if (p == NULL || p->h.storage == chars_seq_impl::manual) return;
  delete[] (chars *)p;
}

/* static */ int chars_seq::min_size(int len)
{
  // This calculation allows for the possibility that padding will be
  // inserted between 'h' and 'base'.  It does this by computing the
  // size of 'rep', which includes (in effect) a single-element sequence
  // (i.e., base), then adding additional space for len-1 additional pointers
  // as well as space for a null terminator on each of the 'len' strings.
  return
    round_up(sizeof(chars_seq_impl::rep) + (len-1)*sizeof(chars) + len*1,
	    sizeof(chars));
}

//  ***************************
//  *  Sequence modification  *
//  ***************************

void chars_seq::append(const chars s) throw(chars_seq::failure)
{
  if (p == NULL) p = chars_seq_impl::allocate_buffer();
  int s_len = strlen(s);
  int bytes_for_body = s_len + 1; // include NULL terminator
  chars *seq = &p->base;    // base of pointer vector
  char *s_copy = p->h.bodies - bytes_for_body;

  // The following pointer comparison is portable, per ARM 5.9, since
  // both point into the same array (or one beyond its end).
  if (&seq[p->h.len+1] > (chars *)s_copy) {
    // No space in buffer for string body and pointer.  Expand first.
    chars_seq_impl::expand(p, 1+2*p->h.len, s_len);
    // Recompute pointers affected by 'expand'
    seq = &p->base;
    s_copy = p->h.bodies - bytes_for_body;
  }
  // Now there must be room, regardless of whether we expanded or not.
  assert(&seq[p->h.len+1] <=  (chars *)s_copy);

  memcpy(s_copy, s, bytes_for_body);
  seq[p->h.len] = p->h.bodies = s_copy;
  p->h.len++;
}

void chars_seq::append(const Text &t) throw(chars_seq::failure)
{
  append(t.chars());
}
