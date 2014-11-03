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

#include <Basics.H>
#include <FS.H>
#include <VestaLog.H>
#include <Recovery.H>
#include "FP.H"
#include "Poly.H"
#include "ByteModTable.H"

using std::istream;
using std::ostream;
using std::ios;

/* If defined, INLINE_POLYINC causes calls to PolyInc to be
   inlined; otherwise, actual calls are made to the PolyInc function. */
#define INLINE_POLYINC

/* If defined, UNROLL_LOOP uses a more efficient loop unrolling
   in the ExtendByWords function. */
#define UNROLL_LOOP

const int WordSize = sizeof(Word);
const int WordBits = WordSize * 8;

// This macro is used to make sure that a Word read from memory is
// ordered correctly.  (Because the fingerprinting algorithm
// conceptually processes data in a bit-sequential manner,
// non-little-endian machines must do some byte swapping.)
#if BYTE_ORDER == BIG_ENDIAN
#define ORDER_WORD_BYTES(p_x) Basics::swab64(p_x)
#elif BYTE_ORDER == LITTLE_ENDIAN
#define ORDER_WORD_BYTES(p_x) (p_x)
#else
#error Unknown byte order
#endif

// Polynomial Extension Functions ---------------------------------------------

#ifdef UNROLL_LOOP
static Poly *ByteModTable0 = ByteModTable[0];
static Poly *ByteModTable1 = ByteModTable[1];
static Poly *ByteModTable2 = ByteModTable[2];
static Poly *ByteModTable3 = ByteModTable[3];
static Poly *ByteModTable4 = ByteModTable[4];
static Poly *ByteModTable5 = ByteModTable[5];
static Poly *ByteModTable6 = ByteModTable[6];
#endif
static Poly *ByteModTable7 = ByteModTable[7];

static void ExtendByChar(/*INOUT*/ Poly& p, const unsigned char c) throw ()
/* Change "p" to be the residue mod P of the polynomial represented by "p"
   followed by the character "c". This is the straightforward translation
   of the "ExtendByBytes" function below with "*source == c" and "n == 1". */
{
    // Ensure that ByteModTable is initialized
    ByteModTableInit();

    Poly temp = POLY_ZERO;
    unsigned char c0 = (unsigned char)(p.w[0]);
#ifdef INLINE_POLYINC
    Poly *tblPoly = &(ByteModTable7[c0]);
    temp.w[0] ^= tblPoly->w[0];
    temp.w[1] ^= tblPoly->w[1];
#else
    PolyInc(/*INOUT*/ temp, ByteModTable7[c0]);
#endif
    p.w[0] = (p.w[0] >> 8) | (p.w[1] << (WordBits - 8));
    p.w[1] = (p.w[1] >> 8) | (((Word)c) << (WordBits - 8));
#ifdef INLINE_POLYINC
    p.w[0] ^= temp.w[0];
    p.w[1] ^= temp.w[1];
#else
    PolyInc(/*INOUT*/ p, temp);
#endif
}

static void ExtendByBytes(/*INOUT*/ Poly& p,
  const unsigned char *source, int n) throw ()
/* Change "p" to be the residue mod P of the polynomial represented by "p"
   followed by "n" chars starting from "source". "n" must be a number
   between 1 and 7. */
{
    assert(0 < n && n < 8);

    const int bits = 8 * n;
    Poly temp = POLY_ZERO;
    Word mask = p.w[0];
    for (int i = 0; i < n; i++) {
        unsigned char c0 = (unsigned char)mask;
#ifdef INLINE_POLYINC
	Poly *tblPoly = &(ByteModTable[i+8-n][c0]);
	temp.w[0] ^= tblPoly->w[0];
	temp.w[1] ^= tblPoly->w[1];
#else
	PolyInc(/*INOUT*/ temp, ByteModTable[i+8-n][c0]);
#endif
	mask >>= 8;
    }
#if defined(VALGRIND_SUPPORT)
    // Lower performance but completely safe, even as far as paranoid
    // run-time checkers like valgrind are concerned.
    mask = 0;
    char *mask_c = (char *) &mask;
    for(int i = 0; i < n; i++)
      {
	mask_c[i] = source[i];
      }
    mask = ORDER_WORD_BYTES(mask);
#else
    unsigned int align_mask = sizeof(Word) - 1;
    int align = ((PointerInt) source) & align_mask;
    if (align == 0) {
	mask = ORDER_WORD_BYTES(*(Word *)source);
    } else {
	/* The following is the same as "mask = *(Word *)source",
           but it avoids unaligned accesses. */
	Word *w1 = (Word *)(source - align);
	// We only need w2 if at least the first byte of it is part of
	// the source.  (If it's past the end of the source data,
	// there's no guarantee it's in a valid region of memory.)
	Word *w2 = ((WordSize - align) < n)
	  ? (Word *)(source + (WordSize - align))
	  : (Word *)0;
        align <<= 3; // align *= 8 (convert to bits)
	mask = (ORDER_WORD_BYTES(*w1) >> align);
	// If we have a second word, OR it into the mask.
	if(w2 != 0)
	  {
	    mask |= (ORDER_WORD_BYTES(*w2) << (WordBits - align));
	  }
    }
#endif
    p.w[0] = (p.w[0] >> bits) | (p.w[1] << (WordBits-bits));
    p.w[1] = (p.w[1] >> bits) | (mask << (WordBits-bits));
#ifdef INLINE_POLYINC
    p.w[0] ^= temp.w[0];
    p.w[1] ^= temp.w[1];
#else
    PolyInc(/*INOUT*/ p, temp);
#endif
}

