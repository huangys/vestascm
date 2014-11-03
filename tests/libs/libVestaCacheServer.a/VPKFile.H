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


// VPKFile -- A set of cache entries sharing the same primary key (PK). This
// is the volatile version. The cache itself is a map: PK -> VPKFile.
//
// The methods in the "VPKFile" class are monitored.

#ifndef _VPK_FILE_H
#define _VPK_FILE_H

#include <Basics.H>
#include <FS.H>
#include <SRPC.H>
#include <FP.H>
#include <CacheIntf.H>
#include <BitVector.H>
#include <CacheState.H>
#include <Model.H>
#include <PKEpoch.H>
#include <CacheIndex.H>
#include <FV.H>
#include <VestaVal.H>
#include <Generics.H>

#include "IntIntTblLR.H"
#include "CacheEntry.H"
#include "VPKFileChkPt.H"
#include "SPKFile.H"
#include "SMultiPKFileRep.H"

/* A "VPKFile" is a volatile PKFile, an in-memory structure for
   storing cache entries with the same primary key (PK). It provides
   methods for creating a new "VPKFile", for accessing the names of
   all its free variables (the "AllNames" method), for looking up a
   cache entry (the "Lookup" method), and for creating and adding new
   entries (the "NewEntry", and "AddEntry" methods).

   There are separate methods for creating a new cache entry and for adding it
   to the "VPKFile". This is because the spec requires the cache server's
   "AddEntry" method to create the cache entry, add the entry to the cache's
   "cacheLog", and then to add it to the "VPKFile".

   Some methods of a "VPKFile" are monitored, and some are not. See the
   REQUIRES clause of each method for details. */

// exception class signalling duplicate names in "VPKFile::NewEntry"
class DuplicateNames { };

class VPKFile : public SPKFile {
  public:

  // Exception type for exceeding the maximum number of names allowed
  // per PK
  struct TooManyNames
  {
    // Names which when added pushed us over the limit
    FV::List *newNames;
    TooManyNames(FV::List *names) : newNames(names) { }
  };

    // Mutex for protecting private data fields; this lock is public so that
    // it can be acquired by a CacheS object when new entries are added.
    Basics::mutex mu;
    /* Locking order:
|        (forall vf: VPKFile, cache: CacheS ::
|           vf.mu < cache.mu)
    */

    VPKFile(const FP::Tag& pk, PKFile::Epoch newPKEpoch = 0, FV::Epoch newNamesEpoch = 0)
      throw (FS::Failure);
    /* REQUIRES Sup(LL) < SELF.mu */
    /* Create a new, empty PK file for primary key "pk". First, this
       tries reading the "SPKFile" for "fp" from the stable cache. If
       there is one, it initializes this "VPKFile" from that (but with
       an empty "oldEntries" table). Otherwise, it simply initializes
       this "VPKFile" to a new, empty "VPKFile" for "fp". In that
       event, it initializes the "pkEpoch" and "namesEpoch" of the new
       "VPKFile" to "newPKEpoch" and "newNamesEpoch". This method is
       analogous to the "EmptyVF" function in "SVCache.spec". */

    bool IsEmpty() throw ();
    /* REQUIRES Sup(LL) < SELF.mu */
    /* Return "true" iff this "VPKFile" contains no entries. */

    bool IsStableEmpty() const throw ()
      { return this->isEmpty; }
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Return "true" iff this "VPKFile" contains no entries *and*
       there are no entries in its corresponding stable PKFile. */

    bool HasNewEntries() const throw ()
      { return (newUncommon != (CE::List *)NULL) || (newCommon.Size() > 0); }
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Returns true iff this VPKFile has any new entries pending in its
       "newUncommon" or "newCommon" fields. */

    void SendAllNames(SRPC *srpc, bool debug, bool old_protocol)
      throw (SRPC::failure, PrefixTbl::Overflow);
    /* REQUIRES Sup(LL) < SELF.mu */
    /* This method implements the marshalling of the FreeVariables
       results.  If "debug" is true, it prints the "FreeVariables"
       return debugging message. If "old_protocol" is true, the old
       network protocol for sending free variables is used (limited to
       2^16 free variables total and 2^16-1 entries in the PrefixTbl).
       It also calls the "Etp_CacheS_FreeVariables" function with the
       correct arguments for performance monitoring. Finally, it
       marshals the "isEmpty" value, this VPKFile's free variable
       names, and its "namesEpoch". This work is done here (with the
       lock "SELF.mu" held) to avoid having to unnecessarily copy the
       free variable names. */

