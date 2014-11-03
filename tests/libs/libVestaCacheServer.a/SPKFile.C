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
#include <FP.H>

// cache-common
#include <BitVector.H>
#include <FV.H>
#include <CompactFV.H>
#include <TextIO.H>
#include <Debug.H>

// cache-server
#include "SPKFileRep.H"
#include "IntIntTblLR.H"
#include "CacheEntry.H"
#include "VPKFileChkPt.H"
#include "SPKFile.H"

using std::streampos;
using std::ostream;
using std::ifstream;
using std::endl;
using OS::cio;

// Update Methods -------------------------------------------------------------

bool SPKFile::Update(const VPKFileChkPt *chkpt, const BitVector *toDelete,
  /*OUT*/ BitVector* &exCommonNames, /*OUT*/ BitVector* &exUncommonNames,
  /*OUT*/ BitVector* &packMask, /*OUT*/ IntIntTblLR* &reMap,
  /*OUT*/ bool &isEmpty) throw ()
/* REQUIRES ((chkpt != NULL) && chkpt->hasNewEntries) || (toDelete != NULL) */
/* ENSURES (packMask == NULL) == (reMap == NULL) */
/* See the spec for this method in "SPKFile.H". Here is an overview of how
   the method is implemented.

   First, all the entries in "this->entries" and in the checkpoint are
   scanned. During this scanning process, any entries named in "toDelete"
   are ignored. The scanning is done to compute:

   int totalEntries -- the total number of undeleted entries
   bool newChkptEntries -- does the checkpoint have any undeleted entries?
   BitVector newAllNames -- the union of all names in all undeleted entries
   BitVector newCommonNames -- the intersection of all names in all
     undeleted entries

   Comuting the intersection "newCommonNames" is a bit tricky. On the very
   first encountered entry, it is set to that entry's names. On all subsequent
   entries, it is computed as the intersection of its current value and each
   entry's names. The local variable "totalEntries" is used to decide which
   case to pursue.

   Second, the entries in "this->oldEntries" are updated according to
   "toDelete", "exCommonNames", "exUncommonNames", "packMask", and "reMap".
   That is, any deleted entries are removed from the table, their
   "uncommonNames" sets are updated and packed, and they are added back to the
   table under their (potentially new) common fingerprint. Similarly, the new
   entries in "chkpt" are added to "this->oldEntries". */
{
    // initialize "chkpt" from this PKFile if necessary
    if (chkpt == (VPKFileChkPt *)NULL) {
	chkpt = this->CheckPoint();
	assert(!(chkpt->hasNewEntries));
    } else if (this->sourceFunc == (Text *)NULL) {
        // propogate "sourceFunc" if necessary
	this->sourceFunc = chkpt->sourceFunc;
    }

    // check precondition; initialize OUT parameters
    assert(chkpt->hasNewEntries || (toDelete != (BitVector *)NULL));
    exCommonNames = exUncommonNames = packMask = (BitVector *)NULL;
    reMap = (IntIntTblLR *)NULL;

    // compute new value for set of all names and set of common names
    /* Note: during the processing of ``common'' entries in "this->entries"
       and "chkpt->newCommon", the variables "newAllNames" and
       "newCommonNames" will actually contain the join and meet, respectively,
       of the entries' *uncommon* names, since all of these entries contain
       the common names by definition. After these entries have been
       processed, the "commonNames" are OR'ed into both variables if there
       were any common entries before processing the ``uncommon'' entries
       in "chkpt->newUncommon". */
    BitVector newAllNames, newCommonNames;
    int totalEntries = 0;        // total number of new entries
    bool thisDelOne = false;     // any entries deleted from this SPKFile?
    if (toDelete == (BitVector *)NULL) {
	// fast path: nothing will change, so compute "newAllNames",
	// "newCommonNames", and "totalEntries" from existing values
        if(this->allNames.len > 0)
	  newAllNames.SetInterval(0, this->allNames.len - 1);
	newAllNames -= this->commonNames;
	totalEntries = this->oldEntries.Size();
    } else {
	// scan "this->entries", skipping any to be deleted
	ScanCommonTable(this->oldEntries, toDelete,
          /*INOUT*/ newAllNames, /*INOUT*/ newCommonNames,
          /*INOUT*/ totalEntries, /*INOUT*/ thisDelOne);
    }

    bool newDelOne = false;   // any new entries deleted?
    int numCommonEntries = totalEntries; // number of new common entries (temp)
    int numStableEntries = totalEntries; // remember current count (temp)

    // scan "chkpt->newCommon" entries
    if (chkpt->newCommon.Size() > 0) {
    	ScanCommonTable(chkpt->newCommon, toDelete,
    	  /*INOUT*/ newAllNames, /*INOUT*/ newCommonNames,
    	  /*INOUT*/ totalEntries, /*INOUT*/ newDelOne);
	numCommonEntries = totalEntries;
    }

    // include "commonNames" if there were any common entries
    if (numCommonEntries > 0) {
	newAllNames |= this->commonNames;
	newCommonNames |= this->commonNames;
    }

    // scan "chkpt->newUncommon" entries
    if (chkpt->newUncommon != (CE::List *)NULL) {
	ScanList(chkpt->newUncommon, toDelete,
          /*INOUT*/ newAllNames, /*INOUT*/ newCommonNames,
          /*INOUT*/ totalEntries, /*INOUT*/ newDelOne);
    }

    // does the checkpoint have any new entries?
    bool newChkptEntries = (totalEntries > numStableEntries);
    bool delAny = (thisDelOne || newDelOne);

    /* The new version of the PKFile will be empty if none of its original
       entries are preserved and if there are no new entries to add from
       the checkpoint. It is new if its "pkEpoch" is 0. */
    isEmpty = (totalEntries == 0);           // is new version empty?
    bool isNewPKFile = (this->pkEpoch == 0); // is this a new PKFile?

    // do nothing if the PKFile is unchanged
    /* The PKFile must be changed if there were new entries in the checkpoint
       to add, if some entries from the SPKFile must be deleted, or if some
       new entries were deleted. The latter is required so that the "pkEpoch"
       gets bumped even if there were no entries to add or delete from the
       file on disk. */
    bool pkfileModified = (newChkptEntries || delAny);
    if (!pkfileModified) return pkfileModified;

    // compute "exCommonNames", "exUncommonNames"
    if (isNewPKFile) {
	// PKFile is new; set "exUncommonNames" only
	/* NB: The code below (for the "!isNewPKFile" case) would do the
           exact same thing, but this is a more efficient implementation
           for the "isNewPKFile" case. */
	if (!(newCommonNames.IsEmpty())) {
	  exUncommonNames = NEW_CONSTR(BitVector, (&newCommonNames));
	}
    } else if (isEmpty) {
	// the SPKFile is empty, so there are no more common names
	/* This is a special case that could be handled by the more general
	   "else" branch that follows. */
      exCommonNames = NEW_CONSTR(BitVector, (&(this->commonNames)));
    } else {
	// set "bothCommon = (commonNames & newCommonNames)"
	BitVector bothCommon(&(this->commonNames));
	bothCommon &= newCommonNames;
	// Only set "exCommonNames" or "exUncommonNames" if they
	// would be non-empty.
	if (bothCommon < this->commonNames) {
	  exCommonNames = NEW_CONSTR(BitVector, (&(this->commonNames)));
	    *exCommonNames -= newCommonNames;
	}
	if (bothCommon < newCommonNames) {
	  exUncommonNames = NEW_CONSTR(BitVector, (&newCommonNames));
	    *exUncommonNames -= this->commonNames;
	}
    }

    // update "commonNames" if necessary
    bool commonNamesChanged =
      ((exCommonNames != NULL) || (exUncommonNames != NULL));
    if (commonNamesChanged) {
	this->commonNames = newCommonNames;
    }

    /* Note: at this point, "exCommonNames", "exUncommonNames", "commonNames",
       "newCommonNames", and "newAllNames" are all ``unpacked''. */

    if (isEmpty) {
	// reset data structures when the PKFile is empty
	// note: "this->commonNames" has already been set
	this->allNames.Reset();
	this->oldEntries.Init();

	// allocate empty "packMask", "reMap" so existing names in
        // the VPKFile will be deleted
	packMask = NEW(BitVector);
	reMap = NEW_CONSTR(IntIntTblLR, (/*sizeHint=*/ (Basics::uint32) 0));
    } else {
	// The PKFile is non-empty; update its data structures

	// set "reMap" if some names are removed from "allNames"
	/* Note: this can only happen if some entries were deleted from this
           SPKFile or from the checkpoint (we have to consider the entries 
           in the checkpoint because we are comparing "newAllNames" with the
	   names stored in the checkpoint). */
	if (delAny) {
	    unsigned int nextAvail = newAllNames.NextAvail(/*setIt =*/ false);
	    assert(nextAvail <= chkpt->allNamesLen);
	    if (nextAvail < chkpt->allNamesLen) {
	      // all least one of the bits in "newAllNames" is unset
	      packMask = NEW_CONSTR(BitVector, (&newAllNames));
	      reMap = NEW_CONSTR(IntIntTblLR,
				 (/*sizeHint =*/ chkpt->allNamesLen));
	      BVIter it(*packMask);
	      unsigned int k;
	      for (int i = 0; it.Next(/*OUT*/ k); i++) {
		// bit "k" in "newAllNames" -> bit "i" in new version
		try
		  {
		    bool inTbl = reMap->Put(k, i);
		    assert(!inTbl);
		  }
		catch(IntIntTblLR::InvalidKey e)
		  {
		    cio().start_err() << Debug::Timestamp()
				      << "INTERNAL ERROR: "
				      << "IntIntTblLR::InvalidKey caugt in SPKFile::Update" 
				      << endl << "  value = " << e.key << endl;
		    cio().end_err(/*aborting*/true);
		    abort();
		  }
		catch(IntIntTblLR::InvalidValue e)
		  {
		    cio().start_err() << Debug::Timestamp()
				      << "INTERNAL ERROR: "
				      << "IntIntTblLR::InvalidValue caugt in SPKFile::Update" 
				      << endl << "  value = " << e.val << endl;
		    cio().end_err(/*aborting*/true);
		    abort();
		  }

	      }
	    }
	}
	assert((packMask == NULL) == (reMap == NULL));
	assert((packMask == NULL) || delAny);

	// update "allNames"
	// first, append any new names in the checkpoint
	for (int i = this->allNames.len; i < chkpt->allNamesLen; i++) {
	    this->allNames.Append(chkpt->allNames->name[i]);
	}
	// next, pack the names if necessary
	this->allNames.Pack(packMask);

	// update "this->oldEntries" if necessary
	if (isNewPKFile) {
	    // This is the first time this PKFile is being flushed, so we
	    // don't have to update "oldEntries" at all
	    assert(this->oldEntries.Size() == 0);
	} else if (thisDelOne || commonNamesChanged) {
	    // Only update "this->oldEntries" if some of its entries are
	    // to be deleted or if the set of common names have changed.

	    // update "entries"
	    assert(this->oldEntries.Size() > 0);
	    UpdateCommonTable(this->oldEntries, toDelete, exCommonNames,
              exUncommonNames, packMask, reMap, /*unlazyTag=*/ true);
	}

	// add all new entries in "chkpt" to "this->oldEntries"
	if (chkpt->hasNewEntries) {
	    AddEntries(*chkpt, toDelete, exCommonNames, exUncommonNames,
              packMask, reMap, /*unlazyTag=*/ true);
	}

	// pack "commonNames"
	this->commonNames.Pack(packMask);
    }

    // update epochs
    // Usually this->pkEpoch will already have been updated, but not
    //  in the case where this SPKFile does not already exist on disk.
    this->pkEpoch = chkpt->pkEpoch + 1;      // incr. checkpointed pkEpoch
    this->namesEpoch = chkpt->namesEpoch;    // store checkpointed names epoch
    if (reMap != NULL) {
	// bump names epoch since names have changed again by being collapsed
	this->namesEpoch++;
    }
    assert(pkfileModified);
    return pkfileModified;
} // SPKFile::Update

