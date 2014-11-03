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

// ----------------------------------------------------------------------

// This version of the "ThreadStart" interface can be used wither
// without garbage collection or with the Boehm garbage collector.

// (Actually, what needs to be done to make the garbage collector to
// work correctly differs from platform to platform.  Using GNU ld,
// calls to pthread_create get re-directed to a wrapper provided by
// the collector, so we don't need to do anything at thread startup
// time.)

#include <signal.h>

#include "Basics.H"
#include "ThreadStart.H"

extern "C" void *Basics::ThreadStart_Callback(void *arg) throw ()
{
    // save arguments locally
    ForkArgs &forkArgs = *(ForkArgs *)arg;

    // set up signal handlers
    signal(SIGPIPE, SIG_IGN);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
    signal(SIGILL,  SIG_DFL);
    signal(SIGBUS,  SIG_DFL);

#if defined(__linux__) && defined(GPROF_SUPPORT)
    // gprof support (see comment in ThreadStart.H)
    setitimer(ITIMER_PROF, &(forkArgs.itimer), NULL);
#endif

    // invoke the real procedure
    void *res = (*(forkArgs.proc))(forkArgs.arg);
    delete (ForkArgs *)arg;
    return res;
}

void Basics::ThreadStart_WaitForChild(ForkArgs *forkArgs) throw ()
{
    // We only need to wait when running with the DECwest garbage
    // collector (in which case we won't be using this source file).
}
