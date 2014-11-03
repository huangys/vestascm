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


// from vesta/basics
#include <Basics.H>

// from vesta/srpc
#include <SRPC.H>
#include <MultiSRPC.H>

// from vesta/cache-common
#include <CacheConfig.H>
#include <CacheIntf.H>
#include <ReadConfig.H>
#include <CacheState.H>
#include <Debug.H>

// local includes
#include "ParCacheC.H"
#include "DebugC.H"

using std::ostream;
using std::endl;
using OS::cio;

// constructor
DebugC::DebugC(CacheIntf::DebugLevel debug,
	       bool use_rt, unsigned int rt_secs) throw ()
  : debug(debug), use_read_timeout(use_rt), read_timeout_seconds(rt_secs)
{
    this->serverPort = ReadConfig::TextVal(Config_CacheSection, "Port");
    this->conns = NEW(MultiSRPC);
}

// destructor
DebugC::~DebugC() throw ()
{
    if (this->conns != (MultiSRPC *)NULL)
	delete this->conns;
}

void DebugC::NullCall(CacheIntf::ProcIds id, char *name,
  CacheIntf::DebugLevel level) const throw (SRPC::failure)
{
    MultiSRPC::ConnId cid = -1;

    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);
	if(use_read_timeout)
	  {
	    srpc->enable_read_timeout(read_timeout_seconds);
	  }

	// print call
	if (this->debug >= level) {
	  cio().start_out() << Debug::Timestamp() 
			    << "CALLED -- " << name << endl << endl;
	  cio().end_out();
	}
	
	// send arguments
	srpc->start_call(id, CacheIntf::Version);
	srpc->send_end();

	// receive results
	srpc->recv_end();

	// print return
	if (this->debug >= level) {
	    cio().start_out() << Debug::Timestamp() 
			      << "RETURNED -- " << name << endl << endl;
	    cio().end_out();
	}

	// end the connection
	this->conns->End(cid);
    }
    catch (SRPC::failure) {
	this->conns->End(cid);
	throw;
    }
}

void DebugC::FlushAll() const throw (SRPC::failure)
{
    NullCall(CacheIntf::FlushAllProc, "FlushAll", CacheIntf::MPKFileFlush);
}

void DebugC::GetCacheId(/*OUT*/ CacheId &id)
  const throw (SRPC::failure)
{
    MultiSRPC::ConnId cid = -1;

    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);
	if(use_read_timeout)
	  {
	    srpc->enable_read_timeout(read_timeout_seconds);
	  }
	
	// send arguments
	srpc->start_call(CacheIntf::GetCacheIdProc, CacheIntf::Version);
	srpc->send_end();

	// receive results
	id.Recv(*srpc);
	srpc->recv_end();

	// end the connection
	this->conns->End(cid);
    }
    catch (SRPC::failure) {
	this->conns->End(cid);
	throw;
    }
}

void DebugC::GetCacheState(/*OUT*/ CacheState &state)
  const throw (SRPC::failure)
{
    MultiSRPC::ConnId cid = -1;

    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);
	if(use_read_timeout)
	  {
	    srpc->enable_read_timeout(read_timeout_seconds);
	  }
	
	// send arguments
	srpc->start_call(CacheIntf::GetCacheStateProc, CacheIntf::Version);
	srpc->send_end();

	// receive results
	state.Recv(*srpc);
	srpc->recv_end();

	// end the connection
	this->conns->End(cid);
    }
    catch (SRPC::failure) {
	this->conns->End(cid);
	throw;
    }
}
void DebugC::GetCacheInstance(/*OUT*/ FP::Tag &instance_fp)
     const throw (SRPC::failure)
{
    MultiSRPC::ConnId cid = -1;
    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);
	if(use_read_timeout)
	  {
	    srpc->enable_read_timeout(read_timeout_seconds);
	  }

	// send (no) arguments
	srpc->start_call(CacheIntf::GetCacheInstanceProc, CacheIntf::Version);
	srpc->send_end();

	// receive results
	instance_fp.Recv(*srpc);
	srpc->recv_end();

	// end the connection
	this->conns->End(cid);
    }
    catch (SRPC::failure) {
	this->conns->End(cid);
	throw;
    }    
}
