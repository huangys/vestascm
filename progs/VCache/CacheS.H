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

// CacheS -- the server interface to the Vesta-2 cache server

/* The class "CacheS" provides the callback methods for a running instance of
   a Vesta-2 cache server. A new "CacheS" object is passed to the constructor
   of an "ExpCache" object. The "ExpCache" object listens for cache server
   SRPC connections. When a call is made by the client, the "ExpCache" object
   unmarshals the arguments and invokes the corresponding method of the
   "CacheS" object. */

#ifndef _CACHES_H
#define _CACHES_H

#include <time.h>
#include <Basics.H>
#include <FS.H>
#include <VestaLog.H>
#include <FP.H>

// cache-common
#include <CacheIntf.H>
#include <BitVector.H>
#include <CacheState.H>
#include <Model.H>
#include <PKEpoch.H>
#include <CacheIndex.H>
#include <Derived.H>
#include <FV.H>
#include <VestaVal.H>
#include <GraphLog.H>
#include <PKPrefix.H>

// cache-server
#include <EmptyPKLog.H>
#include <Leases.H>
#include <CacheEntry.H>
#include <CacheLog.H>
#include <Intvl.H>
#include <VPKFile.H>
#include <VMultiPKFile.H>
#include "FlushQueue.H"

// forward declarations
class FlushWorker;
class CleanWorker;
class ChkptWorker;

class CacheS {
  public:
    CacheS(CacheIntf::DebugLevel debug = CacheIntf::None,
      bool noHits = false) throw ();
    /* REQUIRES LL = Empty */
    /* Initialize a new cache server object. Debugging information is printed
       to the standard output for all debugging messages having level at most
       "debug". If "noHits" is true, then the "Lookup" method will never
       return "Hit" as a result. */

    int Version() throw () { return CacheIntf::Version; }
    /* REQUIRES LL = Any */
    /* Returns the version number of this cache server interface. */

    CacheIntf::DebugLevel DebugLevel() const throw () { return this->debug; }
    /* REQUIRES LL = Any */
    /* Return the current debug level. */

    void FreeVariables(const FP::Tag& pk, /*OUT*/ VPKFile* &vf) throw ();
    /* REQUIRES Sup(LL) < SELF.mu) */
    /* Set "vf" to the VPKFile for "pk". */

    CacheIntf::LookupRes
    Lookup(const FP::Tag &pk, FV::Epoch id, const FP::List &fps,
      /*OUT*/ CacheEntry::Index &ci, /*OUT*/ const VestaVal::T* &value)
      throw ();
    /* REQUIRES (FORALL vpk: VPKFile :: Sup(LL) < vpk.mu) */
    /* Look up the cache entry with primary key "pk", free variables set with
       epoch "id", and corresponding value fingerprints "fps".
  
       In the event of a hit, "Hit" is returned, a lease is taken out (or
       renewed) on the matching cache entry, "ci" is set to the index of that
       entry, and "value" is set to its result value. For all other return
       values, "ci" and "value" are unchanged. 

       In the event of a cache miss, "Miss" is returned. In the event that "id"
       is an old epoch for "pk", "FVMismatch" is returned. In this case, the
       caller should call the "freeVariables" method again to get the latest
       epoch and names associated with "pk", and then try "Lookup" again. */

    CacheIntf::AddEntryRes
    AddEntry(FP::Tag *pk, FV::List *names, FP::List *fps,
      VestaVal::T *value, Model::T model, CacheEntry::Indices *kids,
      const Text& sourceFunc, /*OUT*/ CacheEntry::Index& ci)
      throw (VPKFile::TooManyNames);
    /* REQUIRES (FORALL vpk: VPKFile :: Sup(LL) < vpk.mu) */
    /* Add a new entry to the cache under the primary key "pk" corresponding
       to the evaluation of some function; "names" is the list of names that
       are free during the evaluation of the function, and "fps" are the
       fingerprints of the values associated with those names. If the lengths
       of these two lists do not agree, or if "names" contains any duplicate
       names, "CacheIntf::BadAddEntryArgs" is immediately returned; this
       return result indicates a programming error on the part of the client.

       "value" is the value produced by the evaluation of the function,
       "model" is the model in which the function is defined, and "kids" are
       the indices of the cache entries corresponding to function evaluations
       performed directly on behalf of this evaluation. Finally, "sourceFunc"
       is a text denoting the source location of the function definition
       for the new cache entry.

       In the event of success, "CacheIntf::EntryAdded" is returned, a lease
       is taken out on the new cache entry, and "ci" is set to the index of
       the new cache entry. 

       If any of the "kids" lacks a lease, then "CacheIntf::NoLease" is
       returned, and the value of "ci" is unchanged.

       If the maximum number of names per PK is exceeded,
       VPKFile::TooManyNames is thrown. */

