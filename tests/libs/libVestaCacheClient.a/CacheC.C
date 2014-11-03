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

// from vesta/fp
#include <FP.H>

// from vesta/cache-common
#include <CacheConfig.H>
#include <CacheIntf.H>
#include <ReadConfig.H>
#include <BitVector.H>
#include <FV.H>
#include <CacheIndex.H>
#include <Model.H>
#include <VestaVal.H>
#include <Debug.H>
#include <FV2.H>
#include <CompactFV.H>
#include <SelfLocking.H>

// local includes
#include "ParCacheC.H"
#include "CacheC.H"

using std::ostream;
using std::endl;
using OS::cio;

// Counter used to make it easier for someone looking at cache client
// debug output to match up calls to returns in a multi-threaded
// program (i.e. the evaluator).
SelfLockingInt<unsigned int> g_debug_call_count(1);

// constructor
CacheC::CacheC(CacheIntf::DebugLevel debug) throw (SRPC::failure)
: debug(debug)
{
    this->serverPort = ReadConfig::TextVal(Config_CacheSection, "Port");
    this->conns = NEW(MultiSRPC);

    // Get the instance identifier for the cache server.
    init_server_instance();
}

// destructor
CacheC::~CacheC() throw ()
{
    if (this->conns != (MultiSRPC *)NULL)
	delete this->conns;
}

void CacheC::init_server_instance() throw(SRPC::failure)
{
  unsigned int l_debug_call_number;

    // Initialize the cache server instance fingerprint by getting it
    // from the cache server.
    MultiSRPC::ConnId cid = -1;
    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);

	// print args
	if (this->debug >= CacheIntf::OtherOps) {
	  ostream& out_stream = cio().start_out();
	  l_debug_call_number = g_debug_call_count++;
	  out_stream << Debug::Timestamp() << "CALLING -- GetCacheInstance"
		     << " (call #" << l_debug_call_number << ")"
		     << endl << endl;
	  cio().end_out();
	}

	// send (no) arguments
	srpc->start_call(CacheIntf::GetCacheInstanceProc, CacheIntf::Version);
	srpc->send_end();

	// receive results
	this->server_instance.Recv(*srpc);
	srpc->recv_end();

	// print result
	if (this->debug >= CacheIntf::OtherOps) {
	  cio().start_out() << Debug::Timestamp() 
			    << "RETURNED -- GetCacheInstance"
			    << " (call #" << l_debug_call_number << ")" << endl
			    << "  instance fingerprint = " << server_instance 
			    << endl << endl;
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

void CacheC::recv_server_instance_check(SRPC *srpc, const char *func_name)
     const
     throw(SRPC::failure)
{
  // Receive server instance check
  bool l_server_instance_match = (bool)(srpc->recv_int());
  if(!l_server_instance_match)
    {
      // There won't be anything else coming back.
      srpc->recv_end();
      
      // Print a message about this failure
      if (this->debug >= CacheIntf::OtherOps) {
	cio().start_out() << Debug::Timestamp()
			  << "RETURNED -- " << func_name << endl
			  << "  server instance mismatch, call aborted" 
			  << endl << endl;
	cio().end_out();
      }

      // According to comments in SRPC.H, positive numbers should be
      // used to indicate protocol-specific problems.
      throw SRPC::failure(1, ("Cache server instance mismatch "
			      "(cache server must have restarted)"));
    }
}

FV::Epoch CacheC::FreeVariables(const FP::Tag& pk,
  /*OUT*/ CompactFV::List& names, /*OUT*/ bool &isEmpty)
  const throw (SRPC::failure)
{
    FV::Epoch res;
    MultiSRPC::ConnId cid = -1;
    unsigned int l_debug_call_number;

    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);

	// print args
	if (this->debug >= CacheIntf::OtherOps) {
	  ostream& out_stream = cio().start_out();
	  l_debug_call_number = g_debug_call_count++;
	  out_stream << Debug::Timestamp() << "CALLING -- FreeVariables"
		     << " (call #" << l_debug_call_number << ")" << endl
		     << "  pk = " << pk << endl << endl;
	  cio().end_out();
	}

	// start call
	srpc->start_call(CacheIntf::FreeVarsProc, CacheIntf::Version);
	// Send the server instance identifier
	server_instance.Send(*srpc);
	// send arguments
	pk.Send(*srpc);
	srpc->send_end();

	// Receive server instance check
	recv_server_instance_check(srpc, "FreeVariables");

	// receive results
	isEmpty = (bool)(srpc->recv_int());
	names.Recv(*srpc);
	res = (FV::Epoch)(srpc->recv_int());
	srpc->recv_end();

	// print result
	if (this->debug >= CacheIntf::OtherOps) {
	  ostream& out_stream = cio().start_out();
	  out_stream << Debug::Timestamp() << "RETURNED -- FreeVariables"
		     << " (call #" << l_debug_call_number << ")" << endl;
	  out_stream << "  epoch = " << res << endl;
	  out_stream << "  isEmpty = " << BoolName[isEmpty] << endl;
	  out_stream << "  names =" << endl; 
	  names.Print(out_stream, /*indent=*/ 4);
	  out_stream << "  names.tbl =";
	  if (names.tbl.NumArcs() > 0) {
	    out_stream << endl; 
	    names.tbl.Print(out_stream, /*indent=*/ 4);
	  } else {
	    out_stream << " <empty>" << endl;
	  }
	  out_stream << endl;
	  cio().end_out();
	}

	// end the connection
	this->conns->End(cid);
    }
    catch (SRPC::failure) {
	this->conns->End(cid);
	throw;
    }
    return res;
}

