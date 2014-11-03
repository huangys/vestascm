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


#include <Basics.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <FP.H>
#include <CacheConfig.H>
#include <CacheIntf.H>
#include <PKEpoch.H>
#include <Debug.H>

#include "EmptyPKLog.H"

using std::fstream;
using std::ostream;
using std::endl;
using OS::cio;

/* Note: Although the cache server's "CleanCacheLog" method checkpoints the
   "emptyPKLog", it does not write anything into that checkpoint. Hence,
   the checkpoint file can be ignored at recovery. */

EmptyPKLog::EmptyPKLog(Basics::mutex *mu, CacheIntf::DebugLevel debug,
		       bool readonly)
  throw (VestaLog::Error)
/* REQUIRES Sup(LL) = CacheS.mu */
  : mu(mu), debug(debug)
{
  this->log = NEW(VestaLog);
  this->log->open(Config_EmptyPKLogPath.chars(), /*ver=*/ -1, readonly);
  this->rd = NEW_CONSTR(RecoveryReader, (this->log));
  this->oldEmptyPKTbl = NULL;
  this->emptyPKTbl = NEW(EmptyPKLog::PKEpochTbl);
}

void EmptyPKLog::Read(/*OUT*/ FP::Tag &pk, /*OUT*/ PKFile::Epoch &pkEpoch)
  throw (VestaLog::Error, VestaLog::Eof)
/* REQUIRES Sup(LL) = CacheS.mu && ``private log in recovering state'' */
{
    // read the pair from the log
    pk.Recover(*(this->rd));
    this->rd->readAll((char *)(&pkEpoch), sizeof(pkEpoch));

    // add it to the table
    PKFile::Epoch tblEpoch;
    bool inTbl = this->emptyPKTbl->Get(pk, /*OUT*/ tblEpoch);
    // entries in log are (strictly) monotonically increasing
    assert(!inTbl || pkEpoch > tblEpoch);
    bool inTbl2 = this->emptyPKTbl->Put(pk, pkEpoch);
    assert(inTbl == inTbl2);
}

void EmptyPKLog::CheckpointBegin()
  throw (FS::Failure, VestaLog::Error)
/* REQUIRES Sup(LL) < CacheS.mu && ``private log in ready state'' */
{
    this->mu->lock();
    try {
	fstream *chkptFile = this->log->checkpointBegin();
	// the EmptyPKLog checkpoint is always empty
	FS::Close(*chkptFile);
	this->oldEmptyPKTbl = this->emptyPKTbl;
	this->emptyPKTbl = NEW(EmptyPKLog::PKEpochTbl);
    } catch (...) { this->mu->unlock(); throw; }
    this->mu->unlock();
}

void EmptyPKLog::CheckpointEnd() throw (VestaLog::Error)
/* REQUIRES Sup(LL) < CacheS.mu && ``private log in ready state'' */
{
    this->mu->lock();
    try {
	this->log->checkpointEnd();
	this->log->prune(/*ckpkeep=*/ 1);
	delete this->oldEmptyPKTbl;
	this->oldEmptyPKTbl = NULL;
    } catch (...) { this->mu->unlock(); throw; }
    this->mu->unlock();
}

void EmptyPKLog::Write(const FP::Tag &pk, PKFile::Epoch pkEpoch)
  throw (VestaLog::Error)
/* REQUIRES Sup(LL) < CacheS.mu && ``private log in ready state'' */
{
    this->mu->lock();
    PKFile::Epoch tblEpoch = 0;
    if (this->emptyPKTbl->Get(pk, /*OUT*/ tblEpoch)) {
	assert(pkEpoch >= tblEpoch);
    }
    if (pkEpoch > tblEpoch) {
	if (this->debug >= CacheIntf::LogFlush) {
	  cio().start_out() << Debug::Timestamp() 
			    << "Writing entry to EmptyPKLog" << endl
			    << "  pk = " << pk << endl
			    << "  pkEpoch = " << pkEpoch << endl << endl;
	  cio().end_out();
	}
	(void)this->emptyPKTbl->Put(pk, pkEpoch);
	try {
	    this->log->start();
	    pk.Log(*(this->log));
	    this->log->write((char *)(&pkEpoch), sizeof(pkEpoch));
	    this->log->commit();
	}
	catch (...) {
	    this->mu->unlock();
	    throw;
	}
    }
    this->mu->unlock();
}

bool EmptyPKLog::GetEpoch0(const FP::Tag &pk, /*OUT*/ PKFile::Epoch &pkEpoch)
  throw ()
/* REQUIRES Sup(LL) = CacheS.mu */
{
  if (this->emptyPKTbl->Get(pk, /*OUT*/ pkEpoch)) return true;
  if (this->oldEmptyPKTbl == NULL) return false;
  return this->oldEmptyPKTbl->Get(pk, /*OUT*/ pkEpoch);
}

bool EmptyPKLog::GetEpoch(const FP::Tag &pk, /*OUT*/ PKFile::Epoch &pkEpoch)
  throw ()
/* REQUIRES Sup(LL) < CacheS.mu */
{
    this->mu->lock();
    bool res = this->GetEpoch0(pk, /*OUT*/ pkEpoch);
    this->mu->unlock();
    return res;
}
