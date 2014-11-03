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
// ShortIdImpl.C
// Server implementations of remote methods in ShortIdBlock.H
// 

#if __linux__
#include <stdint.h>
#endif
#include <sys/types.h>
#include <dirent.h>
// add declaration to fix broken <dirent.h> header file
extern "C" int _Preaddir_r(DIR *, struct dirent *, struct dirent **);

#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <ctype.h>

#include <VestaConfig.H>
#include <Thread.H>

#include "ShortIdImpl.H"
#include "VestaLog.H"
#include "Recovery.H"
#include "ReadersWritersLock.H"
#include "ShortIdKey.H"
#include "FdCache.H"
#include "VRConcurrency.H"
#include "logging.H"

#include "lock_timing.H"

#include "ReposConfig.h"

#include <iomanip>

using std::fstream;
using std::hex;
using std::dec;
using std::setw;
using std::setfill;

#if defined(HAVE_INITSTATE_R) && defined(HAVE_RANDOM_R)
// Pitfall: on some OSes random's state is per-thread, so it would be
// difficult to make sure it gets reseeded (with a distinct seed!)  in
// every thread.  Instead we use the less-standard random_r function
// if it's availabe.
#define USE_RANDOM_R 1
#elif defined(HAVE_SRANDOM) && defined(HAVE_RANDOM)
#define USE_RANDOM 1
#else
#error Need thread-safe random number generator
#endif

// Types
struct ShortIdBlockInfo {
    time_t leaseExpires;
};
typedef Table<ShortIdKey, ShortIdBlockInfo*>::Default BlockInfoTable;
typedef Table<ShortIdKey, ShortIdBlockInfo*>::Iterator BlockInfoIter;

// Parameters
int LEASE_PERIOD = 60*60*2;       // validity period for leases (sec)
int LANDLORD_SLEEP_MIN = 1*60;    // minimum period between lease checks (sec)
int LANDLORD_SLEEP_MAX = 60*60;   // maximum period between lease checks (sec)
int LANDLORD_WORKLIST_SIZE = 128;

// Module globals
static BlockInfoTable biTable;
#if USE_RANDOM_R
/*static*/ struct random_data randDat;
/*static*/ int randState[32];
#endif
static Basics::mutex mu;          // protects biTable, randDat
static Basics::thread landlord;   // detects expired leases
static Text sid_dir;

