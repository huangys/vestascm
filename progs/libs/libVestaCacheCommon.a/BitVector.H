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

// BitVector -- a zero-based indexable vector of bits
//
// A bit vector is a (zero-based) vector of Boolean values. There is no limit
// to the size of a bit vector, but their implementation was not designed to
// represent sparse vectors efficiently. In particular, the memory
// requirements for a bit vector "bv" are "O(sz)", where "sz" is the result
// returned by the method call "bv.Size()".
//
// Every bit in a bit vector has an index; the index of the least significant
// bit is 0. There are operations for reading the value of a bit at a given
// index, and for setting or resetting a bit at a specified index.
//
// The set bit with largest index is called the vector's most-significant bit.
// A bit is "valid" if its index is at most that of the most-significant bit.
// Setting a bit at an index larger than that of the vector's current
// most-significant bit automatically grows the vector, and resets any bits
// between the old and new most-significant bits.
//
// The methods of a "BitVector" are unmonitored. The read-only methods are
// denoted "const".

#ifndef _BIT_VECTOR_H
#define _BIT_VECTOR_H

#include <Basics.H>
#include <FS.H>
#include <SRPC.H>
#include <VestaLog.H>
#include <Recovery.H>

class BVIter;

class BitVector {
  public:
    BitVector(long sizeHint = 0) throw ()
	: numWords(0), sz(0), firstAvailWd(0), word((Word *)NULL)
	{ Init(sizeHint, /*doZero =*/ true); }
    /* Initialize a new bit vector. If supplied, "sizeHint" is a hint for the
       number of bits the vector is expected to contain. It is a checked
       run-time error for "sizeHint < 0". */

    BitVector(const BitVector *bv) throw ();
    /* Initialize a new bit vector to be a copy of "*bv". Notice that this is
       different from a copy constructor, but it provides a similar
       functionality. The true copy constructor is still private, so clients
       will not have it invisibly invoked by accident. */

    BitVector(RecoveryReader &rd) throw (VestaLog::Error, VestaLog::Eof)
	{ Recover(rd); }

    BitVector(std::istream &ifs) throw (FS::EndOfFile, FS::Failure)
	{ Read(ifs); }

    BitVector(SRPC &srpc) throw (SRPC::failure)
	{ Recv(srpc); }

    ~BitVector() throw () { if (word != (Word *)NULL) delete word; }
    /* De-allocate any space used by the bit vector. */

    bool IsEmpty() const throw ();
    /* Return "true" iff the bit vector has no set bits. */

    unsigned int Size() const throw () { return this->sz; }
    /* Return an upper bound on the bit vector's number of valid bits. Reading
       any bit with an index at least as large as the result is guaranteed to
       return "false". This operation takes O(1) time. */

    unsigned int Cardinality() const throw ();
    /* Return the number of set bits in this bit vector. This operation takes
       "O(sz)" time, where "sz" is the result returned by the "Size" method. */

    bool Read(unsigned int i) const throw ();
    /* Return the state of the vector's "i"'th bit. If the "i"th bit is
       invalid, return "false". This operation takes O(1) time. */ 

    inline bool Write(unsigned int i, bool val) throw ()
        { if (val) return Set(i); else return Reset(i); }
    /* Set the value of the "i"'th bit to "val". If the "i"'th bit was
       previously invalid and "val" is "true", extend the vector so that "i"
       is the new index of the vector's most-significant bit, and so that all
       of the other new bits are reset. Returns the value of bit "i" before
       it was written to "val".

       This operation takes O(1) time. */

    bool Set(unsigned int i) throw ();
    bool Reset(unsigned int i) throw ();
    /* These are equivalent to "Write(i, true)" and "Write(i, false)",
       respectively, but may be implemented more efficiently. */

    Word ReadWord(unsigned int start, unsigned short len) const throw ();
    /* Read the "len" consecutive bits starting at index "start", and return
       them as the low-order "len" bits of a "Word". It is a checked run-time
       error for "len" to exceed the number of bits in a "Word". */

    void WriteWord(unsigned int start, unsigned short len, Word val) throw ();
    /* Write the "len" low-order bits of "val" to the bit vector starting at
       index "start". It is a checked run-time error for "len" to exceed the
       number of bits in a "Word". */

    void WriteInterval(unsigned int lo, unsigned int hi, bool val) throw ()
	{ if (val) SetInterval(lo, hi); else ResetInterval(lo, hi); }
    /* Set all of the bits in the closed interval "[lo, hi]" to the value
       "val".  No change is made if "hi < lo". */

