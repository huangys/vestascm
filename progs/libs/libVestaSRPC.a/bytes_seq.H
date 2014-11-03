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

// Last modified on Tue Mar 22 14:40:25 EST 2005 by ken@xorian.net
//      modified on Mon Jun 16 11:07:40 PDT 1997 by heydon
//      modified on Tue Aug  8 11:33:37 PDT 1995 by levin

#ifndef _bytes_seq
#define _bytes_seq

//  **************************************************************
//  *  Supporting class for sequences of arbitrary byte strings  *
//  **************************************************************

#include <Basics.H>

struct byte_str {            // "byte string"
  char *p;                   // pointer to first byte of string
  int l;                     // number of bytes in string

  inline byte_str(char *p, int l) { this->p = p; this->l = l; };
};

#include "bytes_seq_private.H"

class bytes_seq {
  friend class SRPC;
  friend class SRPC_impl;
  friend class bytes_seq_impl;
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

  bytes_seq(int len_hint = 0, int bytes = 0);                      /* "auto" */
    // This constructor creates a (logically empty) bytes_seq in dynamically
    // allocated storage.  "len_hint" estimates the eventual length of the
    // sequence.  If "bytes" is greater than 0, it specifies the number of
    // bytes of storage to be allocated to hold the bodies of byte sequences
    // (see "append", below).

  bytes_seq(void **buffer, int bytes) throw(failure);            /* "manual" */
    // This constructor creates a (logically empty) bytes_seq in storage
    // provided by the client.  "buffer" specifies the starting address of
    // of the storage, "bytes" specifies its extent.  The buffer is used
    // for the byte string bodies, the sequence itself (i.e., the byte_str
    // structures), and a small amount of overhead.  (It is typed
    // void** to emphasize the necessary alignment, even though its extent
    // is given in bytes.)  For assistance in computing the "bytes"
    // paramter, see the "min_size" function, below.

  ~bytes_seq();
    // Any storage allocated by the "auto" constructor, the "full" constructor
    // or the "append" function (below) used on a bytes_seq created by the
    // "auto" constructor is deallocated.  This destructor has no effect on
    // storage used by a bytes_seq created by the "manual" constructor.

  static int min_size(int len);
    // This function returns the number of bytes required to hold a bytes_seq
    // of "len" empty byte strings.  It is intended to assist the client in
    // computing a sufficient value for the "bytes" parameter to the
    // "manual" constructor. (Not inline because of compiler bug.)

  // Sequence member access

  inline byte_str ith(int i) const { return ((byte_str *)&p->base)[i]; };
    // note: no bounds checking:  in particular, no check for p == NULL

  inline byte_str operator[](int i) const { return ith(i); };
    // i.e., bs[i] is equivalent to bs.ith(i)

  inline int length() const { return (p == NULL ? 0 : p->h.len); };

  // Adding members to the sequence

  void append(const byte_str &b) throw(failure);
    // If the bytes_seq was created with "auto" constructor, the byte string
    // pointed to by 'b' is copied to dynamically allocated storage and a
    // pointer to the copy is appended to the sequence.  Storage allocated
    // at construction time is used first, if available; additional storage
    // is allocated as necessary.  If the bytes_seq was created with the
    // "manual" constructor and has sufficient space in the buffer provided
    // at construction time, the byte string pointed to by 'b' is
    // copied to the buffer and a pointer to the copy is appended to the
    // sequence.  Otherwise, "failure" is thrown.

  inline void operator+=(const byte_str &b) throw(failure) { append(b); };
    // i.e.,  bs += b  is equivalent to bs.append(b)

private:
  bytes_seq_impl::rep *p;

  // A bytes_seq can be neither copied nor assigned.

  bytes_seq(const bytes_seq &);
  bytes_seq(bytes_seq &);
    // Copy constructor; should never be accessed.

  void operator=(bytes_seq &);

};


#endif  /* _bytes_seq  */
