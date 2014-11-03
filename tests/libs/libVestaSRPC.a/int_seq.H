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

// Last modified on Thu Aug  1 11:08:10 EDT 2002 by kcschalk@shr.intel.com
//      modified on Tue Nov 13 12:38:26 EST 2001 by ken@xorian.net
//      modified on Mon Jun 16 11:07:39 PDT 1997 by heydon
//      modified on Tue Aug  8 11:33:21 PDT 1995 by levin

#ifndef _int_seq
#define _int_seq

#include <Basics.H>
#include "int_seq_private.H"

//  *********************************************************************
//  *  Supporting class for SRPC for tranmitting sequences of integers  *
//  *********************************************************************

class int_seq {
  friend class SRPC;
  friend class SRPC_impl;
  friend class int_seq_impl;
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

  int_seq(int len_hint = 0);                                     /* "auto" */
    // This constructor creates a (logically empty) int_seq in dynamically
    // allocated storage.  "len_hint" estimates the eventual length of the
    // sequence.

  int_seq(Basics::int32 *buffer, int bytes) throw(failure);      /* "manual" */
    // This constructor creates a (logically empty) int_seq in storage
    // provided by the client.  "buffer" specifies the starting
    // address of of the storage, "bytes" specifies its extent.  The
    // buffer is used for the sequence itself and a small amount of
    // overhead.  (It is typed int32* to emphasize the required
    // alignment, even though the "bytes" parameter specifies its
    // extent in bytes.)  For assistance in computing "bytes", see the
    // "min_size" function, below.

    // **Warning** Note the subtle difference in the parameter types
    // between this constructor and the next one.  Since their
    // semantics are very different, use care in constructing the
    // arguments!

  int_seq(const Basics::int32 *seq, int len) throw(failure);     /* "full" */
    // This constructor creates a fully filled-in int_seq from an
    // existing array of 32-bit integers.  The "seq" and "len"
    // parameters specify the address and length of an array of 32-bit
    // integers.  The array is copied into dynamically allocated
    // storage.  The resulting sequence cannot be subsequently
    // extended with "append" or "append-ptr"; attempts to do so cause
    // "failure" to be thrown.

    // **Warning** Note the subtle difference in the parameter types
    // between this constructor and the preceding one.  Since their
    // semantics are very different, use care in constructing the
    // arguments!

  ~int_seq();
    // Releases dynamic storage allocated by the constructors above.

  static int min_size(int len);
    // This function returns the number of bytes required to hold a
    // int_seq of "len" 32-bit integers.  (Not inline because of
    // compiler bug.)

  // Conversion to int32*

  inline operator Basics::int32*() const
  { return (p == NULL ? NULL : &p->base); }
    // Warning!  The result returned by this function is valid only as long
    // as the underlying storage exists.  In particular, if the int_seq is
    // destroyed, the pointer becomes invalid.  Moreover, an "append"
    // operation that expands storage may render invalid a result
    // previously returned by this function.  Use with care!


  // Sequence member access

  inline Basics::int32 ith(int i) const
  { return ((Basics::int32 *)&p->base)[i]; }
    // note: no bounds checking:  in particular, no check for p == NULL

  inline Basics::int32 operator[](int i) const { return ith(i); }
    // i.e., is[i] is equivalent to is.ith(i)

  inline int length() const { return (p == NULL ? 0 : p->h.len); }

  // Adding members to the sequence

  void append(Basics::int32 i) throw(failure);
    // "append" adds the new member 'i'  to the end of the sequence,
    // allocating additional storage if necessary.  This works only if the
    // int_seq was created with the "auto" constructor; otherwise, "failure"
    // is thrown.

  inline void operator+=(Basics::int32 i) throw(failure) { append(i); }
    // i.e.,  is += i  is equivalent to is.append(i)

  // Storage management

  void lengthen(int len_hint);
    // If the int_seq was created with the "auto" constructor, its storage
    // is reconfigured according to the new length hint.  A larger buffer
    // is allocated and the existing data is copied into it.  If len_hint
    // is smaller than the previously suppied value (either at construction
    // time or on a previous call of "lengthen"), it has no effect.

private:
  int_seq_impl::rep *p;

  // An int_seq can be neither copied nor assigned.

  int_seq(const int_seq &);
  int_seq(int_seq &);
    // Copy constructor; should never be accessed.

  void operator=(int_seq &);

};

#endif  /* _int_seq */
