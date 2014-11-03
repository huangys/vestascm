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

// Last modified on Fri Apr 22 13:41:10 EDT 2005 by irina.furman@intel.com 
//      modified on Tue Aug  6 16:35:31 EDT 2002 by ken@xorian.net         
//      modified on Thu Aug  1 11:03:24 EDT 2002 by kcschalk@shr.intel.com 
//      modified on Tue Feb  8 11:56:49 PST 2000 by mann  
//      modified on Thu Jun 27 17:59:15 PDT 1996 by heydon
//      modified on Wed Mar  8 23:18:41 PST 1995 by levin


//  *******************************
//  *  Implementation of int_seq  *
//  *******************************

#include "int_seq.H"

typedef int_seq_impl::rep rep;

//  ***************
//  *  Utilities  *
//  ***************

rep *int_seq_impl::allocate_buffer(int len_hint)
{
  // The following odd-looking calculation ensures that the buffer
  // will hold at least 'len_hint' integers and will be properly
  // aligned.  It assumes that the largest alignment required by 'rep'
  // is for type 'int32'.
  int size_as_ints = ((int_seq::min_size(len_hint)+sizeof(Basics::int32)-1) /
		      sizeof(Basics::int32));
  rep* p = (rep*) NEW_PTRFREE_ARRAY(Basics::int32, size_as_ints);
  p->h.storage = grow;
  p->h.len = 0;
  p->h.maxlen = len_hint;
  return p;
}


//  **********************************
//  *  Constructors and Destructors  *
//  **********************************

int_seq::int_seq(int len_hint)
{
  if (len_hint == 0)
    // Defer allocation until needed.  The first allocation might
    // be SRPC::recv_int_seq, which can allocate precisely the right
    // amount.
    p = NULL;
  else p = int_seq_impl::allocate_buffer(len_hint);
}

int_seq::int_seq(Basics::int32 *buffer, int bytes) throw(int_seq::failure)
{
  p = (rep *) buffer;
  p->h.storage = int_seq_impl::manual;
  p->h.len = 0;
  p->h.maxlen = (bytes - min_size(0))/sizeof(Basics::int32);
}

int_seq::int_seq(const Basics::int32 *seq, int len) throw(int_seq::failure)
{
  p = int_seq_impl::allocate_buffer(len);
  p->h.storage = int_seq_impl::full;
  p->h.len = len;
  memcpy((char *)&p->base, (char *)seq, len*sizeof(Basics::int32));
}

int_seq::~int_seq()
{
  if (p == NULL || p->h.storage == int_seq_impl::manual) return;
  delete[] (Basics::int32 *)p;
}

/* static */ int int_seq::min_size(int len)
{
  // This calculation allows for the possibility that padding will be
  // inserted between 'h' and 'base'.  However, on most platforms this
  // really shouldn't happen, since 'h' consists of three 'int' fields
  // and 'base' is an 'int32', which are normally the same type.
  return sizeof(int_seq_impl::rep) + (len-1)*sizeof(Basics::int32);
}


//  ***************************
//  *  Sequence modification  *
//  ***************************

void int_seq::append(Basics::int32 i) throw(int_seq::failure)
{
  Basics::int32 *seq;

  if (p == NULL) p = int_seq_impl::allocate_buffer();
  if (p->h.len == p->h.maxlen) lengthen(2 * p->h.maxlen);

  seq = &p->base;
  seq[p->h.len] = i;
  p->h.len++;
}

void int_seq::lengthen(int len_hint)
{
  if (p == NULL) {
    p = int_seq_impl::allocate_buffer(len_hint);
    return;
  }
  if (len_hint <= p->h.maxlen) return;
  if (p->h.storage != int_seq_impl::grow)
    throw(failure("Buffer has insufficient space."));

  rep *new_p = int_seq_impl::allocate_buffer(len_hint);

  new_p->h.len = p->h.len;
  memcpy((char *)&new_p->base, (char *)&p->base,
	 p->h.len * sizeof(Basics::int32));

  delete[] (Basics::int32 *)p;
  p = new_p;
}
