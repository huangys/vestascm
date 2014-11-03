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
#include <Table.H>

// from vesta/srpc
#include <SRPC.H>
#include <MultiSRPC.H>

// from vesta/cache-common
#include <CacheConfig.H>
#include <CacheIntf.H>
#include <BitVector.H>
#include <ReadConfig.H>
#include <Debug.H>

// local includes
#include "ParCacheC.H"
#include "WeederC.H"

using std::ostream;
using std::endl;
using OS::cio;

// constructor
WeederC::WeederC(CacheIntf::DebugLevel debug) throw (SRPC::failure)
: debug(debug)
{
    this->serverPort = ReadConfig::TextVal(Config_CacheSection, "Port");
    this->conns = NEW(MultiSRPC);

    // Get the instance identifier for the cache server.
    init_server_instance();
}

// destructor
WeederC::~WeederC() throw ()
{
    if (this->conns != (MultiSRPC *)NULL)
	delete this->conns;
}

void WeederC::init_server_instance() throw(SRPC::failure)
{
    // Initialize the cache server instance fingerprint by getting it
    // from the cache server.
    MultiSRPC::ConnId cid = -1;
    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);

	// print args
	if (this->debug >= CacheIntf::OtherOps) {
	  cio().start_out() << Debug::Timestamp() 
			    << "CALLING -- GetCacheInstance" << endl << endl;
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
			    << "RETURNED -- GetCacheInstance" << endl 
			    << "  instance fingerprint = " 
			    << server_instance << endl << endl;
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

void WeederC::recv_server_instance_check(SRPC *srpc, const char *func_name)
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

// WeederC::NullCall() used to be used by both
// WeederC::ResumeLeaseExp() and WeederC::CommitChkpt(), but is now
// just used by the former.  Given that it has only one use, it's not
// much of a simplification anymore and could probably be removed.

void WeederC::NullCall(CacheIntf::ProcIds id, char *name,
  CacheIntf::DebugLevel level) const throw (SRPC::failure)
{
    MultiSRPC::ConnId cid = -1;

    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);

	// print call
	if (this->debug >= level) {
	  cio().start_out() << Debug::Timestamp() 
			    << "CALLED -- " << name << endl << endl;
	  cio().end_out();
	}

	// start call
	srpc->start_call(id, CacheIntf::Version);
	// Send the server instance identifier
	server_instance.Send(*srpc);
	// send [no] arguments
	srpc->send_end();

	// Receive server instance check
	recv_server_instance_check(srpc, name);

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

bool WeederC::WeederRecovering(bool doneMarking) const throw (SRPC::failure)
{
    MultiSRPC::ConnId cid = -1;

    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);

	// print args
	if (this->debug >= CacheIntf::WeederOps) {
	    cio().start_out() << Debug::Timestamp() 
			      << "CALLING -- WeederRecovering" << endl
			      << "  doneMarking = " << BoolName[doneMarking] 
			      << endl << endl;
	    cio().end_out();
	}

	// start call
	srpc->start_call(CacheIntf::WeedRecoverProc, CacheIntf::Version);
	// Send the server instance identifier
	server_instance.Send(*srpc);
	// send arguments
	srpc->send_int((int)doneMarking);
	srpc->send_end();

	// Receive server instance check.  Note that we could, and
	// perhaps even should, be acquiring the server instance
	// identifier here rather than in the WeederC constructor.
	// Doing so would complicate things if we ever had more than
	// one call to WeederRecovering per instance of WeederC, but
	// that seems unlikely anyway.
	recv_server_instance_check(srpc, "WeederRecovering");

	// receive results
        bool err = srpc->recv_int();
	srpc->recv_end();

	// print results
	if (this->debug >= CacheIntf::WeederOps) {
	  cio().start_out() << Debug::Timestamp() 
			    << "RETURNED -- WeederRecovering" << endl
			    << "  err = " << BoolName[err] << endl << endl;
	  cio().end_out();
	}

	// end the connection
	this->conns->End(cid);
	return err;
    }
    catch (SRPC::failure) {
	this->conns->End(cid);
	throw;
    }
}

