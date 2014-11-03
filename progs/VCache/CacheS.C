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

// CacheS.C -- the Vesta-2 cache server

#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>

// #include <gc.h>
#include <Basics.H>
#include <FS.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <VestaLogSeq.H>
#include <AtomicFile.H>
// #include <Drd.H>
#include <FP.H>
#include <UniqueId.H>

// cache-common
#include <BitVector.H>
#include <CacheConfig.H>
#include <CacheIntf.H>
#include <Debug.H>
#include <PKPrefix.H>
#include <Timer.H>
#include <Model.H>
#include <PKEpoch.H>
#include <CacheIndex.H>
#include <Derived.H>
#include <FV.H>
#include <VestaVal.H>
#include <GraphLog.H>

// cache-server
#include <CacheConfigServer.H>
#include <Leases.H>
#include <CacheEntry.H>
#include <CacheLog.H>
#include <Intvl.H>
#include <VPKFile.H>
#include <VMultiPKFile.H>

// local
#include "VCacheVersion.H"
#include "PKPrefixSeq.H"
#include "CacheS.H"

using std::ios;
using std::fstream;
using std::ifstream;
using std::cerr;
using std::endl;
using std::ostream;
using OS::cio;

void *CacheS_DoFreeMPKFiles(void *arg) throw ();
void *CacheS_DoDeletions(void *arg) throw ();

CacheS::CacheS(CacheIntf::DebugLevel debug, bool noHits) throw ()
/* REQUIRES LL = Empty */
: debug(debug), noHits(noHits)
{
    try {
	// initialize cache server values
	this->mu.lock();
	try {
	  this->leases =
	    NEW_CONSTR(Leases, (&(this->mu),
				Config_LeaseTimeoutSecs,
				(this->debug >= CacheIntf::LeaseExp)));
	  this->cache = NEW_CONSTR(CacheMap, (5, /*useGC=*/ true));
	  this->mpkTbl = NEW_CONSTR(MPKMap, (5, /*useGC=*/ true));
	  this->emptyPKLog = NEW_CONSTR(EmptyPKLog,
					(&(this->mu), this->debug));
	  this->weederSRPC = (SRPC *)NULL;
	  this->graphLogChkptVer = -1;
	  this->freeMPKFileEpoch = 0;
	  this->entryCnt = 0;
	  this->idleFlushWorkers = (FlushWorkerList *)NULL;
	  this->numActiveFlushWorkers = 0;
	} catch (...) { this->mu.unlock(); throw; }
	this->mu.unlock();

	this->Recover();

	// Record instance fingerprint.
	this->instanceFp = UniqueId();
	// This is probably excessive paranoia, but include the
	// current total number of cache entries in the fingerprint.
	this->instanceFp.Extend((char *) &(this->entryCnt),
				sizeof(this->entryCnt));
	// Debugging
	if (this->debug >= CacheIntf::StatusMsgs)
	  {
	    cio().start_out() << Debug::Timestamp()
			      << "Instance fingerprint: "
			      << this->instanceFp << endl;
	    cio().end_out();
	  }

	// fork "DoFreeMPKFiles" thread
	Basics::thread *th2 = NEW_PTRFREE(Basics::thread);
	th2->fork_and_detach(CacheS_DoFreeMPKFiles, (void *)this);

	// fork "DoDeletions" thread
	Basics::thread *th1 = NEW_PTRFREE(Basics::thread);
	th1->fork_and_detach(CacheS_DoDeletions, (void *)this);

	// allocate single worker thread to clean cache log
	this->cacheLogMu.lock();
	try {
	  this->idleCleanWorker = NEW_CONSTR(CleanWorker, (this));
	  this->cacheLogLen = 0;
	} catch (...) { this->cacheLogMu.unlock(); throw; }
	this->cacheLogMu.unlock();

	// allocate a thread to handle Checkpoint calls
	this->chkptMu.lock();
	this->availChkptWorkers = (ChkptWorker *)NULL;
        this->queuedChkptWorkers = (ChkptWorker *)NULL;
	this->chkptMu.unlock();
	this->chkptTh.fork_and_detach(ChkptWorker::MainLoop, (void *)this);

	// allocate an initial set of FlushWorker threads
	typedef FlushWorker *FlushWorkerPtr;
        FlushWorkerPtr *workers = NEW_ARRAY(FlushWorkerPtr,
					    Config_FlushWorkerCnt);
	this->mu.lock();
	int i;
	for (i = 0; i < Config_FlushWorkerCnt; i++) {
	    workers[i] = this->NewFlushWorker(/*block=*/ false);
	}
	this->mu.unlock();
	for (i = 0; i < Config_FlushWorkerCnt; i++) {
	    RegisterIdleFlushWorker(workers[i]);
	    workers[i] = NULL;
	}
	delete[] workers;

	// record start time
	this->mu.lock();
	this->startTime = time((time_t *)NULL);
	assert(this->startTime >= 0);
	this->mu.unlock();
    }
    catch (const VestaLog::Error &err) {
      cerr << "VestaLog fatal error: ";
      cerr << "failed recovering cache server" << endl;
      cerr << "  num = " << err.r << "; " << err.msg << endl;
      cerr << "Exiting..." << endl;
      exit(1);
    }
    catch (VestaLog::Eof) {
      cerr << "VestaLog fatal error: ";
      cerr << "unexpected EOF recovering cache server; exiting..." << endl;
      exit(1);
    }
    catch (const FS::Failure &f) {
      cerr << "FS fatal error: ";
      cerr << "unexpected FS error recovering cache server; exiting...";
      cerr << endl << f;
      exit(1);
    }
}

/* Evaluator client methods ------------------------------------------------ */

bool CacheS::FindVPKFile(const FP::Tag &pk, /*OUT*/ VPKFile* &vpk) throw ()
/* REQUIRES Sup(LL) = SELF.mu */
/* Note: This operation will lead to a disk access if the VPKFile for "pk" is
   not currently in the cache. */
{
    bool res = this->cache->Get(pk, /*OUT*/ vpk);
    if (!res) {
	// create the VPKFile; this attempts to read the SPKFile on disk
	try {
	    PKFile::Epoch delPKEpoch = 0;
	    (void)(this->emptyPKLog->GetEpoch0(pk, /*OUT*/ delPKEpoch));
	    FV::Epoch namesEpoch = 0;
	    (void)evictedNamesEpochs.Delete(pk, namesEpoch);
	    vpk = NEW_CONSTR(VPKFile, (pk, delPKEpoch, namesEpoch));
	}
	catch (const FS::Failure &f) {
	    // fatal error
	    cerr << f;
	    assert(false);
	}
    }
    return res;
}

PKFile::Epoch CacheS::PKFileEpoch(const FP::Tag &pk) throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    VPKFile *vpk;
    (void)(this->FindVPKFile(pk, /*OUT*/ vpk));
    return vpk->PKEpoch();
}

bool CacheS::GetVPKFile(const FP::Tag &pk, /*OUT*/ VPKFile* &vpk) throw ()
/* REQUIRES Sup(LL) = SELF.mu */
/* Note: This operation will lead to a disk access if the VPKFile for "pk" is
   not currently in the cache. */
{
    bool res = this->FindVPKFile(pk, /*OUT*/ vpk);
    if (!res) {
	// install it in the cache
	bool inCache = cache->Put(pk, vpk); assert(!inCache);

	// add it to the appropriate VMultiPKFile
	PKPrefix::T pfx(pk); VMultiPKFile *mpk;
	if (!(this->mpkTbl->Get(pfx, /*OUT*/ mpk))) {
	  // create the new VMultiPKFile and install it
	  mpk = NEW_CONSTR(VMultiPKFile, (pfx));
	  bool inMPK = this->mpkTbl->Put(pfx, mpk); assert(!inMPK);
	}
	bool inVMPK = mpk->Put(pk, vpk); assert(!inVMPK);
    }

    return res;
}

void CacheS::FreeVariables(const FP::Tag& pk, /*OUT*/ VPKFile* &vf)
  throw ()
/* REQUIRES Sup(LL) < SELF.mu) */
{
    this->mu.lock();
    this->cnt.freeVarsCnt++;
    (void)(this->GetVPKFile(pk, /*OUT*/ vf));
    int currentFreeEpoch = this->freeMPKFileEpoch;
    this->mu.unlock();

    // Note that this PKFile has been accessed.
    vf->mu.lock();

    // Ensure that we don't have an evicted VPKFile (needed to
    // avoid a race with freeing empty VPKFiles).
    while(vf->Evicted())
      {
	// Unlock the evicted one, as we won't be using it.
	vf->mu.unlock();
	// Get another VPKFile object for this PK (probably
	// creating it).
	this->mu.lock();
	(void)(this->GetVPKFile(pk, /*OUT*/ vf));
	this->mu.unlock();
	// Lock our new VPKFile object.
	vf->mu.lock();
      }

    vf->UpdateFreeEpoch(currentFreeEpoch);
    vf->mu.unlock();
}

static void Etp_CacheS_Lookup(int fps_bytes, int outcome, int val_bytes)
  throw ()
/* This procedure is a no-op. It's for etp(1) logging purposes only.
   It reflects the size of the arguments and result returned by the
   CacheS::Lookup method. "fps_bytes" is the size of the fingerprint
   argument (in bytes). "outcome" is the lookup outcome as defined in the
   "CacheIntf" interface. Finally, "val_bytes" is the total size of the
   returned value in bytes. */
{ /* SKIP */ }

CacheIntf::LookupRes
CacheS::Lookup(const FP::Tag &pk, FV::Epoch id, const FP::List &fps,
  /*OUT*/ CacheEntry::Index &ci, /*OUT*/ const VestaVal::T* &value) throw ()
/* REQUIRES (FORALL vpk: VPKFile :: Sup(LL) < vpk.mu) */
{
    CacheIntf::LookupRes res;
    CacheIntf::LookupOutcome outcome;

    // get VPKFile
    VPKFile *vf;
    this->mu.lock();
    this->cnt.lookupCnt++;
    (void)(this->GetVPKFile(pk, /*OUT*/ vf));
    int currentFreeEpoch = this->freeMPKFileEpoch;
    this->mu.unlock();

    vf->mu.lock();

    // Ensure that we don't have an evicted VPKFile (needed to
    // avoid a race with freeing empty VPKFiles).
    while(vf->Evicted())
      {
	// Unlock the evicted one, as we won't be using it.
	vf->mu.unlock();
	// Get another VPKFile object for this PK (probably
	// creating it).
	this->mu.lock();
	(void)(this->GetVPKFile(pk, /*OUT*/ vf));
	this->mu.unlock();
	// Lock our new VPKFile object.
	vf->mu.lock();
      }

    // Note that this PKFile has been accessed.
    vf->UpdateFreeEpoch(currentFreeEpoch);

    // actual work
    try {
      res = vf->Lookup(id, fps,
		       /*OUT*/ ci, /*OUT*/ value, /*OUT*/ outcome);
    }
    catch (const FS::Failure &f) {
      // fatal error
      cerr << f;
      assert(false);
    }

    vf->mu.unlock();

    if (res == CacheIntf::Hit) {
      if (this->noHits) {
	// don't allow a hit if "-noHits" was specified
	res = CacheIntf::Miss;
      } else {
	this->mu.lock();
	try {
	  // do "hitFilter" screening
	  if (this->hitFilter.Read(ci) &&
	      !(this->leases->IsLeased(ci))) {
	    res = CacheIntf::Miss;
	  } else {
	    // otherwise, take out lease on "ci"
	    this->leases->NewLease(ci);
	  }

	  // update stats if a new entry was paged in
	  if (outcome == CacheIntf::DiskHits) {
	    this->state.oldEntryCnt++;
	    this->state.oldPklSize += value->len;
	  }

	  // Make sure that the CI we're returning is a used CI.
	  // Getting a cache hit on an unused CI indicates cache
	  // corruption, possibly a weeder bug.
	  if(!this->usedCIs.Read(ci))
	    {
	      cio().start_err() << Debug::Timestamp()
				<< "INTERNAL ERROR: hit on unused CI" << endl
				<< "    ci = " << ci << endl
				<< "    pk = " << pk << endl
				<< " (Please report this as a bug.)" << endl;
	      cio().end_err(/*aborting*/true);
	      abort();
	    }
	} catch (...) { this->mu.unlock(); throw; }
	this->mu.unlock();
      }
    }
    Etp_CacheS_Lookup(fps.Size(), outcome,
      (res == CacheIntf::Hit) ? value->Size() : sizeof(value));
    return res;
}

static void Etp_CacheS_AddEntry(int arg_bytes) throw ()
/* This procedure is a no-op. It's for etp(1) logging purposes only.
   It reflects the size of the arguments of the CacheS::AddEntry method.
   "arg_bytes" is the total size of the arguments (in bytes). */
{ /* SKIP */ }

CacheIntf::AddEntryRes
CacheS::AddEntry(FP::Tag *pk, FV::List *names, FP::List *fps,
  VestaVal::T *value, Model::T model, CacheEntry::Indices *kids,
  const Text& sourceFunc, /*OUT*/ CacheEntry::Index& ci)
  throw (VPKFile::TooManyNames)
