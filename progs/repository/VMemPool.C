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
// VMemPool.C
//
// Memory pool for Vesta repository directory structure.  Confined to
// a range of addresses that can be represented in 32 bits.

// High-level garbage collection and checkpointing code is here.

// The free list is staticall partitioned by powers of 2 (i.e. one for
// blocks of 16-31 bytes, one for blocks of 32-64 bytes, and so on).
// Each free list is a circularly linked list with a cursor pointing
// into it.  The algorithm used to satisfy allocation requets starts
// with the free list corresponding to the requested size and moves on
// to the free lists for larger sizes if no usable block is found.
// (If no usable block can be found after examining all the free
// lists, a new block is made from currently unused memory.)  To
// select a block from a free list, we start by searching for the
// first block large enough to satisfy the request (the "first fit"
// algorithm).  The block is split if it was larger than the requested
// size.  However, to avoid the fragmentation caused by splitting
// blocks we first examine the following block on the free list and
// advance as long as it is a better fit.  (This algorithm might be
// called "best local fit".)

// A block on the free list may not be smaller than FREEBK_MINSIZE,
// so: (1) Smaller sized allocates (and frees) are rounded up.  (2) If
// splitting a block would leave a too-small fragment, the fragment is
// filled with "freeByte" type codes and left off the free list.  (3)
// If gc or checkpointing compacts a block to less than FREEBK_MINSIZE
// and it is later freed, again the fragment is filled with "freeByte"
// type codes and left off the free list.  Such fragments are
// reclaimed by the next gc or checkpoint.

// Each allocated or freed block is aligned to a particular number of
// bytes (currently 8).  Alignment is checked and ensured using
// VMemPool::alignmentMask.  When a block of a size not evenly
// divisble by the alignment is allocated, extra bytes are filled with
// "freeByte" type codes.  If the block is later freed these will be
// reabsorbed into the freed block.

// A freed block is combined with adjacent free space to reduce
// fragmentation.  Combining with following space simply requires
// examining the type code of the byte immediately following the freed
// block and combining with and "freeByte" or "freeBlock" type codes.
// Combining with preceding free space uses a table that maps from the
// end of each free block to its beginning (free_space_origins below).
// This table must be updated every time space is allocated or freed
// and whenever free space is combined.

#include <sys/types.h>
#if __linux__
#include <stdint.h>
#endif
#include <stdlib.h>
#include <sys/mman.h>
#include "Basics.H"
#include "VMemPool.H"
#include "VestaSource.H"
#include "VestaConfig.H"
#include "logging.H"
#include "DirShortId.H"
#include "VDirChangeable.H"
#include "VestaAttribsRep.H"
#include "FPShortId.H"
#include <Thread.H>
#include "DebugDumpHelp.H"
#include "Timers.H"
#include <Generics.H>
#include <BufStream.H>
#include <algorithm>
#include <Table.H>
#include <SimpleKey.H>
#include <Sequence.H>

using std::fstream;
using std::hex;
using std::dec;
using std::endl;
using std::streampos;

// Internal block types.
// See VDirChangeable.H for an explanation of the notation.
//
// packed struct VMemPoolFreeByte {
//     Bit8 clientBits: 4;           // Always 0.
//     Bit8 type: 4;                 // Must be VMemPool::freeByte
// };
//
// packed struct VMemPoolFreeBlock {
//     Bit8 clientBits: 4;           // Always 0.
//     Bit8 type: 4;                 // Must be VMemPool::freeBlock
//     Bit32 length;
//     VMemPoolFreeBlock@ next;
//     VMemPoolFreeBlock@ prev;
// };
//

// Offsets in packed struct
#define FREEBK_FLAGS 0
#define FREEBK_LENGTH 1
#define FREEBK_NEXT 5
#define FREEBK_PREV 9
#define FREEBK_MINSIZE 13

// Short pointer to a VMemPool block, with methods for common fields
// and fields in freed blocks.
class VMemPoolBlockSP : VMemPool {
  public:
    Bit32 sp;
    inline VMemPool::typeCode type() {
	return (VMemPool::typeCode)
	  ((base[sp - 1 + FREEBK_FLAGS] >> 4) & 0xf);
    }; 
    inline void setType(VMemPool::typeCode val)
      { base[sp - 1 + FREEBK_FLAGS] = ((int) val) << 4; };
    inline Bit32 length() {
	Bit32 val;
	memcpy(&val, &base[sp - 1 + FREEBK_LENGTH], sizeof(Bit32));
	return val;
    };
    inline void setLength(Bit32 val) {
	memcpy(&base[sp - 1 + FREEBK_LENGTH], &val, sizeof(Bit32));
    };
    inline VMemPoolBlockSP next() {
	VMemPoolBlockSP val;
	memcpy(&val.sp, &base[sp - 1 + FREEBK_NEXT], sizeof(Bit32));
	return val;
    };
    inline void setNext(VMemPoolBlockSP val) {
	memcpy(&base[sp - 1 + FREEBK_NEXT], &val.sp, sizeof(Bit32));
    };
    inline VMemPoolBlockSP prev() {
	VMemPoolBlockSP val;
	memcpy(&val.sp, &base[sp - 1 + FREEBK_PREV], sizeof(Bit32));
	return val;
    };
    inline void setPrev(VMemPoolBlockSP val) {
	memcpy(&base[sp - 1 + FREEBK_PREV], &val.sp, sizeof(Bit32));
    };
    inline friend int operator==(VMemPoolBlockSP a, VMemPoolBlockSP b)
      { return a.sp == b.sp; };
    inline friend int operator!=(VMemPoolBlockSP a, VMemPoolBlockSP b)
      { return a.sp != b.sp; };
    inline VMemPoolBlockSP() { };
    inline VMemPoolBlockSP(Bit32 sp) { this->sp = sp; };
};

// This turns on some extra checking which may be useful when
// modifying VMemPool but spends extra time.
// #define PERFORM_EXTRA_CHECKS 1

// A Table key for short pointers and a table that maps from short
// pointer to short pointer.
typedef SimpleKey<Bit32> SPKey;
typedef Table<SPKey, Bit32>::Default SPSPTable;
typedef Table<SPKey, Bit32>::Iterator SPSPIter;

// A table that maps from the end of a free region to the beginning of
// a free region
class BlockOriginTable : public SPSPTable
{
public:
  BlockOriginTable() { }
  BlockOriginTable(const BlockOriginTable *other)
    : Table<SPKey, Bit32>::Default(other)
  { }
  bool Put(Bit32 k, Bit32 v)
  {
    assert(v < k);
    assert(((v - 1) & VMemPool::alignmentMask) == 0);
    assert(((k - v) & VMemPool::alignmentMask) == 0);
#if defined(PERFORM_EXTRA_CHECKS)
    {
      VMemPoolBlockSP block;
      block.sp = v;
      if((k - v) < FREEBK_MINSIZE)
	{
	  while(block.sp < k)
	    {
	      assert(block.type() == VMemPool::freeByte);
	      block.sp++;
	    }
	}
      else
	{
	  assert(block.type() == VMemPool::freeBlock);
	  assert(block.length() == (k - v));
	}
    }
#endif
    return Table<SPKey, Bit32>::Default::Put(k, v);
  }
};

Sequence<VMemPoolBlockSP> free_lists;

// We don't need free lists for anything smaller than FREEBK_MINSIZE.
// free_lists[0] corresponds to the free list for blocks of at least
// 2^free_list_index_offset bytes.
static int free_list_index_offset = 0;

const VMemPoolBlockSP SNULL(0);

