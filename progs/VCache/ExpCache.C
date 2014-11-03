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

// vesta/basics
#include <Basics.H>
#include <BufStream.H>

// vesta/srpc
#include <SRPC.H>
#include <LimService.H>

// vesta/fp
#include <FP.H>

// vesta/cache-common
#include <PKPrefix.H>
#include <BitVector.H>
#include <CacheConfig.H>
#include <CacheIntf.H>
#include <Debug.H>
#include <CacheState.H>
#include <Model.H>
#include <CacheIndex.H>
#include <FV.H>
#include <VestaVal.H>

// local includes
#include "CacheS.H"
#include "ExpCache.H"

using Basics::OBufStream;
using std::cerr;
using std::ostream;
using std::endl;
using OS::cio;

class ExpCache_handler : public LimService::Handler
{
private:
  CacheS *cs;
  ExpCache *csExp;
public:
  ExpCache_handler(ExpCache *csExp, CacheS *cs) : csExp(csExp), cs(cs) { }

  void call(SRPC *srpc, int intf_ver, int procId) throw(SRPC::failure);
  void call_failure(SRPC *srpc, const SRPC::failure &f) throw();
  void accept_failure(const SRPC::failure &f) throw();
  void other_failure(const char *msg) throw();
  void listener_terminated() throw();
};

void ExpCache_handler::call(SRPC *srpc, int intf_ver, int procId) throw (SRPC::failure)
/* This is the callback procedure passed to the LimService constructor. */
{
    switch (procId) {
      // evaluator client procs
      case CacheIntf::AddEntryProc:    	 csExp->AddEntry(srpc, intf_ver); break;
      case CacheIntf::FreeVarsProc:    	 csExp->FreeVars(srpc, intf_ver); break;
      case CacheIntf::LookupProc:      	 csExp->Lookup(srpc, intf_ver); break;
      case CacheIntf::CheckpointProc:  	 csExp->Checkpoint(srpc);       break;
      case CacheIntf::RenewLeasesProc: 	 csExp->RenewLeases(srpc);      break;
      case CacheIntf::GetCacheInstanceProc:
					 csExp->GetCacheInstance(srpc); break;

      // weeder client procs
      case CacheIntf::WeedRecoverProc: 	 csExp->WeederRecovering(srpc); break;
      case CacheIntf::StartMarkProc:   	 csExp->StartMark(srpc);        break;
      case CacheIntf::SetHitFilterProc:	 csExp->SetHitFilter(srpc);     break;
      case CacheIntf::GetLeasesProc:   	 csExp->GetLeases(srpc);        break;
      case CacheIntf::ResumeLeasesProc:	 csExp->ResumeLeaseExp(srpc);   break;
      case CacheIntf::EndMarkProc:     	 csExp->EndMark(srpc);          break;
      case CacheIntf::CommitChkptProc: 	 csExp->CommitChkpt(srpc);      break;

      // debugging client procs
      case CacheIntf::FlushAllProc:      csExp->FlushAll(srpc);         break;
      case CacheIntf::GetCacheIdProc:    csExp->GetCacheId(srpc);       break;
      case CacheIntf::GetCacheStateProc: csExp->GetCacheState(srpc);    break;

      // programmer error
      default:
	cerr << "Fatal ExpCache error: ";
	cerr << "Unknown method code " << procId << endl;
	assert(false);
    }
}

void ExpCache_handler::call_failure(SRPC *srpc, const SRPC::failure &f)
  throw ()
{
    if (f.r != SRPC::partner_went_away ||
        cs->DebugLevel() >= CacheIntf::StatusMsgs) {
      Text client;
      try {
	client = srpc->remote_socket();
      } catch (SRPC::failure f) {
	client = "unknown";
      }
      ostream& out_stream = cio().start_out();
      out_stream << Debug::Timestamp();
      if (f.r == SRPC::partner_went_away) {
	// remote end disconnected
	out_stream << f.msg << "; client " << client
		   << " (continuing...)" << endl;
      } else {
	out_stream << "Cache Server Failure:" << endl;
	out_stream << "  SRPC failure (code " << f.r << ')' << endl;
	out_stream << "  " << f.msg << endl;
	out_stream << "  client " << client << endl;
	out_stream << ((f.r == SRPC::transport_failure) ?"Exiting":"Continuing");
      }
      out_stream << endl;
      cio().end_out();
    }
}

