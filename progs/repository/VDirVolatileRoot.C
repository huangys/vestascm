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

//       
// VDirVolatileRoot.C
//
// Special one-of-a-kind pseudo-directory used to keep track of
// volatile directories that have been created.  Unlike other
// VestaSource objects, this one has real state, not just a short
// pointer into the sweepable memory pool.  The state is all volatile,
// except for a repository server restart counter.
//

#include <Table.H>
#include <Sequence.H>
#include "VDirVolatileRoot.H"
#include "VDirChangeable.H"
#include "VDirEvaluator.H"
#include "VMemPool.H"
#include "VestaLog.H"
#include "Recovery.H"
#include "VRConcurrency.H"
#include "IndexKey.H"
#include "logging.H"
#include "ShortIdRefCount.H"

#include "lock_timing.H"

#include <BufStream.H>

using std::endl;
using Basics::OBufStream;

class VolRootEntry {
public:
  // Head and tail of the linked list of all VolRootEntry objects.
  // Protected by VolatileRootLock.
  static VolRootEntry *head;
  static VolRootEntry *tail;

  // Links within the linked list.
  VolRootEntry *next, *prev;

  Bit32 srep;
  bool readOnlyExisting;
  time_t creationTime;
  ReadersWritersLock lock;
  ShortIdRefCount sidref;

  // The index of this entry within the volatile root.
  unsigned int index;

  FP::Tag fptag;

  // set to false upon SRPC error talking to evaluator
  bool alive;
  
  // The link/unlink methods link this object into and unlink this
  // object from the global linked list of all VolRootEntry objects.
  // Separated from the constructor/destructor to make it possible to
  // perform the memory allocation for one before acquiring
  // VolatileRootLock.
  void link()
  {
    // We shouldn't be part of the list when this method is called.
    assert(next == 0);
    assert(prev == 0);

    // We're now the tail of the list.
    prev = VolRootEntry::tail;
    VolRootEntry::tail = this;

    if(prev != 0)
      {
	// The list is non-empty, correct the next pointer.
	assert(VolRootEntry::head != 0);
	assert(prev->next == 0);
	prev->next = this;
      }
    else
      {
	// No tail, so the list must be empty.
	assert(VolRootEntry::head == 0);
	assert(prev == 0);
	VolRootEntry::head = this;
      }
  }
  void unlink()
  {
    // Splice ourselves out of the list.
    if(next != 0)
      {
	next->prev = prev;
      }
    if(prev != 0)
      {
	prev->next = next;
      }

    // If we were the head or tail of the list, correct those.
    if(VolRootEntry::head == this)
      {
	VolRootEntry::head = next;
      }
    if(VolRootEntry::tail == this)
      {
	VolRootEntry::tail = prev;
      }

    // Clear our pointers.
    next = 0;
    prev = 0;

    // When we're done, there must either be both a head and a tail or
    // neither.
    assert(((VolRootEntry::head != 0) && (VolRootEntry::tail != 0)) ||
	   ((VolRootEntry::head == 0) && (VolRootEntry::tail == 0)));
  }

  VolRootEntry(unsigned int myIndex)
    : lock(true), next(0), prev(0), index(myIndex),
      fptag((char *) &myIndex, sizeof(myIndex)), alive(true)
  {
  }

  ~VolRootEntry()
  {
    // We must be unlinked before being destroyed.
    assert(next == 0);
    assert(prev == 0);
    assert(VolRootEntry::head != this);
    assert(VolRootEntry::tail != this);
  }
};

VolRootEntry *VolRootEntry::head = 0;
VolRootEntry *VolRootEntry::tail = 0;

typedef Table<IndexKey, VolRootEntry*>::Default VolRootTable;
typedef Table<IndexKey, VolRootEntry*>::Iterator VolRootIter;

