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

// Last modified on Thu Nov  8 12:36:38 EST 2001 by ken@xorian.net
//      modified on Fri Jul 19 09:50:03 PDT 1996 by heydon

#include <sys/time.h>
#include <Basics.H>
#include "Timer.H"

void Timer::T::Start() throw ()
{
    assert(!this->running);
    this->running = true;
    struct timezone tzp;
    int code = gettimeofday(&(this->startTime), &tzp); assert(code == 0);
}

Timer::MicroSecs Timer::T::Stop() throw ()
{
    assert(this->running);
    this->running = false;
    struct timeval endTime;
    struct timezone tzp;
    int code = gettimeofday(&endTime, &tzp); assert(code == 0);
    MicroSecs delta = 1000000UL * (endTime.tv_sec - this->startTime.tv_sec);
    delta += (endTime.tv_usec - this->startTime.tv_usec);
    this->elapsed += delta;
    return delta;
}

Timer::MicroSecs Timer::T::Reset() throw ()
{
    assert(!this->running);
    MicroSecs res = this->elapsed;
    this->elapsed = 0UL;
    return res;
}

Timer::MicroSecs Timer::Grain;

static void Timer_InitGrain() throw ()
/* The value for "Grain" is determined experimentally. */
{
    const int Incr = 100;
    int cnt = 0;
    Timer::T timer;
    while (1) {
	timer.Start();
	for (int j = 0; j < cnt; j++) {
	    int k = j/3; k *= 3;
	}
	Timer::MicroSecs t = timer.Stop();
	if (t != 0UL) {
	    Timer::Grain = t;
	    break;
	}
	cnt += Incr;
    }
}

class TimerInit {
  public:
    TimerInit() throw () { Timer_InitGrain(); }
};
static TimerInit t; // initialize granularity at start-up
