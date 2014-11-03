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
// VMemPool.H
//
// Memory pool for Vesta repository directory structure.  Confined to
// a range of addresses that can be represented in 32 bits.
//

#ifndef _VMEMPOOL
#define _VMEMPOOL 1

#include "Basics.H"
#include "SourceOrDerived.H"
#include <assert.h>
#include <fstream>
#include "Timers.H"

// All memory allocated by VMemPool has the following format.
// See VDirChangeable.H for an explanation of the notation.
//
// packed struct VMemPoolBlock {
//     Bit8 clientBits: 4;           // Available to client.
//     Bit8 typeCode: 4;             // Contains the typeCode for this
//				     //  block, as supplied by the
//				     //  client at allocation time.
//     Bit8 clientData[];            // Arbitrary data; client must be
//				     //  able to determine the block's
//				     //  length by parsing the data. 
// };

extern "C" void VMemPool_init();

class VMemPool {
  public:
    enum typeCode { freeByte = 0, freeBlock, vDirChangeable, vForward,
		    vDirEvaluator, vDirImmutable, vAttrib, vDirAppendable,
		    maxTypeCodes = 0x10, unspecified };

    // The blocks returned are always aligned to the following
    // multiple (must be at least 8 for the Alpha) so that it is safe
    // to protect different blocks with different locks.  This will
    // cause some space wastage if you allocate objects whose size is
    // not a multiple of the alignment.
    static int alignmentMask;

    // Allocate a block of memory with typeCode "type".  The first
    // byte of the allocated memory contains typeCode.  The caller
    // must remember the size of the block, or be able to determine it
    // by scanning the block's contents.
    //
    // Warning: the caller must be holding a lock that prevents
    // weeding and checkpointing, such as StableLock,
    // VolatileRootLock, or a lock on a volatile subtree.  Otherwise
    // the memory might be freed by the garbage collector or moved by
    // the checkpointer before the caller gets a chance to put a
    // pointer somewhere they will find it!
    //
    static void* allocate(typeCode type, Bit32 size) throw();

    // Explicitly free a block of memory, not waiting for the next
    // garbage collection.  If type is specified, assert that the
    // type code is as given; crash if it is not.
    static void free(void* addr, Bit32 size,
		     typeCode type =unspecified) throw();

    // Retrieve the type of a block.
    static typeCode type(void* addr) throw();

    // Because all memory allocated by VMemPool is within a range of
    // 2^32 addresses, pointers returned by allocate can be shortened
    // to 32 bits and lengthened again without loss of information.
    static inline Bit32 shortenPointer(const void* pointer) throw() {
	if (pointer == 0)
	  return 0;
	else
	  return (Bit32) (((Bit8*) pointer) - base + 1);
    }
    static inline void* lengthenPointer(Bit32 spointer) throw() {
	if (spointer == 0)
	  return (void*) 0;
	else
          return base + spointer - 1;
    }

    // Perform a garbage collection, consisting of one mark phase and
    // one sweep phase.  In the mark phase, each registered
    // markCallback is invoked once.  A markCallback may be NULL if
    // none is needed.  In the sweep phase, the registered
    // sweepCallback is invoked once for each allocated block.  The
    // sweepCallback sets "size" to the block's size, and returns true
    // if the block is still in use.  A sweepCallback must be
    // registered for every typeCode that is used in an allocate call.
    // Do not register callbacks for freeByte or freeBlock.  The
    // rebuildCallback is used in an additional sweep pass that
    // rebuilds the DirShortId table.
    //
    static void gc(ShortIdsFile keepDerivedSid) throw();
    typedef void (*markCallback)(void* closure, typeCode type) /*throw ()*/;
    typedef bool (*sweepCallback)(void* closure, typeCode type, void* addr,
				  Bit32& size) /*throw ()*/;
    typedef void (*rebuildCallback)(void* closure, typeCode type, void* addr,
				    Bit32& size) /*throw ()*/;
    static void registerCallbacks(typeCode type,
				  markCallback markcb, void* markcl,
				  sweepCallback sweepcb, void* sweepcl,
				  rebuildCallback rebuildcb, void* rebuildcl)
      throw();

    // Write and read checkpoints.  Writing a checkpoint destroys the
    // memory pool contents, so the checkpoint must be immediately
    // read back or the server must exit.  Volatile directories are
    // included in the checkpoint to support immediate rereading.
    // When reading a checkpoint, the volatile portion is ignored if
    // readVolatile is false.  These routines do not use callbacks as
    // with mark and sweep above; instead, they have built-in
    // knowledge of the roots (RepositoryRoot, MutableRoot, and
    // VolatileRoot).  Reading a checkpoint also rebuilds the
    // DirShortId table.
    //
    // The checkpoint methods that are called by these routines must
    // pad each object written to a multiple of 8 bytes, filling at
    // the end with freeByte objects (bytes with value 0) if needed.
    //
    static void writeCheckpoint(std::fstream& ckpt) throw();
    static void readCheckpoint(std::fstream& ckpt, bool readVolatile) throw();

    static void debugDump(std::ostream &dump) throw ();

    // Get statistics about the heap and usage.  "size" is the total
    // number of bytes in the heap currently.  "free_list_blocks" is
    // the number of blocks on the free list.  "free_list_bytes" is
    // the total number of bytes in all blocks on the free list.
    // "allocate_calls" is the number of calls to allocate.
    // "allocate_rejected_blocks" ar ethe number of blocks from the
    // free list considered and rejected in allocate calls.
    // "free_calls" is the number of calls to free.  "grow_calls" is
    // the number of times grow has been called to expand the heap.
    static void getStats(/*OUT*/ Basics::uint32 &size,
			 /*OUT*/ Basics::uint32 &free_list_blocks,
			 /*OUT*/ Basics::uint32 &free_list_bytes,
			 /*OUT*/ Basics::uint32 &free_wasted_bytes,
			 /*OUT*/ Basics::uint8 &nonempty_free_list_count,
			 /*OUT*/ Basics::uint64 &allocate_calls,
			 /*OUT*/ Basics::uint64 &allocate_rej_sm_blocks,
			 /*OUT*/ Basics::uint64 &allocate_rej_lg_blocks,
			 /*OUT*/ Basics::uint64 &allocate_split_blocks,
			 /*OUT*/ Basics::uint64 &allocate_new_blocks,
			 /*OUT*/ Timers::Elapsed &allocate_time,
			 /*OUT*/ Basics::uint64 &free_calls,
			 /*OUT*/ Basics::uint64 &free_coalesce_before,
			 /*OUT*/ Basics::uint64 &free_coalesce_after,
			 /*OUT*/ Timers::Elapsed &free_time,
			 /*OUT*/ Basics::uint64 &grow_calls);

    // Public entry point for initializing VMemPool.  Safe to call
    // multiple times.
    static void init();

  protected:
    static Bit8* base;
    static void init_inner();

  private:
    static Bit32 totalSize, minGrowBy;
    static void rebuildDirShortIdTable() throw ();
    static void grow(Bit32 growBy) throw();

    friend void VMemPool_init();
};

#endif //_VMEMPOOL
