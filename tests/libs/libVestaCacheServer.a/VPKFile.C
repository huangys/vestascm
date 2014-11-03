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

// vesta/basics
#include <Basics.H>
#include <IntKey.H>
#include <FS.H>
#include <Sequence.H>
#include <Generics.H>

// vesta/srpc
#include <SRPC.H>

// vesta/fp
#include <FP.H>

// vesta/cache-common
#include <PKPrefix.H>
#include <BitVector.H>
#include <CacheIntf.H>
#include <Debug.H>
#include <CacheState.H>
#include <Model.H>
#include <PKEpoch.H>
#include <CacheIndex.H>
#include <FV.H>
#include <VestaVal.H>

// vesta/cache-server
#include "CacheConfigServer.H"
#include "IntIntTblLR.H"
#include "Combine.H"
#include "CacheEntry.H"
#include "VPKFileChkPt.H"
#include "SPKFile.H"
#include "VPKFile.H"
#include "SMultiPKFile.H"

using std::streampos;
using std::ifstream;
using std::cerr;
using std::endl;
using std::ostream;
using OS::cio;

VPKFile::VPKFile(const FP::Tag &pk,
  PKFile::Epoch newPKEpoch, FV::Epoch newNamesEpoch) throw (FS::Failure)
  : SPKFile(pk), newUncommon((CE::List *)NULL), freePKFileEpoch(-1),
    evicted(false)
{
    try {
	// read header information from disk
	this->ReadPKFHeaderTail(pk);

	// fill in the extra fields of a VPKFile
	for (int i = 0; i < this->allNames.len; i++) {
	    bool inMap = this->nameMap.Put(this->allNames.name[i], i);
	    assert(!inMap);
	}
	this->isEmpty = false;
    }
    catch (SMultiPKFileRep::NotFound) {
	// fields initialized to empty VPKFile; set "pkEpoch"
	this->pkEpoch = newPKEpoch;
	this->namesEpoch = newNamesEpoch;
	this->isEmpty = true;
    }
}

void VPKFile::ReadPKFHeaderTail(const FP::Tag &pk)
  throw (FS::Failure, SMultiPKFileRep::NotFound)
{
    PKPrefix::T pfx(pk);
    try {
	// seek to PKFile on disk
	ifstream ifs;
	SMultiPKFile::OpenRead(pfx, /*OUT*/ ifs);
	int version;
	try {
	    streampos origin =
	      SMultiPKFile::SeekToPKFile(ifs, pk, /*OUT*/ version);

	    // read its <PKFHeaderTail>
	    SPKFileRep::Header *dummy;
	    this->Read(ifs, origin, version,
              /*OUT*/ /*hdr=*/ dummy, /*readEntries=*/ false);
	} catch (...) { FS::Close(ifs); throw; }
	FS::Close(ifs);
    }
    catch (FS::EndOfFile) {
	// indicates a programming error
	cerr << "Fatal error: premature EOF in MultiPKFile " << pfx
             << "; crashing..." << endl;
	assert(false);
    }
}

bool VPKFile::IsEmpty() throw ()
/* REQUIRES Sup(LL) < SELF.mu */
{
    bool res;
    this->mu.lock();
    res = ((this->oldEntries.Size() == 0) && (this->newCommon.Size() == 0)
	   && (this->newUncommon == (CE::List *)NULL));
    this->mu.unlock();
    return res;
}

static void Etp_CacheS_FreeVariables(int num_names, int sz) throw ()
/* This procedure is a no-op. It's for etp(1) logging purposes only. It
   reflects the size of the result returned by the CacheS::FreeVariables
   method. "num_names" is the number of free variables, and "sz" is
   a lower bound on the size of those names (in bytes). */
{ /* SKIP */ }

void VPKFile::SendAllNames(SRPC *srpc, bool debug, bool old_protocol)
  throw (SRPC::failure, PrefixTbl::Overflow)
/* REQUIRES Sup(LL) < SELF.mu */
{
    this->mu.lock();

    // post debugging
    if (debug) {
      ostream& out_stream = cio().start_out();
      out_stream << Debug::Timestamp() << "RETURNED -- FreeVariables" << endl;
      out_stream << "  epoch = " << this->namesEpoch << endl;
      out_stream << "  isEmpty = " << BoolName[this->isEmpty] << endl;
      out_stream << "  names =" << endl; this->allNames.Print(out_stream, /*indent=*/ 4);
      out_stream << endl;
      cio().end_out();
    }

    // You might think that this assertion should hold true:

    //    assert(!this->isEmpty || (this->allNames.len == 0));

    // But you'd be wrong.  Adding a cache entry can fail after
    // this->allNames has been augmented if there was a duplicate free
    // variable provided by the caller.  This can leave isEmpty set to
    // true and allNames non-empty.

    Etp_CacheS_FreeVariables(this->allNames.len, this->allNames.CheapSize());

    // send values
    try {
	srpc->send_int((int)this->isEmpty);
	this->allNames.Send(*srpc, old_protocol);
	srpc->send_int((int)this->namesEpoch);
    } catch (...) { this->mu.unlock(); throw; }
    this->mu.unlock();
}