void ExpCache_handler::accept_failure(const SRPC::failure &f)
  throw ()
{
  cio().start_out()
    << Debug::Timestamp()
    << "Cache Server Failure accepting new connextion:" << endl
    << "  SRPC failure (code " << f.r << ')' << endl
    << "  " << f.msg << endl;
  cio().end_out();
}

void ExpCache_handler::other_failure(const char *msg)
  throw ()
{
  cio().start_out()
    << Debug::Timestamp()
    << "Cache Server Failure: " << msg << " (continuing...)" << endl;
  cio().end_out();
}

void ExpCache_handler::listener_terminated() throw()
{
  cio().start_out()
    << Debug::Timestamp()
    << ("Cache Server Failure: Fatal error: unable to accept new connections; "
	"exiting") << endl;
  cio().end_out();
  abort();
}

ExpCache::ExpCache(CacheS *cs, int maxRunning, const Text& hostName)
  throw ()
: debug(cs->DebugLevel())
{
  // initialize call handler
  ExpCache_handler *handler = NEW_CONSTR(ExpCache_handler, (this, cs));

  // initialize ExpCache
  this->cs = cs;
  this->ls = NEW_CONSTR(LimService,
			(Config_Port,
			 maxRunning, *handler,
			 /*stacksize=*/ -1, hostName));
}

void ExpCache::Run() throw (SRPC::failure)
{
    this->ls->Run();
}

bool ExpCache::check_server_instance(SRPC *srpc,
				     const FP::Tag &server_instance,
				     const char *func_name)
     const
     throw(SRPC::failure)
{
  // If we are not the cache server they are expecting...
  if(server_instance != cs->GetCacheInstance())
    {
      // Indicate to the client that we aren't and end the call.
      srpc->send_int(0);
      srpc->send_end();

      // Log this for debugging
      if (this->debug >= CacheIntf::OtherOps) {
	cio().start_out() << Debug::Timestamp() 
			  << "CALLED -- " << func_name << endl
			  << "  server instance mismatch, call aborted" << endl
			  << "  client = " << server_instance << endl
			  << "  server = " << cs->GetCacheInstance() << endl << endl;
	cio().end_out();
      }

      // Tell the caller that they should just return, as we've
      // already terminated the call.
      return false;
    }

  // Tell the caller to proceed as normal.
  return true;
}

void ExpCache::FreeVars(SRPC *srpc, int intf_ver) throw (SRPC::failure)
{
    // Get the server instance fingerprint.
    FP::Tag server_instance(*srpc);

    // get arguments
    FP::Tag pk(*srpc);
    srpc->recv_end();

    // If we have a server instance mismatch...
    if(!check_server_instance(srpc, server_instance, "FreeVariables"))
      {
	// Don't do anything else.
	return;
      }

    // pre debugging
    if (this->debug >= CacheIntf::OtherOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "CALLED -- FreeVariables" << endl
			<< "  pk = " << pk << endl << endl;
      cio().end_out();
    }

    // call server function
    VPKFile *vf;
    cs->FreeVariables(pk, /*OUT*/ vf);

    // Indicate that this is the correct server instance
    srpc->send_int(1);

    try
      {
	// send results
	vf->SendAllNames(srpc,
			 (this->debug >= CacheIntf::OtherOps),
			 (intf_ver < CacheIntf::LargeFVsVersion));
	srpc->send_end();
      }
    // VPKFile::SendAllNames converts the FreeVariables to a CompactFV
    // which uses a PrefixTbl.  This can throw PrefixTbl::Overflow
    // indicating that we've exceeded the internal limits of the
    // PrefixTbl class (by trying to add the 2^16th entry).  If that
    // happens we convert it to an SRPC failure.
    catch (PrefixTbl::Overflow)
      {
	OBufStream l_msg;
	l_msg << "PrefixTbl overflow in sending result of FreeVariables"
	      << endl
	      << "  pk = " << pk << endl
	      << "(This means you've exceeded internal limits of the" << endl
	      << "cache server, and you may have to erase your cache.)";
	srpc->send_failure(SRPC::internal_trouble, Text(l_msg.str()));
      }
}

