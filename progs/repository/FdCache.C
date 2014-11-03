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
// FdCache.C
//
// Cache of open file descriptors for files named by ShortId
//

#include "Basics.H"
#include "FdCache.H"
#include "ShortIdKey.H"
#include "logging.H"

#include <Thread.H>
#include <fcntl.h>

#include "timing.H"

namespace FdCache
{
  // Types
  struct FdInfo {
    int fd;             // Open file descriptor number
    FdCache::OFlag ofl; // Open for rw or ro
    time_t lastUse;     // Time when last used
    // LRU list links
    FdInfo* prev;
    FdInfo* next;
    // Links to more descriptors for the same file
    FdInfo* moreprev;
    FdInfo* more;
    // Need to store key redundantly for removal via LRU
    ShortIdKey key;
  };
  typedef ::Table<ShortIdKey, FdInfo*>::Default FdTable;
  typedef ::Table<ShortIdKey, FdInfo*>::Iterator FdIter;

  
  class PartialCache
  {
  private:
    Basics::mutex mu;
    FdTable table;
    FdInfo lruListHead;
    unsigned int nCached;

    Basics::uint64 hit_count, open_miss_count, try_miss_count,
      evict_count, expire_count;

    void fdiDeleteNL(FdInfo* fdi, char* where);

    // Lookup an entry.  Caller must lock/unlock mu.
    int lookup(ShortIdKey &key, FdCache::OFlag ofl,
	       /*OUT*/ FdCache::OFlag &oflout)
      throw ();

  public:
    PartialCache()
      : nCached(0),
	hit_count(0), open_miss_count(0), try_miss_count(0),
	evict_count(0), expire_count(0)
    {
      this->lruListHead.prev = this->lruListHead.next = &(this->lruListHead);
    }

    int open(ShortId sid, FdCache::OFlag ofl, FdCache::OFlag* oflout) throw ();
    int tryopen(ShortId sid, FdCache::OFlag ofl, FdCache::OFlag* oflOut =NULL)
    throw();
    void close(ShortId sid, int fd, OFlag ofl) throw();
    void flush(ShortId sid, OFlag ofl) throw();

    void janitor_sweep(time_t now) throw();

    void accumulateStats(/*OUT*/ Basics::uint32 &n_in_cache,
			 /*OUT*/ Basics::uint64 &hits,
			 /*OUT*/ Basics::uint64 &open_misses,
			 /*OUT*/ Basics::uint64 &try_misses,
			 /*OUT*/ Basics::uint64 &evictions,
			 /*OUT*/ Basics::uint64 &expirations);
  };
}

// Module tuning parameters
static const int JANITOR_SLEEP = 5*60;  // how often janitor cleans up
static const int JANITOR_TOO_OLD = 2*JANITOR_SLEEP;  // how old is too old

// Module globals
static pthread_once_t once = PTHREAD_ONCE_INIT;
static unsigned int maxCached;
static Basics::thread janitor; // closes unused files that are too old

static unsigned int fdCache_split_bits = 2, fdCache_count;
static Bit32 fdCache_split_mask;
static FdCache::PartialCache *fdCaches;

// Common routine for deletion
void FdCache::PartialCache::fdiDeleteNL(FdCache::FdInfo* fdi, char* where)
{
    FdCache::FdInfo* deleted;
    if (fdi->moreprev == NULL) {
	bool ok = this->table.Delete(fdi->key, deleted, false);
	assert(ok);
	assert(deleted == fdi);
	if (fdi->more != NULL) {
	    this->table.Put(fdi->key, fdi->more);
	}
    }
    fdi->prev->next = fdi->next;
    fdi->next->prev = fdi->prev;
    if (fdi->moreprev) fdi->moreprev->more = fdi->more;
    if (fdi->more) fdi->more->moreprev = fdi->moreprev;

    ::close(fdi->fd);
    Repos::dprintf(DBG_FDCACHE, "FdCache::%s deleted old sid 0x%08x, fd %d, ofl %d\n",
		   where, fdi->key.sid, fdi->fd, fdi->ofl);
    delete fdi;
}

