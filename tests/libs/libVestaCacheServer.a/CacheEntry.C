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
#include <Model.H>
#include <CacheIndex.H>
#include <VestaVal.H>
#include <Debug.H>

#include "IntIntTblLR.H"
#include "Combine.H"
#include "CacheEntry.H"

using std::istream;
using std::ostream;
using std::ifstream;
using std::endl;
using std::ios;
using OS::cio;

// CE::T ----------------------------------------------------------------------

CE::T::T(CacheEntry::Index ci, BitVector *uncommonNames, FP::List* fps,
  IntIntTblLR *imap, VestaVal::T *value, CacheEntry::Indices *kids,
  Model::T model) throw () 
/* REQUIRES VPKFile(SELF).mu IN LL */
: ci(ci), uncommonNames(uncommonNames), value(value), kids(kids),
  value_immut(0), kids_immut(0),
  model(model), imap(imap), fps(fps)
{
    // compute uncommon tag
    uncommonTag = Combine::XorFPTag(*fps, *uncommonNames, imap);
}

CE::T::T(const T *ce) throw ()
/* REQUIRES VPKFile(SELF).mu IN LL */
: ci(ce->ci), uncommonTag(ce->uncommonTag), value(ce->value),
  kids(ce->kids),
  value_immut(ce->value_immut), kids_immut(ce->kids_immut),
  model(ce->model), fps(ce->fps)
{
  this->uncommonNames = NEW_CONSTR(BitVector, (ce->uncommonNames));
  this->imap = (ce->imap == NULL) ? NULL : NEW_CONSTR(IntIntTblLR, (ce->imap));
}

bool CE::T::FPMatch(const FP::List& pkFPs) throw ()
/* Note: This function has been implemented as a series of "if" statements so
   it can easily be modified to keep statistics on hit rates. It would be
   interesting to count the percentage of each of these three cases:

|         XOR test       FP test     Overall
|    1.    Match          Match        Hit
|    2.    Match        No match      Miss
|    3.   No match         ???        Miss

   We hope that the overall percentage of misses is low, but when we do miss,
   we hope the percentage due to case (2) is small compared to case (3). If
   not, the XOR test is not saving us much. */
{
    // fast path: no uncommon names to compare against
    if (this->uncommonNames->Size() == 0) {
	return true;
    }

    // check uncommon FP's using XOR test
    Combine::XorFPTag tag(pkFPs, *(this->uncommonNames));
    if (tag.Xor() != this->uncommonTag.Xor()) {
	// case (3)
	return false;
    }

    // check uncommon FP's using full test (if necessary)
    if (tag.FPVal(pkFPs, *(this->uncommonNames)) !=
	uncommonTag.FPVal(*(this->fps), *(this->uncommonNames), this->imap)) {
	// case (2)
	return false;
    }

    // case (1)
    return true;
}

void CE::T::Pack(const BitVector *packMask, const IntIntTblLR *reMap) throw ()
/* REQUIRES VPKFile(SELF).mu IN LL */
/* REQUIRES (packMask == NULL) == (reMap == NULL) */
{
    assert((packMask == (BitVector *)NULL) == (reMap == (IntIntTblLR *)NULL));
    if (packMask != (BitVector *)NULL) {
	// pack "uncommonNames"
	uncommonNames->Pack(*packMask);

	// update "imap" if it is not the identity
	if (this->imap != (IntIntTblLR *)NULL) {
	  IntIntTblLR *imap2 = NEW_CONSTR(IntIntTblLR,
					  (this->imap->ArraySize()));
	    IntIntTblIter it(this->imap);
	    IntIntTblLR::int_type k, newK, v;
	    while (it.Next(/*OUT*/ k, /*OUT*/ v)) {
		bool inTbl = reMap->Get(k, /*OUT*/ newK); assert(inTbl);
		try {
		  inTbl = imap2->Put(newK, v); assert(!inTbl);
		}
		catch(IntIntTblLR::InvalidKey e)
		  {
		    cio().start_err() << Debug::Timestamp() << "INTERNAL ERROR: "
				      << "IntIntTblLR::InvalidKey caugt in CE::T::Pack ("
				      << __FILE__ << ":" << __LINE__ << ")" << endl
				      << "  value = " << e.key << endl;
		    cio().end_err(/*aborting*/true);
		    abort();
		  }
		catch(IntIntTblLR::InvalidValue e)
		  {
		    cio().start_err() << Debug::Timestamp() << "INTERNAL ERROR: "
				      << "IntIntTblLR::InvalidValue caugt in CE::T::Pack ("
				      << __FILE__ << ":" << __LINE__ << ")" << endl
				      << "  value = " << e.val << endl;
		    cio().end_err(/*aborting*/true);
		    abort();
		  }
	    }
	    this->imap = imap2;
	    imap2 = (IntIntTblLR *)NULL; // drop on floor

	    // test if resulting map is the identity; if so, delete it
	    int mapSz = this->imap->Size();
	    for (k = 0; k < mapSz; k++) {
		if ((!(this->imap->Get(k, /*OUT*/ v))) || (k != v))
		    // not the identity; exit loop early
		    break;
	    }
	    if (k == mapSz) {
		// "imap" is the identity, so drop it on the floor
		this->imap = (IntIntTblLR *)NULL;
	    }
	}
    }
}

