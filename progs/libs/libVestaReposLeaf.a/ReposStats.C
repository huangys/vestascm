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

// ReposStats.C - implementation of everything declared in
// ReposStats.H and ReposStatsSRPC.H

#include "ReposStats.H"
#include "ReposStatsSRPC.H"
#include "VestaSourceSRPC.H"

using std::cerr;
using std::endl;

void ReposStats::FdCacheStats::send(SRPC *srpc) const throw (SRPC::failure)
{
  srpc->send_int32(this->n_in_cache);
  srpc->send_int64(this->hits);
  srpc->send_int64(this->open_misses);
  srpc->send_int64(this->try_misses);
  srpc->send_int64(this->evictions);
  srpc->send_int64(this->expirations);
}

void ReposStats::FdCacheStats::recv(SRPC *srpc) throw (SRPC::failure)
{
  this->n_in_cache = srpc->recv_int32();
  this->hits = srpc->recv_int64();
  this->open_misses = srpc->recv_int64();
  this->try_misses = srpc->recv_int64();
  this->evictions = srpc->recv_int64();
  this->expirations = srpc->recv_int64();
}

void ReposStats::DupeStats::send(SRPC *srpc) const throw (SRPC::failure)
{
  srpc->send_int64(this->non);
  srpc->send_int64(this->inProcess);
  srpc->send_int64(this->completed);
}

void ReposStats::DupeStats::recv(SRPC *srpc) throw (SRPC::failure)
{
  this->non = srpc->recv_int64();
  this->inProcess = srpc->recv_int64();
  this->completed = srpc->recv_int64();
}

void ReposStats::TimedCalls::send(SRPC *srpc) const throw (SRPC::failure)
{
  srpc->send_int64(this->call_count);
  srpc->send_int64(this->elapsed_secs);
  srpc->send_int32(this->elapsed_usecs);
}

void ReposStats::TimedCalls::recv(SRPC *srpc) throw (SRPC::failure)
{
  this->call_count = srpc->recv_int64();
  this->elapsed_secs = srpc->recv_int64();
  this->elapsed_usecs = srpc->recv_int32();
}

void ReposStats::MemStats::send(SRPC *srpc) const throw (SRPC::failure)
{
  srpc->send_int64(this->total);
  srpc->send_int64(this->resident);
}

void ReposStats::MemStats::recv(SRPC *srpc) throw (SRPC::failure)
{
  this->total = srpc->recv_int64();
  this->resident = srpc->recv_int64();
}

void ReposStats::VMemPoolStats::send(SRPC *srpc) const throw (SRPC::failure)
{
  srpc->send_int32(this->size);
  srpc->send_int32(this->free_list_blocks);
  srpc->send_int32(this->free_list_bytes);
  srpc->send_int32(this->free_wasted_bytes);
  srpc->send_int16(this->nonempty_free_list_count);
  srpc->send_int64(this->allocate_calls);
  srpc->send_int64(this->allocate_rej_sm_blocks);
  srpc->send_int64(this->allocate_rej_lg_blocks);
  srpc->send_int64(this->allocate_split_blocks);
  srpc->send_int64(this->allocate_new_blocks);
  srpc->send_int64(this->allocate_time.secs);
  srpc->send_int32(this->allocate_time.usecs);
  srpc->send_int64(this->free_calls);
  srpc->send_int64(this->free_coalesce_before);
  srpc->send_int64(this->free_coalesce_after);
  srpc->send_int64(this->free_time.secs);
  srpc->send_int32(this->free_time.usecs);
  srpc->send_int64(this->grow_calls);
}

void ReposStats::VMemPoolStats::recv(SRPC *srpc) throw (SRPC::failure)
{
  this->size = srpc->recv_int32();
  this->free_list_blocks = srpc->recv_int32();
  this->free_list_bytes = srpc->recv_int32();
  this->free_wasted_bytes = srpc->recv_int32();
  this->nonempty_free_list_count = srpc->recv_int16();
  this->allocate_calls = srpc->recv_int64();
  this->allocate_rej_sm_blocks = srpc->recv_int64();
  this->allocate_rej_lg_blocks = srpc->recv_int64();
  this->allocate_split_blocks = srpc->recv_int64();
  this->allocate_new_blocks = srpc->recv_int64();
  this->allocate_time.secs = srpc->recv_int64();
  this->allocate_time.usecs = srpc->recv_int32();
  this->free_calls = srpc->recv_int64();
  this->free_coalesce_before = srpc->recv_int64();
  this->free_coalesce_after = srpc->recv_int64();
  this->free_time.secs = srpc->recv_int64();
  this->free_time.usecs = srpc->recv_int32();
  this->grow_calls = srpc->recv_int64();
}