// RPC to get a new block of ShortIds from the server, all with the
// specified leafflag.  On return, the block is leased to the caller
// until bk.leaseExpires (set by the function).  If the lease expires
// without being renewed (below), the server will eventually notice
// and reclaim all unused ShortIds in the block.  If local is true,
// the caller is in the same address space as the server, so the lease
// is nonexpiring and is not logged---if the server crashes, the
// caller crashes too, and no longer needs the lease.
void
AcquireShortIdBlock(ShortIdBlock& bk, bool leafflag, bool local) throw()
{
    ShortId start;
    int used, used2 = -1;
    ShortIdBlock bk2;
    
    if (!local) {
	StableLock.acquireWrite(); // protects the log
	RWLOCK_LOCKED_REASON(&StableLock, "AcquireShortIdBlock");
    }
    mu.lock();
    for (;;) {
	// Randomly choose a nonzero start for the block
	int randint;
#if USE_RANDOM_R
# if defined(RANDOM_R_ARGS_INT_RANDOM_DATA)
	(void) random_r(&randint, &randDat);
# elif defined(RANDOM_R_ARGS_RANDOM_DATA_INT)
	(void) random_r(&randDat, &randint);
# else
#  error Unknown arguments for random_r
# endif
#elif USE_RANDOM
	randint = random();
#else
#error Need thread-safe random number generator
#endif
	start = (ShortId)
	  ( (randint & ~(ShortIdBlock::size - 1) &
	     ~ShortIdBlock::leafFlag & ~ShortIdBlock::dirFlag)
	   | (leafflag ? ShortIdBlock::leafFlag : 0) );
	if (start == NullShortId) {
	    continue;
	}
	ShortIdBlockInfo* dummy;
        if (biTable.Get(start, dummy)) {
	    // Some other process is using this block; try another
	    continue;
	}

	// Check that there are some free ShortIds there
	bk.init(start);
	used = 0;
	char *name = ShortIdBlock::shortIdToName(start);
	char *p = strrchr(name, PathnameSep);
	assert(p != 0);
	*p = '\0';
	DIR *dir = opendir(name);
	delete[] name;
	if (!dir) {
	    // Directory not present, so all of block is free
	    break;
	}
	struct dirent de, *done;
	while (readdir_r(dir, /*OUT*/ &de, /*OUT*/ &done) == 0 &&
               done != (struct dirent *)NULL) {
	    char *endptr;
	    long offset = strtol(de.d_name, &endptr, 16);
	    if (*endptr != '\0') continue;
	    bk.set(start + offset);
	    used++;
	}
	closedir(dir);
	if (used >= ShortIdBlock::size) {
	    // Nothing free in this block; try another
	    continue;
	}
	if (used2 == -1 && used > ShortIdBlock::size / 2) {
	    // Block more than half full; save it and try another
	    bk2 = bk;
	    used2 = used;
	    continue;
	} else {
	    break;
	}
    }
    if (used2 != -1) {
	if (used2 < used) {
	    // First one tried was less full!
	    bk = bk2;
	}
    }
    ShortIdKey key(start);
    ShortIdBlockInfo* bi = NEW(ShortIdBlockInfo);
    bk.leaseExpires = bi->leaseExpires =
      (local ? ShortIdBlock::leaseNonexpiring : time(NULL) + LEASE_PERIOD);
    biTable.Put(key, bi);
    Repos::dprintf(DBG_SIDBLEASES,
		   "AcquireShortIdBlock: block = 0x%08x, expiration = %d%s\n",
		   start, bk.leaseExpires, (local ? " (non-expiring)" : ""));
    if (!local) {
	char logrec[256];
	// ('asidb' start leaseExpires)
	int sres = sprintf(logrec, "(asidb 0x%x %d)\n",
			   start, bk.leaseExpires);
	assert(sres > 0);
	assert(sres < sizeof(logrec));
	VRLog.start();
	VRLog.put(logrec);
	VRLog.commit();
    }
    mu.unlock();
    if (!local) {
	StableLock.releaseWrite();
    }
}


// RPC to renew the lease on a block.  Returns false if the block's
// lease had already expired.  (Allowing a lease to expire is a fatal
// client error.)  Otherwise, returns true, and the block is leased to
// the caller until block.leaseExpires (set by the function).  This
// routine is not called for local (nonexpiring) leases.
//
bool
RenewShortIdBlock(ShortIdBlock& bk) throw()
{
    StableLock.acquireWrite();
    RWLOCK_LOCKED_REASON(&StableLock, "RenewShortIdBlock");
    mu.lock();
    ShortIdKey key(bk.start);
    ShortIdBlockInfo* bi;
    bool inTable = biTable.Delete(key, bi); // reuse old bi value
    time_t now = time(0);
    if (!inTable || bi->leaseExpires < now) {
	mu.unlock();
	StableLock.releaseWrite();
	if(inTable) delete bi;
	Repos::dprintf(DBG_SIDBLEASES,
		       "RenewShortIdBlock: %s block = 0x%08x, not renewed\n",
		       (inTable ? "invalid" : "expired"), bk.start);
	return false;
    }
    bk.leaseExpires = bi->leaseExpires = now + LEASE_PERIOD;
    biTable.Put(key, bi);
    Repos::dprintf(DBG_SIDBLEASES,
		   "RenewShortIdBlock: block = 0x%08x, expiration = %d\n",
		   bk.start, bk.leaseExpires);
    char logrec[256];
    // ('asidb' start leaseExpires)
    int sres = sprintf(logrec, "(asidb 0x%x %d)\n",
		       bk.start, bk.leaseExpires);
    assert(sres > 0);
    assert(sres < sizeof(logrec));
    VRLog.start();
    VRLog.put(logrec);
    VRLog.commit();
    mu.unlock();
    StableLock.releaseWrite();
    return true;
}


static void
ReleaseShortIdBlockAt(ShortId start, bool local) throw()
{
    ShortIdBlockInfo* bi;
    bool ok = biTable.Delete(start, bi);
    if (ok) delete bi;
    Repos::dprintf(DBG_SIDBLEASES,
		   "ReleaseShortIdBlockAt: %sblock = 0x%08x\n",
		   (ok ? "" : "invalid "), start);
    if (!local) {
	char logrec[256];
	// ('rsidb' start)
	int sres = sprintf(logrec, "(rsidb 0x%x)\n", start);
	assert(sres > 0);
	assert(sres < sizeof(logrec));
	VRLog.start();
	VRLog.put(logrec);
	VRLog.commit();
    }
}