void FdCache::PartialCache::janitor_sweep(time_t now) throw()
{
  this->mu.lock();
  while (this->nCached > 0 && 
	 (now - this->lruListHead.next->lastUse) >= JANITOR_TOO_OLD) {
    this->fdiDeleteNL(this->lruListHead.next, "JanitorThread");
    this->nCached--;
    this->expire_count++;
  }
  this->mu.unlock();
}

void *
JanitorThread(void *arg)
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    signal(SIGILL, SIG_DFL);

    for (;;) {
	time_t now = time(NULL);
	// Loop over the file descriptor caches, and have each one
	// discard old file descriptors.
	for(unsigned int i = 0; i < fdCache_count; i++)
	  {
	    fdCaches[i].janitor_sweep(now);
	  }
	sleep(JANITOR_SLEEP);
    }

    //return (void *)NULL;    // Not reached
}

extern "C"
{
  static void
  FdCache_init()
  {
    maxCached = getdtablesize() / 2;
    assert(maxCached > 0);

    // Get the number of bits we'll be using to split between
    // different file descriptor caches.
    Text value;
    if (VestaConfig::get("Repository", "fdCache_split_bits", value)) {
	fdCache_split_bits = atoi(value.cchars());
    }

    // Allocate the required number of file descriptor caches.
    fdCache_count = (1 << fdCache_split_bits);
    fdCaches = NEW_ARRAY(FdCache::PartialCache, fdCache_count);

    // Generate a mask to use when finding which file descriptor cache
    // a given ShortId belongs in.
    fdCache_split_mask = 0;
    for(unsigned int i = 0; i < fdCache_split_bits; i++)
      {
	fdCache_split_mask |= (1 << i);
      }
    assert(fdCache_split_mask < fdCache_count);

    Basics::thread_attr janitor_attr;
#if defined (_POSIX_THREAD_PRIORITY_SCHEDULING) && defined(PTHREAD_SCHED_RR_OK) && !defined(__linux__)
    // Linux only allows the superuser to use SCHED_RR
    janitor_attr.set_schedpolicy(SCHED_RR);
    janitor_attr.set_inheritsched(PTHREAD_EXPLICIT_SCHED);
    janitor_attr.set_sched_priority(sched_get_priority_min(SCHED_RR));
#endif

    // Start the janitor thread.
    janitor.fork(JanitorThread, 0, janitor_attr);
  }
}

static unsigned int fdCacheIndex(ShortId sid)
{
  unsigned int result = sid & fdCache_split_mask;
  assert(result < fdCache_count);
  return result;
}

int
FdCache::PartialCache::lookup(ShortIdKey &key, FdCache::OFlag ofl,
			      /*OUT*/ FdCache::OFlag &oflout)
  throw ()
{
  FdCache::FdInfo* fdihead = 0;
  int fd = -1;
  FdCache::OFlag oflused = FdCache::any;

  if (this->table.Delete(key, fdihead, false)) {
    // Chain found in cache; look for one with correct ofl
    FdCache::FdInfo* fdi = fdihead;
    do {
      FdCache::FdInfo* more = fdi->more;
      if (ofl == FdCache::any || ofl == fdi->ofl) {
	fd = fdi->fd;
	assert(fd != -1);
	oflout = fdi->ofl;
	fdi->prev->next = fdi->next;
	fdi->next->prev = fdi->prev;
	if (fdi->moreprev != NULL) {
	  fdi->moreprev->more = fdi->more;
	}
	if (fdi->more != NULL) {
	  fdi->more->moreprev = fdi->moreprev;
	}
	if (fdihead == fdi) {
	  fdihead = fdi->more;
	}
	delete fdi;
	this->nCached--;
	break;
      }
      fdi = more;
    } while (fdi);
    if (fdihead != NULL) {
      this->table.Put(key, fdihead);
    }
  }

  return fd;
}