BitVector* WeederC::StartMark(/*OUT*/ int &newLogVer) const
  throw (SRPC::failure)
{
    BitVector* res;
    MultiSRPC::ConnId cid = -1;

    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);

	// print args
	if (this->debug >= CacheIntf::WeederOps) {
	  cio().start_out() << Debug::Timestamp() 
			    << "CALLING -- StartMark" << endl << endl;
	  cio().end_out();
	}

	// start call
	srpc->start_call(CacheIntf::StartMarkProc, CacheIntf::Version);
	// Send the server instance identifier
	server_instance.Send(*srpc);
	// send arguments
	srpc->send_end();

	// Receive server instance check
	recv_server_instance_check(srpc, "StartMark");

	// receive results
	res = NEW_CONSTR(BitVector, (*srpc));
	newLogVer = srpc->recv_int();
	srpc->recv_end();

	// print results
	if (this->debug >= CacheIntf::WeederOps) {
	  ostream& out_stream = cio().start_out();
	  out_stream << Debug::Timestamp() << "RETURNED -- StartMark" << endl;
	  out_stream << "  initCIs = " << endl;
	  res->PrintAll(out_stream, 6);
	  out_stream << "    (" << res->Cardinality() << " total)" << endl;
	  out_stream << "  newLogVer = " << newLogVer << endl;
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

void WeederC::SetHitFilter(const BitVector &cis) const throw (SRPC::failure)
{
    MultiSRPC::ConnId cid = -1;

    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);

	// print args
	if (this->debug >= CacheIntf::WeederOps) {
	  ostream& out_stream = cio().start_out();
	  out_stream << Debug::Timestamp() << "CALLING -- SetHitFilter" << endl;
	  out_stream << "  cis = " << endl;
	  cis.PrintAll(out_stream, 6);
	  out_stream << "    (" << cis.Cardinality() << " total)" << endl;
	  out_stream << endl;
	  cio().end_out();
	}

	// start call
	srpc->start_call(CacheIntf::SetHitFilterProc, CacheIntf::Version);
	// Send the server instance identifier
	server_instance.Send(*srpc);
	// send arguments
	cis.Send(*srpc);
	srpc->send_end();

	// Receive server instance check
	recv_server_instance_check(srpc, "SetHitFilter");

	// receive results
	srpc->recv_end();

	// print results
	if (this->debug >= CacheIntf::WeederOps) {
	  cio().start_out() << Debug::Timestamp() 
			    << "RETURNED -- SetHitFilter" << endl << endl;
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

BitVector* WeederC::GetLeases() const throw (SRPC::failure)
{
    BitVector* res;
    MultiSRPC::ConnId cid = -1;

    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);

	// print args
	if (this->debug >= CacheIntf::WeederOps) {
	  cio().start_out() << Debug::Timestamp() 
			    << "CALLING -- GetLeases" << endl << endl;
	  cio().end_out();
	}

	// start call
	srpc->start_call(CacheIntf::GetLeasesProc, CacheIntf::Version);
	// Send the server instance identifier
	server_instance.Send(*srpc);
	// send arguments
	srpc->send_end();

	// Receive server instance check
	recv_server_instance_check(srpc, "GetLeases");

	// receive results
	res = NEW_CONSTR(BitVector, (*srpc));
	srpc->recv_end();

	// print results
	if (this->debug >= CacheIntf::WeederOps) {
	  ostream& out_stream = cio().start_out();
	  out_stream << Debug::Timestamp() << "RETURNED -- GetLeases" << endl;
	  out_stream << "  cis = " << endl;
	  res->PrintAll(out_stream, 6);
	  out_stream << "    (" << res->Cardinality() << " total)" << endl;
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

void WeederC::ResumeLeaseExp() const throw (SRPC::failure)
{
    NullCall(CacheIntf::ResumeLeasesProc, "ResumeLeaseExp",
      CacheIntf::WeederOps);
}

static void SendPKPrefixes(SRPC &srpc, const PKPrefixTbl &pfxs)
  throw (SRPC::failure)
/* Marshal the PK prefixes in the domain of "pfxs" to "srpc" so they can
   be reconstituted as a "PKPrefix::List" by the server. */
{
    // send the number of prefixes
    srpc.send_seq_start(pfxs.Size());

    // send the prefixes themselves (in no particular order)
    PKPrefix::T pfx; char dummy;
    PKPrefixIter it(&pfxs);
    while (it.Next(/*OUT*/ pfx, /*OUT*/ dummy)) {
	pfx.Send(srpc);
    }
    srpc.send_seq_end();
}

int WeederC::EndMark(const BitVector &cis, const PKPrefixTbl &pfxs) const
  throw (SRPC::failure)
{
    int chkptVer;
    MultiSRPC::ConnId cid = -1;

    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);

	// print args
	if (this->debug >= CacheIntf::WeederOps) {
	  ostream& out = cio().start_out();
	  out << Debug::Timestamp() << "CALLING -- EndMark" << endl;
	  out << "  cis = " << endl;
	  cis.PrintAll(out, 6);
	  out << "    (" << cis.Cardinality() << " total)" << endl;
	  out << "  prefixes = " << pfxs.Size() << " total" << endl;
	  out << endl;
	  cio().end_out();
	}

	// start call
	srpc->start_call(CacheIntf::EndMarkProc, CacheIntf::Version);
	// Send the server instance identifier
	server_instance.Send(*srpc);
	// send arguments
	cis.Send(*srpc);
	SendPKPrefixes(*srpc, pfxs);
	srpc->send_end();

	// Receive server instance check
	recv_server_instance_check(srpc, "EndMark");

	// receive results
	chkptVer = srpc->recv_int();
	srpc->recv_end();

	// print results
	if (this->debug >= CacheIntf::WeederOps) {
	  cio().start_out() << Debug::Timestamp() 
			    << "RETURNED -- EndMark" << endl
			    << "  chkptVer = " << chkptVer << endl << endl;
	  cio().end_out();
	}

	// end the connection
	this->conns->End(cid);
    }
    catch (SRPC::failure) {
	this->conns->End(cid);
	throw;
    }
    return chkptVer;
}

bool WeederC::CommitChkpt(const Text &chkptFileName)
     const throw (SRPC::failure)
{
    MultiSRPC::ConnId cid = -1;

    try {
	// start the connection
	SRPC *srpc;
	cid = this->conns->Start(*ParCacheC::Locate(), this->serverPort, srpc);

	// print call
	if (this->debug >= CacheIntf::WeederOps) {
	  cio().start_out() << Debug::Timestamp() 
			    << "CALLED -- CommitChkpt" << endl
			    << "  chkptFileName = " << chkptFileName 
			    << endl << endl;
	  cio().end_out();
	}

	// start call
	srpc->start_call(CacheIntf::CommitChkptProc, CacheIntf::Version);
	// Send the server instance identifier
	server_instance.Send(*srpc);
	// send arguments
        srpc->send_Text(chkptFileName);
	srpc->send_end();

	// Receive server instance check
	recv_server_instance_check(srpc, "CommitChkpt");

	// receive results
	bool l_result = srpc->recv_int();
	srpc->recv_end();

	// print return
	if (this->debug >= CacheIntf::WeederOps) {
	  cio().start_out() << Debug::Timestamp() 
			    << "RETURNED -- CommitChkpt" << endl 
			    << "  result: checkpoint " 
			    << (l_result?"accepted":"rejected") << endl << endl;
	  cio().end_out();
	}

	// end the connection
	this->conns->End(cid);
	return l_result;
    }
    catch (SRPC::failure) {
	this->conns->End(cid);
	throw;
    }
}