VPKFileChkPt *SPKFile::CheckPoint() throw ()
{
  VPKFileChkPt *res = NEW(VPKFileChkPt);

  res->sourceFunc = this->sourceFunc;
  res->pkEpoch = this->pkEpoch;
  res->namesEpoch = this->namesEpoch;
  res->allNamesLen = this->allNames.len;
  res->allNames = &(this->allNames);
  return res;
}

void SPKFile::ScanCommonTable(const CFPEntryMap &cfpTbl,
  const BitVector *toDelete, /*INOUT*/ BitVector &uncommonJoin,
  /*INOUT*/ BitVector &uncommonMeet, /*INOUT*/ int &totalEntries,
  /*INOUT*/ bool &delOne) const throw ()
{
    if (cfpTbl.Size() > 0) {
	CFPEntryIter iter(&cfpTbl);
	FP::Tag cfp; CE::List *curr;
	while (iter.Next(/*OUT*/ cfp, /*OUT*/ curr)) {
	    ScanList(curr, toDelete, /*INOUT*/ uncommonJoin,
	      /*INOUT*/ uncommonMeet, /*INOUT*/ totalEntries,
              /*INOUT*/ delOne);
	}
    }
}

void SPKFile::ScanList(const CE::List *head, const BitVector *toDelete,
  /*INOUT*/ BitVector &uncommonJoin, /*INOUT*/ BitVector &uncommonMeet,
  /*INOUT*/ int &totalEntries, /*INOUT*/ bool &delOne) const throw ()
{
    for (const CE::List *curr = head; curr != NULL; curr = curr->Tail()) {
	CE::T *ce = curr->Head();
	if (toDelete != NULL && toDelete->Read(ce->CI())) {
	    // skip this entry, since it is being deleted
	    delOne = true;
	} else {
	    // this entry is not marked for deletion
	    uncommonJoin |= ce->UncommonNames();
	    if (totalEntries > 0) {
		// form intersection
		uncommonMeet &= ce->UncommonNames();
	    } else {
		// first one seen; just assign it to start off
		uncommonMeet = ce->UncommonNames();
	    }
	    totalEntries++;
	}
    }
}