// RPC to return a block to the server
// The caller promises not to assign any more ShortIds from this block, even
//  if some are still unused.  This routine should be called when a process
//  is going to shut down, or when it has used up a block. If local is true,
//  the caller is in the same address space as the server, so the lease
//  is not logged---if the server crashes, the caller crashes too, and
//  no longer needs the lease.
//
void
ReleaseShortIdBlock(ShortIdBlock& bk, bool local) throw()
{
    if (!local) {
	StableLock.acquireWrite();	// protects the log too
	RWLOCK_LOCKED_REASON(&StableLock, "ReleaseShortIdBlock");
    }
    mu.lock();
    ReleaseShortIdBlockAt(bk.start, local);
    mu.unlock();
    if (!local) {
	StableLock.releaseWrite();
    }
}


// 
// Thread to clean up blocks with expired leases
//
// Annoyingly, one can't delete from a Table while in the scope
// of an interator.  We would really like to delete the element
// the iterator has just returned sometimes.  Instead, we make an
// array of things to delete and do them later.  If the array fills,
// we get the rest on the next run.
//
// [Note: Tables have been since been improved to provide a way of
//  deleting while in the scope of an iterator, but the code below
//  has not been rewritten to take advantage of the new feature.]
//
void *
LandlordThread(void *arg)
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGILL, SIG_DFL);
    signal(SIGBUS, SIG_DFL);

    // Start out with the maximum sleep time
    int landlord_sleep = LANDLORD_SLEEP_MAX;

    for (;;) {
	ShortId worklist[LANDLORD_WORKLIST_SIZE];
	int i = 0;
	StableLock.acquireWrite();
	RWLOCK_LOCKED_REASON(&StableLock, "LandlordThread");
	mu.lock();
	{
	    BlockInfoIter iter(&biTable);
	    ShortIdKey key;
	    ShortIdBlockInfo* bi;
	    time_t now = time(NULL);
	    while (iter.Next(key, bi)) {
		if (bi->leaseExpires < now) {
		    worklist[i++] = key.sid;
		    if (i >= LANDLORD_WORKLIST_SIZE) break;
		}
	    }
	}
	int j;
	// Group all log records together into one commit
	VRLog.start();
	for (j=0; j<i; j++) {
	    ReleaseShortIdBlockAt(worklist[j], false);
	}
	VRLog.commit();
	mu.unlock();
	StableLock.releaseWrite();

	// Depending on how much work there is to do, adjust the sleep
	// time
	if(i >= LANDLORD_WORKLIST_SIZE)
	  {
	    // Too much work to do, try not to sleep so long
	    if((landlord_sleep >> 1) >= LANDLORD_SLEEP_MIN)
	      {
		landlord_sleep >>= 1;
	      }
	    else
	      {
		// We're still very busy and we aren't allowed to wake
		// up and work any more often.  Log a message about
		// being very busy.
		Repos::dprintf(DBG_ALWAYS,
			       "Warning: LandlordThread is very busy.  Consider "
			       "raising [Repository]Landlord_worklist_size or "
			       "lowering [Repository]Landlord_sleep_min or "
			       "lowering [Repository]ShortIdBlock_lease_period\n");
	      }
	  }
	else
	  {
	    // Not much work to do, maybe we can sleep longer
	    if((landlord_sleep << 1) <= LANDLORD_SLEEP_MAX)
	      {
		landlord_sleep <<= 1;
	      }
	  }

	sleep(landlord_sleep);
    }

    //return (void *)NULL;     // Not reached
}

