#define USING_GC
// Copyright (C) 2001, Compaq Computer Corporation

// This file is part of Vesta.

// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

//  ***************************************
//  *  Simplified threads implementation  *
//  ***************************************

#ifdef __linux__
// On Linux, we need this #define to get pthread_attr_setguardsize
#define _XOPEN_SOURCE 500
#endif

// When we're using the Boehm garbage collector, we need to include
// the garbage collector's header file so it can hijack a few of the
// pthread functions.
#if defined(USING_GC)

// First include the auto-generated header of #defines for the gc
// library configuration.
#if !defined(__osf__)
#include <used_gc_flags.h>
#endif

// Now include gc.h
#include <gc.h>

#endif

#include "pthreadcompat.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h> // just to get sprintf
#include <sys/time.h>
#include "Basics.H"
#include "ThreadStart.H"

// Some platforms (Tru64) define this, and some (Linux) don't.
#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC (1000000000)
#endif

using std::cerr;
using std::endl;

//  ***********************************
//  *  Simplified pthread operations  *
//  ***********************************

//  Utility failure reporting procedure

static void report (int code, char *who) throw ()
{
  // Convenient visibility in the debugger :-)
  cerr << "pthread_" << who << " failed: "
       << Basics::errno_Text(code)
       << " (errno: " << code << ")" << endl;
  abort();
}


//  **************************
//  *  Mutex implementation  *
//  **************************

Basics::mutex::mutex() throw ()
{
  int code = pthread_mutex_init(&(this->m), (pthread_mutexattr_t *)NULL);
  if (code != 0) report(code, "mutex_init");  
}

Basics::mutex::mutex(int type) throw ()
{
  pthread_mutexattr_t attr;
  int code = pthread_mutexattr_init(&attr);
  if(code != 0) report(code, "mutexattr_init");
  code = pthread_mutexattr_settype(&attr, type); 
  if(code != 0) report(code, "mutexattr_settype");

  code = pthread_mutex_init(&(this->m), &attr);
  if(code != 0) report(code, "mutex_init");  
  
  code = pthread_mutexattr_destroy(&attr);
  if(code != 0) report(code, "mutexattr_destroy");
}

Basics::mutex::~mutex() throw ()
{ /* SKIP */ }

void Basics::mutex::lock() throw ()
{
  int code = pthread_mutex_lock(&(this->m));
  if (code != 0) report(code, "mutex_lock");
}

void Basics::mutex::unlock() throw ()
{
  int code = pthread_mutex_unlock(&(this->m));
  if (code != 0) report(code, "mutex_unlock");
}

bool Basics::mutex::trylock() throw ()
{
  int code = pthread_mutex_trylock(&(this->m));
  if (code == 0)
    return true;
  if (code == EBUSY)
    return false;
  report(code, "mutex_trylock");
}


//  ***************************************
//  *  Condition variable implementation  *
//  ***************************************

Basics::cond::cond() throw ()
{
  int code = pthread_cond_init(&(this->c), (pthread_condattr_t *)NULL);
  if (code != 0) report(code, "cond_init");  
}

Basics::cond::~cond() throw ()
{ /* SKIP */ }

void Basics::cond::wait(mutex &m) throw ()
{
  int code = pthread_cond_wait(&(this->c), &(m.m));
  if (code != 0) report(code, "cond_wait");
}

int Basics::cond::timedwait(mutex &m, struct timespec *abstime) throw ()
{
  int code = pthread_cond_timedwait(&(this->c), &(m.m), abstime);
#ifdef _PTHREAD_D4_ // from "pthreadcompat.h"
  if (code != 0 && errno != 0 && errno != EAGAIN)
      report(errno, "cond_timedwait");
#else
  if (code != 0 && code != ETIMEDOUT)
      report(code, "cond_timedwait");
#endif
  return code;
}

void Basics::cond::signal() throw ()
{
  int code = pthread_cond_signal(&(this->c));
  if (code != 0) report(code, "cond_signal");
}

void Basics::cond::broadcast() throw ()
{
  int code = pthread_cond_broadcast(&(this->c));
  if (code != 0) report(code, "cond_broadcast");
}

//  ******************************************
//  *  Thread attributeclass implementation  *
//  ******************************************

// Default from the old thread::fork_inner
static const size_t g_min_guardsize = 8192;

Basics::thread_attr::thread_attr() throw ()
{
  int code = pthread_attr_init(/*OUT*/ &(this->attr));
  if (code != 0) report(code, "attr_init");

  if(get_guardsize() < g_min_guardsize)
    set_guardsize(g_min_guardsize);
}

