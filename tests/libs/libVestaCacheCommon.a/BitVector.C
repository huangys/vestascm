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
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1s307  USA

#include <Basics.H>
#include <FS.H>
#include <SRPC.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <BufStream.H>
#include "BitVector.H"

using std::endl;
using Basics::OBufStream;

/* When "BV_DEBUG" is defined, extra checks are performed to guarantee that
   the BitVector invariants are satisfied. These extra checks involve work
   that would still be performed even if assertion checking was disabled. */
#define BV_DEBUG

static const Word Zero = 0UL;
static const Word One = 1UL;
static const Word AllOnes = ~Zero;

// Read-only after initialization by BitVectorInit::BitVectorInit()
static int BitsPerWd = -1;   // number of bits per word
static int BytesPerWd;       // number of bytes per word (== "BitsPerWd / 8")
static unsigned int LogBitsPerWd;     // log (base 2) of "BitsPerWd"
static unsigned int LowBitsMask;
static Word *BitMask = NULL;


/* The above values are initialized by the BitVectorInit
   constructor. They are read-only after initialization. The meanings
   of the first three variables are described by their comments above.

   "LowBitsMask" is a bit mask whose low-order "LogBitsPerWd" bits are set
   (with all other bits reset). Hence, for all non-negative integers "i", we
   have: 

|    i / BitsPerWd == i >> LogBitsPerWd
|    i % BitsPerWd == i & LowBitsMask

   "BitMask" points to an array of "BitsPerWd" masks. There is one mask per
   bit; where "BitMask[i] = 1 << i" for "0 <= i < BitsPerWd".

   "IntrvlMask" acts like an array of "2 * BitsPerWd - 1" masks. For "i" in
   the interval "[-(BitsPerWd-1), (BitsPerWd-1)]", we have:

|    IntrvlMask[i] == AllOnes << i

   Notice that the index to the "IntrvlMask" array can be negative. A negative
   left shift is equivalent to a right shift of the negation of the shift
   amount, i.e., if "i < 0", then "IntrvlMask[i] == AllOnes >> (-i)". */

inline unsigned short WdIndex(unsigned int n) throw ()
/* Return "n DIV BitsPerWd". This is the index of the word that contains
   the bit numbered "n". */
{
    return (unsigned short)(n >> LogBitsPerWd);
}

inline unsigned short BitIndex(unsigned int n) throw ()
/* Return "n % BitsPerWd". This is the index of bit "n" within its word. */
{
    return (unsigned short)(n & LowBitsMask);
}

inline unsigned short WdCnt(unsigned int n) throw ()
/* Return ceiling(n / BitsPerWd). This is the number of words required
   to hold a total of "n" bits. */
{
    return WdIndex(n + BitsPerWd - 1);
}

class BitVectorInit {
  public:
    BitVectorInit() throw ();
    // initialize the "BitVector" module
};

static BitVectorInit bitVectorInit;

// This class implements "IntrvlMask" (described above). 
class IntervalMask
{
private:
  // "mask" actually points to the lowest entry (corresponding to the
  // index -(BitsPerWd-1)).  This is neccessary for the garbage
  // collector to see a valid reference to the block and not collect
  // it as garbage.
  static Word* mask;

public:
  // Init is separate from the constructor because it depends on
  // BitsPerWd
  static void Init() 
  {
    mask = NEW_PTRFREE_ARRAY(Word, (2 * BitsPerWd) - 1);
    Word* mask_middle = mask + (BitsPerWd - 1);
    Word left, right;
    mask_middle[0] = left = right = AllOnes;
    for (int i = 1; i < BitsPerWd; i++) {
      mask_middle[i] = (left <<= 1);
      mask_middle[-i] = (right >>= 1);
    }
  }

  Word operator[](int index)
  {
    // Note: assumes Init has already been called!
    return mask[(BitsPerWd - 1) + index];
  }
};

Word* IntervalMask::mask = 0;

// The sole instance of class IntervalMask
static IntervalMask IntrvlMask;

BitVectorInit::BitVectorInit() throw ()
/* Initialize the global variables of this module for this architecture. */
{
    assert(BitsPerWd < 0);
    unsigned int i;
    Word wd;
    
    // initialize "BitsPerWd", "BytesPerWd"
    for (BitsPerWd = 0, wd = One; wd != Zero; wd <<= 1) BitsPerWd++;
    BytesPerWd = BitsPerWd / 8;
    
    // initialize "LogBitsPerWd"
    for (LogBitsPerWd = 0, wd = One; wd < BitsPerWd; wd <<= 1) {
	LogBitsPerWd++;
    }
    
    // initialize "LowBitsMask"
    LowBitsMask = (unsigned int)(BitsPerWd - 1);
    
    // initialize "BitMask"
    BitMask = NEW_PTRFREE_ARRAY(Word, BitsPerWd);
    for (i = 0, wd = One; i < BitsPerWd; i++, wd <<= 1) BitMask[i] = wd;
    
    // initialize "IntrvlMask"
    IntervalMask::Init();
}

// Constructors/destructors ---------------------------------------------------

void BitVector::Init(long sizeHint, bool doZero) throw ()
{
    assert(this->sz == 0 && this->firstAvailWd == 0);
    assert(sizeHint >= 0);
    unsigned short wdCnt = WdCnt(sizeHint);
    if (wdCnt > this->numWords) {
	Extend(wdCnt, /*doPreserve=*/ false);
    }
    if (doZero) {
	for (unsigned int i = 0; i < this->numWords; i++) {
	    this->word[i] = Zero;
	}
    }
}

BitVector::BitVector(const BitVector *bv) throw ()
: numWords(0), firstAvailWd(0), sz(0), word((Word *)NULL)
{
    // invoke assignment operator
    *this = *bv;
}