// Module globals
static pthread_once_t once = PTHREAD_ONCE_INIT;
static Basics::mutex mu;
static long page_size = 0;
Bit8* VMemPool::base = 0;
Bit32 VMemPool::totalSize;
Bit32 VMemPool::minGrowBy;
static VMemPoolBlockSP unusedMem;
static VMemPoolBlockSP unusedMemEnd;
static struct {
    VMemPool::markCallback markcb;
    void* markcl;
    VMemPool::sweepCallback sweepcb;
    void* sweepcl;
    VMemPool::rebuildCallback rebuildcb;
    void* rebuildcl;

    // Statistical information; examine in debugger.
    // Not saved in a checkpoint; accurate only
    // if there has been at least one sweep (source
    // weed) since recovery from a checkpoint.
    Bit32 totalSize;    
} callback[VMemPool::maxTypeCodes];
static const int CkptMinVersion = 4;
static const int CkptMaxVersion = 11;  // version 11 adds (vroot ...)
static int currentCkptVersion;
int VMemPool::alignmentMask;

// Soft limit on the largest allowed VMemPool total size.  Can be
// changed with a configuration setting.  Intended to provide ample
// warning before hitting the hard limit.
Bit32 VMemPool_totalSize_soft_limit = 0xf0000000;

// Statistics:
//
//   free_list_blocks - Number of blocks in the free list
//
//   free_list_bytes - Number of bytes in the free list
//
//   allocate_calls - Number of calls to VMemPool::allocate
//
//   allocate_rej_sm_blocks - Number of blocks on the free list
//   skipped over in VMemPool::allocate because they were too small
//
//   allocate_rej_lg_blocks - Number of blocks on the free list
//   skipped over in VMemPool::allocate because they were larger than
//   needed and the next block on the free list was a better fit
//
//   allocate_split_blocks - Number of blocks that were split to
//   satisfy allocation requests (resulting in another smaller block
//   being put back on the free list).
//
//   allocate_new_blocks - Number of new blocks created by expanding
//   the heap.  (Some require calls to VMemPool::grow but many don't.)
//
//   allocate_time - Total time spent in VMemPool::allocate
//
//   free_calls - Number of calls to VMemPool::free
//
//   free_coalesce_after - Number of time that a freed block was
//   combined with a free block following it in memory
//
//   free_coalesce_before - Number of time that a freed block was
//   combined with a free block preceding it in memory
//
//   free_time - Total time spent in VMemPool::free
//
//   grow_calls - Number of calls to VMemPool::grow
//
static Basics::uint32 free_list_blocks = 0, free_list_bytes = 0;
static Basics::uint64 allocate_calls = 0, free_calls = 0, grow_calls = 0;
static Basics::uint64 allocate_rej_sm_blocks = 0, allocate_rej_lg_blocks = 0, allocate_split_blocks = 0, allocate_new_blocks = 0;
static Basics::uint64 free_coalesce_before = 0, free_coalesce_after = 0;

static Timers::Elapsed allocate_time, free_time;

// Histogram of block sizes allocated
static IntIntTbl *alloc_size_histo = 0;

// This table maps from the short pointer immediatley following a free
// block to the beginning of a free block.  This makes it possible to
// coalesce with preceding free space whenever a block is freed.
BlockOriginTable free_space_origins;

#if defined(PERFORM_EXTRA_CHECKS)
// Paranoid consistency check of the free list.  Useful when making
// changes to VMemPool (though may need to be updated with other
// changes).  Returns true if all is well, so
// "asseert(check_free_list());" is one way to run the check.  If the
// argument is true, a representation of the free list will be printed
// even if nothing seems to be wrong.

// Note that the caller must lock mu above before calling this
// function.
static bool check_free_list(bool print_on_success = false)
{
  SPSPTable free_space_origins_copy((const SPSPTable *) &free_space_origins);

  Basics::OBufStream debug_info;

  bool fail = false;
  VMemPoolBlockSP block;
  for(int i = 0; i < free_lists.size(); i++)
    {
      VMemPoolBlockSP cursor = free_lists.get(i);

      if(cursor == SNULL) continue;

      Bit32 lo_size = (1<<(i+free_list_index_offset)),
	hi_size = ((1<<(i+free_list_index_offset+1))-1);
      debug_info << "Free list " << i
		 << " (sizes " << lo_size
		 << " - " << hi_size
		 << "):" << endl;

      SPSPTable visited;

      block = cursor;
      do
	{
	  Bit32 len = block.length();
	  Bit32 end = (block.sp+len);
	  debug_info << "\t" << block.sp
		     << " - " << end
		     << " (" << len << " bytes)";
	  Bit32 origin;
	  bool inTbl = free_space_origins_copy.Delete(end, origin);
	  if(!inTbl)
	    {
	      debug_info << " [missing origin]";
	      fail = true;
	    }
	  else if(origin != block.sp)
	    {
	      debug_info << " [incorrect origin: " << origin << "]";
	      fail = true;
	    }
	  if(block.next().prev() != block)
	    {
	      debug_info << " [next.prev broken: "
			 << block.next().prev().sp << "]";
	      fail = true;
	    }
	  if(block.prev().next() != block)
	    {
	      debug_info << " [prev.next broken: "
			 << block.prev().next().sp << "]";
	      fail = true;
	    }
	  if(len < lo_size)
	    {
	      debug_info << " [too small for this free list]";
	      fail = true;
	    }
	  else if(len > hi_size)
	    {
	      debug_info << " [too big for this free list]";
	      fail = true;
	    }
	  debug_info << endl;

	  if(visited.Put(block.sp, 1))
	    {
	      debug_info << "\t[cycle detected but not at end]"
			 << endl;
	      fail = true;
	      break;
	    }

	  // Move on to the next block
	  block = block.next();
	}
      while(block != cursor);
    }

  if(free_space_origins_copy.Size() > 0)
    {
      debug_info << "Origins not matching any block on free list:" << endl;
      SPSPIter it(&free_space_origins_copy);
      SPKey end;
      while(it.Next(end, block.sp))
	{
	  Bit32 len = ((Bit32) end) - block.sp;
	  debug_info << "\t" << block.sp
			     << " - " << end
			     << " (" << len << " bytes)";
	  if(block.type() != VMemPool::freeByte)
	    {
	      if(block.type() == VMemPool::freeBlock)
		{
		  debug_info << " [is a freeBlock";
		  if(block.length() != len)
		    {
		      debug_info << " with different length: "
				 << block.length();
		    }
		  debug_info << "]";
		}
	      else
		{
		  debug_info << " [doesn't start with a freeByte]";
		}
	      fail = true;
	    }
	  debug_info << endl;
	}
    }

  if(fail)
    {
      Repos::dprintf(DBG_ALWAYS,
		     "FATAL: free list check failed:\n%s",
		     debug_info.str());
    }
  else if(print_on_success)
    {
      Repos::dprintf(DBG_ALWAYS, "%s",
		     debug_info.str());
    }
  return !fail;
}
#endif

// Set the prevailing checkpoint version: if writing a new checkpoint,
// the latest version; else if recovered from a checkpoint, the
// version recovered from; else the latest version.
static void
setCurrentCkptVersion(int ckptVersion)
{
  currentCkptVersion = ckptVersion;
  if (ckptVersion >= 7) {
    // Note: the alignment code in version 7 was buggy; if you can't
    // recover from a version 7 checkpoint due to assertion failures,
    // delete it and recover from the most recent version 6 or 5
    // checkpoint instead.
    VMemPool::alignmentMask = 7;
  } else if (ckptVersion == 6) {
    VMemPool::alignmentMask = 3;
  } else {
    VMemPool::alignmentMask = 0;
  }
}

