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
// ShortIdBlockBogusRPC.C
// Last modified on Fri Apr 22 12:32:39 EDT 2005 by ken@xorian.net
//      modified on Thu Jul 20 14:29:38 PDT 2000 by mann
//
// Glue code to link server and client in same address space instead
// of doing RPC.
//

#include "ShortIdBlock.H"
#include "ShortIdImpl.H"
#include "VRWeed.H"
#include <assert.h>

ShortIdBlock*
ShortIdBlock::acquire(bool leafflag)
  throw (SRPC::failure /*can't happen*/)
{
    ShortIdBlock* sib = NEW(ShortIdBlock);
    AcquireShortIdBlock(*sib, leafflag, true);
    return sib;
}

bool
ShortIdBlock::renew(ShortIdBlock* block)
  throw (SRPC::failure /*can't happen*/)
{
    assert(false);  // should never be called
    return true;
}

void
ShortIdBlock::release(ShortIdBlock* block)
  throw (SRPC::failure /*can't happen*/)
{
    ReleaseShortIdBlock(*block, true);
}

int
ShortIdBlock::keepDerived(ShortIdsFile ds, time_t dt, bool force)
  throw (SRPC::failure /*can't happen*/)
{
    return KeepDerived(ds, dt, force);
}

void
ShortIdBlock::checkpoint()
  throw (SRPC::failure /*can't happen*/)
{
    Checkpoint();
}

void
ShortIdBlock::getWeedingState(ShortIdsFile& ds, time_t& dt,
			      ShortIdsFile& ss, time_t& st,
			      bool& sourceWeedInProgress,
			      bool& deletionsInProgress,
			      bool& deletionsDone,
			      bool& checkpointInProgress)
  throw (SRPC::failure /*can't happen*/)
{
    GetWeedingState(ds, dt, ss, st, sourceWeedInProgress,
		    deletionsInProgress, deletionsDone, checkpointInProgress);
}
