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
// ReadersWritersLock.C
//
// Readers/writer locks
//

#include "ReadersWritersLock.H"
#include "Basics.H"
#include "lock_timing.H"

#include <assert.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef USE_PTHREAD_RWLOCK
struct RWLock_Queue_Item
{
  // The next and previous queue elements.
  RWLock_Queue_Item *next, *prev;

  // Is this for a waiting writer?
  bool writer;

  // The number of waiting threads (only used by readers).
  unsigned int waiting_count;

  // Conditional variable signalled when it's this items turn.
  Basics::cond my_turn;

#if !defined(NO_ACQUIRE_DEBUG_DATA)
  // Debug info: enqueue_time and enqueue_thread are set when this
  // object is created (and placed in the waiting queue for a
  // ReadersWritersLock).
  time_t enqueue_time;
  pthread_t enqueue_thread;
#endif

  RWLock_Queue_Item(bool write, RWLock_Queue_Item *&tail)
    : writer(write), waiting_count(0),
      next(0), prev(tail)
  {
    // We're the tail now.
    tail = this;

    // If there was a previous entry, we're it's next entry.
    if(prev)
      {
	assert(prev->next == 0);
	prev->next = this;
      }

#if !defined(NO_ACQUIRE_DEBUG_DATA)
    // Remember when this object was enqueued.  (This is sometimes
    // useful for post-motrem debugging.)
    enqueue_time = time(NULL);
    enqueue_thread = pthread_self();
#endif
  }

  ~RWLock_Queue_Item()
  {
    // A queue item should only ever be deleted once it has reached
    // the head.
    assert(prev == 0);

    // Maintain list integrity.
    if(next)
      {
	assert(next->prev == this);
	next->prev = 0;
      }
  }
};
#endif

ReadersWritersLock::~ReadersWritersLock() throw()
{
#ifdef USE_PTHREAD_RWLOCK
  // Loop needed in case another thread holds the lock.
  while(1)
    {
      // Try to destroy the lock.
      int result = pthread_rwlock_destroy(&rwlock);
      // If we succeeded, we're done.
      if(result == 0) break;
      // The only error we expect is EBUSY (meaning that another
      // thread has locked it since we released it above).
      assert(result == EBUSY);
      // Acquire a write lock, possibly blocking.  (This should wait
      // until no other thread is using it.)
      result = pthread_rwlock_wrlock(&rwlock);
      assert(result == 0);
      // Release the lock we just acquired, as we're about to try to
      // destroy it again.
      result = pthread_rwlock_unlock(&rwlock);
      assert(result == 0);
    }
#else
    // Ensure that no other thread has it locked or is waiting to lock
    // it.
  while(1)
    {
      // Get a write lock.
      acquireWrite();
      // Check to see if any threads are waiting for the lock.
      mu.lock();
      bool waiters = (q_head != 0);
      mu.unlock();
      if(waiters)
	{
	  // Some other thread waiting.  Release the lock to allow
	  // them to get it and try again.
	  releaseWrite();
	}
      else
	{
	  // We have it write locked and no other thread holds the
	  // lock: safe to delete it.
	  break;
	}
    }
#endif
}

ReadersWritersLock::ReadersWritersLock(bool favorWriters) throw()
{
#ifdef USE_PTHREAD_RWLOCK
  write_locked = false;
  while(1)
    {
      int result = pthread_rwlock_init(&rwlock, NULL);
      // If we succeeded, we're done.
      if(result == 0) break;
      // The only error we expect is EAGAIN.
      assert(result == EAGAIN);
    }
#else
    readers = 0;
    writers = 0;

    q_head = 0;
    q_tail = 0;

#if !defined(NO_ACQUIRE_DEBUG_DATA)
    acquire_time = 0;
    acquire_thread = 0;
#endif

#endif
}