    void Checkpoint(const FP::Tag &pkgVersion, Model::T model,
      CacheEntry::Indices *cis, bool done) throw ();
    /* REQUIRES Sup(LL) < SELF.chkptMu */
    /* Make stable all cache entries and deriveds reachable from the entries
       corresponding to "cis". If the cache entries corresponding to "cis"
       all have leases, then also write a "Root" entry to the graph log that
       protects them from weeding. The arguments "pkgVersion" and "model"
       identify the top-level model on which the evaluator was invoked:
       "pkgVersion" is the fingerprint of the immutable directory in which
       the model resides, and "model" is the ShortId of the model file
       itself. The "done" argument should be true if the checkpoint is for
       a completed model evaluation, false if it represents an intermediate
       checkpoint. The checkpoint is done synchronously iff "done" is true. */

    void FlushAll() throw ();
    /* REQUIRES
     	 Sup(LL) < SELF.ciLogMu AND
     	 (FORALL vpk: VPKFile :: Sup(LL) < vpk.mu) */
    /* Flush all VMultiPKFiles in the cache, even if they don't have any new
       entries to be flushed. Before returning, this method also forks a
       thread to clean the cache log. */

    void GetCacheId(/*OUT*/ CacheId &id) throw ();
    /* REQUIRES Sup(LL) < SELF.mu */
    /* Set "id" to the identifying information of the cache server. */

    void GetCacheState(/*OUT*/ CacheState &state) throw ();
    /* REQUIRES Sup(LL) < SELF.mu */
    /* Set "state" to the current state information of the cache server. */

    const FP::Tag &GetCacheInstance() const throw ();
    /* Returns the unique instance fingerprint of the cache server.

       Must only be called after the instance is initialized, as read
       access to the instance fingerprint is not protected by a mutex. */

    bool RenewLeases(CacheEntry::Indices *cis) throw ();
    /* REQUIRES Sup(LL) < SELF.mu */
    /* Renew the leases on the cache entries with indexes "cis". Returns true
       if all the cache entries named by "cis" exist and all currently have
       valid leases; false otherwise. Even if false is returned, the leases of
       all existing cache entries with valid leases named in "cis" will have
       had their leases renewed. */

    bool WeederRecovering(SRPC *srpc, bool doneMarking) throw ();
    /* REQUIRES Sup(LL) < SELF.mu */
    /* Indicate that the weeder is in the process of recovering, and set the
       cache server's state accordingly. "doneMarking" should be true iff the
       weeder's "weeded" set is non-empty, i.e., if the weeder had completed
       its mark phase but not the subsequent deletion phase. "srpc" should be
       a pointer to the SRPC connection making this call; the connection is
       used on subsequent calls to detect if a weed is currently in progress.
       Returns true iff there is already a weeder running, in which case no
       action is taken and the weeder should immediately abort. */

    BitVector* StartMark(/*OUT*/ int &newLogVer) throw ();
    /* REQUIRES Sup(LL) < SELF.graphLogMu */
    /* Indicate that the weeder is starting its mark phase. This blocks if the
       cache server is still in the middle of doing deletions from a previous
       weed. Then it disables lease expiration, and flushes the graphLog.
       It then checkpoints the graph log, and sets "newLogVer" to the version
       number of the new graphLog file. The weeder should read all versions up
       to (but not including) "newLogVer". Finally, this method returns the
       bit vector of cache indices in use at the time of the call. */