CE::List* SPKFile::ExtractEntries(/*INOUT*/ CFPEntryMap &cfpTbl) throw ()
{
    // move existing entries from the table to "res"
    CE::List *res = (CE::List *)NULL, *curr;
    FP::Tag cfp;
    CFPEntryIter iter(&cfpTbl);
    while (iter.Next(/*OUT*/ cfp, /*OUT*/ curr)) {
	while (curr != (CE::List *)NULL) {
	  res = NEW_CONSTR(CE::List, (curr->Head(), res));
	  curr = curr->Tail();
	}
    }
    // clear the table
    cfpTbl.Init();
    return res;
}

void SPKFile::UpdateCommonTable(/*INOUT*/ CFPEntryMap &cfpTbl,
  const BitVector *toDelete, const BitVector *exCommonNames,
  const BitVector *exUncommonNames, const BitVector *packMask,
  const IntIntTblLR *reMap, bool unlazyTag) throw ()
/* REQUIRES cfpTbl.Size() > 0 */
/* REQUIRES (packMask == NULL) == (reMap == NULL) */
{
    // ensure pre-condition
    assert(cfpTbl.Size() > 0);
    assert((packMask == NULL) == (reMap == NULL));

    // move existing entries from the table to a list named "existing"
    CE::List *existing = ExtractEntries(cfpTbl);

    // Recompute fingerprints of existing entries, and add them
    // back into the table.
    UpdateList(cfpTbl, existing, toDelete, exCommonNames, exUncommonNames,
      packMask, reMap, unlazyTag);
}

