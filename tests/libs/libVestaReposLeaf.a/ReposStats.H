// Copyright (C) 2004, Kenneth C. Schalk
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

// ReposStats.H - data structures representing statistics about the
// rpeository

#ifndef REPOSSTATS_H
#define REPOSSTATS_H 1

#include <Basics.H>
#include <SRPC.H>
#include "Timers.H"

#ifndef USECS_PER_SEC
#define USECS_PER_SEC 1000000
#endif

namespace ReposStats
{
  enum StatKind
    {
      // 0 can be used when there is no StatKind available.
      invalid = 0,

      // Statistics about the file descriptor cache.
      fdCache,

      // These statistics are across all NFS/SRPC procedures.  At some
      // point in the future, there may also be statistics that are
      // split up by invidual procedure.
      dupeTotal,
      srpcTotal,
      nfsTotal,

      // Repository server memory usage
      memUsage,

      // Statistics about the repository's special-purpose packed heap
      vMemPool,

      // Final enum elemnt is used as a marker of the end of allowable
      // statistics.
      statKindEnd
    };

  class Stat
  {
  public:
    virtual void send(SRPC *srpc) const throw (SRPC::failure) = 0;
    virtual void recv(SRPC *srpc) throw (SRPC::failure) = 0;
  };

  // Statistics for the file descriptor cache.
  class FdCacheStats : public Stat
  {
  public:
    Basics::uint32 n_in_cache;
    Basics::uint64 hits;
    Basics::uint64 open_misses;
    Basics::uint64 try_misses;
    Basics::uint64 evictions;
    Basics::uint64 expirations;

    FdCacheStats()
      : n_in_cache(0), hits(0), open_misses(0), try_misses(0),
	evictions(0), expirations(0)
    { }
    FdCacheStats(const FdCacheStats &other)
      : n_in_cache(other.n_in_cache), hits(other.hits),
	open_misses(other.open_misses), try_misses(other.try_misses),
	evictions(other.evictions), expirations(other.expirations)
    { }
    // No destructor needed
    FdCacheStats& operator=(const FdCacheStats &other)
    {
      n_in_cache = other.n_in_cache;
      hits = other.hits;
      open_misses = other.open_misses;
      try_misses = other.try_misses;
      evictions = other.evictions;
      expirations = other.expirations;
      return *this;
    }

    // Allow for subtraction to compute differences in a time
    // interval.
    inline FdCacheStats operator-(const FdCacheStats &other) const
    {
      FdCacheStats result;
      // Note: we just pass on our n_in_cache, as it's not a
      // cumulative count over the life of the repository.
      result.n_in_cache  = n_in_cache;
      result.hits        = hits        - other.hits;
      result.open_misses = open_misses - other.open_misses;
      result.try_misses  = try_misses  - other.try_misses;
      result.evictions   = evictions   - other.evictions;
      result.expirations = expirations - other.expirations;
      return result;
    }

    void send(SRPC *srpc) const throw (SRPC::failure);
    void recv(SRPC *srpc) throw (SRPC::failure);
  };

  // Statistics about duplicate suppression.
  class DupeStats : public Stat
  {
  public:
    Basics::uint64 non, inProcess, completed;
    DupeStats()
      : non(0), inProcess(0), completed(0)
    { }
    DupeStats(const DupeStats &other)
      : non(other.non), inProcess(other.inProcess), completed(other.completed)
    { }
    // No destructor needed
    DupeStats &operator=(const DupeStats &other)
    {
      non = other.non;
      inProcess = other.inProcess;
      completed = other.completed;
      return *this;
    }

    inline Basics::uint64 total() const
    {
      return non+inProcess+completed;
    }
    inline void zero()
    {
      non = 0;
      inProcess = 0;
      completed = 0;
    }
    inline DupeStats &operator+=(const DupeStats &other)
    {
      non += other.non;
      inProcess += other.inProcess;
      completed += other.completed;
      return *this;
    }
    inline DupeStats operator-(const DupeStats &other) const
    {
      DupeStats result;
      result.non = non - other.non;
      result.inProcess = inProcess - other.inProcess;
      result.completed = completed - other.completed;
      return result;
    }