    void SetHitFilter(const BitVector &cis) throw ();
    /* REQUIRES Sup(LL) < SELF.mu */
    /* Set the cache server's hit filter set to "cis". The hit filter is the
       set of cache entries that may soon be deleted, so hits on them by the
       cache server's "Lookup" method will be disabled. This method requires
       that the cache server is not currently doing deletions from a previous
       weed. */

    BitVector* GetLeases() throw ();
    /* REQUIRES Sup(LL) < SELF.mu */
    /* Return a newly allocated bit vector corresponding to the cache entries
       that are currently leased. */

    void ResumeLeaseExp() throw ();
    /* REQUIRES Sup(LL) < SELF.mu */
    /* Re-enable lease expiration disabled by the "StartMark" method above. */

    int EndMark(const BitVector &cis, const PKPrefix::List &pfxs) throw ();
    /* REQUIRES Sup(LL) < SELF.mu */
    /* Indicate that the weeder's mark phase has completed, and that "cis" are
       the indices of the cache entries to delete. The list "pfxs" contains
       the union of the prefixes of the PK's of all these entries.

       The cache server's hit filter is set to "cis", and the cache server
       will fork a background thread to delete these  entries. The number of
       the checkpoint file into which the weeder should write the ``clean''
       version of the graphLog is returned. */

    bool CommitChkpt(const Text &chkptFileName) throw ();
    /* REQUIRES Sup(LL) < SELF.graphLogMu */
    /* Commit the currently outstanding graphLog checkpoint. This is a no-op
       if there is no outstanding checkpoint.

       The file to which the graphLog checkpoint was written, which is
       the "real" checkpoint name plus a suffix to make it unique, is
       passed as an argument.  This file is renamed to take the place
       of the actual checkpoint.  These extra steps guard against a
       case where two weeders are both trying to checkpoint the graph
       log of the same cache server.

       If for any reason the cache server decides not to accept the
       checkpoint (if, for example, it doesn't think anyone should be
       doing a checkpoint right now), it will return false.
       Otherwise, it will return true. */

  private:
    // dictionary types
    typedef Table<FP::Tag,VPKFile*>::Default CacheMap;
    typedef Table<FP::Tag,VPKFile*>::Iterator CacheMapIter;
    typedef Table<PKPrefix::T,VMultiPKFile*>::Default MPKMap;
    typedef Table<PKPrefix::T,VMultiPKFile*>::Iterator MPKIter;
    typedef Table<FP::Tag,FV::Epoch>::Default NamesEpochTbl;
    typedef Table<FP::Tag,FV::Epoch>::Iterator NamesEpochIter;

    // Data fields ----------------------------------------------------------

    // read-only after initialization
    CacheIntf::DebugLevel debug;
    bool noHits;
    
    // shared
    Basics::mutex mu;		     // protects the following fields:
    Leases *leases;		     // cache entry leases
    CacheMap *cache;		     // cache: PK -> VPKFile
    MPKMap *mpkTbl;		     // mpkTbl: PKPrefix::T -> VMultiPKFile
    BitVector usedCIs;		     // set of used cache indices
    EmptyPKLog *emptyPKLog;	     // disk log of deleted PKFiles (see below)
    bool deleting;		     // deleting cache entries? (STABLE)
    SRPC *weederSRPC;                // cached connection to latest weeder
    Basics::cond doDeleting;	     // "deleting"
    Basics::cond notDeleting;	     // "!deleting"
    BitVector hitFilter;	     // set of weeded cache indices (STABLE)
    PKPrefix::List mpksToWeed;       // which MPKFiles need weeding (STABLE)
    int nextMPKToWeed;               // index into mpksToWeed (STABLE)
    int graphLogChkptVer;            // version of outstanding graphLog chkpt
    VestaLog *weededMPKsLog;	     // log of weeded VMultiPKfiles
    FlushQueue *cacheLogFlushQ;      // queue for flushing cache log
    CacheLog::Entry *vCacheLog;	     // volatile cache log
    CacheLog::Entry *vCacheLogTail;  // tail of volatile cache log
    CacheLog::Entry *vCacheAvail;    // available vCacheLog entries
    FlushQueue *graphLogFlushQ;      // queue for flushing graph log
    GraphLog::Node *vGraphLog;       // volatile graph log
    GraphLog::Node *vGraphLogTail;   // tail of volatile graph log
    GraphLog::Node *vGraphNodeAvail; // available vGraphLog "Node" entries
    int vGraphNodeAvailLen;          // length of vGraphNodeAvail (debugging)
    FlushQueue *ciLogFlushQ;         // queue for flushing cache log
    Intvl::List *vCILog, *vCIAvail;  // log of usedCI's, available objects
    NamesEpochTbl evictedNamesEpochs;// The namesEpochs of evicted
				     // empty PKFiles.

