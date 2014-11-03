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

// Last modified on Wed Nov 27 12:50:07 EST 2002 by ken@xorian.net
//      modified on Fri Jul 19 09:45:56 PDT 1996 by heydon

/* A "Timer::T" is an object for measuring short elapsed times. It reports
   times in milliseconds.

   A timer object is in one of two states: stopped or running. Initially, a
   timer is stopped. There are methods for starting and stopping the timer.
   Whenever a timer is stopped, the time it had been running is added to its
   total elapsed time. A timer may be started and stopped multiple times; the
   total elapsed time accumulates the running time of each interval. The
   "Reset" method can be used to set the total elapsed time to 0.

   The methods of a "Timer::T" are not thread-safe. */

#ifndef _TIMER_H
#define _TIMER_H

#include <sys/time.h>
#include <Basics.H>

class Timer {
  public:
    // A "Timer::T" measures time in microseconds
    typedef Basics::uint64 MicroSecs;

    // the granularity of the timer
    static MicroSecs Grain; // read-only after initialization

    class T {
      public:
	T() throw () : running(false), elapsed(0UL) { /* SKIP */ }
	/* Create a new timer. Initially, the timer is not running and
           has an elapsed time of 0 usecs. */

	void Start() throw ();
	/* Start the timer. A checked run-time error occurs if the timer is
	   already running. */

	MicroSecs Stop() throw ();
	/* Stop the timer, update the total elapsed time, and return the
	   total time since the previous "Start" operation (in usecs).
           The reported time will be either 0 or >= "Grain". A checked
	   run-time error occurs if the timer is not running. */

	MicroSecs Reset() throw ();
	/* Return the total elapsed time of the timer and reset that elapsed
	   time to 0 usecs. A checked run-time error occurs if the timer is
	   running. */

	MicroSecs Elapsed() const throw ()
	    { assert(!this->running); return this->elapsed; }
	/* Report the elapsed time of the timer. A checked run-time error
           occurs if the timer is running. */

      private:
	bool running;		  // is the timer running?
	MicroSecs elapsed;        // total elapsed time
	struct timeval startTime; // starting time of last "Start" operation
    };
};

#endif // _TIMER_H