CacheIntf::LookupRes CacheC::Lookup(
  const FP::Tag& pk, const FV::Epoch id, const FP::List& fps,
  /*OUT*/ CacheEntry::Index& ci, /*OUT*/ VestaVal::T& value)
  const throw (SRPC::failure)
{
    CacheIntf::LookupRes res;
    MultiSRPC::ConnId cid = -1;
    unsigned int l_debug_call_number;

    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);

	// print args
	if (this->debug >= CacheIntf::OtherOps) {
	  ostream& out_stream = cio().start_out();
	  l_debug_call_number = g_debug_call_count++;
	  out_stream << Debug::Timestamp() << "CALLING -- Lookup"
		     << " (call #" << l_debug_call_number << ")" << endl;
	  out_stream << "  pk = " << pk << endl;
	  out_stream << "  epoch = " << id << endl;
	  out_stream << "  fps =" << endl; fps.Print(out_stream, /*indent=*/ 4);
	  out_stream << endl;
	  cio().end_out();
	}

	// start call
	srpc->start_call(CacheIntf::LookupProc, CacheIntf::Version);
	// Send the server instance identifier
	server_instance.Send(*srpc);
	// send arguments
	pk.Send(*srpc);
	srpc->send_int(id);
	fps.Send(*srpc);
	srpc->send_end();

	// Receive server instance check
	recv_server_instance_check(srpc, "Lookup");

	// receive results
	res = (CacheIntf::LookupRes)(srpc->recv_int());
	if (res == CacheIntf::Hit) {
	    ci = srpc->recv_int();
	    value.Recv(*srpc);
	}
	srpc->recv_end();

	// print result
	if (this->debug >= CacheIntf::OtherOps) {
	  ostream& out_stream = cio().start_out();
	  out_stream << Debug::Timestamp() << "RETURNED -- Lookup"
		     << " (call #" << l_debug_call_number << ")" << endl;
	  out_stream << "  result = " << CacheIntf::LookupResName(res) << endl;
	  if (res == CacheIntf::Hit) {
	    out_stream << "  index = " << ci << endl;
	    out_stream << "  value =" << endl; 
	    value.Print(out_stream, /*indent=*/ 4);
	  }
	  out_stream << endl;
	  cio().end_out();
	}

	// end the connection
	this->conns->End(cid);
    }
    catch (SRPC::failure) {
	this->conns->End(cid);
	throw;
    }
    return res;
}