    /* Note: the field "emptyPKLog" is actually read-only after initialization,
       but the fields of the object it points to are protected by "mu". */

    // field for bkgrnd VMultiPKFile deletion thread; also protected by "mu"
    int freeMPKFileEpoch;

    // fields for supporting "GetCacheState" method; also protected by "mu"
    time_t startTime;                // time at which cache came up
    unsigned int entryCnt;           // = usedCIs.Cardinality()
    MethodCnts cnt;                  // # of calls on various cache methods
    EntryState state;                // state of entries in memory

    // Cache server instance identifier, used to prevent an evaluator
    // from continuing across cache server restarts; also protected by
    // "mu"
    FP::Tag instanceFp;

    // fields for flushing MultiPKFiles; also protected by "mu"
    struct FlushWorkerList {
	FlushWorker *worker;
	FlushWorkerList *next;
    };
    FlushWorkerList *idleFlushWorkers; // linked list of worker threads
    Basics::cond availFlushWorker;     // == (idleFlushWorkers != NULL)
    int numActiveFlushWorkers;         // number of allocated flushed workers
    Basics::cond allFlushWorkersDone;  // == (numActiveFlushWorkers == 0)

    // fields for queueing up checkpoint requests; protected by "mu"
    Basics::thread chkptTh;            // thread object for thread
    Basics::mutex chkptMu;             // protects following fields
    ChkptWorker *availChkptWorkers;    // linked list of worker objects
    ChkptWorker *queuedChkptWorkers;   // linked list of objects w/ work to do
    Basics::cond waitingChkptWorker;   // == (queuedChkptWorkers != NULL)

    /* The "cache" is a map from PK's to VPKFile objects. The pair
       "(pk, vpkfile)" is in "cache" iff "vpkfile" is the VPKFile for the
       primary key "pk".

       The "mpkTbl" is a map from PK prefixes (i.e., names of MultiPKFiles)
       to "VMultiPKFile" objects. The pair "(pfx, vmpkfile)" appears in this
       table iff "vmpkfile" is the VMultiPKFile corresponding to prefix "pfx"
       and if the "VToSCache" method should be called to flush that
       MultiPKFile to the stable cache. The "vmpkfile" contains pointers to
       all of the VPKFile's stored in "cache".

       In general, the cache maintains the following invariant:
       
       					   pfx = PKPrefix::T(pk)
       	 (pk, vpkfile) IN cache  <==>   /\ (pfx, vmpkfile) IN mpkTbl
       				        /\ (pk, vpkfile) IN vmpkfile
    */

    // stable logs
    Basics::mutex cacheLogMu;	     // protects writing/reading "cacheLog"
    int cacheLogLen;                 // current length of stable cache log (*)
    CleanWorker *idleCleanWorker;    // available worker thread
    Basics::cond availCleanWorker;   // == (idleCleanWorker != NULL)
    VestaLog *cacheLog;		     // disk log of new entries
    Basics::mutex graphLogMu;	     // protects writing/reading "graphLog"
    VestaLog *graphLog;		     // disk log of new graph nodes
    Basics::mutex ciLogMu;	     // protects writing/reading "ciLogMu"
    VestaLog *ciLog;		     // disk log of usedCI's

    /* (*) Actually, "cacheLogLen" records the number of entries added to the
           stable cache log *since it was last flushed*. */

    /* Locking levels:
|        ciLogMu < graphLogMu < cacheLogMu < mu
    */

    // Stable variables -------------------------------------------------------

    void RecoverDeleting() throw (FS::Failure);
    /* REQUIRES LL = Empty */
    /* Initialize "deleting" from stable storage. */

    void SetStableDeleting(bool del) throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Set the "deleting" value to "del", and write its value persistently. */