// Module globals.
// Protected by VolatileRootLock
static VolRootTable vrtTable;
static Basics::thread gardener;
#define INDEX_BLOCKSIZE 1024
#define GARDENER_SLEEP (60*60)

// nextIndex is protected by StableLock
static unsigned nextIndex = 0;

// Requires VolatileRootLock.read or better
// Does not acquire any lock on the result!
VestaSource::errorCode
VDirVolatileRoot::lookupIndex(unsigned int index, VestaSource*& result,
			      char* arcbuf) throw ()
{
  // Start by initializing the result, in case we fail before setting
  // it to something useful.
  result = 0;

    VolRootEntry* vre;
    
    // Find the directory in the table
    if (vrtTable.Get(index, vre) == 0) {
	return VestaSource::notFound;
    }

    result = NEW_CONSTR(VDirChangeable,
			(vre->readOnlyExisting ?
			 VestaSource::volatileROEDirectory :
			 VestaSource::volatileDirectory,
			 (Bit8*) VMemPool::lengthenPointer(vre->srep),
			 &(vre->sidref)));
    result->master = true;
    result->attribs = NULL;
    result->longid = longid.append(index);
    result->pseudoInode = indexToPseudoInode(index);
    result->ac = this->ac;
    result->VestaSource::fptag = vre->fptag;

    if (arcbuf) {
	sprintf(arcbuf, "%08x", index);
    }

    return VestaSource::ok;
}

// Requires VolatileRootLock.read or better.
// Acquires requested lock on the subtree.
VestaSource::errorCode
VDirVolatileRoot::lookupIndexAndLock(unsigned int index, VestaSource*& result,
				     LongId::lockKindTag lockKind,
				     ReadersWritersLock** lock) throw ()
{
  // Start by initializing the result, in case we fail before setting
  // it to something useful.
  result = 0;

    VolRootEntry* vre;
    
    // Find the directory in the table
    if (vrtTable.Get(index, vre) == 0) {
        if (lock && lockKind != LongId::checkLock) *lock = NULL;
	return VestaSource::notFound;
    }

    result = NEW_CONSTR(VDirChangeable,
			(vre->readOnlyExisting ?
			 VestaSource::volatileROEDirectory :
			 VestaSource::volatileDirectory,
			 (Bit8*) VMemPool::lengthenPointer(vre->srep),
			 &(vre->sidref)));
    result->master = true;
    result->attribs = NULL;
    result->longid = longid.append(index);
    result->pseudoInode = indexToPseudoInode(index);
    result->ac = this->ac;
    result->VestaSource::fptag = vre->fptag;

    switch (lockKind) {
      case LongId::noLock:
	if (lock != NULL) *lock = NULL;
	break;
      case LongId::readLock:
      case LongId::readLockV:
	vre->lock.acquireRead();
	*lock = &(vre->lock);
	break;
      case LongId::writeLock:
      case LongId::writeLockV:
	vre->lock.acquireWrite();
	*lock = &(vre->lock);
	break;
      case LongId::checkLock:
	if (*lock != &(vre->lock)) return VestaSource::invalidArgs;
	break;
    } 

    return VestaSource::ok;
}

// Requires VolatileRootLock.read
// Does not acquire any lock on the result!
VestaSource::errorCode
VDirVolatileRoot::lookup(Arc arc, VestaSource*& result,
			 AccessControl::Identity who, unsigned int indexOffset)
  throw ()
{
  // Start by initializing the result, in case we fail before setting
  // it to something useful.
  result = 0;

    VolRootEntry* vre;
    unsigned int index;
    char *endp;
    assert(indexOffset == 0);
    
    // Find the directory in the table
    index = strtol(arc, &endp, 16);
    if (*endp != '\0' || endp - arc != 8) {
	return VestaSource::notFound;
    }
    if (vrtTable.Get(index, vre) == 0) {
	return VestaSource::notFound;
    }

    result = NEW_CONSTR(VDirChangeable,
			(vre->readOnlyExisting ?
			 VestaSource::volatileROEDirectory :
			 VestaSource::volatileDirectory,
			 (Bit8*) VMemPool::lengthenPointer(vre->srep),
			 &(vre->sidref)));
    result->master = true;
    result->attribs = NULL;
    result->longid = longid.append(index);
    result->pseudoInode = indexToPseudoInode(index);
    result->ac = this->ac;
    result->VestaSource::fptag = vre->fptag;

    return VestaSource::ok;
}