void ExpCache::Lookup(SRPC *srpc, int intf_ver) throw (SRPC::failure)
{
    // Get the server instance fingerprint.
    FP::Tag server_instance(*srpc);

    // get arguments
    FP::Tag pk(*srpc);
    FV::Epoch id = srpc->recv_int();
    FP::List fps(*srpc);
    srpc->recv_end();

    // If we have a server instance mismatch...
    if(!check_server_instance(srpc, server_instance, "Lookup"))
      {
	// Don't do anything else.
	return;
      }

    // pre debugging
    if (this->debug >= CacheIntf::OtherOps) {
      ostream& out_stream = cio().start_out();
      out_stream << Debug::Timestamp() << "CALLED -- Lookup" << endl
		 << "  pk = " << pk << endl
		 << "  epoch = " << id << endl
		 << "  fps =" << endl; 
      fps.Print(out_stream, /*indent=*/ 4);
      out_stream << endl;
      cio().end_out();
    }

    // call server function
    CacheEntry::Index ci = 0;
    const VestaVal::T *value = (VestaVal::T *)NULL;
    CacheIntf::LookupRes res =
      cs->Lookup(pk, id, fps, /*OUT*/ ci, /*OUT*/ value);

    // post debugging
    if (this->debug >= CacheIntf::OtherOps) {
      ostream& out = cio().start_out();
      out << Debug::Timestamp() << "RETURNED -- Lookup" << endl;
      out << "  result = " << CacheIntf::LookupResName(res) << endl;
      if (res == CacheIntf::Hit) {
	out << "  index = " << ci << endl;
	out << "  value =" << endl; value->Print(out, /*indent=*/ 4);
      }
      out << endl;
      cio().end_out();	
    }

    // Indicate that this is the correct server instance
    srpc->send_int(1);

    // send results
    srpc->send_int((int)res);
    if (res == CacheIntf::Hit) {
	srpc->send_int(ci);
	value->Send(*srpc,
		    (intf_ver < CacheIntf::LargeFVsVersion));
    }
    srpc->send_end();
}