CacheIntf::LookupRes VPKFile::Lookup(FV::Epoch id, const FP::List &fps,
  /*OUT*/ CacheEntry::Index &ci, /*OUT*/ const VestaVal::T* &value,
  /*OUT*/ CacheIntf::LookupOutcome &outcome) throw (FS::Failure)
/* REQUIRES Sup(LL) = SELF.mu */
{
    CacheIntf::LookupRes res = CacheIntf::Miss;
    CE::T *ce =  (CE::T *)NULL;  // non-NULL => cache hit

    // initialize OUT parameters
    outcome = CacheIntf::AllMisses;

    // check that "id" is the correct epoch
    if (namesEpoch != id) {
	// "namesEpoch < id" indicates a bug in the client
	res = (namesEpoch < id)
          ? CacheIntf::BadLookupArgs
          : CacheIntf::FVMismatch;
	outcome = CacheIntf::NoOutcome;
    } else if (fps.len != allNames.len) {
        // an "fps" of the wrong length is a bug in the client
	outcome = CacheIntf::NoOutcome;
	res = CacheIntf::BadLookupArgs;
    } else {
	// check for a hit ...
	CE::List *list;
	Combine::FPTag commonTag(fps, this->commonNames);
	// ... vs. new common entries
	if (this->newCommon.Get(commonTag, /*OUT*/ list)
	    && ((ce = LookupInList(list, fps)) != NULL)) {
	    outcome = CacheIntf::NewHits;
	    res = CacheIntf::Hit;
	}
        // ... vs. new uncommon entries
	else if ((ce = LookupInList(this->newUncommon, fps)) != NULL) {
	    outcome = CacheIntf::NewHits;
	    res = CacheIntf::Hit;
	}
	// ... vs. paged-in entries
	else if (this->oldEntries.Get(commonTag, /*OUT*/ list)
		 && ((ce = LookupInList(list, fps)) != NULL)) {
	    outcome = CacheIntf::WarmHits;
	    res = CacheIntf::Hit;
	}
	// ... vs. entries on disk
	else {
	    PKPrefix::T pfx(pk);
	    try {
		// lookup the entry in the SMultiPKFile on disk
		ifstream ifs;
		SMultiPKFile::OpenRead(pfx, /*OUT*/ ifs);
		int version;
		try {
		    streampos origin =
                      SMultiPKFile::SeekToPKFile(ifs, pk, /*OUT*/ version);
		    ce = SPKFile::Lookup(ifs, origin, version, commonTag, fps);
		} catch (...) { FS::Close(ifs); throw; }
		FS::Close(ifs);

		if (ce != (CE::T *)NULL) {
		    outcome = CacheIntf::DiskHits;
		    res = CacheIntf::Hit;

		    // in the event of a hit, add the entry to "oldEntries"
		    this->AddEntryToTable(/*INOUT*/ this->oldEntries,
                      commonTag, ce);
		}
	    }
	    catch (SMultiPKFileRep::NotFound) {
		/* SKIP -- do nothing; if there is no file, then
		   we should report a miss. */
	    }
	    catch (FS::EndOfFile) {
	      // indicates a programming error
	      cerr << "Fatal error: premature EOF in MultiPKFile " << pfx
		   << "; crashing..." << endl;
	      assert(false);
	    }
	}
	if (res == CacheIntf::Hit) {
	    // extract relevant fields
	    assert(ce != (CE::T *)NULL);
	    ci = ce->CI();
	    value = ce->Value();
	}
    }

    return res;
}

CE::T* VPKFile::LookupInList(const CE::List *entries, const FP::List &fps)
  throw ()
/* REQUIRES Sup(LL) = SELF.mu AND ListLock(*entries) IN LL */
{
    for (const CE::List *curr = entries; curr != NULL; curr = curr->Tail()) {
	CE::T *ce = curr->Head();
	if (ce->FPMatch(fps)) {
	    return ce;
	}
    }
    return (CE::T *)NULL;
}

CE::T* VPKFile::NewEntry(CacheEntry::Index ci, FV::List* names, FP::List *fps,
  VestaVal::T* value, Model::T model, CacheEntry::Indices* kids,
  /*OUT*/ FP::Tag* &commonFP)
  throw (DuplicateNames, VPKFile::TooManyNames)