void SPKFile::AddEntries(const VPKFileChkPt &chkpt, const BitVector *toDelete,
  const BitVector *exCommonNames, const BitVector *exUncommonNames,
  const BitVector *packMask, const IntIntTblLR *reMap, bool unlazyTag)
  throw ()
/* REQUIRES (packMask == NULL) == (reMap == NULL) */
{
    assert((packMask == NULL) == (reMap == NULL));

    // add "newCommon" entries (if any)
    if (chkpt.newCommon.Size() > 0) {
	assert(this->pkEpoch > 0);
	CFPEntryIter iter(&(chkpt.newCommon));
	FP::Tag cfp; CE::List *ents;
	while (iter.Next(/*OUT*/ cfp, /*OUT*/ ents)) {
	    UpdateList(this->oldEntries, ents, toDelete, exCommonNames,
	      exUncommonNames, packMask, reMap, unlazyTag);
	}
    }

    // add "newUncommon" entries
    /* Note: Since these entries are all ``uncommon'', their "uncomonNames"
       bit vectors are exactly their free variables, possibly including some
       of this entry's common names. Hence, we don't have to update their
       "uncommonNames" according to "exCommonNames" and "exUncommonNames".
       Instead, we must subtract off the new version of "commonNames". We do
       this by passing NULL for "exCommonNames" and "commonNames" for
       "exUncommonNames". */
    UpdateList(this->oldEntries, chkpt.newUncommon, toDelete,
      /*exCommonNames=*/ (BitVector *)NULL, /*exUncommonNames=*/ &commonNames,
      packMask, reMap, unlazyTag);
}

