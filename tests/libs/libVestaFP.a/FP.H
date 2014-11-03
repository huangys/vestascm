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

// FP -- fingerprints and fingerprint tags

#ifndef _FP_H
#define _FP_H

/* A {\it fingerprint} is a 128-bit representation of a byte string. The
   representation is chosen so that identical strings have identical
   fingerprints, and so that different strings have different fingerprints
   with high probability. When the exact probability of a collision can be
   bounded, the fingerprint is said to satisfy the {\it probabilistic
   guarantee}.

   A fingerprint is simply the value of a string S viewed as a polynomial
   with coefficients in the field GF(2) modulo some irreducible polynomial P
   of degree 128. Unfortunately, fingerprints have the property that taking
   the fingerprint of the bytes comprising a fingerprint produces a
   fingerprint that no longer satisfies the probabilistic guarantee.

   However, a fingerprint can be scrambled by a random permutation to produce
   another 128-bit value called a {\it tag}. Tags have the property that tags
   of tags are secure with respect to the probabilistic guarantee. Hence,
   given strings "s1" and "s2", the following are all legal ways of combining
   the strings to form a single tag:

|    1. Tag t1(s1 + s2);           // where + denotes string concatenation
|    2. Tag t1(s1); t1.Extend(s2);
|    3. Tag t1(s1), t2(s2); t1.Extend(t2);
|    4. Tag t1(s1), t2(s2), t0(""); t0.Extend(t1); t0.Extend(t2);

   Techniques (1) and (2) produce equivalent tags. Techniques (3) and (4)
   are also legal. Technique (3) is probably the most convenient way to
   combine two tags.

   This interface exports a tagged fingerprint type "FP::Tag", a method for
   constructing tags from byte strings, and a function for extending an
   existing tag by a byte string. It also exports a type "FP::List" that
   represents a list of tagged fingerprints.

   IMPORTANT: Use of this interface assumes you are linking your application
   program with a garbage collector! Also, none of these classes defines an
   assignment operator, so objects are copied field-wise by default. This
   means that use of the assignment operator will produce aliases for those
   fields that are pointer types. */

#include <Basics.H>
#include <FS.H>
#include <SRPC.H>
#include <VestaLog.H>
#include <Recovery.H>
#include "Poly.H"

namespace FP {
  class Tag;
}

int Compare(const FP::Tag& fp1, const FP::Tag& fp2) throw ();

namespace FP {
    enum { ByteCnt = PolyVal::ByteCnt }; // tag bytes
    enum { WordCnt = PolyVal::WordCnt }; // tag words

    // True fingerprint type
    class Tag {
      public:
	/*** Constructors ***/

	Tag() throw () { /*SKIP*/ };
        /* Constructs a default (uninitialized) tag. WARNING: When this
           constructor is used, the bytes of the tag are undefined! To
           get the tag of the empty string, use 'Tag("", 0)'. */

	Tag(const char *s, int len = -1) throw ()
	    { if (len < 0) len = strlen(s); Init(s, len); }
	/* Constructs a new tag from the "len" bytes pointed to by "s". If
	   "len" is negative (defaulted), "s" is assumed to point to a
           null-terminated string, and the length of that string is used 
	   as the size of the buffer. */

	Tag(const Text &t) throw () { Init(t.cchars(), t.Length()); }
        /* Constructs a new tag from the characters of "t". */

	Tag(RecoveryReader &rd) throw (VestaLog::Error, VestaLog::Eof)
	    { Recover(rd); }

      Tag(std::istream &ifs) throw (FS::Failure, FS::EndOfFile)
	    { Read(ifs); }

	Tag(SRPC &srpc) throw (SRPC::failure)
	    { Recv(srpc); }

        /*** Extending Tags ***/

	Tag& Extend(const char *s, int len = -1) throw ();
        /* Destructively extend this tag by the "len" bytes pointed to by
	   "s" and return the resulting tag. A negative value for "len" is
           treated as described in the constructor above. */

