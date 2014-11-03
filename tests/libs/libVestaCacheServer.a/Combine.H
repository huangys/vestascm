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

// Last modified on Thu Aug  5 01:44:33 EDT 2004 by ken@xorian.net
//      modified on Mon Nov 10 13:11:07 PST 1997 by heydon

// Combine.H -- defines classes for combining a sequence of Fingerprints

#ifndef _COMBINE_H
#define _COMBINE_H

#include <Basics.H>
#include <FS.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <FP.H>
#include <BitVector.H>

#include "IntIntTblLR.H"

namespace Combine
{
  /* The first two classes --- "XorTag" and "FPTag" --- are really private
     to clients. */

  // XorTag -- The result of XOR'ing together some Fingerprint tags
  //
  /* The value "w" of an "XorTag" is produced by XOR'ing together a sequence
     of Fingerprint tags (wordwise), and then XOR'ing the resulting words
     together into a single word.
  */
  class XorTag {
  public:
    // constructors
    XorTag() { /*SKIP*/ }
    XorTag(const FP::List& pkFPs, BitVector& bv,
	   const IntIntTblLR *imap = NULL) throw ();
    /* Initialize the "XorTag" as the exclusive OR of the fingerprints
       "pkFPs.fp[imap(i)]" such that "bv.Read(i)". If "imap" is NULL,
       use the identity map. */

    // log/recover
    void Log(VestaLog &log) const throw (VestaLog::Error)
    { log.write((char *)(&(this->w)), sizeof(this->w)); }
    void Recover(RecoveryReader &rd) throw (VestaLog::Error, VestaLog::Eof)
    { rd.readAll((char *)(&(this->w)), sizeof(this->w)); }

    // write/read
    void Write(std::ostream &ofs) const throw (FS::Failure)
    { FS::Write(ofs, (char *)(&(this->w)), sizeof(this->w)); }
    void Read(std::istream &ifs) throw (FS::EndOfFile, FS::Failure)
    { FS::Read(ifs, (char *)(&(this->w)), sizeof(this->w)); }
    void Debug(std::ostream &os) const throw ();

    // other operators
    XorTag& operator ^= (const FP::Tag& fp) throw ()
    { this->w ^= fp.Hash(); return *this; }
    bool operator == (const XorTag& other) throw ()
    { return this->w == other.w; }
    bool operator != (const XorTag& other) throw ()
    { return this->w != other.w; }

    // data
    Word w;
  }; // XorTag

  // "FPTag" -- a combined fingerprint
  //
  class FPTag : public FP::Tag {
  public:
    // constructors
    FPTag() { /*SKIP*/ }
    FPTag(const FP::List& fps, const BitVector& bv,
          const IntIntTblLR *imap = NULL) throw ();
    /* Initialize this fingerprint to the combined fingerprint of the
       words "fps[imap(i)].w" such that "bv.Read(i)". The fingerprint is
       combined in the order determined by increasing values of "i".
       If "imap == NULL", the identity map is used. */
  }; // FPTag

  // XorFPTag -- A combination of an XorTag and an FPTag.
  //
  /* The "XorFPTag" type is a tag of a sequence of "FP::Tag"'s. An
     "XorFPTag" value has two logical fields: an exclusive OR of the 
     fingerprint word, and the combined fingerprint of the words. Both
     are available through methods.
  */
  class XorFPTag {
  public:
    enum { LSB = 0x1 };

    // constructors
    XorFPTag() throw ()
    { /* SKIP */ };
    XorFPTag(RecoveryReader &rd) throw (VestaLog::Error, VestaLog::Eof)
    { Recover(rd); }
    XorFPTag(const FP::List& fps, const BitVector& bv,
	     const IntIntTblLR *imap = NULL) throw ()
    { Init(fps, bv, imap); }
    void Zero() throw () { this->xort.w = 0; }
    void Init(const FP::List& fps, const BitVector& bv,
	      const IntIntTblLR *imap = NULL) throw ();
    /* Initialize the "xort" field to be the exclusive OR of the words
       "fps[imap(i)].w" such that "bv.Read(i)". If "imap == NULL", the
       identity map is used. */

    Word Xor() throw () { return this->xort.w | LSB; }
    /* Return the "Xor" part of this object. */

    FP::Tag& FPVal(const FP::List &fps, const BitVector &bv,
		   const IntIntTblLR *imap = NULL) throw ();
    /* Return the combined fingerprint of the words "fps[imap(i)].w" such
       that "bv.Read(i)". The fingerprint is combined in the order
       determined by increasing values of "i". If "imap == NULL", the
       identity map is used. If this tag's fingerprint value was lazy,
       this method has the benevolent side-effect of unlazying it. */

    void UnlazyFPVal(const FP::List &fps, const BitVector &bv,
		     const IntIntTblLR *imap = NULL) throw ()
    { (void) FPVal(fps, bv, imap); }
    /* If this tag's fingerprint value is lazy, unlazy it according to
       the "FPVal" method above. */

    void InvalidateFPVal() throw () { this->xort.w &= ~LSB; }
    /* Invalidate the fingerprint for this tag because the *order* of
       the fingerprints used to compute it has changed. */

    bool FPValIsUnlazied() throw () { return this->xort.w & LSB; }
    /* Return true iff the combined fingerprint has been computed. */

    void Log(VestaLog &log) const throw (VestaLog::Error);
    /* Append this tag to "log", which must be in the "logging" state. */

    void Recover(RecoveryReader &rd)
      throw (VestaLog::Error, VestaLog::Eof);
    /* Recover this tag from "rd". */

    void Write(std::ostream &ofs) const throw (FS::Failure);
    void Read(std::istream &ifs) throw (FS::EndOfFile, FS::Failure);
    /* Write/read this XorFPTag to/from "ofs"/"ifs". */

    void Debug(std::ostream &os, int indent = 2) const throw ();
    /* Write debugging information to "os"; does not end in newline. */

  private:
    XorTag xort;	    // exclusive OR of fingerprint tags
    FP::Tag fp;		    // combined FP

    /* Note: The least-significant bit of "xort.w" is not the XOR, but
       instead indicates if "fp" is valid or not. Hence, the "Xor" method
       always returns a value that has its least-significant bit set. */
  }; // XorFPTag
}

#endif // _COMBINE_H