/* REQUIRES (FORALL vpk: VPKFile :: Sup(LL) < vpk.mu) */
{
    GraphLog::Node *dummy_var; // causes GC-related crash not to occur?
    CacheIntf::AddEntryRes res = CacheIntf::BadAddEntryArgs;
    if (names->len != fps->len) {
	res = CacheIntf::BadAddEntryArgs;
    } else {
	int kid; VPKFile *vf;
      	this->mu.lock();
	this->cnt.addEntryCnt++;
	int currentFreeEpoch = this->freeMPKFileEpoch;
	try {
	    // find VPKFile for this PK
	    (void)(this->GetVPKFile(*pk, /*OUT*/ vf));

	    // get next available CI and log it
	    BitVector *except = this->deleting
              ? &(this->hitFilter) : (BitVector *)NULL;
	    ci = this->usedCIs.NextAvailExcept(except);
	    this->entryCnt++;
	    this->LogCI(/*op=*/ Intvl::Add, ci);

	    // take out lease on "ci"
	    this->leases->NewLease(ci);

	    // check for necessary cache entry leases
	    for (kid = 0; kid < kids->len; kid++) {
	      if (!this->leases->IsLeased(kids->index[kid])) break;
	    }

	    // If all child CIs are leased...
	    if (kid == kids->len) {
	      // add node to "vGraphLog"
	      this->LogGraphNode(ci, pk, model, kids, &(value->dis));
	    }
	} catch (...) { this->mu.unlock(); throw; }
	this->mu.unlock();

      	if (kid < kids->len) {
	    // exited loop prematurely
	    res = CacheIntf::NoLease;
      	} else {
	    vf->mu.lock();

	    // Ensure that we don't have an evicted VPKFile (needed to
	    // avoid a race with freeing empty VPKFiles).
	    while(vf->Evicted())
	      {
		// Unlock the evicted one, as we won't be using it.
		vf->mu.unlock();
		// Get another VPKFile object for this PK (probably
		// creating it).
		this->mu.lock();
		(void)(this->GetVPKFile(*pk, /*OUT*/ vf));
		this->mu.unlock();
		// Lock our new VPKFile object.
		vf->mu.lock();
	      }

	    // Note that this PKFile has been accessed.
	    vf->UpdateFreeEpoch(currentFreeEpoch);

	    try {
		// create cache entry
		FP::Tag *commonFP;
		CE::T *entry = vf->NewEntry(ci, names, fps,
					    value, model, kids,
					    /*OUT*/ commonFP);

		// add node to "vCacheLog"
		this->mu.lock();
		try {
		    this->LogCacheEntry(sourceFunc, pk, vf->PKEpoch(),
					ci, value, model, kids,
					names, fps);
		} catch (...) { this->mu.unlock(); throw; }
		this->mu.unlock();

		// add entry to VPKFile
		vf->AddEntry(NEW_CONSTR(Text, (sourceFunc)), entry, commonFP);
		this->mu.lock();
		this->state.newEntryCnt++;
		this->state.newPklSize += entry->Value()->len;
		this->mu.unlock();
		res = CacheIntf::EntryAdded;
	    }
	    catch (DuplicateNames) {
		res = CacheIntf::BadAddEntryArgs;
	    }
	    catch (...) { vf->mu.unlock(); throw; }
	    vf->mu.unlock();

	    // flush the MultiPKFile if necessary
	    PKPrefix::T pfx(*pk);
	    this->AddEntryToMPKFile(pfx,
              "MultiPKFile threshold exceeded on CacheS::AddEntry");
      	}
    }
    Etp_CacheS_AddEntry(sizeof(*pk) + names->CheapSize()
      + fps->Size() + value->Size() + kids->Size());
    return res;
} // CacheS::AddEntry

// Flushing MPKFile methods ---------------------------------------------------

void CacheS::AddEntryToMPKFile(const PKPrefix::T &pfx, const char *reason,
			       bool inRecovery)
  throw ()
/* REQUIRES Sup(LL) < SELF.mu */
{
    VMultiPKFile *mpk;
    this->mu.lock();
    try {
	bool inTbl = this->mpkTbl->Get(pfx, /*OUT*/ mpk); assert(inTbl);
	mpk->IncEntries();
	mpk->UpdateEpoch(this->freeMPKFileEpoch);
	// We will only flush this MPKFile if we're out of recovery.
	// Doing so during recovery would increment the pkEpoch of the
	// PKFiles it contains.  This could cause an inconsistency
	// with the pkEpochs recorded in the cache log we're still
	// recovering.
	if (!inRecovery && mpk->IsFull()) {
	    this->FlushMPKFile(pfx, reason, /*block=*/ false);
	}
    } catch (...) { this->mu.unlock(); throw; }
    this->mu.unlock();
}

void CacheS::FlushMPKFile(const PKPrefix::T &pfx,
  const char *reason, bool block) throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    FlushWorker *worker = this->NewFlushWorker(block);
    worker->Start(pfx, reason);
}

FlushWorker *CacheS::NewFlushWorker(bool block) throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    FlushWorker *res;
    if (!block && this->idleFlushWorkers == (FlushWorkerList *)NULL) {
      // create new FlushWorker object
      res = NEW_CONSTR(FlushWorker, (this));
    } else {
	while (this->idleFlushWorkers == NULL) {
	    assert(block); // we'll only get here if in blocking call
	    this->availFlushWorker.wait(this->mu);
	}
	// get FlushWorker off avail list
	res = this->idleFlushWorkers->worker;
	this->idleFlushWorkers = this->idleFlushWorkers->next;
    }
    this->numActiveFlushWorkers++;
    return res;
}

void CacheS::RegisterIdleFlushWorker(FlushWorker *worker) throw ()
/* REQUIRES Sup(LL) < SELF.mu */
{
  FlushWorkerList *cons = NEW(FlushWorkerList);
  cons->worker = worker;
  this->mu.lock();
  cons->next = this->idleFlushWorkers;
  this->idleFlushWorkers = cons;
  this->numActiveFlushWorkers--;
  if (this->numActiveFlushWorkers == 0) {
    this->allFlushWorkersDone.broadcast();
  }
  this->mu.unlock();
  this->availFlushWorker.signal();
}

void *CacheS_DoFreeMPKFiles(void *arg) throw ()
/* REQUIRES Sup(LL) < ((CacheS *)arg)->mu */
{
    CacheS *cache = (CacheS *)arg;
    
    while (true) {
	MethodCnts cnt;
	if (!Config_FreeAggressively) {
	    // snapshot cache load
	    cache->mu.lock();
	    cnt = cache->cnt;
	    cache->mu.unlock();
	}

	// pause
	Basics::thread::pause(Config_FreePauseDur);

	// increment the flush epoch
	cache->mu.lock();
	int lastFreeMPKFileEpoch = cache->freeMPKFileEpoch;
	cache->freeMPKFileEpoch++;

	if (!Config_FreeAggressively) {
	    // see if the cache is lightly loaded; if not, try again later
	    if (cnt != cache->cnt) {
		cache->mu.unlock();
		continue;
	    }
	}

	// collect list of MPKFiles that have not been accessed this epoch
	CacheS::MPKIter it(cache->mpkTbl);
	PKPrefixSeq toFlush(/*sizeHint=*/ cache->mpkTbl->Size() / 2);
	PKPrefixSeq toPurge(/*sizeHint=*/ cache->mpkTbl->Size() / 2);
	PKPrefix::T pfx; VMultiPKFile *mpk;
	while (it.Next(/*OUT*/ pfx, /*OUT*/ mpk)) {
	    if (mpk->IsStale(lastFreeMPKFileEpoch)) {
		toFlush.addhi(pfx);
	    } else if (mpk->IsUnmodified()) {
	        toPurge.addhi(pfx);
	    }
	}
	cache->mu.unlock();

	if (toFlush.size() > 0) {
	    // pre debugging
	    if (cache->debug >= CacheIntf::MPKFileFlush) {
	      cio().start_out() << Debug::Timestamp()
				<< "STARTED -- Flushing VMultiPKFiles" << endl
				<< "  number = " << toFlush.size() << endl << endl;
	      cio().end_out();
	    }

	    cache->mu.lock();
	    try {
		// free the cache entries consumed by them
		while (toFlush.size() > 0) {
		    cache->FlushMPKFile(toFlush.remlo(),
                      /*reason=*/ "CacheS_DoFreeMPKFiles called",
                      /*block=*/ true);
		}

		// wait for *all* flush workers to finish
		while (cache->numActiveFlushWorkers > 0) {
		    cache->allFlushWorkersDone.wait(cache->mu);
		}
	    } catch (...) { cache->mu.unlock(); throw; }
	    cache->mu.unlock();

	    // post debugging
	    if (cache->debug >= CacheIntf::MPKFileFlush) {
	      cio().start_out() << Debug::Timestamp()
				<< "FINISHED -- Flushing VMultiPKFiles" << endl << endl;
	      cio().end_out();
	    }

	    // clean the cache log
	    cache->cacheLogMu.lock();
	    try {
		cache->TryCleanCacheLog(/*upper_bound=*/ -1,
                  /*reason=*/ "CacheS_DoFreeMPKFiles thread");
	    } catch (...) { cache->cacheLogMu.unlock(); throw; }
	    cache->cacheLogMu.unlock();
        }

	// Loop over any unmodified MPKFiles purging old entries from
	// memory.
	if(toPurge.size() > 0)
	  {
	    // pre debugging
	    if (cache->debug >= CacheIntf::MPKFileFlush)
	      {
		cio().start_out() << Debug::Timestamp()
				  << "STARTED -- Freeing old entries from unmodified VMultiPKFiles" 
				  << endl << "  number = " << toPurge.size() << endl << endl;
		cio().end_out();
	      }

	    EntryState dState; // initialized all 0

	    while(toPurge.size() > 0)
	      {
		pfx = toPurge.remhi();

		cache->mu.lock();

		// Look up this VMultiPKFile
		bool inTbl = cache->mpkTbl->Get(pfx, mpk);
		assert(inTbl && (mpk != 0));

		// Take a copy of its VPKFile table
		SMultiPKFile::VPKFileMap vpks_in_mpk(&(mpk->VPKFileTbl()));

		cache->mu.unlock();

		// Loop over the VPKFiles in the VMultiPKFile
		SMultiPKFile::VPKFileIter it(&vpks_in_mpk);
		FP::Tag pk;
		VPKFile *vpk;
		while(it.Next(/*OUT*/ pk, /*OUT*/ vpk))
		  {
		    // Lock this one.
		    vpk->mu.lock();

		    // If this VPKFile is ready to have its warm
		    // entries purged,,,
		    if(vpk->ReadyForPurgeWarm(lastFreeMPKFileEpoch))
		      {
			// As a size hint (for the size of the table
			// after all entries are removed), use half
			// its current size.
			int sizeHint = vpk->OldEntries()->Size() / 2;

			// Have this VPKFile purge all its old
			// entries.
			vpk->DeleteOldEntries(sizeHint, /*INOUT*/ dState);
		      }

		    // Unlock it now that we're done with it.
		    vpk->mu.unlock();
		  }
	      }

	    // Update the cache server state based on the entries we
	    // just freed.
	    cache->mu.lock();
	    cache->state += dState;
	    cache->mu.unlock();

	    // post debugging
	    if (cache->debug >= CacheIntf::MPKFileFlush)
	      {
	        cio().start_out() << Debug::Timestamp()
		     << "FINISHED -- Freeing old entries from unmodified VMultiPKFiles" << endl
		     << "  entries freed = " << -(dState.oldEntryCnt) << endl
		     << "  pickled bytes freed = " << -(dState.oldPklSize)
		     << endl << endl;
		cio().end_out();
	      }
	  }

	// Pre-debugging for the freeing step
	if (cache->debug >= CacheIntf::MPKFileFlush)
	  {
	    cio().start_out() << Debug::Timestamp()
			      << "STARTED -- Freeing empty VPKFiles and VMultiPKFiles"
			      << endl << endl;
	    cio().end_out();
	  }

	// We need to iterate over all VPKFiles.  However, due to
	// locking order constraints, we can't hold cache->mu while
	// acquiring the individual VPKFile locks.  So, we take a copy
	// of the current table of VPKFiles and iterate over that.
	cache->mu.lock();
	CacheS::CacheMap vpk_tbl_copy(cache->cache, /*useGC=*/ true);
	cache->mu.unlock();

	// Look for VPKFiles to free.
	unsigned int num_vpks_freed = 0;
	CacheS::CacheMapIter vpk_it(&vpk_tbl_copy);
	FP::Tag pk;
	VPKFile *vpk;
	while(vpk_it.Next(/*OUT*/ pk, /*OUT*/ vpk))
	  {
	    vpk->mu.lock();

	    // If this VPKFile hasn't been touched recently, and it
	    // has neither new entries yet to be flushed to disk nor
	    // old entries, then we may be able to remove it.
	    if(vpk->ReadyForEviction(lastFreeMPKFileEpoch))
	      {
		cache->mu.lock();

		// Find its VMultiPKFile.
		PKPrefix::T pfx(pk);
		VMultiPKFile *mpk;
		bool inTbl = cache->mpkTbl->Get(pfx, mpk);
		assert(inTbl && (mpk != 0));

		// Last check: is this VMultiPKFile being re-written?
		// If so, we can't evict the VPKFile.  Also, if it
		// will be re-written soon, don't evict the VPKFile,
		// as in that case it would be re-created in the near
		// future.
		if(!mpk->FlushRunning() && !mpk->FlushPending())
		  {
		    // Remove the VPKFile from the cache table of
		    // VPKFiles.
		    VPKFile *removed;
		    inTbl = cache->cache->Delete(pk, removed, false);
		    assert(inTbl && (removed == vpk));

		    // Remove it from its VMultiPKFile too.
		    inTbl = mpk->Delete(pk, removed);
		    assert(inTbl && (removed == vpk));

		    // Mark this VPKFile as evicted (to avoid a race with
		    // adding entries).
		    vpk->Evict();

		    // If the stable PKFile for this VPKFile is empty,
		    // and it has a non-zero namesEpoch, remember its
		    // namesEpoch to prevent its namesEpoch from
		    // decreasing.  (If there's an evaluator between a
		    // FreeVars and Lookup call for this PK, and the
		    // namesEpoch decreases, the Lookup call will
		    // return "bad lookup args".)
		    if(vpk->IsStableEmpty() && (vpk->NamesEpoch() != 0))
		      {
			inTbl =
			  cache->evictedNamesEpochs.Put(pk,
							vpk->NamesEpoch());
			assert(!inTbl);
		      }

		    // After the end of this iteration, the
		    // VPKFile will not be referenced, and the
		    // garbage collector can reclaim its storage.
		    num_vpks_freed++;
		  }

		cache->mu.unlock();
	      }
	    vpk->mu.unlock();
	  }

	// Help out the garbage collector by removing our copy of
	// references to the VPKFiles.
	vpk_tbl_copy.Init();
	vpk = 0;

	// If we've removed any VPKFiles from the cache table, resize
	// it.  (We need to do this because we call Delete with
	// resize=false, which is necessary because we're iterating
	// over the table.)
	if(num_vpks_freed > 0)
	  {
	    cache->mu.lock();
	    cache->cache->Resize();
	    cache->mu.unlock();
	  }

	// To free VMultiPKFiles, we must iterate over and modify the
	// cache->mpkTbl.  Therefore we hold the cache server lock the
	// entire time.
	cache->mu.lock();

	// Look for VMultiPKFiles to free.
	unsigned int num_vmpks_freed = 0;
	it.Reset();
	while (it.Next(/*OUT*/ pfx, /*OUT*/ mpk))
	  {
	    // If this VMultiPKFile has no VPKFiles (because we
	    // removed its last one above), and there is no re-wriote
	    // of it running now or pending in the near future, remove
	    // it.
	    if((mpk->NumVPKFiles() == 0) &&
	       !mpk->FlushRunning() && !mpk->FlushPending())
	      {
		VMultiPKFile *removed;
		bool inTbl = cache->mpkTbl->Delete(pfx, removed, false);
		assert(inTbl && (removed == mpk));

		num_vmpks_freed++;
	      }
	  }

	// Help the garbage collector.
	mpk = 0;

	// If we've removed any VMultiPKFiles from mpkTbl, resize it.
	if(num_vmpks_freed > 0)
	  {
	    cache->mpkTbl->Resize();
	  }

	// Now that we're done freeing VPKFiles and VMultiPKFiles (and
	// thus modifying the cache's data structures), we can unlock
	// its mutex.
	cache->mu.unlock();

	// Post-debugging for the freeing step
	if (cache->debug >= CacheIntf::MPKFileFlush)
	  {
	    cio().start_out() << Debug::Timestamp()
		 << "FINISHED -- Freeing empty VPKFiles and VMultiPKFiles"
		 << endl
		 << "  VPKFiles freed = " << num_vpks_freed << endl
		 << "  VMultiPKFiles freed = " << num_vmpks_freed
		 << endl << endl;
	    cio().end_out();
	  }
    }
    //assert(false); // not reached
    //return (void *)NULL;
} // CacheS_DoFreeMPKFiles