void ReadersWritersLock::acquireRead() throw()
{
  RWLOCK_ACQUIRE_READ_START(this);
#ifdef USE_PTHREAD_RWLOCK
  while(1)
    {
      int result = pthread_rwlock_rdlock(&rwlock);
      // If we succeeded, we're done.
      if(result == 0) break;
      // The only error we expect is EAGAIN.
      assert(result == EAGAIN);
    }
#else
    mu.lock();
    // If there's a writer ...
    if((writers > 0) ||
       // .. or another thread is waiting ...
       ((q_head != 0) &&
	// ... and it's not the special case of a single group of
	// readers some of which haven't acquired the lock yet (in
	// which case it's safe to acquire a read lock immediately)
	// ...
	!((q_head == q_tail) && !q_head->writer)))
      // ... wait our turn.
      {
	RWLock_Queue_Item *q_entry = 0;

	// If the tail is a reader, wait along with it (as the
	// condition variable will be broadcast).
	if((q_tail != 0) && (!q_tail->writer))
	  {
	    assert(q_head != 0);
	    q_entry = q_tail;
	    
	  }
	// Otherwise create a new entry and put it at the end of the
	// queue.
	else
	  {
	    q_entry = NEW_CONSTR(RWLock_Queue_Item, (false, this->q_tail));

	    // If this is the first entry, it's the head also.
	    if(q_head == 0)
	      {
		assert(q_tail->prev == 0);
		q_head = q_entry;
	      }

	    // q_head and q_tail must either both be non-zero or both be
	    // zero.
	    assert(((q_head != 0) && (q_tail != 0)) ||
		   ((q_head == 0) && (q_tail == 0)));
	  }

	assert(q_entry != 0);

	q_entry->waiting_count++;
	q_entry->my_turn.wait(this->mu);
	q_entry->waiting_count--;

	// We should only ever be woken up when this entry is at the
	// head and there are no writers.
	assert(q_entry == q_head);
	assert(writers == 0);

	// If nobody's waiting here any more, remove this entry.
	if(q_entry->waiting_count == 0)
	  {
	    q_head = q_head->next;

	    // If case this was the last entry in the queue, fix
	    // q_tail as well.
	    if(q_tail == q_entry)
	      {
		assert(q_head == 0);
		q_tail = 0;
	      }

	    // q_head and q_tail must either both be non-zero or both be
	    // zero.
	    assert(((q_head != 0) && (q_tail != 0)) ||
		   ((q_head == 0) && (q_tail == 0)));

	    // Delete the entry
	    delete q_entry;
	  }
      }

    // There's now one more reader.
    readers++;

#if !defined(NO_ACQUIRE_DEBUG_DATA)
    if(readers == 1)
      {
	// If we're the first reader, save the debug info
	acquire_time = time(NULL);
	acquire_thread = pthread_self();
      }
#endif

    mu.unlock();
#endif
    RWLOCK_ACQUIRE_READ_DONE(this, false);
}

void ReadersWritersLock::releaseRead() throw()
{
  RWLOCK_RELEASE_START(this);
#ifdef USE_PTHREAD_RWLOCK
  int result = pthread_rwlock_unlock(&rwlock);
  // We expect no errors.
  assert(result == 0);
#else
    mu.lock();

    // There's one less reader now
    assert(readers > 0);
    readers--;

#if !defined(NO_ACQUIRE_DEBUG_DATA)
    if(readers == 0)
      {
	// If we were the last reader, erase the debug info
	acquire_time = 0;
	acquire_thread = 0;
      }
#endif

    // If there are no more readers and there's a thread waiting its
    // turn, signal it to wake up.
    if((readers == 0) && (q_head != 0))
      {
	// There are only two possible cases:

	// 1. This is a thread waiting for a write lock, and thus only
	// one thread will be waiting.

	// 2. This is a bundle of readers that this thread was a part
	// of, but not all threads in it have gotten their turn yet.

	// We could probably use signal rather than broadcast, but it
	// shouldn't hurt to broadcast.
	q_head->my_turn.broadcast();
      }
    mu.unlock();
#endif
    RWLOCK_RELEASE_DONE(this);
}