// Extend/Reduce methods ------------------------------------------------------

void BitVector::Extend(unsigned short wordCnt, bool doPreserve /*=true*/) throw ()
{
    assert(wordCnt > this->numWords);
    unsigned short newNumWords = max(wordCnt, 2U * this->numWords);
    Word *newWord = NEW_PTRFREE_ARRAY(Word, newNumWords);

    // preserve bit vector if necessary
    if (doPreserve) {
	// copy old buffer to new if necessary
	if (this->word != NULL) {
	    memcpy((char *)newWord, (char *)(this->word),
	      this->numWords * BytesPerWd);
	    delete this->word;
	}

	// zero out new high words
	for (unsigned short i = this->numWords; i < newNumWords; i++) {
	    newWord[i] = Zero;
	}
    }

    // switch to new buffer
    this->numWords = newNumWords;
    this->word = newWord;
}

void BitVector::ExpandSz(unsigned int i, unsigned short wx) throw ()
{
    i++;
    if (wx < this->numWords) {
	// fast path -- we already have enough words
	this->sz = max(this->sz, i);
    } else {
	// slow path -- extend "word" array
	Extend(wx + 1);
	this->sz = i;
    }
}

void BitVector::ReduceSz() throw ()
{
    int i = ((int) WdCnt(this->sz)) - 1;
    assert(i < this->numWords);
    for (/* SKIP */; i >= 0 && this->word[i] == Zero; i--) /* SKIP */;
    this->sz = min(this->sz, ((unsigned int)(i+1)) * BitsPerWd);
    assert(this->firstAvailWd * BitsPerWd <= this->sz);
}

// Read/Write methods ---------------------------------------------------------

bool BitVector::IsEmpty() const throw ()
{
    // do constant-time checks first
    if (this->sz == 0) return true;
    if (this->firstAvailWd > 0) return false;

    // otherwise, iterate over all candidate words in the bit vector
    unsigned short inUseWds = WdCnt(this->sz);
    assert(inUseWds > 0 && this->word != (Word *)NULL);
    for (unsigned short i = 0; i < inUseWds; i++) {
	if (this->word[i] != Zero) return false;
    }
    /* The following is correct, but would require "IsEmpty" to be
       a non-const method. It is a benign side-effect in the case
       where the bit vector is empty. */
    // this->sz = 0;
    return true;
}

// BitCnt[i] is the number of bits set in the binary representation of
// the 8-bit value 'i'.
static const unsigned short BitCnt[256] = {
  /*   0 */  0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
  /*  16 */  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  /*  32 */  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  /*  48 */  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  /*  64 */  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  /*  80 */  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  /*  96 */  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  /* 112 */  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  /* 128 */  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  /* 144 */  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  /* 160 */  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  /* 176 */  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  /* 192 */  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  /* 208 */  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  /* 224 */  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  /* 240 */  4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8
};
static const int ByteMask = 0xff;

unsigned int BitVector::Cardinality() const throw ()
{
    // do constant-time checks first
    if (this->sz == 0) return 0;

    // iterate over words in bit vector
    unsigned short inUseWds = WdCnt(this->sz);
    unsigned int res = ((unsigned int)(this->firstAvailWd)) * BitsPerWd;
    for (unsigned short i = this->firstAvailWd; i < inUseWds; i++) {
	for (register Word wd = this->word[i]; wd != Zero; wd >>= 8) {
	    res += (unsigned int)(BitCnt[wd & ByteMask]);
	}
    }
    return res;
}

bool BitVector::Read(unsigned int i) const throw ()
{
    if (i >= this->sz) return false;
    unsigned short wx = WdIndex(i);
    if (wx < this->firstAvailWd) return true;
    return ((this->word[wx] & BitMask[BitIndex(i)]) != Zero);
}

Word BitVector::ReadWord(unsigned int start, unsigned short len) const throw ()
{
    Word res;
    assert(len <= BitsPerWd);
    if (start >= this->sz) return Zero;
    unsigned short startWd = WdIndex(start);
    unsigned short startBit = BitIndex(start);
    assert(startWd < this->numWords);
    if (startBit + len <= BitsPerWd) {
	// fast path -- bits all in one word
	Word mask = IntrvlMask[len - BitsPerWd];   // "len" 1's in low bits
	res = (this->word[startWd] >> startBit) & mask;
    } else {
	// slow path -- bits span word boundary
	unsigned short loLen = BitsPerWd - startBit;
	unsigned short hiLen = len - loLen;
	Word loMask = IntrvlMask[loLen - BitsPerWd]; // "loLen" 1's in low bits
	res = (this->word[startWd] >> startBit) & loMask;
	startWd++; // move on to next word
	if (startWd < WdCnt(this->sz)) {
	    assert(startWd < this->numWords);
	    Word hiMask = IntrvlMask[hiLen - BitsPerWd];
	    Word hiBits = this->word[startWd] & hiMask;
	    res |= (hiBits << loLen);
	}
    }
    return res;
}

