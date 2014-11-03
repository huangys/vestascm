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

// Last modified on Mon May 23 22:50:58 EDT 2005 by ken@xorian.net
//      modified on Thu Sep 25 14:14:24 PDT 1997 by heydon

#include <Basics.H>
#include <FS.H>
#include <VestaLog.H>
#include <FP.H>
#include <BitVector.H>
#include <CacheState.H>

#include "CacheConfigServer.H"
#include "EmptyPKLog.H"
#include "VPKFile.H"
#include "SMultiPKFile.H"
#include "VMultiPKFile.H"

using std::ifstream;

bool VMultiPKFile::IsFull() throw ()
/* REQUIRES Sup(LL) = CacheS.mu */
{
    bool res = (this->numNewEntries >= Config_MPKFileFlushNum)
	&& (this->numWaiting == 0) && (!(this->autoFlushPending));
    if (res) this->autoFlushPending = true;
    return res;
}

bool VMultiPKFile::LockForWrite(Basics::mutex &mu, const BitVector *toDelete)
     throw()
{
    // return immediately if there's no work
    if (this->numNewEntries == 0 && toDelete == (BitVector *)NULL) {
	return false;
    }

    // wait until no threads are running "ToSCache" for this MultiPKFile
    while (this->numRunning > 0) {
	assert(this->numRunning == 1);
	this->numWaiting++;
	this->noneRunning.wait(mu);
	this->numWaiting--;
    }

    /* If there were multiple threads waiting to go, the first one probably
       had work to do, but the rest probably don't. So, we check again to
       see if we can exit early. */
    if (this->numNewEntries == 0 && toDelete == (BitVector *)NULL) {
	return false;
    }

    // we've now committed to doing the flush
    assert(this->numRunning == 0);
    this->numRunning++;
    this->autoFlushPending = false;

    return true;
}


bool VMultiPKFile::ChkptForWrite(Basics::mutex &mu, const BitVector *toDelete,
				 /*OUT*/ SMultiPKFile::VPKFileMap &toFlush,
				 /*OUT*/ SMultiPKFile::ChkPtTbl &vpkChkptTbl)
     throw()
{
    // copy "this->tbl" to "toFlush" (pointers to the VPKFiles only)
    mu.lock();
    toFlush = this->tbl;
    this->numNewEntries = 0;
    this->freeMPKFileEpoch = -1;
    mu.unlock();

    // Now checkpoint in memory the current state of the VPKFiles and
    // determine whether there is any work to be done.
    bool l_result = SMultiPKFile::ChkptForRewrite(toFlush, toDelete,
						  vpkChkptTbl);

    // If it turns out that we have no writing to do, update
    // "numRunning" and signal waiting threads (if any)
    if(!l_result)
      {
	this->ReleaseWriteLock(mu);
      }

    // Return the result of SMultiPKFile::ChkptForRewrite.
    return l_result;
}

void VMultiPKFile::ReleaseWriteLock(Basics::mutex &mu) throw()
/* It's worth noting that, as of this writing, PrepareForWrite commits
   us to performing the write, and this function doesn't completely
   undo that.  However, all possible errors past the point of
   PRepareForWrite (in either flushing the graph log or writing the
   MultiPKFile) are currently considered fatal.  If that ever ceases
   to be the case, this function will need to do some more work. */
{
  mu.lock();
  this->numRunning--;
  assert(this->numRunning == 0);
  mu.unlock();
  this->noneRunning.signal();
}

void VMultiPKFile::ToSCache(Basics::mutex &mu,

			    // From SMultiPKFile::PrepareForRewrite
			    bool mpkFileExists, ifstream &ifs,
			    SMultiPKFileRep::Header *hdr,

			    // From ChkptForRewrite above
			    SMultiPKFile::VPKFileMap &toFlush,
			    SMultiPKFile::ChkPtTbl &vpkChkptTbl,

			    const BitVector *toDelete,
			    EmptyPKLog *emptyPKLog,
			    /*INOUT*/ EntryState &state)
  throw (FS::Failure, FS::EndOfFile, VestaLog::Error)
/* REQUIRES (forall vf: VPKFile :: Sup(LL) < vf.mu) */
{
  // Not only should there be only one, but this thread should be it.
  assert(this->numRunning == 1);

    // flush VPKFile's in "toFlush"
    // This must be called without any locks!
    SMultiPKFile::Rewrite(prefix,
			  mpkFileExists, ifs, hdr,
			  toFlush, vpkChkptTbl,
			  toDelete, emptyPKLog,
      /*INOUT*/ state);

    // remove empty VPKFiles from this VMultiPKFile
    /*** NYI ***/

    // update "numRunning"; signal waiting threads (if any)
    this->ReleaseWriteLock(mu);
} // VMultiPKFile::ToSCache

bool VMultiPKFile::IsStale(int latestEpoch) throw ()
/* REQUIRES Sup(LL) = CacheS.mu */
{
    return (0 <= this->freeMPKFileEpoch
            && this->freeMPKFileEpoch <= (latestEpoch -
					  Config_FlushNewPeriodCnt));
}