void ReadersWritersLock::acquireWrite() throw()
{
  RWLOCK_ACQUIRE_WRITE_START(this);
#ifdef USE_PTHREAD_RWLOCK
  while(1)
    {
      int result = pthread_rwlock_wrlock(&rwlock);
      // If we succeeded, we're done.
      if(result == 0) break;
      // The only error we expect is EAGAIN.
      assert(result == EAGAIN);
    }
  write_locked = true;
#else
    mu.lock();

    // If any threads hold the lock or writer another thread is
    // waiting, wait our turn.
    if((readers > 0) || (writers > 0) || (q_head != 0))
      {
	// Allocate a queue entry on the stack.  We can do this for
	// writers, as only one thread will use this queue entry.
	RWLock_Queue_Item q_entry(true, this->q_tail);

	// If this is the first entry, it's the head also.
	if(q_head == 0)
	  {
	    assert(q_tail->prev == 0);
	    q_head = &q_entry;
	  }

	// q_head and q_tail must either both be non-zero or both be
	// zero.
	assert(((q_head != 0) && (q_tail != 0)) ||
	       ((q_head == 0) && (q_tail == 0)));

	// Wait our turn.
	q_entry.my_turn.wait(this->mu);

	// We should only ever be woken up when this entry is at the
	// head and no thread holds the lock.
	assert(&q_entry == q_head);
	assert((writers == 0) && (readers == 0));

	// Remove this entry from the head of the queue.
	q_head = q_head->next;

	// If case this was the last entry in the queue, fix
	// q_tail as well.
	if(q_tail == &q_entry)
	  {
	    assert(q_head == 0);
	    q_tail = 0;
	  }

	// q_head and q_tail must either both be non-zero or both be
	// zero.
	assert(((q_head != 0) && (q_tail != 0)) ||
	       ((q_head == 0) && (q_tail == 0)));
      }
    writers++;

#if !defined(NO_ACQUIRE_DEBUG_DATA)
    // We're the sole writer, so save the debug info
    acquire_time = time(NULL);
    acquire_thread = pthread_self();
#endif

    mu.unlock();
#endif
    RWLOCK_ACQUIRE_WRITE_DONE(this, false);
}

void ReadersWritersLock::releaseWrite() throw()
{
  RWLOCK_RELEASE_START(this);
#ifdef USE_PTHREAD_RWLOCK
  assert(write_locked);
  write_locked = false;
  int result = pthread_rwlock_unlock(&rwlock);
  // We expect no errors.
  assert(result == 0);
#else
    mu.lock();
    assert(writers == 1);
    writers = 0;

#if !defined(NO_ACQUIRE_DEBUG_DATA)
    // We were the sole writer, so erase the debug info
    acquire_time = 0;
    acquire_thread = 0;
#endif

    // If there are threads waiting, signal at least one to wake up.
    if(q_head != 0)
      {
	// If it's a writer, there will be only one thread waiting on
	// this condition variable.  If it's a reader, there may be
	// multiple threads waiting.  Therefore we always broadcast.
	q_head->my_turn.broadcast();
      }
    mu.unlock();
#endif
    RWLOCK_RELEASE_DONE(this);
}

bool ReadersWritersLock::tryRead() throw()
{
  RWLOCK_ACQUIRE_READ_START(this);
  bool gotit;
#ifdef USE_PTHREAD_RWLOCK
  int result = pthread_rwlock_tryrdlock(&rwlock);
  gotit = (result == 0);
#else
    mu.lock();
    // If any thread holds a write lock, or any thread is queued
    // waiting for the lock, don't take it.  Otherwise, there can only
    // be readers at the moment so acquire the lock.
    if((writers > 0) || (q_head != 0)) {
        gotit = false;
    } else {
        gotit = true;
        readers++;

#if !defined(NO_ACQUIRE_DEBUG_DATA)
	if(readers == 1)
	  {
	    // If we're the first reader, save the debug info
	    acquire_time = time(NULL);
	    acquire_thread = pthread_self();
	  }
#endif
    }
    mu.unlock();
#endif
    RWLOCK_ACQUIRE_READ_DONE(this, !gotit);
    return gotit;
}

