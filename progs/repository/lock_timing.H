// Copyright (C) 2004, Kenneth C. Schalk
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

// lock_timing.H - declarations for performance debugging of
// StableLock and VolatileRootLock.

// Last modified on Thu Dec  9 17:11:16 EST 2004 by ken@xorian.net

#ifndef _LOCK_TIMING_H_
#define _LOCK_TIMING_H_

#if defined(REPOS_PERF_DEBUG)

class ReadersWritersLock;

// These are meant to be called inside the ReadersWritersLock class.
void rwlock_acquire_read_start(ReadersWritersLock *lock_p);

#define RWLOCK_ACQUIRE_READ_START(lock) rwlock_acquire_read_start(lock)

void rwlock_acquire_read_done(ReadersWritersLock *lock_p, bool failed);

#define RWLOCK_ACQUIRE_READ_DONE(lock, fail) rwlock_acquire_read_done(lock, fail)

void rwlock_acquire_write_start(ReadersWritersLock *lock_p);

#define RWLOCK_ACQUIRE_WRITE_START(lock) rwlock_acquire_write_start(lock)

void rwlock_acquire_write_done(ReadersWritersLock *lock_p,bool failed);

#define RWLOCK_ACQUIRE_WRITE_DONE(lock, fail) rwlock_acquire_write_done(lock, fail)

void rwlock_release_start(ReadersWritersLock *lock_p);

#define RWLOCK_RELEASE_START(lock) rwlock_release_start(lock)

void rwlock_release_done(ReadersWritersLock *lock_p);

#define RWLOCK_RELEASE_DONE(lock) rwlock_release_done(lock)

// Record the reason a lock was acquired
void rwlock_locked_reason(ReadersWritersLock *lock_p, const char *reason);

#define RWLOCK_LOCKED_REASON(lock,reason) rwlock_locked_reason(lock,reason)

#if defined(__cplusplus)

// Turn timing recording on or off.
void rwlock_timing_control(bool enable);

#endif

#else // defined(REPOS_PERF_DEBUG)

#define RWLOCK_ACQUIRE_READ_START(lock) ((void)0)
#define RWLOCK_ACQUIRE_READ_DONE(lock, fail) ((void)0)
#define RWLOCK_ACQUIRE_WRITE_START(lock) ((void)0)
#define RWLOCK_ACQUIRE_WRITE_DONE(lock, fail) ((void)0)
#define RWLOCK_RELEASE_START(lock) ((void)0)
#define RWLOCK_RELEASE_DONE(lock) ((void)0)
#define RWLOCK_LOCKED_REASON(lock,reason) ((void)0)

#endif // defined(REPOS_PERF_DEBUG)

#endif