    void RecoverHitFilter() throw (FS::Failure);
    /* REQUIRES LL = Empty */
    /* Initialize "hitFilter" from stable storage. */

    void WriteHitFilter() throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Write the current value of "hitFilter" to stable storage. */

    void ClearStableHitFilter() throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Set the "hitFilter" value to the empty set, and write its value
       persistently. */
    
    void SetStableHitFilter(const BitVector &hf) throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Set the "hitFilter" value to "hf", and write its value persistently. */

    void SetMPKsToWeed(const PKPrefix::List &pfxs) throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Set "mpksToWeed" and write its value persistently. */

    void RecoverMPKsToWeed() throw (FS::Failure);
    /* REQUIRES LL = Empty */
    /* Initialize "mpksToWeed" from stable storage. */

    void ResetWeededMPKs() throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Set "nextMPKToWeed" to 0, and reset the persistent log of the
       MultiPKFiles that have been weeded to the empty set. */

    void AddToWeededMPKs(int num) throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Atomically record that "num" more MultiPKFiles in the "mpksToWeed"
       list have been flushed via "VToSCache". */

    void RecoverWeededMPKs() throw (VestaLog::Eof, VestaLog::Error);
    /* REQUIRES LL = Empty */
    /* Initialize "nextMPKToWeed" from stable storage. */

    // (Multi)PKFiles ---------------------------------------------------------

    bool FindVPKFile(const FP::Tag &pk, /*OUT*/ VPKFile* &vpk) throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* If there is a "VPKFile" in the cache under the primary key
       "pk", set "vpk" to point to it and return true.  Otherwise, try
       creating a new "VPKFile" (with no "entries") from the stable
       cache and return false. If there is no "SPKFile" for "pk",
       create a new empty "VPKFile". */

    PKFile::Epoch PKFileEpoch(const FP::Tag &pk) throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Return the "pkEpoch" of the PKFile for "pk" in the volatile
       cache. If no PKFile exists for "pk", the result will be 0. */

    bool GetVPKFile(const FP::Tag &pk, /*OUT*/ VPKFile* &vpk) throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Like "FindVPKFile" above, but also adds the new "VPKFile" to
       the cache and sets "vpk" to point to it. Returns "true" iff a "VPKFile"
       already existed in the cache under "pk". This method is analogous to
       the "GetVF" function in "SVCache.spec". */

    void VToSCache(const PKPrefix::T &pfx, const BitVector *toDelete = NULL)
      throw ();
    /* REQUIRES 
         Sup(LL) < SELF.ciLogMu AND
         (FORALL vpk: VPKFile :: Sup(LL) < vpk.mu) */
    /* If there are any new entries for the MultiPKFile with prefix "pfx",
       flush them to the stable cache. If "toDelete" is non-NULL, then any
       cache entries contained in the same MultiPKFile as "pfx" whose
       indices correspond to set bits in "toDelete" are deleted from the
       cache. */

    void AddEntryToMPKFile(const PKPrefix::T &pfx, const char *reason,
			   bool inRecovery = false)
      throw ();
    /* REQUIRES Sup(LL) < SELF.mu */
    /* Called to indicate that a new entry has been added to a VPKFile
       in the MultiPKFile with prefix "pfx". If heuristics indicate
       that the MultiPKFile needs flushing, the "FlushMPKFile" below
       is called with "reason" and with "block = false".  If
       "inRecovery" is true, the MultiPKFile will not be flushed
       immediately.  This avoids incrementing pkEpchs while still
       replaying the cache log. */

    void FlushMPKFile(const PKPrefix::T &pfx,
      const char *reason, bool block=false) throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Fork a background thread to flush the MultiPKFile with prefix "pfx".
       The "reason" argument is for debugging purposes; it should indicate
       why the MultiPKFile is being flushed. If "block" is true, then this
       method will block until a FlushWorker thread becomes available if
       none currently are. */

    friend void* CacheS_DoFreeMPKFiles(void *arg) throw ();
    /* REQUIRES Sup(LL) < ((CacheS *)arg)->mu */
    /* A background thread that attempts to free memory used by
       MultiPKFiles that have not been accessed recently. The "arg"
       is actually of type "CacheS *". */

