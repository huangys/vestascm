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
// ReadersWritersLock.H
//
// Readers/writers locks
//
#ifndef _RDWRLOCK
#define _RDWRLOCK 1

#include <pthread.h>
#include "Basics.H"

#ifdef PTHREAD_RWLOCK_INITIALIZER
// #define USE_PTHREAD_RWLOCK
#endif

#ifndef USE_PTHREAD_RWLOCK
struct RWLock_Queue_Item;
#endif

// If we're building an executable without assertions, we might as
// well strip out the acquire/enqueue time.
#if defined(NDEBUG)
#define NO_ACQUIRE_DEBUG_DATA
#endif

// Simple readers/writers locks.  There can be either many readers,
// one writer, or neither, but not both at once.

// If favorWriters is false, a new reader is granted the lock whenever
// possible (that is, when the lock is not held, or when other readers
// hold it).  If favorWriters is true, a new reader is not granted the
// lock if one or more writers are waiting to acquire it.

// Special features for repository: (1) mutex is static, (2) fields
// are public and methods are virtual to allow subclasses to add or
// override methods.

class ReadersWritersLock {
  private:
#ifdef USE_PTHREAD_RWLOCK
  pthread_rwlock_t rwlock;

  // Note: this member variable is only modified by a thread holding a
  // write lock.
  bool write_locked;
  
  // Try to ensure that the readers/writers lock doesn't chare a cache
  // line with anything else.
  char pad[64];

#else
    Basics::mutex mu;
    // Invariant: either readers == writers == 0, or
    //                   readers > 0 && writers == 0, or
    //                   readers == 0 && writers = 1
    int readers;
    int writers;

    RWLock_Queue_Item *q_head, *q_tail;

#if !defined(NO_ACQUIRE_DEBUG_DATA)
    // Debug info: acquire_time and acquire_thread are set when it is
    // first locked for reading or writing.  (When there are multiple
    // readers, only the first thread sets these.)  This is sometimes
    // useful for post-motrem debugging.
    time_t acquire_time;
    pthread_t acquire_thread;
#endif
  
#endif
  public:  // normal public methods
    ReadersWritersLock(bool favorWriters) throw();
    ~ReadersWritersLock() throw();
    void acquireRead() throw();
    void releaseRead() throw();  // assert(readers > 0);
    void acquireWrite() throw();
    void releaseWrite() throw(); // assert(writers == 1);

  // virtual void downgradeWriteToRead() throw(); // assert(writers == 1);

    // If lock can be acquired immediately, do so and return true.
    // If there is a conflict, return false.
    bool tryRead() throw();
    bool tryWrite() throw();

    // Assert (readers > 0) xor (writers == 1);
    // If writers == 1, releaseWrite and return true.
    // If readers > 0, releaseRead and return false.
    bool release() throw();

    // Assert (readers > 0) xor (writers == 1);
    // If writers == 1, downgradeWriteToRead and return true.
    // If readers > 0, return false.
    // virtual bool downgrade() throw();
};
#endif //_RDWRLOCK