int
FdCache::open(ShortId sid, FdCache::OFlag ofl, FdCache::OFlag* oflout) throw ()
{
  pthread_once(&once, FdCache_init);
  unsigned int index = fdCacheIndex(sid);
  assert(fdCaches != NULL);
  return fdCaches[index].open(sid, ofl, oflout);
}

int
FdCache::PartialCache::open(ShortId sid, FdCache::OFlag ofl, FdCache::OFlag* oflout) throw ()
{
  // First search the cache for an existing file descriptor.
  FdCache::OFlag oflused = FdCache::any;
  ShortIdKey key(sid);

  RECORD_TIME_POINT;
  this->mu.lock();
  RECORD_TIME_POINT;

  int fd = this->lookup(key, ofl, oflused);

  // Update hit/miss statistics
  if(fd != -1)
    this->hit_count++;
  else
    this->open_miss_count++;

  this->mu.unlock();
  RECORD_TIME_POINT;

  if (fd == -1)
    {
      // Not found in cache: open a new file descriptor
      if (ofl == FdCache::any) {
	oflused = FdCache::ro; //!! heuristic
      } else {
	oflused = ofl;
      }
      RECORD_TIME_POINT;
      fd = SourceOrDerived::fdopen(sid, oflused == FdCache::ro ?
				   O_RDONLY : O_RDWR);
      RECORD_TIME_POINT;
      Repos::dprintf(DBG_FDCACHE, "FdCache::open 0x%08x opened fd %d ofl %d=>%d\n",
		     sid, fd, ofl, oflused);
    }
  else
    {
      Repos::dprintf(DBG_FDCACHE,
		     "FdCache::open 0x%08x found fd %d ofl %d=>%d\n",
		     sid, fd, ofl, oflused);
    }
  assert(oflused != FdCache::any);
  if (oflout) *oflout = oflused;
  return fd;
}

int FdCache::tryopen(ShortId sid, FdCache::OFlag ofl, FdCache::OFlag* oflout)
  throw ()
{
  pthread_once(&once, FdCache_init);
  unsigned int index = fdCacheIndex(sid);
  assert(fdCaches != NULL);
  return fdCaches[index].tryopen(sid, ofl, oflout);
}

int
FdCache::PartialCache::tryopen(ShortId sid, FdCache::OFlag ofl, FdCache::OFlag* oflout)
  throw ()
{
  // First search the cache for an existing file descriptor.
  FdCache::OFlag oflused = FdCache::any;
  ShortIdKey key(sid);

  RECORD_TIME_POINT;
  this->mu.lock();
  RECORD_TIME_POINT;

  int fd = this->lookup(key, ofl, oflused);

  // Update hit/miss statistics
  if(fd != -1)
    this->hit_count++;
  else
    this->try_miss_count++;

  this->mu.unlock();
  RECORD_TIME_POINT;

  if(fd != -1)
    {
      assert(oflused != FdCache::any);
      if (oflout) *oflout = oflused;
      Repos::dprintf(DBG_FDCACHE,
		     "FdCache::tryopen 0x%08x found fd %d ofl %d=>%d\n",
		     sid, fd, ofl, oflused);
    }
  return fd;
}

void
FdCache::close(ShortId sid, int fd, FdCache::OFlag ofl) throw ()
{
  pthread_once(&once, FdCache_init);
  unsigned int index = fdCacheIndex(sid);
  assert(fdCaches != NULL);
  fdCaches[index].close(sid, fd, ofl);
}

