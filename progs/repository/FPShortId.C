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

// FPShortId.C
// Last modified on Mon Jan  9 22:47:39 EST 2006 by ken@xorian.net
//      modified on Tue Aug  7 18:15:00 PDT 2001 by mann
//      modified on Fri Aug 14 11:06:34 PDT 1998 by nas

// Fingerprint to ShortId lookup table
// Very simple hash table that stores short pointers into
// the compressed data structures. Use with caution.

// Structure similar to DirShortId.C but with own table class

#include "VestaLog.H"
#include "VRConcurrency.H"
#include "FP.H"
#include "VMemPool.H"
#include "VDirChangeable.H"
#include "FPShortId.H"
#include "Basics.H"

#include "math.h"
#include <VestaConfig.H>
#include <Thread.H>

// Test value
#define START_FPTBL_SIZE 5000
#define MAX_FPTBL_SIZE 50000000
#define RESIZE_TRIGGER 0.7
#define RESIZE_FACTOR 2.0

// Types

// Module globals
static Basics::mutex mu;
static FPShortIdTable *fpFileSidTable;
static FPShortIdTable *fpDirSidTable;
// end mutex

// Utility functions

FP::Tag
DirFP(Bit8* rep)
{
    VDirChangeable vdc(VestaSource::immutableDirectory, rep);
    return vdc.fptag();
}

ShortId
DirId(Bit8* rep) {
    VDirChangeable vdc(VestaSource::immutableDirectory, rep);
    return vdc.getID();
}

void
InitFPShortId()
{
    fpFileSidTable =
      NEW_CONSTR(FPShortIdTable,
		 (VDirChangeable::efptag,
		  VDirChangeable::value,
		  START_FPTBL_SIZE, RESIZE_FACTOR, RESIZE_TRIGGER));
    fpDirSidTable =
      NEW_CONSTR(FPShortIdTable,
		 (DirFP, DirId,
		  START_FPTBL_SIZE, RESIZE_FACTOR, RESIZE_TRIGGER));
}

void
SetFPFileShortId(Bit8 *entry) 
{
    mu.lock();
    assert(fpFileSidTable->Set(entry, true));
    mu.unlock();
}

void
SetFPDirShortId(Bit8 *rep) 
{
    mu.lock();
    assert(fpDirSidTable->Set(rep, true));
    mu.unlock();
}

ShortId
GetFPShortId(const FP::Tag& fptag)
{
    mu.lock();
    ShortId sid;
    sid = fpFileSidTable->Get(fptag);
    if (sid == NullShortId) {
	sid = fpDirSidTable->Get(fptag);
    }
    mu.unlock();
    
    return sid;
}

void
DeleteAllFPShortId()
{
    mu.lock();
    fpFileSidTable->Clear();
    fpDirSidTable->Clear();
    mu.unlock();
}

// FP Table class

FPShortIdTable::FPShortIdTable(GetFP getfp, GetSid getsid,
			       Bit32 sizeHint, double resizeF, double resizeT)
{
    this->getfp = getfp;
    this->getsid = getsid;
    tableSize = sizeHint;
    resizeFactor = resizeF;
    resizeTrigger = resizeT;
    table = NEW_PTRFREE_ARRAY(Bit32, sizeHint);
    numEntries = 0;
    memset(table, 0, tableSize * sizeof(Bit32));    
}

FPShortIdTable::~FPShortIdTable()
{
    delete table;
    tableSize = 0;
}

Bit32 FPShortIdTable::Size()
{
    return tableSize;
}

Bit32 FPShortIdTable::Entries()
{
    return numEntries;
}

bool FPShortIdTable::Resize(Bit32 size)
{
    Bit32 *oldTable = table;
    Bit32 oldTableSize = tableSize;
    bool ret = true;
    int i;
    
    table = NEW_PTRFREE_ARRAY(Bit32, size);
    if (!table) return false;
    tableSize = size;
    numEntries = 0;

    memset(table, 0, tableSize * sizeof(Bit32));

    for (i = 0; i < oldTableSize; i++) {
        if (oldTable[i] != 0) {
            Bit8 *ptr = (Bit8*)VMemPool::lengthenPointer(oldTable[i]);
            ret = Set(ptr, false);
	    assert(ret);
        }
    }
    delete [] oldTable;

    return ret;
}


// Lookup a fingerprint in the table
ShortId FPShortIdTable::Get(const FP::Tag& fptag)
{
    Bit32 i,j,k;
    FP::Tag efptag;
    
    i = j = fptag.Hash() % tableSize;

    if (table[i] == (Bit32)NULL) return NullShortId;
    Bit8 *ptr = (Bit8*)VMemPool::lengthenPointer(table[i]);
    efptag = getfp(ptr);
    i = (i + 1) % tableSize;

    // Use the overloaded comparison operator
    while((!(fptag == efptag)) && i!=j)
    {
        if (table[i] == (Bit32)NULL) return NullShortId;
        ptr = (Bit8*)VMemPool::lengthenPointer(table[i]);
        efptag = getfp(ptr);
        i = (i + 1) % tableSize;
    }
    
    if (i!=j) return getsid(ptr); 
    else return NullShortId;    
}

// Register an FP::Tag with a VestaSource
bool FPShortIdTable::Set(Bit8 *ptr, bool resize) 
{
    Bit32 i,j;
    FP::Tag fptag;
    
    fptag = getfp(ptr);

    if (resize && (double)numEntries/(double)tableSize > resizeTrigger)
        assert(Resize((Bit32)floor((double)tableSize*resizeFactor)));
    
    i = j = fptag.Hash() % tableSize;
    while (table[i]) {
	if (i == j-1) return false;
	FP::Tag fptag2;
	fptag2 = getfp((Bit8*)VMemPool::lengthenPointer(table[i]));
	if (fptag == fptag2) break;
	i = (i+1) % tableSize;
    }
    
    table[i] = VMemPool::shortenPointer(ptr);
    
    numEntries++;
    return true;
}

// Empty out the table
void
FPShortIdTable::Clear()
{
    numEntries = 0;
    memset(table, 0, tableSize * sizeof(Bit32));    
}