        Tag& Extend(const Text &t) throw ()
	    { return this->Extend(t.cchars()); }
        /* Destructively extend this tag by the characters of "t". */

	Tag& Extend(const Tag &tag) throw ()
            {
	      unsigned char bytes[FP::ByteCnt];
	      tag.ToBytes(bytes);
	      return this->Extend((char *)bytes,
				  sizeof_assert(bytes, FP::ByteCnt));
	    }
        /* Destructively extend this tag by the bytes of "tag". */

	Tag& Extend(char c) throw ();
        /* Destructively extend this tag by the single character "c". */

        /*** Extending Raw Fingerprints ***/

	void Unpermute(/*OUT*/ RawFP &fp) const throw ();
        /* Destructively set "fp" to the raw fingerprint of this tag. */

        static void ExtendRaw(/*INOUT*/ RawFP &fp, const char *s, int len = -1)
          throw ();
        static void ExtendRaw(/*INOUT*/ RawFP &fp, const Text &t) throw ()
	    { ExtendRaw(/*INOUT*/ fp, t.cchars()); }
        static void ExtendRaw(/*INOUT*/ RawFP &fp, const Tag &tag) throw ()
	    {
	      unsigned char bytes[FP::ByteCnt];
	      tag.ToBytes(bytes);
	      ExtendRaw(/*INOUT*/ fp, (char *)(bytes),
			sizeof_assert(bytes, FP::ByteCnt));
	    }
        static void ExtendRaw(/*INOUT*/ RawFP &fp, char c) throw ();
	/* Extend the raw fingerprint "fp" by the bytes of "s", the characters
	    of "t", the bytes of "tag", and the character "c", respectively. */

	void Permute(const RawFP &f) throw ();
	/* Destructively set this "FP::Tag" to the tag of the raw
           fingerprint "f". */

        /* If you need to extend a tag by several byte sequences in a row,
           it is more efficient to unscramble the tag into its raw fingerprint
	   once, extend the raw fingerprint many times, and then scramble
           the raw fingerprint back into a tag once. These methods allow
           clients to do that. It is an unchecked error for these procedures
           to be used in any way other than a sequence of calls of the form:

             Unpermute  (ExtendRaw)+  Permute
        */

        /*** Logging/Recovery ***/

	void Log(VestaLog& log) const throw (VestaLog::Error)
	    {
	      unsigned char bytes[FP::ByteCnt];
	      this->ToBytes(bytes);
	      log.write((char *)bytes, FP::ByteCnt);
	    }
	/* Append this tag to "log". Requires "log" to be
	   in the "logging" state. */
 
        void Recover(RecoveryReader &rd) throw (VestaLog::Error, VestaLog::Eof)
	    {
	      unsigned char bytes[FP::ByteCnt];
	      rd.readAll((char *)bytes, FP::ByteCnt);
	      this->FromBytes(bytes);
	    }
        /* Recover this fingerprint from "rd". */

        /*** Write/Read ***/

	/* Write/read this fingerprint to/from the given stream. */
        void Write(std::ostream &ofs) const throw (FS::Failure)
	    {
	      unsigned char bytes[FP::ByteCnt];
	      this->ToBytes(bytes);
	      FS::Write(ofs, (char *)(bytes), sizeof(bytes));
	    }
        void Read(std::istream &ifs) throw (FS::EndOfFile, FS::Failure)
	    {
	      unsigned char bytes[FP::ByteCnt];
	      FS::Read(ifs, (char *)(bytes), sizeof(bytes));
	      this->FromBytes(bytes);
	    }

        /*** Send/Receive ***/