// Requires VolatileRootLock.read
VestaSource::errorCode
VDirVolatileRoot::list(unsigned int firstIndex,
		       VestaSource::listCallback callback, void* closure,
		       AccessControl::Identity who,
		       bool deltaOnly, unsigned int indexOffset) throw ()
{
    assert(indexOffset == 0);
 
    // Start with the oldest entry unless there are some entries and
    // we're supposed to start at a particular point.
    VolRootEntry *vre = VolRootEntry::head;
    if((VolRootEntry::head != 0) && (firstIndex != 0))
      {
	// First optimistically see if the requested index exists.  If
	// so, start from there.
	if(!vrtTable.Get(firstIndex, vre))
	  {
	    if(VolRootEntry::tail->index >= VolRootEntry::head->index)
	      {
		// The indicies are sequential.  We haven't wrapped
		// recently.

		if(firstIndex > VolRootEntry::tail->index)
		  {
		    // firstIndex is past the end of the indicies, so
		    // we're done.  (There's a small chance that the
		    // caller chose firstIndex before the indicies
		    // wrapped around, but that seems extremely
		    // unlikely to come up in practice.)
		    vre = 0;
		  }
		else if(firstIndex <= VolRootEntry::head->index)
		  {
		    // firstIndex is before the beginning of the
		    // indicies, so start at the beginning.
		    vre = VolRootEntry::head;
		  }
		else
		  {
		    // firstIndex is somewhere in the range of current
		    // indices, so we search for the place to start
		    // the listing.
		    vre = VolRootEntry::head;
		    while((vre != 0) && (vre->index < firstIndex))
		      {
			vre = vre->next;
		      }
		  }
	      }
	    else
	      {
		// The indicies have wrapped around and we still have
		// some from before the wrap.

		if((firstIndex > VolRootEntry::tail->index) &&
		   (firstIndex < VolRootEntry::head->index))
		  {
		    // firstIndex is in the dead space in the middle.
		    // Assume we're done.
		    vre = 0;
		  }
		else if(firstIndex >= VolRootEntry::head->index)
		  {
		    // Look for a start point in the middle.  Stop if
		    // we reach the wrap point.
		    vre = VolRootEntry::head;
		    while((vre != 0) &&
			  (vre->index < firstIndex) &&
			  (vre->index >= VolRootEntry::head->index))
		      {
			vre = vre->next;
		      }
		  }
		else if(firstIndex <= VolRootEntry::tail->index)
		  {
		    // firstIndex is in the portion of the indicies
		    // after the wrap.  First find the wrap point.
		    vre = VolRootEntry::head;
		    while((vre != 0) &&
			  (vre->index >= VolRootEntry::head->index))
		      {
			vre = vre->next;
		      }

		    // Now search for the point to start the listing.
		    while((vre != 0) &&
			  (vre->index < firstIndex))
		      {
			vre = vre->next;
		      }
		  }
	      }
	  }
      }

    // Follow the linked list of entries until we run out.
    while(vre != 0)
      {
	char arcbuf[MAX_ARC_LEN+1];
	sprintf(arcbuf, "%08x", vre->index);
	if (!callback(closure, vre->readOnlyExisting ? 
		      VestaSource::volatileROEDirectory :
		      VestaSource::volatileDirectory, arcbuf, vre->index,
		      indexToPseudoInode(vre->index), NullShortId, true))
	  {
	    break;
	  }
	vre = vre->next;
      }
    return VestaSource::ok;
}


