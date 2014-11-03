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

//
// ShortIdClient.C
// Last modified on Mon May 23 21:40:36 EDT 2005 by ken@xorian.net  
//      modified on Thu Jul 20 14:24:29 PDT 2000 by mann  
//      modified on Mon Apr 28 17:30:27 PDT 1997 by heydon
//
// Client code, using Roy's SRPC package
//

#include "ShortIdBlock.H"
#include "ShortIdSRPC.H"
#include "SRPC.H"
#include "VestaConfig.H"
#include <Thread.H>
#include <pthread.h>
#include <assert.h>

using std::cerr;
using std::endl;

// Module globals
static pthread_once_t once = PTHREAD_ONCE_INIT;
static SRPC* srpc;
static Basics::mutex mu;  // protects srpc

extern "C"
{
  static void
  ShortIdClientImport()
  {
    Text port, host;
    try {
	port = VestaConfig::get_Text("Repository", "ShortIdSRPC_port");
	host = VestaConfig::get_Text("Repository", "ShortIdSRPC_host");
	srpc = NEW_CONSTR(SRPC, (SRPC::caller, port, host));
    } catch (VestaConfig::failure f) {
	cerr << f.msg << endl;
	throw;  // meant to be uncaught and fatal
    }
  }
}

ShortIdBlock*
ShortIdBlock::acquire(bool leafflag) throw(SRPC::failure)
{
    pthread_once(&once, ShortIdClientImport);
    ShortIdBlock *bk = NEW(ShortIdBlock);
    int size = sizeof(bk->bits);
    mu.lock();
    srpc->start_call(ShortIdSRPC::AcquireShortIdBlock,
		     ShortIdSRPC::version);
    srpc->send_int((int) leafflag);
    srpc->send_end();
    bk->start = bk->next = (ShortId) srpc->recv_int();
    bk->leaseExpires = (time_t) srpc->recv_int();
    srpc->recv_bytes_here((char *) bk->bits, size);
    assert(size == sizeof(bk->bits));
    srpc->recv_end();
    mu.unlock();
    return bk;
}

bool
ShortIdBlock::renew(ShortIdBlock* bk) throw(SRPC::failure)
{
    pthread_once(&once, ShortIdClientImport);
    mu.lock();
    srpc->start_call(ShortIdSRPC::RenewShortIdBlock,
		     ShortIdSRPC::version);
    srpc->send_int((int) bk->start);
    srpc->send_end();
    bk->leaseExpires = (time_t) srpc->recv_int();
    bool res = (bool) srpc->recv_int();
    srpc->recv_end();
    mu.unlock();
    return res;
}

void
ShortIdBlock::release(ShortIdBlock* bk) throw(SRPC::failure)
{
    pthread_once(&once, ShortIdClientImport);
    mu.lock();
    srpc->start_call(ShortIdSRPC::ReleaseShortIdBlock,
		     ShortIdSRPC::version);
    srpc->send_int((int) bk->start);
    srpc->send_end();
    srpc->recv_end();
    mu.unlock();
}

int
ShortIdBlock::keepDerived(ShortIdsFile ds, time_t dt, bool force)
  throw(SRPC::failure)
{
    pthread_once(&once, ShortIdClientImport);
    mu.lock();
    srpc->start_call(ShortIdSRPC::KeepDerived,
		     ShortIdSRPC::version);
    srpc->send_int((int) ds);
    srpc->send_int((int) dt);
    srpc->send_int((int) force);
    srpc->send_end();
    int res = srpc->recv_int();
    srpc->recv_end();
    mu.unlock();
    return res;
}

void
ShortIdBlock::checkpoint() throw(SRPC::failure)
{
    pthread_once(&once, ShortIdClientImport);
    mu.lock();
    srpc->start_call(ShortIdSRPC::Checkpoint,
		     ShortIdSRPC::version);
    srpc->send_end();
    srpc->recv_end();
    mu.unlock();
}

void
ShortIdBlock::getWeedingState(ShortIdsFile& ds, time_t& dt,
			      ShortIdsFile& ss, time_t& st,
			      bool& sourceWeedInProgress,
			      bool& deletionsInProgress,
			      bool& deletionsDone,
			      bool& checkpointInProgress)
  throw(SRPC::failure)
{
    pthread_once(&once, ShortIdClientImport);
    mu.lock();
    srpc->start_call(ShortIdSRPC::GetWeedingState,
		     ShortIdSRPC::version);
    srpc->send_end();
    ds = (ShortIdsFile) srpc->recv_int();
    dt = (time_t) srpc->recv_int();
    ss = (ShortIdsFile) srpc->recv_int();
    st = (time_t) srpc->recv_int();
    sourceWeedInProgress = (bool) srpc->recv_int();
    deletionsInProgress = (bool) srpc->recv_int();
    deletionsDone = (bool) srpc->recv_int();
    checkpointInProgress = (bool) srpc->recv_int();
    srpc->recv_end();
    mu.unlock();
}

