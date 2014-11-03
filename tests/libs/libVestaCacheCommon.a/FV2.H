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

#ifndef _FV_2_H
#define _FV_2_H

/* Like "FV", but the individual names are *sequences* of texts, rather
   than simple texts containing delimiters. These are only used by the client.

   IMPORTANT: Use of this interface assumes you are linking your application
   program with a garbage collector! Also, none of these classes defines an
   assignment operator, so objects are copied field-wise by default. This
   means that use of the assignment operator will produce aliases for those
   fields that are pointer types. */

#include <Basics.H>
#include <Generics.H>
#include <SRPC.H>

namespace FV2
{
  // defined below
  class T;
  class List;
  class ListApp;
}

std::ostream& operator << (std::ostream &os, const FV2::T &t) throw ();
std::ostream& operator << (std::ostream &os, const FV2::List &names) throw ();

namespace FV2
{
  class T: public AtomSeq {
  public:
    // default constructor
    T(int sizeHint = 7) : AtomSeq(sizeHint) { /*SKIP*/; }

    // constructor (from a text)
    T(const Text &t, char delim = '/') throw ()
      : AtomSeq(/*sizehint=*/ 7) { FromStr(t.cchars(), delim); }
    /* Return a new "T" whose elements are the "delim"-separated
       sub-strings of "t". */

    // convert to a text
    Text ToText(const char delim = '/') const throw ();
    /* Return a text representation of the values of this sequence
       concatenated together and separated by "delim". */

    // print
    void Print(std::ostream &os) const throw();
    /* Write an ASCII representation of "t" to "os". */

  private:
    void FromStr(const char *str, const char delim) throw ();
    /* Append the "delim"-separated texts of the null-terminated string
       "str" to this "FV::T". */

    char* ToStr(char *buff, const int sz, char delim, char type = '\0')
      const throw ();
    /* Return a null-terminated string corresponding to the Text's of this
       sequence concatenated together and separated by "delim". If "type"
       is any character but the null character, that character and "/" are
       prepended onto the result. In that case, it is a checked run-time
       error if "sz" is not at least 2.

       "buff" should be a non-NULL buffer of size at least "sz". If
       possible, the result will be written into "buff", and "buff" will
       be returned. Otherwise, a new string will be allocated on the heap
       that is large enough to hold the result. The return result is the
       character array containing the resulting string; it will either be
       "buff" or a block of memory allocated on the heap. */

    // send
    void Send(SRPC &srpc, char type = '\0') const throw (SRPC::failure);
    /* Note: There is no "Recv" method because the client receives
       names in response to the "FreeVariables" method in the form
       of a "CompactFV" object. */

    friend class List;
  };

  typedef T *TPtr;

  class List {
  public:
    Basics::uint32 len;
    FV2::TPtr *name; // array of pointers

    // constructors
    List() throw () : len(0), name(NULL) { /*SKIP*/ }

    // send
    void Send(SRPC &srpc, const char *types = NULL) const
      throw (SRPC::failure);
    /* Note: There is no "Recv" method because the client receives
       names in response to the "FreeVariables" method in the form
       of a "CompactFV" object. */

    // print
    void Print(std::ostream &os, int indent, const char *types = NULL) const
      throw ();
  private:
    // hide copy constructor
    List(const List&);
  };

  // list with append operation
  class ListApp: public List {
  public:
    // constructors
    ListApp() throw () : List(), maxLen(0) { /*SKIP*/ }

    void Reset() throw () { len = 0; }
    /* Reset this list so as not to contain any names; this does not
       free the list's underlying storage. */

    int Append(FV2::T *s) throw ();
    /* Append the name "s" to the list, and return the index of "s" in
       the (zero-based) "name" array. */
  private:
    int maxLen;		  // size of "name"
    ListApp(const ListApp&);  // hide from clients
  };
}

#endif // _FV_2_H