void ExpCache::AddEntry(SRPC *srpc, int intf_ver) throw (SRPC::failure)
{
    // Get the server instance fingerprint.
    FP::Tag server_instance(*srpc);

    // get arguments
    FP::Tag* pk = NEW_PTRFREE_CONSTR(FP::Tag, (*srpc));
    FV::List *names = NEW_CONSTR(FV::List, (*srpc));
    FP::List *fps = NEW_CONSTR(FP::List, (*srpc));
    VestaVal::T *value =
      NEW_CONSTR(VestaVal::T, (*srpc,
			       (intf_ver < CacheIntf::LargeFVsVersion)));
    Model::T model = Model::Recv(*srpc);
    CacheEntry::Indices *kids = NEW_CONSTR(CacheEntry::Indices, (*srpc));
    Text sourceFunc;
    srpc->recv_Text(/*OUT*/ sourceFunc);
    srpc->recv_end();

    // If we have a server instance mismatch...
    if(!check_server_instance(srpc, server_instance, "AddEntry"))
      {
	// Don't do anything else.
	return;
      }

    // pre debugging
    if (this->debug >= CacheIntf::AddEntryOp) {
      ostream& out_stream = cio().start_out();
      out_stream << Debug::Timestamp() << "CALLED -- AddEntry" << endl;
      out_stream << "  pk = " << *pk << endl;
      out_stream << "  names =" << endl; names->Print(out_stream, /*indent=*/ 4);
      out_stream << "  fps =" << endl; fps->Print(out_stream, /*indent=*/ 4);
      out_stream << "  value =" << endl; value->Print(out_stream, /*indent=*/ 4);
      out_stream << "  model = " << model << endl;
      out_stream << "  kids = " << *kids << endl;
      out_stream << "  sourceFunc = " << sourceFunc << endl;
      out_stream << endl;
      cio().end_out();
    }

    // call server function
    CacheEntry::Index ci;
    CacheIntf::AddEntryRes res;
    try
      {
	res = cs->AddEntry(pk, names, fps,
			   value, model, kids, sourceFunc, /*OUT*/ ci);
      }
    catch(VPKFile::TooManyNames e)
      {
	OBufStream l_msg;
	l_msg << Text("The maximum number of secondary dependencies has "
		      "been reached for:").WordWrap() << endl << endl
	      << "  function = " << sourceFunc << endl
	      << "  pk       = " << *pk << endl << endl
	      << Text("(This means you've exceeded internal limits of the"
		      "cache server, and you will have to modify your build "
		      "instructions so that this function has fewer "
		      "secondary dependencies.  This is a good idea anyway, "
		      "as having a large number of secondary dependencies "
		      "reduces build performance.)").WordWrap() << endl << endl
	      << Text("Here are the new secondary dependencies which "
		      "reached the limit:").WordWrap() << endl << endl;
	e.newNames->Print(l_msg, /*indent=*/ 2);

	srpc->send_failure(SRPC::internal_trouble, Text(l_msg.str()));
      }

    // post debugging
    if (this->debug >= CacheIntf::AddEntryOp) {
      ostream& out_stream = cio().start_out();
      out_stream << Debug::Timestamp() << "RETURNED -- AddEntry" << endl;
      out_stream << "  result = " << CacheIntf::AddEntryResName(res) << endl;
      if (res == CacheIntf::EntryAdded) {
	out_stream << "  ci = " << ci << endl;
      }
      out_stream << endl;
      cio().end_out();
    }

    // Indicate that this is the correct server instance
    srpc->send_int(1);

    // send results
    srpc->send_int(ci);
    srpc->send_int((int)res);
    srpc->send_end();
}

void ExpCache::Checkpoint(SRPC *srpc) throw (SRPC::failure)
{
    // Get the server instance fingerprint.
    FP::Tag server_instance(*srpc);

    // get arguments
    FP::Tag pkgVersion(*srpc);
    Model::T model = Model::Recv(*srpc);
    CacheEntry::Indices *cis = NEW_CONSTR(CacheEntry::Indices,(*srpc));
    bool done = (bool)srpc->recv_int();
    srpc->recv_end();

    // If we have a server instance mismatch...
    if(!check_server_instance(srpc, server_instance, "Checkpoint"))
      {
	// Don't do anything else.
	return;
      }

    // pre debugging
    if (this->debug >= CacheIntf::LogFlush) {
      ostream& out_stream = cio().start_out();
      out_stream << Debug::Timestamp() << "CALLED -- Checkpoint" << endl;
      out_stream << "  pkgVersion = " << pkgVersion << endl;
      out_stream << "  model = " << model << endl;
      out_stream << "  cis   = "; cis->Print(out_stream, /*indent=*/ 4);
      out_stream << "  done  = " << BoolName[done] << endl;
      out_stream << endl;
      cio().end_out();
    }

    // call server function
    cs->Checkpoint(pkgVersion, model, cis, done);

    // post debugging
    if (this->debug >= CacheIntf::LogFlush) {
      cio().start_out() << Debug::Timestamp() 
			<< "RETURNED -- Checkpoint" << endl << endl;
      cio().end_out();
    }

    // Indicate that this is the correct server instance
    srpc->send_int(1);

    // send results
    srpc->send_end();
}

