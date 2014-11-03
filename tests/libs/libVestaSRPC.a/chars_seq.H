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

// Last modified on Tue Mar 22 13:45:08 EST 2005 by ken@xorian.net
//      modified on Mon Jun 16 11:07:39 PDT 1997 by heydon
//      modified on Tue Aug  8 11:32:39 PDT 1995 by levin

#ifndef _chars_seq
#define _chars_seq

//  *********************************************************
//  *  Supporting class for sequences of character strings  *
//  *********************************************************

#include <Basics.H>
#include "chars_seq_private.H"

class chars_seq {
  friend class SRPC;
  friend class SRPC_impl;
  friend class chars_seq_impl;
public:

  // Generic failure type, for exception handling.

  struct failure {
    Text msg;
    inline failure(const Text &msg) { this->msg = msg; };
    inline failure(const failure &f) { this->msg = f.msg; };
    inline failure& operator=(const failure &f) {
      this->msg = f.msg; return *this; };
  };

  // Construction

  typedef char *chars;

  chars_seq(int len_hint = 0, int bytes = 0);                     /* "auto" */
    // This constructor creates a (logically empty) chars_seq in dynamically
    // allocated storage.  "len_hint" estimates the eventual length of the
    // sequence.  If "bytes" is greater than 0, it specifies the number of
    // bytes of storage to be initially allocated to hold the bodies of
    // strings (see "append", below).

  chars_seq(void **buffer, int bytes) throw(failure);            /* "manual" */
    // This constructor creates a (logically empty) chars_seq in storage
    // provided by the client.  "buffer" specifies the starting address of
    // of the storage, "bytes" specifies its extent.  The buffer is used
    // for the string bodies, the sequence itself (i.e., the pointers to
    // the string bodies), and a small amount of overhead.  (It is typed
    // void** to emphasize the necessary alignment, even though its extent
    // is given in bytes.)  For assistance in computing the "bytes"
    // paramter, see the "min_size" function, below.

  chars_seq(const char **seq, int len) throw(failure);           /* auto */
    // This constructor creates a fully filled-in chars_seq from an existing
    // sequence of strings.  The "seq" and "len" parameters specify the address
    // and length of a sequence of pointers to null-terminated character
    // strings.

  ~chars_seq();
    // Any storage allocated by the "auto" constructor, the "full" constructor
    // or the "append" function (below) used on a chars_seq created by the
    // "auto" constructor is deallocated.  This destructor has no effect on
    // storage used by a chars_seq created by the "manual" constructor.

  static int min_size(int len);
    // This function returns the number of bytes required to hold a chars_seq
    // of "len" empty strings.  It is intended to assist the client in
    // computing a sufficient value for the "bytes" parameter to the
    // "manual" constructor. (Not inline because of compiler bug.)

  // Conversion to char**

  inline operator chars*() const { return (p == NULL ? NULL : &p->base); };
    // Warning!  The result returned by this function is valid only as long
    // as the underlying storage exists.  In particular, if the chars_seq is
    // destroyed, the pointer becomes invalid.  Moreover, an "append" or
    // "append-ptr" operation that expands storage may render invalid a result
    // previously returned by this function.  Use with care!

  // Sequence member access

  inline chars ith(int i) const { return ((chars *)&p->base)[i]; };
    // note: no bounds checking:  in particular, no check for p == NULL

  inline chars operator[](int i) const { return ith(i); };
    // i.e., cs[i] is equivalent to cs.ith(i)

  inline int length() const { return (p == NULL ? 0 : p->h.len); };

  // Adding members to the sequence

  void append(const chars s) throw(failure);
    // If the chars_seq was created with "auto" constructor, 's' is copied to
    // dynamically allocated storage and a pointer to the copy is appended to
    // the sequence.  Storage allocated at construction time is used first,
    // if available; otherwise, additional storage is allocated as necessary.
    // If the chars_seq was created with the "manual" constructor and has
    // sufficient space in the buffer provided at construction time, 's' is
    // copied to the buffer and a pointer to the copy is appended to the
    // sequence.  Otherwise, "failure" is thrown.

  void append(const Text &t) throw(failure);
    // This function is semantically equivalent to:
    //   append(t.chars());

  inline void operator+=(const chars s) throw(failure) { append(s); };
    // i.e.,  cs += s  is equivalent to cs.append(s)

  inline void operator+=(const Text &t) throw(failure) { append(t); };
    // i.e.,  cs += t  is equivalent to cs.append(t)

private:
  chars_seq_impl::rep *p;

  // A chars_seq can be neither copied nor assigned.

  chars_seq(const chars_seq &);
  chars_seq(chars_seq &);
    // Copy constructor; should never be accessed.

  void operator=(chars_seq &);

};


#endif  /* _chars_seq  */