    CacheIntf::LookupRes Lookup(FV::Epoch id, const FP::List &fps,
      /*OUT*/ CacheEntry::Index &ci, /*OUT*/ const VestaVal::T* &value,
      /*OUT*/ CacheIntf::LookupOutcome &outcome) throw (FS::Failure);
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Lookup an entry in the cache. First, "id" is compared to the epoch for
       all free variables for this VPKFile. If it is out of date,
       "CacheIntf::FVMismatch" is returned. Otherwise, an attempt is made to
       lookup the entry whose free variables have fingerprints "fps". If no
       matching entry is found, "CacheIntf::Miss" is returned and "outcome" is
       set to "LookupOutcome::AllMisses". In the event of a hit,
       "CacheIntf::Hit" is returned, "ci" and "value" are set to the index and
       result value of the corresponding cache entry, and "outcome" is set to
       the fine-grain outcome used for statistical purposes. Otherwise,
       "outcome" is set to the special value "LookupOutcome::NoOutcome".

       The search is first performed in the volatile cache, and then in the
       stable cache. If there is an error reading the stable cache on disk,
       "FS::Failure" is thrown. */

    CE::T* NewEntry(CacheEntry::Index ci, FV::List* names, FP::List *fps,
      VestaVal::T* value, Model::T model, CacheEntry::Indices* kids,
      /*OUT*/ FP::Tag* &commonFP)
      throw (DuplicateNames, TooManyNames);
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Create and return a new cache entry for this "VPKFile"
       according to the arguments (see "CacheS::AddEntry"). The
       "names" are the names of the free variables for the new entry,
       and the "fps" are their corresponding fingerprints. If "names"
       contains any names not in the name sequence for this "VPKFile",
       they are added to the sequence and the "VPKFile"'s epoch is
       incremented. If it contains duplicates, "DuplicateNames" is
       thrown.  If the adding names exceeds the maximum number of
       allowed names, TooManyNames is thrown.  (If either exception is
       thrown, any modifications to the name sequence is undone.)

       The "commonFP" OUT parameter is set to NULL if this entry does not
       contain all of the VPKFile's common names, or to a pointer to their
       combined fingerprint if it does.

       The responsibility for freeing storage for the arguments transfers
       from the caller to the callee. */ 

    void AddEntry(const Text* sourceFunc, CE::T *ce, FP::Tag* commonFP,
      PKFile::Epoch newPKEpoch = -1) throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Add the new cache entry "ce" to this "VPKFile". The argument
       "sourceFunc" names the source location of the function definition
       for this new entry. The entry "ce" should have been created by a
       previous call to the "NewEntry" method for this "VPKFile". If
       "commonFP" is NULL, then this entry is assumed not to have all the
       common names for its VPKFile; otherwise, "commonFP" should be the
       combined fingerprint of the fingerprints of the values of those
       common names.

       If "newPKEpoch" is not defaulted, then "this->pkEpoch" is set
       to "newPKEpoch". It is a checked run-time error for
       "newPKEpoch < this->pkEpoch". */

    FP::Tag *CommonFP(const CE::T& entry) throw ()
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Return the combined fingerprint of the fingerprints in "entry" for this
       VPKFile's common names. It is an unchecked runtime error for "entry"
       not to contain all the common names. */
        { return entry.CombineFP(commonNames); }

    VPKFileChkPt *CheckPoint() throw ();
    /* REQUIRES Sup(LL) < SELF.mu */
    /* Return a newly-allocated checkpoint of this "VPKFile". The
       "pkEpoch" of this VPKFile is atomically incremented after the
       checkpoint is taken. */

    void DecrPKEpoch() throw () { this->pkEpoch--; }
    /* REQUIRES Sup(LL) = SELF.mu */

