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

#include "Timers.H"

#include <assert.h>

struct timeval Timers::delta_time(const struct timeval &time_a,
				  const struct timeval &time_b)
{
  struct timeval result;

  if(time_a.tv_sec == time_b.tv_sec)
    {
      result.tv_sec = 0;
      // Need to handle time moving backwards thanks to ntp
      result.tv_usec = ((time_b.tv_usec >= time_a.tv_usec)
			? time_b.tv_usec - time_a.tv_usec
			: 0);
    }
  else if(time_b.tv_sec > time_a.tv_sec)
    {
      result.tv_sec = (time_b.tv_sec - time_a.tv_sec) - 1;
      result.tv_usec = (time_b.tv_usec + USECS_PER_SEC) - time_a.tv_usec;
      if(result.tv_usec >= USECS_PER_SEC)
	{
	  result.tv_sec += result.tv_usec/USECS_PER_SEC;
	  result.tv_usec %= USECS_PER_SEC;
	}
    }
  else
    {
      // Need to handle time moving backwards thanks to ntp
      result.tv_sec = 0;
      result.tv_usec = 0;
    }

  return result;
}

void Timers::IntervalRecorder::start()
{
  int err = gettimeofday(&(this->start_time), 0);
  assert(err == 0);
}

Timers::Delta Timers::IntervalRecorder::get_delta()
{
  struct timeval end_time;
  int err = gettimeofday(&(end_time), 0);
  assert(err == 0);

  return Timers::Delta(this->start_time, end_time);
}
