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

// Created on Fri Nov  7 09:54:23 PST 1997 by heydon

/* Defines the types "CacheEntry::Index", "CacheEntry::Indices",
   and "CacheEntry::IndicesApp".

   IMPORTANT: Use of this interface assumes you are linking your
   application program with a garbage collector! Also, none of these
   classes defines an assignment operator, so objects are copied
   field-wise by default. This means that use of the assignment
   operator will produce aliases for those fields that are pointer
   types. */

#ifndef _CACHE_INDEX_H
#define _CACHE_INDEX_H

#include <Basics.H>
#include <FS.H>
#include <SRPC.H>
#include <VestaLog.H>
#include <Recovery.H>
#include "ImmutableVal.H"

namespace CacheEntry
{
  typedef Basics::uint32 Index;
  class Indices;
}

std::ostream& operator << (std::ostream &os, const CacheEntry::Indices &cis) throw ();

namespace CacheEntry
{
  // A list of indices
  class Indices {
  public:
    // data fields
    CacheEntry::Index *index;      // array of indices
    Basics::int32 len;             // size of "index" array

    // constructor/destructor
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
    { return sizeof(this->len) +
	(this->len * sizeof(*(this->index))); }

    // log/recover
    void Log(VestaLog &log) const throw (VestaLog::Error);
    void Recover(RecoveryReader &rd)
      throw (VestaLog::Error, VestaLog::Eof);

    // write/read
    void Write(std::ostream &ofs) const throw (FS::Failure);
    void Read(std::istream &ifs) throw (FS::EndOfFile, FS::Failure);

    // read as an immutable value
    static ImmutableVal* ReadImmutable(std::istream &ifs)
      throw (FS::EndOfFile, FS::Failure);

    // send/receive
    void Send(SRPC &srpc) const throw (SRPC::failure);
    void Recv(SRPC &srpc) throw (SRPC::failure);

    // print
    void Print(std::ostream &os, int indent) const throw ();

  private:
    // hide the copy constructor from clients
    Indices(const Indices&);
  };

  // an appendable list of cache indices
  class IndicesApp: public Indices {
  public:
    // constructor
    IndicesApp() throw ()
      : Indices(), maxLen(0) { /* SKIP */ }

    // size
    int Size() const throw ()
    { return sizeof(this->len) + sizeof(this->maxLen) +
	(this->len * sizeof(*(this->index))); }

    void Append(Index ci) throw ();
    /* Append the index "ci" to this list. */

  private:
    int maxLen;

    // hide the copy constructor from clients
    IndicesApp(const IndicesApp&);
  };
}

#endif // _CACHE_INDEX_H
