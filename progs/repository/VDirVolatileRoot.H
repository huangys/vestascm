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
// VDirVolatileRoot.H
// Last modified on Fri Feb 10 19:00:18 EST 2006 by ken@xorian.net
//      modified on Mon Nov 27 17:12:21 PST 2000 by mann
//
// One-of-a-kind pseudo-directory used to keep track of volatile
// directories that have been created.  Unlike true VestaSource
// objects, this object has real state, not just a short pointer into
// the sweepable memory pool.
//

#ifndef _VDIRVOLR
#define _VDIRVOLR

#include "VestaSource.H"
#include "ReadersWritersLock.H"

// This directory is protected by VolatileRootLock.  Each subtree
// immediately below it is protected by a separate lock of its own,
// which can be obtained using the lookupIndexAndLock method.  A write
// lock on the root and every tree below can be acquired/released with
// the lockAll/unlockAll methods.  Only one volatile root directory
// exists.  Multiple VDirVolatileRoot objects can be created, but they
// all reference the same directory.

class VDirVolatileRoot : public VestaSource {
  public:
    // Must be called exactly once during repository initialization.
    static void init() throw();

    // Our versions of virtual methods from base class.
    // These require VolatileRootLock.read and do not obtain any locks.
    inline void resync(AccessControl::Identity who =NULL) throw () { };
    VestaSource::errorCode lookup(Arc arc, VestaSource*& result,
		 AccessControl::Identity who =NULL,
		 unsigned int indexOffset =0) throw ();
    VestaSource::errorCode lookupIndex(unsigned int index,
		 VestaSource*& result,
		 char* arcbuf =NULL) throw();
    VestaSource::errorCode
      list(unsigned int firstIndex,
	   VestaSource::listCallback callback, void* closure,
	   AccessControl::Identity who =NULL,
	   bool deltaOnly =false, unsigned int indexOffset =0)
	throw ();
    inline time_t timestamp() throw() { return 2; };

    // Garbage collection support
    // Requrires VolatileRootLock.write and all subtree write locks.
    void mark(bool byName =true, ArcTable* hidden =NULL) throw();

    // Write the volatile memory checkpoint
    // Requrires VolatileRootLock.write and all subtree write locks.
    Bit32 checkpoint(Bit32& nextSP, std::fstream& ckpt) throw();

    // Checkpoint other state as log-style records
    // Requrires VolatileRootLock.write and all subtree write locks.
    static void finishCheckpoint(std::fstream& ckpt) throw();

    // Special methods for this object type only:

    // Create a new volatile directory under the volatile root, based
    // on an evaluatorDirectory specified by (hostname, port, handle), and
    // lock it.  May acquire and release StableLock, and takes care of
    // acquiring VolatileRootLock, so caller must not hold them.
    VestaSource::errorCode
      createVolatileDirectory(char* hostname, char* port, Bit64 handle,
			      VestaSource*& result, time_t timestamp,
			      LongId::lockKindTag lockKind,
			      ReadersWritersLock** lock,
			      bool readOnlyExisting) throw();


    // Delete by index.  Not needed in other kinds of directory.
    // Requrires VolatileRootLock.write.
    VestaSource::errorCode
      deleteIndex(unsigned int index) throw();

    // Lookup by index and lock the subtree.
    // Requrires VolatileRootLock.write.
    VestaSource::errorCode
      lookupIndexAndLock(unsigned int index, VestaSource*& result,
			 LongId::lockKindTag lockKind,
			 ReadersWritersLock** lock) throw();

    // Lock the root and all subtrees.
    static void lockAll();

    // Unlock the root and all subtrees.
    static void unlockAll();

    virtual VestaSource *copy() throw();
};


#endif // _VDIRVOLR