extern "C"
{
  void VMemPool_init()
  {
    VMemPool::init_inner();
  }
}

void VMemPool::init()
{
  pthread_once(&once, VMemPool_init);
}

// Find which free list a particular size belongs in.
static inline int free_list_index(Bit32 size)
{
  assert(size >= FREEBK_MINSIZE);
  size >>= free_list_index_offset;

  int result = 0;
  while(size > 1)
    {
      size >>= 1;
      result++;
    }
  assert(size == 1);
  return result;
}

// Reset all free lists to be empty.
static void reset_free_lists()
{
  for(int i = 0; i < free_lists.size(); i++)
    {
      free_lists.put(i, SNULL);
    }
}

// Remove a block from the free list.  If no_origin is true, the
// caller has already removed the block from the "free_space_origins"
// table.  If the caller knows the free list index of the block it
// should be passed in "fl_index".
static void remove_from_free_list(VMemPoolBlockSP block,
				  bool no_origin = false,
				  int fl_index = -1)
{
  assert(block.type() == VMemPool::freeBlock);
  Bit32 length = block.length();
  assert(length > FREEBK_MINSIZE);

  // Compute the free-list index if the caller didn't know it
  if(fl_index < 0)
    {
      fl_index = free_list_index(length);
    }

  assert(fl_index < free_lists.size());
  VMemPoolBlockSP &cursor = free_lists.get_ref(fl_index);

  assert(cursor != SNULL);

#if defined(PERFORM_EXTRA_CHECKS)
  // Make sure that this block is actually on this free list
  {
    VMemPoolBlockSP p = cursor;
    bool found = false;
    do
      {
	if(p == block) found = true;
	p = p.next();
      }
    while(!found && (p != cursor));
    assert(found);
  }
#endif

  if(block.prev() == block)
    {
      // Free list was of length 1
      assert(block.next() == block);
      assert(cursor == block);
      cursor = SNULL;

      // We just cleared this free list, so remove empty free lists if
      // possible.  (This can speed up allocations slightly.)
      while((free_lists.size() > 0) &&
	    (free_lists.gethi() == SNULL))
	free_lists.remhi();
    }
  else
    {
      VMemPoolBlockSP prev = block.prev();
      VMemPoolBlockSP next = block.next();
      assert(prev.next() == block);
      assert(next.prev() == block);
      prev.setNext(next);
      next.setPrev(prev);
      if(cursor == block) cursor = prev;
    }

  if(!no_origin)
    {
      Bit32 start;
      bool inTbl =
	free_space_origins.Delete((block.sp+length), start,
				  /*resize=*/false);
#if defined(PERFORM_EXTRA_CHECKS)
      if(!inTbl) check_free_list(true);
#endif
      assert(inTbl);
      assert(start == block.sp);
    }
#if defined(PERFORM_EXTRA_CHECKS)
  // The caller says that this block has already been removed from the
  // origin table.  Make sure that's true.
  else
    {
      Bit32 start;
      bool inTbl =
	free_space_origins.Delete((block.sp+length), start,
				  /*resize=*/false);
      assert(!inTbl);
    }
#endif

  free_list_blocks--;
  free_list_bytes -= length;

  callback[(int)VMemPool::freeBlock].totalSize -= length;
}

// Add a block to the free list.
static void add_to_free_list(VMemPoolBlockSP block)
{
  assert(block.type() == VMemPool::freeBlock);
  Bit32 length = block.length();
  assert(length > FREEBK_MINSIZE);

  unsigned int fl_index = free_list_index(length);

  while(fl_index >= free_lists.size())
    {
      free_lists.addhi(SNULL);
    }
  VMemPoolBlockSP &cursor = free_lists.get_ref(fl_index);

  if(cursor == SNULL)
    {
      block.setNext(block);
      block.setPrev(block);
      cursor = block;
    }
  else
    {
      VMemPoolBlockSP next = cursor.next();
      block.setNext(next);
      block.setPrev(cursor);
      cursor.setNext(block);
      next.setPrev(block);
    }

  bool inTbl = free_space_origins.Put(block.sp+length, block.sp);
  assert(!inTbl);

  free_list_blocks++;
  free_list_bytes += length;

  callback[(int)VMemPool::freeBlock].totalSize += length;
}

void*
VMemPool::allocate(VMemPool::typeCode type, Bit32 size) throw ()
{
    VMemPoolBlockSP p;
    Bit32 asize = size;

    // VMemPool should already be initialized
    assert(page_size != 0);

    // Get the time that this call started
    Timers::IntervalRecorder call_timer;

    // Round up to minimum size that can be put on free list.
    // Note: a sweep or checkpoint can still make a block
    // too small to be put on the free list.
    if (asize < FREEBK_MINSIZE) {
	asize = FREEBK_MINSIZE;
    }

    // Round up to the required alignment
    // Assumes 2's complement negation.
    asize += (-asize) & VMemPool::alignmentMask;
    mu.lock();

#if defined(PERFORM_EXTRA_CHECKS)
    assert(check_free_list());
#endif

    //Repos::dprintf(DBG_VMEMPOOL, "allocate(%d, %u) ", (int) type, size);

    for(unsigned int fl_index = free_list_index(asize);
	fl_index < free_lists.size();
	fl_index++)
      {
	VMemPoolBlockSP freedMemCursor = free_lists.get(fl_index);
	if(freedMemCursor == SNULL) continue;

	VMemPoolBlockSP prev = freedMemCursor;
	p = prev.next();
	do {
	    assert(prev == p.prev());
	    Bit32 p_len = p.length();
	    if (p_len >= asize) {
		// Found a block
		Bit32 excess = p_len - asize;
		assert((excess & VMemPool::alignmentMask) == 0);

		// If this block is larger than we need, peek at
		// following blocks and advance if they're smaller and
		// still large enough.  (In the terminology of memory
		// allocators this might be called "best local fit".
		// We started by finding the first block large enough
		// to satisfy the allocation, and if we stopped there
		// that would be the "first fit" algortihm.  We're
		// going to look for a better fit, but we're not going
		// to try too hard so it's not quite the "best fit"
		// algorithm.)
		while(excess > 0)
		  {
		    VMemPoolBlockSP pnext = p.next();
		    Bit32 pnext_len = pnext.length();
		    // If the next block isn't big enough or isn't
		    // smaller than this one, exit the loop
		    if((pnext_len < asize) ||
		       (pnext_len >= p_len))
		      {
			break;
		      }

		    // Use this block instead to reduce splitting.
		    prev = p;
		    p = pnext;
		    p_len = pnext_len;
		    excess = pnext_len - asize;

		    // This block was rejected because it was too
		    // large (and we saw a better fit nearby)
		    allocate_rej_lg_blocks++;
		  }

		// We'll use or split this block, so remove it from
		// the free list.
		remove_from_free_list(p, false, fl_index);

		if (excess < FREEBK_MINSIZE) {
		    // Exact size, or nearly
		    asize = p_len;
		    assert((asize & VMemPool::alignmentMask) == 0);
		} else {
		    // Larger block, need to split
		    VMemPoolBlockSP q;
		    q.sp = p.sp + asize;
		    q.setType(VMemPool::freeBlock);
		    q.setLength(excess);

		    add_to_free_list(q);

		    allocate_split_blocks++;
		}
		goto finish;
	    }
	    prev = p;
	    p = p.next();
	    // This block was rejected because it was too small to
	    // satisfy this allocation request.
	    allocate_rej_sm_blocks++;
	} while (prev != freedMemCursor);
	// fall though => couldn't find a big enough block on this
	// free list
    }
    // Make a new block in the previously unused area
    p = unusedMem;
    if (unusedMemEnd.sp - unusedMem.sp <= asize) {
	// Grow pool by (at least) the amount needed
	grow(asize - (unusedMemEnd.sp - unusedMem.sp));
    }
    unusedMem.sp += asize;
    allocate_new_blocks++;

  finish:
    callback[(int)type].totalSize += size;
    callback[(int)VMemPool::freeBlock].totalSize -= asize;
    if(size < asize)
      {
	VMemPoolBlockSP remainder(p.sp + size);

	// Mark as free bytes until we get to an alignment byte
	while ((size < asize) &&
	       ((size & VMemPool::alignmentMask) != 0)) {
	  remainder.setType(VMemPool::freeByte);
	  callback[(int)VMemPool::freeByte].totalSize++;
	  remainder.sp++;
	  size++;
	}

	// If there's still more left
	if(size < asize)
	  {
	    if((asize - size) < FREEBK_MINSIZE)
	      {
		Bit32 free_start = remainder.sp;

		// Mark the remainder as free bytes
		while (size < asize) {
		  remainder.setType(VMemPool::freeByte);
		  callback[(int)VMemPool::freeByte].totalSize++;
		  remainder.sp++;
		  size++;
		}
		assert(remainder.sp == p.sp + asize);

		// Remember this for coalescing later.  (Even if it's too
		// small to be a free block it could be pulled into a
		// following block without being unaligned.)
		bool inTbl = free_space_origins.Put(remainder.sp, free_start);
		assert(!inTbl);
	      }
	    else
	      {
		// This case is a little odd, but can happen if the
		// needed size is less than FREEBK_MINSIZE and we find
		// a free block that's larger than FREEBK_MINSIZE but
		// smaller than 2*FREEBK_MINSIZE.

		remainder.setType(VMemPool::freeBlock);
		remainder.setLength(asize-size);

		add_to_free_list(remainder);

		allocate_split_blocks++;

		asize = size;
	      }
	  }
      }
    p.setType(type);
    allocate_calls++;

    // Record size of this allocation if we're keeping an allocation
    // size histogram
    if(alloc_size_histo != 0)
      {
	int count = 0;
	(void) alloc_size_histo->Get(asize, count);
	count++;
	(void) alloc_size_histo->Put(asize, count);
      }

#if defined(PERFORM_EXTRA_CHECKS)
    assert(check_free_list());
#endif

    // Record the time taken by this call
    allocate_time += call_timer.get_delta();

    mu.unlock();

    void *ret = (void*) VMemPool::lengthenPointer(p.sp);
    assert((((PointerInt)ret) & VMemPool::alignmentMask) == 0);
    //Repos::dprintf(DBG_VMEMPOOL, "returns 0x%016x\n", ret);

    return ret;
}