/* REQUIRES Sup(LL) = SELF.mu */
{
    assert(names->len == fps->len);
    bool newNames = false;
    unsigned int old_allNames_len = this->allNames.len;

    // the set of names for this entry corresponding to "names"
    BitVector *namesBV = NEW_CONSTR(BitVector,
				    (/*sizeHint=*/ this->allNames.len));

    // (partial) map of index in "allNames" to index in "names" (and hence,
    // in "fps"); the domain of the map is the set of indices of the set
    // bits in "namesBV". Invariant: "imap.Get(i,j) => allNames[i]==names[j]".
    IntIntTblLR *imap = (IntIntTblLR *)NULL;

    try
      {
	/* For each name in "names", we look up the name in "this->nameMap" to see
	   if the name is new for this "VPKFile" or not. If it is not new, we
	   get the index of the name from the table. If it is new, we append the
	   name to "allNames" and add it to "this->nameMap". In either case, we
	   set the appropriate bit in "namesBV", and record the mapping from bit
	   vector index to "names/fps" index in "imap". */
	for (long i = 0; i < names->len; i++) {
	  int index;		// index of "names->name[i]" in "allNames"
	  FV::T *name = &(names->name[i]);
	  if (!(this->nameMap.Get(*name, /*OUT*/ index))) {
	    // name not in "allNames"
	    index = this->allNames.Append(*name);
	    newNames = true;
	    bool inMap = this->nameMap.Put(*name, index); assert(!inMap);
	  }
	  assert(index >= 0);
	  if (namesBV->Set(index)) throw DuplicateNames();
	  try
	    {
	      if ((imap == (IntIntTblLR *)NULL) && (index != i)) {
		// no longer the identity map
		imap = NEW_CONSTR(IntIntTblLR, (names->len));
		for (long k = 0; k < i; k++) {
		  bool inMap = imap->Put(k, k); assert(!inMap);
		}
	      }
	      if (imap != (IntIntTblLR *)NULL) {
		bool inMap = imap->Put(index, i); assert(!inMap);
	      }
	      else
		{
		  // Even if we have no imap (maybe this is the first
		  // entry being added to this PKFile), make sure we
		  // don't exceed the imap key limit when adding
		  // names.
		  (void) IntIntTblLR::CheckKey(index);
		  (void) IntIntTblLR::CheckValue(i);
		}
	    }
	  catch(IntIntTblLR::InvalidKey e)
	    {
	      // We've exceed the imap limit by having too many free
	      // variables in this PK.  We must have been adding some
	      // new ones.
	      assert(newNames);
	      // Gather the names we added which put us over the
	      // limit.
	      FV::List *addedNames =
		NEW_CONSTR(FV::List, (this->allNames.len - old_allNames_len));
	      for(unsigned int n = 0;
		  n < (this->allNames.len - old_allNames_len);
		  n++)
		{
		  addedNames->name[n] =
		    this->allNames.name[old_allNames_len + n];
		}
	      // Convert this to a different exception type
	      throw VPKFile::TooManyNames(addedNames);
	    }
	  catch(IntIntTblLR::InvalidValue e)
	    {
	      // This should never happen.  Any entry we put in imap
	      // will have a key of the index in allNames and a value
	      // of the index in names.  By definition the value
	      // cannot be higher than the key (as we must add each
	      // name to allNames first).  IntIntTblLR::Put checks for
	      // an invalid key first, so we can only get
	      // IntIntTblLR::InvalidKey as an exception.
	      cio().start_err() << Debug::Timestamp()
		   << "INTERNAL ERROR: "
		   << "IntIntTblLR::InvalidValue caugt in VPKFile::NewEntry "
		   << " (which should be impossible)" << endl
		   << "  value = " << e.val << endl;
	      cio().end_err(/*aborting*/true);
	      // This is a fatal error.  Abort (which should also dump core).
	      abort();
	    }
	}
      }
    catch(...)
      {
	// If something goes wrong adding names (either DuplicateNames
	// or TooManyNames), remove the new names
	if(newNames)
	  {
	    // First delete them from this->nameMap
	    for(unsigned int i = old_allNames_len; i < this->allNames.len; i++)
	      {
		int unused;
		bool inMap = this->nameMap.Delete(this->allNames.name[i], unused); assert(inMap);
	      }
	    // Then shrink this->allNames
	    this->allNames.len = old_allNames_len;
	  }
	throw;
      }

    // increment the epoch if any new names were added
    if (newNames) {
	this->namesEpoch++;
    }

    // delete arg(s) not incorporated into entry
    delete names;

    // initialize "uncommonNames" and "commonFP"
    BitVector *uncommonNames = NEW_CONSTR(BitVector, (namesBV));
    commonFP = (FP::Tag *)NULL;
    if (this->IsCommon(*uncommonNames)) {
	*uncommonNames -= this->commonNames;
	commonFP = NEW_PTRFREE_CONSTR(Combine::FPTag, (*fps, this->commonNames, imap));
    }

    // create and return the new cache entry
    return NEW_CONSTR(CE::T,
		      (ci, uncommonNames, fps, imap, value, kids, model));
} // NewEntry

