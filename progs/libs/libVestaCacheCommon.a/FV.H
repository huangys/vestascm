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

// Created on Fri Nov  7 10:11:38 PST 1997 by heydon

/* Defines the types "FV::Epoch", "FV::T", "FV::List", and "FV::ListApp".
   A "FV::T" is a free variable, a subtype of "Text".

   IMPORTANT: Use of this interface assumes you are linking your
   application program with a garbage collector! Also, none of these
   classes defines an assignment operator, so objects are copied
   field-wise by default. This means that use of the assignment
   operator will produce aliases for those fields that are pointer
   types. */

#ifndef _FV_H
#define _FV_H

#include <Basics.H>
#include <FS.H>
#include <SRPC.H>
#include <VestaLog.H>
#include <Recovery.H>
#include "BitVector.H"
#include "PrefixTbl.H"

namespace FV
{
  // epoch of free variables set
  typedef int Epoch;

  // free variable
  class T: public Text {
  public:
    // inherit all constructors from "Text"
    T() throw () : Text() { /*SKIP*/ }
    T(const T& t) throw (): Text(t) { /*SKIP*/ }
    T(const char c) throw (): Text(c) { /*SKIP*/ }
    T(const char *str) throw (): Text(str) { /*SKIP*/ }
    T(const char *bytes, int len) throw (): Text(bytes, len) { /*SKIP*/ }

    // send/receive
    void Send(SRPC &srpc) const throw (SRPC::failure)
    { srpc.send_chars(this->s); }
    void Recv(SRPC &srpc) throw (SRPC::failure)
    { this->s = srpc.recv_chars(); }
  };
    
  // list of free variables
  class List {
  public:
    FV::T *name;		// array of names
    Basics::uint32 len;      // size of "name" array

    List() throw () : len(0), name(NULL) { /*SKIP*/ }
    List(int size) throw () : len(size), name(NEW_ARRAY(FV::T, size)) { }
    List(SRPC &srpc) throw (SRPC::failure) { Recv(srpc); }
    List(RecoveryReader &rd) throw (VestaLog::Error, VestaLog::Eof)
    { Recover(rd); }
    List(std::istream &ifs) throw (FS::EndOfFile, FS::Failure)
    { Read(ifs); }

    // copy
    void Copy(const List &l) throw ();
    /* Set this list to a copy of the names in "l". */

    // size
    int CheapSize() const throw ()
    /* This is an underestimate of the actual storage size of a List,
       but it statically computable. */
    { return sizeof(this->len) + (this->len * sizeof(*(this->name))); }
    int Size() const throw ();
    /* This is the true size of a List, but it takes time linear
       in the sum of the sizes of the List names to compute. */

    // send/receive
    void Send(SRPC &srpc, bool old_protocol = false)
      const throw (SRPC::failure, PrefixTbl::Overflow);
    void Recv(SRPC &srpc) throw (SRPC::failure);

    // print
    friend std::ostream& operator << (std::ostream &os, const List &names) throw ();
    void Print(std::ostream &os, int indent,
	       const BitVector *bv = (BitVector *)NULL) const throw ();
    /* Print a list of names, each on its own line and indented by
       "indent" spaces. If "bv" is non-NULL and "indent > 1", then each
       name in the list whose index corresponds to a set bit in "bv" is
       preceeded by a "*". */

    // logging/recovery
    void Log(VestaLog &log) const throw (VestaLog::Error);
    void Recover(RecoveryReader &rd)
      throw (VestaLog::Error, VestaLog::Eof);

    // write/read
    void Write(std::ostream &ofs) const throw (FS::Failure);
    void Read(std::istream &ifs) throw (FS::EndOfFile, FS::Failure);

    // Comparison (primarily for testing)
    bool operator==(const FV::List &other) const throw ();
    bool operator!=(const FV::List &other) const throw ()
    {
      return !(*this == other);
    }

  private:
    // hide the copy constructor from clients
    List(const List&);
  };

  // list with append operation
  class ListApp: public List {
  public:
    // constructors
    ListApp() throw ()
      : List(), maxLen(0) { /*SKIP*/ }
    ListApp(int sizeHint) throw ()
      : List(), maxLen(sizeHint) {
      this->name = NEW_ARRAY(FV::T, sizeHint);
    }
    ListApp(RecoveryReader &rd) throw (VestaLog::Error, VestaLog::Eof)
    { Recover(rd); }
    ListApp(std::istream &ifs) throw (FS::EndOfFile, FS::Failure)
    { Read(ifs); }

    void Reset() throw () { len = 0; }
    /* Reset this list so as not to contain any names; this does not free
       the list's underlying storage. */

    void Grow(int sizeHint) throw ();
    /* Allocate enough storage so this list can hold up to "sizeHint"
       names without requiring any new allocation. */

    int Append(const FV::T& s) throw ();
    /* Append the text "s" to the list, and return the index of "s" in
       the (zero-based) "name" array. */

    void Pack(const BitVector &packMask) throw ();
    /* "Pack" the names in this list so as to include only those names
       whose indices correspond to set bits in "packMask". The order of
       those preserved names is unchanged, but the names are packed toward
       the start of the array so there are no holes in the list. */
    void Pack(const BitVector *packMask) throw ()
    { if (packMask != (BitVector *)NULL) Pack(*packMask); }

    // logging/recovery
    void Recover(RecoveryReader &rd)
      throw (VestaLog::Error, VestaLog::Eof)
    {
      List::Recover(rd);
      this->maxLen = this->len;
    }

    // write/read
    void Read(std::istream &ifs) throw (FS::EndOfFile, FS::Failure)
    {
      List::Read(ifs);
      this->maxLen = this->len;
    }

  private:
    Basics::uint32 maxLen;		  // size of "name"

    // hide the copy constructor from clients
    ListApp(const ListApp&);
  };
}

#endif // _FV_H