void BitVector::WriteWord(unsigned int start, unsigned short len, Word val) throw ()
{
    assert(len <= BitsPerWd);
    if(len == 0) return;
    unsigned short startWd = WdIndex(start);
    unsigned short startBit = BitIndex(start);
    unsigned int end = (start + len) - 1; // index of last bit being set
    unsigned short endWd = WdIndex(end);

    // update "sz", "firstAvailWd" -- extend bit vector if necessary
    ExpandSz(end, endWd);
    this->firstAvailWd = min(this->firstAvailWd, startWd);

    // set the bits
    if (startWd == endWd) {
	// fast path -- bits all in one word
	assert(startBit + len <= BitsPerWd);
	Word mask = IntrvlMask[len - BitsPerWd];
	val &= mask;		        // clear any unwanted bits in "val"
	mask <<= startBit;              // shift mask up to proper position
	this->word[startWd] &= ~mask;   // reset target bits
	this->word[startWd] |= (val << startBit);
    } else {
	// slow path -- bits span word boundary
	unsigned short loLen = BitsPerWd - startBit;
	unsigned short hiLen = len - loLen;
	Word loMask = IntrvlMask[BitsPerWd - loLen];
	Word hiMask = IntrvlMask[hiLen - BitsPerWd];

        // lo word
	this->word[startWd] &= ~loMask;
	this->word[startWd] |= (val << startBit);

	// hi word
	startWd++; // move on to next word
	assert(startWd < this->numWords);
	Word hiVal = (val >> loLen) & hiMask; // clear any unwanted bits
	this->word[startWd] &= ~hiMask;
	this->word[startWd] |= hiVal;
    }
}

bool BitVector::Set(unsigned int i) throw ()
{
    register unsigned short wx = WdIndex(i), bx = BitIndex(i);
    ExpandSz(i, wx);
    assert(wx < this->numWords);
    bool res = ((this->word[wx] & BitMask[bx]) != Zero);
    if (!res) { this->word[wx] |= BitMask[bx]; }
    return res;
}

bool BitVector::Reset(unsigned int i) throw ()
{
    register unsigned short wx = WdIndex(i), bx = BitIndex(i);
    bool res;
    if (i < this->sz) {
	assert(wx < this->numWords);
	res = ((this->word[wx] & BitMask[bx]) != Zero);
	if (res) {
	    this->word[wx] &= ~(BitMask[bx]);
	    this->firstAvailWd = min(this->firstAvailWd, wx);
	}
    } else {
	res = false;
    }
    return res;
}

void BitVector::SetInterval(unsigned int lo, unsigned int hi) throw ()
{
    if(hi < lo) return;  // Nothing to do in this case.

    unsigned short wxLo = WdIndex(lo), bxLo = BitIndex(lo);
    unsigned short wxHi = WdIndex(hi), bxHi = BitIndex(hi);

    // extend "sz" (and "word" if necessary)
    ExpandSz(hi, wxHi);
    assert(wxHi < this->numWords);

    // set the bits
    if (wxLo == wxHi) {
	// all bits in single word
	Word mask = IntrvlMask[(bxHi-bxLo+1) - BitsPerWd] << bxLo;
	this->word[wxLo] |= mask;
    } else {
	// bits span multiple words
	register unsigned short i;

	// partial low word
	if (wxLo < this->firstAvailWd ||
            (bxLo == 0 && wxLo == this->firstAvailWd)) {
	    // low bits are already set by I4
	    i = this->firstAvailWd;
	    /* (The next statement anticipates the final result; it is
               not true at this instant.) */
	    this->firstAvailWd = max(this->firstAvailWd, wxHi);
	    assert(this->firstAvailWd * BitsPerWd <= this->sz);
	} else {
	    // normal case
	    this->word[wxLo] |= IntrvlMask[bxLo];
	    i = wxLo + 1;
	}

	// middle full words
	while (i < wxHi) {
	    this->word[i++] = AllOnes;
	}

	// partial hi word
	if (i == wxHi) { // guard needed in case "this->firstAvailWd > wxHi"
	    this->word[i] |= IntrvlMask[(bxHi+1) - BitsPerWd];
	}
    }
}

void BitVector::ResetInterval(unsigned int lo, unsigned int hi) throw ()
{
    if(lo >= this->sz) return;

    // reduce "hi" if necessary by I3
    hi = min(hi, this->sz - 1);

    if (lo <= hi) {
        unsigned short wxLo = WdIndex(lo), bxLo = BitIndex(lo);
	unsigned short wxHi = WdIndex(hi), bxHi = BitIndex(hi);

	// reset the bits
	assert(wxHi < this->numWords);
	if (wxLo == wxHi) {
	    // all bits in single word
	    Word mask = IntrvlMask[(bxHi-bxLo+1) - BitsPerWd] << bxLo;
	    this->word[wxLo] &= ~mask;
	} else {
	    // bits span multiple words
	    register unsigned short i;

	    // partial low word
	    this->word[wxLo] &= ~(IntrvlMask[bxLo]);
	    i = wxLo + 1;

	    // middle full words
	    while (i < wxHi) {
		this->word[i++] = Zero;
	    }

	    // partial hi word
	    Word mask = IntrvlMask[(bxHi+1) - BitsPerWd];
	    this->word[i] &= ~mask;
	}

	// reduce "this->sz" if possible
	if (hi == this->sz - 1) {
	    this->sz = min(this->sz, lo);
	}

	// reduce "this->firstAvailWd" if necessary
	this->firstAvailWd = min(this->firstAvailWd, wxLo);
    }
}

void BitVector::ResetAll(bool freeMem) throw ()
{
    if (this->word != NULL) {
	if (freeMem) {
	    // maintain I1
	    delete this->word;
	    this->word = (Word *)NULL;
	    this->numWords = 0;
	} else {
	    // maintain I3
	    unsigned short wdCnt = WdCnt(this->sz);
	    assert(wdCnt <= this->numWords);
	    for (unsigned short i = 0; i < wdCnt; i++) {
		this->word[i] = Zero;
	    }
	}
    }
    this->sz = 0;

    // maintain I4
    this->firstAvailWd = 0;
}