void CE::T::CycleNames(const BitVector &delBV,
  const IntIntTblLR &delMap) throw ()
/* REQUIRES VPKFile(SELF).mu IN LL */
{
  try {
    assert((uncommonNames != NULL) && (!delBV.IsEmpty()));

    // fill in "imap" if it is the identity
    if (this->imap == (IntIntTblLR *)NULL) {
      this->imap = NEW_CONSTR(IntIntTblLR, (fps->len));
      for (unsigned int k = 0; k < fps->len; k++) {
	bool inTbl = this->imap->Put(k, k); assert(!inTbl);
      }
    }

    // update "uncommonNames", "imap"
    BVIter it(&delBV); unsigned int oldBx; IntIntTblLR::int_type newBx;
    while (it.Next(/*OUT*/ oldBx)) {
	// update "this->uncommonNames"
	bool inTbl = delMap.Get(oldBx, /*OUT*/ newBx); assert(inTbl);
	bool set = this->uncommonNames->Reset(oldBx); assert(set);
        set = this->uncommonNames->Set(newBx); assert(!set);

	// update "this->imap"
	IntIntTblLR::int_type fpIndex;
	inTbl = this->imap->Delete(oldBx, /*OUT*/ fpIndex); assert(inTbl);
	inTbl = this->imap->Put(newBx, fpIndex); assert(!inTbl);
    }

    // update "this->uncommonTag"
    /* Note: we don't have to recompute the XOR because it won't change;
       but we do have to invalidate the unlazied fingerprint (if any),
       since the order of the names has changed. */
    this->uncommonTag.InvalidateFPVal();
  }
  catch(IntIntTblLR::InvalidKey e)
    {
      cio().start_err() << Debug::Timestamp()
			<< "INTERNAL ERROR: "
			<< "IntIntTblLR::InvalidKey caugt in CE::T::CycleNames (" 
			<< __FILE__ << ":" << __LINE__ << ")" << endl
			<< "  value = " << e.key << endl;
      cio().end_err(/*aborting*/true);
      abort();
    }
  catch(IntIntTblLR::InvalidValue e)
    {
      cio().start_err() << Debug::Timestamp()
			<< "INTERNAL ERROR: "
			<< "IntIntTblLR::InvalidValue caugt in CE::T::CycleNames (" 
			<< __FILE__ << ":" << __LINE__ << ")" << endl
			<< "  value = " << e.val << endl;
      cio().end_err(/*aborting*/true);
      abort();
    }

}

void CE::T::Update(const BitVector *exCommonNames,
  const BitVector *exUncommonNames, const BitVector *packMask,
  const IntIntTblLR *reMap) throw ()
/* REQUIRES VPKFile(SELF).mu IN LL */
/* REQUIRES (packMask == NULL) == (reMap == NULL) */
{
    if (exCommonNames != NULL || exUncommonNames != NULL) {
	// update "uncommonNames"
	if (exCommonNames != (BitVector *)NULL)
	    *(this->uncommonNames) |= *exCommonNames;
	if (exUncommonNames != (BitVector *)NULL)
	    *(this->uncommonNames) -= *exUncommonNames;

	// update uncommon tag
	this->uncommonTag.Init(*(this->fps),
          *(this->uncommonNames), this->imap);
    }

    // pack "unCommonNames" and "imap" (if necessary)
    Pack(packMask, reMap);
}