// Code to rebuild biTable from the log
static void
AsidbCallback(RecoveryReader* rr, char &c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    long ltmp;
    ShortId start;
    time_t leaseExpires;
    RecoveryReader::Ident id;
    
    // start leaseExpires
    rr->getLong(c, ltmp);
    start = (ShortId) ltmp;
    rr->getLong(c, ltmp);
    leaseExpires = (ShortId) ltmp;

    if(leaseExpires == 0x7fffffff)
      // Ignore any entries for non-expiring leases.  (There was a bug
      // which used ot put them in repository checkpoint files.  We
      // use the hard-coded value for the non-expiring timestamp that
      // the version with the bug used.)
      Repos::dprintf(DBG_SIDBLEASES,
		     "AsidbCallback: Ignoring recovered non-expiring block = 0x%08x\n",
		     start);
      return;
    
    ShortIdKey key(start);
    
    ShortIdBlockInfo* bi;
    mu.lock();
    bool inTable = biTable.Delete(key, bi);
    if(!inTable)
      bi = NEW(ShortIdBlockInfo);
    else
      assert(bi != 0);
    bi->leaseExpires = leaseExpires;
    inTable = biTable.Put(key, bi);
    assert(!inTable);
    mu.unlock();
}

static void
RsidbCallback(RecoveryReader* rr, char &c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    ShortId start;
    long lstart;
    
    // start
    rr->getLong(c, lstart);
    start = (ShortId) lstart;
    mu.lock();
    ShortIdBlockInfo* bi;
    bool ok = biTable.Delete(start, bi);
    if (ok) delete bi;
    mu.unlock();
}

// Code to do checkpointing
void
ShortIdBlockCheckpoint(fstream& ckpt) throw()
{
    // VRLock.write is already held
    mu.lock();
    BlockInfoIter iter(&biTable);
    ShortIdKey key;
    ShortIdBlockInfo* bi;

    while (iter.Next(key, bi)) {
      Repos::dprintf(DBG_SIDBLEASES,
		     "ShortIdBlockCheckpoint: block = 0x%08x, expiration = %d%s\n",
		     key.sid, bi->leaseExpires,
		     ((bi->leaseExpires == ShortIdBlock::leaseNonexpiring)
		      ? " (non-expiring)" : ""));
        if(bi->leaseExpires == ShortIdBlock::leaseNonexpiring)
	  // There's no need to checkpoint internally-used
	  // non-expiring leases.
	  continue;
	ckpt << "(asidb 0x" << hex << key.sid << dec
	  << " " << bi->leaseExpires << ")\n";
    }

    mu.unlock();
}

void DebugShortIdBlocks(const char *message) throw()
{
    if (!Repos::isDebugLevel(DBG_SIDBLEASES)) return;

    mu.lock();
    BlockInfoIter iter(&biTable);
    ShortIdKey key;
    ShortIdBlockInfo* bi;

    unsigned int i = 0;
    while (iter.Next(key, bi))
      {
	Repos::dprintf(DBG_ALWAYS,
		       "DebugShortIdBlocks(%s): [%u] block = 0x%08x, expiration = %d%s\n",
		       message, i++, key.sid, bi->leaseExpires,
		       ((bi->leaseExpires == ShortIdBlock::leaseNonexpiring)
			? " (non-expiring)" : ""));
      }
    if(i == 0)
      {
	Repos::dprintf(DBG_ALWAYS,
		       "DebugShortIdBlocks(%s): no leases\n", message);
      }
    mu.unlock();
}

static const int charsPerArc[] = { 3, 3, 2, 0 };
static const int MAX_CHARS_PER_ARC = 3;

extern "C"
{
  static int
  arccmp(const void* a, const void* b)
  {
    return strcmp((char*) a, (char*) b);
  }
}

static ShortId
NextSidToKeep(SourceOrDerived& sidstream)
{
    ShortId nextkeep;

    if (sidstream.eof()) {
	Repos::dprintf(DBG_SIDDEL, "at end of ShortIdsFile\n");
	nextkeep = NullShortId;
    } else {
	for (;;) {
	    sidstream >> nextkeep;
	    if (sidstream.fail()) {
		assert(sidstream.eof());
		Repos::dprintf(DBG_SIDDEL, "hit end of ShortIdsFile\n");
		nextkeep = NullShortId;
	    } else if (nextkeep == NullShortId) {
		Repos::dprintf(DBG_ALWAYS, "error: NullShortId on keep list\n");
		continue; // skip the null
	    }
	    Repos::dprintf(DBG_SIDDEL, "next to keep: 0x%08x\n", nextkeep);
	    break;
	}
    }
    return nextkeep;
}

// This macro, which represents the units of the st_blocks in the stat
// structure, is not always provided by system headers.  (The units
// are supposed to be 512-byte blocks across all systems.)
#if !defined(S_BLKSIZE)
#define S_BLKSIZE 512
#endif