// Create a new volatile directory under the volatile root, based
// on a new evaluatorDirectory specified by (hostname, port, handle).
// No lock required on entry; acquires lock as requested.
VestaSource::errorCode
VDirVolatileRoot::
  createVolatileDirectory(char* hostname, char* port, Bit64 handle,
			  VestaSource*& result,
			  time_t timestamp,
			  LongId::lockKindTag lockKind,
			  ReadersWritersLock** lock,
			  bool readOnlyExisting) throw ()
{
    StableLock.acquireWrite();
    RWLOCK_LOCKED_REASON(&StableLock, "createVolatileDirectory (logging)");
    if (nextIndex % INDEX_BLOCKSIZE == 0) {
	char logrec[512];
	OBufStream ost(logrec, sizeof(logrec));
	// ('vidx' nextIndex)
	ost << "(vidx " << nextIndex << ")\n";
	VRLog.start();
	VRLog.put(ost.str());
	VRLog.commit();
	// Indices wrap before we reach anything that could be
	// interpreted as signed when converted to a signed integer
	// (which happens with the NFS readdir call).  Also we never
	// use 0.
	if ((nextIndex >= 0x7ffffc00) || (nextIndex == 0)) {
	  nextIndex = 1;
	}
    }
    unsigned int index = nextIndex++;
    StableLock.releaseWrite();

    // Do some preparation before acquiring VolatileRootLock.
    if (timestamp == 0) timestamp = time(NULL);
    VolRootEntry* vre = NEW_CONSTR(VolRootEntry, (index));
    vre->readOnlyExisting = readOnlyExisting;
    vre->creationTime = time(NULL);

    // Get a pointer to the volatile root, and (important!)
    //  write-lock the VolatileRootLock, protecting the new VDirEvaluator
    //  rep that we are about to allocate from being deleted or moved,
    //  and protecting the vrtTable.
    assert(lockKind != LongId::readLockV && lockKind != LongId::writeLockV);
    ReadersWritersLock* vrootlock;
    VestaSource* vroot = 
      VestaSource::volatileRoot(LongId::writeLock, &vrootlock);

    RWLOCK_LOCKED_REASON(vrootlock, "createVolatileDirectory");
    VDirEvaluator edir(readOnlyExisting ? VestaSource::evaluatorROEDirectory
		       : VestaSource::evaluatorDirectory,
		       hostname, port, handle, &vre->alive, timestamp);

    // Ugh, this method is a friend of VDirChangeable so it can use
    // private methods.  Might be better to put the following code in
    // VDirChangeable instead.
    VDirChangeable* vdir =
      NEW_CONSTR(VDirChangeable,
		 (readOnlyExisting ? VestaSource::volatileROEDirectory
		  : VestaSource::volatileDirectory));
    vdir->setIsMoreOrBase(vdir->rep, VDirChangeable::isBase);
    vdir->setMoreOrBase(vdir->rep, VMemPool::shortenPointer(edir.rep));
    vdir->baseCache = edir.rep;
    vdir->setTimestamp(timestamp);
    vdir->setID(index);
    vdir->sidref = &(vre->sidref);
    // end of Ugh code

    vre->srep = VMemPool::shortenPointer(vdir->rep);

    vre->link();
    vrtTable.Put(index, vre);

    vdir->longid = VolatileRootLongId.append(index);
    vdir->VestaSource::master = true;
    vdir->pseudoInode = index;
    vdir->ac = vroot->ac;
    vdir->VestaSource::fptag = vre->fptag;

    result = vdir;

    switch (lockKind) {
      case LongId::noLock:
	vrootlock->releaseWrite();
	break;
      case LongId::readLock:
	vre->lock.acquireRead();
	vrootlock->releaseWrite();
	*lock = &(vre->lock);
	break;
      case LongId::writeLock:
	vre->lock.acquireWrite();
	vrootlock->releaseWrite();
	*lock = &(vre->lock);
	break;
      case LongId::checkLock:
      case LongId::readLockV:
      case LongId::writeLockV:
	assert(false); // should not happen
	break;
    }

    return VestaSource::ok;
}