void CE::T::XorUncommonNames(const BitVector &mask) throw ()
/* REQUIRES VPKFile(SELF).mu IN LL */
{
    *(this->uncommonNames) ^= mask;
    this->uncommonTag.Init(*(this->fps), *(this->uncommonNames), this->imap);
}


// Note: this method is a class member, not an instance member.  There
// is an instance member of the same name that calls this function.
bool CE::T::CheckUsedNames(const BitVector *commonNames,
			   const BitVector *uncommonNames,
			   const IntIntTblLR* imap,
			   const FP::List *fps,
			   /*OUT*/ unsigned int &missing)
  throw ()
{
  if(imap != 0)
    {
      IntIntTblIter it(imap);
      IntIntTblLR::int_type k, v;
      while (it.Next(/*OUT*/ k, /*OUT*/ v))
	{
	  // Every key in imap must be either commonNames or
	  // this->uncommonNames.
	  if(((commonNames == 0) || !commonNames->Read(k)) &&
	     ((uncommonNames == 0) || !uncommonNames->Read(k)))
	    {
	      missing = k;
	      return true;
	    }
	}
    }
  // Identity map: use the length of fps to determine which names are
  // used by this entry.
  else if(fps != 0)
    {
      for(unsigned int i = 0; i < fps->len; i++)
	{
	  // Every name for which we have a fingerprint must be in
	  // either commonNames or this->uncommonNames.
	  if(((commonNames == 0) || !commonNames->Read(i)) &&
	     ((uncommonNames == 0) || !uncommonNames->Read(i)))
	    {
	      missing = i;
	      return true;
	    }
	}
    }

  // If we make it to the end, then we found no problems.
  return false;
}

void CE::T::Log(VestaLog& log) const
  throw (VestaLog::Error)
/* REQUIRES VPKFile(SELF).mu IN LL */
{
    log.write((char *)(&(this->ci)), sizeof(this->ci));
    this->uncommonNames->Log(log);
    if (this->uncommonNames->Size() > 0) {
	this->uncommonTag.Log(log);
    }
    this->value->Log(log);
    this->kids->Log(log);
    Model::Log(this->model, log);
    if (this->imap == (IntIntTblLR *)NULL) {
	IntIntTblLR empty;
	empty.Log(log);
    } else {
	this->imap->Log(log);
    }
    this->fps->Log(log);
}

void CE::T::Recover(RecoveryReader &rd)
  throw (VestaLog::Error, VestaLog::Eof)
/* REQUIRES VPKFile(SELF).mu IN LL */
{
    rd.readAll((char *)(&(this->ci)), sizeof(this->ci));
    this->uncommonNames = NEW_CONSTR(BitVector, (rd));
    if (this->uncommonNames->Size() > 0) {
	this->uncommonTag.Recover(rd);
    } else {
	this->uncommonTag.Zero();
    }
    this->value = NEW_CONSTR(VestaVal::T, (rd));
    this->kids = NEW_CONSTR(CacheEntry::Indices, (rd));
    Model::Recover(/*OUT*/ this->model, rd);
    this->imap = NEW_CONSTR(IntIntTblLR, (rd));
    if (this->imap->Size() == 0) {
	// drop it on the floor
	this->imap = (IntIntTblLR *)NULL;
    }
    this->fps = NEW_CONSTR(FP::List, (rd));
}

void CE::T::Write(ostream &ofs, ifstream *ifs) const throw (FS::Failure)
/* REQUIRES VPKFile(SELF).mu IN LL */
{
    // write main values
    CacheEntry::Index ciToWrite = this->ci;
    FS::Write(ofs, (char *)(&ciToWrite), sizeof(ciToWrite));
    this->uncommonNames->Write(ofs);
    if (this->uncommonNames->Size() > 0) {
	this->uncommonTag.Write(ofs);
    }
    if (this->value_immut != 0) {
	// immutable values stored in backing file "ifs"
	assert(ifs != (ifstream *)NULL);
	assert(this->kids_immut != 0);
	this->value_immut->Copy(*ifs, ofs);
    } else {
	// normal case; write values in memory
        assert(this->value != 0);
	this->value->Write(ofs);
    }
    if(this->kids_immut != 0)
      {
	assert(ifs != (ifstream *)NULL);
	this->kids_immut->Copy(*ifs, ofs);
      }
    else
      {
	assert(this->kids != 0);
	this->kids->Write(ofs);
      }
    Model::Write(this->model, ofs);
}