// Scan a directory level in ShortId storage.  Return the number of
// entries remaining.  On error, return -(errno).  Delete any ShortIds
// not listed in the sidfile, and any subdirectories that are emptied.
static int
ScanDirForDelete(int levelnum, const char* dirname, Bit32 dirnum,
		 ShortId& nextkeep, SourceOrDerived& sidstream, time_t lease,
		 SourceOrDerived& deleted_stream,
		 /*OUT*/ Basics::uint32 &deleted_count,
		 /*OUT*/ Basics::uint64 &deleted_space)
{
    typedef char SidArc[MAX_CHARS_PER_ARC + 1];
    SidArc *arcs = NEW_PTRFREE_ARRAY(SidArc, 1 << (MAX_CHARS_PER_ARC * 4));
    int narcs = 0;
    int i, nkept, ret;
    DIR* dir = opendir(dirname);
    if (dir == NULL) {
        int errno_save = errno;
	Text err = Basics::errno_Text(errno_save);
	Repos::dprintf(DBG_ALWAYS, "error opening sid directory %s: %s"
		       " (errno = %d)\n",
		       dirname, err.cchars(), errno_save);
	assert(errno_save != 0);
	return -errno_save;
    }
    struct dirent de, *done;
    while (readdir_r(dir, /*OUT*/ &de, /*OUT*/ &done) == 0 &&
           done != (struct dirent *)NULL) {
	bool ok = true;
	for (i = 0; i < charsPerArc[levelnum]; i++) {
	    if (!isxdigit(de.d_name[i]) || isupper(de.d_name[i])) {
		ok = false;
		break;
	    }
	}
	if (!ok || de.d_name[charsPerArc[levelnum]] != '\000') continue;
	strcpy(arcs[narcs++], de.d_name);
    }
    closedir(dir);
    if (narcs == 0) return 0;
    qsort(arcs, narcs, MAX_CHARS_PER_ARC + 1, arccmp);
    nkept = narcs;
    if (charsPerArc[levelnum + 1] == 0) {
	// At bottom level, can do file deletions
	for (i = 0; i < narcs; i++) {
	    ShortId cursid = (dirnum << (charsPerArc[levelnum]*4)) +
	      strtoul(arcs[i], NULL, 16);
	    for (;;) {
		if (nextkeep != NullShortId && cursid > nextkeep) {
		    if (SourceOrDerived::dirShortId(nextkeep)) {
			Repos::dprintf(DBG_SIDDISP, "directory: 0x%08x\n", nextkeep);
		    } else {
			Repos::dprintf(DBG_ALWAYS, "missing: 0x%08x\n", nextkeep);
		    }
		    nextkeep = NextSidToKeep(sidstream);
		    continue;
		}
		break;
	    }
	    if (nextkeep == cursid) {
		Repos::dprintf(DBG_SIDDISP, "listed: 0x%08x\n", cursid);
		nextkeep = NextSidToKeep(sidstream);
	    } else {
		// cursid < nextkeep || nextkeep == NullShortId
	        char *sidname = NEW_PTRFREE_ARRAY(char, (strlen(dirname) + 1 +
							 strlen(arcs[i]) + 1));
		if (*dirname == '\0') {
		    strcpy(sidname, arcs[i]);
		} else {
		    sprintf(sidname, "%s%c%s",
			    dirname, PathnameSep, arcs[i]);
		}
		struct stat statbuf;
		int res = stat(sidname, &statbuf);
		if (res < 0) {
		    if (errno == ENOENT) {
		      // Harmless race with eager deletion of this sid
		      nkept--;
		      delete [] sidname;
		      continue;
		    }
		    int errno_save = errno;
		    Text err = Basics::errno_Text(errno_save);
		    Repos::dprintf(DBG_ALWAYS,
				   "error on stat of sid %08x (%s): %s"
				   " (errno = %d)\n",
				   cursid, sidname, err.cchars(), errno_save);
		    assert(errno_save != 0);
		    delete [] sidname;
		    return -errno_save;
		}
		if (statbuf.st_ctime < lease) {
		    Repos::dprintf(DBG_SIDDISP, "garbage: 0x%08x\n", cursid);
		    if (!Repos::isDebugLevel(DBG_SIDNODEL)) {
			res = unlink(sidname);
			if (res < 0 && errno != ENOENT) {
			    int errno_save = errno;
			    Text err = Basics::errno_Text(errno_save);
			    Repos::dprintf(DBG_ALWAYS,
					   "error on unlink of sid %08x (%s): %s"
					   " (errno = %d)\n",
					   cursid, sidname, err.cchars(), errno_save);
			    assert(errno_save != 0);
			    delete [] sidname;
			    return -errno_save;
			}
			deleted_stream << cursid << "\n";
			deleted_count++;
			deleted_space += (statbuf.st_blocks * S_BLKSIZE);
		    }
		    FdCache::flush(cursid, FdCache::any);
		    nkept--;
		} else {
		    Repos::dprintf(DBG_SIDDISP, "leased: 0x%08x\n", cursid);
		}
		delete [] sidname;
	    }
	}
    } else {
	for (i = 0; i < narcs; i++) {
	    char *subdirname = NEW_PTRFREE_ARRAY(char, (strlen(dirname) + 1 +
							strlen(arcs[i]) + 1));
	    Bit32 subdirnum = (dirnum << (charsPerArc[levelnum]*4)) +
	      strtoul(arcs[i], NULL, 16);
	    if (*dirname == '\0') {
		strcpy(subdirname, arcs[i]);
	    } else {
		sprintf(subdirname, "%s%c%s",
			dirname, PathnameSep, arcs[i]);
	    }
	    ret = ScanDirForDelete(levelnum + 1, subdirname, subdirnum,
				   nextkeep, sidstream, lease,
				   deleted_stream, deleted_count, deleted_space);
	    if (ret < 0) {
	      delete [] subdirname;
	      return ret;
	    }
	    if (ret == 0) {
		Repos::dprintf(DBG_SIDDISP, "empty: %s\n", subdirname);
		if (!Repos::isDebugLevel(DBG_SIDNODEL)) {
		    int res = rmdir(subdirname);
		    if (res < 0) {
		        int errno_save = errno;
			Text err = Basics::errno_Text(errno_save);
  		        Repos::dprintf(DBG_ALWAYS, "error on sid rmdir %s: %s"
				       " (errno = %d)\n",
				       subdirname, err.cchars(), errno_save);
			assert(errno_save != 0);
			delete [] subdirname;
			return -errno_save;
		    }
		}
		nkept--;
	    }
	    delete [] subdirname;
	}
    }
    delete [] arcs;
    return nkept;
}