void VPKFile::AddEntry(const Text* sourceFunc, CE::T* ce, FP::Tag* commonFP,
  PKFile::Epoch newPKEpoch) throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    // increase "this->pkEpoch" if a larger "newPKEpoch" was given
    if (newPKEpoch != -1) {
	assert(newPKEpoch >= this->pkEpoch);
	this->pkEpoch = newPKEpoch;
    }

    // update this->sourceFunc if necessary
    if (this->sourceFunc == (Text *)NULL) {
        this->sourceFunc = sourceFunc;
    }

    // add entry to proper "new" bin
    if (commonFP == (FP::Tag*)NULL) {
      this->newUncommon = NEW_CONSTR(CE::List, (ce, this->newUncommon));
    } else {
	this->AddEntryToTable(/*INOUT*/ this->newCommon, *commonFP, ce);
    }

    // This VPKFile is now non-empty
    this->isEmpty = false;
}

static CE::List* VPKFile_ChkptEntryList(CE::List *entries) throw ()
/* Return a copy of the list "entries". */
{
    CE::List *res = (CE::List *)NULL;
    for (CE::List *curr = entries; curr != NULL; curr = curr->Tail()) {
      CE::T *newCE = NEW_CONSTR(CE::T, (curr->Head()));
      res = NEW_CONSTR(CE::List, (newCE, res));
    }
    return res;
}

static void VPKFile_ChkptEntryTbl(const SPKFile::CFPEntryMap &inTbl,
  /*OUT*/ SPKFile::CFPEntryMap &outTbl) throw ()
{
    FP::Tag cfp;
    CE::List *oldEntries;
    SPKFile::CFPEntryIter it(&inTbl);
    while (it.Next(/*OUT*/ cfp, /*OUT*/ oldEntries)) {
	CE::List *newEntries = VPKFile_ChkptEntryList(oldEntries);
	bool inTbl = outTbl.Put(cfp, newEntries, /*resize=*/ false);
	assert(!inTbl);
    }
    outTbl.Resize();
}

VPKFileChkPt *VPKFile::CheckPoint() throw ()
/* REQUIRES Sup(LL) < SELF.mu */
{
    this->mu.lock();
    // copy SPKFile fields
    VPKFileChkPt *res = SPKFile::CheckPoint();

    // copy remaining VPKFile fields
    res->newUncommon = VPKFile_ChkptEntryList(this->newUncommon);
    if (this->newCommon.Size() > 0) {
	VPKFile_ChkptEntryTbl(this->newCommon, /*OUT*/ res->newCommon);
    }
    res->newUncommonHead = this->newUncommon;
    res->newCommonHeads = this->newCommon; // tbl assignment; copies ptrs only
    res->hasNewEntries = this->HasNewEntries();

    // increment the PK epoch so entries created after this
    // checkpoint has been taken will have a new epoch
    this->pkEpoch++;
    this->mu.unlock();
    return res;
}

// Update Methods -------------------------------------------------------------

void VPKFile::Update(const SPKFile &pf,
  const VPKFileChkPt &chkpt, const BitVector *toDelete,
  const BitVector *exCommonNames, const BitVector *exUncommonNames,
  BitVector *packMask, IntIntTblLR *reMap, bool becameEmpty,
  /*INOUT*/ EntryState &state) throw ()
