// Copyright (c) 2000, Compaq Computer Corporation
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

// File: ThreadData.H

#ifndef ThreadData_H
#define ThreadData_H

#include "EvalBasics.H"
#include <CacheC.H>
#include <BufStream.H>
#include <set>
#include <BasicsAllocator.H>

//
// Thread-specific data for threads started by _par_map
//
struct ThreadData {
  // A set of CIs.
  typedef std::set<CacheEntry::Index,
		   std::less<CacheEntry::Index>,
		   Basics::Allocator<CacheEntry::Index> > orphanCI_set;

  // List of all extant ThreadData objects
  static Basics::mutex mu;
  static ThreadData* head;
  ThreadData* prev;
  ThreadData* next; 

  // Fields of individual ThreadData objects
  int id;           // Dense, small integer ids.  Main thread should be 0.
  int funcCallDepth;
  Basics::OBufStream *traceRes;
  Exprs *callStack;
  ThreadData *parent;
  int parentCallStackSize;
  CacheEntry::IndicesApp* orphanCIs;

  // Orphaned CIs from child threads spawned by this one (i.e. as part
  // of a _par_map call) which have not yet been reclaimed by this
  // thread.  (We can't asynchronously add such CIs into our own
  // orphanCIs as threads complete, or they may become child CIs of
  // new cache entries added by this thread.)
  orphanCI_set unclaimed_child_orphanCIs;
};

// Create thread-specific data for the current thread.  Call this
// routine once from each new thread after forking it.  Caller assigns
// ids.  Also OK to call repeatedly from the same thread (e.g., the
// main thread); each call frees the old ThreadData and creates a
// fresh one.  The ThreadData object is automatically freed when a
// thread terminates.
ThreadData* ThreadDataCreate(int id, CacheEntry::IndicesApp *threadCIs);

// Get thread-specific data for the current thread.
ThreadData* ThreadDataGet();

// Initialize module.  Also calls ThreadDataCreate(0) for main thread.
void ThreadDataInit();

inline Text ThreadLabel()
{
  ThreadData *td = 0;
  if(maxThreads > 1) td = ThreadDataGet();
  return ((td != 0)
	  ? IntToText(td->id) + "/"
	  : Text(""));
}

// This mutex protects all the orphanCIs member variables in all the
// ThreadData instances.  (It's also used to protect some other data
// like addEntryCount in ApplyCache.C.)
extern Basics::mutex frontierMu;

#endif // ThreadData_H
