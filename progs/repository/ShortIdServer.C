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
// ShortIdServer.C
//
// Server stubs for remote methods in ShortIdBlock.H
// 

#if __linux__
#include <stdint.h>
#endif
#include "ShortIdImpl.H"
#include "SRPC.H"
#include "ShortIdSRPC.H"
#include "VestaConfig.H"
#include "VRWeed.H"
#include "logging.H"
#include <time.h>
#include <assert.h>
#include <Thread.H>
#include <LimService.H>

#define DEF_MAX_RUNNING 32
#define STACK_SIZE 512000L

// By default we give a client a maximum of five minutes to complete a
// call they make.  Of course normally it should be much less than
// this.  The limit is there just to prevent a suspended or
// misbehaving client from causing a server thread to be waiting
// indefinitely.
static unsigned int readTimeout = 300;

class ShortIdReceptionist : public LimService::Handler
{
public:
  ShortIdReceptionist() { }

  void call(SRPC *srpc, int intf_ver, int proc_id) throw(SRPC::failure);
  void call_failure(SRPC *srpc, const SRPC::failure &f) throw();
  void accept_failure(const SRPC::failure &f) throw();
  void other_failure(const char *msg) throw();
  void listener_terminated() throw();
};

// Function to accept calls.
//
void
ShortIdReceptionist::call(SRPC *srpc, int intf_ver, int proc_id)
  throw(SRPC::failure)
{
    // Needed? 
    signal(SIGPIPE, SIG_IGN);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGILL, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    // end Needed?
    
    // Don't allow us to block forever waiting for a client.
    srpc->enable_read_timeout(readTimeout);

    switch (proc_id) {
      case ShortIdSRPC::AcquireShortIdBlock:
	{
	    ShortIdBlock bk;
	    AcquireShortIdBlock(bk, (bool) srpc->recv_int());
	    srpc->recv_end();
	    srpc->send_int((int) bk.start);
	    srpc->send_int((int) bk.leaseExpires);
	    srpc->send_bytes((const char *) bk.bits, sizeof(bk.bits));
	    srpc->send_end();
	}
	break;
      case ShortIdSRPC::RenewShortIdBlock:
	{
	    ShortIdBlock bk;
	    bk.start = (ShortId) srpc->recv_int(); //other fields trash
	    srpc->recv_end();
	    bool res = RenewShortIdBlock(bk);
	    srpc->send_int((int) bk.leaseExpires);
	    srpc->send_int((int) res);
	    srpc->send_end();
	}
	break;
      case ShortIdSRPC::ReleaseShortIdBlock:
	{
	    ShortIdBlock bk;
	    bk.start = (ShortId) srpc->recv_int(); //other fields trash
	    srpc->recv_end();
	    ReleaseShortIdBlock(bk);
	    srpc->send_end();
	}
	break;
      case ShortIdSRPC::OldKeepDerived:
      case ShortIdSRPC::KeepDerived:
	{
	    ShortIdsFile sf = (ShortIdsFile) srpc->recv_int();
	    time_t time;
	    time = (time_t) srpc->recv_int();
	    bool force = false;
	    if (proc_id != ShortIdSRPC::OldKeepDerived) {
              (bool) srpc->recv_int();
	    }
	    srpc->recv_end();
	    int res = KeepDerived(sf, time, force);
	    srpc->send_int(res);
	    srpc->send_end();
	}
	break;
      case ShortIdSRPC::Checkpoint:
	srpc->recv_end();
	Checkpoint();
	srpc->send_end();
	break;
      case ShortIdSRPC::GetWeedingState:
	{
	    srpc->recv_end();
	    ShortIdsFile ds, ss;
	    time_t dt, st;
	    bool swip, dip, ddun, cip;
	    GetWeedingState(ds, dt, ss, st, swip, dip, ddun, cip);
	    srpc->send_int((int) ds);
	    srpc->send_int((int) dt);
	    srpc->send_int((int) ss);
	    srpc->send_int((int) st);
	    srpc->send_int((int) swip);
	    srpc->send_int((int) dip);
	    srpc->send_int((int) ddun);
	    srpc->send_int((int) cip);
	    srpc->send_end();
	}
	break;
      default:
	Repos::dprintf(DBG_ALWAYS, "ShortIdReceptionist got unknown proc_id %d; "
		       "client %s\n", proc_id, srpc->remote_socket().cchars());
	srpc->send_failure(SRPC::version_skew,
			   "ShortIdSRPC: Unknown proc_id");
	break;
    }
}

void
ShortIdReceptionist::call_failure(SRPC* srpc, const SRPC::failure &f)
  throw()
{
    if (f.r == SRPC::partner_went_away && !Repos::isDebugLevel(DBG_SRPC)) return;
    const char* client;
    try {
	client = srpc->remote_socket().cchars();
    } catch (SRPC::failure f) {
	client = "unknown";
    }
    Repos::dprintf(DBG_ALWAYS, "ShortIdReceptionist got SRPC::failure: %s (%d); "
		   "client %s\n", f.msg.cchars(), f.r, client);
}

void ShortIdReceptionist::accept_failure(const SRPC::failure &f) throw()
{
  Repos::dprintf(DBG_ALWAYS,
		 "ShortIdReceptionist got SRPC::failure "
		 "accepting new connection: %s (%d)\n",
		 f.msg.cchars(), f.r);
}

void ShortIdReceptionist::other_failure(const char *msg) throw()
{
  Repos::dprintf(DBG_ALWAYS,
		 "ShortIdReceptionist: %s\n", msg);
}

void ShortIdReceptionist::listener_terminated() throw()
{
  Repos::dprintf(DBG_ALWAYS,
		 "ShortIdReceptionist: Fatal error: unable to accept new "
		 "connections; exiting\n");
  abort();
}

void
ShortIdServerExport(char *serverName)
{
    static Text port;
    int max_running=DEF_MAX_RUNNING;
    Text unused;
    try {
	port = VestaConfig::get_Text("Repository", "ShortIdSRPC_port");
	if(VestaConfig::get("Repository", "ShortIdSRPC_max_running", unused))
	    max_running = VestaConfig::get_int("Repository",
					       "ShortIdSRPC_max_running");
	if(VestaConfig::get("Repository", "ShortIdSRPC_read_timeout", unused))
	    readTimeout = VestaConfig::get_int("Repository",
					       "ShortIdSRPC_read_timeout");
    } catch (VestaConfig::failure f) {
        Repos::dprintf(DBG_ALWAYS,
		       "VestaConfig::failure in ShortIdServerExport: %s\n",
		       f.msg.cchars());
	abort();
    }
    static ShortIdReceptionist handler;
    static LimService receptionist(port, ShortIdSRPC::version, max_running,
				   handler, STACK_SIZE);
    receptionist.Forked_Run();
}