void
FdCache::PartialCache::close(ShortId sid, int fd, FdCache::OFlag ofl) throw ()
{
  RECORD_TIME_POINT;
  this->mu.lock();
  RECORD_TIME_POINT;
  assert(fd != -1);
  ShortIdKey key(sid);
  FdCache::FdInfo* fdi;
  FdCache::FdInfo* oldfdi;
  if (!this->table.Delete(key, oldfdi, false)) {
    oldfdi = NULL;
  }
  fdi = NEW(FdCache::FdInfo);
  fdi->fd = fd;
  assert(ofl != FdCache::any);
  fdi->ofl = ofl;
  fdi->lastUse = time(NULL);
  fdi->next = &this->lruListHead;
  fdi->prev = this->lruListHead.prev;
  fdi->prev->next = fdi;
  fdi->next->prev = fdi;
  fdi->key = key;
  fdi->more = oldfdi;
  fdi->moreprev = NULL;
  if (oldfdi) oldfdi->moreprev = fdi;
  this->table.Put(key, fdi);
  Repos::dprintf(DBG_FDCACHE, "FdCache::close 0x%08x cached fd %d %s\n",
		 sid, fd, oldfdi ? "(dupe)" : "");
  this->nCached++;
  if (this->nCached > (maxCached / fdCache_count)) {
    fdiDeleteNL(lruListHead.next, "close");
    this->nCached--;
    this->evict_count++;
  }
  this->mu.unlock();
  RECORD_TIME_POINT;
}

void
FdCache::flush(ShortId sid, FdCache::OFlag ofl) throw ()
{
  pthread_once(&once, FdCache_init);
  unsigned int index = fdCacheIndex(sid);
  assert(fdCaches != NULL);
  fdCaches[index].flush(sid, ofl);
}

void
FdCache::PartialCache::flush(ShortId sid, FdCache::OFlag ofl) throw ()
{
  this->mu.lock();
  ShortIdKey key(sid);
  FdCache::FdInfo* fdihead;
  if (this->table.Delete(key, fdihead, false)) {
    FdCache::FdInfo* fdi = fdihead;
    do {
      FdCache::FdInfo* more = fdi->more;
      if (ofl == FdCache::any || ofl == fdi->ofl) {
	fdi->prev->next = fdi->next;
	fdi->next->prev = fdi->prev;
	if (fdi->moreprev != NULL) {
	  fdi->moreprev->more = fdi->more;
	}
	if (fdi->more != NULL) {
	  fdi->more->moreprev = fdi->moreprev;
	}
	if (fdihead == fdi) {
	  fdihead = fdi->more;
	}
	Repos::dprintf(DBG_FDCACHE,
		       "FdCache::flush 0x%08x deleted fd %d ofl %d\n",
		       sid, fdi->fd, fdi->ofl);
	::close(fdi->fd);
	delete fdi;
	this->nCached--;
      }
      fdi = more;
    } while (fdi);
    if (fdihead != NULL) {
      this->table.Put(key, fdihead);
    }
  }
  this->mu.unlock();
}

void
FdCache::getStats(/*OUT*/ Basics::uint32 &n_in_cache,
		  /*OUT*/ Basics::uint64 &hits,
		  /*OUT*/ Basics::uint64 &open_misses,
		  /*OUT*/ Basics::uint64 &try_misses,
		  /*OUT*/ Basics::uint64 &evictions,
		  /*OUT*/ Basics::uint64 &expirations)
{
  // First zero the output parameters, as each PartialCache will add
  // its statistics to them.
  n_in_cache = 0;
  hits = 0;
  open_misses = 0;
  try_misses = 0;
  evictions = 0;
  expirations = 0;

  // Loop over the file descriptor caches, and gather statistics from
  // each one.
  for(unsigned int i = 0; i < fdCache_count; i++)
    {
      fdCaches[i].accumulateStats(n_in_cache,
				  hits, open_misses, try_misses,
				  evictions, expirations);
    }
}

void
FdCache::PartialCache::accumulateStats(/*OUT*/ Basics::uint32 &n_in_cache,
				       /*OUT*/ Basics::uint64 &hits,
				       /*OUT*/ Basics::uint64 &open_misses,
				       /*OUT*/ Basics::uint64 &try_misses,
				       /*OUT*/ Basics::uint64 &evictions,
				       /*OUT*/ Basics::uint64 &expirations)
{
  this->mu.lock();
  n_in_cache += this->nCached;
  hits += this->hit_count;
  open_misses += this->open_miss_count;
  try_misses += this->try_miss_count;
  evictions += this->evict_count;
  expirations += this->expire_count;
  this->mu.unlock();
}
