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
#include <FP.H>
#include <BitVector.H>

#include "IntIntTblLR.H"
#include "Combine.H"

using std::ostream;
using std::istream;
using std::endl;

// Combine::XorTag ------------------------------------------------------------

static Combine::XorTag& NamesXOR(const FP::List& fps, const BitVector& bv,
  /*OUT*/ Combine::XorTag& res, const IntIntTblLR *imap = NULL) throw ()
/* Set "res" to the combined fingerprint of the words "fps[imap(i)].w"
   for those bits "i" such that "bv.Read(i)", and return the result. The
   fingerprints are combined by computing the exclusive OR of their words.
   If "imap == NULL", the identity map is used. */
{
    unsigned int i;
    FP::Tag *fp;

    // initialize result
    res.w = 0UL;

    if (bv.Size() > 0) {
	// compute XOR
	BVIter iter(bv);
	while (iter.Next(/*OUT*/ i)) {
	    IntIntTblLR::int_type mapRes;
	    if (imap == (IntIntTblLR *)NULL)
		mapRes = i;
	    else {
		bool inMap = imap->Get(i, /*OUT*/ mapRes);
		assert(inMap);
	    }
	    res ^= fps.fp[mapRes];
	}
    }
    return res;
}

Combine::XorTag::XorTag(const FP::List& pkFPs, BitVector& bv,
  const IntIntTblLR *imap) throw ()
{
    (void)NamesXOR(pkFPs, bv, *this, imap);
}

void Combine::XorTag::Debug(ostream &os) const throw ()
{
    char buff[17];
    int printLen = sprintf(buff, "%016" FORMAT_LENGTH_INT_64 "x", this->w);
    assert(printLen == 16);
    os << buff;
}

// Combine::FPTag -------------------------------------------------------------

struct AvailBuff {
  char *buff;
  int len;
  AvailBuff *next;
};

static Basics::mutex availMu;
static AvailBuff *head;

static AvailBuff *NextAvail(int size) throw ()
/* Return an "AvailBuff" contain a buffer of length at least "size". */
{
    AvailBuff *res;
    availMu.lock();
    if (head == (AvailBuff *)NULL) {
      res = NEW(AvailBuff);
      res->buff = NEW_PTRFREE_ARRAY(char, size);
      res->len = size;
      availMu.unlock();
    } else {
	res = head;
	head = head->next;
	availMu.unlock();
	if (res->len < size) {
	    delete[] res->buff;
	    res->buff = NEW_PTRFREE_ARRAY(char, size);
	}
    }
    return res;
}

static void ReturnAvail(AvailBuff *avail) throw ()
/* Return "avail" to the list of available buffers. */
{
    availMu.lock();
    avail->next = head;
    head = avail;
    availMu.unlock();
}

static FP::Tag& NamesFP(const FP::List& fps, const BitVector& bv,
  /*OUT*/ FP::Tag& res, const IntIntTblLR *imap = NULL) throw ()
/* Set "res" to the combined fingerprint of the words "fps[imap(i)].w"
   for those bits "i" such that "bv.Read(i)", and return the result. The
   fingerprints are combined in increasing index order (in "bv") by
   computing the fingerprint tag of the concatenation of their words.
   If "imap == NULL", the identity map is used. */
{
    AvailBuff *avail = (AvailBuff *)NULL;
    char *fpBuff;
    int buffLen = 0;

    if (bv.Size() > 0) {
	// fill a buffer with the bytes of the relevant fingerprints
	unsigned int i;
	BVIter iter(bv);
	// the following is a conservative upper-bound on space required
	avail = NextAvail(bv.Size() * FP::ByteCnt);
	fpBuff = avail->buff;
	while (iter.Next(/*OUT*/ i)) {
	    IntIntTblLR::int_type mapRes;
	    if (imap == (IntIntTblLR *)NULL)
		mapRes = i;
	    else {
		bool inMap = imap->Get(i, /*OUT*/ mapRes);
		assert(inMap);
	    }
	    Word *words = fps.fp[mapRes].Words();
	    memcpy(fpBuff + buffLen, (char *)words, FP::ByteCnt);
	    buffLen += FP::ByteCnt;
	}
    }

    // create a new fingerprint on those bytes
    res = FP::Tag(fpBuff, buffLen);

    // clean up and return
    if (avail != (AvailBuff *)NULL) ReturnAvail(avail);
    return res;
}

Combine::FPTag::FPTag(const FP::List& fps, const BitVector& bv,
  const IntIntTblLR *imap) throw ()
{
    (void)NamesFP(fps, bv, *this, imap);
}

// Combine::XorFPTag ----------------------------------------------------------

void Combine::XorFPTag::Init(const FP::List& fps, const BitVector& bv,
  const IntIntTblLR *imap) throw ()
{
    (void)NamesXOR(fps, bv, /*OUT*/ this->xort, imap);
    this->xort.w &= ~LSB;
}

FP::Tag& Combine::XorFPTag::FPVal(const FP::List &fps,
  const BitVector &bv, const IntIntTblLR *imap) throw ()
{
    if (!(this->xort.w & LSB)) {
	(void)NamesFP(fps, bv, /*OUT*/ this->fp, imap);
	this->xort.w |= LSB;
    }
    return this->fp;
}

void Combine::XorFPTag::Log(VestaLog &log) const
  throw (VestaLog::Error)
{
    xort.Log(log);
    if (this->xort.w & LSB) fp.Log(log);
}

void Combine::XorFPTag::Recover(RecoveryReader &rd)
  throw (VestaLog::Error, VestaLog::Eof)
{
    this->xort.Recover(rd);
    if (this->xort.w & LSB) this->fp.Recover(rd);
}

void Combine::XorFPTag::Write(ostream &ofs) const
  throw (FS::Failure)
{
    this->xort.Write(ofs);
    if (this->xort.w & LSB) this->fp.Write(ofs);
}

void Combine::XorFPTag::Read(istream &ifs)
  throw (FS::EndOfFile, FS::Failure)
{
    this->xort.Read(ifs);
    if (this->xort.w & LSB) this->fp.Read(ifs);
}

inline void Indent(ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << " ";
}

void Combine::XorFPTag::Debug(ostream &os, int indent) const throw ()
{
    Indent(os, indent); os << "xor = "; this->xort.Debug(os); os << endl;
    Indent(os, indent); os << "fp  = ";
    if (this->xort.w & LSB) os << this->fp; else os << "<lazy>";
}