    FlushWorker *NewFlushWorker(bool block = false) throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Return a new "FlushWorker" object that is ready to run to flush a
       MultiPKFile to disk. If "block" is "true", block until a previously-
       created flush worker is available to do the work. Once this method
       returns, you call the worker's "Start" method to start it running
       on a specified MPKFile. */

    void RegisterIdleFlushWorker(FlushWorker *fw) throw ();
    /* REQUIRES Sup(LL) < SELF.mu */
    /* Return "fw" to the list of idle flush worker threads. This
       should be called by the worker when it has completed its job. */

    friend void* CacheS_FlushWorker(void *arg) throw ();
    /* REQUIRES Sup(LL) < SELF.mu */
    /* The apply function for threads forked to flush a MultiPKFile.
       "arg" is actually of type "(FlushWorker *)". */

    // Checkpoint -----------------------------------------------------------

    void DoCheckpoint(const FP::Tag &pkgVersion, Model::T model,
      CacheEntry::Indices *cis, bool done) throw ();
    /* REQUIRES Sup(LL) < SELF.ciLogMu */
    /* This method does the actual work of the "Checkpoint" method. */

    ChkptWorker *QueueChkpt(const FP::Tag &pkgVersion, Model::T model,
      CacheEntry::Indices *cis, bool done) throw ();
    /* REQUIRES Sup(LL) < SELF.chkptMu */
    /* Add a new checkpoint object (whose fields are initialized
       to "pkgVersion", "model", "cis", and "done") to the queue of
       checkpoints to perform. */ 

    void FinishChkpt(ChkptWorker *cw) throw ();
    /* REQUIRES Sup(LL) < SELF.chkptMu */
    /* Reset the checkpoint object "cw" and return it to the list of
       available checkpoint objects. */

    friend class ChkptWorker;

    // Recovery -------------------------------------------------------------

    void Recover() throw (VestaLog::Error, VestaLog::Eof, FS::Failure);
    /* REQUIRES LL = Empty */
    /* Read logs to restore in-memory cache. */

    friend void* CacheS_DoDeletions(void *cacheS) throw ();
    /* REQUIRES LL = 0 */
    /* The body of the background thread for deleting cache entries. "cacheS"
       will actually be of type "CacheS*". */

    // Methods related to "cacheLog" ----------------------------------------

