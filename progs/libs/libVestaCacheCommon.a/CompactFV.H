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

// Created on Mon Mar 30 11:34:17 PST 1998 by heydon

#ifndef _COMPACT_FV_H
#define _COMPACT_FV_H

/* This class defines an alternative, compact representation
   for a sequence of names. The names are represented by a
   "PrefixTbl" paired with two arrays. The first array gives
   the indices of the names in the "PrefixTbl", and the second
   array gives the one-character prefix for each name. */

#include <Basics.H>
#include <SRPC.H>
#include <FS.H>
#include "FV.H"
#include "PrefixTbl.H"
#include "BitVector.H"

namespace CompactFV
{
  class List {
  private:
    void StoreIndex(Basics::uint32 pos, Basics::uint32 val);
  public:
    PrefixTbl tbl;		// table of names
    Basics::uint32 num;		// cardinality of "idx" and "types"
    Basics::uint32 *idx;	// indices of names in "tbl"; has length "num"
    Basics::uint16 *idx_sm;
    char *types;		// array of types; has length "num"
    // NOTE:
    //
    // - "idx" and "idx_sm" ar mutually exclusive; at least one must
    // be NULL
    // 
    // - "idx", "idx_sm", and "types" may all be NULL if "num == 0".

    List() throw ()
      : num(0), idx(0), idx_sm(0), types(0)
    { /*SKIP*/; }
    List(const FV::List &names) throw (PrefixTbl::Overflow);
    /* Initialize this "CompactFV::List" to represent the
       names "names". */

    List(SRPC &srpc) throw (SRPC::failure)
      : num(0), idx(0), idx_sm(0), types(0)
    { this->Recv(srpc); }
    List(std::ifstream &ifs, bool old_format = false)
      throw (FS::Failure, FS::EndOfFile)
      : num(0), idx(0), idx_sm(0), types(0)
    { this->Read(ifs, old_format); }

    Basics::uint32 GetIndex(Basics::uint32 pos) const throw ();

    // convert to FV::List
    void ToFVList(/*INOUT*/ FV::ListApp &fvl) const throw ();
    /* Append the names in this "CompactFV::List" to "fvl", which is
       required to be empty. */

    // write/read
    void Write(std::ostream &ofs, bool old_format = false)
      const throw (FS::Failure);
    void Read(std::istream &ifs, bool old_format = false)
      throw (FS::EndOfFile, FS::Failure);

    // send/receive
    void Send(SRPC &sprc, bool old_protocol = false) const throw (SRPC::failure);
    void Recv(SRPC &sprc, bool old_protocol = false) throw (SRPC::failure);

    // Test whether the old format can be used to write this into a
    // file.  (The same test determines whether a table can be sent over
    // the network with the old protocol.)
    inline bool CanWriteOld() const throw()
    {
      if(!this->tbl.CanWriteOld()) return false;
      if(this->idx != 0) return false;
      Basics::uint16 disk_num = this->num;
      return (((Basics::uint32) disk_num) == this->num);
    }

    // print
    void Print(std::ostream &os, int indent) const throw ();

    // Compare (used for testing)
    bool operator==(const List &other) const throw();
    inline bool operator!=(const List &other) const throw()
    {
      return !(*this == other);
    }
  };
}

#endif // _COMPACT_FV_H