// Weeder client methods ------------------------------------------------------

bool CacheS::WeederRecovering(SRPC *srpc, bool doneMarking) throw ()
/* REQUIRES Sup(LL) < SELF.mu */
{
    this->mu.lock();

    // check if a weed is already in progress
    if (this->weederSRPC != (SRPC *)NULL) {
	if (this->weederSRPC->alive()) {
	    this->mu.unlock();
	    return true;
	}
    }

    // cache this srpc connection to indicate that a weed is in progress
    this->weederSRPC = srpc;

    // resume lease expiration
    try {
        // if lease expiration is disabled, then "doneMarking" must be "false"
        assert(this->leases->ExpirationIsEnabled() || !doneMarking);
	this->leases->EnableExpiration();

	// test if we need to back out to the idle state
	if (!(this->hitFilter.IsEmpty()) && !this->deleting && !doneMarking) {
	    this->ClearStableHitFilter();
	}
    } catch (...) { this->mu.unlock(); throw; }
    this->mu.unlock();
    return false;
}

BitVector* CacheS::StartMark(/*OUT*/ int &newLogVer) throw ()
/* REQUIRES Sup(LL) < SELF.graphLogMu */
{
    // main work
    BitVector *res;
    int currChkptVer;
    this->mu.lock();
    try {
	while (this->deleting) {
	    this->notDeleting.wait(this->mu);
	}
	res = NEW_CONSTR(BitVector, (&(this->usedCIs)));
	this->leases->DisableExpiration();
	currChkptVer = this->graphLogChkptVer;
    } catch (...) { this->mu.unlock(); throw; }
    this->mu.unlock();

    // flush & checkpoint the GraphLog
    try {
	this->FlushGraphLog();

	// abort an old checkpoint if one is outstanding
	if (currChkptVer >= 0) this->AbortGraphLogChkpt();

        // start a new checkpoint
	newLogVer = this->ChkptGraphLog();
	assert(newLogVer >= 0);
    } catch (const FS::Failure &f) {
	cerr << "Fatal FS error: ";
	cerr << "while checkpointing \"graphLog\"; exiting..." << endl;
	cerr << f << endl;
	exit(1);
    } catch (const VestaLog::Error &err) {
	cerr << "Fatal VestaLog error: ";
	cerr << "while flushing \"graphLog\"" << endl
	     << "  " << err.msg;
	if(err.r != 0)
	  {
	    cerr << " (errno = " << err.r << ")";
	  }
	cerr << endl << "Exiting..." << endl;
	exit(1);
    }

    // record the outstanding graphLog checkpoint number
    this->mu.lock();
    this->graphLogChkptVer = newLogVer;
    this->mu.unlock();

    return res;
}

void CacheS::SetHitFilter(const BitVector &cis) throw ()
/* REQUIRES Sup(LL) < SELF.mu */
{
    this->mu.lock();
    try {
	assert(!this->deleting);
	this->SetStableHitFilter(cis);
    } catch (...) { this->mu.unlock(); throw; }
    this->mu.unlock();
}

BitVector* CacheS::GetLeases() throw ()
/* REQUIRES Sup(LL) < SELF.mu */
{
    BitVector *res;
    this->mu.lock();
    try {
	res = leases->LeaseSet();
    } catch (...) { this->mu.unlock(); throw; }
    this->mu.unlock();
    return res;
}

void CacheS::ResumeLeaseExp() throw ()
/* REQUIRES Sup(LL) < SELF.mu */
{
    this->mu.lock();
    try {
	leases->EnableExpiration();
    } catch (...) { this->mu.unlock(); throw; }
    this->mu.unlock();
}

int CacheS::EndMark(const BitVector &cis, const PKPrefix::List &pfxs) throw ()
/* REQUIRES
     Sup(LL) < SELF.mu AND
     !cis.IsEmpty() AND
     (this->hitFilter.IsEmpty() OR (cis <= this->hitFilter))
*/
{
    int chkptVer;

    // verify preconditions; record whether "hitFilter" is empty, "deleting"
    assert(!(cis.IsEmpty()));
    bool emptyHF, deleting2;
    this->mu.lock();
    try {
	emptyHF = this->hitFilter.IsEmpty();
	deleting2 = this->deleting;
	chkptVer = this->graphLogChkptVer;
	assert(emptyHF || (cis <= this->hitFilter));
    } catch (...) { this->mu.unlock(); throw; }
    this->mu.unlock();

    // record result if necessary
    if (chkptVer < 0) {
	this->graphLogMu.lock();
	try {
	    chkptVer = this->graphLog->logVersion();
	} catch (const VestaLog::Error &err) {
	    cerr << "VestaLog fatal error: ";
	    cerr << "failed calling 'logVersion' in 'CacheS::EndMark" << endl;
	    cerr << "  num = " << err.r << "; " << err.msg << endl;
	    cerr << "Exiting..." << endl;
	    exit(1);
	}
	this->graphLogMu.unlock();
	this->mu.lock();
	this->graphLogChkptVer = chkptVer;
	this->mu.unlock();
    }

    // only advance to state 2 if currently in state 1
    if (!emptyHF && !deleting2) {
	this->mu.lock();
	try {
	    // set hitFilter
	    this->SetStableHitFilter(cis);

	    // set "pksToWeed", reset "pksWeeded"
	    this->SetMPKsToWeed(pfxs);
	    this->ResetWeededMPKs();

	    // enable deletions
	    this->SetStableDeleting(true);
	    this->doDeleting.signal();
	} catch (...) { this->mu.unlock(); throw; }
	this->mu.unlock();
    }
    assert(chkptVer >= 0);
    return chkptVer;
} // CacheS::EndMark

bool CacheS::CommitChkpt(const Text &chkptFileName) throw ()
/* REQUIRES Sup(LL) < SELF.graphLogMu */
{
  // Construct the full path of the checkpoint file that the
  // weeder has provided us with.  It comes in relative to the
  // graph log root, so we need to prepend that to it.
  Text temp_chkptFileName(Config_GraphLogPath);
  temp_chkptFileName += '/';
  temp_chkptFileName += chkptFileName;

  // Get the graph log checkpoint verion we're about to commit.
  this->mu.lock();
  int chkptVer = this->graphLogChkptVer;
  this->mu.unlock();

  // If it's positive, then we are actually in the middle of
  // checkpointing it and we should continue.  Otherwise, this is some
  // kind of spurious call.
  if(chkptVer >= 0)
    {
      // The checkpoint filename should be relative, not absolute.  If
      // it looks like an absolute filename, reject it.
      if(chkptFileName[0] == '/')
	{
	  return false;
	}

      // Determine the full filename that the checkpoint.should have.
      Text final_chkptFileName(Config_GraphLogPath);
      // WARNING: this is just asking for a buffer overrun.  Should be
      // OK for 64-bit ints though.
      char buff[26];
      int spres = sprintf(buff, "/%d.ckp", chkptVer);
      assert((spres >= 0) && (spres <= 25));
      final_chkptFileName += buff;

      // We expect the checkpoint to be the same as the real name but
      // with a suffix for uniqueness.  If it's not, reject it.
      if((temp_chkptFileName.Length() <= final_chkptFileName.Length()) ||
	 (temp_chkptFileName.Sub(0, final_chkptFileName.Length())
	  != final_chkptFileName))
	{
	  return false;
	}

      // Check to make sure that the new checkpoint exists.  If it
      // doesn't, reject it.
      if(!FS::Exists(temp_chkptFileName))
	{
	  return false;
	}

      // Rename the [pruned] checkpoint of the graph log to the name
      // it should really have.
      if(rename(temp_chkptFileName.cchars(),
		final_chkptFileName.cchars()) != 0)
	{
	  // If the rename fails, we treat it as a fatal error.  It
	  // would be nice to handle it a little more robustly, but
	  // this puts us in kind of a tough situation.  If this ever
	  // actually comes up, it could leave the cache in a somewhat
	  // invalid state.
	  cerr << "CacheS::CommitChkpt fatal error: rename(2) failed!" << endl;
	  cerr << "  errno = " << errno << endl;
	  cerr << "  from  = " << temp_chkptFileName << endl;
	  cerr << "  to    = " << final_chkptFileName << endl;
	  assert(false);
	}
      else
	{
	  // Now actually complete the checkpoint.
	  this->graphLogMu.lock();
	  try {
	    this->graphLog->checkpointEnd();
	    this->graphLog->prune(/*ckpkeep=*/ 1);
	    this->mu.lock();
	    this->graphLogChkptVer = -1;
	    this->weederSRPC = NULL; // weeder is done; unlock
	    this->mu.unlock();
	  } catch (const VestaLog::Error &err) {
	    cerr << "Fatal VestaLog error: ";
	    cerr << "while committing \"graphLog\" checkpoint" << endl
		 << "  " << err.msg;
	    if(err.r != 0)
	      {
		cerr << " (errno = " << err.r << ")";
	      }
	    cerr << endl << "Exiting..." << endl;
	    exit(1);
	  }
	  this->graphLogMu.unlock();

	  // Indicate that we have accepted the checkpoint.
	  return true;
	}
    }
  // If we know that we're not going to use this checkpointed graph
  // log, delete it.
  else
    {
      try
	{
	  FS::Delete(temp_chkptFileName);
	}
      catch(FS::Failure)
	{
	  // We don't really care if that operation failed.
	}

      // Perhaps we should do something to tell the caller that we've
      // decided not to accept their checkpoint?

    }

  // If we make it here, we did not accept the checkpoint.  (Perhaps
  // we weren't expecting a graph log checkpoint right now?)  Indicate
  // to the caller that we have declined their checkpoint.
  return false;
}