const Word Magic = CONST_INT_64(0x07edd5e59a4e28c2);
const int MagicTable[] = {
  63, 0, 58, 1, 59, 47, 53, 2, 60, 39, 48, 27, 54, 33, 42, 3, 61, 51, 37,
  40, 49, 18, 28, 20, 55, 30, 34, 11, 43, 14, 22, 4, 62, 57, 46, 52, 38, 26,
  32, 41, 50, 36, 17, 19, 29, 10, 13, 21, 56, 45, 25, 31, 35, 16, 9, 12, 44,
  24, 15, 8, 23, 7, 6, 5
};

unsigned int BitVector::NextAvailExcept(BitVector *except, bool setIt) throw ()
{
    unsigned int res;
    unsigned short maxWdExc; // only valid if "except != NULL"
    unsigned short wx = this->firstAvailWd; // word in which result bit will be set

    // find first word containing any 0's
    unsigned short maxWd = WdCnt(this->sz);
    while ((wx < maxWd) && (this->word[wx] == AllOnes)) wx++;
    this->firstAvailWd = wx;

    // advance further if "except != NULL"
    if (except != (BitVector *)NULL) {
	// advance "wx"
	wx = max(wx, except->firstAvailWd);
	maxWdExc = WdCnt(except->sz);

	// advance where both bit vectors are long enough
	unsigned short minWd = min(maxWd, maxWdExc);
	while ((wx < minWd) && ((this->word[wx] | except->word[wx])==AllOnes))
	    wx++;

	// advance through longer of the two
	if (wx == minWd) {
	    if (maxWdExc > maxWd) {
		while ((wx < maxWdExc) && (except->word[wx] == AllOnes)) wx++;
	    } else if (maxWd > maxWdExc) {
		while ((wx < maxWd) && (this->word[wx] == AllOnes)) wx++;
	    }
	}
    }

    // compute word in which to find 0
    res = ((unsigned int)wx) * BitsPerWd;
    Word wd = (wx < maxWd) ? this->word[wx] : Zero;
    if (except != NULL && wx < maxWdExc) {
	wd |= except->word[wx];
    }
    assert(wd != AllOnes);

    // find the first 0 in "wd"; set "bx" to its index within the word
    unsigned short bx = 0;
    if (wd != Zero) {
	// complement; now find a 1
	wd = ~wd;
	assert(wd != Zero);
#ifdef SLOW
	// I believe this unused code is supposed to be equivalent to
	// the following code which uses MagicTable

	short maskSz = BitsPerWd >> 1; // == BitsPerWd / 2
	Word mask = IntrvlMask[-maskSz];
	while (maskSz > 0) {
	    if (!(wd & mask)) {
		bx += maskSz; wd >>= maskSz;
	    }
	    maskSz >>= 1; mask >>= maskSz;
	}
#endif
	wd = wd & ~(wd - 1); // reset all bits above least set bit
	bx = MagicTable[(wd * Magic) >> (BitsPerWd - LogBitsPerWd)];
	res += (unsigned int)bx;
    }

    // set the bit if necessary
    if (setIt) {
	ExpandSz(res, wx);
	this->word[wx] |= BitMask[bx];
    }
    return res;
}

int BitVector::MSB() const throw ()
{
    // find the non-zero word with largest index
    int wx = ((int) WdCnt(this->sz)) - 1; assert(wx < this->numWords);
    for (/*SKIP*/; wx >= 0 && this->word[wx] == Zero; wx--) /*SKIP*/;

    // return immediately if vector is empty
    if (wx < 0) return -1;

    // use binary search to find MSB in "this->word[wx]"
    unsigned int res = ((unsigned int)wx) * BitsPerWd;
    Word w = this->word[wx]; assert(w != Zero);
    unsigned int maskSz = BitsPerWd >> 1; // == BitsPerWd / 2
    Word mask = IntrvlMask[maskSz];
    while (maskSz > 0) {
	if (w & mask) {
	    res += maskSz; 
	} else {
	    w <<= maskSz;
	}
	maskSz >>= 1; mask <<= maskSz;
    }
    return res;
}

void BitVector::Pack(const BitVector &m) throw ()
/* REQUIRES SELF <= m */
{
    unsigned short wdCnt = WdCnt(this->sz), mWdCnt = WdCnt(m.sz);
    assert(wdCnt <= this->numWords && mWdCnt <= m.numWords);
    unsigned short minWdCnt = min(wdCnt, mWdCnt);

    // we only have to consider bits in words at least "m.firstAvailWd"
    unsigned short wx = m.firstAvailWd;           // index of result word

    // return immediately if possible
    if (wx >= wdCnt) return;

    // otherwise, pack the bits
    const Word ZeroBit = BitMask[0];
    unsigned int bx = 0; Word thisBit = ZeroBit; // index and mask for result bit
    unsigned short mwx;                            // index of source word
    for (mwx = wx; mwx < minWdCnt; mwx++) {
    	/* Loop invariants:
    	    I1. mwx < wdCnt && mwx < mWdCnt
    	    I2. wx <= mwx
    	    I3. thisBit = BitMask[bx]
        */
	unsigned int mbx;     // index of source bit within word "mwx"; like "bx"
	Word mBit;   // mask for source bit (see I4 below); like "thisBit"
	Word mMask;  // shifted word (see I5 below)
	for (mbx = 0, mMask = m.word[mwx], mBit = ZeroBit;
             mMask != Zero; mbx++, mMask >>= 1, mBit <<= 1) {
            /* Loop invariants:
                I4. mBit == BitMask[mbx]
                I5. mMask == m.word[mwx] >> mbx
            */
	    if (mMask & ZeroBit) {
		// set bit this(wx, thisBit) to this(mwx, mBit)
		if (this->word[mwx] & mBit) {
		    // set the bit
		    this->word[wx] |= thisBit;
		} else {
		    // reset the bit
		    this->word[wx] &= ~thisBit;
		}
		// advance (wx, thisBit)
		bx++; thisBit <<= 1;
		if (thisBit == Zero) {
		    bx = 0; thisBit = ZeroBit;
		    wx++;
		}
	    } else {
		// check precondition
		assert(!(this->word[mwx] & mBit));
	    }
	}

	// if on the last source word...
	if (mwx == (minWdCnt - 1) && mbx < BitsPerWd) {
	    // ...check that remainder of "this->word[mwx]" (if any) is zero
	    assert((this->word[mwx] & IntrvlMask[mbx]) == Zero);
	}
    }
    assert(mwx == minWdCnt && wx <= mwx);

    // zero out bits in half-open interval [(wx, bx), (mwx, 0))
    if (wx < mwx) {
	this->word[wx] &= ~(IntrvlMask[bx]);               // rest of this word
        for (wx++; wx < mwx; wx++) this->word[wx] = Zero;  // whole words
    }
    assert(wx == mwx);

#ifdef BV_DEBUG
    // check that remainder of bits in this bit vector are Zero
    for (/*SKIP*/; wx < wdCnt; wx++)
	assert(this->word[wx] == Zero);
#endif // BV_DEBUG
}