void ExpCache::RenewLeases(SRPC *srpc) throw (SRPC::failure)
{
    // Get the server instance fingerprint.
    FP::Tag server_instance(*srpc);

    // get arguments
    CacheEntry::Indices *cis = NEW_CONSTR(CacheEntry::Indices, (*srpc));
    srpc->recv_end();

    // If we have a server instance mismatch...
    if(!check_server_instance(srpc, server_instance, "RenewLeases"))
      {
	// Don't do anything else.
	return;
      }

    // pre debugging
    if (this->debug >= CacheIntf::LeaseExp) {
      cio().start_out() << Debug::Timestamp() 
			<< "CALLED -- RenewLeases" << endl 
			<< "  cis = " << *cis << endl << endl;
      cio().end_out();
    }

    // call server function
    bool res = cs->RenewLeases(cis);

    // post debugging
    if (this->debug >= CacheIntf::LeaseExp) {
      cio().start_out() << Debug::Timestamp() 
			<< "RETURNED -- RenewLeases" << endl
			<< "  result = " << BoolName[res] << endl << endl;
      cio().end_out();
    }

    // Indicate that this is the correct server instance
    srpc->send_int(1);

    // send results
    srpc->send_int((int)res);
    srpc->send_end();
}

void ExpCache::WeederRecovering(SRPC *srpc) throw (SRPC::failure)
{
    // Get the server instance fingerprint.
    FP::Tag server_instance(*srpc);

    // get arguments
    bool doneMarking = srpc->recv_int();
    srpc->recv_end();

    // If we have a server instance mismatch...
    if(!check_server_instance(srpc, server_instance, "WeederRecovering"))
      {
	// Don't do anything else.
	return;
      }

    // pre debugging
    if (this->debug >= CacheIntf::WeederOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "CALLED -- WeederRecovering" << endl
			<< "  doneMarking = " << BoolName[doneMarking] << endl << endl;
      cio().end_out();
    }

    // call server function
    bool err = cs->WeederRecovering(srpc, doneMarking);

    // post debugging
    if (this->debug >= CacheIntf::WeederOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "RETURNED -- WeederRecovering" << endl
			<< "  err = " << BoolName[err] << endl << endl;
      cio().end_out();
    }

    // Indicate that this is the correct server instance
    srpc->send_int(1);

    // send results
    srpc->send_int((int)err);
    srpc->send_end();
}

void ExpCache::StartMark(SRPC *srpc) throw (SRPC::failure)
{
    // Get the server instance fingerprint.
    FP::Tag server_instance(*srpc);

    // get arguments
    srpc->recv_end();

    // If we have a server instance mismatch...
    if(!check_server_instance(srpc, server_instance, "StartMark"))
      {
	// Don't do anything else.
	return;
      }

    // pre debugging
    if (this->debug >= CacheIntf::WeederOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "CALLED -- StartMark" << endl << endl;
      cio().end_out();
    }

    // call server function
    int newLogVer;
    BitVector *res = cs->StartMark(/*OUT*/ newLogVer);

    // post debugging
    if (this->debug >= CacheIntf::WeederOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "RETURNED -- StartMark" << endl
			<< "  initCIs = " << *res << endl
			<< "  newLogVer = " << newLogVer << endl << endl;
      cio().end_out();
    }

    // Indicate that this is the correct server instance
    srpc->send_int(1);

    // send results
    res->Send(*srpc);
    srpc->send_int(newLogVer);
    srpc->send_end();
}

void ExpCache::SetHitFilter(SRPC *srpc) throw (SRPC::failure)
{
    // Get the server instance fingerprint.
    FP::Tag server_instance(*srpc);

    // get arguments
    BitVector cis(*srpc);
    srpc->recv_end();

    // If we have a server instance mismatch...
    if(!check_server_instance(srpc, server_instance, "SetHitFilter"))
      {
	// Don't do anything else.
	return;
      }

    // pre debugging
    if (this->debug >= CacheIntf::WeederOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "CALLED -- SetHitFilter" << endl
			<< "  cis = " << cis << endl << endl;
      cio().end_out();
    }

    // call server function
    cs->SetHitFilter(cis);

    // post debugging
    if (this->debug >= CacheIntf::WeederOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "RETURNED -- SetHitFilter" << endl << endl;
      cio().end_out();
    }

    // Indicate that this is the correct server instance
    srpc->send_int(1);

    // send results
    srpc->send_end();
}

