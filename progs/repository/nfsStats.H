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

// nfsStats.H - declaration of a class which gathers NFS call
// statistics

// Last modified on Mon May 23 21:52:13 EDT 2005 by ken@xorian.net

#ifndef NFSSTATS_H
#define NFSSTATS_H

#include <sys/time.h>

#include <Basics.H>

// An instance of this class gets created for each NFS server thread.
// It records the number of NFS calls and total time spent servicing
// all calls over the lifetime of the thread.
class NFS_Call_Stats
{
private:
  // Single linked list of all instances of this class.
  static Basics::mutex head_mu;
  static NFS_Call_Stats *head;
  NFS_Call_Stats *next;

  // Protext local fields.
  Basics::mutex mu;

  // Statistics for one NFS-servicing thread.
  Basics::uint64 call_count;
  Basics::uint64 elapsed_secs;
  Basics::uint32 elapsed_usecs;

  // Add the statistics from this instance into a running total.
  void accumulateStats(/*OUT*/ Basics::uint64 &calls,
		       /*OUT*/ Basics::uint64 &secs,
		       /*OUT*/ Basics::uint32 &usecs);

  // Record an NFS call.
  void recordCall(Basics::uint32 secs, Basics::uint32 &usecs);

public:

  NFS_Call_Stats();
  ~NFS_Call_Stats();

  static void getStats(/*OUT*/ Basics::uint64 &calls,
		       /*OUT*/ Basics::uint64 &secs,
		       /*OUT*/ Basics::uint32 &usecs);

  // Class whose creation and destruction mark the beginning and end
  // of a call.
  class Helper
  {
  private:
    struct timeval call_start;

    NFS_Call_Stats &stats;
  public:
    Helper(NFS_Call_Stats &my_stats);
    ~Helper();
  };

  friend class Helper;
};

#endif // NFSSTATS_H