    void SetInterval(unsigned int lo, unsigned int hi) throw ();
    void ResetInterval(unsigned int lo, unsigned int hi) throw ();
    /* These are equivalent to "WriteInterval(lo, hi, true)" and
       "WriteInterval(lo, hi, false)", respectively, but may be implemented
       more efficiently. */

    void ResetAll(bool freeMem = false) throw ();
    /* Reset all the bits in the bit vector. If "freeMem" is true, then any
       memory allocated for the current bit vector will be freed. This is
       worthwhile if the current bit vector is large and is not expected to
       grow so large in the future. */

    unsigned int NextAvailExcept(BitVector *except = (BitVector *)NULL,
      bool setIt = true) throw ();
    /* Return the index of the least significant unset bit that is also unset
       in "except"; if "setIt" is true (the default), then also set that bit
       in this bit vector. If "except" is "NULL" (the default), it is as if it
       is a bit vector of all unset bits. */

    unsigned int NextAvail(bool setIt = true) throw ()
	{ return NextAvailExcept((BitVector *)NULL, setIt); }
    /* Return the index of the least significant unset bit; if "setIt" is
       "true" (the default), then also set that bit in this bit vector. */

    int MSB() const throw ();
    /* Return the index of this bit vector's most-significant bit,
       or -1 if it is empty. */

    void Pack(const BitVector &mask) throw ();
    /* REQUIRES SELF <= mask */
    /* to be written */

    void Pack(const BitVector *mask) throw ()
	{ if (mask != (BitVector *)NULL) Pack(*mask); }
    /* REQUIRES (mask != NULL) => (SELF <= *mask) */
    /* If "mask" is non-NULL, pack this bit vector according to "mask"; see
	the other "Pack" method above. */

    void Log(VestaLog &log) const throw (VestaLog::Error);
    /* Append this bit vector to "log", which must be in the "logging"
       state. */

    void Recover(RecoveryReader &rd) throw (VestaLog::Error, VestaLog::Eof);
    /* Recover this bit vector from "rd". */

    // write/read
    void Write(std::ostream &ofs) const throw (FS::Failure);
    void Read(std::istream &ifs) throw (FS::EndOfFile, FS::Failure);

    // send/recv
    void Send(SRPC &srpc) const throw (SRPC::failure);
    void Recv(SRPC &srpc) throw (SRPC::failure);

    // print
    friend std::ostream& operator << (std::ostream &os, const BitVector &fv) throw ()
      { fv.Print(os); return os; }
    void Print(std::ostream &s, int maxWidth = 64) const throw ();
    /* Write a (partial) representation of the bit vector to "s". If writing
       the complete representation would take more than "maxWidth" characters,
       "..." is written to indicate that the output is only partial. */
    void PrintAll(std::ostream &s, int indent = 0, int maxWidth = 70) const throw ();
    /* Write the complete contents of this bit vector to "s", indenting each
       output line by "indent" spaces. Each line is wrapped at a total of at
       most "maxWidth" characters. */

    // assignment operator
    BitVector& operator = (const BitVector& bv) throw ();

    // bitwise comparisons
    friend bool operator == (const BitVector& bv1, const BitVector& bv2)
      throw ();
    friend bool operator != (const BitVector& bv1, const BitVector& bv2)
      throw () { return !(bv1 == bv2); }
    friend bool operator <= (const BitVector& bv1, const BitVector& bv2)
      throw ();
    friend bool operator >= (const BitVector& bv1, const BitVector& bv2)
      throw () { return (bv2 <= bv1); }
    friend bool operator < (const BitVector& bv1, const BitVector& bv2)
      throw ();
    friend bool operator > (const BitVector& bv1, const BitVector& bv2)
      throw () { return (bv2 < bv1); }

    // bitwise operations
    friend BitVector* operator & (const BitVector& bv1, const BitVector& bv2)
      throw ();
    friend BitVector* operator | (const BitVector& bv1, const BitVector& bv2)
      throw ();
    friend BitVector* operator ^ (const BitVector& bv1, const BitVector& bv2)
      throw ();
    friend BitVector* operator - (const BitVector& bv1, const BitVector& bv2)
      throw ();
    /* These routines allocate and return a pointer to a new bit vector whose
       bits are the bitwise AND, OR, XOR, and difference of "bv1" and "bv2",
       respectively. */

    // destructive bitwise operations
    BitVector& operator &= (const BitVector& bv) throw ();
    BitVector& operator |= (const BitVector& bv) throw ();
    BitVector& operator ^= (const BitVector& bv) throw ();
    BitVector& operator -= (const BitVector& bv) throw ();