CacheIntf::AddEntryRes CacheC::AddEntry(
  const FP::Tag& pk, const char *types, const FV2::List& names,
  const FP::List& fps, const VestaVal::T& value, const Model::T model,
  const CacheEntry::Indices& kids, const Text& sourceFunc,
  /*OUT*/ CacheEntry::Index& ci) throw (SRPC::failure)
{
    CacheIntf::AddEntryRes res;
    MultiSRPC::ConnId cid = -1;
    unsigned int l_debug_call_number;

    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);

	// print args
	if (this->debug >= CacheIntf::AddEntryOp) {
	  ostream& out_stream = cio().start_out();
	  l_debug_call_number = g_debug_call_count++;
	  out_stream << Debug::Timestamp() << "CALLING -- AddEntry"
		     << " (call #" << l_debug_call_number << ")" << endl;
	  out_stream << "  pk = " << pk << endl;
	  out_stream << "  names =" << endl; 
	  names.Print(out_stream, /*indent=*/ 4, types);
	  out_stream << "  fps =" << endl; fps.Print(out_stream, /*indent=*/ 4);
	  out_stream << "  value =" << endl; 
	  value.Print(out_stream, /*indent=*/ 4);
	  out_stream << "  model = " << model << endl;
	  out_stream << "  kids = " << kids << endl;
          out_stream << "  sourceFunc = " << sourceFunc << endl;
	  out_stream << endl;
	  cio().end_out();
	}

	// start call
	srpc->start_call(CacheIntf::AddEntryProc, CacheIntf::Version);
	// Send the server instance identifier
	server_instance.Send(*srpc);
	// send arguments
	pk.Send(*srpc);
	names.Send(*srpc, types);
	fps.Send(*srpc);
	value.Send(*srpc);
	Model::Send(model, *srpc);
	kids.Send(*srpc);
        srpc->send_Text(sourceFunc);
	srpc->send_end();

	// Receive server instance check
	recv_server_instance_check(srpc, "AddEntry");

	// receive results
	ci = srpc->recv_int();
	res = (CacheIntf::AddEntryRes)(srpc->recv_int());
	srpc->recv_end();

	// print results
	if (this->debug >= CacheIntf::AddEntryOp) {
	  ostream& out_stream = cio().start_out();
	  out_stream << Debug::Timestamp() << "RETURNED -- AddEntry"
		     << " (call #" << l_debug_call_number << ")" << endl;
	  out_stream << "  result = " << CacheIntf::AddEntryResName(res) << endl;
	  if (res == CacheIntf::EntryAdded) {
	    out_stream << "  ci = " << ci << endl;
	  }
	  out_stream << endl;
	  cio().end_out();
	}

	// end the connection
	this->conns->End(cid);
    }
    catch (SRPC::failure) {
	this->conns->End(cid);
	throw;
    }
    return res;
}

void CacheC::Checkpoint (const FP::Tag &pkgVersion, Model::T model,
  const CacheEntry::Indices& cis, bool done) throw (SRPC::failure)
{
    MultiSRPC::ConnId cid = -1;
    unsigned int l_debug_call_number;

    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);

	// print args
	if (this->debug >= CacheIntf::LogFlush) {
	  ostream& out_stream = cio().start_out();
	  l_debug_call_number = g_debug_call_count++;
	  out_stream << Debug::Timestamp() << "CALLING -- Checkpoint"
		     << " (call #" << l_debug_call_number << ")" << endl;
	  out_stream << "  pkgVersion = " << pkgVersion << endl;
	  out_stream << "  model = " << model << endl;
	  out_stream << "  cis = " << cis  << endl;
	  out_stream << "  done = " << BoolName[done] << endl;
	  out_stream << endl;
	  cio().end_out();
	}

	// start call
	srpc->start_call(CacheIntf::CheckpointProc, CacheIntf::Version);
	// Send the server instance identifier
	server_instance.Send(*srpc);
	// send arguments
	pkgVersion.Send(*srpc);
	Model::Send(model, *srpc);
	cis.Send(*srpc);
	srpc->send_int((int)done);
	srpc->send_end();

	// Receive server instance check
	recv_server_instance_check(srpc, "Checkpoint");

	// No results for this call.
	srpc->recv_end();

	// print result
	if (this->debug >= CacheIntf::LogFlush) {
	  cio().start_out() << Debug::Timestamp() << "RETURNED Checkpoint"
			    << " (call #" << l_debug_call_number << ")" 
			    << endl << endl;
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

bool CacheC::RenewLeases(const CacheEntry::Indices& cis)
  throw (SRPC::failure)
{
    bool res;
    MultiSRPC::ConnId cid = -1;
    unsigned int l_debug_call_number;

    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);

	// print args
	if (this->debug >= CacheIntf::LeaseExp) {
	  ostream& out_stream = cio().start_out();
	  l_debug_call_number = g_debug_call_count++;
	  out_stream << Debug::Timestamp() << "CALLING -- RenewLeases"
		     << " (call #" << l_debug_call_number << ")" << endl;
	  out_stream << "  cis = " << cis << endl << endl;
	  cio().end_out();
	}

	// start call
	srpc->start_call(CacheIntf::RenewLeasesProc, CacheIntf::Version);
	// Send the server instance identifier
	server_instance.Send(*srpc);
	// send arguments
	cis.Send(*srpc);
	srpc->send_end();

	// Receive server instance check
	recv_server_instance_check(srpc, "RenewLeases");

	// receive results
	res = (bool)(srpc->recv_int());
	srpc->recv_end();

	// print results
	if (this->debug >= CacheIntf::LeaseExp) {
	  cio().start_out() << Debug::Timestamp() << "RETURNED -- RenewLeases"
			    << " (call #" << l_debug_call_number << ")" << endl
			    << "  res = " << (res ? "true" : "false") 
			    << endl << endl;
	  cio().end_out();
	}

	// end the connection
	this->conns->End(cid);
    }
    catch (SRPC::failure) {
	this->conns->End(cid);
	throw;
    }
    return res;
}
