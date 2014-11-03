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

// Last modified on Sun May 22 21:34:32 EDT 2005 by ken@xorian.net
//      modified on Fri Aug 28 09:07:57 PDT 1998 by heydon

#ifndef _THREAD_START_H
#define _THREAD_START_H

// On Linux, gprof requires help to collect profile data from more
// than just the main thread.  The GPROF_SUPPORT macro turns on a
// feature in the thread start code that causes profile data to be
// recorded by all threads.  (It may be harmless to have this on all
// the time, but for now it's optional.)  For more information on this
// issue, see:

// http://sam.zoy.org/writings/programming/gprof.html

#if defined(__linux__) && defined(GPROF_SUPPORT)
#include <sys/time.h>
#endif

#include "Thread.H"

namespace Basics
{
  struct ForkArgs {
    thread::StartRoutine proc;
    void *arg;

    // fields for blocking parent until child is running
    mutex mu;
    bool started;
    cond untilStarted;

#if defined(__linux__) && defined(GPROF_SUPPORT)
    // gprof support
    struct itimerval itimer; 
#endif
  };

  extern "C" void *ThreadStart_Callback(void *arg) throw ();
  /* This is the function called by "thread::fork" and
     "thread::fork_and_detach". The argument "arg" is
     actually of type "ForkArgs *". */

  void ThreadStart_WaitForChild(ForkArgs *forkArgs) throw ();
  /* When linked with the garbage collector, this procedure
     must be called by the parent before it drops "forkArgs"
     on the floor to be collected. This procedure may block
     until the child thread has really started running and
     has noted "fa" on its thread stack to prevent it from
     being collected. */
}

#endif // _THREAD_START_H