void *CacheS_DoDeletions(void *arg) throw ()
/* REQUIRES LL = 0 */
{
    CacheS *cs = (CacheS *)arg;	// unchecked NARROW

    while (true) {
	// wait until "deleting"
	cs->mu.lock();
	while (!(cs->deleting)) {
	    cs->doDeleting.wait(cs->mu);
	}

	// pre debugging
	if (cs->debug >= CacheIntf::WeederOps) {
	  cio().start_out() << Debug::Timestamp() 
			    << "STARTED -- DoDeletions" << endl << endl;
	  cio().end_out();
	}

	// flush necessary "VPKFiles"
	int cnt = 0;
	while (cs->nextMPKToWeed < cs->mpksToWeed.len) {
            // Invariant: "cs->mu" is locked

	    // flush the next VMPKFile
	    int next = cs->nextMPKToWeed;
	    cs->mu.unlock();
	    cs->VToSCache(cs->mpksToWeed.pfx[next], &(cs->hitFilter));
	    cs->mu.lock();
	    cs->nextMPKToWeed++;

	    // log every 10th one
	    if (++cnt == 10) {
		cs->AddToWeededMPKs(cnt);
		cnt = 0;
	    }
	}
	if (cnt > 0) cs->AddToWeededMPKs(cnt);
	cs->mu.unlock();

	// checkpoint the new value of "usedCIs"
	try {
	    cs->ChkptUsedCIs(cs->hitFilter);
	} catch (const VestaLog::Error &err) {
	    cerr << "Fatal VestaLog error: ";
	    cerr << "while checkpointing \"usedCIs\"" << endl
		 << "  " << err.msg;
	    if(err.r != 0)
	      {
		cerr << " (errno = " << err.r << ")";
	      }
	    cerr << endl << "Exiting..." << endl;
	    exit(1);
	} catch (const FS::Failure &f) {
	    cerr << "Fatal FS error: ";
	    cerr << "while checkpointing \"usedCIs\"; exiting..." << endl;
	    cerr << f << endl;
	    exit(1);
	}

	// set "hitFilter = {}"
	cs->mu.lock();
	cs->ClearStableHitFilter();

	// done deleting
	cs->SetStableDeleting(false);
	cs->notDeleting.signal();
	cs->mu.unlock();

	// post debugging
	if (cs->debug >= CacheIntf::WeederOps) {
	  cio().start_out() << Debug::Timestamp() 
			    << "FINISHED -- DoDeletions" << endl << endl;
	  cio().end_out();
	}

	// clean the cache log
	cs->cacheLogMu.lock();
	try {
	    cs->TryCleanCacheLog(/*upper_bound=*/ -1,
              /*reason=*/ "CacheS_DoDeletions thread");
	} catch (...) { cs->cacheLogMu.unlock(); throw; }
	cs->cacheLogMu.unlock();
    }
    //assert(false);  // not reached
    //return (void *)NULL;
} // CacheS_DoDeletions

/* Extra methods ----------------------------------------------------------- */

void CacheS::Checkpoint(const FP::Tag &pkgVersion, Model::T model,
  CacheEntry::Indices *cis, bool done) throw ()
/* REQUIRES Sup(LL) < SELF.chkptMu */
{
    // queue the checkpoint work to be done by the checkpoint thread
    ChkptWorker *ckpt = this->QueueChkpt(pkgVersion, model, cis, done);

    // if call is synchronous, block until the thread completes
    if (done) {
	ckpt->WaitUntilDone();
	this->FinishChkpt(ckpt); // return object to avail list
    }
}

ChkptWorker *CacheS::QueueChkpt(const FP::Tag &pkgVersion, Model::T model,
  CacheEntry::Indices *cis, bool done) throw ()
/* REQUIRES Sup(LL) < SELF.chkptMu */
{
    ChkptWorker *res;
    this->chkptMu.lock();
    try {
	// get the next available ChkptWorker object
	if (this->availChkptWorkers != (ChkptWorker *)NULL) {
	    res = this->availChkptWorkers;
	    this->availChkptWorkers = res->next;
	    res->Init(pkgVersion, model, cis, done);
	} else {
	  res = NEW_CONSTR(ChkptWorker, (pkgVersion, model, cis, done));
	}

	// add it to the end of the queue
        ChkptWorker *last = (ChkptWorker *)NULL;
	ChkptWorker *curr = this->queuedChkptWorkers;
	while (curr != (ChkptWorker *)NULL) {
	    last = curr; curr = curr->next;
	}
	if (last == (ChkptWorker *)NULL)
	    this->queuedChkptWorkers = res;
	else
	    last->next = res;
    } catch (...) { this->chkptMu.unlock(); throw; }
    this->chkptMu.unlock();

    // signal the worker thread that there is work to do
    this->waitingChkptWorker.signal();
    return res;
}

void CacheS::FinishChkpt(ChkptWorker *cw) throw ()
/* REQUIRES Sup(LL) < SELF.chkptMu */
{
    this->chkptMu.lock();
    try {
	// return the ChkptWorker object to the avail list
	cw->Reset();
	cw->next = this->availChkptWorkers;
	this->availChkptWorkers = cw;
    } catch (...) { this->chkptMu.unlock(); throw; }
    this->chkptMu.unlock();
}

void *ChkptWorker::MainLoop(void *arg) throw ()
/* REQUIRES Sup(LL) < CacheS::ciLogMu */
{
    CacheS *cache = (CacheS *)arg;
    while (true) {
	// wait until there is more work to do
	cache->chkptMu.lock();
	while (cache->queuedChkptWorkers == (ChkptWorker *)NULL) {
	    cache->waitingChkptWorker.wait(cache->chkptMu);
	}
	ChkptWorker *curr = cache->queuedChkptWorkers;
	// skip asynchronous checkpoints if more are pending
	while (!(curr->done) && curr->next != (ChkptWorker *)NULL) {
	    // return skipped object to avail list
	    ChkptWorker *avail = curr;
	    curr = curr->next;
	    avail->Reset();
	    avail->next = cache->availChkptWorkers;
	    cache->availChkptWorkers = avail;
	}
	assert(curr != (ChkptWorker *)NULL);
	cache->queuedChkptWorkers = curr->next;
	curr->next = (ChkptWorker *)NULL;
	cache->chkptMu.unlock();

	// do the work
	cache->DoCheckpoint(curr->pkgVersion, curr->model,
          curr->cis, curr->done);

	// finish
	if (curr->done) {
	    // signal the waiting thread in the synchronous case
	    curr->mu.lock();
	    curr->chkptComplete = true;
	    curr->mu.unlock();
	    curr->isDone.signal();
	} else {
	    // in the asynchronous case, return the worker to the avail list
	    cache->FinishChkpt(curr);
	}
    }
    //assert(false);  // not reached
    //return (void *)NULL;
} // ChkptWorker::MainLoop

void CacheS::DoCheckpoint(const FP::Tag &pkgVersion, Model::T model,
  CacheEntry::Indices *cis, bool done) throw ()
/* REQUIRES Sup(LL) < SELF.ciLogMu */
{
  // flush all logs
  try {
    this->FlushCacheLog();
  }
  catch (const VestaLog::Error &err) {
    cerr << "Fatal VestaLog error: ";
    cerr << "while flushing \"cacheLog\"" << endl
	 << "  " << err.msg;
    if(err.r != 0)
      {
	cerr << " (errno = " << err.r << ")";
      }
    cerr << endl << "Exiting..." << endl;
    exit(1);
  }
  
  // create root node
  GraphLog::Root root(pkgVersion, model, cis, done);

  int i;
  this->mu.lock();

  // check for necessary cache entry leases
  for(i = 0; i < cis->len; i++) {
    if(!this->leases->IsLeased(cis->index[i])) break;
  }
  this->mu.unlock();
  if(i < cis->len) {
    // exited loop prematurely
    ostream& err_stream = cio().start_err();
    err_stream << Debug::Timestamp()				\
	       << "Checkpint rejected: ci " << cis->index[i] 
	       << " is not leased." << endl << endl;
    // Print the full GraphLog root we're rejecting
    root.DebugFull(err_stream);
    cio().end_err();
  }
  else {
    // debugging
    if (this->debug >= CacheIntf::LogFlush) {
      ostream& out_stream = cio().start_out();
      try {
	out_stream << Debug::Timestamp() << "Writing root to GraphLog:" << endl;
	root.Debug(out_stream);
	out_stream << endl;
      } 
      catch (...) { cio().end_out(); throw; }
      cio().end_out();
    }
    
    // write GraphLog root
    this->graphLogMu.lock();
    try {
      this->graphLog->start();
      root.Log(*(this->graphLog));
      this->graphLog->commit();
    }
    catch (const VestaLog::Error &err) {
      cerr << "Fatal VestaLog error: ";
      cerr << "writing root node to \"graphLog\"" << endl
	   << "  " << err.msg;
      if(err.r != 0)
	{
	  cerr << " (errno = " << err.r << ")";
	}
      cerr << endl << "Exiting..." << endl;
      exit(1);
    }
    catch (...) { this->graphLogMu.unlock(); throw; }
    this->graphLogMu.unlock();
  }
} // CacheS::DoChekpoint

void ChkptWorker::Init(const FP::Tag &pkgVersion, Model::T model,
  CacheEntry::Indices *cis, bool done) throw ()
{
    this->next = (ChkptWorker *)NULL;
    this->pkgVersion = pkgVersion;
    this->model = model;
    this->cis = cis;
    this->done = done;
    this->chkptComplete = false;
}

void ChkptWorker::Reset() throw ()
{
    this->cis = (CacheEntry::Indices *)NULL; // drop pointer on floor
}

void ChkptWorker::WaitUntilDone() throw ()
/* REQUIRES Sup(LL) < SELF.mu */
{
    this->mu.lock();
    while (!(this->chkptComplete)) {
	this->isDone.wait(this->mu);
    }
    this->mu.unlock();
}

void CacheS::FlushAll() throw ()
/* REQUIRES 
     Sup(LL) < SELF.ciLogMu AND
     (FORALL vpk: VPKFile :: Sup(LL) < vpk.mu) */
{
    // determine size of MultiPKFile table
    this->mu.lock();
    int mpkCnt = this->mpkTbl->Size();

    // return if there is no work
    if (mpkCnt == 0)
      {
	this->mu.unlock();
	return;
      }

    // save prefixes to flush with "this->mu" held
    PKPrefix::T *toFlush = NEW_PTRFREE_ARRAY(PKPrefix::T, mpkCnt);
    try {
	// copy the prefixes in "mpkTbl" into "toFlush"
	MPKIter it(this->mpkTbl);
	PKPrefix::T pfx; VMultiPKFile *mpk;
	int i;
	for (i = 0; it.Next(/*OUT*/ pfx, /*OUT*/ mpk); i++) {
	    assert(i < mpkCnt);
	    toFlush[i] = pfx;
	}

	// flush all the prefixes to the stable cache
	for (i = 0; i < mpkCnt; i++) {
	    this->FlushMPKFile(toFlush[i],
              /*reason=*/ "CacheS::FlushAll called", /*block=*/ true);
	}

	// wait for *all* flush workers to finish
	while (this->numActiveFlushWorkers > 0) {
	    this->allFlushWorkersDone.wait(this->mu);
	}
    } catch (...) { this->mu.unlock(); throw; }
    this->mu.unlock();

    // clean the cache log
    this->cacheLogMu.lock();
    try {
	this->TryCleanCacheLog(/*upper_bound=*/ -1,
          /*reason=*/ "CacheS::FlushAll called");
    } catch (...) { this->cacheLogMu.unlock(); throw; }
    this->cacheLogMu.unlock();
} // CacheS::FlushAll

void CacheS::GetCacheId(/*OUT*/ CacheId &id) throw ()
/* REQUIRES Sup(LL) < SELF.mu */
{
    char name[MAXHOSTNAMELEN];
    int res = gethostname(name, MAXHOSTNAMELEN);
    id.host = (res == 0) ? Text(name) : Text("<unknown host>");
    id.port = Config_Port;
    id.stableDir = Config_SCachePath;
    id.cacheVersion = VCacheVersion;
    id.intfVersion = CacheIntf::Version;
    this->mu.lock();
    id.startTime = this->startTime;
    this->mu.unlock();
}

void CacheS::GetCacheState(/*OUT*/ CacheState &state) throw ()
/* REQUIRES Sup(LL) < SELF.mu */
{
    unsigned long total, resident;
    OS::GetProcessSize(total, resident);
    state.virtualSize = total;
    state.physicalSize = resident;

    this->mu.lock();
    state.vmpkCnt = this->mpkTbl->Size();
    state.vpkCnt = this->cache->Size();
    state.entryCnt = this->entryCnt;
    state.cnt = this->cnt;
    state.s = this->state;
    state.hitFilterCnt = this->hitFilter.Cardinality();
    if (this->deleting) {
	state.delEntryCnt = state.hitFilterCnt;
	state.mpkWeedCnt = this->mpksToWeed.len - this->nextMPKToWeed;
    } else {
	state.delEntryCnt = 0;
	state.mpkWeedCnt = 0;
    }
    this->mu.unlock();
}

const FP::Tag &CacheS::GetCacheInstance()
     const
     throw ()
/* Must only be called after the instance is initialized, as read
   access to the instance fingerprint is not protected by a mutex. */
{
    return this->instanceFp;
}

bool CacheS::RenewLeases(CacheEntry::Indices *cis) throw ()
/* REQUIRES Sup(LL) < SELF.mu */
{
    bool res = true;
    this->mu.lock();
    try {
	for (int i = 0; i < cis->len; i++) {
	    CacheEntry::Index ci = cis->index[i];
	    if (this->usedCIs.Read(ci)) {
		try {
		    this->leases->RenewLease(ci);
		} catch (Leases::NoLease) {
		    res = false;
		}
	    } else {
		res = false;
	    }
	}
    } catch (...) { this->mu.unlock(); throw; }
    this->mu.unlock();
    return res;
}

void CacheS::VToSCache(const PKPrefix::T &pfx, const BitVector *toDelete)
  throw ()
