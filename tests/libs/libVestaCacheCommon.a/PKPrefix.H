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

// Last modified on Tue Aug  3 16:56:58 EDT 2004 by ken@xorian.net  
//      modified on Sat Feb 12 12:02:13 PST 2000 by mann  
//      modified on Mon Nov 10 12:41:26 PST 1997 by heydon

// PKPrefix -- A fingerprint prefix for indexing a table of MultiPKFiles.

#ifndef _PK_PREFIX_H
#define _PK_PREFIX_H

#include <Basics.H>
#include <SRPC.H>
#include <FP.H>

class PKPrefix {
  public:
    static int Granularity() throw ();
    /* Return the current granularity. */

    class T {
      public:
	T() throw () { /*SKIP*/ }
	/* Default constructor; leaves prefix uninitialized. */

	T(const FP::Tag& fp) throw ()
	    { this->w = fp.Word0(); Init(Granularity()); }
	/* Initialize the prefix from the fingerprint "fp". */

	T(const FP::Tag& fp, int gran) throw ()
	    { this->w = fp.Word0(); Init(gran); }
	/* Initialize the prefix from the fingerprint "fp" with
           granularity "gran". This method should only be used
           by test programs. */

        Text Pathname(int sigBits, int arcBits) const throw ();
	/* Return a (relative) pathname for the significant "sigBits" of this
	   PKPrefix. Each arc name is formed from "arcBits" bits. */

        /*** Write/Read ***/

	/* Write/read this prefix to/from the given stream. */
	void Write(std::ostream &ofs) const throw (FS::Failure)
	    { FS::Write(ofs, (char *)(&(this->w)), sizeof(this->w)); }
	void Read(std::istream &ifs) throw (FS::EndOfFile, FS::Failure)
	    { FS::Read(ifs, (char *)(&(this->w)), sizeof(this->w)); }

        /*** Send/Receive ***/

        /* Send/receive the prefix to/from "srpc". */
        void Send(SRPC &srpc) const throw (SRPC::failure)
	    { srpc.send_int64(this->w); }
        void Recv(SRPC &srpc) throw (SRPC::failure)
            { this->w = srpc.recv_int64(); }

	// operators
	friend std::ostream& operator << (std::ostream &os, const T &pfx) throw ();
	friend bool operator == (const T &pfx1, const T &pfx2) throw ()
	    { return pfx1.w == pfx2.w; }
	friend bool operator != (const T &pfx1, const T &pfx2) throw ()
	    { return pfx1.w != pfx2.w; }

	// hash
	Word Hash() const throw () { return this->w; };
	/* Return a hash value for this fingerprint. */

      private:
        Word w;

	void Init(int gran) throw();
        /* Initialize this prefix for granularity "gran". */
    };

    class List {
      public:
	PKPrefix::T *pfx;  // array of prefixes
	int len;           // size of "pfx" array

	List() throw () : len(0), pfx(NULL) { /*SKIP*/ }
	List(SRPC &srpc) throw (SRPC::failure) { Recv(srpc); }

	// write/read
	void Write(std::ostream &ofs) const throw (FS::Failure);
	void Read(std::istream &ifs) throw (FS::EndOfFile, FS::Failure);

	// send/receive
        void Send(SRPC &srpc) const throw (SRPC::failure);
        void Recv(SRPC &srpc) throw (SRPC::failure);

	// print
	void Print(std::ostream &os, int indent) const throw ();
      private:
	// hide copy constructor
	List(const List&);
    };
};

#endif // _PK_PREFIX_H
