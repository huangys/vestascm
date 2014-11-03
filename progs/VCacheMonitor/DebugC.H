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

// DebugC -- the debugging client interface to the Vesta-2 cache server

/* The class "DebugC" provides a client connection to one or more running
   instances of the Vesta-2 cache server. It provides non-standard debugging
   operations not required by most clients. */

#ifndef _DEBUG_C_H
#define _DEBUG_C_H

#include <Basics.H>

// from vesta/srpc
#include <SRPC.H>
#include <MultiSRPC.H>

// from vesta/fp
#include <FP.H>

// from vesta/cache-src/common
#include <CacheIntf.H>
#include <CacheState.H>

class DebugC {
  public:
    // constructor: initialize the client
    DebugC(CacheIntf::DebugLevel debug = CacheIntf::None,
	   bool use_rt = false, unsigned int rt_secs = 60) throw ();

    // destructor: close and free any open connections to servers
    ~DebugC() throw();

    void FlushAll() const throw (SRPC::failure);
    /* Flush all VMultiPKFiles in the cache, even if they don't have
       any new entries to be flushed. */

    void GetCacheId(/*OUT*/ CacheId &id) const throw (SRPC::failure);
    /* Set "id" to the identifying information of the cache server. */

    void GetCacheState(/*OUT*/ CacheState &state) const throw (SRPC::failure);
    /* Set "state" to the current state information of the cache server. */

    void GetCacheInstance(/*OUT*/ FP::Tag &instance_fp) const
      throw (SRPC::failure);
    /* Set "instance_fp" to the unique instance fingerprint of the
       cache server. */

  protected:
    // The client maintains a cache of connections in a "Connections" object
    MultiSRPC *conns;
    // The port we talk to the server on
    Text serverPort;
    
    // Read-only after initialization
    CacheIntf::DebugLevel debug;

    // Configuraiton for the read timeout feature.
    bool use_read_timeout;
    unsigned int read_timeout_seconds;

  private:
    void NullCall(CacheIntf::ProcIds id, char *name,
      CacheIntf::DebugLevel level) const throw (SRPC::failure);
    /* Make the remote call to procedure "id" with no arguments or return
       value. */

    // hide copy constructor from clients
    DebugC(const DebugC&);
};

#endif // _DEBUG_C_H
