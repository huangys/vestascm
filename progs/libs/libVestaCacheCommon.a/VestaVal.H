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

// Created on Fri Nov  7 14:27:48 PST 1997 by heydon

/* This interface defines the type "VestaVal::T", the representation
   of a Vesta function result value.

   IMPORTANT: Use of this interface assumes you are linking your
   application program with a garbage collector! Also, none of these
   classes defines an assignment operator, so objects are copied
   field-wise by default. This means that use of the assignment
   operator will produce aliases for those fields that are pointer
   types. */

#ifndef _VESTA_VAL_H
#define _VESTA_VAL_H

#include <Basics.H>
#include <FS.H>
#include <SRPC.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <FP.H>
#include "ImmutableVal.H"
#include "Derived.H"
#include "PrefixTbl.H"

class VestaVal {
  public:
    class T {
      public:
	// data fields
	FP::Tag fp;		// fingerprint
	Derived::Indices dis;	// reachable deriveds
        PrefixTbl prefixTbl;    // table of names used by pickle
	Basics::int32 len;	// number of pickled bytes
	char *bytes;		// array of pickled bytes

	// constructors/destructor
	T() throw ()
	    : len(0), bytes(NULL) { /*SKIP*/ }
	T(char *bytebuff, int len = -1) throw ();
	T(RecoveryReader &rd,
	  bool old_format = false) throw (VestaLog::Error, VestaLog::Eof)
            { Recover(rd, old_format); }
	T(std::istream &ifs,
	  bool old_format = false) throw (FS::EndOfFile, FS::Failure)
            { Read(ifs, old_format); }
        T(SRPC &srpc, bool old_protocol = false) throw (SRPC::failure)
            { Recv(srpc, old_protocol); }

        // size
        int Size() const throw ()
          { return sizeof(this->fp) + this->dis.Size()
	      + this->prefixTbl.MemorySize()
              + sizeof(this->len) + (this->len * sizeof(*(this->bytes))); }

	// logging/recovery
	void Log(VestaLog &log) const throw (VestaLog::Error);
	void Recover(RecoveryReader &rd,
		     bool old_format = false)
          throw (VestaLog::Error, VestaLog::Eof);

	// write/read
	void Write(std::ostream &ofs) const throw (FS::Failure);
	void Read(std::istream &ifs,
		  bool old_format = false) throw (FS::EndOfFile, FS::Failure);

        // read as an immutable value
        static ImmutableVal* ReadImmutable(std::istream &ifs,
					   bool old_format = false)
          throw (FS::EndOfFile, FS::Failure);

	// send/receive
        void Send(SRPC &srpc, bool old_protocol = false)
	  const throw (SRPC::failure);
        void Recv(SRPC &srpc, bool old_protocol = false)
	  throw (SRPC::failure);
 
	// print
	void Print(std::ostream &os, int indent) const throw ();
	/* Print the value as a sequence of lines (each field of the value
	   begins a new line), with each line indented by "indent" spaces. */
      private:
        // hide the copy constructor from clients
	T(const T&);
    };
};

#endif // _VESTA_VAL_H