void
VMemPool::free(void* addr, Bit32 size, typeCode type) throw ()
{
    // VMemPool should already be initialized
    assert(page_size != 0);

    // Get the time that this call started
    Timers::IntervalRecorder call_timer;

    VMemPoolBlockSP p;
    p.sp = VMemPool::shortenPointer(addr);
    assert((((PointerInt)(addr)) & VMemPool::alignmentMask) == 0);
    if (type != unspecified) {
	assert(p.type() == type);
    }
    mu.lock();

#if defined(PERFORM_EXTRA_CHECKS)
    assert(check_free_list());
#endif

    //Repos::dprintf(DBG_VMEMPOOL, "free(0x%016x, %d) ", addr, size);

    if (type != unspecified) {
	assert(p.type() == type);
    }
    callback[(int)p.type()].totalSize -= size;
    // Find size to free by merging in following freeBytes
    Bit32 asize = size;
    VMemPoolBlockSP q;
    q.sp = p.sp + asize;
    while (q.sp < unusedMem.sp &&
	   ((q.type() == VMemPool::freeByte) ||
	    (q.type() == VMemPool::freeBlock))) {
      if(q.type() == VMemPool::freeByte)
	{
	  q.sp++;
	  asize++;
	  callback[(int)VMemPool::freeByte].totalSize--;

	  // If this is an aligned byte, there may be an entry in
	  // free_space_origins for this position that we should
	  // remove as we're absorbing these free bytes
	  Bit32 start;
	  if(
#if !defined(PERFORM_EXTRA_CHECKS)
	     (((q.sp - 1) & VMemPool::alignmentMask) == 0) &&
#endif
	     free_space_origins.Delete(q.sp, start,
				       /*resize=*/false))
	    {
#if defined(PERFORM_EXTRA_CHECKS)
	      assert(((q.sp - 1) & VMemPool::alignmentMask) == 0);
#endif
	      // If so then the origin must be within the new free
	      // block
	      assert(start > p.sp);
	      assert(start < (p.sp + asize));

	      // We're combining with a following free region.  (Note
	      // that we don't count every free byte as any block of
	      // an unaligned size will combine with some free bytes
	      // that follow it to reach the alignment boundary.)
	      free_coalesce_after++;
	    }
	}
      else
	{
	  // We're combining with a following block
	  free_coalesce_after++;

	  assert(q.type() == VMemPool::freeBlock);

	  remove_from_free_list(q);

	  Bit32 q_len = q.length();
	  asize += q_len;
	  q.sp += q_len;
	}
    }
    // Merge in any preceding free blocks.  (Really there should be at
    // most one.)
    while(free_space_origins.Delete(p.sp, q.sp, /*resize=*/false))
      {
	// We're combining with space before this freed block.
	free_coalesce_before++;

	assert(((q.sp - 1) & VMemPool::alignmentMask) == 0);
	assert(p.sp > q.sp);
	Bit32 extra = p.sp - q.sp;
	if(extra < FREEBK_MINSIZE)
	  {
#if defined(PERFORM_EXTRA_CHECKS)
	    {
	      VMemPoolBlockSP byte;
	      byte.sp = q.sp;
	      while(byte.sp < p.sp)
		{
		  assert(byte.type() == VMemPool::freeByte);
		  byte.sp++;
		}
	    }
#else
	    assert(q.type() == VMemPool::freeByte);
#endif
	    callback[(int)VMemPool::freeByte].totalSize -= extra;
	  }
	else
	  {
	    assert(q.type() == VMemPool::freeBlock);
	    assert(q.length() == extra);

	    remove_from_free_list(q, true);
	  }
	asize += extra;
	p.sp = q.sp;
      }
    // We should never have the start or the end of the block be
    // unaligned.
    assert(((p.sp - 1) & VMemPool::alignmentMask) == 0);
    assert((asize & VMemPool::alignmentMask) == 0);

    if (asize < FREEBK_MINSIZE) {
        Bit32 free_start = p.sp, free_end = p.sp+asize;

	// The block is too small to go on the free list; fill it with
	// freeBytes instead.
	while (asize--) {
	    p.setType(VMemPool::freeByte);
	    p.sp++;
	    callback[(int)VMemPool::freeByte].totalSize++;
	}

	// Remember the block for later coalescing unless it's 0 bytes
	if(free_end > free_start)
	  {
	    bool inTbl = free_space_origins.Put(free_end, free_start);
	    assert(!inTbl);
	  }
    } else {
        p.setType(VMemPool::freeBlock);
	p.setLength(asize);

	add_to_free_list(p);
    }

    free_calls++;

#if defined(PERFORM_EXTRA_CHECKS)
    assert(check_free_list());
#endif

    // Record the time taken by this call
    free_time += call_timer.get_delta();

    mu.unlock();
}