void SPKFile::UpdateList(/*INOUT*/ CFPEntryMap &cfpTbl,
  const CE::List *ents, const BitVector *toDelete,
  const BitVector *exCommonNames, const BitVector *exUncommonNames,
  const BitVector *packMask, const IntIntTblLR *reMap, bool unlazyTag)
  throw ()
/* REQUIRES (packMask == NULL) == (reMap == NULL) */
{
    for (/*SKIP*/; ents != (CE::List *)NULL; ents = ents->Tail()) {
	CE::T *ce = ents->Head();
	if ((toDelete == (BitVector *)NULL) || !(toDelete->Read(ce->CI()))) {
	    // compute common fingerprint
	    /* Note: this must be done *before* the entry is updated since
	       "commonNames" has not been packed at this time. */
	    FP::Tag *commonFP = ce->CombineFP(commonNames);

	    // update the entry
	    ce->Update(exCommonNames, exUncommonNames, packMask, reMap);

	    // unlazy the cache entry's uncommon tag (if necessary)
	    if (unlazyTag) ce->UnlazyUncommonFP();

	    // add it to "cfpTbl"
	    AddEntryToTable(cfpTbl, *commonFP, ce);
	}
    }
}

void SPKFile::AddEntryToTable(/*INOUT*/ CFPEntryMap &cfpTbl,
  const FP::Tag &cfp, CE::T *ce) const throw ()
{
    CE::List *curr;
    if (!cfpTbl.Get(cfp, /*OUT*/ curr)) {
	curr = (CE::List *)NULL;
    }
    curr = NEW_CONSTR(CE::List, (ce, curr));
    (void)cfpTbl.Put(cfp, curr);
}

// Lookup Methods -------------------------------------------------------------

/* static */
CE::T* SPKFile::Lookup(ifstream &ifs, streampos origin,
  int version, const FP::Tag &commonTag, const FP::List &fps)
  throw (FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ifs) == origin == ``start of <PKFile>'' */
{
    // verify precondition
    assert(FS::Posn(ifs) == origin);

    // read header
    SPKFileRep::Header hdr(ifs, origin, version, /*readEntries=*/ false);

    // seek to proper <PKEntries>
    streampos offset = LookupCFP(ifs, origin, version, commonTag, hdr);
    if (offset < 0) return (CE::T *)NULL;
    FS::Seek(ifs, origin + offset);

    // lookup result based on "version"
    CE::T *res;
    if (version <= SPKFileRep::LargeFVsVersion) {
	res = LookupEntryV1(ifs, version, fps);
    } else {
	// no support for other versions
	assert(false);
    }
    return res;
}

/* static */
streampos SPKFile::LookupCFP(ifstream &ifs, streampos origin,
  int version, const FP::Tag &cfp, const SPKFileRep::Header &hdr)
  throw (FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ifs) == ``start of <CFPTypedHeader>'' */
{
    streampos res;
    switch (hdr.type) {
      case SPKFileRep::HT_List:
	if (version <= SPKFileRep::LargeFVsVersion) {
	    res = LookupCFPInListV1(ifs, origin, cfp, hdr.num);
	} else {
	    // no support for other versions
	    assert(false);
	}
	break;
      case SPKFileRep::HT_SortedList:
	if (version <= SPKFileRep::LargeFVsVersion) {
	    res = LookupCFPInSortedListV1(ifs, origin, cfp, hdr.num);
	} else {
	    // no support for other versions
	    assert(false);
	}
	break;
      case SPKFileRep::HT_HashTable:
      case SPKFileRep::HT_PerfectHashTable:
	/*** NYI ***/
	assert(false);
      default:
	assert(false);
    }
    return res;
}

/* static */
streampos SPKFile::LookupCFPInListV1(ifstream &ifs, streampos origin,
  const FP::Tag &cfp, int num) throw (FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ifs) == ``start of <CFPSeqHeader>'' */
{
    for (int i = 0; i < num; i++) {
	SPKFileRep::HeaderEntry he(ifs, origin);
	if (he.cfp == cfp) {
	    // found it!
	    return he.offset;
	}
    }
    return (-1);
}