BitVector& BitVector::operator = (const BitVector& bv) throw ()
{
    unsigned short copyWds = WdCnt(bv.sz); // number of words to copy
    unsigned short lastNonZero; // upper bound on index of last word to be zeroed

    // allocate the words array
    if (copyWds > this->numWords) {
	Extend(copyWds, /*doPreserve=*/ false);
	// we have to zero all the way to end of the array
	lastNonZero = this->numWords;
    } else {
	/* We only have to zero up to the last word that might contain
           set bits; the words past that are zero by I3. */
	lastNonZero = WdCnt(this->sz);
    }
    assert(copyWds <= this->numWords && lastNonZero <= this->numWords);

    // update sizes
    this->sz = bv.sz;
    this->firstAvailWd = bv.firstAvailWd;

    // copy valid words and zero remaining ones
    unsigned short i;
    for (i = 0; i < copyWds; i++) { this->word[i] = bv.word[i]; }
    for (/*SKIP*/; i < lastNonZero; i++) { this->word[i] = Zero; }

#ifdef BV_DEBUG
    // verify I3
    for (/*SKIP*/; i < this->numWords; i++)
	assert(this->word[i] == Zero);
#endif // BV_DEBUG

    return *this;
}

// Bitwise operator methods ---------------------------------------------------

bool operator == (const BitVector& bv1, const BitVector& bv2) throw ()
{
    if (bv1.sz == 0) {
	return bv2.IsEmpty();
    } else if (bv2.sz == 0) {
	return bv1.IsEmpty();
    }
    assert((bv1.word != (Word *)NULL) && (bv2.word != (Word *)NULL));
    unsigned short wds1 = WdCnt(bv1.sz), wds2 = WdCnt(bv2.sz);
    unsigned short minWds = min(wds1, wds2);
    unsigned short firstWd = min(bv1.firstAvailWd, bv2.firstAvailWd);

    // check that words agree where they have words in common
    unsigned short i;
    for (i = firstWd; i < minWds; i++) {
	if (bv1.word[i] != bv2.word[i]) return false;
    }

    // check that remainder of longer word is all Zero
    if (wds1 > wds2) {
	for (/*SKIP*/; i < wds1; i++) {
	    if (bv1.word[i] != Zero) return false;
	}
    } else if (wds2 > wds1) {
	for (/*SKIP*/; i < wds2; i++) {
	    if (bv2.word[i] != Zero) return false;
	}
    }
    return true;
}

bool operator <= (const BitVector& bv1, const BitVector& bv2) throw ()
/* The implementation works by first determining how many words "bv1" and
   "bv2" have in common. For each pair of words in common, we return false
   immediately if the word of "bv1" has a bit set that is not set in the
   corresponding word of "bv2". If "bv1" has more words than "bv2", we must
   then also check that all extra words of "bv1" are zero. */
{
    // check for simple, special case
    if (bv1.sz == 0) return true;
    assert(bv1.word != (Word *)NULL);

    unsigned short wds1 = WdCnt(bv1.sz), wds2 = WdCnt(bv2.sz);
    unsigned short minWds = min(wds1, wds2);

    // Check that "bv1"'s bits are a subset of "bv2"'s for all common words;
    // we don't have to check the first "firstAvailWd" words of "bv2", since
    // all their bits are set, and so the corresponding words of "bv1" are
    // guaranteed to be a subset.
    unsigned short i;
    for (i = bv2.firstAvailWd; i < minWds; i++) {
	if (bv1.word[i] & ~(bv2.word[i])) return false;
    }

    // check that any extra words of "bv1" are zero
    for (/*SKIP*/; i < wds1; i++) {
	if (bv1.word[i] != Zero) return false;
    }

    // if both tests pass, return "true"
    return true;
}

bool operator < (const BitVector& bv1, const BitVector& bv2) throw ()
{
    // check for special cases
    if (bv2.IsEmpty()) return false;
    if (bv1.IsEmpty()) return true;
    assert((bv1.word != (Word *)NULL) && (bv2.word != (Word *)NULL));

    /* First, check if there are any unset bits in the first
       "bv2.firstAvailWd" words of "bv1". The search can start
       at word "bv1.firstAvailWd" of "bv1" because all words
       before that are known to be all ones by I4. */
    unsigned short i;
    for (i = bv1.firstAvailWd; i < bv2.firstAvailWd; i++) {
	if (bv1.word[i] != AllOnes) return true;
    }

    // Next, look over words that agree
    unsigned short wds1 = WdCnt(bv1.sz), wds2 = WdCnt(bv2.sz);
    unsigned short minWds = min(wds1, wds2);
    for (/*SKIP*/; i < minWds; i++) {
	if (bv1.word[i] & ~(bv2.word[i])) return false;
	// now, every bit set in "bv1.word[i]" is also set in "bv2.word[i]"
	if (bv1.word[i] != bv2.word[i]) return true;
    }

    // common words are equal; check for set bits in bv2 not set in bv1
    for (/*SKIP*/; i < wds2; i++) {
	if (bv2.word[i] != Zero) return true;
    }
    return false;
}