// Delete by index.  Not needed in other kinds of directory.
// VolatileRootLock.write is required!
// Note: Some code is duplicated in GardenerThread
VestaSource::errorCode
VDirVolatileRoot::deleteIndex(unsigned int index) throw ()
{
    VolRootEntry* vre;

    bool ok = vrtTable.Delete(index, vre, false);
    if (!ok) return VestaSource::notFound; 

    // Remove it from the linked list used for listing the contents of
    // the volatile root.
    vre->unlink();

    // Make sure no one else is playing in this tree
    vre->lock.acquireWrite();

    // Explicitly free data structure hanging from vre.  We do this 
    // to conserve memory.  The memory would eventually be freed by
    // the next source weed, but those don't occur frequently enough.
    VDirChangeable vdc((vre->readOnlyExisting
			? VestaSource::volatileROEDirectory
			: VestaSource::volatileDirectory), 
		       (Bit8*) VMemPool::lengthenPointer(vre->srep));
    VestaSource* vs;
    VestaSource::errorCode err = vdc.getBase(vs);
    assert(err == VestaSource::ok);
    VDirEvaluator* vde = (VDirEvaluator*) vs;
    vde->freeTree();
    // Note that we didn't give vdc its shortid reference counter
    // (vre->sidref).  This prevents freeTree from unlinking any
    // mutable shortids still in the volatile directory.  This is
    // needed because the evaluator will accept mutable files when
    // gathering results, so a mutable shortid left in a volatile
    // directory may now be refernced by a cache entry.
    vdc.freeTree();

    vre->lock.releaseWrite();
    delete vre;
    delete vs;
    
    return VestaSource::ok;
}

void 
VDirVolatileRoot::lockAll()
{
  VolatileRootLock.acquireWrite();
  VolRootIter iter(&vrtTable);
  IndexKey key;
  VolRootEntry* vre;
  while (iter.Next(key, vre)) {
    vre->lock.acquireWrite();
  }
}

void 
VDirVolatileRoot::unlockAll()
{
  VolRootIter iter(&vrtTable);
  IndexKey key;
  VolRootEntry* vre;
  while (iter.Next(key, vre)) {
    vre->lock.releaseWrite();
  }
  VolatileRootLock.releaseWrite();
}

static void
VidxCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    unsigned long ulindex;
    rr->getULong(c, ulindex);
    // Skip over any indices that may have been used since this log
    // record was written, and be sure we write another log record
    // when assigning the next index.  Note that necessarily
    // ulindex % INDEX_BLOCKSIZE == 0.
    //
    nextIndex = ulindex + INDEX_BLOCKSIZE;
}


// Requires write locks on all volatile directories!
void
VDirVolatileRoot::mark(bool byName, ArcTable* hidden) throw ()
{
    assert(byName);
    assert(hidden == NULL);
    VolRootIter iter(&vrtTable);
    IndexKey key;
    VolRootEntry* val;
    while (iter.Next(key, val)) {
	VDirChangeable vs(VestaSource::volatileDirectory,
			  (Bit8*) VMemPool::lengthenPointer(val->srep));
	vs.setHasName(true);
	vs.mark();
    }
}