    void Update(const SPKFile &pf,
      const VPKFileChkPt &chkpt, const BitVector *toDelete,
      const BitVector *exCommonNames, const BitVector *exUncommonNames,
      BitVector *packMask, IntIntTblLR *reMap, bool becameEmpty,
      /*INOUT*/ EntryState &state) throw ();
    /* REQUIRES sup(LL) = SELF.mu */
    /* Update this VPKFile to be based on the common free variables of
       "pf", but not to include the entries in "chkpt". "toDelete" are
       the entries to delete (and may be NIL), "exCommonNames",
       "exUncommonNames", "packMask", "reMap", and becameEmpty are the
       values set by the "SPKFile::Update" method for this PKFile. */

    void UpdateFreeEpoch(int currentEpoch) throw ()
    /* REQUIRES Sup(LL) = SELF.mu */
      {	this->freePKFileEpoch = currentEpoch; }

    int GetFreeEpoch() throw()
    /* REQUIRES Sup(LL) = SELF.mu */
      { return this->freePKFileEpoch; }

    void DeleteOldEntries(int sizeHint, /*INOUT*/ EntryState &state) throw ();
    /* REQUIRES sup(LL) = SELF.mu */
    /* Delete all entries from "this->oldEntries", and update "state" to
       reflect the old entries so deleted from this VPKFile. */

    void Evict() throw()
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Indicate that this VPKFile object has been evicted from the
       cache.  This should only be called by the cache when all
       references to this VPKFile have been removed from the tables in
       the cache itself and its owning VMultiPKFile. */
      { this->evicted = true; }

    bool Evicted() const throw()
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Has this VPKFile been evicted from the cache? */
      { return this->evicted; }

    bool ReadyForPurgeWarm(int latestEpoch) const throw();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Should warm entries be purged from this VPKFile? */

    bool ReadyForEviction(int latestEpoch) const throw();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Should this VPKFile be evicted from the cache? */

  private:
    // dictionary types
    typedef Table<FV::T,int>::Default NameMap;
    typedef Table<FV::T,int>::Iterator NameMapIter;

    // data fields
    NameMap nameMap;		// table: name -> index in "allNames" list
    CE::List *newUncommon;	// new entries not containing all "commonNames"
    CFPEntryMap newCommon;	// new entries containing all "commonNames"

    /* The "nameMap" is a table that maps each name in "allNames" to its index
       in "allNames". Hence, the set of names in the domain of "nameMap" is
       exactly the set of names in "allNames". The map is defined so we have:

         nameMap = (allNames[i], i)   forall 0 <= i < allNames.len

       The "newUncommon" field is a list of the new entries that don't contain
       all of this PKFile's common names. The "newCommon" field is a table
       (indexed by common finger print) of entries that do contain all of this
       PKFile's common names. */

    // Is this a completely empty VPKFile (i.e. has no entries and no
    // stable PKFile)?  True in either of the following two cases:

    // 1. This is a newly created VPKFile that has no on-disk SPKFile
    // and has never had any entries added to it.

    // 2. This PK's stable PKFile has become empty by having all its
    // entries deleted by weeding, and no new entries have been added
    // since.
    bool isEmpty;

    int freePKFileEpoch;        // epoch of activity on this PKFile

    bool evicted;		// Has this VPKFile been evicted from
				// the cache?  (Needed to avoid a race
				// between freeing VPKFiles and adding
				// new entries.)

    // private methods
    void ReadPKFHeaderTail(const FP::Tag &pk)
      throw (FS::Failure, SMultiPKFileRep::NotFound);
    /* Read the <PKFHeaderTail> of the SPKFile for "pk" into this VPKFile.
       "SMultiPKFileRep::NotFound" is thrown if there is no stable MultiPKFile
       for "pk". */

    bool IsCommon(const BitVector &nms) throw ()
      { return (!this->commonNames.IsEmpty() && this->commonNames <= nms); }
    /* Returns true iff an entry with names "nms" should be considered
       "common", i.e., stored in the "newCommon" table as opposed to the
       "newUncommon" list. */

    void PutCommonEntry(CFPEntryMap &tbl, const FP::Tag &commonFP, CE::T *ce)
      throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Add the entry "ce" to the table "tbl" under the common fingerprint
       "commonFP". */