BitVector* operator & (const BitVector& bv1, const BitVector& bv2) throw ()
{
    unsigned int newSz = min(bv1.sz, bv2.sz);
    BitVector *res = NEW_CONSTR(BitVector, (newSz, /*doZero =*/ false));

    // initialize sizes
    res->sz = newSz;
    res->firstAvailWd = min(bv1.firstAvailWd, bv2.firstAvailWd);
    assert(res->firstAvailWd * BitsPerWd <= res->sz);

    unsigned short i, wdCnt1 = WdCnt(bv1.sz), wdCnt2 = WdCnt(bv2.sz);
    if (newSz > 0) { // work only necessary if both non-empty
	// set initial "firstAvailWd" words
	assert(res->firstAvailWd <= res->numWords);
	for (i = 0; i < res->firstAvailWd; i++) {
	    res->word[i] = AllOnes;
	}

	// compute conjunction for words in common
	unsigned short minWds = min(wdCnt1, wdCnt2);
	assert(minWds <= res->numWords);
	for (/*SKIP*/; i < minWds; i++) {
	    res->word[i] = bv1.word[i] & bv2.word[i];
	}
    } else {
	assert(res->firstAvailWd == 0);
	i = 0;
    }

    // zero out the rest (in case "res->numWords > minWds")
    for (/*SKIP*/; i < res->numWords; i++) {
	res->word[i] = Zero;
    }

    // reduce size if any high-order Zero words
    res->ReduceSz();
    return res;
}

BitVector& BitVector::operator &= (const BitVector& bv) throw ()
{
    unsigned short wdCnt = WdCnt(this->sz);
    if (wdCnt > 0) {		// work only necessary if *this is non-empty
	// set sizes
	unsigned short wdCnt2 = WdCnt(bv.sz);
	this->sz = min(this->sz, bv.sz);
	this->firstAvailWd = min(this->firstAvailWd, bv.firstAvailWd);

	// compute conjunction for words in common
	unsigned short i, minWds = min(wdCnt, wdCnt2);
	for (i = this->firstAvailWd; i < minWds; i++) {
	    this->word[i] &= bv.word[i];
	}

	// zero the rest (in case this->numWords > bv.numWords)
	for (/*SKIP*/; i < wdCnt; i++) {
	    this->word[i] = Zero;
	}
	ReduceSz();
    }
    return *this;
}

BitVector* operator | (const BitVector& bv1, const BitVector& bv2) throw ()
{
    unsigned int newSz = max(bv1.sz, bv2.sz);
    BitVector *res = NEW_CONSTR(BitVector, (newSz, /*doZero =*/ false));

    // initialize sizes
    res->sz = newSz;
    res->firstAvailWd = max(bv1.firstAvailWd, bv2.firstAvailWd);
    assert(res->firstAvailWd * BitsPerWd <= res->sz);

    unsigned short i, wdCnt1 = WdCnt(bv1.sz), wdCnt2 = WdCnt(bv2.sz);
    if (newSz > 0) { // work only necessary if either non-empty
	// set initial "firstAvailWd" words
	assert(res->firstAvailWd <= res->numWords);
	for (i = 0; i < res->firstAvailWd; i++) {
	    res->word[i] = AllOnes;
	}

	// compute disjunction for words in common
	unsigned short minWds = min(wdCnt1, wdCnt2);
	assert(minWds <= res->numWords);
	for (/*SKIP*/; i < minWds; i++) {
	    res->word[i] = bv1.word[i] | bv2.word[i];
	}

	// copy from longer bit-vector
	unsigned short maxWds = max(wdCnt1, wdCnt2);
	assert(maxWds <= res->numWords);
	Word *wds = (wdCnt1 > wdCnt2) ? bv1.word : bv2.word;
	for (/*SKIP*/; i < maxWds; i++) {
	    res->word[i] = wds[i];
	}
    } else {
	assert(res->firstAvailWd == 0);
	i = 0;
    }

    // zero out the rest (in case "res->numWords > maxWds")
    for (/*SKIP*/; i < res->numWords; i++) {
	res->word[i] = Zero;
    }
    return res;
}

BitVector& BitVector::operator |= (const BitVector& bv) throw ()
{
    unsigned short wdCnt2 = WdCnt(bv.sz);
    if (wdCnt2 > 0) {		// work only necessary if "bv" is non-empty
	// extend "word" array if necessary
	if (wdCnt2 > this->numWords) Extend(wdCnt2);
	assert(wdCnt2 <= this->numWords);

	// update sizes
	this->sz = max(this->sz, bv.sz);
	unsigned short i = this->firstAvailWd;
	this->firstAvailWd = max(this->firstAvailWd, bv.firstAvailWd);

	// set more "firstAvailWd" words if necessary
	for (/*SKIP*/; i < this->firstAvailWd; i++) {
	    this->word[i] = AllOnes;
	}

	// compute disjunction for words in common
	unsigned short wdCnt = WdCnt(this->sz);
	unsigned short minWds = min(wdCnt, wdCnt2);
	for (/*SKIP*/; i < minWds; i++) {
	    this->word[i] |= bv.word[i];
	}

	// copy from "bv" if it is longer
	for (/*SKIP*/; i < wdCnt2; i++) {
	    this->word[i] = bv.word[i];
	}
    }
    return *this;
}

