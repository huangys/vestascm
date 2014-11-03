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

// Created on Fri Nov  7 13:25:19 PST 1997 by heydon
// Last modified on Wed Aug  4 17:43:19 EDT 2004 by ken@xorian.net
//      modified on Mon Nov 10 12:41:28 PST 1997 by heydon

/* This interface defines the classes "Derived::Index" (a ShortId),
   "Derived::Indices", and "Derived::IndicesApp".

   IMPORTANT: Use of this interface assumes you are linking your
   application program with a garbage collector! Also, none of these
   classes defines an assignment operator, so objects are copied
   field-wise by default. This means that use of the assignment
   operator will produce aliases for those fields that are pointer
   types. */

#ifndef _DERIVED_H
#define _DERIVED_H

#include <Basics.H>
#include <FS.H>
#include <SRPC.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <SourceOrDerived.H>

namespace Derived
{
  // Derived index type
  typedef ShortId Index;

  // A list of derived indices
  class Indices {
  public:
    // data fields
    Index *index;		// array of indices
    Basics::int32 len;	// size of "index" array

    // constructors/destructor
    Indices() throw ()
      : len(0), index(NULL) { /*SKIP*/ }
    Indices(RecoveryReader &rd) throw (VestaLog::Error, VestaLog::Eof)
    { Recover(rd); }
    Indices(std::istream &ifs) throw (FS::EndOfFile, FS::Failure)
    { Read(ifs); }
    Indices(SRPC &srpc) throw (SRPC::failure)
    { Recv(srpc); }

    // size
    int Size() const throw ()
    { return sizeof(this->len) + (this->len * sizeof(*(this->index))); }

    // logging/recovery
    void Log(VestaLog &log) const throw (VestaLog::Error);
    void Recover(RecoveryReader &rd)
      throw (VestaLog::Error, VestaLog::Eof);

    // write/read
    void Write(std::ostream &ofs) const throw (FS::Failure);
    void Read(std::istream &ifs) throw (FS::EndOfFile, FS::Failure);

    // skip
    static int Skip(std::istream &ifs) throw (FS::EndOfFile, FS::Failure);
    /* Advance "ifs" past the "Derived::Indices" stored at its current
       position, and return the total number of bytes by which "ifs" was
       advanced. */

    // send/receive
    void Send(SRPC &srpc) const throw (SRPC::failure);
    void Recv(SRPC &srpc) throw (SRPC::failure);

    // print
    void Print(std::ostream &os, int indent) const throw ();

  private:
    // hide the copy constructor from clients
    Indices(const Indices&);
  };

  // an appendable list of derived indices
  class IndicesApp: public Indices {
  public:
    IndicesApp() throw ()
      : Indices(), maxLen(0) { /* SKIP */ }

    void Append(Index di) throw ();
    /* Append the index "di" to this list. */
  private:
    int maxLen;

    // hide the copy constructor from clients
    IndicesApp(const IndicesApp&);
  };
}

std::ostream& operator << (std::ostream &os, const Derived::Indices &dis) throw ();

#endif // _DERIVED_H
