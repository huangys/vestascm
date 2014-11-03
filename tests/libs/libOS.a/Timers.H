// Copyright (C) 2007, Vesta Free Software Project
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

#ifndef _TIMERS_H_
#define _TIMERS_H_

#include <sys/time.h>
#include <Basics.H>

#define USECS_PER_SEC 1000000

namespace Timers
{
  // Compute the different between two points in time.  Accounts for
  // several strange cases that come up occasionally (such as time
  // going backwards due to system clock adjustments from automatic
  // adjustment tools like ntp).
  struct timeval delta_time(const struct timeval &time_a,
			    const struct timeval &time_b);

  // A class that extends struct timeval to make it easier to use it
  // as a delata between two points in time.
  class Delta : public timeval
  {
  public:
    Delta() { tv_sec = 0; tv_usec = 0; }

    Delta(const struct timeval &time_a,
	  const struct timeval &time_b)
    {
      *this = delta_time(time_a, time_b);
    }

    // Correct overflow of usecs by converting to secs.
    void usecs_overflow()
    {
      if(tv_usec >= USECS_PER_SEC)
	{
	  tv_sec += tv_usec/USECS_PER_SEC;
	  tv_usec %= USECS_PER_SEC;
	}
    }

    inline Delta &operator+=(const struct timeval &other)
    {
      tv_sec += other.tv_sec;
      tv_usec += other.tv_usec;
      usecs_overflow();

      return *this;
    }

    inline Delta &operator=(const struct timeval &other)
    {
      tv_sec = other.tv_sec;
      tv_usec = other.tv_usec;

      return *this;
    }

    operator double() const
    {
      double result = tv_sec;
      result += ((double)tv_usec)/USECS_PER_SEC;
      return result;
    }
  };

  // A class for accumulating total elapsed time.  Uses a 64-bit
  // integer for seconds, which might seem like overkill until you
  // consider accumulating total time from multiple threads.
  struct Elapsed
  {
    Basics::uint64 secs;
    Basics::uint32 usecs;

    Elapsed() : secs(0), usecs(0) { }

    // Correct overflow of usecs by converting to secs.
    void usecs_overflow()
    {
      if(usecs >= USECS_PER_SEC)
	{
	  secs += usecs/USECS_PER_SEC;
	  usecs %= USECS_PER_SEC;
	}
    }

    Elapsed &operator+=(const struct timeval &tv)
    {
      // We shouldn't ever get a negative elapsed time, but let's
      // protect ourselves just in case
      if((tv.tv_sec >= 0) && (tv.tv_usec >= 0))
	{
	  secs += tv.tv_sec;
	  usecs += tv.tv_usec;
	}
      usecs_overflow();
      return *this;
    }

    Elapsed &operator+=(const Elapsed &other)
    {
      secs += other.secs;
      usecs += other.usecs;
      usecs_overflow();
      return *this;
    }

    Elapsed operator-(const Elapsed &other) const
    {
      Elapsed result;

      if(secs > other.secs)
	{
	  result.secs = (secs - other.secs) - 1;
	  result.usecs = ((usecs + USECS_PER_SEC) -
			  other.usecs);
	  result.usecs_overflow();
	}
      else if(secs == other.secs)
	{
	  result.secs = 0;
	  if(usecs > other.usecs)
	    {
	      result.usecs = usecs - other.usecs;
	    }
	  else
	    {
	      result.usecs = 0;
	    }
	}
      else
	{
	  result.secs = 0;
	  result.usecs = 0;
	}
      return result;
    }

    bool operator <(const Elapsed &other) const
    {
      // Convert to sane values for comparison
      Elapsed lhs = *this, rhs = other;
      lhs.usecs_overflow();
      rhs.usecs_overflow();

      if(lhs.secs == rhs.secs)
	{
	  return (lhs.usecs < rhs.usecs);
	}
      return lhs.secs < rhs.secs;
    }
  };

  // Helper to record a single time interval
  class IntervalRecorder
  {
  private:
    struct timeval start_time;
  public:
    // start records the current time
    void start();

    // stop returns the difference between the start time and the
    // current time
    Delta get_delta();

    IntervalRecorder(bool start_now = true)
    {
      if(start_now) start();
    }
  };
}

#endif