Basics::thread_attr::thread_attr(size_t stacksize) throw ()
{
  int code = pthread_attr_init(/*OUT*/ &(this->attr));
  if (code != 0) report(code, "attr_init");

  if(get_guardsize() < g_min_guardsize)
    set_guardsize(g_min_guardsize);

  set_stacksize(stacksize);
}

Basics::thread_attr::thread_attr(const thread_attr &other) throw()
{
  int code = pthread_attr_init(/*OUT*/ &(this->attr));
  if (code != 0) report(code, "attr_init");

  set_guardsize(other.get_guardsize());
  set_stacksize(other.get_stacksize());
  
#if defined (_POSIX_THREAD_PRIORITY_SCHEDULING)
  set_schedpolicy(other.get_schedpolicy());
# if !defined(VALGRIND_SUPPORT)
  // valgrind chokes on the inheritsched pthread calls
  set_inheritsched(other.get_inheritsched());
# endif
  set_sched_priority(other.get_sched_priority());
#endif
}

Basics::thread_attr::~thread_attr() throw ()
{
  int code = pthread_attr_destroy(/*OUT*/ &(this->attr));
  if (code != 0) report(code, "attr_destroy");
}

Basics::thread_attr &Basics::thread_attr::operator=(const Basics::thread_attr &other) throw()
{
  set_guardsize(other.get_guardsize());
  set_stacksize(other.get_stacksize());
  
#if defined (_POSIX_THREAD_PRIORITY_SCHEDULING)
  set_schedpolicy(other.get_schedpolicy());
# if !defined(VALGRIND_SUPPORT)
  // valgrind chokes on the inheritsched pthread calls
  set_inheritsched(other.get_inheritsched());
# endif
  set_sched_priority(other.get_sched_priority());
#endif

  return *this;
}

void Basics::thread_attr::set_guardsize(size_t guardsize) throw()
{
  int code = pthread_attr_setguardsize(/*INOUT*/ &(this->attr),
				       max(guardsize, g_min_guardsize));
  if (code != 0) report(code, "attr_setguardsize");
}

size_t Basics::thread_attr::get_guardsize() const throw()
{
  size_t result;
  int code = pthread_attr_getguardsize(&(this->attr), /*OUT*/ &result);
  if (code != 0) report(code, "attr_getguardsize");
  return result;
}

void Basics::thread_attr::set_stacksize(size_t stacksize) throw()
{
  int code = pthread_attr_setstacksize(/*INOUT*/ &(this->attr),
				       max(stacksize, PTHREAD_STACK_MIN));
  if (code != 0) report(code, "attr_setstacksize");
}

size_t Basics::thread_attr::get_stacksize() const throw()
{
  size_t result;
  int code = pthread_attr_getstacksize(&(this->attr), /*OUT*/ &result);
  if (code != 0) report(code, "attr_getstacksize");
  return result;
}

#if defined (_POSIX_THREAD_PRIORITY_SCHEDULING)
void Basics::thread_attr::set_schedpolicy(int policy) throw()
{
  int code = pthread_attr_setschedpolicy(/*INOUT*/ &(this->attr), policy);
  if (code != 0) report(code, "attr_schedpolicy");
}

int Basics::thread_attr::get_schedpolicy() const throw()
{
  int result;
  int code = pthread_attr_getschedpolicy(&(this->attr), /*OUT*/ &result);
  if (code != 0) report(code, "attr_getschedpolicy");
  return result;
}

void Basics::thread_attr::set_inheritsched(int inheritsched) throw()
{
  int code = pthread_attr_setinheritsched(/*INOUT*/ &(this->attr),
					  inheritsched);
  if (code != 0) report(code, "attr_setinheritsched");
}

int Basics::thread_attr::get_inheritsched() const throw()
{
  int result;
  int code = pthread_attr_getinheritsched(&(this->attr), /*OUT*/ &result);
  if (code != 0) report(code, "attr_getinheritsched");
  return result;
}

void Basics::thread_attr::set_sched_priority(int sched_priority) throw()
{
  // The only defined member of struct sched_param is sched_priority,
  // but there may be others, so we first get it, then set the
  // sched_param member, then set it.
  struct sched_param my_sched_param;
  int code = pthread_attr_getschedparam(/*INOUT*/ &(this->attr),
					&my_sched_param);
  if (code != 0) report(code, "attr_getschedparam");
  my_sched_param.sched_priority = sched_priority;
  code =  pthread_attr_setschedparam(/*INOUT*/ &(this->attr),
				     &my_sched_param);
  if (code != 0) report(code, "attr_setschedparam");
}