static void ExtendByWords(/*INOUT*/ Poly& p, const Word *source, int n)
  throw ()
/* Change "p" to be the residue mod P of a polynomial represented by "p"
   followed by "n" words starting from "source". */
{
    Poly temp;

    for (int i = 0; i < n; i++) {
	temp = POLY_ZERO;
#ifdef UNROLL_LOOP
	{
	    unsigned char *ptr = (unsigned char *) &(p.w[0]);
#if BYTE_ORDER == BIG_ENDIAN
	    Poly p7 = ByteModTable7[*ptr++];
	    Poly p6 = ByteModTable6[*ptr++];
	    Poly p5 = ByteModTable5[*ptr++];
	    Poly p4 = ByteModTable4[*ptr++];
	    Poly p3 = ByteModTable3[*ptr++];
	    Poly p2 = ByteModTable2[*ptr++];
	    Poly p1 = ByteModTable1[*ptr++];
	    Poly p0 = ByteModTable0[*ptr];
#elif BYTE_ORDER == LITTLE_ENDIAN
	    Poly p0 = ByteModTable0[*ptr++];
	    Poly p1 = ByteModTable1[*ptr++];
	    Poly p2 = ByteModTable2[*ptr++];
	    Poly p3 = ByteModTable3[*ptr++];
	    Poly p4 = ByteModTable4[*ptr++];
	    Poly p5 = ByteModTable5[*ptr++];
	    Poly p6 = ByteModTable6[*ptr++];
	    Poly p7 = ByteModTable7[*ptr];
#else
#error Unknown byte order
#endif
	    temp.w[0] ^= p0.w[0]; temp.w[1] ^= p0.w[1];
	    temp.w[0] ^= p1.w[0]; temp.w[1] ^= p1.w[1];
	    temp.w[0] ^= p2.w[0]; temp.w[1] ^= p2.w[1];
	    temp.w[0] ^= p3.w[0]; temp.w[1] ^= p3.w[1];
	    temp.w[0] ^= p4.w[0]; temp.w[1] ^= p4.w[1];
	    temp.w[0] ^= p5.w[0]; temp.w[1] ^= p5.w[1];
	    temp.w[0] ^= p6.w[0]; temp.w[1] ^= p6.w[1];
	    temp.w[0] ^= p7.w[0]; temp.w[1] ^= p7.w[1];
	}
#else /* original version */
	Word mask = p.w[0];
	for (int j = 0;  j < 8; j++) {
	    unsigned char c0 = (unsigned char)mask;
#ifdef INLINE_POLYINC
	    Poly *tblPoly = &(ByteModTable[j][c0]);
	    temp.w[0] ^= tblPoly->w[0];
	    temp.w[1] ^= tblPoly->w[1];
#else
	    PolyInc(/*INOUT*/ temp, ByteModTable[j][c0]);
#endif // USE_POLY_INC
	    mask >>= 8;
	}
#endif
	p.w[0] = p.w[1];
	p.w[1] = ORDER_WORD_BYTES(source[i]);
#ifdef INLINE_POLYINC
	p.w[0] ^= temp.w[0];
	p.w[1] ^= temp.w[1];
#else
	PolyInc(/*INOUT*/ p, temp);
#endif
    }
}

// Fingerprint Extension Function ---------------------------------------------

static void RawFPExtend(/*INOUT*/ RawFP& fp,
  const char *addr, int len) throw ()