// Requires write locks on all volatile directories!
Bit32
VDirVolatileRoot::checkpoint(Bit32& nextSP, std::fstream& ckpt) throw ()
{
    // Write unformatted part of the volatile checkpoint and update
    //  volatile root dir entries to use the new short pointers.
    //  The entries themselves don't have to be written to the checkpoint.

    VolRootIter iter(&vrtTable);
    IndexKey key;
    VolRootEntry* val;

    while (iter.Next(key, val)) {
	VDirChangeable vs(VestaSource::volatileDirectory,
			  (Bit8*) VMemPool::lengthenPointer(val->srep));
	val->srep = vs.checkpoint(nextSP, ckpt);
	// Crash if writing to the checkpoint file is failing
	if (!ckpt.good()) {
	    int errno_save = errno;
	    Text err = Basics::errno_Text(errno_save);
	    Repos::dprintf(DBG_ALWAYS,
			   "write to checkpoint file failed: %s (errno = %d)\n",
			   err.cchars(), errno_save);
	    assert(ckpt.good());  // crash
	}
    }
    return nextSP;
}

void
VDirVolatileRoot::finishCheckpoint(std::fstream& ckpt) throw ()
{
  // Re-create the last vidx log record
  ckpt << "(vidx " << nextIndex - (nextIndex % INDEX_BLOCKSIZE) << ")" << endl;
}