/* static */
streampos SPKFile::LookupCFPInSortedListV1(ifstream &ifs, streampos origin,
  const FP::Tag &cfp, int num) throw (FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ifs) == ``start of <CFPSeqHeader>'' */
{
    // lookup the entry using binary search
    int lo = 0, hi = num;
    streampos base = FS::Posn(ifs); // record start of entries on disk
    while (lo < hi) {
	// Loop invariant: Matching entry (if any) has index in [lo, hi).
        // On each iteration, the difference "hi - lo" is guaranteed to
        // decrease, so the loop is guaranteed to terminate.

	// read middle entry
	int mid = (lo + hi) / 2;
	assert(lo <= mid && mid < hi); // mid is in [lo, hi)
	FS::Seek(ifs, (base +
		       (streampos) (mid * SPKFileRep::HeaderEntry::Size())));
	SPKFileRep::HeaderEntry midEntry(ifs, origin);

	// compare middle CFP to "cfp"
	int cmp = Compare(cfp, midEntry.cfp);
	if (cmp < 0) {
	    // matching entry in [lo, mid) => make "hi" smaller
	    hi = mid;
	} else if (cmp > 0) {
	    // matching entry in [mid+1, hi) => make "lo" larger
	    lo = mid + 1;
	} else {
	    return (streampos)(midEntry.offset);
	}
    }
    return (-1);
}

/* static */
CE::T* SPKFile::LookupEntryV1(ifstream &ifs, int version,
			      const FP::List &fps)
  throw (FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ifs) == ``start of <PKEntries>'' */
{
    // read number of entries
    SPKFileRep::UShort numEntries;
    FS::Read(ifs, (char *)(&numEntries), sizeof(numEntries));

    // look for match on secondary key
    for (int i = 0; i < numEntries; i++) {
	// read the next entry
        CE::T *ce = NEW_CONSTR(CE::T,
			       (ifs, version < SPKFileRep::LargeFVsVersion));
	assert(ce->UncommonFPIsUnlazied());
        /* Note: no lock is required on the following "FPMatch"
           operation because "ce" is fresh. */
	if (ce->FPMatch(fps)) {
	    return ce;
	}
    }
    return (CE::T *)NULL;
}

// Read/Write Methods ---------------------------------------------------------

void SPKFile::Read(ifstream &ifs, streampos origin, int version,
  /*OUT*/ SPKFileRep::Header* &hdr, bool readEntries, bool readImmutable)
  throw (FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ifs) == ``start of <PKFile>'' */
{
    // verify that we are positioned correctly
    assert(FS::Posn(ifs) == origin);

    // read/skip <CFPHeader>
    if (readEntries) {
      // read header and its corresponding header entries
      hdr = NEW_CONSTR(SPKFileRep::Header,
		       (ifs, origin,
			version, /*readEntries=*/ true));
    } else {
	hdr = (SPKFileRep::Header *)NULL;
	SPKFileRep::Header::Skip(ifs, origin, version);
    }

    // read <PKFHeaderTail>
    if (version >= SPKFileRep::SourceFuncVersion) {
      Text *temp = NEW(Text);  // non-const
      TextIO::Read(ifs, /*OUT*/ *temp);
      this->sourceFunc = temp;
      if (this->sourceFunc->Length() == 0) this->sourceFunc = (Text *)NULL;
    } else {
	this->sourceFunc = (Text *)NULL;
    }
    FS::Read(ifs, (char *)(&(this->pkEpoch)), sizeof(this->pkEpoch));
    FS::Read(ifs, (char *)(&(this->namesEpoch)), sizeof(this->namesEpoch));
    // read CompactFV::List from file
    CompactFV::List compactFV(ifs,
			      // handle 16-bit format
			      version < SPKFileRep::LargeFVsVersion);
    this->allNames.Grow(/*sizeHint=*/ compactFV.num + 5);
    compactFV.ToFVList(/*INOUT*/ this->allNames); // convert to "allNames"
    this->commonNames.Read(ifs);

    // read <PKEntries>* (if necessary)
    if (readEntries) {
	// iterate over the CFP groups
	for (int i = 0; i < hdr->num; i++) {
	    // check that we are positioned correctly
	    assert(FS::Posn(ifs) == origin + (streampos) hdr->entry[i].offset);

	    // read <PKEntries> record
	    CE::List *entryList = this->ReadEntries(ifs, version,
						    readImmutable);

	    // install the entries in "this->oldEntries" table
	    bool inTbl = this->oldEntries.Put(hdr->entry[i].cfp, entryList);
	    assert(!inTbl);
	}
    }
}