    void LogCacheEntry(const Text& sourceFunc, FP::Tag *pk,
		       PKFile::Epoch pkEpoch,
		       CacheEntry::Index ci, VestaVal::T *value,
		       Model::T model, CacheEntry::Indices *kids,
		       FV::List *names, FP::List *fps) throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Append the tuple "(sourceFunc, pk, pkEpoch, ci, value, model,
       kids, names, fps)" to the "vCacheLog". */

    void FlushCacheLog() throw (VestaLog::Error);
    /* REQUIRES Sup(LL) < SELF.ciLogMu */
    /* Flush the "vCacheLog" to disk ("cacheLog"). This flushes the "vCILog"
       and "vGraphLog" first. At most one thread can be flushing the cache
       log at a time, so the caller will be blocked if another thread is
       currently flushing the log. */

    void CleanCacheLog() throw (VestaLog::Error, VestaLog::Eof, FS::Failure);
    /* REQUIRES Sup(LL) < SELF.cacheLogMu */
    /* Remove unnecessary entries from the cache log. The only entries that
       are preserved are those that are new entries in the volatile cache
       that have not been flushed to stable PKFiles. This method writes the
       preserved entries to the cache log checkpoint, and it writes an empty
       checkpoint for the emptyPKLog. */

    CleanWorker *NewCleanWorker() throw ();
    /* REQUIRES Sup(LL) = SELF.cacheLogMu */
    /* Block until the background thread for cleaning the cache log is
       available; then return it. */

    void RegisterIdleCleanWorker(CleanWorker *cw) throw ();
    /* REQUIRES Sup(LL) < SELF.cacheLogMu */
    /* Return "cw" to the list of idle clean worker threads. This
       should be called by the workern when it has completed its job. */

    friend void* CacheS_CleanCacheLogWorker(void *arg) throw ();
    /* REQUIRES Empty(LL) */
    /* This is the apply function for a thread that is forked to clean
       the CacheLog. */

    void CleanCacheLogEntries(RecoveryReader &rd, std::fstream &ofs,
      /*INOUT*/ EmptyPKLog::PKEpochTbl &pkEpochTbl,
      /*INOUT*/ int &oldCnt, /*INOUT*/ int &newCnt)
      throw (VestaLog::Error, VestaLog::Eof, FS::Failure);
    /* REQUIRES Sup(LL) < SELF.cacheLogMu */

    void TryCleanCacheLog(int upper_bound, const char *reason) throw ();
    /* REQUIRES Sup(LL) = SELF.cacheLogMu */
    /* If the number of entries written to the stable cache log since it was
       last cleaned exceeds "upper_bound", fork a thread to ``clean'' it.
       The "reason" is a string used for debugging to indicate why the log is
       being cleaned. */

    void RecoverEmptyPKLog(/*INOUT*/ bool &empty)
      throw (VestaLog::Error, VestaLog::Eof);
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Read "this->emptyPKLog", adding epochs to its associated table.
       Sets "empty" to "false" if any entries are processed. */

    void RecoverCacheLogEntries(RecoveryReader &rd,
      /*INOUT*/ EmptyPKLog::PKEpochTbl &pkEpochTbl, /*INOUT*/ bool &empty)
      throw (VestaLog::Error, VestaLog::Eof);
    /* REQUIRES Sup(LL) = SELF.cacheLogMu */
    /* Read cache log entries from "rd" until end of file. "pkEpochTbl" is a
       table mapping PKs to their known (from disk) "pkEpoch"s. It is
       augmented to include any newly-discovered associations. If the
       debugging level is high enough, "empty" is set to "false" if any
       entries are read. */

    void RecoverCacheLog() throw (VestaLog::Error, VestaLog::Eof, FS::Failure);
    /* REQUIRES LL = Empty */

    // Methods related to "graphLog" ----------------------------------------

    void LogGraphNode(CacheEntry::Index ci, FP::Tag *loc, Model::T model,
      CacheEntry::Indices *kids, Derived::Indices *refs) throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Append the quintuple "(ci, loc, model, kids, refs)" to the
       "vGraphLog". */

    void FlushGraphLog() throw (VestaLog::Error);
    /* REQUIRES Sup(LL) < SELF.ciLogMu */
    /* Flush the "vGraphLog" to disk ("graphLog"). This flushes the "vCILog"
       first. At most one thread can be flushing the graph log at a time, so
       the caller will be blocked if the log is currently being flushed by
       another thread. */

    int ChkptGraphLog() throw (FS::Failure, VestaLog::Error);
    /* REQUIRES Sup(LL) < SELF.graphLogMu */
    /* Checkpoint the graphLog and return the number of the new checkpoint
       and log file. */

    void AbortGraphLogChkpt() throw (VestaLog::Error);
    /* REQUIRES Sup(LL) < SELF.graphLogMu */
    /* Abort the currently outstanding graphLog checkpoint. */

    void RecoverGraphLog() throw (VestaLog::Error, VestaLog::Eof, FS::Failure);
    /* REQUIRES LL = Empty */

    // Methods related to the "ciLog" ---------------------------------------

    void LogCI(Intvl::Op op, CacheEntry::Index ci) throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Append "ci" to the "vCILog"; "op" indicates if the index should be
	added or removed from the "usedCIs" set. */

    void FlushUsedCIs() throw (VestaLog::Error);
    /* REQUIRES Sup(LL) < SELF.ciLogMu */
    /* Flush the "vCILog" to disk ("ciLog"). */

    Intvl::List *FlushUsedCIsList(Intvl::List *vLog, /*OUT*/ int &numFlushed)
      throw (VestaLog::Error);
    /* REQUIRES SELF.ciLogMu IN LL */
    /* Flush the entries in "vLog" to the "usedCIs" log on disk, and return
       a pointer to the last node in the list. Requires "vLog != NULL". Sets
       "numFlushed" to the number of intervals flushed to the log. */

    void ChkptUsedCIs(const BitVector &del)
      throw (VestaLog::Error, FS::Failure);
    /* REQUIRES Sup(LL) < SELF.ciLogMu */
    /* Atomically flush all volatile "usedCIs" entries to the "usedCIs" disk
       log, subtract the entries in "del" from "this->usedCIs", and checkpoint
       the "usedCIs" disk log so it contains the new value of "usedCIs". */

    void RecoverCILog() throw (VestaLog::Error, VestaLog::Eof, FS::Failure);
    /* REQUIRES LL = Empty */
    /* Set "usedCIs" from the "ciLog" on disk. */

    // make copy constructor inaccessible to clients
    CacheS(const CacheS&);
};

