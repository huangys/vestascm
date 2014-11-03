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

// ReposStatsSRPC.H - client API for getting statistics from the
// repository

#ifndef _REPOSSTATSSRPC_H
#define _REPOSSTATSSRPC_H 1

#include <Sequence.H>
#include <Table.H>
#include <SRPC.H>
#include "AccessControl.H"
#include "ReposStats.H"

namespace ReposStats
{
  // A key type for Table, used below
  struct StatKey
  {
    StatKind kind;
    StatKey() throw() : kind(invalid) { }
    StatKey(StatKind k) throw() : kind(k) { }

    // methods required by "Table.H"
    Word Hash() const throw () { return (Word)kind; }
    StatKind& operator = (const StatKind& val) { return (kind = val); }
    bool operator == (const StatKey& other) throw ()
    { return kind == other.kind; }
  };

  // Clients request statistics with a sequence of statistic types
  typedef Sequence<StatKind> StatsRequest;

  // Clients receive results as a table of statistics
  typedef Table<StatKey, Stat*>::Default StatsResult;
  typedef Table<StatKey, Stat*>::Iterator StatsResultIter;

  // Call to get statistics
  void getStats(/*OUT*/ StatsResult &result,
		const StatsRequest &request,
		AccessControl::Identity who =NULL,
		Text reposHost ="",
		Text reposPort ="",
		bool use_rt = false, unsigned int rt_secs = 60)
    throw (SRPC::failure);

  // Basic information about the server
  struct ServerInfo
  {
    // The software version of the server
    Text version;
    // The time that the server was started
    Basics::uint64 start_time;
    // The number of seconds the server has been running
    Basics::uint32 uptime;
  };

  // Call to get basic information about the server
  void getServerInfo(/*OUT*/ ServerInfo &result,
		     AccessControl::Identity who =NULL,
		     Text reposHost ="",
		     Text reposPort ="",
		     bool use_rt = false, unsigned int rt_secs = 60)
    throw (SRPC::failure);
}

#endif