VMemPool::typeCode
VMemPool::type(void* addr) throw ()
{
    VMemPoolBlockSP p;
    p.sp = VMemPool::shortenPointer(addr);
    return p.type();
}

static Text sorted_histo_text(IntIntTbl &histo)
{
  IntKey key; int val;

  // Extract the table keys and sort them
  IntSeq keys;
  IntIntIter it(&histo);
  while(it.Next(key, val)) keys.addhi(key.Val());
  std::sort(keys.begin(), keys.end());

  // Print them in order of ascending block size
  Basics::OBufStream result;
  while(keys.size())
    {
      key = keys.remlo();
      bool inTbl = histo.Get(key, val); assert(inTbl);
      result << "\t" << key << " : " << val << endl;
    }
  return result.str();
}

void
VMemPool::gc(ShortId keepDerivedSid) throw ()
{
    int i;
    VMemPool::init();
    mu.lock();
    // Source mark phase.  Recursively mark everything reachable from
    // the roots.
    for (i = 0; i < (int) VMemPool::maxTypeCodes; i++) {
	if (callback[i].markcb != NULL) {
	    callback[i].markcb(callback[i].markcl,
			       (VMemPool::typeCode) i);
	}
	callback[i].totalSize = 0;
    }

    // Recursively mark immutable directories that are on the derived
    // keep list and everything reachable from them.  (Note that we
    // don't do anything about the derived keep time, because sources
    // are not leased; see design document on Vesta internal home page.)
    if (keepDerivedSid != NullShortId) {
	SourceOrDerived sidstream;
	sidstream.open(keepDerivedSid); 
	sidstream >> hex;
	for (;;) {
	    ShortId nextkeep;
	    sidstream >> nextkeep;
	    if (sidstream.fail()) {
		assert(sidstream.eof());
		break;
	    }
	    if (SourceOrDerived::dirShortId(nextkeep)) {
		Bit32 sp = GetDirShortId(nextkeep);
		if (sp != 0) {
		    VDirChangeable vs(VestaSource::immutableDirectory,
				      (Bit8*) VMemPool::lengthenPointer(sp));
		    if (!vs.hasName()) {
			vs.setHasName(true);
			vs.mark();
		    }
		}
	    }
	}
	sidstream.close();
    }

    // Sweep phase.  Also rebuilds the free list, compacting adjacent
    // free blocks.
    VMemPoolBlockSP p;
    p.sp = VMemPool::shortenPointer(base);
    VMemPoolBlockSP curFree;
    curFree = SNULL;
    Bit32 curFreeSize = 0;
    reset_free_lists();
    free_space_origins.Init();
    free_list_blocks = 0;
    free_list_bytes = 0;
    while (p.sp < unusedMem.sp) {
	VMemPool::typeCode ptype = p.type();
	Bit32 size;
	bool free =
	  !callback[(int) ptype].sweepcb(callback[(int) ptype].sweepcl, 
					 ptype, (void*)
					 VMemPool::lengthenPointer(p.sp),
					 size);
	if (free) {
	    //Repos::dprintf(DBG_VMEMPOOL, 
            //        "gc free, type %d, sp 0x%08x\n", (int) ptype, p.sp);
	    if (curFree == SNULL) {
		curFree = p;
		curFreeSize = size;
	    } else {
		curFreeSize += size;
	    }
	} else {
	    if (curFree != SNULL) {
	        // Skip free bytes to achieve required alignment
	        while ((curFreeSize > 0) &&
		       ((curFree.sp - 1) & VMemPool::alignmentMask)) {
		    callback[(int)VMemPool::freeByte].totalSize++;
		    curFree.setType(VMemPool::freeByte);
		    curFree.sp++;
		    curFreeSize--;
		}
		assert((curFreeSize & VMemPool::alignmentMask) == 0);
		if (curFreeSize < FREEBK_MINSIZE) {
		    Bit32 free_start = curFree.sp,
		          free_end = curFree.sp+curFreeSize;
		    callback[(int)VMemPool::freeByte].totalSize +=curFreeSize;
		    while (curFreeSize--) {
			curFree.setType(VMemPool::freeByte);
			curFree.sp++;
		    }
		    if(free_end > free_start)
		      {
			bool inTbl =
			  free_space_origins.Put(free_end, free_start);
			assert(!inTbl);
		      }
		} else {
		    curFree.setType(VMemPool::freeBlock);
		    curFree.setLength(curFreeSize);
		    add_to_free_list(curFree);
		}
		curFree = SNULL;
		curFreeSize = 0;
	    }
	    callback[(int) ptype].totalSize += size;
	}
	p.sp += size;
	assert(p.sp <= unusedMem.sp); // bug trap
    }

    if (curFree != SNULL) {
      // Adjust curFree to proper alignment
      while ((curFreeSize > 0) &&
	     (curFree.sp - 1) & VMemPool::alignmentMask) {
	assert(curFree.sp < unusedMemEnd.sp);
	callback[(int)VMemPool::freeByte].totalSize++;
	curFree.setType(VMemPool::freeByte);
	curFree.sp++;
	curFreeSize--;
      }
      unusedMem = curFree;
    }    

    // Enumerate and log freed DirShortIds
    LogAllDirShortIds("fsid");

    // Rebuild hash tables
    VMemPool::rebuildDirShortIdTable();

    if (Repos::isDebugLevel(DBG_VMEMPOOL)) {
      Repos::dprintf(0, "Memory pool size statistics:\n"
		     "freeByte\t%u\n"
		     "freeBlock\t%u\n"
		     "vDirChangeable\t%u\n"
		     "vForward\t%u\n"
		     "vDirEvaluator\t%u\n"
		     "vDirImmutable\t%u\n"
		     "vAttrib\t\t%u\n"
		     "vDirAppendable\t%u\n",
		     callback[freeByte].totalSize,
		     callback[freeBlock].totalSize,
		     callback[vDirChangeable].totalSize,
		     callback[vForward].totalSize,
		     callback[vDirEvaluator].totalSize,
		     callback[vDirImmutable].totalSize,
		     callback[vAttrib].totalSize,
		     callback[vDirAppendable].totalSize);
      {
	if(alloc_size_histo != 0)
	  {
	    Text histo = sorted_histo_text(*alloc_size_histo);
	    Repos::dprintf(0, "Memory pool allocated block size histogram:\n%s",
			   histo.cchars());
	  }
      }

      VDirChangeable::printStats();
    }

    mu.unlock();
}

// Assumes mutex is locked!
void
VMemPool::rebuildDirShortIdTable() throw ()
{
    DeleteAllDirShortIds();
    DeleteAllFPShortId();

    // Sweep through memory, rebuilding the DirShortId table
    // and the FPShortId table.
    VMemPoolBlockSP p;
    p.sp = VMemPool::shortenPointer(base);
    while (p.sp < unusedMem.sp) {
	VMemPool::typeCode ptype = p.type();
	Bit32 size = 0, oldsize;
	oldsize = size; // for debugging only
	callback[(int) ptype].rebuildcb(callback[(int) ptype].rebuildcl, 
					ptype, (void*)
					VMemPool::lengthenPointer(p.sp),
					size);
	assert(size > 0); // bug trap
	p.sp += size;
	assert(p.sp <= unusedMem.sp); // bug trap
    }
}

