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

// ExpCache -- Export the Vesta-2 cache server via SRPC

/* An "ExpCache" object exports the Vesta-2 cache server interface via
   SRPC.  This modules uses the "LimService" interface to limit access
   to the server, limiting both the maximum number of concurrent
   threads allowed to run in the server.  This limit is specified in
   the "ExpCache" object constructor.

   The "ExpCache" object also de-multiplexes incoming calls and unmarshals
   arguments for the appropriate server function. The "CacheS" object
   implementing the server is supplied to the "ExpCache" object constructor,
   and that object's methods are invoked in response to server SRPC calls. */

#ifndef _EXP_CACHE_H
#define _EXP_CACHE_H

#include <Basics.H>
#include <SRPC.H>
#include <LimService.H>
#include <CacheIntf.H>

#include "CacheS.H"

class ExpCache {
  public:

    ExpCache(CacheS *cs, int maxRunning,
      const Text& hostName = Text("")) throw ();
    /* Initialize an "ExpCache" object to export an instance of the Vesta-2
       cache server. To run the server, use the "Run" method below. */

    void Run() throw (SRPC::failure);
    /* Export version "cs.version()" of the Vesta-2 cache server, on the host
       named "hostname". If "hostName" is defaulted, then the interface is
       exported from the current host. However, if the local machine has
       multiple IP addresses, "hostName" can be used to differentiate among
       them.

       The exported interface allows at most "maxRunning" threads to
       run concurrently in the server.

       On a successful client call, a thread is forked in the server that
       determines the server procedure to run, unmarshals the arguments,
       and then invokes the correct method of the "cs" object.

       Storage for "cs" should not be deleted until after the storage for
       the "ExpCache" object is deleted. */

  private:
    // read-only after initialization
    CacheIntf::DebugLevel debug;

    // data fields
    CacheS *cs;
    LimService *ls;

    // Perform the server instance check for calls protected by it.
    // Return true if call should proceed as normal (which means the
    // instance matches).
    bool check_server_instance(SRPC *srpc, const FP::Tag &server_instance,
			       const char *func_name)
      const throw(SRPC::failure);

    // callbacks
    void AddEntry(SRPC *srpc, int intf_ver) throw (SRPC::failure);
    void FreeVars(SRPC *srpc, int intf_ver) throw (SRPC::failure);
    void Lookup(SRPC *srpc, int intf_ver) throw (SRPC::failure);
    void Checkpoint(SRPC *srpc) throw (SRPC::failure);
    void RenewLeases(SRPC *srpc) throw (SRPC::failure);
    void WeederRecovering(SRPC *srpc) throw (SRPC::failure);
    void StartMark(SRPC *srpc) throw (SRPC::failure);
    void SetHitFilter(SRPC *srpc) throw (SRPC::failure);
    void GetLeases(SRPC *srpc) throw (SRPC::failure);
    void ResumeLeaseExp(SRPC *srpc) throw (SRPC::failure);
    void EndMark(SRPC *srpc) throw (SRPC::failure);
    void CommitChkpt(SRPC *srpc) throw (SRPC::failure);
    void FlushAll(SRPC *srpc) throw (SRPC::failure);
    void GetCacheId(SRPC *srpc) throw (SRPC::failure);
    void GetCacheState(SRPC *srpc) throw (SRPC::failure);
    void GetCacheInstance(SRPC *srpc) throw (SRPC::failure);


    friend class ExpCache_handler;

    // hide copy constructor from clients
    ExpCache(const ExpCache&);
};

#endif // _EXP_CACHE_H