/* REQUIRES sup(LL) = SELF.mu */
/* This method updates the "VPKFile" to put it in sync with the
   corresponding "SPKFile" "pf". The arguments "exCommonNames",
   "exUncommonNames", "packMask", "reMap", and "becameEmpty" reflect
   how the VPKFile should be changed.

   The method must consider three kinds of entries in the VPKFile:

     1) New entries that have arrived since the VPKFile was checkpointed.
        These entries were not written to the SPKFile.
     2) New entries that existed at the time the VPKFile was checkpointed,
        and that have since been written to the SPKFile. Hence, the updated
        versions of these entries are stored in "pf.oldEntries".
     3) Old entries that were already written to the SPKFile before the
        VPKFile was ever checkpointed. Updated versions of these entries
	are also stored in "pf.oldEntries".

   The entries of type (1) need not be updated; they will remain new entries.
   It is a matter of policy as to how the entries of type (2) and (3) should
   be handled. For each catagory, the question is whether or not the entries
   should be saved in memory in the table "this->oldEntries". The two Boolean
   configuration values "KeepNewOnFlush" and "KeepOldOnFlush" determine which
   entries are kept in memory.

   In more detail, here is what the method does:

   First, those new entries in this "VPKFile" that are also in the "chkpt" are
   deleted from "this->newCommon" and "this->newUncommon".

   Second, the "packMask" and "reMap" values are updated to account for the
   fact that some new names may have been added to the VPKFile since it was
   checkpointed.

   Third, "this->oldEntries" is set according to the policy determined by the
   configuration variables "KeepNewOnFlush" and "KeepOldOnFlush". This is
   done by forming a list of entries to keep (by CI) according to the
   policy, erasing the current "this->oldEntries" table, and then copying
   the kept entries from "pf.oldEntries" to "this->oldEntries". Hence, these
   entries are not explictly updated here. Instead, the entries are simply
   copied from "pf" (the SPKFile), where they were already updated.

   Fourth, if the set of common names have changed or any entries are being
   deleted, any new entries of type (1) must be processed. We do not expect
   there to be many new entries, so the current implementation simply moves
   any "newCommon" entries to the "newUncommon" list.

   When the new entries on the "newUncommon" list are processed, there is one
   other potential complication. These entries may contain names that have
   been deleted from the SPKFile's "allNames" list because entries containing
   those names have been deleted. If any entries in the "newUncommon" list
   contain such deleted names, the names are re-appended to this VPKFile's
   "allNames" list and the "packMask" and "reMap" values are updated
   accordingly. See "VPKFile::UpdateUncommonList" and
   "VPKFile::CycleDeletedNamesInList" for details.

   Fifth, if the stable PKFile became empty by having all its entries
   deleted ("becameEmpty" is true), and we have no new entries added
   since the checkpoint was taken, then we return to the "fresh" state
   (by setting "isEmpty" to true). */
{
    /** STEP 1 **/
    // remove checkpointed entries from "newCommon" and "newUncommon"
    if (chkpt.hasNewEntries) {
	DeleteCheckpointedEntries(chkpt, /*INOUT*/ state);
    }

    /** STEP 2 **/
    /* Between the time that the VPKFile was checkpointed and now, some new
       entries may have been added. In the process, some new names may have
       been added to "allNames". The "packMask" and "reMap" arguments need
       to be augmented to include these new names. */
    if (packMask != (BitVector *)NULL) {
       	// the new names are those with indices in the half-open interval
        // "[chkpt.allNamesLen, this->allNames.len)"
       	if (this->allNames.len > chkpt.allNamesLen) {
	    // add new names to "packMask"
	    int lastBx = packMask->MSB();  // first, record index of old MSB
	    packMask->SetInterval(chkpt.allNamesLen, this->allNames.len-1);

	    // add corresponding entries to "reMap"
	    assert(reMap != (IntIntTblLR *)NULL);
	    IntIntTblLR::int_type nextBx = 0;
	    if(reMap->Get(lastBx, /*OUT*/ nextBx))
	      nextBx++;
	    for (int i = chkpt.allNamesLen; i < this->allNames.len; i++) {
	      try
		{
		  bool inTbl = reMap->Put(i, nextBx++); assert(!inTbl);
		}
	      catch(IntIntTblLR::InvalidKey e)
		{
		  cio().start_err() << Debug::Timestamp()
		       << "INTERNAL ERROR: "
		       << "IntIntTblLR::InvalidKey caugt in VPKFile::Update" << endl
		       << "  value = " << e.key << endl;
		  cio().end_err(/*aborting*/true);
		  abort();
		}
	      catch(IntIntTblLR::InvalidValue e)
		{
		  cio().start_err() << Debug::Timestamp()
		       << "INTERNAL ERROR: "
		       << "IntIntTblLR::InvalidValue caugt in VPKFile::Update" << endl
		       << "  value = " << e.val << endl;
		  cio().end_err(/*aborting*/true);
		  abort();
		}
	    }
	}
    }

    /** STEP 3 **/
    // determine which entries to keep in "this->oldEntries"
    /* We have not yet updated any of the entries in "this->oldEntries",
       but we have updated all of the entries in "pf.OldEntries()". Hence,
       we record the CIs of any entries we want to store in
       "this->oldEntries" (in "keepCIs"), erase "this->oldEntries", and
       then copy those entries in "pf.OldEntries()" whose CIs are in
       "keepCIs" to "this->oldEntries". Note that any entries deleted
       according to "toDelete" will not be stored in "pf.OldEntries", so
       they will not be saved in "this->oldEntries". */
    IntIntTbl keepCIs(/*sizeHint=*/ 0, /*useGC=*/ true);

    // Save new entries (type (2)) if the policy so dictates
    if (Config_KeepNewOnFlush && chkpt.hasNewEntries) {
	RecordCIsFromList(chkpt.newUncommon, /*INOUT*/ keepCIs);
	RecordCIsFromTbl(chkpt.newCommon, /*INOUT*/ keepCIs);
    }

    // Save existing ``warm'' entries (type (3)) if the policy so dictates
    if (Config_KeepOldOnFlush) {
	RecordCIsFromTbl(this->oldEntries, /*INOUT*/ keepCIs);
    }

    /* copy entries from "pf.OldEntries()" with CIs in "keepCIs"
       to "this->oldEntries" */
    int sizeHint = max(this->oldEntries.Size(), keepCIs.Size());
    this->DeleteOldEntries(sizeHint, /*INOUT*/ state);
    if (keepCIs.Size() > 0) {
	this->CopyByCIs(pf.OldEntries(), /*INOUT*/ keepCIs, /*INOUT*/ state);
    }

    /** STEP 4 **/
    // recompute if common names have changed or there are entries to delete
    bool commonNamesChanged = ((exCommonNames != (BitVector *)NULL)
      || (exUncommonNames != (BitVector *)NULL));
    if (commonNamesChanged || (toDelete != (BitVector *)NULL)) {

	/* At this point, any entries in "newCommon" and "newUncommon" are new
	   entries that were added to this VPKFile since graphlog was flushed
	   at the start of the weed. Hence, even if "toDelete" is non-NULL,
	   the graphlog cannot name any of the entries in "newCommon" or
	   "newUncommon".

           However, if the set of common names as been augmented at all (i.e.,
	   if "exUncommonNames" is non-NULL), the entries in the "newCommon"
	   table may no longer possess all the common names. If the set of
	   common names has decreased, it may also be the case that some
	   entries in "newUncommon" may *have* all common names, and so may be
	   shifted to the "newCommon" table. However, we do not expect many
	   new entries to have been created since the checkpoint, so for
	   now we just move all such entries to the "newUncommon" list.
	   
           Furthermore, if "packMask" is non-NULL, the set of "allNames" has
	   changed, so any new entries need to be packed according to
	   "packMask" and "reMap". */
	if (this->newCommon.Size() > 0) {
	    // move the "newCommon" entries -> "newUncommon"
	    /* NOTE: This must be done before "commonNames" has changed
	       according to "exCommonNames" and "exUncommonNames"! */
	    MoveCommonToUncommon(this->commonNames);
	}

	if (commonNamesChanged) {
	    // set new value for common names
	    if (exCommonNames != (BitVector *)NULL)
		this->commonNames ^= *exCommonNames;
	    if (exUncommonNames != (BitVector *)NULL)
		this->commonNames ^= *exUncommonNames;
	}
	if (packMask != (BitVector *)NULL) {
	    // check that no names in "this->commonNames" are being deleted
	    BitVector temp(&(this->commonNames)); temp -= *packMask;
	    assert(temp.IsEmpty());
	}

	/* Even if the common names have not changed, we still have to
           process the new entries because their names and bit vectors
           may need packing. */

	// go over the new entries now on the "newUncommon" list
	if (this->newUncommon != (CE::List *)NULL) {
	    UpdateUncommonList(chkpt.sourceFunc, /*INOUT*/ packMask,
              /*INOUT*/ reMap, /*unlazyTag=*/ false);
	}

	// pack the common names (this is a no-op if "packMask == NULL")
	this->commonNames.Pack(packMask);
	assert(this->commonNames == pf.CommonNames());

	// pack "this->allNames" if necessary
	if (packMask != (BitVector *)NULL) {
	    this->allNames.Pack(*packMask);
	    this->namesEpoch++;

	    // recompute "nameMap"
	    this->nameMap.Init(/*sizeHint=*/ this->allNames.len);
	    for (int i = 0; i < this->allNames.len; i++) {
		bool inTbl = nameMap.Put(this->allNames.name[i], i);
		assert(!inTbl);
	    }
	}
    }

    /** STEP 5 **/
    // If all entries were deleted from the stable PKFile and we have
    // no new entries, this is now an empty VPKFile.
    if(becameEmpty && !this->HasNewEntries())
      {
	// We are now an empty VPKFile.
	this->isEmpty = true;

	// We must have been deleting.
	assert(toDelete != (BitVector *)NULL);

	// We shouldn't have kept any old entries.
	assert(this->oldEntries.Size() == 0);
      }

} // Update