void
VMemPool::registerCallbacks(VMemPool::typeCode type,
			    VMemPool::markCallback markcb, void* markcl,
			    VMemPool::sweepCallback sweepcb, void* sweepcl,
			    VMemPool::rebuildCallback rebuildcb,
			    void* rebuildcl) throw ()
{
    VMemPool::init();
    mu.lock();
    callback[(int) type].markcb = markcb;
    callback[(int) type].markcl = markcl;
    callback[(int) type].sweepcb = sweepcb;
    callback[(int) type].sweepcl = sweepcl;
    callback[(int) type].rebuildcb = rebuildcb;
    callback[(int) type].rebuildcl = rebuildcl;
    callback[(int) type].totalSize = 0;
    mu.unlock();
}

// Callbacks for free bytes and blocks
static bool sweepFreeByteCallback(void* closure, VMemPool::typeCode type,
				  void* addr, Bit32& size)
{
    size = 1;
    return false;
}

static void rebuildFreeByteCallback(void* closure, VMemPool::typeCode type,
				    void* addr, Bit32& size)
{
    size = 1;
}

static bool sweepFreeBlockCallback(void* closure, VMemPool::typeCode type,
				   void* addr, Bit32& size)
{
    size = ((VMemPoolBlockSP) VMemPool::shortenPointer(addr)).length();
    return false;
}

static void rebuildFreeBlockCallback(void* closure, VMemPool::typeCode type,
				     void* addr, Bit32& size)
{
    size = ((VMemPoolBlockSP) VMemPool::shortenPointer(addr)).length();
}

void
VMemPool::init_inner()
{
    // Compute log2(FREEBK_MINSIZE).  We don't need to keep any free
    // lists for sizes smaller thatn this.
    {
      free_list_index_offset = 0;
      Bit32 first_list_size = FREEBK_MINSIZE;
      while(first_list_size > 1)
	{
	  first_list_size >>= 1;
	  free_list_index_offset++;
	}
    }

#if defined(_SC_PAGESIZE)
    page_size = sysconf(_SC_PAGESIZE);
#elif defined(_SC_PAGE_SIZE)
    page_size = sysconf(_SC_PAGE_SIZE);
#else
#error Need page size
#endif
    assert(page_size != 0);

    setCurrentCkptVersion(CkptMaxVersion); // default if no ckpt read
    mu.lock();
    Bit8* baseReq;
    try {
	Text t;
	if (VestaConfig::get("Repository", "VMemPool_size", t)) {
	    // Clear errno, as strtoul will only modify it on
	    // overflow.
	    errno = 0;
	    totalSize = strtoul(t.cchars(), NULL, 0);
	    if(errno == ERANGE)
	      {
		Repos::dprintf(DBG_ALWAYS,
			       "Fatal: [Repository]VMemPool_size is too large\n");
		abort();
	      }
	    assert(totalSize > 0);
	    totalSize = (((totalSize - 1) / page_size) + 1) * page_size;
	} else {
	    totalSize = page_size;
	}
	if (VestaConfig::get("Repository", "VMemPool_size_soft_limit", t)) {
	    // Clear errno, as strtoul will only modify it on
	    // overflow.
	    errno = 0;
	    VMemPool_totalSize_soft_limit = strtoul(t.cchars(), NULL, 0);
	    if(errno == ERANGE)
	      {
		Repos::dprintf(DBG_ALWAYS,
			       "Fatal: [Repository]VMemPool_size_soft_limit is too large\n");
		abort();
	      }
	    assert(VMemPool_totalSize_soft_limit > 0);
	}
	if (VestaConfig::get("Repository", "VMemPool_base", t)) {
	    // Clear errno, as strtoul will only modify it on
	    // overflow.
	    errno = 0;
	    baseReq = (Bit8*) strtoul(t.cchars(), NULL, 0);
	    if(errno == ERANGE)
	      {
		Repos::dprintf(DBG_ALWAYS,
			       "Fatal: [Repository]VMemPool_base is too large\n");
		abort();
	      }
	    baseReq = (Bit8*) (((((PointerInt) baseReq - 1)
				 / page_size) + 1) * page_size);
	} else {
	    baseReq = NULL;
	}
	if (VestaConfig::get("Repository", "VMemPool_minGrowBy", t)) {
	    // Clear errno, as strtoul will only modify it on
	    // overflow.
	    errno = 0;
	    minGrowBy = (Bit32) strtoul(t.cchars(), NULL, 0);
	    if(errno == ERANGE)
	      {
		Repos::dprintf(DBG_ALWAYS,
			       "Fatal: [Repository]VMemPool_minGrowBy is too large\n");
		abort();
	      }
	} else {
	    minGrowBy = page_size;
	}

	if(VestaConfig::is_set("Repository", "VMemPool_alloc_size_histo") &&
	   VestaConfig::get_bool("Repository", "VMemPool_alloc_size_histo"))
	  {
	    alloc_size_histo = NEW(IntIntTbl);
	  }

    } catch (VestaConfig::failure f) {
	Repos::dprintf(DBG_ALWAYS, "VMemPool::init got VestaConfig::failure: %s\n",
		       f.msg.cchars());
	abort();
    }
#if MALLOC_VMEMPOOL
    base = (Bit8*) malloc(totalSize);
    assert(base != NULL);
#else
    if (baseReq == NULL) {
	base = (Bit8*) mmap(baseReq, totalSize, PROT_READ|PROT_WRITE,
			    MAP_ANON|/*MAP_VARIABLE|*/MAP_PRIVATE, -1, 0);
    } else {
	base = (Bit8*) mmap(baseReq, totalSize, PROT_READ|PROT_WRITE,
			    MAP_ANON|MAP_FIXED|MAP_PRIVATE, -1, 0);
    }
    assert(base != (Bit8*) -1);
#endif
    unusedMemEnd.sp = totalSize;
    unusedMem.sp = VMemPool::shortenPointer(base);
    callback[(int) VMemPool::freeByte].sweepcb = sweepFreeByteCallback;
    callback[(int) VMemPool::freeByte].rebuildcb = rebuildFreeByteCallback;
    callback[(int) VMemPool::freeByte].totalSize = 0;
    callback[(int) VMemPool::freeBlock].sweepcb = sweepFreeBlockCallback;
    callback[(int) VMemPool::freeBlock].rebuildcb = rebuildFreeBlockCallback;
    callback[(int) VMemPool::freeBlock].totalSize = totalSize;
    Repos::dprintf(DBG_VMEMPOOL, "VMemPool baseReq 0x%p, base 0x%p,\n"
		   "  size 0x%x, minGrowBy 0x%x\n",
		   baseReq, base, totalSize, minGrowBy);
    mu.unlock();
}