/* Given that "fp" is the fingerprint of a string "s", set "fp" to the
   fingerprint of "s" concatenated with the "len" chars starting at "addr". */
{
    // Ensure that ByteModTable is initialized
    ByteModTableInit();

    unsigned char *p = (unsigned char *) addr;
    int residue;

    // Do first residue bytes
    if (len >= WordSize) {
	residue = WordSize - (((PointerInt) p) % WordSize);
	if (residue != WordSize) {
	    ExtendByBytes(/*INOUT*/ fp, p, residue);
	    p += residue;
	    len -= residue;
	}
    }

    // Do middle bytes, now starting from word boundary
    if (len >= WordSize) {
	ExtendByWords(/*INOUT*/ fp, (Word *) p, len / WordSize);
	p += (len / WordSize) * WordSize;
	len %= WordSize;
    }

    // Do last few bytes
    if (len != 0) {
	ExtendByBytes(/*INOUT*/ fp, p, len);
    }
}

// FP::Tag Methods ------------------------------------------------------------

void FP::Tag::Init(const char *s, int len) throw ()
{
    RawFP fp = POLY_ONE;
    RawFPExtend(/*INOUT*/ fp, s, len);
    Permute(fp);
}

FP::Tag& FP::Tag::Extend(const char *s, int len) throw ()
{
    if (len < 0) len = strlen(s);
    RawFP fp;
    Unpermute(/*OUT*/ fp);
    RawFPExtend(/*INOUT*/ fp, s, len);
    Permute(fp);
    return *this;
}

FP::Tag& FP::Tag::Extend(char c) throw ()
{
    RawFP fp;
    Unpermute(/*OUT*/ fp);
    ExtendByChar(/*INOUT*/ fp, c);
    Permute(fp);
    return *this;
}

void FP::Tag::ExtendRaw(/*INOUT*/ RawFP &fp, const char *s, int len)
  throw ()
{
    if (len < 0) len = strlen(s);
    RawFPExtend(/*INOUT*/ fp, s, len);
}

void FP::Tag::ExtendRaw(/*INOUT*/ RawFP &fp, char c) throw ()
{
    ExtendByChar(/*INOUT*/ fp, c);
}

Word FP::Tag::Hash() const throw ()
{
    Word res = w[0];
    for (int i = 1; i < FP::WordCnt; i++) res ^= w[i];
    return res;
}

void FP::Tag::ToBytes(unsigned char *buffer) const throw ()
{
  unsigned int i = 0;
  for(unsigned int word = 0; word < FP::WordCnt; word++)
    {
      for(unsigned int bit = 0; bit < WordBits; bit +=8)
	{
	  buffer[i++] = (this->w[word] >> bit);
	}
    }
  assert(i == FP::ByteCnt);
}

void FP::Tag::FromBytes(const unsigned char *buffer) throw ()
{
  unsigned int i = 0;
  for(unsigned int word = 0; word < FP::WordCnt; word++)
    {
      this->w[word] = 0;
      for(unsigned int bit = 0; bit < WordBits; bit +=8)
	{
	  this->w[word] |= (((Word) buffer[i++]) << bit);
	}
    }
  assert(i == FP::ByteCnt);
}

bool FP::Tag::operator == (const FP::Tag& other) const throw ()
{
    for (int i = 0; i < FP::WordCnt; i++) {
	if (this->w[i] != other.w[i]) return false;
    }
    return true;
}

bool FP::Tag::operator != (const FP::Tag& other) const throw ()
{
    for (int i = 0; i < FP::WordCnt; i++) {
	if (this->w[i] != other.w[i]) return true;
    }
    return false;
}

void FP::Tag::Print(ostream &os, const char *word_separator)
  const throw ()
{
  char buff[17];
  for (int i = 0; i < FP::WordCnt; i++) {
    sprintf(buff, "%016" FORMAT_LENGTH_INT_64 "x", this->w[i]);
    os << buff;
    if (i < FP::WordCnt - 1) os << word_separator;
  }
}

int Compare(const FP::Tag& fp1, const FP::Tag& fp2) throw ()
{
    for (int i = 0; i < FP::WordCnt; i++) {
	if (fp1.w[i] != fp2.w[i]) {
	    return ((fp1.w[i] < fp2.w[i]) ? -1 : 1);
	}
    }
    return 0;
}

/* Tags are produced from fingerprints as follows:

   1) Use a permutation "perm" on the byte values; that is, each byte value is
   individually permuted to a value in the interval [0..255].

   2) View the permuted result as a vector of two long unsigneds and
   multiply it by a non-singular matrix A with coefficients in
   [0..2^64-1] mod 2^64.

   To convert from a tag back into a fingerprint, we keep the
   inverse of "perm" in "perminv", and the inverse of A in B.
*/