    CE::T* LookupInList(const CE::List *entries, const FP::List &fps) throw ();
    /* REQUIRES Sup(LL) = SELF.mu AND ListLock(*entries) IN LL */
    /* Look for a hit on an entry in the list "entries" where the free
       variables have the given fingerprints "fps". If there is a match,
       return the resulting cache entry; otherwise, return NULL. The "entries"
       are all expected to have the correct combined common fingerprint (if
       any); the test for a hit is based solely on the combined uncommon
       fingerprint. */

    void DeleteCheckpointedEntries(const VPKFileChkPt &chkpt,
      /*INOUT*/ EntryState &state) throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Remove the checkpointed entries in "chkpt" from this VPKFile's
       "newCommon" and "newUncommon" bins. This does not changed the set of
       entries stored in "chkpt". Update the counts in "state" as cache
       entries are removed from this VPKFile. */

    void MoveCommonToUncommon(const BitVector &oldCommon) throw ();
    /* REQUIRES newCommon.Size() > 0 */
    /* Move the entries in the "newCommon" table to the "newUncommon" list,
       OR'ing the "oldCommon" vector into each entry's "uncommonNames". */

    void MoveCommonListToUncommon(CE::List *head, const BitVector &oldCommon)
      throw ();
    /* REQUIRES SELF.mu IN LL */
    /* REQUIRES head != (CE::List *)NULL */
    /* Move the common entries in "head" to the "newUncommon" list, XOR'ing
       the "oldCommon" vector into each entry's "uncommonNames". */

    void UpdateUncommonList(const Text* sourceFunc,
      /*INOUT*/ BitVector *packMask, /*INOUT*/ IntIntTblLR *reMap,
      bool unlazyTag) throw ();
    /* REQUIRES sup(LL) = SELF.mu */
    /* Walk over the entries in "newUncommon", packing each entry according to
       "packMask" and "reMap" (if non-NULL), and unlazying the uncommon
       fingerprint iff "unlazyTag" is true. Each entry is added to either the
       "newCommon" table or the "newUncommon" list based on whether it contains
       all the common names or not.

       Note: The VPKFile's "commonNames" should already have been updated
       before this method is called. However, since the entry's
       "uncommonNames" are compared to this VPKFile's "commonNames" before the
       "uncommonNames" have been packed, the "commonNames" should not have been
       packed before this call. */

    void RecordCIsFromList(const CE::List *ents, /*INOUT*/ IntIntTbl& keep)
      const throw ();
    /* REQUIRES ListLock(*ents) IN LL */
    /* Add the CIs of all entries in "ents" to the table "keep". (Actually,
       the table records pairs of the form "(ci, 0)"). */

    void RecordCIsFromTbl(const CFPEntryMap& tbl, /*INOUT*/ IntIntTbl& keep)
      const throw ();
    /* Add the CIs of all entries in "tbl" to the table "keep". (Actually,
       the table records pairs of the form "(ci, 0)"). */

    void CopyByCIs(const CFPEntryMap& source, /*INOUT*/ IntIntTbl& cis,
      /*INOUT*/ EntryState &state) throw ();
    /* REQUIRES SELF.mu IN LL */
    /* Add any entry in "source" whose CI is in the domain of "cis" to the
       table "this->oldEntries". "cis" is an INOUT parameter because this
       function has benevolent side-effects on it (it increments the range
       value of each CI selected from the table). */

    void CycleDeletedNamesInList(CE::List *curr,
      /*INOUT*/ BitVector &packMask, /*INOUT*/ IntIntTblLR &reMap) throw ();
    /* REQUIRES sup(LL) = SELF.mu */
    /* For each entry in the list "curr" (all of which are assumed to be
       ``uncommon''), see if the entry contains any names not named in
       "packMask". If so, cycle such names to the end of the "this->allNames"
       list, update the "this->nameMap" appropriately, and update "packMask",
       "reMap", and the entries themselves to correspond to the new order of
       the names. */

    // make copy constructor inaccessible to clients
    VPKFile(const VPKFile&);
};

#endif // _VPK_FILE_H