// Prune away old volatile directories whose owners are dead
// Duplicates some code from deleteIndex
void*
GardenerThread(void* arg) throw ()
{
  signal(SIGPIPE, SIG_IGN);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGSEGV, SIG_DFL);
  signal(SIGABRT, SIG_DFL);
  signal(SIGBUS, SIG_DFL);
  signal(SIGILL, SIG_DFL);

  for (;;) {
    sleep(GARDENER_SLEEP);

    // Counts of the number of volatile directories we skip because
    // they're new, the number that we check and are valid, and the
    // number we delete because they're evaluator is dead.
    unsigned int count_new = 0, count_valid = 0, count_deleted = 0;

    // First pass: find dead directories with a read lock.  We avoid
    // holding a write lock while iterating over all volatile
    // directories, as that could take some time.
    VolatileRootLock.acquireRead();
    RWLOCK_LOCKED_REASON(&VolatileRootLock, "GardenerThread, 1st pass");
    Sequence<unsigned int, true>
      check_index_list(/*sizeHint=*/ vrtTable.Size());
    IndexKey key;
    VolRootEntry* vre = VolRootEntry::head;
    time_t now = time(NULL);
    while (vre != 0) {
      // Don't check very new ones
      if (now - vre->creationTime < GARDENER_SLEEP)
	{
	  // We're going through the entries in creation order, so
	  // there's no need to consider any more.
	  break;
	}
      check_index_list.addhi(vre->index);
      vre = vre->next;
    }
    // Remember how many we skipped because they were new.
    count_new = vrtTable.Size() - check_index_list.size();
    VolatileRootLock.releaseRead();

    // Second pass: delete dead directories acquiring a write lock for
    // each one with a short delay inbetween to avoid starving other
    // threads.
    while(check_index_list.size() > 0)
      {
	// First we need to check whether this index is still present
	// and is connected to a dead evaluator
	key.index = check_index_list.remlo();
	bool dead = false;
	VolatileRootLock.acquireRead();
	RWLOCK_LOCKED_REASON(&VolatileRootLock,
			     "GardenerThread, 2nd pass, checking");
	// If the index is still in the table and not currently locked
	// by some other thread, then we'll check to see if the
	// evaluator is alive
	bool got_it = vrtTable.Get(key, vre) && vre->lock.tryWrite();
	VolatileRootLock.releaseRead();
	// Note that we release VolatileRootLock before checking
	// whether the associated evaluator is alive, as that
	// operation could take a substantial amount of time.
	if(got_it)
	  {
	    VDirChangeable vdc(VestaSource::volatileDirectory,
			       (Bit8*) VMemPool::lengthenPointer(vre->srep));
	    VestaSource* vs;
	    VestaSource::errorCode err = vdc.getBase(vs);
	    assert(err == VestaSource::ok);
	    VDirEvaluator* vde = (VDirEvaluator*) vs;
	    dead = !vde->alive();
	    vre->lock.releaseWrite();
	    delete vs;
	  }

	// If the evaluator was alive (or the index no longer existed,
	// or another thread had it locked), move on to the next index
	if(!dead)
	  {
	    count_valid++;
	    continue;
	  }

	bool deleted = false;
	Text hostname, port;

	VolatileRootLock.acquireWrite();
	RWLOCK_LOCKED_REASON(&VolatileRootLock,
			     "GardenerThread, 2nd pass, deleting");
	// Since we released VolatileRootLock after picking this
	// index, it's possible it was deleted before we got here.
	if(vrtTable.Delete(key, vre, false))
	  {
	    // Remove it from the linked list used for listing the
	    // contents of the volatile root.
	    vre->unlink();

	    // Make sure no one else is playing in this tree
	    vre->lock.acquireWrite();
	    VDirChangeable vdc((vre->readOnlyExisting
				? VestaSource::volatileROEDirectory
				: VestaSource::volatileDirectory),
			       (Bit8*) VMemPool::lengthenPointer(vre->srep));
	    VestaSource* vs;
	    VestaSource::errorCode err = vdc.getBase(vs);
	    assert(err == VestaSource::ok);
	    VDirEvaluator* vde = (VDirEvaluator*) vs;
	    vde->getClientHostPort(hostname, port);
	    deleted = true;
	    // It shouldn't have spontaneously become alive again.
	    assert(!vde->alive());
	    vde->purge();
	    vde->freeTree();
	    // Note that we didn't give vdc its shortid reference
	    // counter (vre->sidref).  This prevents freeTree from
	    // unlinking any mutable shortids still in the volatile
	    // directory.  This is needed because the evaluator will
	    // accept mutable files when gathering results, so a
	    // mutable shortid left in a volatile directory may now be
	    // refernced by a cache entry.  Also, the evaluator adds
	    // the cache entry for a tool invocation before deleting
	    // the volatile directory, so it's possible that the
	    // evaluator died after adding the cache entry but before
	    // deleting the volatile directory.
	    vdc.freeTree();
	    vre->lock.releaseWrite();
	    delete vre;
	    delete vs;
	  }
	VolatileRootLock.releaseWrite();
	if(deleted)
	  {
	    count_deleted++;
	    Repos::dprintf(DBG_GARDENER,
			   "GardenerThread: removed volatile directory "
			   "%08x from dead client %s:%s\n",
			   key.index, hostname.cchars(), port.cchars());
	  }
	// Give other threads a moment to acquire VolatileRootLock
	// before we lock it again.
	sleep(1);
      }

    // It's worth noting that we don't count the number of volatile
    // directories which get deleted out from under us while we work.
    // That's pretty rare though, and not teribly interesting.
    Repos::dprintf(DBG_GARDENER,
		   "GardenerThread: done pruning, volatile directory counts: "
		   "removed = %d, old but valid = %d, too recent = %d\n",
		   count_deleted, count_valid, count_new);
  }

  //return (void*) NULL;  // Not reached
}

void VDirVolatileRoot::init() throw ()
{
  static int done = false;
  assert(!done);
  done = true;
  RegisterRecoveryCallback("vidx", VidxCallback);
  Basics::thread_attr gardener_attr;
#if defined (_POSIX_THREAD_PRIORITY_SCHEDULING) && defined(PTHREAD_SCHED_RR_OK) && !defined(__linux__)
  // Linux only allows the superuser to use SCHED_RR
  gardener_attr.set_schedpolicy(SCHED_RR);
  gardener_attr.set_inheritsched(PTHREAD_EXPLICIT_SCHED);
  gardener_attr.set_sched_priority(sched_get_priority_min(SCHED_RR));
#endif
  
  gardener.fork(GardenerThread, NULL, gardener_attr);
}

VestaSource *VDirVolatileRoot::copy() throw()
{
  VestaSource *result = NEW(VDirVolatileRoot);
  *result = *this;
  return result;
}