const Word
  A[2][2] = {{CONST_INT_64(0xce36f163f737a677),
	      CONST_INT_64(0x431bf4ecc646b337)},
             {CONST_INT_64(0x1960326FA38D04D0),
	      CONST_INT_64(0x10155F23A2F024F9)}},
  B[2][2] = {{CONST_INT_64(0x94033a389a279d77),
	      CONST_INT_64(0xd79f3b15576598a7)},
             {CONST_INT_64(0x67f2d59b2369b1d0),
	      CONST_INT_64(0x63e096e4228c019)}};

const unsigned char
  perm[256] = {89, 171, 235, 183, 176, 181, 91, 54, 49,
    151, 11, 0, 73, 138, 118, 160, 172, 251, 255, 192, 102, 39, 15, 169,
    149, 110, 240, 133, 213, 196, 217, 199, 29, 43, 52, 153, 32, 2, 179,
    6, 211, 165, 161, 224, 194, 209, 8, 93, 197, 162, 207, 229, 83, 247,
    129, 188, 145, 186, 59, 147, 202, 109, 141, 78, 38, 92, 68, 190,
    252, 116, 85, 184, 34, 103, 88, 140, 123, 76, 131, 67, 26, 166, 185,
    63, 90, 86, 5, 246, 58, 238, 231, 232, 241, 106, 7, 225, 75, 45,
    146, 19, 23, 99, 9, 216, 96, 236, 95, 218, 182, 40, 124, 201, 82,
    230, 214, 206, 107, 137, 249, 212, 77, 119, 253, 1, 210, 35, 69,
    167, 79, 4, 198, 180, 226, 122, 128, 244, 163, 250, 121, 55, 135,
    14, 154, 100, 243, 187, 173, 3, 46, 33, 157, 42, 152, 51, 30, 142,
    98, 48, 148, 254, 223, 159, 41, 74, 155, 248, 205, 18, 175, 108, 56,
    228, 195, 17, 237, 104, 62, 47, 12, 72, 158, 25, 134, 234, 239, 242,
    80, 143, 101, 203, 81, 215, 10, 27, 204, 24, 37, 191, 105, 208, 132,
    126, 50, 156, 227, 125, 65, 130, 139, 136, 31, 44, 97, 94, 53, 127,
    233, 221, 84, 117, 220, 219, 200, 164, 120, 20, 113, 22, 168, 66,
    170, 87, 150, 70, 193, 189, 177, 28, 36, 114, 178, 13, 71, 64, 115,
    16, 144, 57, 245, 111, 222, 60, 174, 61, 112, 21},
  perminv[256] = {11, 123, 37, 147, 129, 86, 39, 94, 46, 102, 192, 10,
    178, 241, 141, 22, 245, 173, 167, 99, 225, 255, 227, 100, 195, 181,
    80, 193, 237, 32, 154, 210, 36, 149, 72, 125, 238, 196, 64, 21, 109,
    162, 151, 33, 211, 97, 148, 177, 157, 8, 202, 153, 34, 214, 7, 139,
    170, 247, 88, 58, 251, 253, 176, 83, 243, 206, 229, 79, 66, 126,
    233, 242, 179, 12, 163, 96, 77, 120, 63, 128, 186, 190, 112, 52,
    218, 70, 85, 231, 74, 0, 84, 6, 65, 47, 213, 106, 104, 212, 156,
    101, 143, 188, 20, 73, 175, 198, 93, 116, 169, 61, 25, 249, 254,
    226, 239, 244, 69, 219, 14, 121, 224, 138, 133, 76, 110, 205, 201,
    215, 134, 54, 207, 78, 200, 27, 182, 140, 209, 117, 13, 208, 75, 62,
    155, 187, 246, 56, 98, 59, 158, 24, 232, 9, 152, 35, 142, 164, 203,
    150, 180, 161, 15, 42, 49, 136, 223, 41, 81, 127, 228, 23, 230, 1,
    16, 146, 252, 168, 4, 236, 240, 38, 131, 5, 108, 3, 71, 82, 57, 145,
    55, 235, 67, 197, 19, 234, 44, 172, 29, 48, 130, 31, 222, 111, 60,
    189, 194, 166, 115, 50, 199, 45, 124, 40, 119, 28, 114, 191, 103,
    30, 107, 221, 220, 217, 250, 160, 43, 95, 132, 204, 171, 51, 113,
    90, 91, 216, 183, 2, 105, 174, 89, 184, 26, 92, 185, 144, 135, 248,
    87, 53, 165, 118, 137, 17, 68, 122, 159, 18};

