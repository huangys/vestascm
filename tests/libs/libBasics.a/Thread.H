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

#ifndef _thread
#define _thread

//  ***************************************
//  *  Simplified threads implementation  *
//  ***************************************

namespace Basics
{
  class thread;

  //  ****************************
  //  *  Mutex class definition  *
  //  ****************************

  class mutex {
    friend class cond;
  public:
    // The default constructor creates a mutex of default type.
    mutex() throw ();

    // This constructor creates a mutex of specified type. Valid types:

    //   PTHREAD_MUTEX_NORMAL
    //   PTHREAD_MUTEX_ERRORCHECK
    //   PTHREAD_MUTEX_RECURSIVE
    //   PTHREAD_MUTEX_DEFAULT

    // See pthread_mutexattr_settype(3) manual for descriptions of the
    // mutex types
    mutex(int type) throw();

    ~mutex() throw ();

    void lock() throw ();
    void unlock() throw ();
    bool trylock() throw ();

  private:
    pthread_mutex_t m;
    // Try to ensure that the mutex doesn't share a cache line with
    // anything else.
    char pad[64];
  };

  //  *****************************************
  //  *  Condition variable class definition  *
  //  *****************************************

  // The Linux system headers define "signal" as a macro with non-GNU
  // compilers.  This causes us trouble, since we have a method named
  // "signal" and want to compile with the Compaq compiler on Linux.
#ifdef signal
#undef signal
#endif

  class cond {
  public:
    cond() throw ();
    ~cond() throw ();

    void wait(mutex &m) throw ();
    int timedwait(mutex &m, struct timespec *abstime) throw ();
    /* This returns 0 if the condition variable was signalled, or
       ETIMEDOUT if the condition variable was not signalled before
       time "abstime". */

    void signal() throw ();
    void broadcast() throw ();

  private:
    pthread_cond_t c;
    // Try to ensure that the condition variable doesn't share a cache
    // line with anything else.
    char pad[64];
  };

  class thread_attr
  {
  public:
    thread_attr() throw ();
    // Convenience constructor provided to make it easier to support
    // existing code that onyl wants to specify a stack size.
    thread_attr(size_t stacksize) throw ();
    thread_attr(const thread_attr &other) throw();
    ~thread_attr() throw ();

    void set_guardsize(size_t guardsize) throw();
    size_t get_guardsize() const throw();
    void set_stacksize(size_t stacksize) throw();
    size_t get_stacksize() const throw();

#if defined (_POSIX_THREAD_PRIORITY_SCHEDULING)
    void set_schedpolicy(int policy) throw();
    int get_schedpolicy() const throw();
    void set_inheritsched(int inheritsched) throw();
    int get_inheritsched() const throw();
    void set_sched_priority(int sched_priority) throw();
    int get_sched_priority() const throw();
#endif

    thread_attr &operator=(const thread_attr &other) throw();

  private:
    pthread_attr_t attr;

    // Allow the thread class to reach inside and get our
    // pthread_attr_t.
    friend class thread;
  };

  //  *****************************
  //  *  Thread class definition  *
  //  *****************************

  class thread {
  public:
    thread() throw () { /* SKIP */ }
    thread(const thread &other) throw() : t(other.t) { }
    ~thread() throw () { /* SKIP */ }

    typedef void* (*StartRoutine)(void *arg) /*throw ()*/;
    // pointer to function (pointer to void) returning pointer to void
    // The thread should not call pthread_exit() or be cancelled.

    void fork(StartRoutine proc, void *arg, const thread_attr &attr) throw ();
    void fork(StartRoutine proc, void *arg) throw ()
    {
      thread_attr attr;
      fork(proc, arg, attr);
    }
    void fork(StartRoutine proc, void *arg, long stacksize) throw ()
    {
      thread_attr attr;
      if(stacksize > 0)
	attr.set_stacksize(stacksize);
      fork(proc, arg, attr);
    }
    /* If "stacksize" is not defaulted, it specifies the stack size (in bytes)
       of the forked thread. */

    void detach() throw ();

    void fork_and_detach(StartRoutine proc, void *arg, const thread_attr &attr)
      throw ();
    void fork_and_detach(StartRoutine proc, void *arg) throw ()
    {
      thread_attr attr;
      fork_and_detach(proc, arg, attr);
    }
    void fork_and_detach(StartRoutine proc, void *arg, long stacksize) throw ()
    {
      thread_attr attr;
      if(stacksize > 0)
	attr.set_stacksize(stacksize);
      fork_and_detach(proc, arg, attr);
    }
    /* If "stacksize" is not defaulted, it specifies the stack size (in bytes)
       of the forked thread. */

    void *join() throw ();

    thread &operator=(const thread &other) throw()
    {
      t = other.t;
      return *this;
    }

    void get_sched_param(/*OUT*/ int &policy,
			 /*OUT*/ int &sched_priority) throw();
    void set_sched_param(int policy, int sched_priority) throw();

    static void pause(int secs, int usecs = 0) throw ();
    /* Pause the calling thread for "secs" seconds plus "usecs"
       microseconds. */

    static thread self() throw();
    // Return a thread object referring to the calling thread.

  private:
    thread(pthread_t existing) throw () : t(existing) { }
    pthread_t t;
    void fork_inner(StartRoutine proc, void *arg, const thread_attr &attr)
      throw ();
  };
}

#endif  /* _thread */