void
VMemPool::grow(Bit32 growBy) throw ()
{
#if NOGROW_VMEMPOOL
    Repos::dprintf(DBG_ALWAYS, 
		   "Out of memory; crashing! (total %d, used %d, req %d)\n",
		   unusedMemEnd.sp, unusedMem.sp, growBy);
    abort();
#else
    Bit32 gb = growBy;
    if (gb < minGrowBy) gb = minGrowBy;
    gb = (((gb - 1) / page_size) + 1) * page_size;

    // Catch overflow of our soft limit on totalSize
    if((totalSize + gb) > VMemPool_totalSize_soft_limit)
      {
	Repos::dprintf(DBG_ALWAYS,
		       "FATAL ERROR: VMemPool soft limit exceeded: "
		       "current size 0x%x, soft limit 0x%x, "
		       "grow req 0x%x, rounded 0x%x\n"
		       "\tYou should consider deleting some contents from your "
		       "repository (/vesta and /vesta-work)\n"
		       "\tYou should definitely run the weeder "
		       "(after performing any deletions)\n"
		       "\tYou can change the soft limit with "
		       "[Repository]VMemPool_size_soft_limit\n",
		       totalSize, VMemPool_totalSize_soft_limit, growBy, gb);
	abort();
      }
    // Catch overflow of short pointer space by growing beyond the
    // 32-bit limit.
    else if(((Bit32) totalSize + gb) < totalSize)
      {
	Repos::dprintf(DBG_ALWAYS,
		       "FATAL ERROR: VMemPool hard limit exceeded: "
		       "current size 0x%x, exceeded by 0x%x, "
		       "grow req 0x%x, rounded 0x%x\n"
		       "\tTo recover, you will need to delete some contents "
		       "from your repository and run the weeder\n",
		       totalSize, ((Bit32) totalSize + gb), growBy, gb);
	abort();
      }

#if MALLOC_VMEMPOOL
    // Can this work?  Might invalidate pointers in other threads
    base = (Bit8*) realloc(base, totalSize + gb);
    assert(base != NULL);
#else
    void* more = mmap(base + totalSize, gb, PROT_READ|PROT_WRITE,
		      MAP_ANON|/*MAP_VARIABLE|*/MAP_PRIVATE, -1, 0);
    assert(more == base + totalSize);
    grow_calls++;
#endif
    totalSize += gb;
    callback[(int)VMemPool::freeBlock].totalSize += gb;
    Repos::dprintf(DBG_VMEMPOOL, 
		   "VMemPool grow req 0x%x, rounded 0x%x, new size 0x%x\n",
		   growBy, gb, totalSize);
    unusedMemEnd.sp = totalSize;
#endif
}

//
// Checkpointing code
//

void
VMemPool::writeCheckpoint(fstream& ckpt) throw ()
{
    Bit32 nextSP = 1;
    Bit32 endianFlag = 0x01020304;

    mu.lock(); // needed?

    // Get pointers to roots now; don't call these functions after
    // possibly munging memory, because they read the attributes to
    // set the access permissions.
    VestaSource* rr = VestaSource::repositoryRoot();
    VestaSource* mr = VestaSource::mutableRoot();
    VestaSource* vr = VestaSource::volatileRoot();

    // Start checkpointing stable memory
    setCurrentCkptVersion(CkptMaxVersion);
    ckpt << "(smem " << CkptMaxVersion << endl;
    ckpt.write((char *) &endianFlag, sizeof(Bit32));

    // Leave space to write first unused SP value
    streampos lenpos = ckpt.tellp();
    ckpt.write((char *) &nextSP, sizeof(Bit32));

    // Checkpoint all directory structure and internal attributes
    Bit32 rSP = rr->checkpoint(nextSP, ckpt);
    Bit32 mSP = mr->checkpoint(nextSP, ckpt);
    CheckpointAllDirShortIds(nextSP, ckpt);

    // Checkpoint attributes of stable roots
    VestaAttribsRep* ar;
    ar = (VestaAttribsRep*) VMemPool::lengthenPointer(rr->firstAttrib());
    Bit32 raSP = 0;
    if (ar) raSP = ar->checkpoint(nextSP, ckpt);
    rr->rep = NULL;
    *(Bit32*)rr->attribs = 0;
    ar = (VestaAttribsRep*) VMemPool::lengthenPointer(mr->firstAttrib());
    Bit32 maSP = 0;
    if (ar) maSP = ar->checkpoint(nextSP, ckpt);
    mr->rep = NULL;
    *(Bit32*)mr->attribs = 0;

    // Write end of stable memory checkpoint
    streampos endpos = ckpt.tellp();
    ckpt.seekp(lenpos, fstream::beg);
    ckpt.write((char *) &nextSP, sizeof(Bit32));
    ckpt.seekp(endpos, fstream::beg);
    ckpt << "\n)\n";

    // Record new stable root rep and attrib pointers
    ckpt << "(rroot " << rSP << " " << raSP << ")\n";
    ckpt << "(mroot " << mSP << " " << maSP << ")\n";

    // Start checkpointing volatile memory (for immediate re-read)
    ckpt << "(vmem " << CkptMaxVersion << endl;
    ckpt.write((char *) &endianFlag, sizeof(Bit32));

    // Leave space to write first unused SP value
    lenpos = ckpt.tellp();
    ckpt.write((char *) &nextSP, sizeof(Bit32));

    (void) vr->checkpoint(nextSP, ckpt);

    // Checkpoint attributes of volatile root
    ar = (VestaAttribsRep*) VMemPool::lengthenPointer(vr->firstAttrib());
    Bit32 vaSP = 0;
    if (ar) vaSP = ar->checkpoint(nextSP, ckpt);
    *(Bit32*)vr->attribs = 0;

    // Write end of volatile memory checkpoint
    endpos = ckpt.tellp();
    ckpt.seekp(lenpos, fstream::beg);
    ckpt.write((char *) &nextSP, sizeof(Bit32));
    ckpt.seekp(endpos, fstream::beg);
    ckpt << "\n)\n";

    // Record new volatile root attrib pointer
    ckpt << "(vroot " << vaSP << ")\n";

    mu.unlock();
}

static void assertGood(fstream& ckpt)
{
    if (!ckpt.good()) {
        int errno_save = errno;
	Text err = Basics::errno_Text(errno_save);
	Repos::dprintf(DBG_ALWAYS, 
		       "read from checkpoint file failed: %s (errno = %d)\n",
		       err.cchars(), errno_save);
	assert(ckpt.good());  // crash
    }
}