void FP::Tag::Permute(const RawFP &f) throw ()
{
    unsigned char *p = (unsigned char *) &(f.w);
 
    // First use a permutation on the byte values
    int i;
    for (i=0;  i < FP::ByteCnt; i++, p++) {
	*p = perm[*p];
    }

    // Second, multiply by the matrix "A"
    this->w[0] = f.w[0] * A[0][0] + f.w[1] * A[1][0];
    this->w[1] = f.w[0] * A[0][1] + f.w[1] * A[1][1];
}

void FP::Tag::Unpermute(/*OUT*/ RawFP &res) const throw ()
{
    // multiply the the inverse matrix "B"
    res.w[0] = w[0] * B[0][0] + w[1] * B[1][0];
    res.w[1] = w[0] * B[0][1] + w[1] * B[1][1];
 
    // permute bytes with inverse "perminv"
    int i;
    unsigned char *p = (unsigned char *) &(res.w);
    for (i=0;  i < FP::ByteCnt; i++, p++) {
	*p = perminv[*p];
    }
}

// FP::List Methods -----------------------------------------------------------

void FP::List::Log(VestaLog& log) const throw (VestaLog::Error)
{
    log.write((char *)(&(this->len)), sizeof(int));
    for (int i = 0; i < this->len; i++) {
	this->fp[i].Log(log);
    }
}

void FP::List::Recover(RecoveryReader &rd)
  throw (VestaLog::Error, VestaLog::Eof)
{
    rd.readAll((char *)(&(this->len)), sizeof(int));
    if (this->len > 0) {
	this->fp = NEW_PTRFREE_ARRAY(FP::Tag, len);
	for (int i = 0; i < this->len; i++) {
	    this->fp[i].Recover(rd);
	}
    } else {
	this->fp = (FP::Tag *)NULL;
    }
}

void FP::List::Write(ostream &ofs) const throw (FS::Failure)
{
    FS::Write(ofs, (char *)(&(this->len)), sizeof(int));
    for (int i = 0; i < this->len; i++) {
	this->fp[i].Write(ofs);
    }
}

void FP::List::Read(istream &ifs) throw (FS::EndOfFile, FS::Failure)
{
    FS::Read(ifs, (char *)(&(this->len)), sizeof(int));
    if (this->len > 0) {
        this->fp = NEW_PTRFREE_ARRAY(FP::Tag, this->len);
        for (int i = 0; i < this->len; i++) {
	    this->fp[i].Read(ifs);
	}
    } else {
	this->fp = (FP::Tag *)NULL;
    }
}

Basics::uint64 FP::List::Skip(std::istream &ifs)
  throw (FS::EndOfFile, FS::Failure)
{
  int len;
  FS::Read(ifs, (char *)(&len), sizeof(int));
  Basics::uint64 dataLen = FP::ByteCnt * len;
  FS::Seek(ifs, dataLen, ios::cur);
  return (dataLen + sizeof(int));
}

void FP::List::Send(SRPC &srpc) const throw (SRPC::failure)
{
    srpc.send_seq_start(this->len);
    for (int i = 0; i < this->len; i++) {
	this->fp[i].Send(srpc);
    }
    srpc.send_seq_end();
}

void FP::List::Recv(SRPC &srpc) throw (SRPC::failure)
{
    srpc.recv_seq_start(&(this->len), NULL);
    this->fp = NEW_PTRFREE_ARRAY(FP::Tag, this->len);
    for (int i = 0; i < this->len; i++) {
	this->fp[i].Recv(srpc);
    }
    srpc.recv_seq_end();
}

inline void Indent(ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << " ";
}

void FP::List::Print(ostream &os, int indent) const throw ()
{
    if (this->len > 0) {
	for (int i = 0; i < this->len; i++) {
	    Indent(os, indent);
	    os << "fp[" << i << "] = " << this->fp[i] << "\n";
	}
    } else {
	Indent(os, indent);
	os << "<< no fingerprints >>\n";
    }
}

// FP static Methods ----------------------------------------------------------

void FP::FileContents(istream &ifs, /*INOUT*/ FP::Tag &fp)
  throw (FS::Failure)
{
    const int BuffSize = 1024;
    char buff[BuffSize];
    while (true) {
	// try reading a full buffer's worth
	if (ifs.read(buff, BuffSize)) {
	    // read completed successfully
	    fp.Extend(buff, BuffSize);
	} else {
	    // error or EOF
	    int numRead = ifs.gcount();
	    if (ifs.rdstate() & ios::failbit && numRead < BuffSize) {
		// EOF was encountered
		fp.Extend(buff, numRead);
		// ifs.clear(0);  // so close() will succeed
		break;
	    } else {
		// other error
		throw (FS::Failure("FP::FileContents"));
	    }
	}
    }
}