/* REQUIRES 
     Sup(LL) < SELF.ciLogMu AND
     (FORALL vpk: VPKFile :: Sup(LL) < vpk.mu) */
{
    // see if the MultiPKFile needs to be rewritten
    VMultiPKFile *mpk = (VMultiPKFile *)NULL;
    this->mu.lock();
    try {
	if (!this->mpkTbl->Get(pfx, /*OUT*/ mpk)) {
	    /* There are no volatile cache entries for this MultiPKFile, but
	       we may still have to rewrite the MultiPKFile if deletions are
	       pending. */
	    if (toDelete != (BitVector *)NULL) {
	      // create a new, empty VMultiPKfile for "pfx"
	      mpk = NEW_CONSTR(VMultiPKFile, (pfx));
	      bool inMPK = this->mpkTbl->Put(pfx, mpk); assert(!inMPK);
	    }
	} else {
	  assert((mpk != NULL) && (mpk->Prefix() == pfx));
	}
    } catch (...) { this->mu.unlock(); throw; }

    // return if there is no work to do
    if (mpk == (VMultiPKFile *)NULL)
      {
	this->mu.unlock();
	return;
      }

    // Acquire and exclusive lock to write the MPKFile.  This ensures
    // that this is the only thread writing it.
    if(mpk->LockForWrite(this->mu, toDelete))
      {
	this->mu.unlock();

	// pre debugging
	if (this->debug >= CacheIntf::MPKFileFlush) {
	  cio().start_out() << Debug::Timestamp() 
			    << "STARTED -- Flushing MPKFile" << endl
			    << "  prefix = " << pfx << endl << endl;
	  cio().end_out();
	}

	// actual work
	try {

	  // Next we need to read the header of the stable MultiPKFile.
	  // We do this to determine the set of PKs in the stable copy,
	  // so that we can ensure that there is VPKFile for each.  (If
	  // we leave any without VPKFiles, entries added by clients
	  // during the reqrite may not be properly updated by the
	  // changes made during the rewrite.)
	  ifstream mpkfile_ifs;
	  SMultiPKFileRep::Header *mpkfile_hdr;
	  bool mpkFileExists =
	    SMultiPKFile::PrepareForRewrite(pfx, mpkfile_ifs, mpkfile_hdr);

	  // Now make sure we have VPKFiles
	  for (unsigned int i = 0; i < mpkfile_hdr->num; i++)
	    {
	      FP::Tag *pki = mpkfile_hdr->pkSeq[i];
	      assert(PKPrefix::T(*pki) == pfx);

	      VPKFile *vpk;
	      // (We could use CacheS::GetVPKFile here, but since we
	      // already know which VMultiPKFile any new ones would
	      // belong in, this is slightly more efficient.)
	      this->mu.lock();
	      if(!this->FindVPKFile(*pki, /*OUT*/ vpk))
		{
		  // Install it in the cache and the MultiPKFile.
		  bool inCache = cache->Put(*pki, vpk); assert(!inCache);
		  bool inVMPK = mpk->Put(*pki, vpk); assert(!inVMPK);
		}
	      this->mu.unlock();
	    }

	  // Prepare to write the MPKFile, capturing all the VPKFiles
	  // to be written and their current state.  Note that we must
	  // do this *before* flushing the graph log, as new entry
	  // additions may take place in parallel with us, and entries
	  // must be written to the graph log before being written the
	  // their MultiPKFiles.  (This may return false indicating
	  // that nothing has changed and there's therefore no work to
	  // do.)
	  SMultiPKFile::VPKFileMap vpksToFlush;
	  SMultiPKFile::ChkPtTbl vpkChkptTbl;
	  if(mpk->ChkptForWrite(this->mu, toDelete, vpksToFlush, vpkChkptTbl))
	    {
	      // Before actually rewriting the MultiPKFile, flush the
	      // GraphLog and UsedCIs log
	      try
		{
		  this->FlushGraphLog();
		}
	      // If anything goes wrong with flushing the graph log...
	      catch(...)
		{
		  // Release the writing lock for this MPK acquired by
		  // VMultiPKFile::LockForWrite
		  mpk->ReleaseWriteLock(this->mu);

		  // Close the old MultiPKFile if we opened one.
		  if(mpkFileExists) FS::Close(mpkfile_ifs);

		  throw;
		}

	      // rewrite the MultiPKFile corresponding to "mpk"
	      EntryState dState; // initialized all 0
	      mpk->ToSCache(this->mu,

			    // From SMultiPKFile::PrepareForRewrite
			    mpkFileExists, mpkfile_ifs, mpkfile_hdr,

			    // From VMultiPKFile::ChkptForRewrite
			    vpksToFlush, vpkChkptTbl,

			    toDelete, this->emptyPKLog,
			    /*INOUT*/ dState);

	      // delete the "VMultiPKFile" in the cache if it is empty
	      /*** NYI ***/

	      // update "this->state" from "dState"
	      this->mu.lock();
	      this->state += dState;
	      this->mu.unlock();
	    }
	  else
	    {
	      // Since we decided there were no changes to commit, close
	      // the old MultiPKFile.
	      if(mpkFileExists) FS::Close(mpkfile_ifs);
	    }
	}
	catch (const VestaLog::Error &e) {
	  // fatal error
	  cerr << "VestaLog::Error:" << endl;
	  cerr << "  " << e.msg;
	  if (e.r != 0) cerr << ", errno = " << e.r;
	  cerr << endl;
	  assert(false);
	}
	catch (const FS::Failure &f) {
	  // fatal error
	  cerr << f;
	  assert(false);
	}
	catch (const FS::EndOfFile &f) {
	  // fatal error
	  cerr << "CacheS::VToSCache: Unexpected end of file" << endl;
	  assert(false);
	}

	// post debugging
	if (this->debug >= CacheIntf::MPKFileFlush) {
	  cio().start_out() << Debug::Timestamp() 
			    << "FINISHED -- Flushing MPKFile" << endl
			    << "  prefix = " << pfx << endl << endl;
	  cio().end_out();
	}
      }
    // If VMultiPKFile::LockForWrite indicates that there's nothing to
    // do, we still need to release CacheS.mu.
    else
      {
	this->mu.unlock();
      }
} // CacheS::VToSCache

void CacheS::Recover() throw (VestaLog::Error, VestaLog::Eof, FS::Failure)
/* REQUIRES LL = Empty */
{
    this->RecoverCILog();
    this->RecoverDeleting();
    this->RecoverHitFilter();
    this->mu.lock();
    bool localDeleting;
    localDeleting = this->deleting;
    this->mu.unlock();
    if (localDeleting) {
	/* The list of MPKFiles to weed only needs to be recovered
           if we are in the process of deleting cache entries. */
	this->RecoverMPKsToWeed();
    }
    this->RecoverWeededMPKs();
    this->RecoverGraphLog();
    this->RecoverCacheLog();
}

// Stable variables -----------------------------------------------------------

void CacheS::RecoverDeleting() throw (FS::Failure)
/* REQUIRES LL = Empty */
{
    ifstream ifs;
    bool delVal;  // for debugging
    try {
	FS::OpenReadOnly(Config_DeletingFile, /*OUT*/ ifs);
	this->mu.lock();
	try {
	    FS::Read(ifs, (char *)(&(this->deleting)), sizeof(this->deleting));
	    delVal = this->deleting;
	} catch (...) {
	    this->mu.unlock();
	    FS::Close(ifs);
	    throw;
	}
	this->mu.unlock();
	FS::Close(ifs);
    }
    catch (FS::DoesNotExist) {
	this->mu.lock();
	delVal = this->deleting = false;
	this->mu.unlock();
    }
    catch (FS::EndOfFile) {
	// programming error
	assert(false);
    }

    // debugging
    if (this->debug >= CacheIntf::LogRecover) {
      cio().start_out() << Debug::Timestamp() 
			<< "RECOVERED -- Deleting" << endl
			<< "  deleting = " << BoolName[delVal] << endl << endl;
      cio().end_out();
    }
}

void CacheS::SetStableDeleting(bool del) throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    // set volatile "deleting" variable
    this->deleting = del;

    // write "deleting" atomically
    AtomicFile ofs;
    try {
	ofs.open(Config_DeletingFile.chars(), ios::out);
	if (!ofs) {
	    throw (FS::Failure(Text("CacheS::SetStableDeleting"),
			       Config_DeletingFile));
	}
	FS::Write(ofs, (char *)(&(this->deleting)), sizeof(this->deleting));
	FS::Close(ofs); // swing file pointer atomically
    }
    catch (const FS::Failure &f) {
	cerr << "unexpected FS error saving \"deleting\"; exiting..." << endl;
	cerr << f << endl;
	exit(1);
    }
}

void CacheS::RecoverHitFilter() throw (FS::Failure)
/* REQUIRES LL = Empty */
{
    ifstream ifs;
    try {
	FS::OpenReadOnly(Config_HitFilterFile, /*OUT*/ ifs);
	this->mu.lock();
	try {
	    this->hitFilter.Read(ifs);
	} catch (...) {
	    this->mu.unlock();
	    FS::Close(ifs);
	    throw;
	}
	this->mu.unlock();
	FS::Close(ifs);
    }
    catch (FS::DoesNotExist) {
	/* SKIP -- use initial (empty) value for "hitFilter" */
    }
    catch (FS::EndOfFile) {
	// programming error
	assert(false);
    }

    // debugging
    if (this->debug >= CacheIntf::LogRecover) {
	this->mu.lock(); // required to protect read of "hitFilter"
        try {
	  ostream& out_stream = cio().start_out();
	  try {
	     out_stream << Debug::Timestamp() 
			<< "RECOVERED -- HitFilter" << endl
			<< "  hitFilter = " << this->hitFilter << endl << endl;
	  } catch (...) { cio().end_out(); throw; }
	  cio().end_out();
        } catch (...) { this->mu.unlock(); throw; }
	this->mu.unlock();
    }
}

void CacheS::WriteHitFilter() throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    /* For now, we do this atomically, although the spec says we do not
       have to. ***/
    AtomicFile ofs;
    try {
	ofs.open(Config_HitFilterFile.chars(), ios::out);
	if (ofs.fail()) {
	    throw (FS::Failure(Text("CacheS::WriteHitFilter"),
			       Config_HitFilterFile));
	}
	this->hitFilter.Write(ofs);
	FS::Close(ofs); // swing file pointer atomically
    }
    catch (const FS::Failure &f) {
	cerr << "unexpected FS error saving \"hitFilter\"; exiting..." << endl;
	cerr << f;
	exit(1);
    }
}

void CacheS::ClearStableHitFilter() throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    this->hitFilter.ResetAll(/*freeMem =*/ true);
    this->WriteHitFilter();
}

void CacheS::SetStableHitFilter(const BitVector &hf) throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    this->hitFilter = hf;
    this->WriteHitFilter();
}

void CacheS::SetMPKsToWeed(const PKPrefix::List &pfxs) throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    // set "mpksToWeed"
    this->mpksToWeed = pfxs;

    // write "mpksToWeed" to a stable log
    AtomicFile ofs;
    try {
	ofs.open(Config_MPKsToWeedFile.chars(), ios::out);
	if (ofs.fail()) {
	    throw (FS::Failure(Text("CacheS::SetMPKsToWeed"),
			       Config_MPKsToWeedFile));
	}
	this->mpksToWeed.Write(ofs);
	FS::Close(ofs); // swing file pointer atomically
    }
    catch (const FS::Failure &f) {
	cerr << "unexpected FS error saving \"mpksToWeed\"; exiting..." <<endl;
	cerr << f;
	exit(1);
    }
}

void CacheS::RecoverMPKsToWeed() throw (FS::Failure)
/* REQUIRES LL = Empty */
{
    ifstream ifs;
    try {
	FS::OpenReadOnly(Config_MPKsToWeedFile, /*OUT*/ ifs);
	this->mu.lock();
	try {
	    mpksToWeed.Read(ifs);
	} catch (...) {
	    this->mu.unlock();
	    FS::Close(ifs);
	    throw;
	}
	this->mu.unlock();
	FS::Close(ifs);
    }
    catch (FS::DoesNotExist) {
	/* SKIP -- use initial (empty) value for "mpksToWeed" */
    }
    catch (FS::EndOfFile) {
	// programming error
	assert(false);
    }

    // debugging
    if (this->debug >= CacheIntf::LogRecover) {
	this->mu.lock(); // required to protect read of "mpksToWeed"
	try {
	  ostream& out_stream = cio().start_out();
	  try {
	    out_stream << Debug::Timestamp() << "RECOVERED -- MPKsToWeed" << endl;
	    if (this->mpksToWeed.len > 0) {
	      for (int i = 0; i < this->mpksToWeed.len; i++) {
		out_stream << "  pfx[" << i << "] = "
			   << this->mpksToWeed.pfx[i] << endl;
	      }
	    } else {
	      out_stream << "  <<Empty>>" << endl;
	    }
	    out_stream << endl;
	  } catch (...) { cio().end_out(); throw; }
	  cio().end_out();
	} catch (...) { this->mu.unlock(); throw; }
	this->mu.unlock();
    }
}

static int CacheS_ReadLogVersion(const Text &dirName)
  throw (FS::Failure, FS::DoesNotExist)
/* Read the file named "version" in the directory "dirName". If it exists,
   return the value of the first integer it contains (in ASCII). */
{
    ifstream ifs;
    FS::OpenReadOnly(dirName + "/version", /*OUT*/ ifs);
    int res;
    ifs >> res;
    assert(ifs.good());
    FS::Close(ifs);
    return res;
}

void CacheS::ResetWeededMPKs() throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    // reset the log recording which MPKs have been weeded
    this->nextMPKToWeed = 0;
    assert(this->weededMPKsLog != (VestaLog *)NULL);
    try {
	fstream *chkptFile = weededMPKsLog->checkpointBegin();
	// empty checkpoint; just start new log
	FS::Close(*chkptFile);
	this->weededMPKsLog->checkpointEnd();
	this->weededMPKsLog->prune(/*ckpkeep=*/ 1);
    } catch (const FS::Failure &f) {
	cerr << "Fatal FS error: ";
	cerr << "while checkpointing \"weededMPKsLog\"; exiting..." << endl;
	cerr << f << endl;
	exit(1);
    }
    catch (const VestaLog::Error &err) {
	cerr << "Fatal VestaLog error: ";
	cerr << "reseting \"weededMPKsLog\"" << endl
	     << "  " << err.msg;
	if(err.r != 0)
	  {
	    cerr << " (errno = " << err.r << ")";
	  }
	cerr << endl << "Exiting..." << endl;
	exit(1);
    }
}