BitVector* operator ^ (const BitVector& bv1, const BitVector& bv2) throw ()
{
    unsigned int newSz = max(bv1.sz, bv2.sz);
    BitVector *res = NEW_CONSTR(BitVector, (newSz, /*doZero =*/ false));

    // initialize sizes
    res->sz = newSz;
    res->firstAvailWd = 0;

    unsigned short i;
    if (newSz > 0) {
	// compute exclusive-OR over common words
	unsigned short wdCnt1 = WdCnt(bv1.sz), wdCnt2 = WdCnt(bv2.sz);
	unsigned short minWds = min(wdCnt1, wdCnt2);
	for (i = 0; i < minWds; i++) {
	    res->word[i] = bv1.word[i] ^ bv2.word[i];
	}

	// copy remaining words of longer vector
	if (wdCnt1 > wdCnt2) {
	    assert(wdCnt1 <= res->numWords);
	    for (/*SKIP*/; i < wdCnt1; i++) {
		res->word[i] = bv1.word[i];
	    }
	} else if (wdCnt2 > wdCnt1) {
	    assert(wdCnt2 <= res->numWords);
	    for (/*SKIP*/; i < wdCnt2; i++) {
		res->word[i] = bv2.word[i];
	    }
	}
    } else {
	i = 0;
    }

    // zero out the rest (in case constructor allocates more than necessary)
    for (/*SKIP*/; i < res->numWords; i++) {
	res->word[i] = Zero;
    }
    return res;
}

BitVector& BitVector::operator ^= (const BitVector& bv) throw ()
{
    unsigned short wdCnt2 = WdCnt(bv.sz);
    if (wdCnt2 > 0) {		// work only necessary if "bv" is non-empty
	// extend "word" array if necessary
	if (wdCnt2 > this->numWords) Extend(wdCnt2);
	assert(wdCnt2 <= this->numWords);

	// initialize sizes
	this->sz = max(this->sz, bv.sz);
	this->firstAvailWd = 0;

	// compute XOR of words in common
	unsigned short i, wdCnt = WdCnt(this->sz);
	unsigned short minWds = min(wdCnt, wdCnt2);
	for (i = 0; i < minWds; i++) {
	    this->word[i] ^= bv.word[i];
	}

	// if "bv" is longer, copy its words
	for (/*SKIP*/; i < wdCnt2; i++) {
	    this->word[i] = bv.word[i];
	}
    }
    return *this;
}

BitVector* operator - (const BitVector& bv1, const BitVector& bv2) throw ()
{
    unsigned int newSz = bv1.sz;   // the result is the same size as "bv1"
    BitVector *res = NEW_CONSTR(BitVector, (newSz, /*doZero =*/ false));

    // initialize sizes
    res->sz = newSz;
    res->firstAvailWd = 0;

    // subtract where they have words in common
    unsigned short i, wdCnt2 = WdCnt(bv2.sz);
    if (newSz > 0) {		// work only necessary if "bv1" is non-empty
	unsigned short wdCnt1 = WdCnt(bv1.sz);
	unsigned short minWds = min(wdCnt1, wdCnt2);
	assert(wdCnt1 <= res->numWords);
	for (i = 0; i < minWds; i++) {
	    res->word[i] = bv1.word[i] & ~(bv2.word[i]);
	}

	// copy the rest from "bv1" (if any)
	for (/*SKIP*/; i < wdCnt1; i++) {
	    res->word[i] = bv1.word[i];
	}
    } else {
	i = 0;
    }

    // zero out the rest (in case "res->numWords > wdCnt1")
    for (/*SKIP*/; i < res->numWords; i++) {
	res->word[i] = Zero;
    }

    // reduce size if any high-order Zero words
    res->ReduceSz();
    return res;
}

BitVector& BitVector::operator -= (const BitVector& bv) throw ()
{
    unsigned short wdCnt2 = WdCnt(bv.sz);
    if (this->sz > 0 && wdCnt2 > 0) { // work only necessary if both non-empty
	// initialize sizes
	unsigned short wdCnt = WdCnt(this->sz);
	// this->sz is unchanged
	this->firstAvailWd = 0;

	// subtract where there are words in common
	unsigned short minWds = min(wdCnt, wdCnt2);
	for (unsigned short i = 0; i < minWds; i++) {
	    this->word[i] &= ~(bv.word[i]);
	}

        // reduce size if any high-order Zero words
	this->ReduceSz();
    }
    return *this;
}

// I/O methods ----------------------------------------------------------------

void BitVector::Log(VestaLog &log) const throw (VestaLog::Error)
{
    // NB: only as many words as necessary are written
    unsigned short numUsedWords = WdCnt(this->sz);
    log.write((char *)(&numUsedWords), sizeof(numUsedWords));
    if (numUsedWords > 0) {
	log.write((char *)(&(this->firstAvailWd)), sizeof(this->firstAvailWd));
	log.write((char *)(&(this->sz)), sizeof(this->sz));
	log.write((char *)(this->word), numUsedWords * BytesPerWd);
    }
}

void BitVector::Recover(RecoveryReader &rd)
  throw (VestaLog::Error, VestaLog::Eof)
{
    rd.readAll((char *)(&(this->numWords)), sizeof(this->numWords));
    if (this->numWords > 0) {
	rd.readAll((char *)(&(this->firstAvailWd)),sizeof(this->firstAvailWd));
	rd.readAll((char *)(&(this->sz)), sizeof(this->sz));
	this->word = NEW_PTRFREE_ARRAY(Word, this->numWords);
	rd.readAll((char *)(this->word), this->numWords * BytesPerWd);
    } else {
	this->firstAvailWd = 0;
	this->sz = 0;
	this->word = (Word *)NULL;
    }
}