int Basics::thread_attr::get_sched_priority() const throw()
{
  struct sched_param my_sched_param;
  int code = pthread_attr_getschedparam(/*INOUT*/ &(this->attr),
					&my_sched_param);
  if (code != 0) report(code, "attr_getschedparam");
  return my_sched_param.sched_priority;
}
#endif

//  *********************************
//  *  Thread class implementation  *
//  *********************************

void Basics::thread::fork_inner(StartRoutine proc, void *arg,
				const thread_attr &attr)
  throw ()
{
  ForkArgs *forkArgs = NEW(ForkArgs);
  forkArgs->proc = proc;
  forkArgs->arg = arg;
  forkArgs->started = false;

#if defined(__linux__) && defined(GPROF_SUPPORT)
  // gprof support (see comment in ThreadStart.H)
  getitimer(ITIMER_PROF, &(forkArgs->itimer));
#endif

  int code = pthread_create(&(this->t), &(attr.attr),
    ThreadStart_Callback, (void *)forkArgs);
  if (code != 0) {
    report(code, "create");
  }

  // wait until child is running
  ThreadStart_WaitForChild(forkArgs);
}

void Basics::thread::fork(StartRoutine proc, void *arg,
			  const thread_attr &attr)
  throw ()
{
  fork_inner(proc, arg, attr);
}

void Basics::thread::detach() throw ()
{
  int code = pthread_detach(this->t);
  if (code != 0) report(code, "detach");
}

void Basics::thread::fork_and_detach(StartRoutine proc, void *arg,
				     const Basics::thread_attr &attr)
  throw ()
{
  this->fork(proc, arg, attr);
  this->detach();
}

void *Basics::thread::join() throw ()
{
  void *result;
  int code = pthread_join(this->t, /*OUT*/ &result);
  if (code != 0) report(code, "join");
  return result;
}

void Basics::thread::get_sched_param(/*OUT*/ int &policy,
				     /*OUT*/ int &sched_priority) throw()
{
  struct sched_param my_sched_param;
  int code = pthread_getschedparam(this->t,
				   &policy,
				   &my_sched_param);
  if (code != 0) report(code, "getschedparam");
  sched_priority = my_sched_param.sched_priority;
}

void Basics::thread::set_sched_param(int policy, int sched_priority) throw()
{
  // The only defined member of struct sched_param is sched_priority,
  // but there may be others, so we first get it, then set the
  // sched_param member, then set it.
  int old_policy;
  struct sched_param my_sched_param;
  int code = pthread_getschedparam(this->t,
				   &old_policy,
				   &my_sched_param);
  if (code != 0) report(code, "getschedparam");
  my_sched_param.sched_priority = sched_priority;
  code = pthread_setschedparam(this->t,
			       policy,
			       &my_sched_param);
  if (code != 0) report(code, "setschedparam");
}

// dummy mutex and condition variable for "thread::pause"
static Basics::mutex dummyMu;
static Basics::cond dummyCond;

void Basics::thread::pause(int secs, int usecs) throw ()
{
    // get the current time
    int code;
    struct timeval now;
    if ((code = gettimeofday(&now, NULL)) != 0) {
	report(code, "gettimeofday");
    }

    struct timeval goal = { now.tv_sec + secs, now.tv_usec + usecs };

    // add "secs", "usecs" to current time
    struct timespec tv =
      { goal.tv_sec, 1000 * (goal.tv_usec) };

    // handle overflow in "tv.tv_nsec" field
    while (tv.tv_nsec > NSEC_PER_SEC) {
	tv.tv_nsec -= NSEC_PER_SEC;
	tv.tv_sec++;
    }

    do {
      // wait -- since nothing signals this condition var, it will time out
      dummyMu.lock();
      (void) dummyCond.timedwait(dummyMu, &tv);
      dummyMu.unlock();

      if ((code = gettimeofday(&now, NULL)) != 0) {
	report(code, "gettimeofday");
      }

      // Loop to handle early wakeups.
    } while(now.tv_sec < goal.tv_sec ||
	    now.tv_sec == goal.tv_sec && now.tv_usec < goal.tv_usec);
}

Basics::thread Basics::thread::self() throw()
{
  return thread(pthread_self());
}