void CacheS::AddToWeededMPKs(int num) throw ()
/* REQUIRES Sup(LL) = SELF.mu /\ weededMPKsLog is in ``ready'' state*/
{
    try {
	this->weededMPKsLog->start();
	this->weededMPKsLog->write((char *)(&num), sizeof(num));
	this->weededMPKsLog->commit();
    }
    catch (const VestaLog::Error &err) {
	cerr << "Fatal VestaLog error: ";
	cerr << "adding entry to \"weededMPKsLog\"" << endl
	     << "  " << err.msg;
	if(err.r != 0)
	  {
	    cerr << " (errno = " << err.r << ")";
	  }
	cerr << endl << "Exiting..." << endl;
	exit(1);
    }
}

void CacheS::RecoverWeededMPKs() throw (VestaLog::Eof, VestaLog::Error)
/* REQUIRES LL = Empty */
{
    this->mu.lock();
    try {
      // open log and initialize volatile values
      this->weededMPKsLog = NEW(VestaLog);
      this->nextMPKToWeed = 0;
      this->weededMPKsLog->open(Config_WeededLogPath.chars());

      // recover log
      do {
	while (!this->weededMPKsLog->eof()) {
	  int delta;
	  this->weededMPKsLog->readAll((char *)(&delta), sizeof(delta));
	  this->nextMPKToWeed += delta;
	}
      } while (this->weededMPKsLog->nextLog());
      this->weededMPKsLog->loggingBegin();
    } catch (...) { this->mu.lock(); throw; }
    this->mu.unlock();

    // debugging
    if (this->debug >= CacheIntf::LogRecover) {
	this->mu.lock(); // required to protect read of "nextMPKToWeed"
	try {
	  ostream& out_stream = cio().start_out();
	  try {
	    out_stream << Debug::Timestamp() << "RECOVERED -- WeededLog" << endl;
	    if (this->nextMPKToWeed > 0) {
	      out_stream << "  Weeded MultiPKFiles = " << this->nextMPKToWeed;
	    } else {
	      out_stream << "  <<Empty>>";
	    }
	    out_stream << endl << endl;
	  } catch (...) { cio().end_out(); throw; }
	  cio().end_out();
	} catch (...) { this->mu.unlock(); throw; }
	this->mu.unlock();
    }
}

// CacheLog methods ---------------------------------------------------------

void CacheS::LogCacheEntry(const Text& sourceFunc, FP::Tag *pk,
			   PKFile::Epoch pkEpoch,
			   CacheEntry::Index ci, VestaVal::T *value,
			   Model::T model, CacheEntry::Indices *kids,
			   FV::List *names, FP::List *fps)
  throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    CacheLog::Entry *logEntry;

    // get new entry and fill it in
    if (this->vCacheAvail == (CacheLog::Entry *)NULL) {
      logEntry = NEW_CONSTR(CacheLog::Entry, (sourceFunc, pk, pkEpoch,
					      ci, value, model, kids,
					      names, fps));
    } else {
	logEntry = this->vCacheAvail;
	this->vCacheAvail = logEntry->next;
	// Note: Init resets next to NULL.
	logEntry->Init(sourceFunc, pk, pkEpoch,
		       ci, value, model, kids, names, fps);
    }

    // append new entry to end of list
    if (this->vCacheLogTail == (CacheLog::Entry *)NULL)
	this->vCacheLog = logEntry;
    else
	this->vCacheLogTail->next = logEntry;
    this->vCacheLogTail = logEntry;
}

void CacheS::FlushCacheLog() throw (VestaLog::Error)
/* REQUIRES Sup(LL) < SELF.ciLogMu */
{
    CacheLog::Entry *vLog, *curr;

    // capture "vCacheLog" in "vLog" local and reset "vCacheLog"
    this->mu.lock();
    vLog = this->vCacheLog;
    this->vCacheLog = this->vCacheLogTail = (CacheLog::Entry *)NULL;
    this->cacheLogFlushQ->Enqueue();
    this->mu.unlock();

    // flush lower-level logs
    this->FlushGraphLog();

    // make deriveds stable??
    /*** NYI ***/

    // return if there is nothing to flush
    if (vLog == (CacheLog::Entry *)NULL)
      {
	this->mu.lock();
	this->cacheLogFlushQ->Dequeue();
	this->mu.unlock();
	return;
      }

    // debugging start
    if (this->debug >= CacheIntf::LogFlush) {
      cio().start_out() << Debug::Timestamp() 
			<< "STARTED -- Flushing CacheLog" << endl << endl;
      cio().end_out();
    }

    // flush log
    CacheLog::Entry *last;
    int numFlushed = 0;
    this->cacheLogMu.lock();
    try {
        this->cacheLog->start();
        for (curr = vLog; curr != NULL; curr = curr->next) {
	    last = curr;
            if (numFlushed % 10 == 0 && numFlushed > 0) {
                /* Commit after every tenth log entry so the cache log
                   does not get very big between commit points. */
                this->cacheLog->commit();
                this->cacheLog->start();
            }
            curr->Log(*(this->cacheLog));
            numFlushed++;
            if (this->debug >= CacheIntf::LogFlushEntries) {
	      curr->Debug(cio().start_out());
	      cio().end_out();
	    }
	    curr->Reset();
	    // DrdReuse((caddr_t)curr, sizeof(*curr));
        }
        this->cacheLog->commit();
    
        // clean the cache log if it is too long
        this->cacheLogLen += numFlushed;
        this->TryCleanCacheLog(Config_MaxCacheLogCnt,
          /*reason=*/ "CacheS::FlushCacheLog called");
    } catch (...) { this->cacheLogMu.unlock(); throw; }
    this->cacheLogMu.unlock();

    // debugging end
    if (this->debug >= CacheIntf::LogFlushEntries) {
      cio().start_out() << endl;
      cio().end_out();
    }
    if (this->debug >= CacheIntf::LogFlush) {
      cio().start_out() << Debug::Timestamp() 
			<< "FINISHED -- Flushing CacheLog" << endl
			<< "  Entries flushed = " << numFlushed << endl << endl;
      cio().end_out();
    }
    
    // return "vLog" entries to avail list
    this->mu.lock();
    last->next = this->vCacheAvail;
    this->vCacheAvail = vLog;
    this->cacheLogFlushQ->Dequeue();
    this->mu.unlock();
} // CacheS::FlushCacheLog

void CacheS::CleanCacheLogEntries(RecoveryReader &rd, fstream &ofs,
  /*INOUT*/ EmptyPKLog::PKEpochTbl &pkEpochTbl,
  /*INOUT*/ int &oldCnt, /*INOUT*/ int &newCnt)
  throw (VestaLog::Error, VestaLog::Eof, FS::Failure)
/* REQUIRES Sup(LL) < SELF.cacheLogMu */
{
    while (!rd.eof()) {
	// read entry
	CacheLog::Entry logEntry(rd);
	FP::Tag *pk = logEntry.pk;          // alias for "logEntry.pk"
	PKFile::Epoch pkEpoch;
	oldCnt++;
	this->mu.lock();
	try {
	    // find the pkEpoch of the on-disk SPKFile
	    if (!pkEpochTbl.Get(*pk, /*OUT*/ pkEpoch)) {
		// if not in the table, read the epoch from disk
		pkEpoch = PKFileEpoch(*pk);
		bool inTbl = pkEpochTbl.Put(*pk, pkEpoch); assert(!inTbl);
	    }

	    // if no stable PKFile, consult the emptyPKEpochTbl
	    if (pkEpoch == 0) {
	        (void) this->emptyPKLog->GetEpoch0(*pk, /*OUT*/ pkEpoch);
	    }

	    // copy the entry if its "pkEpoch" is large enough
	    if (logEntry.pkEpoch >= pkEpoch) {
		logEntry.Write(ofs);
		newCnt++;
	    }
	} catch (...) { this->mu.unlock(); throw; }
	this->mu.unlock();
    }
} // CacheS::CleanCacheLogEntries

void CacheS::CleanCacheLog()
  throw (VestaLog::Error, VestaLog::Eof, FS::Failure)
/* REQUIRES Sup(LL) < SELF.cacheLogMu */
{
    // number of entries in old and new log (for debugging output only)
    int oldCnt = 0, newCnt = 0;

    // debugging start
    if (this->debug >= CacheIntf::LogFlush) {
      cio().start_out() << Debug::Timestamp() 
			<< "STARTED -- CleanCacheLog" << endl << endl;
      cio().end_out();
    }

    // checkpoint the current "cacheLog" and "emptyPKLog"
    /* Note: The "CheckpointBegin method of the emptyPKLog begins an empty
       checkpoint, and moves all old in-memory entries to a staging area 
       from which  they will be deleted when the checkpoint commits. */
    this->emptyPKLog->CheckpointBegin();
    fstream *cacheLogChkpt;
    int newLogVersion;
    this->cacheLogMu.lock();
    try {
	cacheLogChkpt = this->cacheLog->checkpointBegin();
	newLogVersion = this->cacheLog->logVersion();
    } catch (...) { this->cacheLogMu.unlock(); throw; }
    this->cacheLogMu.unlock();

    /* Next, we open a new read-only "VestaLog" on the current cache log.
       Since this log is local and will only be read by this thread,
       operations on it do not need to be protected by a lock. */
    RecoveryReader *rd;
    VestaLogSeq currCacheLogSeq(Config_CacheLogPath.chars());
    currCacheLogSeq.Open(/*startVer=*/ -1, /*readonly=*/ true);

    /* We keep a temporary table mapping PKs of existing SPKFiles to their
       corresponding "pkEpoch"s so that we do not have to go to disk more
       than once for each PK. This is a simple optimization to speed the
       process of cleaning the cache log. The table is discarded once the
       cacheLog has been cleaned. */ 
    EmptyPKLog::PKEpochTbl pkEpochTbl;

    // read the last valid checkpoint and log(s), copying to the checkpoint
    try {
	while ((rd = currCacheLogSeq.Next(newLogVersion)) != NULL) {
	    this->CleanCacheLogEntries(*rd, *cacheLogChkpt,
              /*INOUT*/ pkEpochTbl, /*INOUT*/ oldCnt, /*INOUT*/ newCnt);
	}
    } catch (...) { currCacheLogSeq.Close(); throw; }
    currCacheLogSeq.Close();

    // close the checkpoint file being written
    FS::Close(*cacheLogChkpt);

    // commit the checkpoints in the proper order
    this->cacheLogMu.lock();
    try {
	this->cacheLog->checkpointEnd();
	this->cacheLog->prune(/*ckpkeep=*/ 1);
	this->emptyPKLog->CheckpointEnd();
    } catch (...) { this->cacheLogMu.unlock(); throw; }
    this->cacheLogMu.unlock();

    // debugging end
    if (this->debug >= CacheIntf::LogFlush) {
      cio().start_out() << Debug::Timestamp() 
			<< "FINISHED -- CleanCacheLog" << endl
			<< "  old log size = " << oldCnt << endl
			<< "  new log size = " << newCnt << endl << endl;
      cio().end_out();
    }
} // CacheS::CleanCacheLog

void* CacheS_CleanCacheLogWorker(void *arg) throw ()
/* REQUIRES Empty(LL) */
/* This is the apply function for a thread that is forked to clean
   the CacheLog. */
{
    CleanWorker *cw = (CleanWorker *)arg;
    try {
	while (true) {
	    // wait for work to do
	    cw->mu.lock();
	    while (!(cw->argsReady)) {
		cw->workToDo.wait(cw->mu);
	    }
	    cw->mu.unlock();

	    // do the work
	    cw->cs->CleanCacheLog();

	    // restore thread to avail list
	    cw->Finish();
	    cw->cs->RegisterIdleCleanWorker(cw);
	}
    }
    catch (const VestaLog::Error &err) {
	cerr << "VestaLog fatal error: ";
        cerr << "failed cleaning cache log; exiting..." << endl;
	cerr << "num = " << err.r << "; " << err.msg << endl;
	exit(1);
    }
    catch (VestaLog::Eof) {
	cerr << "VestaLog fatal error: ";
        cerr << "unexpected EOF cleaning cache log; exiting..." << endl;
	exit(1);
    }
    catch (const FS::Failure &f) {
        cerr << "FS fatal error: ";
	cerr << "unexpected FS error cleaning cache log; exiting..." << endl;
	cerr << f;
	exit(1);
    }
    // make compiler happy
    assert(false);
    return (void *)NULL;
} // CacheS_CleanCacheLogWorker

CleanWorker *CacheS::NewCleanWorker() throw ()
/* REQUIRES Sup(LL) = SELF.cacheLogMu */
{
    // only allow one clean worker at a time
    while (this->idleCleanWorker == NULL) {
	this->availCleanWorker.wait(this->cacheLogMu);
    }
    CleanWorker *res = this->idleCleanWorker;
    this->idleCleanWorker = NULL;
    return res;
}

void CacheS::RegisterIdleCleanWorker(CleanWorker *cw) throw ()
/* REQUIRES Sup(LL) < SELF.cacheLogMu */
{
    this->cacheLogMu.lock();
    assert(this->idleCleanWorker == NULL); // only one clean worker
    this->idleCleanWorker = cw;
    this->cacheLogMu.unlock();
    this->availCleanWorker.signal();
}

void CacheS::TryCleanCacheLog(int upper_bound, const char *reason) throw ()
/* REQUIRES Sup(LL) = SELF.cacheLogMu */
{
    // if log is too long, fork a thread to clean it
    if (this->cacheLogLen > upper_bound) {
	CleanWorker *worker = this->NewCleanWorker();
	this->cacheLogLen = 0;
	worker->Start(reason);
    }
}

