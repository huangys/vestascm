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

// nfsStats.C - implementation of a class which gathers NFS call
// statistics

// Last modified on Mon May 23 21:52:28 EDT 2005 by ken@xorian.net

#include "nfsStats.H"

#include "ReposStats.H"

Basics::mutex NFS_Call_Stats::head_mu;
NFS_Call_Stats *NFS_Call_Stats::head = 0;

void NFS_Call_Stats::accumulateStats(/*OUT*/ Basics::uint64 &calls,
				     /*OUT*/ Basics::uint64 &secs,
				     /*OUT*/ Basics::uint32 &usecs)
{
  this->mu.lock();
  calls += this->call_count;
  secs += this->elapsed_secs;
  usecs += this->elapsed_usecs;
  this->mu.unlock();
}

void NFS_Call_Stats::recordCall(Basics::uint32 secs, Basics::uint32 &usecs)
{
  this->mu.lock();
  // Record that another call has been completed.
  this->call_count++;

  // Add the time for this call to our running total.
  this->elapsed_secs += secs;
  this->elapsed_usecs += usecs;
  if(this->elapsed_usecs > USECS_PER_SEC)
    {
      this->elapsed_secs += this->elapsed_usecs/USECS_PER_SEC;
      this->elapsed_usecs %= USECS_PER_SEC;
    }
  this->mu.unlock();
}

void NFS_Call_Stats::getStats(/*OUT*/ Basics::uint64 &calls,
			      /*OUT*/ Basics::uint64 &secs,
			      /*OUT*/ Basics::uint32 &usecs)
{
  // Reset our parameters.
  calls = 0;
  secs = 0;
  usecs = 0;

  // Get the current list of instances
  NFS_Call_Stats *list;
  NFS_Call_Stats::head_mu.lock();
  list = NFS_Call_Stats::head;
  NFS_Call_Stats::head_mu.unlock();

  while(list != 0)
    {
      // Accumulate into our parameters
      list->accumulateStats(calls, secs, usecs);

      // Handle usecs overflow
      if(usecs > USECS_PER_SEC)
	{
	  secs += usecs/USECS_PER_SEC;
	  usecs %= USECS_PER_SEC;
	}

      // Advance to the next instance.  (Note: no lock required to
      // access this member variable as it's set at instantiation and
      // never changed.)
      list = list->next;
    }
}

NFS_Call_Stats::NFS_Call_Stats()
  : next(0), call_count(0), elapsed_secs(0), elapsed_usecs(0)
{
  // Insert ourselves into the global list
  NFS_Call_Stats::head_mu.lock();
  this->next = NFS_Call_Stats::head;
  NFS_Call_Stats::head = this;
  NFS_Call_Stats::head_mu.unlock();
}

NFS_Call_Stats::~NFS_Call_Stats()
{
  // We don't ever expect to be destroyed.
  assert(false);
}

NFS_Call_Stats::Helper::Helper(NFS_Call_Stats &my_stats)
  : stats(my_stats)
{
  // Save the time that this call started
  struct timezone unused_tz;
  int err = gettimeofday(&this->call_start, &unused_tz);
  assert(err == 0);
}

NFS_Call_Stats::Helper::~Helper()
{
  // Get the time that this call ended
  struct timezone unused_tz;
  struct timeval call_end;
  int err = gettimeofday(&call_end, &unused_tz);
  assert(err == 0);

  // Compute the time taken by this call.
  Basics::uint32 call_secs, call_usecs;
  if(call_end.tv_sec == this->call_start.tv_sec)
    {
      call_secs = 0;
      if(call_end.tv_usec >= this->call_start.tv_usec)
	call_usecs = call_end.tv_usec - this->call_start.tv_usec;
      else
	// Time went backwards.  This can happen if the system time
	// gets adjusted.  Count this call as having taken 0 time.
	call_usecs = 0;
    }
  else if(call_end.tv_sec > this->call_start.tv_sec)
    {
      call_secs = (call_end.tv_sec - this->call_start.tv_sec) - 1;
      call_usecs = ((call_end.tv_usec + USECS_PER_SEC) -
		    this->call_start.tv_usec);
    }
  else
    {
      // Time went backwards.  This can happen if the system time gets
      // adjusted.  Count this call as having taken 0 time.
      call_secs = 0;
      call_usecs = 0;
    }

  // Record the time for this call in the stats recorder.
  stats.recordCall(call_secs, call_usecs);
}