// Worker thread classes ------------------------------------------------------

// base class for various kinds of worker threads
class CacheWorker {
  public:
    CacheWorker(CacheS *cs) throw ();
    /* Initialize a new worker object ready to run on "cs".
       Call the associated "Start" method to actually run it
       on particular arguments. */

    void Start(const char *reason) throw ();
    /* Signal the thread to go. Subclasses may have their own Start
       methods to allow passing arguments to the thread.
       The "reason" argument is for debugging; it indicates
       the task on behalf of which the worker thread is acting. */

    void Finish() throw ();
    /* Indicate that the worker thread is idle. */
  protected:
    // the thread and associated cache server
    Basics::thread th;
    CacheS *cs;

    // synchronization fields
    Basics::mutex mu;
    bool argsReady;
    Basics::cond workToDo;
    const char *reason;    // for debugging info only
};

// worker thread for flush a specified MultiPKFile to the stable cache
class FlushWorker : public CacheWorker {
  public:
    FlushWorker(CacheS *cs) throw ();

    void Start(const PKPrefix::T &pfx, const char *reason) throw ();
    /* Start the worker thread flushing the MultiPKFile
       named by "pfx". The "reason" argument indicates why
       this MultiPKFile is being flushed. */

  private:
    friend void* CacheS_FlushWorker(void *arg) throw ();
    /* REQUIRES LL = 0 */
    /* Callback function for the cache server worker thread
       that flushes the MultiPKFile named by "arg->pfx".
       "arg" will actually be of type "FlushWorker *". */ 

    // thread args
    PKPrefix::T pfx;
};

// worker thread for cleaning the cache log
class CleanWorker : public CacheWorker {
  public:
    CleanWorker(CacheS *cs) throw ();

  private:
    friend void* CacheS_CleanCacheLogWorker(void *arg) throw ();
    /* REQUIRES LL = 0 */
    /* Callback function for the cache server worker thread
       that cleans the cache log. "arg" will actually be of
       type "CleanWorker *". */ 
};

// class for checkpointing an evaluation
class ChkptWorker {
  /* As opposed to the other "*Worker" objects, there is not a separate
     thread per "ChkptWorker" object. Instead, the cache forks a single
     thread running the "ChkptWorker::MainLoop" method below. Each
     "ChkptWorker" object is simply a container of arguments on which
     the "CacheS::DoCheckpoint" method is invoked. */
  public:
    // constructors/initializers
    ChkptWorker(const FP::Tag &pkgVersion, Model::T model,
      CacheEntry::Indices *cis, bool done) throw ()
	  { Init(pkgVersion, model, cis, done); }
    void Init(const FP::Tag &pkgVersion, Model::T model,
      CacheEntry::Indices *cis, bool done) throw ();

    // reset object fields after using it
    void Reset() throw ();

    // block until this checkpoint completes
    void WaitUntilDone() throw ();
    /* REQUIRES Sup(LL) < SELF.mu */

    static void* MainLoop(void *arg) throw ();
    /* REQUIRES Sup(LL) < CacheS::ciLogMu */
    /* Repeatedly invoke "DoCheckpoint" whenever there is another worker
       in the "CacheS::queuedChkptWorkers" queue. "arg" is actually of
       type "CacheS". */

    // next object in linked list
    ChkptWorker *next;

  private:
    // arguments
    FP::Tag pkgVersion;
    Model::T model;
    CacheEntry::Indices *cis;
    bool done;

    // synchronization fields
    Basics::mutex mu;           // protects following fields
    bool chkptComplete; // did the checkpoint finish?
    Basics::cond isDone;        // == (chkptComplete)
};

#endif // _CACHES_H