void CacheS::RecoverEmptyPKLog(/*INOUT*/ bool &empty)
  throw (VestaLog::Error, VestaLog::Eof)
/* REQUIRES Sup(LL) = SELF.mu */
{
    while (!this->emptyPKLog->EndOfFile()) {
	// read the value from the log
	FP::Tag pk; PKFile::Epoch logPKEpoch;
	this->emptyPKLog->Read(/*OUT*/ pk, /*OUT*/ logPKEpoch);
	assert(logPKEpoch > 0);

	// print it (if necessary)
	if (this->debug >= CacheIntf::LogRecover) {
	  cio().start_out() << "  pk = " << pk
			    << ", pkEpoch = " << logPKEpoch << endl;
	  empty = false;
	  cio().end_out();
	}
    }
} // CacheS::RecoverEmptyPKLog

void CacheS::RecoverCacheLogEntries(RecoveryReader &rd,
  /*INOUT*/ EmptyPKLog::PKEpochTbl &pkEpochTbl, /*INOUT*/ bool &empty)
  throw (VestaLog::Error, VestaLog::Eof)
/* REQUIRES Sup(LL) = SELF.cacheLogMu */
{
    while (!rd.eof()) {
	// read entry
	CacheLog::Entry logEntry(rd);
	this->cacheLogLen++;

	// Note: you might think that it would be OK to supress replay
	// of entries on the list of pending deletions, but you's be
	// wrong.  This causes subtle bugs due to the way pkEpoch gets
	// updated and how entries get purged from the cache log.

	/* We only need to read in those cache entries that have
           not been flushed to stable PK files. We can tell if an
           entry is stale by comparing its "pkEpoch" to that of
           the epoch in the header of the stable PK file, and if
           necessary, in the emptyPKLog. Stale entries are ignored. */
	FP::Tag *pk = logEntry.pk;          // alias for "logEntry.pk"
        PKFile::Epoch pkEpoch; // epoch in stable cache or emptyPKLog
	this->mu.lock();
	try {
            /* Implementation note: We first consult the stable PKFile,
               and *then* the emptyPKLog (but only if no stable PKFile
               exists for this PK). This order is important because the
               emptyPKLog is committed before the SMultiPKFile is committed.
               If the cache server crashes after the former has been
               committed, but before the latter one has, there will be an
	       entry in the emptyPKLog for a PKFile that was actually not
	       deleted. So if a PKFile exists, we ignored the emptyPKLog. */

	    // find the pkEpoch of the on-disk SPKFile
	    if (!pkEpochTbl.Get(*pk, /*OUT*/ pkEpoch)) {
		// if not in table, try reading epoch from disk
		pkEpoch = this->PKFileEpoch(*pk);
		bool inTbl = pkEpochTbl.Put(*pk, pkEpoch); assert(!inTbl);
	    }

	    // if there is no stable PKFile, consult the emptyPKLog
	    if (pkEpoch == 0) {
		(void) this->emptyPKLog->GetEpoch0(*pk, /*OUT*/ pkEpoch);
	    }
	} catch (...) { this->mu.unlock(); throw; }
	this->mu.unlock();

	// process the entry only if its "pkEpoch" is large enough
	if (logEntry.pkEpoch >= pkEpoch) {
	    // read header of stable PK file
	    VPKFile *vf;
	    this->mu.lock();

 	    // Make sure that the CI of this entry is in the used set.
 	    // Recovering a cache log entry for an unused CI indicates
 	    // a bug.
 	    if(!this->usedCIs.Read(logEntry.ci))
 	      {
		cio().start_err() << Debug::Timestamp()
 		     << "INTERNAL ERROR: unused CI in cache log:" << endl
 		     << "  pk           = " << *pk << endl
 		     << "  ci           = " << logEntry.ci << endl
 		     << " (Please report this as a bug.)" << endl;
		cio().end_err(/*aborting*/true);
 		abort();
 	      }

	    (void)(this->GetVPKFile(*pk, /*OUT*/ vf));
	    this->state.newEntryCnt++;
	    this->state.newPklSize += logEntry.value->len;
	    int currentFreeEpoch = this->freeMPKFileEpoch;
	    this->mu.unlock();

	    // release lock on cacheLog to read "vf"
	    this->cacheLogMu.unlock();
	    try {
		// add entry to cache
		vf->mu.lock();
		// Note that this PKFile has been accessed.
		vf->UpdateFreeEpoch(currentFreeEpoch);
		try {
		  FP::Tag *commonFP;
		  CE::T *entry = vf->NewEntry(logEntry.ci,
					      logEntry.names, logEntry.fps,
					      logEntry.value,
					      logEntry.model, logEntry.kids,
					      /*OUT*/ commonFP);

		  vf->AddEntry(NEW_CONSTR(Text, (logEntry.sourceFunc)), entry,
			       commonFP, logEntry.pkEpoch);
		} catch (...) { vf->mu.unlock(); throw; }
		vf->mu.unlock();

		// flush MPKFile if necessary
		PKPrefix::T pfx(*pk);
		this->AddEntryToMPKFile(pfx,
					"MultiPKFile threshold exceeded during recovery",
					true);
	    // re-acquire "cacheLogMu"
	    } catch (...) { this->cacheLogMu.lock(); throw; }
	    this->cacheLogMu.lock();

	    // debugging output
	    if (this->debug >= CacheIntf::LogRecover) {
	      logEntry.Debug(cio().start_out());
	      empty = false;
	      cio().end_out();
	    }
	} // if
    } // while
} // CacheS::RecoverCacheLogEntries

void CacheS::RecoverCacheLog()
  throw (VestaLog::Error, VestaLog::Eof, FS::Failure)
/* REQUIRES LL = Empty */
{
    // debugging start
    bool empty = true; // used only for debugging output
    if (this->debug >= CacheIntf::LogRecover) {
      cio().start_out() << Debug::Timestamp() << "RECOVERING -- EmptyPKLog" << endl;
      cio().end_out();
    }

    // read EmptyPKLog into "this->emptyPKLog"'s internal pkEpoch table
    this->mu.lock();
    try {
	/* Note: The "EmptyPKLog" always has an empty checkpoint, so we
           don't have to call the log's "openCheckpoint" method to read
           it. We simply recover from the log files. */
	do {
	    this->RecoverEmptyPKLog(/*INOUT*/ empty);
        } while (this->emptyPKLog->NextLog());
	this->emptyPKLog->EndRecovery();
    } catch (...) { this->mu.unlock(); throw; }
    this->mu.unlock();

    // debugging end / start
    if (this->debug >= CacheIntf::LogRecover) {
      ostream& out_stream = cio().start_out();
      if (empty) out_stream << "  <<Empty>>" << endl;
      out_stream << endl;
      out_stream << Debug::Timestamp() << "RECOVERING -- CacheLog" << endl;
      cio().end_out();
    }

    // open log and initialize volatile values
    this->mu.lock();
    this->cacheLog = NEW(VestaLog);
    this->cacheLogFlushQ = NEW_CONSTR(FlushQueue, (&(this->mu)));
    this->vCacheLog = this->vCacheAvail = (CacheLog::Entry *)NULL;
    this->vCacheLogTail = (CacheLog::Entry *)NULL;
    this->mu.unlock();

    /* We keep a temporary table mapping PKs of existing SPKFiles to their
       corresponding "pkEpoch"s so that we do not have to go to disk more
       than once for each PK. This is a simple optimization to speed recovery.
       The table is discarded once the cacheLog has been recovered. */
    EmptyPKLog::PKEpochTbl pkEpochTbl;

    // open the log file
    empty = true;
    this->cacheLogMu.lock();
    try {
	this->cacheLog->open(Config_CacheLogPath.chars());

        // recover from the checkpoint (if any)
	fstream *chkpt = this->cacheLog->openCheckpoint();
	if (chkpt != (fstream *)NULL) {
	    RecoveryReader rd(chkpt);
	    this->RecoverCacheLogEntries(rd,
              /*INOUT*/ pkEpochTbl, /*INOUT*/ empty);
	    FS::Close(*chkpt);
	}

	// recover from log files
	do {
	    RecoveryReader rd(this->cacheLog);
	    this->RecoverCacheLogEntries(rd,
              /*INOUT*/ pkEpochTbl, /*INOUT*/ empty);
	} while (this->cacheLog->nextLog());
	this->cacheLog->loggingBegin(); // switch log to ``ready'' state

    } catch (...) { this->cacheLogMu.unlock(); throw; }
    this->cacheLogMu.unlock();

    // debugging end
    if (this->debug >= CacheIntf::LogRecover) {
      ostream& out_stream = cio().start_out();
      if (empty) out_stream << "  <<Empty>>" << endl;
      out_stream << endl;
      cio().end_out();
    }
} // CacheS::RecoverCacheLog

// GraphLog methods ---------------------------------------------------------

void CacheS::LogGraphNode(CacheEntry::Index ci, FP::Tag *loc, Model::T model,
  CacheEntry::Indices *kids, Derived::Indices *refs) throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    GraphLog::Node *logNode;

    // get new entry and fill it in
    if (this->vGraphNodeAvail == (GraphLog::Node *)NULL) {
      logNode = NEW_CONSTR(GraphLog::Node, (ci, loc, model, kids, refs));
    } else {
	logNode = this->vGraphNodeAvail;
	assert(logNode->kind == GraphLog::NodeKind);
	this->vGraphNodeAvail = logNode->next;
	this->vGraphNodeAvailLen--;
	logNode->Init(ci, loc, model, kids, refs);
    }

    // append new entry to end of list
    if (this->vGraphLogTail == (GraphLog::Node *)NULL)
	this->vGraphLog = logNode;
    else
	this->vGraphLogTail->next = logNode;
    this->vGraphLogTail = logNode;
}

void CacheS::FlushGraphLog() throw (VestaLog::Error)
/* REQUIRES Sup(LL) < SELF.ciLogMu */
{
    GraphLog::Node *vLog;

    // capture "vGraphLog" in "vLog" local
    this->mu.lock();
    vLog = this->vGraphLog;
    this->vGraphLog = this->vGraphLogTail = (GraphLog::Node *)NULL;
    this->graphLogFlushQ->Enqueue();
    this->mu.unlock();

    // flush lower-level logs
    this->FlushUsedCIs();

    // return if there is nothing to flush
    if (vLog == (GraphLog::Node *)NULL)
      {
	this->mu.lock();
	this->graphLogFlushQ->Dequeue();
	this->mu.unlock();
	return;
      }

    // debugging start
    if (this->debug >= CacheIntf::LogFlush) {
      cio().start_out() << Debug::Timestamp() 
			<< "STARTED -- Flushing GraphLog" << endl << endl;
      cio().end_out();
    }

    // flush log
    int numFlushed = 0;
    GraphLog::Node *last;
    this->graphLogMu.lock();
    try {
	this->graphLog->start();
	for (GraphLog::Node *curr = vLog; curr != NULL; curr = curr->next) {
	    last = curr;
	    if (numFlushed % 20 == 0 && numFlushed > 0) {
		/* Commit after every 20th log entry so the graph log
		   does not get very big between commit points. */
		this->graphLog->commit();
		this->graphLog->start();
	    }
	    curr->Log(*(this->graphLog));
	    numFlushed++;
	    if (this->debug >= CacheIntf::LogFlushEntries) {
	      curr->Debug(cio().start_out());
	      cio().end_out();
	    }
	    curr->Reset();
	    // DrdReuse((caddr_t)curr, sizeof(*curr));
	}
	this->graphLog->commit();
    } catch (...) { this->graphLogMu.unlock(); throw; }
    this->graphLogMu.unlock();

    // debugging end
    if (this->debug >= CacheIntf::LogFlushEntries) {
      cio().start_out() << endl;
      cio().end_out();
    }
    if (this->debug >= CacheIntf::LogFlush) {
      cio().start_out() << Debug::Timestamp() 
			<< "FINISHED -- Flushing GraphLog" << endl
			<< "  Nodes flushed = " << numFlushed << endl << endl;
      cio().end_out();
    }

    // return "vLog" entries to avail list
    this->mu.lock();
    last->next = this->vGraphNodeAvail;
    this->vGraphNodeAvail = vLog;
    this->vGraphNodeAvailLen += numFlushed;
    this->graphLogFlushQ->Dequeue();
    this->mu.unlock();
}

int CacheS::ChkptGraphLog() throw (FS::Failure, VestaLog::Error)
/* REQUIRES Sup(LL) < SELF.graphLogMu */
{
    int res;
    fstream *fs;
    this->graphLogMu.lock();
    try {
	fs = this->graphLog->checkpointBegin();
	FS::Close(*fs);
	res = this->graphLog->logVersion();
    } catch (...) { this->graphLogMu.unlock(); throw; }
    this->graphLogMu.unlock();
    return res;
}

void CacheS::AbortGraphLogChkpt() throw (VestaLog::Error)
/* REQUIRES Sup(LL) < SELF.graphLogMu */
{
    this->graphLogMu.lock();
    try {
	this->graphLog->checkpointAbort();
    } catch (...) { this->graphLogMu.unlock(); throw; }
    this->graphLogMu.unlock();
}

void CacheS::RecoverGraphLog()
  throw (VestaLog::Error, VestaLog::Eof, FS::Failure)