void
ReposStats::getStats(/*OUT*/ ReposStats::StatsResult &result,
		     const ReposStats::StatsRequest &request,
		     AccessControl::Identity who,
		     Text reposHost,
		     Text reposPort,
		     bool use_rt, unsigned int rt_secs)
  throw (SRPC::failure)
{
  // Convert the sequence of requested statistics into an array of
  // 16-bit integers for sending to the server, filtering for only the
  // kinds of statistics we actually know how to receive.
  Basics::int16 *request_seq = NEW_PTRFREE_ARRAY(Basics::int16,
						 request.size());
  int request_seq_len = 0;
  for(int i = 0; i < request.size(); i++)
    {
      ReposStats::StatKind kind = request.get(i);
      if((kind > ReposStats::invalid) && (kind < ReposStats::statKindEnd))
	{
	  request_seq[request_seq_len++] = (Basics::int16) kind;
	}
    }

  SRPC* srpc;
  MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, reposHost, reposPort);

  if(use_rt)
    {
      srpc->enable_read_timeout(rt_secs);
    }

  srpc->start_call(VestaSourceSRPC::GetStats,
		   VestaSourceSRPC::version);
  VestaSourceSRPC::send_identity(srpc, who);
  srpc->send_int16_array(request_seq, request_seq_len);
  srpc->send_end();
  // We're done with this now.
  delete [] request_seq;
  request_seq = 0;  // Help out GC programs.

  srpc->recv_seq_start();
  while(1)
    {
      bool done = false;
      ReposStats::StatKind kind =
	(ReposStats::StatKind) srpc->recv_int16(&done);
      if(done)
	break;
      switch(kind)
	{
	case ReposStats::fdCache:
	  {
	    ReposStats::FdCacheStats *fd_stats = NEW(ReposStats::FdCacheStats);
	    fd_stats->recv(srpc);
	    result.Put(kind, fd_stats);
	  }
	  break;
	case ReposStats::dupeTotal:
	  {
	    ReposStats::DupeStats *dupe_stats = NEW(ReposStats::DupeStats);
	    dupe_stats->recv(srpc);
	    result.Put(kind, dupe_stats);
	  }
	  break;
	case ReposStats::srpcTotal:
	case ReposStats::nfsTotal:
	  {
	    ReposStats::TimedCalls *call_stats = NEW(ReposStats::TimedCalls);
	    call_stats->recv(srpc);
	    result.Put(kind, call_stats);
	  }
	  break;
	case ReposStats::memUsage:
	  {
	    ReposStats::MemStats *mem_stats = NEW(ReposStats::MemStats);
	    mem_stats->recv(srpc);
	    result.Put(kind, mem_stats);
	  }
	  break;
	case ReposStats::vMemPool:
	  {
	    ReposStats::VMemPoolStats *vmp_stats = NEW(ReposStats::VMemPoolStats);
	    vmp_stats->recv(srpc);
	    result.Put(kind, vmp_stats);
	  }
	  break;
	default:
	  cerr << ("VDirSurrogate::getReposStats received unknown statistic "
		   "type (")
	       << (unsigned int) kind <<")" << endl;
	  // @@@ Need to signal failure to both client and server.
	  break;
	}
    }
  srpc->recv_seq_end();

  srpc->recv_end();
  VestaSourceSRPC::End(id);
}

void
ReposStats::getServerInfo(/*OUT*/ ServerInfo &result,
			  AccessControl::Identity who,
			  Text reposHost,
			  Text reposPort,
			  bool use_rt, unsigned int rt_secs)
  throw (SRPC::failure)
{
  SRPC* srpc;
  MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, reposHost, reposPort);

  if(use_rt)
    {
      srpc->enable_read_timeout(rt_secs);
    }

  srpc->start_call(VestaSourceSRPC::GetServerInfo,
		   VestaSourceSRPC::version);
  VestaSourceSRPC::send_identity(srpc, who);
  srpc->send_end();

  char *version = srpc->recv_chars();
  result.version = version;
  delete [] version;

  result.start_time = srpc->recv_int64();
  result.uptime = srpc->recv_int32();

  srpc->recv_end();
  VestaSourceSRPC::End(id);
}