static void VPKFile_TruncateList(/*INOUT*/ CE::List* &head,
  const CE::List *tail, /*INOUT*/ EntryState &state) throw ()
/* REQUIRES ListLock(head) IN LL */
/* REQUIRES (tail != NULL) => (head != NULL) */
/* If "tail" is NULL, do nothing. Otherwise, this requires that "head" is
   non-NULL and that "tail" points into the list starting at "head". The list
   is truncated to the longest list that no longer includes "*tail" (and any
   elements reachable from it. */
{
    if (tail != (CE::List *)NULL) {
	// find element pointing to "tail"
	assert(head != (CE::List *)NULL);
	CE::List *curr, *prev = (CE::List *)NULL;
	for (curr = head; curr != tail; curr = curr->Tail()) {
	    prev = curr;
	}

	// set previous element to NULL
	if (prev == (CE::List *)NULL) {
	    assert(head == tail);
	    head = (CE::List *)NULL;
	} else {
	    prev->SetTail((CE::List *)NULL);
	}

	// update "state"
	for (; curr != (CE::List *)NULL; curr = curr->Tail()) {
	    state.newEntryCnt--;
	    state.newPklSize -= curr->Head()->Value()->len;
	}
    }
}

void VPKFile::DeleteCheckpointedEntries(const VPKFileChkPt &chkpt,
  /*INOUT*/ EntryState &state) throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    // delete all entries in "chkpt.newUncommonHead".
    VPKFile_TruncateList(this->newUncommon,
      chkpt.newUncommonHead, /*INOUT*/ state);

    // delete all entries pointed to from entries in "chkpt.newCommonHeads"
    if (chkpt.newCommonHeads.Size() > 0) {
	/* Truncating the entries in "this->newCommon" may leave some of
           them set to the empty list. We delete those lists from the
           table. */
	CFPEntryIter iter(&(chkpt.newCommonHeads));
	FP::Tag key; CE::List *tail;
	while (iter.Next(/*OUT*/ key, /*OUT*/ tail)) {
	    CE::List *head;
	    bool inTbl = this->newCommon.Get(key, /*OUT*/ head);
	    assert(inTbl && head != (CE::List *)NULL);
	    VPKFile_TruncateList(/*INOUT*/ head, tail, /*INOUT*/ state);
	    if (head == (CE::List *)NULL) {
		this->newCommon.Delete(key, /*OUT*/ tail, /*resize=*/ false);
		assert(inTbl);
	    }
	}
	this->newCommon.Resize();
    }
}