/* REQUIRES LL = Empty */
/* This procedure ignores the current log, since it was already written to
   disk. However, it must advance the log's file pointer to the end of the log
   so appending will work correctly. */
{
    // open log and initialize volatile values
    this->mu.lock();
    this->graphLog = NEW(VestaLog);
    this->graphLogFlushQ = NEW_CONSTR(FlushQueue, (&(this->mu)));
    this->vGraphLog = this->vGraphNodeAvail = (GraphLog::Node *)NULL;
    this->vGraphLogTail = (GraphLog::Node *)NULL;
    this->vGraphNodeAvailLen = 0;
    this->mu.unlock();

    this->graphLogMu.lock();
    try {
	// open the log file
	this->graphLog->open(Config_GraphLogPath.chars());

	/* Note: we do not even bother opening & reading the checkpoint
           file, since the cache server never *reads* the graphLog, it
           just appends to it. */

	// read log until EOF
	char c;
	do {
	    while (!this->graphLog->eof()) this->graphLog->get(c);
	} while (this->graphLog->nextLog());

        // recover the checkpoint if one was in progress
        fstream *ofs = this->graphLog->checkpointResume(/*mode=*/ ios::out);
        if (ofs != (fstream *)NULL) {
	    FS::Close(*ofs);
	    this->mu.lock();
	    this->graphLogChkptVer = this->graphLog->logVersion();
	    this->mu.unlock();
        }

	// begin logging
	this->graphLog->loggingBegin();
    } catch (...) { this->graphLogMu.unlock(); throw; }
    this->graphLogMu.unlock();
} // CacheS::RecoverGraphLog

// UsedCI methods -----------------------------------------------------------

void CacheS::LogCI(Intvl::Op op, CacheEntry::Index ci) throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    if (this->vCILog != (Intvl::List *)NULL && this->vCILog->i.op == op
        && this->vCILog->i.hi + 1 == ci) {
	this->vCILog->i.hi = ci;
    } else {
	// set "il" to new "Intvl::List"
	Intvl::List *il;
	if (this->vCIAvail == (Intvl::List *)NULL) {
	  il = NEW(Intvl::List);
	} else {
	    il = this->vCIAvail;
	    this->vCIAvail = il->next;
	}

	// fill it in and prepend it to "vCILog"
	il->i.op = op;
	il->i.lo = il->i.hi = ci;
	il->next = this->vCILog;
	this->vCILog = il;
    }
}

void CacheS::FlushUsedCIs() throw (VestaLog::Error)
/* REQUIRES Sup(LL) < SELF.ciLogMu */
{
    Intvl::List *vLog, *curr, *last;

    // capture "vCILog" in "vLog" local and reset "vCILog"
    this->mu.lock();
    vLog = this->vCILog;
    this->vCILog = (Intvl::List *)NULL;
    this->ciLogFlushQ->Enqueue();
    this->mu.unlock();

    if (vLog == (Intvl::List *)NULL)
      {
	this->mu.lock();
	this->ciLogFlushQ->Dequeue();
	this->mu.unlock();

	return;
      }

    // debugging start
    if (this->debug >= CacheIntf::LogFlush) {
      cio().start_out() << Debug::Timestamp() 
			<< "STARTED -- Flushing Used CI's Log" << endl << endl;
      cio().end_out();
    }

    // flush the entries in "vLog"
    int numFlushed;
    this->ciLogMu.lock();
    try {
	last = this->FlushUsedCIsList(vLog, /*OUT*/ numFlushed);
    } catch (...) { this->ciLogMu.unlock(); throw; }
    ciLogMu.unlock();

    // debugging end
    if (this->debug >= CacheIntf::LogFlush) {
      cio().start_out() << Debug::Timestamp() 
			<< "FINISHED -- Flushing Used CI's Log" << endl
			<< "  Intervals flushed = " << numFlushed << endl << endl;
      cio().end_out();
    }

    // return "vLog" entries to avail list
    this->mu.lock();
    last->next = this->vCIAvail;
    this->vCIAvail = vLog;
    this->ciLogFlushQ->Dequeue();
    this->mu.unlock();
}

Intvl::List *CacheS::FlushUsedCIsList(Intvl::List *vLog,
  /*OUT*/ int &numFlushed) throw (VestaLog::Error)
/* REQUIRES SELF.ciLogMu IN LL */
{
    // flush log
    assert(vLog != (Intvl::List *)NULL);
    Intvl::List *curr, *last;
    numFlushed = 0;
    this->ciLog->start();
    for (curr = vLog; curr != (Intvl::List *)NULL; curr = curr->next) {
	last = curr;
	curr->i.Log(*(this->ciLog));
	numFlushed++;
	if (this->debug >= CacheIntf::LogFlushEntries) {
	  curr->i.Debug(cio().start_out());
	  cio().end_out();
	}
	// DrdReuse((caddr_t)curr, sizeof(*curr));
    }
    this->ciLog->commit();
    if (this->debug >= CacheIntf::LogFlushEntries) {
      cio().start_out() << endl;
      cio().end_out();
    }
    return last;
}

void CacheS::ChkptUsedCIs(const BitVector &del)
  throw (VestaLog::Error, FS::Failure)
/* REQUIRES Sup(LL) < SELF.ciLogMu */
/* NOTE: The locking in this method is a bit tricky. To guarantee that the
   checkpoint contains the exact set of CIs in use after subtracting off the
   entries "del", we have to hold the central lock "SELF.mu" while the CILog
   is flushed, while the entries "del" are subtracted from "this->usedCIs",
   and while a copy of that bit vector is made. This prevents any new
   entries from being created during that interval. Since the lock that
   protects the CILog ("SELF.ciLogMu") precedes "SELF.mu" in locking order,
   we have to first acquire that lock before acquiring "SELF.mu".

   So as not to hold "SELF.mu" for very long, we first flush the CILog using
   the normal "FlushUsedCIs" method, which only holds "SELF.mu" long enough to
   get a local copy of the pointer to the head of the (in-memory) list of
   CILog entries. Once the log has been flushed in this way, the hope is that
   it will be nearly empty when the log is flushed with "SELF.mu" held, so
   that central lock will not be held for very long. We then flush the log,
   subtract off the entries "del" and make a copy of "this->usedCIs", all
   with "SELF.mu" held. When the new CILog checkpoint is created, only
   "SELF.ciLogMu" needs to be held, since holding that lock will prevent
   other threads from writing to the CILog. */
{
    BitVector *usedCIsCopy;  // copy of "this->usedCIs" after subtracting "del"
    fstream *chkptFS;        // file to which checkpoint is written

    // first, flush the "usedCIs" log
    this->FlushUsedCIs();

    // debugging start
    if (this->debug >= CacheIntf::LogFlush) {
      cio().start_out() << Debug::Timestamp()
			<< "STARTED -- Flushing Used CI's Log" << endl << endl;
      cio().end_out();
    }

    // now checkpoint the new "usedCIs" value w/ appropriate locks
    int numFlushed;
    this->ciLogMu.lock();
    try {
        this->mu.lock();
        try {

        // see if there are any more entries to flush
        // this code is a lot like "FlushUsedCIs", but with diff locking
        Intvl::List *vLog = this->vCILog;
	this->vCILog = (Intvl::List *)NULL;
	if (vLog != (Intvl::List *)NULL) {
	    // flush any remaining "usedCIs", but with "this->mu" held
	    Intvl::List *last = this->FlushUsedCIsList(vLog,/*OUT*/numFlushed);

	    // return "vLog" entries to avail list
	    last->next = this->vCIAvail;
	    this->vCIAvail = vLog;
	}

	// subtract off the entries in "del"
	this->usedCIs -= del;
	this->entryCnt = this->usedCIs.Cardinality();

	// make a copy of "this->usedCIs" (with "this->mu" held)
	usedCIsCopy = NEW_CONSTR(BitVector, (&(this->usedCIs)));

        } catch (...) { this->mu.unlock(); this->ciLogMu.unlock(); throw; }
        this->mu.unlock();

        // start a new checkpoint
        chkptFS = this->ciLog->checkpointBegin();
    } catch (...) { this->ciLogMu.unlock(); throw; }
    this->ciLogMu.unlock();

    // debugging end
    if (this->debug >= CacheIntf::LogFlush) {
      cio().start_out() << Debug::Timestamp()
			<< "FINISHED -- Flushing Used CI's Log" << endl
			<< "  Intervals flushed = " << numFlushed << endl << endl;
      cio().end_out();
    }

    // write "usedCIsCopy" to checkpoint file
    try {
	usedCIsCopy->Write(*chkptFS); // may be long-running operation
	FS::Close(*chkptFS);
    } catch (...) {
	// in event of any failure while writing checkpoint, abort it
	this->ciLogMu.lock();
	try {
	    this->ciLog->checkpointAbort();
	} catch (...) { this->ciLogMu.unlock(); throw; }
	this->ciLogMu.unlock();
	throw;
    }

    // commit the checkpoint and prune the log
    this->ciLogMu.lock();
    try {
	this->ciLog->checkpointEnd();
	this->ciLog->prune(/*ckpkeep=*/ 1);
    } catch (...) { this->ciLogMu.unlock(); throw; }
    this->ciLogMu.unlock();
}

void CacheS::RecoverCILog() throw (VestaLog::Error, VestaLog::Eof, FS::Failure)
/* REQUIRES LL = Empty */
{
    // debugging start
    bool empty;		     // only set if "debug >= CacheIntf::LogRecover"
    if (this->debug >= CacheIntf::LogRecover) {
      cio().start_out() << Debug::Timestamp() 
		 << "RECOVERING -- Used CI's Log" << endl;
      empty = true;
      cio().end_out();      
    }

    // open log and initialize volatile values
    this->mu.lock();
    this->ciLog = NEW(VestaLog);
    this->ciLogFlushQ = NEW_CONSTR(FlushQueue, (&(this->mu)));
    this->usedCIs.ResetAll();
    this->entryCnt = 0;
    this->vCILog = this->vCIAvail = (Intvl::List *)NULL;
    this->mu.unlock();

    // open log and recover checkpoint (if any)
    fstream *chkpt = (fstream *)NULL;
    this->ciLogMu.lock();
    try {
	// open the log file
	this->ciLog->open(Config_CILogPath.chars());

	// recover from checkpoint
	chkpt = this->ciLog->openCheckpoint();
	if (chkpt != (fstream *)NULL) {
	    RecoveryReader rd(chkpt);
	    this->mu.lock();
	    try {
		this->usedCIs.Recover(rd);
	    } catch (...) { this->mu.unlock(); throw; }
	    this->mu.unlock();
	    FS::Close(*chkpt);
	}
    } catch (...) { this->ciLogMu.unlock(); throw; }
    this->ciLogMu.unlock();

    // more debugging
    if (chkpt != (fstream *)NULL && this->debug >= CacheIntf::LogRecover) {
      cio().start_out() << "  usedCIs = " << usedCIs << endl;
      empty = false;
      cio().end_out();
    }

    // recover from log file(s)
    this->ciLogMu.lock();
    try {
	// recover from logs
	RecoveryReader rd(this->ciLog);
	do {
	    while (!rd.eof()) {
		Intvl::T intvl(rd);
		this->mu.lock();
		try {
		    assert(intvl.hi >= intvl.lo);
		    assert(intvl.lo >= 0);
		    this->usedCIs.WriteInterval(intvl.lo, intvl.hi,
		      (intvl.op == Intvl::Add));
		} catch (...) { this->mu.unlock(); throw; }
		this->mu.unlock();
		if (this->debug >= CacheIntf::LogRecover) {
		    intvl.Debug(cio().start_out());
		    cio().end_out();
		    empty = false;
		}
	    }
	} while (this->ciLog->nextLog());
	this->ciLog->loggingBegin();
    } catch (...) { this->ciLogMu.unlock(); throw; }
    this->ciLogMu.unlock();

    // update "this->entryCnt"
    this->mu.lock();
    this->entryCnt = this->usedCIs.Cardinality();
    this->mu.unlock();

    // debugging end
    if (this->debug >= CacheIntf::LogRecover) {
      ostream& out_stream = cio().start_out();
      if (empty) out_stream << "  <<Empty>>" << endl;
      out_stream << endl;
      cio().end_out();
    }
}

// CacheWorker ----------------------------------------------------------------

CacheWorker::CacheWorker(CacheS *cs) throw ()
  : cs(cs), reason("idle")
{
    // initially, the thread is not runable
    this->mu.lock();
    this->argsReady = false;
    this->mu.unlock();
}

void CacheWorker::Start(const char *reason) throw ()
{
    // signal the thread to go
    this->mu.lock();
    this->argsReady = true;
    this->reason = reason;
    this->mu.unlock();
    this->workToDo.signal();
}

void CacheWorker::Finish() throw ()
{
    this->mu.lock();
    this->argsReady = false;
    this->reason = "idle";
    this->mu.unlock();
}

void* CacheS_FlushWorker(void *arg) throw ();

FlushWorker::FlushWorker(CacheS *cs) throw ()
  : CacheWorker(cs)
{
    // fork the thread that does the actual work
    this->th.fork_and_detach(CacheS_FlushWorker, (void *)this);
}

void* CacheS_FlushWorker(void *arg) throw ()
/* REQUIRES LL = 0 */
{
    FlushWorker *fw = (FlushWorker *)arg;
    while (true) {
	// wait for work to do
	fw->mu.lock();
	while (!(fw->argsReady)) {
	    fw->workToDo.wait(fw->mu);
	}
	fw->mu.unlock();

	// do the work
	fw->cs->VToSCache(fw->pfx);

	// restore thread to avail list
	fw->Finish();
	fw->cs->RegisterIdleFlushWorker(fw);
    }

    //assert(false); // not reached
    //return (void *)NULL;
}

void FlushWorker::Start(const PKPrefix::T &pfx, const char *reason) throw ()
{
    // save argument
    this->pfx = pfx;

    // invoke "Start" method of supertype
    ((CacheWorker *)this)->Start(reason);
}

CleanWorker::CleanWorker(CacheS *cs) throw ()
  : CacheWorker(cs)
{
    // fork the thread that does the actual work
    this->th.fork_and_detach(CacheS_CleanCacheLogWorker, (void *)this);
}