bool ReadersWritersLock::tryWrite() throw()
{
  bool gotit;
  RWLOCK_ACQUIRE_WRITE_START(this);
#ifdef USE_PTHREAD_RWLOCK
  int result = pthread_rwlock_trywrlock(&rwlock);
  if(result == 0) write_locked = true;
  gotit = (result == 0);
#else
    mu.lock();
    // If any thread holds the lock, or any thread is queued waiting
    // for the lock, don't take it.  Otherwise, acquire the lock.
    if ((readers > 0) || (writers > 0) || (q_head != 0)) {
        gotit = false;
    } else {
        gotit = true;
        writers++;
#if !defined(NO_ACQUIRE_DEBUG_DATA)
	// We're the sole writer, so save the debug info
	acquire_time = time(NULL);
	acquire_thread = pthread_self();
#endif
    }
    mu.unlock();
#endif
    RWLOCK_ACQUIRE_WRITE_DONE(this, !gotit);
    return gotit;
}

bool ReadersWritersLock::release() throw()
{
  RWLOCK_RELEASE_START(this);
  bool hadwrite;
#ifdef USE_PTHREAD_RWLOCK
  hadwrite = write_locked;
  if(write_locked) write_locked = false;
  int result = pthread_rwlock_unlock(&rwlock);
  // We expect no errors.
  assert(result == 0);
#else
    mu.lock();
    hadwrite = writers > 0;
    if (hadwrite) {
	assert(writers == 1);
	writers = 0;
    } else {
	assert(readers > 0);
	readers--;
    }

#if !defined(NO_ACQUIRE_DEBUG_DATA)
    if(hadwrite || (readers == 0))
      {
	// If we were the last one holding the lock, erase the debug
	// info
	acquire_time = 0;
	acquire_thread = 0;
      }
#endif

    // If there are threads waiting, and either the lock is no longer
    // held or readers are waiting while other threads hold a read
    // lock, signal at least one to wake up.
    if((q_head != 0) &&
       (hadwrite || (readers == 0) || !q_head->writer))
      {
	// If it's a writer, there will be only one thread waiting on
	// this condition variable.  If it's a reader, there may be
	// multiple threads waiting.  Therefore we always broadcast.
	q_head->my_turn.broadcast();
      }
    
    mu.unlock();
#endif
    RWLOCK_RELEASE_DONE(this);
    return hadwrite;
}

/*
void ReadersWritersLock::downgradeWriteToRead() throw()
{
#ifdef USE_PTHREAD_RWLOCK
  // We must have the write lock
  assert(write_locked);
  write_locked = false;
  // Release the lock.
  int result = pthread_rwlock_unlock(&rwlock);
  // We expect no errors.
  assert(result == 0);
  // Loop to acquire read lock.
  while(1)
    {
      int result = pthread_rwlock_rdlock(&rwlock);
      // If we succeeded, we're done.
      if(result == 0) break;
      // The only error we expect is EAGAIN.
      assert(result == EAGAIN);
    }
#else
    mu.lock();
    assert(writers == 1);
    writers = 0;
    assert(readers == 0);
    readers = 1;
    mu.unlock();
    waitingToRead.broadcast();
#endif
}

bool ReadersWritersLock::downgrade() throw()
{
#ifdef USE_PTHREAD_RWLOCK
  bool hadwrite = write_locked;
  if(write_locked) write_locked = false;
  int result = pthread_rwlock_unlock(&rwlock);
  // We expect no errors.
  assert(result == 0);
  // Loop to acquire read lock.
  while(1)
    {
      int result = pthread_rwlock_rdlock(&rwlock);
      // If we succeeded, we're done.
      if(result == 0) break;
      // The only error we expect is EAGAIN.
      assert(result == EAGAIN);
    }
  return hadwrite;
#else
    bool hadwrite;
    mu.lock();
    hadwrite = writers > 0;
    if (hadwrite) {
	assert(writers == 1);
	assert(readers == 0);
	writers = 0;
	readers = 1;
    } else {
	assert(readers > 0);
    }
    mu.unlock();
    if (hadwrite) {
        waitingToRead.broadcast();
    }
    return hadwrite;
#endif
}

*/