    void send(SRPC *srpc) const throw (SRPC::failure);
    void recv(SRPC *srpc) throw (SRPC::failure);
  };

  // This class is used to represent statistics for both SRPC calls
  // and NFS calls.
  class TimedCalls : public Stat
  {
  public:
    // Number of calls completed
    Basics::uint64 call_count;
    // Elapsed time over all calls
    Basics::uint64 elapsed_secs;
    Basics::uint32 elapsed_usecs;

    TimedCalls()
      : call_count(0), elapsed_secs(0), elapsed_usecs(0)
    { }
    TimedCalls(const TimedCalls &other)
      : call_count(other.call_count),
	elapsed_secs(other.elapsed_secs),
	elapsed_usecs(other.elapsed_usecs)
    { }
    TimedCalls &operator=(const TimedCalls &other)
    {
      call_count = other.call_count;
      elapsed_secs = other.elapsed_secs;
      elapsed_usecs = other.elapsed_usecs;
      return *this;
    }

    inline TimedCalls operator-(const TimedCalls &other) const
    {
      TimedCalls result;
      result.call_count = call_count - other.call_count;
      if(elapsed_secs == other.elapsed_secs)
	{
	  result.elapsed_secs = 0;
	  assert(elapsed_usecs >= other.elapsed_usecs);
	  result.elapsed_usecs = elapsed_usecs - other.elapsed_usecs;
	}
      else
	{
	  assert(elapsed_secs > other.elapsed_secs);
	  result.elapsed_secs = (elapsed_secs - other.elapsed_secs) - 1;
	  result.elapsed_usecs = ((elapsed_usecs + USECS_PER_SEC) -
				  other.elapsed_usecs);
	  if(result.elapsed_usecs > USECS_PER_SEC)
	    {
	      result.elapsed_secs += result.elapsed_usecs/USECS_PER_SEC;
	      result.elapsed_usecs %= USECS_PER_SEC;
	    }
	}
      return result;
    }

    void send(SRPC *srpc) const throw (SRPC::failure);
    void recv(SRPC *srpc) throw (SRPC::failure);
  };

  class MemStats : public Stat
  {
  public:
    Basics::uint64 total, resident;
    MemStats()
      : total(0), resident(0)
    { }
    MemStats(Basics::uint64 t, Basics::uint64 r)
      : total(t), resident(r)
    { }
    MemStats(const MemStats &other)
      : total(other.total), resident(other.resident)
    { }
    // No destructor needed
    MemStats &operator=(const MemStats &other)
    {
      total = other.total;
      resident = other.resident;
      return *this;
    }

    void send(SRPC *srpc) const throw (SRPC::failure);
    void recv(SRPC *srpc) throw (SRPC::failure);
  };

  class VMemPoolStats : public Stat
  {
  public:
    // Total size of the VMemPool heap
    Basics::uint32 size;

    // Current free number of blocks on the free list and total number
    // of bytes in those blocks
    Basics::uint32 free_list_blocks, free_list_bytes;

    // Current number of bytes wasted for alignment of blocks.
    Basics::uint32 free_wasted_bytes;

    // Current number of non-empty free lists
    Basics::uint8 nonempty_free_list_count;

    // Total number of calls to VMemPool::allocate, total number of
    // calls to VMemPool::free, total number of calls to
    // VMemPool::grow.
    Basics::uint64 allocate_calls, free_calls, grow_calls;

    // Total number of blocks rejected when looking for a suitable one
    // on the free list.  Some are rejected for being too small, some
    // for being too large.
    Basics::uint64 allocate_rej_sm_blocks, allocate_rej_lg_blocks;

    // Total number of blocks split by VMemPool::allocate (i.e. a
    // block larger than the requested size was used to satisfy the
    // allocation resulting in another smaller free block plus the
    // allocation block).
    Basics::uint64 allocate_split_blocks;

    // Total number of new blocks created by expanded the heap.  (Only
    // some expansions call VMemPool::grow.)
    Basics::uint64 allocate_new_blocks;

    // Total number of times that VMemPool::free was able to combine a
    // free block with free space either before or after the freed
    // block.
    Basics::uint64 free_coalesce_before, free_coalesce_after;