void CE::T::Read(istream &ifs, bool old_format, bool readImmutable)
  throw (FS::EndOfFile, FS::Failure)
/* REQUIRES VPKFile(SELF).mu IN LL */
{
    // read main values
    FS::Read(ifs, (char *)(&(this->ci)), sizeof(this->ci));
    this->uncommonNames = NEW_CONSTR(BitVector, (ifs));
    if (this->uncommonNames->Size() > 0) {
	this->uncommonTag.Read(ifs);
    } else {
	this->uncommonTag.Zero();
    }
    // Note that we must read the value into memory if we're using the
    // old format even if readImmutable is false.  (It contains an
    // embedded PrefixTbl, and we can't copy that from an old format
    // file to a new format file.)
    if(readImmutable || old_format)
      {
	this->value = NEW_CONSTR(VestaVal::T, (ifs, old_format));
	this->value_immut = 0;
      }
    else
      {
	this->value_immut = VestaVal::T::ReadImmutable(ifs, old_format);
	this->value = 0;
      }
    if (readImmutable) {
      this->kids = NEW_CONSTR(CacheEntry::Indices, (ifs));
      this->kids_immut = 0;
    } else {
      this->kids_immut = CacheEntry::Indices::ReadImmutable(ifs);
      this->kids = 0;
    }
    Model::Read(/*OUT*/ this->model, ifs);

    // null out ``extra'' fields
    this->imap = (IntIntTblLR *)NULL;
    this->fps = (FP::List *)NULL;
}

void CE::T::WriteExtras(ostream &ofs, ifstream *ifs) const throw (FS::Failure)
/* REQUIRES VPKFile(SELF).mu IN LL */
{
    if (this->imap == (IntIntTblLR *)NULL) {
	IntIntTblLR empty;
	empty.Write(ofs);
    } else {
	this->imap->Write(ofs);
    }
    assert(this->fps != 0);
    this->fps->Write(ofs);
}

void CE::T::ReadExtras(istream &ifs, bool old_format, bool readImmutable)
  throw (FS::EndOfFile, FS::Failure)
/* REQUIRES VPKFile(SELF).mu IN LL */
{
  this->imap = NEW_CONSTR(IntIntTblLR, (ifs, old_format));
  if (this->imap->Size() == 0) {
    // drop it on the floor
    this->imap = (IntIntTblLR *)NULL;
  }
  this->fps = NEW_CONSTR(FP::List, (ifs));
}

inline void Indent(ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << " ";
}

void CE::T::Debug(ostream &os, int indent) const throw ()
/* REQUIRES VPKFile(SELF).mu IN LL */
{
    Indent(os, indent); os << "ci    = " << this->ci << endl;
    Indent(os, indent); os << "unFVs = {" << endl;
    this->uncommonNames->PrintAll(os, indent+4);
    Indent(os, indent); os << "  } ("
			   << this->uncommonNames->Cardinality() << " total)"
			   << endl;
    if (this->uncommonNames->Size() > 0) {
	Indent(os, indent);
	os << "unTag =" << endl; this->uncommonTag.Debug(os, indent+2);
	os << endl;
    }
    Indent(os, indent);
    os << "value =" << endl; this->value->Print(os, indent + 2);
    Indent(os, indent); os << "kids  = "; this->kids->Print(os, indent + 2);
    Indent(os, indent); os << "model = " << this->model << endl;
}

void CE::T::DebugExtras(ostream &os, int indent) const throw ()
/* REQUIRES VPKFile(SELF).mu IN LL */
{
    Indent(os, indent); os << "imap = ";
    if (this->imap == (IntIntTblLR *)NULL) {
	os << "<identity-map>" << endl;
    } else {
	this->imap->Print(os, indent+2);
    }
    Indent(os, indent);
    os << "fps  =" << endl; this->fps->Print(os, indent + 2);
}

unsigned CE::List::Length(const List *list) throw ()
{
    unsigned res = 0;
    for (/*SKIP*/; list != (List *)NULL; list = list->tail) res++;
    return res;
}