void ExpCache::GetLeases(SRPC *srpc) throw (SRPC::failure)
{
    // Get the server instance fingerprint.
    FP::Tag server_instance(*srpc);

    // get arguments
    srpc->recv_end();

    // If we have a server instance mismatch...
    if(!check_server_instance(srpc, server_instance, "GetLeases"))
      {
	// Don't do anything else.
	return;
      }

    // pre debugging
    if (this->debug >= CacheIntf::WeederOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "CALLED -- GetLeases" << endl << endl;
      cio().end_out();
    }

    // call server function
    BitVector *res = cs->GetLeases();

    // post debugging
    if (this->debug >= CacheIntf::WeederOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "RETURNED -- GetLeases" << endl
			<< "  cis = " << *res << endl << endl;
      cio().end_out();
    }

    // Indicate that this is the correct server instance
    srpc->send_int(1);

    // send results
    res->Send(*srpc);
    srpc->send_end();
}

void ExpCache::ResumeLeaseExp(SRPC *srpc) throw (SRPC::failure)
{
    // Get the server instance fingerprint.
    FP::Tag server_instance(*srpc);

    // get arguments
    srpc->recv_end();

    // If we have a server instance mismatch...
    if(!check_server_instance(srpc, server_instance, "ResumeLeaseExp"))
      {
	// Don't do anything else.
	return;
      }

    // pre debugging
    if (this->debug >= CacheIntf::WeederOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "CALLED -- ResumeLeaseExp" << endl << endl;
      cio().end_out();
    }

    // call server function
    cs->ResumeLeaseExp();

    // post debugging
    if (this->debug >= CacheIntf::WeederOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "RETURNED -- ResumeLeaseExp" << endl << endl;
      cio().end_out();
    }

    // Indicate that this is the correct server instance
    srpc->send_int(1);

    // send results
    srpc->send_end();
}

void ExpCache::EndMark(SRPC *srpc) throw (SRPC::failure)
{
    // Get the server instance fingerprint.
    FP::Tag server_instance(*srpc);

    // get arguments
    BitVector cis(*srpc);
    PKPrefix::List pfxs(*srpc);
    srpc->recv_end();

    // If we have a server instance mismatch...
    if(!check_server_instance(srpc, server_instance, "EndMark"))
      {
	// Don't do anything else.
	return;
      }

    // pre debugging
    if (this->debug >= CacheIntf::WeederOps) {
      ostream& out_stream = cio().start_out();
      out_stream << Debug::Timestamp() 
		 << "CALLED -- EndMark" << endl
		 << "  cis = " << cis << endl
		 << "  prefixes ="; 
      pfxs.Print(out_stream, /*indent=*/ 4);
      out_stream << endl;
      cio().end_out();
    }

    // call server function
    int chkptVer = cs->EndMark(cis, pfxs);

    // post debugging
    if (this->debug >= CacheIntf::WeederOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "RETURNED -- EndMark" << endl
			<< "  chkptVer = " << chkptVer << endl << endl;
      cio().end_out();
    }

    // Indicate that this is the correct server instance
    srpc->send_int(1);

    // send results
    srpc->send_int(chkptVer);
    srpc->send_end();
}

