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

// Created on Tue Oct  7 09:09:12 PDT 1997 by heydon
// Last modified on Mon May 23 22:29:34 EDT 2005 by ken@xorian.net  
//      modified on Mon Feb 14 18:27:21 PST 2000 by mann  
//      modified on Sun Aug 23 12:14:14 PDT 1998 by heydon

#include <stdlib.h> // for rand_r(3)
// add declaration to fix broken <stdlib.h>
extern "C" int _Prand_r(unsigned int *seedptr);

#include <Basics.H>
#include "FlushQueue.H"

using std::cout;
using std::cerr;
using std::endl;

static Basics::mutex mu;
static FlushQueue *q;

static void Syntax(char *msg, char *arg = (char *)NULL) throw ()
{
    cerr << "Error: " << msg;
    if (arg != (char *)NULL) cerr << ": `" << arg << "'";
    cerr << endl;
    cerr << "Syntax: TestFlushQueue [ -threads num ] [ -pause msecs ]" << endl;
    exit(1);
}

class ThreadArgs {
  public:
    ThreadArgs(int threadId, int msecPause) throw ()
	: threadId(threadId), msecPause(msecPause) { /*SKIP*/ }
    Basics::thread th;
    int threadId;
    int msecPause;
};

static void Pause(int total_msecs) throw ()
{
    int secs = total_msecs / 1000;
    int msecs = total_msecs % 1000;
    Basics::thread::pause(secs, msecs);
}

static void* ThreadBody(void *voidarg) throw ()
// "voidarg" is actually of type "ThreadArgs *"
{
    ThreadArgs *args = (ThreadArgs *)voidarg;
    unsigned int seed = args->threadId;
    while (true) {
	// random pause
	int r = rand_r(&seed);
	unsigned long pause = 5L * (long)(args->msecPause) * (long)r;
	Pause((int)(pause / RAND_MAX));
	
	// enqueue
	mu.lock();
	cout << "Thread " << args->threadId << ": enqueued" << endl;
	q->Enqueue();
	cout << "Thread " << args->threadId << ": working" << endl;
	mu.unlock();

	// do work
	Pause(args->msecPause);

	// dequeue
	mu.lock();
	cout << "Thread " << args->threadId << ": dequeued" << endl;
	q->Dequeue();
	mu.unlock();

    }

    //return (void *)NULL; // not reached
}

int main(int argc, char *argv[]) 
{
    // command-line args
    int numThreads = 5;
    int msecPause = 300;

    // parse command-line
    int arg = 1;
    while (arg < argc) {
	char *curr = argv[arg];
	if (*curr == '-') {
	    if (!strcmp(curr, "-threads")) {
		arg++;
		if (arg < argc) {
		    if (sscanf(argv[arg], "%d", &numThreads) != 1) {
			Syntax("illegal argument to -threads", argv[arg]);
		    }
		    arg++;
		} else {
		    Syntax("no argument supplied to switch", curr);
		}
	    } else if (!strcmp(curr, "-pause")) {
		arg++;
		if (arg < argc) {
		    if (sscanf(argv[arg], "%d", &msecPause) != 1) {
			Syntax("illegal argument to -pause", argv[arg]);
		    }
		    arg++;
		} else {
		    Syntax("no argument supplied to switch", curr);
		}
	    } else {
		Syntax("unrecognized switch", curr);
	    }
	} else {
	    Syntax("unrecognized argument", curr);
	}
    }
    assert(arg == argc);

    // initialize queue
    q = NEW_CONSTR(FlushQueue, (&mu));

    // fork threads
    for (int i = 0; i < numThreads; i++) {
      ThreadArgs *args = NEW_PTRFREE_CONSTR(ThreadArgs, (i + 1, msecPause));
      args->th.fork_and_detach(ThreadBody, (void *)args);
    }

    // pause indefinitely
    Basics::cond c;
    mu.lock();
    while (true) c.wait(mu);
    //mu.unlock(); // not reached
}
