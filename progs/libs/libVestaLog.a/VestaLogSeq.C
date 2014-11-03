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

// Created on Fri Mar  7 13:45:15 PST 1997 by heydon

// Last modified on Fri Apr 22 11:35:32 EDT 2005 by ken@xorian.net
//      modified on Thu Mar 13 13:53:58 PST 1997 by heydon

#include <Basics.H>
#include <FS.H>
#include "VestaLog.H"
#include "Recovery.H"
#include "VestaLogSeq.H"

using std::fstream;

void VestaLogSeq::Open(int ver, bool readonly) throw (VestaLog::Error)
{
    assert(!this->opened);
    this->vlog.open(this->dir, ver, readonly);
    this->opened = true;
    this->readChkpt = false;
    this->chkptFS = (fstream *)NULL;
    this->rr = (RecoveryReader *)NULL;
}

RecoveryReader* VestaLogSeq::Next(int endVer)
  throw (VestaLog::Error, FS::Failure)
{
    assert(this->opened);

    // handle checkpoint file
    if (!this->readChkpt) {
	// try opening the checkpoint first
	this->readChkpt = true;
	if ((this->chkptFS = this->vlog.openCheckpoint()) != (fstream *)NULL) {
	  return NEW_CONSTR(RecoveryReader, (this->chkptFS));
	}
    } else if (this->chkptFS != (fstream *)NULL) {
	// close previously-opened checkpoint file (if any)
	FS::Close(*(this->chkptFS));
	this->chkptFS = (fstream *)NULL;
    }

    // try opening the next log file
    if (this->rr == (RecoveryReader *)NULL) {
	// process first log file
	if (endVer < 0 || this->vlog.logVersion() < endVer) {
	  this->rr = NEW_CONSTR(RecoveryReader, (&(this->vlog)));
	    return this->rr;
	}
    } else {
	// second & subsequent log files
	if (this->vlog.nextLog() &&
            (endVer < 0 || this->vlog.logVersion() < endVer)) {
	    return this->rr;
	}
    }

    // no more log files
    return (RecoveryReader *)NULL;
}

void VestaLogSeq::Close() throw ()
{
    assert(this->opened);

    // close previously-opened checkpoint file (if any)
    if (this->chkptFS != (fstream *)NULL) {
	FS::Close(*(this->chkptFS));
	this->chkptFS = (fstream *)NULL;
    }

    // close log
    this->vlog.close();

    // clean up for re-use
    this->opened = false;
    this->rr = (RecoveryReader *)NULL;
}
