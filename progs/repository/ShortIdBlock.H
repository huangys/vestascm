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
// ShortIdBlock.H
//
// This is a low-level interface to the repository server, used by the 
// SourceOrDerived library code.  
//
#ifndef _VSIDB
#define _VSIDB

#include <sys/types.h>
#include <sys/socket.h>
#include "SourceOrDerived.H"
#include "SRPC.H"

// A ShortIdBlock represents a block of (possibly) available ShortIds.
// They all have the same leafflag value.  Methods here are all local code,
// not RPC.
// 
class ShortIdBlock {
  public:
    enum { size=256 }; // must be a power of 2
    Bit8 bits[size / 8];
    ShortId start;
    time_t leaseExpires;
    static const time_t leaseNonexpiring;
    // ShortIdBlock will not assign a ShortId with dirFlag set and
    //  leafFlag clear; only DirShortId will do that.  (Note: the
    //  present allocator does not use ShortIds with dirFlag and
    //  leafFlag both set, but some are still assigned at SRC as normal
    //  leaf blocks for historical reasons.)
    enum { leafFlag = 0x80000000, dirFlag = 0x40000000 };
    ShortId next;     // next >= start

    ShortId assignNextAvail() throw();  // => NullShortId if none available
    inline bool assigned(ShortId s) const throw()
      {	return (bool) ((bits[(s-start)/8] & (1 << ((s-start)%8))) != 0); };
    inline void set(ShortId s) throw()
      {	bits[(s-start)/8] |= 1 << ((s-start)%8); };
    inline bool leaf() throw()
      { return SourceOrDerived::leafShortId(start); };
    inline void init(ShortId st) throw() {
	memset((void *) bits, 0, sizeof(bits));
	next = start = st;
	leaseExpires = 0;
    };

    // The following are methods only for name scoping purposes

    // Convert ShortId to filename.  If prepend is "", the name is
    // relative to [Repository]metadata_root as defined in the
    // vesta.cfg file.  If prepend is nonempty, its value is prepended
    // to the string that would otherwise be returned.  The caller is
    // responsible for freeing the storage returned.
    static char* shortIdToName(ShortId sid, const char* prepend ="") throw();

    // RPC to get a new block of ShortIds from the server, all with
    // the specified leafflag.  On return, the block is leased to the
    // caller until block->leaseExpires (set by the function).  If the
    // lease expires without being renewed (below), the server will
    // eventually notice and reclaim all unused ShortIds in the block.
    // The caller is responsible for freeing the storage returned.
    //
    static ShortIdBlock *acquire(bool leafflag =false) throw (SRPC::failure);

    // RPC to renew the lease on a block.  Returns false if the block's
    // lease had already expired.  (Allowing a lease to expire is a fatal
    // client error.)  Otherwise, returns true, and the block is leased to
    // the caller until block.leaseExpires (set by the function).  
    //
    static bool renew(ShortIdBlock *block) throw (SRPC::failure);

    // RPC to return a block to the server.  The caller promises not
    // to assign any more ShortIds from this block, even if some are
    // still unused.  This routine should be called when a process is
    // going to shut down, or when it has used up a block.
    // This routine does not free the local storage for the block.
    //
    static void release(ShortIdBlock *block) throw (SRPC::failure);

    // RPC Weeder interface.  See SourceOrDerived.H.
    static int keepDerived(ShortIdsFile ds, time_t dt, bool force)
      throw (SRPC::failure);
    static void checkpoint() throw (SRPC::failure);
    static void getWeedingState(ShortIdsFile& ds, time_t& dt,
				ShortIdsFile& ss, time_t& st,
				bool& sourceWeedInProgress,
				bool& deletionsInProgress,
				bool& deletionsDone,
				bool& checkpointInProgress)
      throw(SRPC::failure);
};

#endif // _VSIDB



