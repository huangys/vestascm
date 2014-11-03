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
// DirShortId.C
// Last modified on Mon May 23 21:37:56 EDT 2005 by ken@xorian.net  
//      modified on Thu Jul 19 15:36:18 PDT 2001 by mann  
//      modified on Mon Apr 28 17:30:28 PDT 1997 by heydon

// ShortIds for immutable directories.  Server side only.

// The ids must be assigned *deterministically*, so that the same id
// gets assigned when a log is replayed during recovery as was
// assigned before.  Therefore the deletion of an id, making it
// available for reuse, must be logged.  We do this in a cute way to
// save memory.  The deletion phase of weeding deletes all the *still
// in use* ids from the table, then logs those that remain, which are
// the ones that are made available for reuse.  It then empties the
// table and repopulates it.

#include "DirShortId.H"
#include "ShortIdKey.H"
#include "logging.H"
#include "VestaLog.H"
#include "VRConcurrency.H"
#include "VDirChangeable.H"

#include <VestaConfig.H>
#include <Thread.H>

using std::fstream;

// Types
struct DirShortIdInfo {
    Bit32 sp;
};
typedef Table<ShortIdKey, DirShortIdInfo>::Default DirShortIdTable;
typedef Table<ShortIdKey, DirShortIdInfo>::Iterator DirShortIdIter;

// Module globals
static Basics::mutex mu; // protects the following:
static DirShortIdTable* dsidTable;
// end mutex

void
InitDirShortId() throw()
{
    dsidTable = NEW(DirShortIdTable);
}

// Allocate a new directory sid (i.e., one not currently registered),
// and register it as corresponding to a directory at short address
// sp.
ShortId
NewDirShortId(const FP::Tag& fptag, Bit32 sp) throw()
{
    mu.lock();
    DirShortIdInfo info;
    ShortIdKey sidkey;
    Word hash = fptag.Hash();
    do {
	sidkey.sid =
	  (ShortId) ((hash & ~ShortIdBlock::leafFlag) | ShortIdBlock::dirFlag);
	hash++;
    } while (dsidTable->Get(sidkey, info));
    info.sp = sp;
    dsidTable->Put(sidkey, info);
    mu.unlock();
    return sidkey.sid;
}

// Lookup a sid in the table.
Bit32 GetDirShortId(ShortId sid) throw()
{
    mu.lock();
    ShortIdKey sidkey(sid);
    DirShortIdInfo info;    
    if (!dsidTable->Get(sidkey, info)) {
	info.sp = 0;
    }
    mu.unlock();
    return info.sp;
}

// Register or reregister an existing directory sid as being at short
// address sp.
void
SetDirShortId(ShortId sid, Bit32 sp) throw()
{
    mu.lock();
    ShortIdKey sidkey(sid);
    DirShortIdInfo info;    
    info.sp = sp;
    (void) dsidTable->Put(sidkey, info);
    mu.unlock();
}

// Delete one ShortId registration.
void
DeleteDirShortId(ShortId sid) throw()
{
    mu.lock();
    ShortIdKey sidkey(sid);
    DirShortIdInfo info;    
    (void) dsidTable->Delete(sidkey, info);
    mu.unlock();
}

// Log all the ShortIds that are currently in the table.
// Assumes the correct locks for log access are held.
void
LogAllDirShortIds(char* tag)
{
    mu.lock();
    DirShortIdIter iter(dsidTable);
    ShortIdKey sidkey;
    DirShortIdInfo info;
    VRLog.start();
    VRLog.put("(");
    VRLog.put(tag);
    while (iter.Next(sidkey, info)) {
	char sid[16];
	sprintf(sid, " 0x%08x", sidkey.sid);
	VRLog.put(sid);
    }
    VRLog.put(")\n");
    VRLog.commit();
    mu.unlock();
}

// Checkpoint the ShortIds that are currently in the table and not
// checkpointed already.
void
CheckpointAllDirShortIds(Bit32& nextSP, fstream& ckpt)
{
    mu.lock();
    DirShortIdIter iter(dsidTable);
    ShortIdKey sidkey;
    DirShortIdInfo info;
    while (iter.Next(sidkey, info)) {
	VDirChangeable vs(VestaSource::immutableDirectory,
			  (Bit8*) VMemPool::lengthenPointer(info.sp));
	(void) vs.checkpoint(nextSP, ckpt);
    }
    mu.unlock();
}

// Delete all ShortId registrations, in preparation for rebuilding
// the registration table.
void
DeleteAllDirShortIds() throw()
{
    mu.lock();
    dsidTable->Init(dsidTable->Size());
    mu.unlock();    
}

