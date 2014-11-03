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

#ifndef _PREFIX_TBL_H
#define _PREFIX_TBL_H

/* A "PrefixTbl" is a data structure for representing a set
   of pathnames. If the pathnames in the set share common
   prefixes, the "PrefixTbl" representation is quite compact.
   Each pathname in the table is identified by a non-negative
   integer. */

#include <Basics.H>
#include <Generics.H>
#include <SRPC.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <FS.H>
#include "FV2.H"

class PrefixTbl {
public:

  // This type is thrown as an exception when no more entries can be
  // added to a PrefixTbl.
  struct Overflow
  {
    Basics::uint32 numArcs;
    Overflow(Basics::uint32 arcs) : numArcs(arcs) { }
  };

  typedef Basics::uint32 index_t;

  // Index value used to mark the end of a chain in a PrefixTbl.
  static const Basics::uint32 endMarker;
  static const Basics::uint16 endMarker_sm;

  PrefixTbl() throw ()
  /* Construct a new, empty "PrefixTbl". */
    : numArcs(0), maxArcs(0),
      indexArray_sm(0), indexArray(0),
      arcArray(0)
  { /*SKIP*/};
  
  PrefixTbl(Basics::uint32 szHint) throw ();
  /* Construct a new, empty "PrefixTbl" with expected size "szHint". */

  PrefixTbl(std::istream &ifs,
	    bool old_format = false) throw (FS::EndOfFile, FS::Failure)
  { this->Read(ifs, old_format); }

  inline Basics::uint32 PrefixIndex(Basics::uint32 idx) const throw ()
  {
    if(idx > this->numArcs) return endMarker;
    if(this->indexArray_sm != 0)
      {
	assert(this->indexArray == 0);
	assert(this->numArcs <= small_max);
	if(this->indexArray_sm[idx] == endMarker_sm) return endMarker;
	return this->indexArray_sm[idx];
      }
    assert(this->indexArray != 0);
    return this->indexArray[idx];
  }
  /* Return the index of the prefix of the name with index "idx". */

  Text Arc(Basics::uint32 idx) const throw () { return this->arcArray[idx]; }
  /* Return the last arc of the pathname with index "idx". */

  Basics::uint32 NumArcs() const throw () { return this->numArcs; };
  /* Return the number of pathnames in the table. */

  // Table type used for building a PrefixTbl
  typedef Table<Text,Basics::uint32>::Default PutTbl;
  
  Basics::uint32 Put(const char* path, PutTbl /*INOUT*/ &tbl)
    throw (Overflow);
  Basics::uint32 Put(const FV2::T& path, PutTbl /*INOUT*/ &tbl)
    throw (Overflow);
  /* Insert the pathname "path" into the table, and return its
     integer identifier. The "tbl" argument is a table maintained
     by the client. Exactly one "tbl" should be used with each
     "PrefixTbl". On the first call to "Put", "tbl" should be
     empty, and the client should not perform any other modifications
     on "tbl" asside from passing it to "Put". Once the client is
     done calling "Put", "tbl" can be disposed of. */
  
  FV2::T *Get(Basics::uint32 idx) const throw ();
  /* Return the pathname with integer identifier "idx". */

  bool GetString(Basics::uint32 idx, char *buff, int buff_len) const throw ();
  /* If the pathname with integer identifier "idx" does not exceed
     "buff_len" (including the null termination character at the end
     of the pathname), write that pathname into "buff" and return
     true. Otherwise, return false. In the latter case, some
     characters in "buff" may have been modified. */

  Basics::uint64 MemorySize() const throw ();
  /* Return an upper bound on the size (in bytes) required to store
     this table in memory. */

  static Basics::uint64 Skip(std::istream &ifs,
			     bool old_format = false)
    throw (FS::EndOfFile, FS::Failure);
  /* Assumes "ifs" is positioned at a pickled "PrefixTbl". Skips
     over the table, and returns the number of bytes skipped. */
  
  // write/read
  void Write(std::ostream &ofs, bool old_format = false)
    const throw (FS::Failure);
  void Read(std::istream &ifs, bool old_format = false)
    throw (FS::EndOfFile, FS::Failure);

  // Test whether the old format can be used to write this into a
  // file.  (The same test determines whether a table can be sent over
  // the network with the old protocol.)
  inline bool CanWriteOld() const throw()
  { return (this->numArcs <= PrefixTbl::small_max); }

  // logging/recovery
  void Log(VestaLog &log, bool old_format = false)
    const throw (VestaLog::Error);
  void Recover(RecoveryReader &rd, bool old_format = false)
    throw (VestaLog::Error, VestaLog::Eof);

  // send/recv
  void Send(SRPC &srpc, bool old_protocol = false)
    const throw (SRPC::failure);
  void Recv(SRPC &srpc, bool old_protocol = false)
    throw (SRPC::failure);

  // print
  void Print(std::ostream &os, int indent = 0) const throw ();
  /* Print a representation of this table to "os", indented by "indent"
     characters. */

  bool operator==(const PrefixTbl &other) const throw();
  inline bool operator!=(const PrefixTbl &other) const throw()
  {
    return !(*this == other);
  }
protected:
  // The maximum number of entries we can keep in a 16-bit table
  static const Basics::uint32 small_max;

  /* Invariants:
     I0. 0 <= numArcs <= maxArcs
     I2. maxArcs > 0 => length(indexArray) == length(arcArray) == maxArcs
     I3. maxArcs == 0 <==> ((indexArray == NULL) /\ (arcArray == NULL))
  */
  Basics::uint32 numArcs;	// number of used table entries
  Basics::uint32 maxArcs;	// maximum number of table entries
  Basics::uint32 *indexArray;	// array for prefix indices
  Basics::uint16 *indexArray_sm;
  Text *arcArray;		// array for arcs
  
  Basics::uint32 AddArc(const Text& arc) throw (Overflow);
  /* Add the arc "arc" to the next position in "arcArray" (growing
     both "indexArray" and "arcArray" if necessary), and return its
     index in "arcArray". */

  void StoreIndex(Basics::uint32 pos, Basics::uint32 val)
    throw();
  void StoreEndMarker(Basics::uint32 pos) throw();

  // Write the individual entreis after the length has been written
  void WriteEntries(std::ostream &ofs)
    const throw (FS::Failure);
};

#endif // _PREFIX_TBL_H