void
VMemPool::readCheckpoint(fstream& ckpt, bool readVolatile) throw ()
{
    char buf1[64];
    char buf2[64];
    char newline;
    Bit32 endianFlag;
    Bit32 nextSP, rootSP, rootAttribsSP;
    int ckptVersion;

    mu.lock();

    // Get pointers to roots
    VestaSource* rr = VestaSource::repositoryRoot();
    VestaSource* mr = VestaSource::mutableRoot();
    VestaSource* vr = VestaSource::volatileRoot();

    // Get header for stable data
    ckpt.get(buf1, sizeof(buf1));
    assertGood(ckpt);
    int ok = sscanf(buf1, "(smem %d", &ckptVersion);
    assert(ok == 1);
    if (ckptVersion < CkptMinVersion || ckptVersion > CkptMaxVersion) {
	Repos::dprintf(DBG_ALWAYS, "checkpoint version %d not between %d and %d\n",
		       ckptVersion, CkptMinVersion, CkptMaxVersion);
	exit(2);
    }
    setCurrentCkptVersion(ckptVersion);
    ckpt.get(newline);
    assertGood(ckpt);
    assert(newline == '\n');
    ckpt.read((char *) &endianFlag, sizeof(Bit32));
    assertGood(ckpt);
    switch (endianFlag) {
      case 0x01020304:
	break;
      case 0x04030201:
	assert(false);  // Not implemented yet
	break;
      default:
	assert(false);
	break;
    }
    ckpt.read((char *) &nextSP, sizeof(Bit32));
    assertGood(ckpt);

    // Get stable data
    if (nextSP > unusedMemEnd.sp) {
      grow(nextSP - unusedMemEnd.sp);
    }
    ckpt.read((char *) VMemPool::base, nextSP - 1);
    assertGood(ckpt);
    reset_free_lists();
    free_space_origins.Init();
    free_list_blocks = 0;
    free_list_bytes = 0;
    Bit32 volSP = nextSP;
    assert(((nextSP - 1) & VMemPool::alignmentMask) == 0);
    unusedMem.sp = nextSP;

    // Get stable data trailer
    ckpt.get(newline);
    assertGood(ckpt);
    assert(newline == '\n');
    ckpt.get(buf2, sizeof(buf2));
    assertGood(ckpt);
    assert(strcmp(buf2, ")") == 0);
    ckpt.get(newline);
    assertGood(ckpt);
    assert(newline == '\n');

    // Get stable roots.
    if (ckptVersion >= 5) {
	ckpt >> buf1 >> rootSP >> rootAttribsSP >> buf2;
    } else {
	ckpt >> buf1 >> rootSP >> buf2;
	rootAttribsSP = 0;
    }	
    assertGood(ckpt);
    assert(strcmp(buf1, "(rroot") == 0);
    assert(strcmp(buf2, ")") == 0);
    ckpt.get(newline);
    assertGood(ckpt);
    assert(newline == '\n');
    rr->rep = (Bit8*) VMemPool::lengthenPointer(rootSP);
    *(Bit32*) rr->attribs = rootAttribsSP;
    rr->resync();

    if (ckptVersion >= 5) {
	ckpt >> buf1 >> rootSP >> rootAttribsSP >> buf2;
    } else {
	ckpt >> buf1 >> rootSP >> buf2;
	rootAttribsSP = 0;
    }	
    assertGood(ckpt);
    assert(strcmp(buf1, "(mroot") == 0);
    assert(strcmp(buf2, ")") == 0);
    ckpt.get(newline);
    assertGood(ckpt);
    assert(newline == '\n');
    mr->rep = (Bit8*) VMemPool::lengthenPointer(rootSP);
    *(Bit32*) mr->attribs = rootAttribsSP;
    mr->resync();

    // Get header for volatile data
    ckpt.get(buf1, sizeof(buf1));
    assertGood(ckpt);
    ok = sscanf(buf1, "(vmem %d", &ckptVersion);
    assert(ok == 1);

    ckpt.get(newline);
    assertGood(ckpt);
    assert(newline == '\n');
    ckpt.read((char *) &endianFlag, sizeof(Bit32));
    assertGood(ckpt);
    switch (endianFlag) {
      case 0x01020304:
	break;
      case 0x04030201:
	assert(false);		// Not implemented yet
	break;
      default:
	assert(false);
	break;
    }
    ckpt.read((char *) &nextSP, sizeof(Bit32));
    assertGood(ckpt);

    // Get or skip volatile data
    if (readVolatile) {
        // We only read volatile checkpoints we just wrote, so no
        // need to be able to handle old versions.
        assert(ckptVersion == CkptMaxVersion);
	assert(nextSP >= unusedMem.sp);

	if (nextSP > unusedMemEnd.sp) {
	  grow(nextSP - unusedMemEnd.sp);
	}
#if 0
	// Seems to be a cxx library bug that causes 1 byte to be
        // skipped if we try to read 0 bytes here!?
	ckpt.read((char *) VMemPool::lengthenPointer(unusedMem.sp),
		  nextSP - unusedMem.sp);
#else
	if (nextSP > unusedMem.sp) {
	    ckpt.read((char *) VMemPool::lengthenPointer(unusedMem.sp),
		      nextSP - unusedMem.sp);
	    assertGood(ckpt);
	}
#endif
	unusedMem.sp = nextSP;
    } else {
	// Skip volatile data
	ckpt.seekg(nextSP - volSP, std::ios::cur);
	assertGood(ckpt);
    }
    
    // Get volatile data trailer
    ckpt.get(newline);
    assertGood(ckpt);
    assert(newline == '\n');
    ckpt.get(buf2, sizeof(buf2));
    assertGood(ckpt);
    assert(strcmp(buf2, ")") == 0);
    ckpt.get(newline);
    assertGood(ckpt);
    assert(newline == '\n');

    // Get or skip volatile root attribs pointer
    if (ckptVersion >= 11) {
      ckpt >> buf1 >> rootAttribsSP >> buf2;
      assertGood(ckpt);
      assert(strcmp(buf1, "(vroot") == 0);
      assert(strcmp(buf2, ")") == 0);
      if (readVolatile) {
	*(Bit32*) vr->attribs = rootAttribsSP;
	vr->resync();
      }
    }

    rebuildDirShortIdTable();

    mu.unlock();
}

void VMemPool::debugDump(std::ostream &dump) throw ()
{
    VestaSource* rr = VestaSource::repositoryRoot();
    VestaSource* mr = VestaSource::mutableRoot();

    rr->debugDump(dump);

    VestaAttribsRep* ar;
    ar = (VestaAttribsRep*) VMemPool::lengthenPointer(rr->firstAttrib());
    if (ar)
      {
	dump << "Repository root attributes: ";
	PrintBlockID(dump, rr->firstAttrib());
	dump << endl;
	ar->debugDump(dump);
      }

    mr->debugDump(dump);

    ar = (VestaAttribsRep*) VMemPool::lengthenPointer(mr->firstAttrib());
    if (ar)
      {
	dump << "Mutable root attributes: ";
	PrintBlockID(dump, mr->firstAttrib());
	dump << endl;
	ar->debugDump(dump);
      }
}

void VMemPool::getStats(/*OUT*/ Basics::uint32 &sz,
			/*OUT*/ Basics::uint32 &f_l_blocks,
			/*OUT*/ Basics::uint32 &f_l_bytes,
			/*OUT*/ Basics::uint32 &f_w_bytes,
			/*OUT*/ Basics::uint8 &ne_f_l_count,
			/*OUT*/ Basics::uint64 &a_calls,
			/*OUT*/ Basics::uint64 &a_rej_sm_blocks,
			/*OUT*/ Basics::uint64 &a_rej_lg_blocks,
			/*OUT*/ Basics::uint64 &a_split_blocks,
			/*OUT*/ Basics::uint64 &a_new_blocks,
			/*OUT*/ Timers::Elapsed &a_time,
			/*OUT*/ Basics::uint64 &f_calls,
			/*OUT*/ Basics::uint64 &f_coalesce_b,
			/*OUT*/ Basics::uint64 &f_coalesce_a,
			/*OUT*/ Timers::Elapsed &f_time,
			/*OUT*/ Basics::uint64 &g_calls)
{
  mu.lock();

  // Current size of VMemPool
  sz = unusedMem.sp - 1;

  // Current free list
  f_l_blocks = free_list_blocks;
  f_l_bytes = free_list_bytes;

  // Current free bytes wasted for alignment
  f_w_bytes = callback[(int)VMemPool::freeByte].totalSize;

  // Current number of non-empty free lists
  ne_f_l_count = 0;
  for(int i = 0; i < free_lists.size(); i++)
    {
      if(free_lists.get(i) != SNULL) ne_f_l_count++;
    }

  // Total number of allocate calls and stats about allocate
  a_calls = allocate_calls;
  a_rej_sm_blocks = allocate_rej_sm_blocks;
  a_rej_lg_blocks = allocate_rej_lg_blocks;
  a_split_blocks = allocate_split_blocks;
  a_new_blocks = allocate_new_blocks;
  a_time = allocate_time;

  // Total number of free calls and stats about free
  f_calls = free_calls;
  f_coalesce_b = free_coalesce_before;
  f_coalesce_a = free_coalesce_after;
  f_time = free_time;

  // Total number of grow calls
  g_calls = grow_calls;

  mu.unlock();
}