void ExpCache::CommitChkpt(SRPC *srpc) throw (SRPC::failure)
{
    // Get the server instance fingerprint.
    FP::Tag server_instance(*srpc);

    // get arguments
    Text chkptFileName;
    srpc->recv_Text(/*OUT*/ chkptFileName);
    srpc->recv_end();

    // If we have a server instance mismatch...
    if(!check_server_instance(srpc, server_instance, "CommitChkpt"))
      {
	// Don't do anything else.
	return;
      }

    // pre debugging
    if (this->debug >= CacheIntf::WeederOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "CALLED -- CommitChkpt" << endl
			<< "  chkptFileName = " << chkptFileName 
			<< endl << endl;
      cio().end_out();
    }

    // call server function
    bool l_result = cs->CommitChkpt(chkptFileName);

    // post debugging
    if (this->debug >= CacheIntf::WeederOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "RETURNED -- CommitChkpt" << endl
			<< "  result: checkpoint " 
			<< (l_result?"accepted":"rejected") << endl << endl;
      cio().end_out();
    }

    // Indicate that this is the correct server instance
    srpc->send_int(1);

    // send results
    srpc->send_int((int) l_result);
    srpc->send_end();
}

void ExpCache::FlushAll(SRPC *srpc) throw (SRPC::failure)
{
    // get arguments
    srpc->recv_end();

    // pre debugging
    if (this->debug >= CacheIntf::MPKFileFlush) {
     cio().start_out() << Debug::Timestamp() 
		       << "CALLED -- FlushAll" << endl << endl;
     cio().end_out();
    }

    // call server function
    cs->FlushAll();

    // post debugging
    if (this->debug >= CacheIntf::MPKFileFlush) {
      cio().start_out() << Debug::Timestamp() 
			<< "RETURNED -- FlushAll" << endl << endl;
      cio().end_out();
    }

    // send results
    srpc->send_end();
}

void ExpCache::GetCacheId(SRPC *srpc) throw (SRPC::failure)
{
    // get arguments
    srpc->recv_end();

    // pre debugging
    if (this->debug >= CacheIntf::OtherOps) {
      cio().start_out()	<< Debug::Timestamp() 
			<< "CALLED -- GetCacheId" << endl << endl;
      cio().end_out();
    }

    // call server function
    CacheId id;
    cs->GetCacheId(/*OUT*/ id);

    // post debugging
    if (this->debug >= CacheIntf::OtherOps) {
      ostream& out_stream = cio().start_out();
      out_stream << Debug::Timestamp() << "RETURNED -- GetCacheId" << endl;
      id.Print(out_stream, /*indent=*/ 2);
      out_stream << endl;
      cio().end_out();
    }

    // send results
    id.Send(*srpc);
    srpc->send_end();
}

void ExpCache::GetCacheInstance(SRPC *srpc) throw (SRPC::failure)
{
    // get arguments
    srpc->recv_end();

    // pre debugging
    if (this->debug >= CacheIntf::OtherOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "CALLED -- GetCacheInstance" << endl << endl;
      cio().end_out();
    }

    // call server function
    FP::Tag fp = cs->GetCacheInstance();

    // post debugging
    if (this->debug >= CacheIntf::OtherOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "RETURNED -- GetCacheInstance" << endl 
			<< "  " << fp << endl << endl;
      cio().end_out();
    }

    // send results
    fp.Send(*srpc);
    srpc->send_end();
}

void ExpCache::GetCacheState(SRPC *srpc) throw (SRPC::failure)
{
    // get arguments
    srpc->recv_end();

    // pre debugging
    if (this->debug >= CacheIntf::OtherOps) {
      cio().start_out() << Debug::Timestamp() 
			<< "CALLED -- GetCacheState" << endl << endl;
      cio().end_out();
    }

    // call server function
    CacheState state;
    cs->GetCacheState(/*OUT*/ state);

    // post debugging
    if (this->debug >= CacheIntf::OtherOps) {
      ostream& out_stream = cio().start_out() ;
      out_stream << Debug::Timestamp() << "RETURNED -- GetCacheState" << endl;
      state.Print(out_stream, /*indent=*/ 2);
      out_stream << endl;
      cio().end_out();
    }

    // send results
    state.Send(*srpc);
    srpc->send_end();
}