CE::List *SPKFile::ReadEntries(ifstream &ifs, int version, bool readImmutable)
  throw (FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ifs) == ``start of <PKEntries>'' */
{
    // read number of entries
    SPKFileRep::UShort numEntries;
    FS::Read(ifs, (char *)(&numEntries), sizeof(numEntries));

    /* Note: no lock is required on the "CE::T::T" and "CE::T::ReadExtras"
       methods below because the cache entry being modified is fresh.
       The same holds for the "CE::List::List" constructor. */

    CE::T *entry;
    CE::List *head = (CE::List *)NULL, *prev, *curr;

    // read <PKEntry>*, building up resulting list (in order)
    for (int i = 0; i < numEntries; i++) {
      entry = NEW_CONSTR(CE::T, (ifs, version < SPKFileRep::LargeFVsVersion,
				 readImmutable));
      curr = NEW_CONSTR(CE::List, (entry, (CE::List *)NULL));
      if (head == (CE::List *)NULL)
	head = curr;	  // first entry
      else
	prev->SetTail(curr);  // append new entry to end of list
      prev = curr;
    }

    // read <PKEntryExtra>*
    for (curr = head; curr != (CE::List *)NULL; curr = curr->Tail()) {
      curr->Head()->ReadExtras(ifs, version < SPKFileRep::LargeFVsVersion,
			       readImmutable);
    }
    return head;
}

void SPKFile::Write(ifstream &ifs, ostream &ofs, streampos origin,
  /*OUT*/ SPKFileRep::Header* &hdr) const
  throw (FS::Failure)
/* REQUIRES FS::Posn(ofs) == ``start of <PKFile>'' && hdr == NULL */
{
    // key/value variables
    FP::Tag key;
    CE::List *entryList;

    // initialize SPKFileRep::Header
    hdr = NEW(SPKFileRep::Header);
    hdr->num = this->oldEntries.Size();
    hdr->entry = NEW_ARRAY(SPKFileRep::HeaderEntry, hdr->num);
    CFPEntryIter it(&(this->oldEntries));
    int i;
    for (i = 0; it.Next(/*OUT*/ key, /*OUT*/ entryList); i++) {
	assert(i < hdr->num);
	hdr->entry[i].cfp = key;
    }

    // write <CFPHeader>
    hdr->Write(ofs, origin);

    // write <PKFHeaderTail>
    const Text *t = (this->sourceFunc != (Text *)NULL)
      ? this->sourceFunc : NEW(Text);
    TextIO::Write(ofs, *t);
    FS::Write(ofs, (char *)(&(this->pkEpoch)), sizeof(this->pkEpoch));
    FS::Write(ofs, (char *)(&(this->namesEpoch)), sizeof(this->namesEpoch));
    try
      {
	CompactFV::List compactFV(this->allNames);
	compactFV.Write(ofs);
      }
    // Constructing the CompactFV from this->allNames uses a
    // PrefixTbl.  This can throw PrefixTbl::Overflow indicating that
    // we've exceeded the internal limits of the PrefixTbl class (by
    // trying to add the 2^16th entry).  If that happens we print an
    // error message and abort, probably dumping core.
    catch(PrefixTbl::Overflow)
      {
	cio().start_err() << Debug::Timestamp()
		<< "INTERNAL ERROR: "
		<< "PrefixTbl overflow in writing free variables to "
		<< "stable PKFile:" << endl
		<< "  pk         = " << pk << endl
		<< "  sourceFunc = " << ((this->sourceFunc != (Text *)NULL)
					 ? this->sourceFunc->cchars()
					 : "<UNKNOWN>") << endl
		<< "(This means you've exceeded internal limits of the" << endl
		<< "cache server, and you may have to erase your cache.)" << endl;
	cio().end_err(/*aborting*/true);
	// This is a fatal error.  Abort (which should also dump  core).
	abort();
      }
    this->commonNames.Write(ofs);

    // write <PKEntries>*
    for (i = 0; i < hdr->num; i++) {
	SPKFileRep::HeaderEntry *entryPtr = &(hdr->entry[i]);
	entryPtr->offset = FS::Posn(ofs) - origin;
	bool inTbl = this->oldEntries.Get(entryPtr->cfp, /*OUT*/ entryList);
	assert(inTbl);
	WriteEntries(ifs, ofs, entryList);
    }
} // SPKFile::Write