void BitVector::Write(std::ostream &ofs) const throw (FS::Failure)
{
    // NB: only as many words as necessary are written
    unsigned short numUsedWords = WdCnt(this->sz);
    FS::Write(ofs, (char *)(&numUsedWords), sizeof(numUsedWords));
    if (numUsedWords > 0) {
	FS::Write(ofs, (char *)(&(this->firstAvailWd)),
		  sizeof(this->firstAvailWd));
	FS::Write(ofs, (char *)(&(this->sz)), sizeof(this->sz));
	FS::Write(ofs, (char *)(this->word), numUsedWords * BytesPerWd);
    }
}

void BitVector::Read(std::istream &ifs) throw (FS::EndOfFile, FS::Failure)
{
    FS::Read(ifs, (char *)(&(this->numWords)), sizeof(this->numWords));
    if (this->numWords > 0) {
	FS::Read(ifs, (char *)(&(this->firstAvailWd)),
		 sizeof(this->firstAvailWd));
	FS::Read(ifs, (char *)(&(this->sz)), sizeof(this->sz));
	this->word = NEW_PTRFREE_ARRAY(Word, this->numWords);
	FS::Read(ifs, (char *)(this->word), this->numWords * BytesPerWd);
    } else {
	this->firstAvailWd = 0;
	this->sz = 0;
	this->word = (Word *)NULL;
    }
}

void BitVector::Send(SRPC &srpc) const throw (SRPC::failure)
{
    // NB: only as many words as necessary are written
    unsigned short numUsedWords = WdCnt(this->sz);
    srpc.send_short(numUsedWords);
    if (numUsedWords > 0) {
	srpc.send_short(this->firstAvailWd);
	srpc.send_int(this->sz);
	srpc.send_int64_array((const Basics::int64 *) this->word,
			      numUsedWords);
    }
}

void BitVector::Recv(SRPC &srpc) throw (SRPC::failure)
{
    this->numWords = srpc.recv_short();
    if (this->numWords > 0) {
	this->firstAvailWd = srpc.recv_short();
	this->sz = srpc.recv_int();
	int len;
	this->word = (Word *) srpc.recv_int64_array(len);
	assert(this->numWords == len);
    } else {
	this->firstAvailWd = 0;
	this->sz = 0;
	this->word = (Word *)NULL;
    }
}

void BitVector::Print(std::ostream &os, int maxWidth) const throw ()
// The output is written in hexidecimal, one byte at a time...
{
    assert(maxWidth >= 30); // just to be safe

    // account for " (####### total)" at end of line
    maxWidth -= 16;

    unsigned short maxWd = WdCnt(this->sz);
    int widthWd = maxWidth / (2 * BytesPerWd);
    bool elided = (widthWd < maxWd);
    if (elided) {
	// recompute # of words to print, subtracting out space for
	// trailing "..."
	maxWidth -= 3;
	widthWd = maxWidth / (2 * BytesPerWd);
	maxWd = min(maxWd, widthWd);
    }
    if (maxWd == 0) { os << "<<Empty>>"; return; }
    for (unsigned short i = 0; i < maxWd; i++) {
	Word w = this->word[i];
	for (unsigned int j = 0; j < BytesPerWd; j++) {
	    char buff[3];
	    int printLen = sprintf(buff, "%02x", (unsigned int)(w & 0xff));
	    assert(printLen == 2);
	    os << buff;
	    w >>= 8;
	}
    }
    if (elided) os << "...";
    os << " (" << this->Cardinality() << " total)";
}

inline void Indent(std::ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << ' ';
}

void ExtendRow(std::ostream &os, int indent, int maxWidth,
	       const char *s, /*INOUT*/ int &col) throw ()
{
  int s_len = strlen(s);
  if (col + s_len > maxWidth) {
    os << endl;
    Indent(os, indent);
    col = indent;
  }
  os << s << ' ';
  col += (s_len + 1);
}

void BitVector::PrintAll(std::ostream &os, int indent, int maxWidth) const throw ()
{
  int col = indent;

  unsigned int lo = 0, prev = 0, curr;

  bool have_lo = false;

  BVIter iter(*this);
  Indent(os, indent);
  while (iter.Next(/*OUT*/ curr)) {
    // produce output if 'curr' does not extend a run
    if (!have_lo || (curr > prev + 1)) {
      // see if there is anything to print
      if (have_lo) {
	OBufStream s;
	s << lo;
	if (prev > lo) s << '-' << prev;
	s << ',';
	ExtendRow(os, indent, maxWidth, s.str(), /*INOUT*/ col);
      }
      // start a new potential run
      lo = curr;
      have_lo = true;
    }
    prev = curr;
  }
  // clean up
  if (!have_lo) {
    os << "<<Empty>>";
  } else {
    // print last buffered element(s)
    OBufStream s;
    s << lo;
    if (prev > lo) s << '-' << prev;
    ExtendRow(os, indent, maxWidth, s.str(), /*INOUT*/ col);
  }
  os << endl;
}

// BVIter methods -------------------------------------------------------------

bool BVIter::Next(/*OUT*/ unsigned int& res) throw ()
{
    while (this->bitIndex < this->bv->sz) {
        Word wd = this->bv->word[this->wordIndex];
        if (wd == Zero) {
            this->bitIndex += BitsPerWd;
        } else {
	    while (this->mask != Zero) {
		if (this->mask & wd) {
		    this->mask <<= 1;
		    res = this->bitIndex++;
		    return true;
		}
		this->bitIndex++;
		this->mask <<= 1;
            }
	}
	this->wordIndex++;
	this->mask = One;
    }
    return false;
}