    // The total amount of time spent in all calls to
    // VMemPool::allocate and VMemPool::free
    Timers::Elapsed allocate_time, free_time;

    VMemPoolStats()
      : size(0),
	free_list_blocks(0), free_list_bytes(0), free_wasted_bytes(0),
	nonempty_free_list_count(0),
	allocate_calls(0), free_calls(0), grow_calls(0),
	allocate_rej_sm_blocks(0), allocate_rej_lg_blocks(0),
	allocate_split_blocks(0), allocate_new_blocks(0),
	free_coalesce_before(0), free_coalesce_after(0)
    { }
    VMemPoolStats(const VMemPoolStats &other)
      : size(other.size),
	free_list_blocks(other.free_list_blocks),
	free_list_bytes(other.free_list_bytes),
	free_wasted_bytes(other.free_wasted_bytes),
	nonempty_free_list_count(other.nonempty_free_list_count),
	allocate_calls(other.allocate_calls),
	allocate_rej_sm_blocks(other.allocate_rej_sm_blocks),
	allocate_rej_lg_blocks(other.allocate_rej_lg_blocks),
	allocate_split_blocks(other.allocate_split_blocks),
	allocate_new_blocks(other.allocate_new_blocks),
	allocate_time(other.allocate_time),
	free_calls(other.free_calls),
	free_coalesce_before(other.free_coalesce_before),
	free_coalesce_after(other.free_coalesce_after),
	free_time(other.free_time),
	grow_calls(other.grow_calls)
    { }
    // No destructor needed
    VMemPoolStats &operator=(const VMemPoolStats &other)
    {
      size = other.size;
      free_list_blocks = other.free_list_blocks;
      free_list_bytes = other.free_list_bytes;
      free_wasted_bytes = other.free_wasted_bytes;
      nonempty_free_list_count = other.nonempty_free_list_count;
      allocate_calls = other.allocate_calls;
      allocate_rej_sm_blocks = other.allocate_rej_sm_blocks;
      allocate_rej_lg_blocks = other.allocate_rej_lg_blocks;
      allocate_split_blocks = other.allocate_split_blocks;
      allocate_new_blocks = other.allocate_new_blocks;
      allocate_time = other.allocate_time;
      free_calls = other.free_calls;
      free_coalesce_before = other.free_coalesce_before;
      free_coalesce_after = other.free_coalesce_after;
      free_time = other.free_time;
      grow_calls = other.grow_calls;

      return *this;
    }

    inline VMemPoolStats operator-(const VMemPoolStats &other) const
    {
      VMemPoolStats result;

      // Just propagate the instantaneous stats: size,
      // free_list_blocks, free_list_bytes, free_wasted_bytes,
      // nonempty_free_list_count.
      result.size = size;
      result.free_list_blocks = free_list_blocks;
      result.free_list_bytes = free_list_bytes;
      result.free_wasted_bytes = free_wasted_bytes;
      result.nonempty_free_list_count = nonempty_free_list_count;

      // Compute the difference for the others.
      result.allocate_calls = allocate_calls - other.allocate_calls;
      result.allocate_rej_sm_blocks = (allocate_rej_sm_blocks -
				       other.allocate_rej_sm_blocks);
      result.allocate_rej_lg_blocks = (allocate_rej_lg_blocks -
				       other.allocate_rej_lg_blocks);
      result.allocate_split_blocks = (allocate_split_blocks -
				      other.allocate_split_blocks);
      result.allocate_new_blocks = (allocate_new_blocks -
				    other.allocate_new_blocks);
      result.allocate_time = allocate_time - other.allocate_time;

      result.free_calls = free_calls - other.free_calls;
      result.free_coalesce_before = (free_coalesce_before -
				     other.free_coalesce_before);
      result.free_coalesce_after = (free_coalesce_after -
				    other.free_coalesce_after);
      result.free_time = free_time - other.free_time;

      result.grow_calls = grow_calls - other.grow_calls;

      return result;
    }

    void send(SRPC *srpc) const throw (SRPC::failure);
    void recv(SRPC *srpc) throw (SRPC::failure);
  };

}

#endif // REPOSSTATS_H