        /* Send/receive the fingerprint to/from "srpc". */
        void Send(SRPC &srpc) const throw (SRPC::failure)
	    {
	      unsigned char bytes[FP::ByteCnt];
	      this->ToBytes(bytes);
	      srpc.send_bytes((char *)(bytes),
			      sizeof_assert(bytes, FP::ByteCnt));
	    }
        void Recv(SRPC &srpc) throw (SRPC::failure)
	    { 
	      unsigned char bytes[FP::ByteCnt];
	      int len = sizeof(bytes);
	      srpc.recv_bytes_here((char *)(bytes), len);
	      assert(len == FP::ByteCnt);
	      this->FromBytes(bytes);
	    }

	Word Hash() const throw ();
	/* Return a hash value for this fingerprint. */

        void Print(std::ostream &os, const char *word_separator = " ")
	  const throw ();
        /* Print an ASCII representation of this tag. */

	// operators
        bool operator == (const Tag &other) const throw ();
        bool operator != (const Tag &other)const  throw ();

	// comparison
        friend int ::Compare(const Tag& fp1, const Tag& fp2) throw ();
        /* Return -1, 0, or 1 as "fp1" is less than, equal to, or greater
	   than "fp2". */

	// size
	int Size() const throw () { return FP::ByteCnt; }

        // Convert a tag to a sequence of bytes, or fill in a tag from
        // a sequence of bytes.  These functions should be used rather
        // than Words below for portability.
        void ToBytes(unsigned char *buffer) const throw ();
        void FromBytes(const unsigned char *buffer) throw ();

	// accessor (should not be used by most clients)
	Word *Words() throw () { return this->w; }
        Word Word0() const throw () { return this->w[0]; }
        Word Word1() const throw () { return this->w[1]; }

      protected:
	// representation -- two 64-bit words
	// clients should treat this field as read-only
	Word w[WordCnt];

      private:
	void Init(const char *s, int len) throw ();
        /* Initialize this tag from the "len" bytes pointed to by "s". */
    };

    // a list of tags
    class List {
      public:
	// data fields
	FP::Tag *fp;			// array of fingerprint values
	int len;                        // size of "fp" array

	// constructors/destructor
	List() throw ()
	    { len = 0; fp = (FP::Tag *)NULL; }
        List(int len) throw ()
	    {
	      this->len = len;
	      fp = NEW_PTRFREE_ARRAY(FP::Tag, len);
	    }
	List(RecoveryReader &rd) throw (VestaLog::Error, VestaLog::Eof)
	    { Recover(rd); }
        List(std::istream &ifs) throw (FS::EndOfFile, FS::Failure)
	    { Read(ifs); }
	List(SRPC &srpc) throw (SRPC::failure)
	    { Recv(srpc); }

	// size
	int Size() const throw ()
	    { return sizeof(this->len) + (this->len * sizeof(FP::Tag)); }

	// log/recover
	void Log(VestaLog& log) const throw (VestaLog::Error);
	void Recover(RecoveryReader &rd)
          throw (VestaLog::Error, VestaLog::Eof);

	// write/read
        void Write(std::ostream &ofs) const throw (FS::Failure);
        void Read(std::istream &ifs) throw (FS::EndOfFile, FS::Failure);
        static Basics::uint64 Skip(std::istream &ifs)
	  throw (FS::EndOfFile, FS::Failure);

        // send/receive
        void Send(SRPC &srpc) const throw (SRPC::failure);
        void Recv(SRPC &srpc) throw (SRPC::failure);

	// print
        void Print(std::ostream &os, int indent) const throw ();
	/* Print a list of fingerprints, each on its own line and indented by
	   "indent" spaces. */
      private:
        // hide the copy constructor from clients
	List(const List&);
    };

  void FileContents(std::istream &ifs, /*INOUT*/ FP::Tag &fp)
    throw (FS::Failure);
  /* Extend the fingerprint tag "fp" with the bytes of the stream "ifs".
     "ifs" is read until end-of-file is encountered. */
}

inline std::ostream& operator << (std::ostream &os, const FP::Tag &fp) throw ()
{
  fp.Print(os);
  return os;
}

#endif // _FP_H