    friend class BVIter;

  private:
    Basics::uint16 numWords;	// size of "word" array
    Basics::uint16 firstAvailWd;	// see I4 below
    Basics::uint32 sz;		// upper bound on number of valid bits
    Word *word;			// array of words

    /* A newly constructed bit vector has "sz == 0". The following
       invariants hold for an initialized bit vector:  

|      I0. numWords >= 0
|      I1. numWords > 0 <==> word != NULL <==> numWords == NUMBER(*word)
|      I2. 0 <= sz <= (numWords * bitsPerWd)
|      I3. (forall i: sz <= i < (numWords * bitsPerWd): !bit(word, i))
|      I4. (forall i: 0 <= i < (firstAvailWd * bitsPerWd): bit(word, i))
|      I5. 0 <= firstAvailWd <= numWords

       where "bitsPerWd" is the number of bits per Word, and "bit(word, i)"
       is "true" iff the "i"th bit of the "word" array is set (where the
       index of the least significant bit is 0).

       By I1, the only words that can be read/written are those with indices
       in the interval "[0, numWords-1]".

       By I3, the bits in the "word" array that can possibly be non-zero are
       those with indices in the interval "[0, sz-1]".

       By I4, "firstAvailWd" is the index of the first word that may possibly
       contain bits that are not all set. Notice, however, that I4 does not
       in any way require "firstAvailWd" to be maximal. For example, I4 is
       satisfied trivially when "firstAvailWd == 0".

       By I3 and I4, we have: "firstAvailWd * bitsPerWd <= sz". */

    BitVector(long sizeHint, bool doZero) throw ()
	: numWords(0U), firstAvailWd(0U), sz(0U), word((Word *)NULL)
	{ Init(sizeHint, doZero); }
    /* See "Init" below. */

    void Init(long sizeHint, bool doZero) throw ();
    /* Initialize a new bit vector. If supplied, "sizeHint" is a hint for the
       number of bits the vector is expected to contain. It is a checked
       run-time error for "sizeHint < 0". If "doZero" is "false", then the
       words of the bit vector are not zeroed; it is the client's
       responsibility to ensure that all invariants (especially I3) are
       satisfied. */ 

    void Extend(unsigned short wordCnt, bool doPreserve = true) throw ();
    /* REQUIRES wordCnt > this->numWords */
    /* Extend the bit vector to include at least a total of "wordCnt"
       words. If "doPreserve" is "true", the contents of the bit vector
       are preserved, and all bits in the new words above the significant
       ones are guaranteed to be reset according to I3. Otherwise, the
       contents of the words are unspecified.

       This method may be called on a vector for which "numWords == 0" and
       "word == NULL", in which case it does the necessary allocation. */

    void ExpandSz(unsigned int i, unsigned short wx) throw ();
    /* Augment "sz" (and "word" if necessary) so as to contain bit "i"; "wx"
       must be the word containing that bit. */

    void ReduceSz() throw ();
    /* Reduce "sz" if any of the high-order words of the "word" array are
       zero. It's wise to perform this operation after possibly resetting
       some of the high-order bits of a bit-vector. */

    // make copy constructor inaccessible to clients
    BitVector(const BitVector& bv);
};

// BVIter -- An object for iterating over the set bits of a BitVector
//
// A "BVIter" object can be used to iterate over the set bits of a
// "BitVector". If "bv" is a "BitVector", then "BVIter(bv)" is a new
// iterator on the bit vector "bv"; it's "Next()" method can be used
// to return the indices of the set bits in "bv" in increasing order.
// The result of the "Next()" method is undefined if the iterator's
// underlying bit vector is changed after the iterator was constructed.

class BVIter {
  public:
    BVIter(const BitVector& bv) throw ()
	: bv(&bv), bitIndex(0), wordIndex(0), mask(1UL) { /*SKIP*/ }
    /* Initialize a new iterator on the bit vector "bv". */

    bool Next(/*OUT*/ unsigned int& res) throw ();
    /* Set "res" to the index of the next set bit in the iterator's bit vector
       if one exists, and return "true". Otherwise, leave "res" unchanged and
       return "false". */

  private:
    const BitVector *bv;	// the underlying bit vector
    unsigned int bitIndex;               // overall bit index
    unsigned int wordIndex;              // index into BitVector "word" array
    Word mask;			// bit mask
};

#endif // _BIT_VECTOR_H