void VPKFile::DeleteOldEntries(int sizeHint, /*INOUT*/ EntryState &state)
  throw ()
/* REQUIRES sup(LL) = SELF.mu */
{
    // update "state" to account for all entries in "oldEntries"
    CFPEntryIter iter(&(this->oldEntries));
    FP::Tag key; CE::List *curr;
    while (iter.Next(/*OUT*/ key, /*OUT*/ curr)) {
	for (; curr != (CE::List *)NULL; curr = curr->Tail()) {
	    state.oldEntryCnt--;
	    state.oldPklSize -= curr->Head()->Value()->len;
	}
    }

    // reset the "oldEntries" table
    this->oldEntries.Init(sizeHint);
}

void VPKFile::MoveCommonToUncommon(const BitVector &oldCommon) throw ()
/* REQUIRES this->newCommon.Size() > 0 */
{
    // move each list in the table
    assert(this->newCommon.Size() > 0);
    SPKFile::CFPEntryIter it(&(this->newCommon));
    FP::Tag cfp; CE::List *head;
    while (it.Next(/*OUT*/ cfp, /*OUT*/ head)) {
	MoveCommonListToUncommon(head, oldCommon);
    }

    // clear the table
    newCommon.Init(/*sizeHint =*/ newCommon.Size());
}

void VPKFile::MoveCommonListToUncommon(CE::List *head,
  const BitVector &oldCommon) throw () 
/* REQUIRES SELF.mu, ListLock(*head) IN LL */
/* REQUIRES head != (CE::List *)NULL */
{
    assert(head != (CE::List *)NULL);
    CE::List *curr, *prev;
    for (curr = head; curr != (CE::List *)NULL;
         prev = curr, curr = curr->Tail()) {
	CE::T *ce = curr->Head();
	ce->XorUncommonNames(oldCommon);
    }
    prev->SetTail(this->newUncommon);
    this->newUncommon = head;
}

void VPKFile::UpdateUncommonList(const Text* sourceFunc,
  /*INOUT*/ BitVector *packMask, /*INOUT*/ IntIntTblLR *reMap,
  bool unlazyTag) throw ()
/* REQUIRES sup(LL) = SELF.mu */
{
    CE::List *curr = this->newUncommon;
    this->newUncommon = (CE::List *)NULL;
    
    if (packMask != (BitVector *)NULL) {
	assert(reMap != (IntIntTblLR *)NULL);
	CycleDeletedNamesInList(curr, *packMask, *reMap);
    }
    
    for (/*SKIP*/; curr != (CE::List *)NULL; curr = curr->Tail()) {
	CE::T *ce = curr->Head();
	
	// make the entry common if necessary
	FP::Tag *commonFP = (FP::Tag *)NULL;
	if (this->IsCommon(ce->UncommonNames())) {
	    ce->XorUncommonNames(commonNames);
	    commonFP = CommonFP(*ce);
	}
	
	// add the entry to the cache
	/* Note: this must be done before the entry is packed, since this
	    uses the unpacked version of "commonNames" to compute the max
		common timestamp for the entry in the event that the entry is
		    common (i.e., "commonFP != NULL"). */
	AddEntry(sourceFunc, ce, commonFP);
	
	// pack the entry's names, bit vectors
	ce->Pack(packMask, reMap);
	
	// unlazy the cache entry's uncommon tag (if necessary)
	if (unlazyTag) ce->UnlazyUncommonFP();
    }
} // UpdateUncommonList

void VPKFile::RecordCIsFromList(const CE::List *ents,
  /*INOUT*/ IntIntTbl& keep) const throw ()
/* REQUIRES ListLock(*ents) IN LL */
{
    for (const CE::List *curr = ents; curr != NULL; curr = curr->Tail()) {
	IntKey key(curr->Head()->CI());
	bool inTbl = keep.Put(IntKey(key), 0); assert(!inTbl);
    }
}

void VPKFile::RecordCIsFromTbl(const CFPEntryMap& tbl,
  /*INOUT*/ IntIntTbl& keep) const throw ()
{
    if (tbl.Size() > 0) {
	CFPEntryIter it(&tbl);
	FP::Tag cfp; CE::List *head;
	while (it.Next(/*OUT*/ cfp, /*OUT*/ head)) {
	    RecordCIsFromList(head, keep);
	}
    }
}