// Do actual deletions, at the end of a weed.
// sidfile must be sorted.
int
DeleteAllShortIdsBut(ShortIdsFile sidfile, time_t lease,
		     ShortIdsFile deleted_file,
		     /*OUT*/ Basics::uint32 &deleted_count,
		     /*OUT*/ Basics::uint64 &deleted_space) throw()
{
    ShortId nextkeep;
    SourceOrDerived sidstream, deleted_stream;
    sidstream.open(sidfile);
    sidstream >> hex;
    if (!sidstream.good()) {
	int ret = errno;
	Text err = Basics::errno_Text(ret);
	Repos::dprintf(DBG_ALWAYS,
		       "error opening combined keepSidFile %08x: %s (errno = %d)\n",
		       sidfile, err.cchars(), ret);
	assert(ret != 0);
	sidstream.close();
	return ret;
    }
    deleted_stream.open(deleted_file, std::ios::out);
    if (!deleted_stream.good())
      {
	int ret = errno;
	Text err = Basics::errno_Text(ret);
	Repos::dprintf(DBG_ALWAYS,
		       "error opening deletedSidFile %08x: %s (errno = %d)\n",
		       deleted_file, err.cchars(), ret);
	assert(ret != 0);
	deleted_stream.close();
	return ret;
      }
    deleted_stream << hex << setw(8) << setfill('0');
    deleted_count = 0;
    deleted_space = 0;
    // Prime the pump
    nextkeep = NextSidToKeep(sidstream);
    int ret =
      ScanDirForDelete(0, sid_dir.cchars(), 0, nextkeep, sidstream, lease,
		       deleted_stream, deleted_count, deleted_space); 
    deleted_stream.flush();
    deleted_stream.close();
    sidstream.close();
    if (ret < 0) return -ret;
    return 0;
}