void SPKFile::WriteEntries(ifstream &ifs, ostream &ofs, CE::List *entryList)
  const throw (FS::Failure)
/* REQUIRES FS::Posn(ofs) == ``start of <PKEntries>'' */
{
    // write number of entries
    SPKFileRep::UShort numEntries = CE::List::Length(entryList);
    FS::Write(ofs, (char *)(&numEntries), sizeof(numEntries));

    // write <PKEntry>*
    CE::List *curr;
    for (curr = entryList; curr != NULL; curr = curr->Tail()) {
      {
	unsigned int missing;
	if(curr->Head()->CheckUsedNames(&(this->commonNames), /*OUT*/ missing))
	  {
	    cio().start_err() << Debug::Timestamp()
		 << "INTERNAL ERROR: "
		 << "Detected incorrect commonNames|uncommonNames:" << endl
		 << "  pk           = " << pk << endl
		 << "  ci           = " << curr->Head()->CI() << endl
		 << "  missing name = " << missing << endl << endl
		 << " (Please report this as a bug.)" << endl;
	    cio().end_err(/*aborting*/true);
	    // This is a fatal error.  Abort (which should also dump core).
	    abort();
	  }
      }

	curr->Head()->Write(ofs, &ifs);
    }

    // write <PKEntryExtra>*
    for (curr = entryList; curr != NULL; curr = curr->Tail()) {
      curr->Head()->WriteExtras(ofs, &ifs);
    }
}

static void SPKFile_WriteBanner(ostream &os, char c, int width) throw ()
{
    os << "// "; width -= 3;
    for (int i = 0; i < width; i++) os << c;
    os << endl;
}

void SPKFile::Debug(ostream &os, const SPKFileRep::Header &hdr,
  streampos origin, bool verbose) const throw ()
{
    const int BannerWidth = 75;

    // write general header
    os << endl;
    SPKFile_WriteBanner(os, '+', BannerWidth);
    os << "// <PKFile> (offset " << origin << ")" << endl;
    SPKFile_WriteBanner(os, '+', BannerWidth);
    if (!verbose) {
	os << endl << "pk  = " << pk << endl;
    } else {
	os << "pk        = " << pk << endl;
    }

    // write <CFPHeader>
    hdr.Debug(os, verbose);
    int i;
    for (i = 0; i < hdr.num; i++) {
	hdr.entry[i].Debug(os, verbose);
    }

    if (verbose) {
	// write <PKFHeaderTail>
	os << endl << "// <PKFHeaderTail>" << endl;
        os << "sourceFunc  = " <<
          ((this->sourceFunc != (Text *)NULL)
            ? this->sourceFunc->cchars()
            : "<UNKNOWN>")
          << endl;
	os << "pkEpoch     = " << this->pkEpoch << endl;
	os << "nmsEpoch    = " << this->namesEpoch << endl;
	os << "allNames    =" << endl;
	this->allNames.Print(os, 2, &(this->commonNames));
	os << "commonNms   = {" << endl;
	this->commonNames.PrintAll(os, 4);
	os << "  } (" << this->commonNames.Cardinality() << " total)" << endl;
    }

    // write <PKEntries>*
    for (i = 0; i < hdr.num; i++) {
	// write header info
	os << endl;
	if (verbose) {
	    SPKFile_WriteBanner(os, '-', BannerWidth);
	}
	os << "// <PKEntries> (offset " << hdr.entry[i].offset << ")" << endl;
	CE::List *list;
	bool inTbl = this->oldEntries.Get(hdr.entry[i].cfp, /*OUT*/ list);
	assert(inTbl);
	os << "numEntries = " << CE::List::Length(list) << endl;

	if (verbose) {
	    // write entries
	    CE::List *curr;
	    int i;
	    for (curr=list, i=1; curr != NULL; curr=curr->Tail(), i++) {
		os << endl << "// <PKEntry> (#" << i << ")" << endl;
		curr->Head()->Debug(os);
	    }

	    // write entries
	    for (curr=list, i=1; curr != NULL; curr=curr->Tail(), i++) {
		os << endl << "// <PKEntryExtra> (#" << i << ")" << endl;
		curr->Head()->DebugExtras(os);
	    }
	}
    }
} // SPKFile::Debug