void VPKFile::CopyByCIs(const CFPEntryMap& source,
  /*INOUT*/ IntIntTbl& cis, /*INOUT*/ EntryState & state) throw ()
/* REQUIRES SELF.mu IN LL */
{
    if (source.Size() > 0 && cis.Size() > 0) {
	CFPEntryIter it(&source);
	FP::Tag cfp; CE::List *head, *curr;
	while (it.Next(/*OUT*/ cfp, /*OUT*/ head)) {
	    for (curr = head; curr != NULL; curr = curr->Tail()) {
		CE::T *ce = curr->Head();
		int cnt;
		if (cis.Get(ce->CI(), /*OUT*/ cnt)) {
		    // make sure same CI is not selected twice
		    assert(cnt == 0);
		    (void) cis.Put(ce->CI(), cnt+1);

		    // add this entry to "this->oldEntries"
		    this->AddEntryToTable(this->oldEntries, cfp, ce);

		    // update "state"
		    state.oldEntryCnt++;
		    state.oldPklSize += ce->Value()->len;
		}
	    }
	}
    }
}

void VPKFile::CycleDeletedNamesInList(CE::List *curr,
  /*INOUT*/ BitVector &packMask, /*INOUT*/ IntIntTblLR &reMap) throw ()
/* REQUIRES sup(LL) = SELF.mu AND ListLock(*curr) IN LL */
{
    // compute the index of where any new names get packed to
    int lastBx = packMask.MSB();
    IntIntTblLR::int_type nextBx = 0;
    if(reMap.Get(lastBx, /*OUT*/ nextBx))
      nextBx++;

    // keep track of new indices of deleted names
    // this maps old indices of deleted names to their new indices
    IntIntTblLR delMap(reMap.ArraySize());

    for (/*SKIP*/; curr != (CE::List *)NULL; curr = curr->Tail()) {
	CE::T *ce = curr->Head();
	BitVector delBV(ce->UncommonNames());
	delBV -= packMask;
	if (!(delBV.IsEmpty())) {
	    // update "VPKFile" values, "packMask", "reMap"
	    BVIter it(&delBV); unsigned int oldBx; IntIntTblLR::int_type newBx;
	    while (it.Next(/*OUT*/ oldBx)) {
		if (!delMap.Get(oldBx, /*OUT*/ newBx)) {
		  try {
		    // add the name to "this->allNames" (if necessary)
		    FV::T *name = &(this->allNames.name[oldBx]); // alias
		    newBx = this->allNames.Append(*name);
		    bool inTbl = delMap.Put(oldBx, newBx); assert(!inTbl);

		    // update "this->nameMap"
		    inTbl = this->nameMap.Put(*name, newBx); assert(inTbl);

		    // update "packMask"
		    bool set = packMask.Set(newBx); assert(!set);

		    // update "reMap"
		    inTbl = reMap.Put(newBx, nextBx++);
		    assert(!inTbl);
		  }
		  catch(IntIntTblLR::InvalidKey e)
		    {
		      cio().start_err() << Debug::Timestamp()
			   << "INTERNAL ERROR: "
			   << "IntIntTblLR::InvalidKey caugt in VPKFile::CycleDeletedNamesInList" << endl
			   << "  value = " << e.key << endl;
		      cio().end_err(/*aborting*/true);
		      abort();
		    }
		  catch(IntIntTblLR::InvalidValue e)
		    {
		      cio().start_err() << Debug::Timestamp()
			   << "INTERNAL ERROR: "
			   << "IntIntTblLR::InvalidValue caugt in VPKFile::CycleDeletedNamesInList" << endl
			   << "  value = " << e.val << endl;
		      cio().end_err(/*aborting*/true);
		      abort();
		    }
		}
	    }

	    // update the entry "ce"
	    ce->CycleNames(delBV, delMap);
	}
    }
}

bool VPKFile::ReadyForPurgeWarm(int latestEpoch) const throw()
  /* REQUIRES Sup(LL) = SELF.mu */
{
  // We're ready to have our warm entries purged from memory iff:

  // - Enough time has passed since we were last used (configurable by
  // PurgeWarmPeriodCnt)

  // - We have no new entries

  // - We have one or more old (warm) entries
  return ((this->freePKFileEpoch <= (latestEpoch -
				     Config_PurgeWarmPeriodCnt)) &&
	  !this->HasNewEntries() &&
	  (this->OldEntries()->Size() > 0));
}

bool VPKFile::ReadyForEviction(int latestEpoch) const throw()
  /* REQUIRES Sup(LL) = SELF.mu */
{
  // We're ready to be evicted from the cache iff:

  // - Enough time has passed since we were last used (configurable by
  // EvictPeriodCnt)

  // - We have no new entries

  // - We have no old (warm) entries
  return ((this->freePKFileEpoch <= (latestEpoch -
				     Config_EvictPeriodCnt)) &&
	  !this->HasNewEntries() &&
	  (this->OldEntries()->Size() == 0));
}