void
ShortIdServerInit()
{
#if USE_RANDOM_R
    memset((void *) randState, 0, sizeof(randState));
    randDat.state = NULL;
# if defined(INITSTATE_R_ARGS_SEED_STATE_SIZE_CHARPP_RD)
    char *dummy;
    (void) initstate_r((unsigned) time(0),
		       (char *) randState, sizeof(randState),
		       &dummy, &randDat);
# elif defined(INITSTATE_R_ARGS_SEED_STATE_SIZE_RD)
    (void) initstate_r((unsigned) time(0),
		       (char *) randState, sizeof(randState),
		       &randDat);
# else
#  error Unknown initstate_r arguments
# endif
#elif USE_RANDOM
    srandom((unsigned) time(NULL));
#else
#error Need thread-safe random number generator
#endif //NOTDEF
    RegisterRecoveryCallback("asidb", AsidbCallback);
    RegisterRecoveryCallback("rsidb", RsidbCallback);
    try {
	bool ok = VestaConfig::get("Repository", "sid_dir", sid_dir);
	if (!ok) sid_dir = "";

	if(VestaConfig::is_set("Repository", "ShortIdBlock_lease_period"))
	  LEASE_PERIOD =
	    VestaConfig::get_int("Repository", "ShortIdBlock_lease_period");
	if(LEASE_PERIOD < 120)
	  {
	    Repos::dprintf(DBG_ALWAYS,
			   "Warning: value for "
			   "[Repository]ShortIdBlock_lease_period (%d) is too "
			   "low; resetting to minimum of 120 seconds "
			   "(2 minutes)\n",
			   LEASE_PERIOD);
	    LEASE_PERIOD  = 120;
	  }

	if(VestaConfig::is_set("Repository", "Landlord_worklist_size"))
	  LANDLORD_WORKLIST_SIZE =
	    VestaConfig::get_int("Repository", "Landlord_worklist_size");
	if(LANDLORD_WORKLIST_SIZE < 128)
	  {
	    Repos::dprintf(DBG_ALWAYS,
			   "Warning: value for "
			   "[Repository]Landlord_worklist_size (%d) is too "
			   "low; resetting to minimum of 128 leases\n",
			   LANDLORD_WORKLIST_SIZE);
	    LANDLORD_WORKLIST_SIZE  = 128;
	  }

	if(VestaConfig::is_set("Repository", "Landlord_sleep_max"))
	  LANDLORD_SLEEP_MAX =
	    VestaConfig::get_int("Repository", "Landlord_sleep_max");
	if(LANDLORD_SLEEP_MAX > 60*60)
	  {
	    Repos::dprintf(DBG_ALWAYS,
			   "Warning: value for "
			   "[Repository]Landlord_sleep_max (%d) is too "
			   "high; resetting to maximum of 3600 seconds "
			   "(1 hour)\n",
			   LANDLORD_SLEEP_MAX);
	    LANDLORD_SLEEP_MAX = 60*60;
	  }

	if(VestaConfig::is_set("Repository", "Landlord_sleep_min"))
	  LANDLORD_SLEEP_MIN =
	    VestaConfig::get_int("Repository", "Landlord_sleep_min");
	if(LANDLORD_SLEEP_MIN > (LANDLORD_SLEEP_MAX >> 1))
	  {
	    Repos::dprintf(DBG_ALWAYS,
			   "Warning: value for "
			   "[Repository]Landlord_sleep_max (%d) is too "
			   "high; resetting to maximum of %d seconds "
			   "(1/2 of [Repository]Landlord_sleep_max)\n",
			   LANDLORD_SLEEP_MIN, (LANDLORD_SLEEP_MAX >> 1));
	    LANDLORD_SLEEP_MIN = (LANDLORD_SLEEP_MAX >> 1);
	  }

    } catch (VestaConfig::failure f) {
	Repos::dprintf(DBG_ALWAYS, "VestaConfig::failure %s\n", f.msg.cchars());
	abort();
    }
}

void
ShortIdServerInit2()
{
  Basics::thread_attr landlord_attr;
#if defined (_POSIX_THREAD_PRIORITY_SCHEDULING) && defined(PTHREAD_SCHED_RR_OK) && !defined(__linux__)
  // Linux only allows the superuser to use SCHED_RR
  landlord_attr.set_schedpolicy(SCHED_RR);
  landlord_attr.set_inheritsched(PTHREAD_EXPLICIT_SCHED);
  landlord_attr.set_sched_priority(sched_get_priority_min(SCHED_RR));
#endif
  
  landlord.fork(LandlordThread, 0, landlord_attr);
}
