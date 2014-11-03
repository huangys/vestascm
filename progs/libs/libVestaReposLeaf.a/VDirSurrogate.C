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
// VDirSurrogate.C
//
// Remote surrogate for a VestaSource.  Used outside the repository
// address space (including in a different repository).
//

#include <pthread.h>
#include <assert.h>
#include <pwd.h>
#include <zlib.h> // Used for readWhole, which gets compressed data from the server
#include "VestaSource.H"
#include "VestaSourceSRPC.H"
#include "VDirSurrogate.H"
#include "MultiSRPC.H"
#include "VestaConfig.H"
#include "ListResults.H"

using std::cerr;
using std::endl;

// Module globals
static int listChunkSize = 8300;
static int listPerEntryOverhead = 40;

// Kludge, should never be used.  Forces readOnlyExisting argument
// to VDirSurrogate::createVolatileDirectory to be false.
static int forceWritableExisting = 0;

// These control buffer sizes in VDirSurrogate::readWhole below
static int readWhole_recv_bufsiz = (64*1024);
static int readWhole_inflate_bufsiz = (128*1024);

extern "C"
{
  void 
  VDirSurrogate_init()
  {
    try {
	Text val;
	if (VestaConfig::get("Repository",
			     "VestaSourceSRPC_listChunkSize", val)) {
	    listChunkSize = atoi(val.cchars());
	}
	if (VestaConfig::get("Repository",
			     "VestaSourceSRPC_listPerEntryOverhead", val)) {
	    listPerEntryOverhead = atoi(val.cchars());
	}
	if (VestaConfig::get("Repository", "forceWritableExisting", val)) {
	    forceWritableExisting = atoi(val.cchars());
	}
	if (VestaConfig::get("Repository",
			     "VestaSourceSRPC_readWhole_recv_bufsiz",
			     val)) {
	  readWhole_recv_bufsiz = atoi(val.cchars());
	}
	if (VestaConfig::get("Repository",
			     "VestaSourceSRPC_readWhole_inflate_bufsiz",
			     val)) {
	  readWhole_inflate_bufsiz = atoi(val.cchars());
	}
	AccessControl::commonInit();
    } catch (VestaConfig::failure f) {
	cerr << "VestaConfig::failure " << f.msg << endl;
	throw;  // likely to be uncaught and fatal
    }
  }
}

void
VDirSurrogateInit()
{
  static pthread_once_t vdsonce = PTHREAD_ONCE_INIT;
  pthread_once(&vdsonce, VDirSurrogate_init);
}

Text 
VDirSurrogate::defaultHost() throw()
{
  return VestaSourceSRPC::defaultHost();
}

Text 
VDirSurrogate::defaultPort() throw()
{
  return VestaSourceSRPC::defaultInterface();
}

VDirSurrogate::VDirSurrogate(time_t timestamp, ShortId sid,
			     const Text host__, const Text port__,
			     int executable, Basics::uint64 size)
  throw(SRPC::failure)
{
    VDirSurrogateInit();
    timestampCache = timestamp;
    sidCache = sid;
    if (host__.Empty()) {
	host_ = defaultHost();
	port_ = defaultPort();
    } else {
	host_ = host__;
	port_ = port__;
    }
    executableCache = executable;
    sizeCache = size;
}

// Common code for Lookup and CreateVolatileDirectory
static VestaSource::errorCode
GetLookupResult(SRPC* srpc, VestaSource::typeTag& type, LongId& olongid,
		bool& master, Bit32& pseudoInode, ShortId& sid,
		time_t& timestamp, Bit8*& attribs, FP::Tag& fptag)
{
    VestaSource::errorCode err =
      (VestaSource::errorCode) srpc->recv_int();
    if (err == VestaSource::ok) {
	type = (VestaSource::typeTag) srpc->recv_int();
	int len = sizeof(olongid.value);
	srpc->recv_bytes_here((char *) &olongid.value, len);
	master = (bool) srpc->recv_int();
	pseudoInode = (Bit32) srpc->recv_int();
	sid = srpc->recv_int();
	timestamp = (time_t) srpc->recv_int();
	attribs = (Bit8*) (bool) srpc->recv_int();
	fptag.Recv(*srpc);
    }
    srpc->recv_end();
    return err;
}

// Common code for LongId::lookup, VestaSource::lookup,
// and VestaSource::resync
static VestaSource::errorCode
Lookup(SRPC* srpc, const LongId& longid, const char* arc,
       AccessControl::Identity who,
       VestaSource::typeTag& type, LongId& olongid, bool& master,
       Bit32& pseudoInode, ShortId& sid, time_t& timestamp, Bit8*& attribs,
       FP::Tag& fptag)
{
    VestaSource::errorCode err;

    srpc->start_call(VestaSourceSRPC::Lookup,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_chars(arc);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_end();

    err = GetLookupResult(srpc, type, olongid, master, pseudoInode, sid,
			  timestamp, attribs, fptag);

    return err;
}

VestaSource*
VDirSurrogate::LongIdLookup(LongId longid, Text host, Text port,
			    AccessControl::Identity who)
  throw(SRPC::failure)
{
    ShortId sid;
    VestaSource::errorCode err;
    VestaSource::typeTag otype;
    LongId olongid;
    bool omaster;
    Bit32 opseudoInode;
    ShortId osid;
    time_t otimestamp;
    Bit8* oattribs;
    FP::Tag ofptag;
    VestaSource* result;
    VDirSurrogate* sresult;    
    SRPC* srpc;
    
    VDirSurrogateInit();
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host, port);
    err = Lookup(srpc, longid, "", who, otype, olongid, omaster, opseudoInode,
		 osid, otimestamp, oattribs, ofptag);
    VestaSourceSRPC::End(id);

    if (err != VestaSource::ok) return NULL;
    
    switch (otype) {
      case VestaSource::immutableFile:
      case VestaSource::mutableFile:
      case VestaSource::ghost:
      case VestaSource::stub:
      case VestaSource::device:
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
	sresult = NEW_CONSTR(VDirSurrogate, (otimestamp, osid, host, port));
	result = sresult;
	break;
	
      default:
	assert(false);
    };
    
    result->rep = NULL;
    result->type = otype;
    result->longid = olongid;
    result->master = omaster;
    result->pseudoInode = opseudoInode;
    result->attribs = oattribs;
    result->fptag = ofptag;

    return result;
}

bool
VDirSurrogate::LongIdValid(LongId longid, Text host, Text port,
			   AccessControl::Identity who)
  throw(SRPC::failure)
{
    ShortId sid;
    VestaSource::errorCode err;
    VestaSource::typeTag otype;
    LongId olongid;
    bool omaster;
    Bit32 opseudoInode;
    ShortId osid;
    time_t otimestamp;
    Bit8* oattribs;
    FP::Tag ofptag;
    VestaSource* result;
    VDirSurrogate* sresult;    
    SRPC* srpc;
    
    VDirSurrogateInit();
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host, port);
    err = Lookup(srpc, longid, "", who, otype, olongid, omaster, opseudoInode,
		 osid, otimestamp, oattribs, ofptag);
    VestaSourceSRPC::End(id);

    return (err == VestaSource::ok);
}

VestaSource*
VDirSurrogate::repositoryRoot(Text host, Text port, AccessControl::Identity who
			      ) throw (SRPC::failure)
{
    VestaSource *ret = NEW_CONSTR(VDirSurrogate, (2, NullShortId, host, port));
    ret->longid = RootLongId;
    ret->resync(who);
    return ret;
}

VestaSource*
VDirSurrogate::mutableRoot(Text host, Text port, AccessControl::Identity who
			   ) throw (SRPC::failure)
{
    VestaSource *ret = NEW_CONSTR(VDirSurrogate, (2, NullShortId, host, port));
    ret->longid = MutableRootLongId;
    ret->resync(who);
    return ret;
}

VestaSource*
VDirSurrogate::volatileRoot(Text host, Text port, AccessControl::Identity who
			    ) throw (SRPC::failure)
{
    VestaSource *ret = NEW_CONSTR(VDirSurrogate, (2, NullShortId, host, port));
    ret->longid = VolatileRootLongId;
    ret->resync(who);
    return ret;
}

VestaSource::errorCode
VDirSurrogate::createVolatileDirectory(const char* hostname, Bit64 handle,
				       VestaSource*& result,
				       time_t timestamp,
				       bool readOnlyExisting,
				       Text reposHost, Text reposPort)
  throw (SRPC::failure)
{
    static Text port;
    if (port.Empty()) {
	try {
	    port = VestaConfig::get_Text("Evaluator", "EvaluatorDirSRPC_port");
	} catch (VestaConfig::failure f) {
	    cerr << "createVolatileDirectory got VestaConfig::failure: "
	      << f.msg << endl;
	    throw;
	}
    }

    return VDirSurrogate::createVolatileDirectory(hostname, port.cchars(),
						  handle, result, timestamp,
						  readOnlyExisting,
						  reposHost, reposPort);
}


VestaSource::errorCode
VDirSurrogate::createVolatileDirectory(const char* hostname, const char* port,
				       Bit64 handle,
				       VestaSource*& result, time_t timestamp,
				       bool readOnlyExisting,
				       Text reposHost, Text reposPort)
     throw (SRPC::failure)
{
    VestaSource::errorCode err;
    VestaSource::typeTag otype;
    LongId olongid;
    bool omaster;
    Bit32 opseudoInode;
    ShortId osid;
    time_t otimestamp;
    Bit8* oattribs;
    FP::Tag ofptag;
    SRPC* srpc;
    
    VDirSurrogateInit();
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, reposHost, reposPort);
    
    // Kludge, please ignore
    if (forceWritableExisting) readOnlyExisting = false;

    srpc->start_call(VestaSourceSRPC::CreateVolatileDirectory,
		     VestaSourceSRPC::version);
    srpc->send_chars(hostname);
    srpc->send_chars(port);
    srpc->send_bytes((char*) &handle, sizeof(handle));
    srpc->send_int((int) timestamp);
    srpc->send_int((int) readOnlyExisting);
    srpc->send_end();
    
    err = GetLookupResult(srpc, otype, olongid, omaster, opseudoInode, osid,
			  otimestamp, oattribs, ofptag);
    VestaSourceSRPC::End(id);
    if (err != VestaSource::ok) return err;
    
    assert(otype == (readOnlyExisting ? VestaSource::volatileROEDirectory :
		     VestaSource::volatileDirectory));
    
    result =
      NEW_CONSTR(VDirSurrogate, (otimestamp, osid, reposHost, reposPort));
    result->rep = NULL;
    result->type = otype;
    result->longid = olongid;
    result->master = omaster;
    result->pseudoInode = opseudoInode;
    result->attribs = oattribs;
    result->fptag = ofptag;
    
    return VestaSource::ok;
}

VestaSource::errorCode
VDirSurrogate::deleteVolatileDirectory(VestaSource* dir)
     throw (SRPC::failure)
{
    SRPC* srpc;
    VDirSurrogate *vds = (VDirSurrogate *)dir; // ugh
    assert(dir->type == VestaSource::volatileDirectory ||
	   dir->type == VestaSource::volatileROEDirectory);
    VDirSurrogateInit();
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc,
						  vds->host_, vds->port_);
    srpc->start_call(VestaSourceSRPC::DeleteVolatileDirectory,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &dir->longid.value,
		     sizeof(dir->longid.value));
    srpc->send_end();
    VestaSource::errorCode err =
      (VestaSource::errorCode) srpc->recv_int();
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}

void
VDirSurrogate::getNFSInfo(char*& socket, LongId& root, LongId& muRoot)
     throw (SRPC::failure)
{
    int len;
    SRPC* srpc;
    VDirSurrogateInit();
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc);
    srpc->start_call(VestaSourceSRPC::GetNFSInfo,
		     VestaSourceSRPC::version);
    srpc->send_end();
    socket = srpc->recv_chars();
    len = sizeof(LongId);
    srpc->recv_bytes_here((char *) &root.value, len);
    len = sizeof(LongId);
    srpc->recv_bytes_here((char *) &muRoot.value, len);
    srpc->recv_end();
    VestaSourceSRPC::End(id);
}

ShortId VDirSurrogate::fpToShortId(const FP::Tag& fptag, Text host, Text port)
     throw (SRPC::failure)
{
    ShortId sid;
    SRPC* srpc;
    VDirSurrogateInit();
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host, port);
    srpc->start_call(VestaSourceSRPC::FPToShortId,
		     VestaSourceSRPC::version);
    fptag.Send(*srpc);
    srpc->send_end();

    sid = (ShortId)srpc->recv_int();
    srpc->recv_end();
    VestaSourceSRPC::End(id);

    return sid;	
}

void
VDirSurrogate::getUserInfo(AccessControl::IdInfo &result /*OUT*/,
			   AccessControl::Identity who,
			   AccessControl::Identity subject,
			   Text reposHost,
			   Text reposPort)
  throw (SRPC::failure)
{
  // Make sure that the default repository is initialized.
  VDirSurrogateInit();

  // If no user was explicitly requested, then we get information
  // about the calling user.
  if(subject == NULL)
    {
      subject = who;
    }

  // Contact the repository.
  SRPC* srpc;
  MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, reposHost, reposPort);

  // Make the GetUserInfo call, passing two identities: the requestor
  // and the subject.
  srpc->start_call(VestaSourceSRPC::GetUserInfo,
		   VestaSourceSRPC::version);
  VestaSourceSRPC::send_identity(srpc, who);
  VestaSourceSRPC::send_identity(srpc, subject);
  srpc->send_end();

  // Receive the global names (including all aliases) of this user.
  srpc->recv_chars_seq(result.names);

  // Receive the global groups of which this user is a member.
  srpc->recv_chars_seq(result.groups);

  // Receive the user's UNIX user and group IDs as used by the
  // repository's NFS interface.
  result.unix_uid = srpc->recv_int32();
  result.unix_gid = srpc->recv_int32();

  // Receive the special permissions of the user (encoded as bits in a
  // 16-bit integer).
  Basics::uint16 specials = srpc->recv_int16();
  result.is_root = specials & 1;
  result.is_admin = specials & (1 << 1);
  result.is_wizard = specials & (1 << 2);
  result.is_runtool = specials & (1 << 3);

  // End the call.
  srpc->recv_end();
  VestaSourceSRPC::End(id);
}

void
VDirSurrogate::refreshAccessTables(AccessControl::Identity who,
				   Text reposHost,
				   Text reposPort)
  throw (SRPC::failure)
{
  VDirSurrogateInit();

  SRPC* srpc;
  MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, reposHost, reposPort);

  srpc->start_call(VestaSourceSRPC::RefreshAccessTables,
		   VestaSourceSRPC::version);
  VestaSourceSRPC::send_identity(srpc, who);
  srpc->send_end();

  srpc->recv_end();
  VestaSourceSRPC::End(id);
}

VestaSource::errorCode
VDirSurrogate::acquireMastership(const char* pathname,
				 Text dstHost, Text dstPort,
				 Text srcHost, Text srcPort,
				 char pathnameSep,
				 AccessControl::Identity dwho,
				 AccessControl::Identity swho)
     throw (SRPC::failure)
{
    SRPC* srpc;
    VDirSurrogateInit();

    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, dstHost, dstPort);
    srpc->start_call(VestaSourceSRPC::AcquireMastership,
		     VestaSourceSRPC::version);
    srpc->send_chars(pathname);
    srpc->send_chars(srcHost.cchars());
    srpc->send_chars(srcPort.cchars());
    srpc->send_int((int) pathnameSep);
    VestaSourceSRPC::send_identity(srpc, dwho);
    VestaSourceSRPC::send_identity(srpc, swho ? swho : dwho);
    srpc->send_end();
    VestaSource::errorCode err =
      (VestaSource::errorCode) srpc->recv_int();
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}


VestaSource::errorCode
VDirSurrogate::replicate(const char* pathname,
			 bool asStub, bool asGhost,
			 Text dstHost, Text dstPort,
			 Text srcHost, Text srcPort,
			 char pathnameSep,
			 AccessControl::Identity dwho,
			 AccessControl::Identity swho)
     throw (SRPC::failure)
{
    SRPC* srpc;
    VDirSurrogateInit();

    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, dstHost, dstPort);
    srpc->start_call(VestaSourceSRPC::Replicate,
		     VestaSourceSRPC::version);
    srpc->send_chars(pathname);
    srpc->send_int((int) asStub);
    srpc->send_int((int) asGhost);
    srpc->send_chars(srcHost.cchars());
    srpc->send_chars(srcPort.cchars());
    srpc->send_int((int) pathnameSep);
    VestaSourceSRPC::send_identity(srpc, dwho);
    VestaSourceSRPC::send_identity(srpc, swho ? swho : dwho);
    srpc->send_end();
    VestaSource::errorCode err =
      (VestaSource::errorCode) srpc->recv_int();
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}

VestaSource::errorCode
VDirSurrogate::replicateAttribs(const char* pathname,
				bool includeAccess,
				Text dstHost, Text dstPort,
				Text srcHost, Text srcPort,
				char pathnameSep,
				AccessControl::Identity dwho,
				AccessControl::Identity swho)
     throw (SRPC::failure)
{
    SRPC* srpc;
    VDirSurrogateInit();

    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, dstHost, dstPort);
    srpc->start_call(VestaSourceSRPC::ReplicateAttribs,
		     VestaSourceSRPC::version);
    srpc->send_chars(pathname);
    srpc->send_int((int) includeAccess);
    srpc->send_chars(srcHost.cchars());
    srpc->send_chars(srcPort.cchars());
    srpc->send_int((int) pathnameSep);
    VestaSourceSRPC::send_identity(srpc, dwho);
    VestaSourceSRPC::send_identity(srpc, swho ? swho : dwho);
    srpc->send_end();
    VestaSource::errorCode err =
      (VestaSource::errorCode) srpc->recv_int();
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}


void
VDirSurrogate::resync(AccessControl::Identity who) throw (SRPC::failure)
{
    // Redo lookup of our own longid
    VestaSource::errorCode err;
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);
    err = Lookup(srpc, longid, "", who, type, longid, master,
		 pseudoInode, sidCache, timestampCache, attribs, fptag);
    VestaSourceSRPC::End(id);
    assert(err == VestaSource::ok);
    // Clear stat cache
    executableCache = -1;
    sizeCache = (Basics::uint64) -1;
}

VestaSource::errorCode
VDirSurrogate::lookup(Arc arc, VestaSource*& result,
		      AccessControl::Identity who,
		      unsigned int indexOffset) throw (SRPC::failure)
{
  // Start by initializing the result, in case we fail before setting
  // it to something useful.
  result = 0;

    assert(indexOffset == 0);
    ShortId sid;
    VestaSource::errorCode err;
    VestaSource::typeTag otype;
    LongId olongid;
    bool omaster;
    Bit32 opseudoInode;
    ShortId osid;
    time_t otimestamp;
    VDirSurrogate* sresult;    
    Bit8* oattribs;
    FP::Tag ofptag;
    
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    err = Lookup(srpc, longid, arc, who, otype, olongid, omaster,
		 opseudoInode, osid, otimestamp, oattribs, ofptag);
    VestaSourceSRPC::End(id);
    if (err != VestaSource::ok) return err;
    
    switch (otype) {
      case VestaSource::immutableFile:
      case VestaSource::mutableFile:
      case VestaSource::ghost:
      case VestaSource::stub:
      case VestaSource::device:
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
	sresult = NEW_CONSTR(VDirSurrogate, (otimestamp, osid, host_, port_));
	result = sresult;
	break;
	
      default:
	assert(false);
    };
    
    result->rep = NULL;
    result->type = otype;
    result->longid = olongid;
    result->master = omaster;
    result->pseudoInode = opseudoInode;
    result->attribs = oattribs;
    result->fptag = ofptag;
    
    return VestaSource::ok;
}

VestaSource::errorCode
VDirSurrogate::lookupIndex(unsigned int index, VestaSource*& result,
			   char *arcbuf) throw (SRPC::failure)
{
  // Start by initializing the result, in case we fail before setting
  // it to something useful.
  result = 0;

    ShortId sid;
    VestaSource::errorCode err;
    VestaSource::typeTag otype;
    LongId olongid;
    bool omaster;
    Bit32 opseudoInode;
    ShortId osid;
    time_t otimestamp;
    VDirSurrogate* sresult;    
    Bit8* oattribs;
    FP::Tag ofptag;
    
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::LookupIndex,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_int((int) index);
    srpc->send_int((int) (arcbuf != NULL));
    srpc->send_end();

    err = (VestaSource::errorCode) srpc->recv_int();
    if (err == VestaSource::ok) {
	otype = (VestaSource::typeTag) srpc->recv_int();
	int len = sizeof(olongid.value);
	srpc->recv_bytes_here((char *) &olongid.value, len);
	omaster = (bool) srpc->recv_int();
	opseudoInode = (Bit32) srpc->recv_int();
	osid = srpc->recv_int();
	otimestamp = (time_t) srpc->recv_int();
	oattribs = (Bit8*) (bool) srpc->recv_int();
	ofptag.Recv(*srpc);
	if (arcbuf != NULL) {
	    int arclen = MAX_ARC_LEN + 1;
	    srpc->recv_chars_here(arcbuf, arclen);
	}
    }
    srpc->recv_end();

    VestaSourceSRPC::End(id);
    if (err != VestaSource::ok) return err;
    
    switch (otype) {
      case VestaSource::immutableFile:
      case VestaSource::mutableFile:
      case VestaSource::ghost:
      case VestaSource::stub:
      case VestaSource::device:
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
	sresult = NEW_CONSTR(VDirSurrogate, (otimestamp, osid, host_, port_));
	result = sresult;
	break;
	
      default:
	assert(false);
    };
    
    result->rep = NULL;
    result->type = otype;
    result->longid = olongid;
    result->master = omaster;
    result->pseudoInode = opseudoInode;
    result->attribs = oattribs;
    result->fptag = ofptag;
    
    return VestaSource::ok;
}

VestaSource::errorCode
VDirSurrogate::lookupPathname(const char* pathname, VestaSource*& result,
			      AccessControl::Identity who, char pathnameSep)
     throw (SRPC::failure)
{
    ShortId sid;
    VestaSource::errorCode err;
    VestaSource::typeTag otype;
    LongId olongid;
    bool omaster;
    Bit32 opseudoInode;
    ShortId osid;
    time_t otimestamp;
    VDirSurrogate* sresult;    
    Bit8* oattribs;
    FP::Tag ofptag;
    
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::LookupPathname,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_chars(pathname);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_int((int) pathnameSep);
    srpc->send_end();

    err = GetLookupResult(srpc, otype, olongid, omaster, opseudoInode, osid,
			  otimestamp, oattribs, ofptag);
    VestaSourceSRPC::End(id);
    if (err != VestaSource::ok) return err;
    
    switch (otype) {
      case VestaSource::immutableFile:
      case VestaSource::mutableFile:
      case VestaSource::ghost:
      case VestaSource::stub:
      case VestaSource::device:
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
	sresult = NEW_CONSTR(VDirSurrogate, (otimestamp, osid, host_, port_));
	result = sresult;
	break;
	
      default:
	assert(false);
    };
    
    result->rep = NULL;
    result->type = otype;
    result->longid = olongid;
    result->master = omaster;
    result->pseudoInode = opseudoInode;
    result->attribs = oattribs;
    result->fptag = ofptag;
    
    return VestaSource::ok;
}

VestaSource::errorCode
VDirSurrogate::getBase(VestaSource*& result, AccessControl::Identity who)
     throw (SRPC::failure)
{
    VestaSource::errorCode err;
    VDirSurrogateInit();
    SRPC* srpc;   
    VestaSource::typeTag otype;
    LongId olongid;
    bool omaster;
    Bit32 opseudoInode;
    ShortId osid;
    time_t otimestamp;
    Bit8* oattribs;
    FP::Tag ofptag;

    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);
    srpc->start_call(VestaSourceSRPC::GetBase, VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &this->longid.value,
		     sizeof(this->longid.value));
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_end();

    err = GetLookupResult(srpc, otype, olongid, omaster, opseudoInode, osid,
			  otimestamp, oattribs, ofptag);
    
    VestaSourceSRPC::End(id);
    if (err != VestaSource::ok) return err;
    
    VDirSurrogate* sresult;    
    switch (otype) {
      case VestaSource::immutableFile:
      case VestaSource::mutableFile:
      case VestaSource::ghost:
      case VestaSource::stub:
      case VestaSource::device:
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
	sresult = NEW_CONSTR(VDirSurrogate, (otimestamp, osid, host_, port_));
	result = sresult;
	break;
	
      default:
	assert(false);
    };
    
    result->rep = NULL;
    result->type = otype;
    result->longid = olongid;
    result->master = omaster;
    result->pseudoInode = opseudoInode;
    result->attribs = oattribs;
    result->fptag = ofptag;
    
    return VestaSource::ok;
}


VestaSource::errorCode
VDirSurrogate::list(unsigned int firstIndex,
		    VestaSource::listCallback callback, void* closure,
		    AccessControl::Identity who,
		    bool deltaOnly, unsigned int indexOffset)
{
    assert(indexOffset == 0);
    int curChunkSize = 0;
    VestaSource::errorCode err;
    unsigned int index = firstIndex;

    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    // Do multiple calls, each fetching about chunkSize bytes worth of
    // results.  The idea is that our client will probably return
    // false ("stop") after it has received that many bytes, so we
    // don't want to get more than that in a stream from the server.
    bool stop_req = false, dir_end = false;
    // Note that we buffer the results of each call *before* calling
    // the callback, as we don't want to hold up the server during the
    // callback's execution.
    ListResultSeq list_result;
    do {
	srpc->start_call(VestaSourceSRPC::List,
			 VestaSourceSRPC::version);
	srpc->send_bytes((const char*) &longid.value,
			 sizeof(longid.value));
	srpc->send_int(index);
	VestaSourceSRPC::send_identity(srpc, who);
	srpc->send_int(deltaOnly);
	srpc->send_int(listChunkSize);
	srpc->send_int(listPerEntryOverhead);
	srpc->send_end();

	err = VestaSource::ok;
	srpc->recv_seq_start();
	for (;;) {
	    ListResultItem list_item;
	    int arclen = sizeof(list_item.arc);
	    bool got_end;
	    srpc->recv_chars_here(list_item.arc, arclen, &got_end);
	    if (got_end) {
		// Sequence ended due to chunkSize limit
		err = VestaSource::ok;
		break;
	    }
	    list_item.type = (VestaSource::typeTag) srpc->recv_int();
	    index = (unsigned int) srpc->recv_int();
	    list_item.index = index;
	    if (list_item.type == VestaSource::unused) {
		// End of directory reached, or error
		dir_end = true;
		err = (VestaSource::errorCode) index;
		break;
	    }
	    list_item.pseudoInode = (Bit32) srpc->recv_int();
	    list_item.filesid = (ShortId) srpc->recv_int();
	    list_item.master = (bool) srpc->recv_int();

	    // Add this item to the result sequence.
	    list_result.addhi(list_item);
	}
	srpc->recv_seq_end();
	srpc->recv_end();
	index += 2; // don't read the same one again

	// Call the callback once per entry returned by the server
	// until the callback tells us to stop or we run out.
	while((list_result.size() > 0) && !stop_req)
	  {
	    ListResultItem list_item = list_result.remlo();

	    curChunkSize += strlen(list_item.arc) + listPerEntryOverhead;

	    stop_req = !callback(closure, list_item.type, list_item.arc,
				 list_item.index, list_item.pseudoInode,
				 list_item.filesid, list_item.master);
	  }

	if (stop_req && listChunkSize > curChunkSize + 100)
	  {
	    // Seems we guessed wrong; use smaller size now.
	    listChunkSize = curChunkSize + 100;
	  }
    } while (!stop_req && !dir_end);
    
    VestaSourceSRPC::End(id);
    return err;
}

VestaSource::errorCode
VDirSurrogate::reallyDelete(Arc arc, AccessControl::Identity who,
			    bool existCheck, time_t timestamp)
     throw (SRPC::failure)
{
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::ReallyDelete,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &this->longid.value,
		     sizeof(this->longid.value));
    srpc->send_chars(arc);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_int((int) existCheck);
    srpc->send_int((int) timestamp);
    srpc->send_end();
    VestaSource::errorCode err =
      (VestaSource::errorCode) srpc->recv_int();
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}


VestaSource::errorCode
VDirSurrogate::insertFile(Arc arc, ShortId sid, bool master,
			  AccessControl::Identity who,
			  VestaSource::dupeCheck chk,
			  VestaSource** newvs, time_t timestamp,
			  FP::Tag* fptag) throw (SRPC::failure)
{
    if (newvs) *newvs = NULL; // in case of error
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::InsertFile,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &this->longid.value,
		     sizeof(this->longid.value));
    srpc->send_chars(arc);
    srpc->send_int((int) sid);
    srpc->send_int((int) master);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_int((int) chk);
    srpc->send_int((int) timestamp);
    if (fptag == NULL) {
	srpc->send_bytes(NULL, 0);
    } else {
	fptag->Send(*srpc);
    }
    srpc->send_end();
    VestaSource::errorCode err =
      (VestaSource::errorCode) srpc->recv_int();
    if (err == VestaSource::ok) {
	int len = sizeof(LongId);
	VDirSurrogate* vs =
	  NEW_CONSTR(VDirSurrogate, (timestamp, sid, host_, port_));
	vs->type = VestaSource::immutableFile;
	srpc->recv_bytes_here((char *) &vs->longid.value, len);
	vs->master = master;
	vs->pseudoInode = (Bit32) srpc->recv_int();
	vs->fptag.Recv(*srpc);
	vs->sidCache = (ShortId) srpc->recv_int();
	if (newvs == NULL)
	  delete vs;
	else
	  *newvs = vs;
    }
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}


VestaSource::errorCode
VDirSurrogate::insertMutableFile(Arc arc, ShortId sid, bool master,
				 AccessControl::Identity who,
				 VestaSource::dupeCheck chk,
				 VestaSource** newvs, time_t timestamp)
     throw (SRPC::failure)
{
    if (newvs) *newvs = NULL; // in case of error
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::InsertMutableFile,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &this->longid.value,
		     sizeof(this->longid.value));
    srpc->send_chars(arc);
    srpc->send_int((int) sid);
    srpc->send_int((int) master);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_int((int) chk);
    srpc->send_int((int) timestamp);
    srpc->send_end();
    VestaSource::errorCode err =
      (VestaSource::errorCode) srpc->recv_int();
    if (err == VestaSource::ok) {
	int len = sizeof(LongId);
	VDirSurrogate* vs =
	  NEW_CONSTR(VDirSurrogate, (timestamp, sid, host_, port_));
	vs->type = VestaSource::mutableFile;
	srpc->recv_bytes_here((char *) &vs->longid.value, len);
	vs->master = master;
	vs->pseudoInode = (Bit32) srpc->recv_int();
	vs->fptag.Recv(*srpc);
	vs->sidCache = (ShortId) srpc->recv_int();
	if (newvs == NULL)
	  delete vs;
	else
	  *newvs = vs;
    }
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}


VestaSource::errorCode
VDirSurrogate::insertImmutableDirectory(Arc arc, VestaSource* dir,
					bool master,
					AccessControl::Identity who,
					VestaSource::dupeCheck chk,
					VestaSource** newvs, time_t timestamp,
					FP::Tag* fptag)
     throw (SRPC::failure)
{
    if (newvs) *newvs = NULL; // in case of error
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::InsertImmutableDirectory,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &this->longid.value,
		     sizeof(this->longid.value));
    srpc->send_chars(arc);
    if (dir != NULL) {
	srpc->send_bytes((const char*) &dir->longid.value,
			 sizeof(dir->longid.value));
    } else {
	srpc->send_bytes((const char*) &NullLongId.value,
			 sizeof(NullLongId.value));
    }
    srpc->send_int((int) master);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_int((int) chk);
    srpc->send_int((int) timestamp);
    if (fptag == NULL) {
	srpc->send_bytes(NULL, 0);
    } else {
	fptag->Send(*srpc);
    }
    srpc->send_end();
    VestaSource::errorCode err =
      (VestaSource::errorCode) srpc->recv_int();
    if (err == VestaSource::ok) {
	int len = sizeof(LongId);
	VDirSurrogate* vs =
	  NEW_CONSTR(VDirSurrogate, (timestamp, NullShortId, host_, port_));
	vs->type = VestaSource::immutableDirectory;
	srpc->recv_bytes_here((char *) &vs->longid.value, len);
	vs->master = master;
	vs->pseudoInode = srpc->recv_int();
	vs->fptag.Recv(*srpc);
	vs->sidCache = srpc->recv_int();
	if (newvs == NULL)
	  delete vs;
	else
	  *newvs = vs;
    }
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}

VestaSource::errorCode
VDirSurrogate::insertAppendableDirectory(Arc arc, bool master,
					 AccessControl::Identity who,
					 VestaSource::dupeCheck chk,
					 VestaSource** newvs, time_t timestamp)
     throw (SRPC::failure)
{
    if (newvs) *newvs = NULL; // in case of error
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::InsertAppendableDirectory,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &this->longid.value,
		     sizeof(this->longid.value));
    srpc->send_chars(arc);
    srpc->send_int((int) master);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_int((int) chk);
    srpc->send_int((int) timestamp);
    srpc->send_end();
    VestaSource::errorCode err =
      (VestaSource::errorCode) srpc->recv_int();
    if (err == VestaSource::ok) {
	int len = sizeof(LongId);
	VDirSurrogate* vs =
	  NEW_CONSTR(VDirSurrogate, (timestamp, NullShortId, host_, port_));
	vs->type = VestaSource::appendableDirectory;
	srpc->recv_bytes_here((char *) &vs->longid.value, len);
	vs->master = master;
	vs->pseudoInode = srpc->recv_int();
	vs->fptag.Recv(*srpc);
	if (newvs == NULL)
	  delete vs;
	else
	  *newvs = vs;
    }
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}


VestaSource::errorCode
VDirSurrogate::insertMutableDirectory(Arc arc, VestaSource* dir,
				      bool master,
				      AccessControl::Identity who,
				      VestaSource::dupeCheck chk,
				      VestaSource** newvs, time_t timestamp)
     throw (SRPC::failure)
{
    if (newvs) *newvs = NULL; // in case of error
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::InsertMutableDirectory,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &this->longid.value,
		     sizeof(this->longid.value));
    srpc->send_chars(arc);
    if (dir != NULL) {
	srpc->send_bytes((const char*) &dir->longid.value,
			 sizeof(dir->longid.value));
    } else {
	srpc->send_bytes((const char*) &NullLongId.value,
			 sizeof(NullLongId.value));
    }
    srpc->send_int((int) master);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_int((int) chk);
    srpc->send_int((int) timestamp);
    srpc->send_end();
    VestaSource::errorCode err =
      (VestaSource::errorCode) srpc->recv_int();
    if (err == VestaSource::ok) {
	int len = sizeof(LongId);
	VDirSurrogate* vs =
	  NEW_CONSTR(VDirSurrogate, (timestamp, NullShortId, host_, port_));
	vs->type = VestaSource::mutableDirectory;
	srpc->recv_bytes_here((char *) &vs->longid.value, len);
	vs->master = master;
	vs->pseudoInode = srpc->recv_int();
	vs->fptag.Recv(*srpc);
	if (newvs == NULL)
	  delete vs;
	else
	  *newvs = vs;
    }
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}

VestaSource::errorCode
VDirSurrogate::insertGhost(Arc arc, bool master, AccessControl::Identity who,
			   VestaSource::dupeCheck chk, VestaSource** newvs,
			   time_t timestamp)
     throw (SRPC::failure)
{
    if (newvs) *newvs = NULL; // in case of error
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::InsertGhost,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &this->longid.value,
		     sizeof(this->longid.value));
    srpc->send_chars(arc);
    srpc->send_int((int) master);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_int((int) chk);
    srpc->send_int((int) timestamp);
    srpc->send_end();
    VestaSource::errorCode err =
      (VestaSource::errorCode) srpc->recv_int();
    if (err == VestaSource::ok) {
	int len = sizeof(LongId);
	VDirSurrogate* vs =
	  NEW_CONSTR(VDirSurrogate, (timestamp, NullShortId, host_, port_));
	vs->type = VestaSource::ghost;
	srpc->recv_bytes_here((char *) &vs->longid.value, len);
	vs->master = master;
	vs->pseudoInode = srpc->recv_int();
	vs->fptag.Recv(*srpc);
	if (newvs == NULL)
	  delete vs;
	else
	  *newvs = vs;
    }
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}

VestaSource::errorCode
VDirSurrogate::insertStub(Arc arc, bool master, AccessControl::Identity who,
			  VestaSource::dupeCheck chk,
			  VestaSource** newvs, time_t timestamp)
     throw (SRPC::failure)
{
    if (newvs) *newvs = NULL; // in case of error
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::InsertStub,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &this->longid.value,
		     sizeof(this->longid.value));
    srpc->send_chars(arc);
    srpc->send_int((int) master);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_int((int) chk);
    srpc->send_int((int) timestamp);
    srpc->send_end();
    VestaSource::errorCode err =
      (VestaSource::errorCode) srpc->recv_int();
    if (err == VestaSource::ok) {
	int len = sizeof(LongId);
	VDirSurrogate* vs =
	  NEW_CONSTR(VDirSurrogate, (timestamp, NullShortId, host_, port_));
	vs->type = VestaSource::stub;
	srpc->recv_bytes_here((char *) &vs->longid.value, len);
	vs->master = master;
	vs->pseudoInode = srpc->recv_int();
	vs->fptag.Recv(*srpc);
	if (newvs == NULL)
	  delete vs;
	else
	  *newvs = vs;
    }
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}


VestaSource::errorCode
VDirSurrogate::renameTo(Arc arc, VestaSource* fromDir, Arc fromArc,
			AccessControl::Identity who,
			VestaSource::dupeCheck chk, time_t timestamp)
     throw (SRPC::failure)
{
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::RenameTo,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &this->longid.value,
		     sizeof(this->longid.value));
    srpc->send_chars(arc);
    srpc->send_bytes((const char*) &fromDir->longid.value,
		     sizeof(fromDir->longid.value));
    srpc->send_chars(fromArc);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_int((int) chk);
    srpc->send_int((int) timestamp);
    srpc->send_end();
    VestaSource::errorCode err =
      (VestaSource::errorCode) srpc->recv_int();
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}


VestaSource::errorCode
VDirSurrogate::makeMutable(VestaSource*& result, ShortId sid,
			   Basics::uint64 copyMax, AccessControl::Identity who)
     throw (SRPC::failure)
{
    VestaSource::errorCode err;
    VestaSource::typeTag otype;
    LongId olongid;
    bool omaster;
    Bit32 opseudoInode;
    ShortId osid;
    time_t otimestamp;
    Bit8* oattribs;
    FP::Tag ofptag;
    
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    
    srpc->start_call(VestaSourceSRPC::MakeMutable,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &this->longid.value,
		     sizeof(this->longid.value));
    srpc->send_int((int) sid);
    srpc->send_int((int) (copyMax & 0xffffffff));
    srpc->send_int((int) (copyMax >> 32));
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_end();
    
    err = GetLookupResult(srpc, otype, olongid, omaster, opseudoInode, osid,
			  otimestamp, oattribs, ofptag);
    
    VestaSourceSRPC::End(id);
    if (err != VestaSource::ok) return err;
    
    VDirSurrogate* sresult;    
    switch (otype) {
      case VestaSource::immutableFile:
      case VestaSource::mutableFile:
      case VestaSource::ghost:
      case VestaSource::stub:
      case VestaSource::device:
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
	sresult = NEW_CONSTR(VDirSurrogate, (otimestamp, osid, host_, port_));
	result = sresult;
	break;
	
      default:
	assert(false);
    };
    
    result->rep = NULL;
    result->type = otype;
    result->longid = olongid;
    result->master = omaster;
    result->pseudoInode = opseudoInode;
    result->attribs = oattribs;
    result->fptag = ofptag;
    
    return VestaSource::ok;
}


bool
VDirSurrogate::inAttribs(const char* name, const char* value)
     throw (SRPC::failure)
{
    bool retval;

    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::InAttribs,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_chars(name);
    srpc->send_chars(value);
    srpc->send_end();

    retval = (bool) srpc->recv_int();
    srpc->recv_end();
    VestaSourceSRPC::End(id);

    return retval;
}

char*
VDirSurrogate::getAttrib(const char* name)
     throw (SRPC::failure)
{
    char* value;
    bool null;

    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::GetAttrib,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_chars(name);
    srpc->send_end();

    null = (bool) srpc->recv_int();
    value = srpc->recv_chars();
    srpc->recv_end();
    VestaSourceSRPC::End(id);

    if (null) {
        if(value != 0)
	  {
	    delete [] value;
	    value = 0; /*For GC applications*/
	  }
	return NULL;
    } else {
	return value;
    }
}

void
VDirSurrogate::getAttrib(const char* name,
			 VestaSource::valueCallback cb, void* cl)
{
    char* value;

    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::GetAttrib2,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_chars(name);
    srpc->send_end();

    srpc->recv_seq_start();
    AttribResultSeq l_result;
    for (;;) {
	bool got_end;
	value = srpc->recv_chars(&got_end);
	if (got_end) break;
	l_result.addhi(value);
    }
    srpc->recv_seq_end();
    srpc->recv_end();
    VestaSourceSRPC::End(id);

    bool cont = true;
    while(l_result.size() > 0)
      {
	char* value = l_result.remlo();
	if (cont) cont = cb(cl, value);
	delete [] value;
      }
}

void
VDirSurrogate::listAttribs(VestaSource::valueCallback cb, void* cl)
{
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::ListAttribs,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_end();

    srpc->recv_seq_start();
    AttribResultSeq l_result;
    for (;;) {
	bool got_end;
	char* value = srpc->recv_chars(&got_end);
	if (got_end) break;
	l_result.addhi(value);
    }
    srpc->recv_seq_end();
    srpc->recv_end();
    VestaSourceSRPC::End(id);

    bool cont = true;
    while(l_result.size() > 0)
      {
	char* value = l_result.remlo();
	if (cont) cont = cb(cl, value);
	delete [] value;
      }
}

void
VDirSurrogate::getAttribHistory(VestaSource::historyCallback cb, void* cl)
{
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::GetAttribHistory,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_end();

    srpc->recv_seq_start();
    AttribHistoryResultSeq l_result;
    for (;;) {
	bool got_end;
	AttribHistoryResultItem l_item;
	l_item.op = (VestaSource::attribOp) srpc->recv_int(&got_end);
	if (got_end) break;
	l_item.name = srpc->recv_chars();
	l_item.value = srpc->recv_chars();
	l_item.timestamp = (time_t) srpc->recv_int();
	l_result.addhi(l_item);
    }
    srpc->recv_seq_end();
    srpc->recv_end();
    VestaSourceSRPC::End(id);

    bool cont = true;
    while(l_result.size() > 0)
      {
	AttribHistoryResultItem l_item = l_result.remlo();
	if (cont) cont = cb(cl,
			    l_item.op, l_item.name, l_item.value,
			    l_item.timestamp);
	delete [] l_item.name;
	delete [] l_item.value;
      }
}

VestaSource::errorCode
VDirSurrogate::writeAttrib(VestaSource::attribOp op, const char* name,
			   const char* value, AccessControl::Identity who,
			   time_t timestamp)
     throw (SRPC::failure)
{
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::WriteAttrib,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_int((int) op);
    srpc->send_chars(name);
    srpc->send_chars(value);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_int((int) timestamp);
    srpc->send_end();
    VestaSource::errorCode err =
      (VestaSource::errorCode) srpc->recv_int();
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}


VestaSource::errorCode
VDirSurrogate::makeFilesImmutable(unsigned int threshold,
				  AccessControl::Identity who)
     throw (SRPC::failure)
{
    VestaSource::errorCode err;
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::MakeFilesImmutable,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_int((int) threshold);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_end();
    err = (VestaSource::errorCode) srpc->recv_int();
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}    

VestaSource::errorCode
VDirSurrogate::setIndexMaster(unsigned int index, bool state,
			      AccessControl::Identity who)
     throw (SRPC::failure)
{
    VestaSource::errorCode err;
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::SetIndexMaster,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_int((int) index);
    srpc->send_int((int) state);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_end();
    err = (VestaSource::errorCode) srpc->recv_int();
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}

VestaSource::errorCode
VDirSurrogate::setMaster(bool state, AccessControl::Identity who)
     throw (SRPC::failure)
{
    unsigned int index;
    LongId plongid = longid.getParent(&index);

    VestaSource::errorCode err;
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::SetIndexMaster,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &plongid.value,
		     sizeof(plongid.value));
    srpc->send_int((int) index);
    srpc->send_int((int) state);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_end();
    err = (VestaSource::errorCode) srpc->recv_int();
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    if (err == VestaSource::ok) {
	master = state;
    }
    return err;
}

VestaSource::errorCode
VDirSurrogate::cedeMastership(const char* requestid, const char** grantidOut,
			      AccessControl::Identity who)
     throw (SRPC::failure)
{
  *grantidOut = NULL;
  VestaSource::errorCode err;
  VDirSurrogateInit();
  SRPC* srpc;
  MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

  srpc->start_call(VestaSourceSRPC::CedeMastership,
		   VestaSourceSRPC::version);
  srpc->send_bytes((const char*) &longid.value, sizeof(longid.value));
  srpc->send_chars(requestid);
  VestaSourceSRPC::send_identity(srpc, who);
  srpc->send_end();
  err = (VestaSource::errorCode) srpc->recv_int();
  if (err == VestaSource::ok) {
    *grantidOut = srpc->recv_chars();
  }
  srpc->recv_end();
  VestaSourceSRPC::End(id);
  return err;
}

VestaSource::errorCode
VDirSurrogate::read(void* buffer, /*INOUT*/int* nbytes,
		    Basics::uint64 offset, AccessControl::Identity who)
     throw (SRPC::failure)
{
    VestaSource::errorCode err;
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id =VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::Read,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_int(*nbytes);
    srpc->send_int(offset & 0xffffffff);
    srpc->send_int(offset >> 32ul);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_end();
    err = (VestaSource::errorCode) srpc->recv_int();
    if (err == VestaSource::ok) {
	srpc->recv_bytes_here((char *)buffer, *nbytes);
    }
    srpc->recv_end();
	VestaSourceSRPC::End(id);
    return err;
}

static void
DoStat(VDirSurrogate* thiz, time_t& timestampCache, int& executableCache, 
       Basics::uint64& sizeCache)
     throw (SRPC::failure)
{
    VestaSource::errorCode err;
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc,
						  thiz->host(), thiz->port());

    srpc->start_call(VestaSourceSRPC::Stat,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &thiz->longid.value,
		     sizeof(thiz->longid.value));
    srpc->send_end();
    err = (VestaSource::errorCode) srpc->recv_int();
    timestampCache = (time_t) srpc->recv_int();
    executableCache = srpc->recv_int();
    sizeCache = (unsigned int) srpc->recv_int();
    sizeCache += ((Basics::uint64) srpc->recv_int()) << 32;
    srpc->recv_end();
   VestaSourceSRPC::End(id);
}

bool
VDirSurrogate::executable()
     throw (SRPC::failure)
{
    if (executableCache == -1) {
	DoStat(this, timestampCache, executableCache, sizeCache);
    }
    return executableCache;
}

Basics::uint64
VDirSurrogate::size()
     throw (SRPC::failure)
{
    if (sizeCache == (Basics::uint64) -1) {
	DoStat(this, timestampCache, executableCache, sizeCache);
    }
    return sizeCache;
}

time_t
VDirSurrogate::timestamp()
     throw (SRPC::failure)
{
    if (timestampCache == (time_t) -1) {
	DoStat(this, timestampCache, executableCache, sizeCache);
    }
    return timestampCache;
}

VestaSource::errorCode
VDirSurrogate::write(const void* buffer, /*INOUT*/int* nbytes,
		     Basics::uint64 offset, AccessControl::Identity who)
     throw (SRPC::failure)
{
    VestaSource::errorCode err;
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id =VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::Write,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_int(offset & 0xffffffff);
    srpc->send_int(offset >> 32ul);
    srpc->send_bytes((const char*)buffer, *nbytes);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_end();
    err = (VestaSource::errorCode) srpc->recv_int();
    if (err == VestaSource::ok) {
	*nbytes = srpc->recv_int();
	timestampCache = -1;
	sizeCache = (Basics::uint64) -1;
    }
    srpc->recv_end();
   VestaSourceSRPC::End(id);
    return err;
}

VestaSource::errorCode 
VDirSurrogate::setExecutable(bool x, AccessControl::Identity who)
     throw (SRPC::failure)
{
    VestaSource::errorCode err;
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id =VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::SetExecutable,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_int((int) x);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_end();
    err = (VestaSource::errorCode) srpc->recv_int();
    if (err == VestaSource::ok) {
	executableCache = x;
    }
    srpc->recv_end();
   VestaSourceSRPC::End(id);
    return err;
}

VestaSource::errorCode 
VDirSurrogate::setSize(Basics::uint64 s, AccessControl::Identity who)
     throw (SRPC::failure)
{
    VestaSource::errorCode err;
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id =VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::SetSize,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_int((int)(s & 0xffffffff));
    srpc->send_int((int)(s >> 32));
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_end();
    err = (VestaSource::errorCode) srpc->recv_int();
    if (err == VestaSource::ok) {
	sizeCache = s;
	timestampCache = (time_t) -1;
    }
    srpc->recv_end();
   VestaSourceSRPC::End(id);
    return err;
}

VestaSource::errorCode
VDirSurrogate::setTimestamp(time_t ts, AccessControl::Identity who)
     throw (SRPC::failure)
{
    VestaSource::errorCode err;
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id =VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::SetTimestamp,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    srpc->send_int((int) ts);
    VestaSourceSRPC::send_identity(srpc, who);
    srpc->send_end();
    err = (VestaSource::errorCode) srpc->recv_int();
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    if (err == VestaSource::ok) {
	timestampCache = ts;
    }
    return err;
}

VestaSource::errorCode
VDirSurrogate::measureDirectory(/*OUT*/VestaSource::directoryStats &result,
				AccessControl::Identity who)
  throw (SRPC::failure)
{
    VestaSource::errorCode err;
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id =VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::MeasureDirectory,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    VestaSourceSRPC::send_identity(srpc, who); 
    srpc->send_end(); 
    err = (VestaSource::errorCode) srpc->recv_int();
    if (err == VestaSource::ok)
      {
	result.baseChainLength = srpc->recv_int32();
	result.usedEntryCount = srpc->recv_int32();
	result.usedEntrySize = srpc->recv_int32();
	result.totalEntryCount = srpc->recv_int32();
	result.totalEntrySize = srpc->recv_int32();
      }
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}

VestaSource::errorCode
VDirSurrogate::collapseBase(AccessControl::Identity who)
  throw (SRPC::failure)
{
    VestaSource::errorCode err;
    VDirSurrogateInit();
    SRPC* srpc;
    MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, host_, port_);

    srpc->start_call(VestaSourceSRPC::CollapseBase,
		     VestaSourceSRPC::version);
    srpc->send_bytes((const char*) &longid.value,
		     sizeof(longid.value));
    VestaSourceSRPC::send_identity(srpc, who); 
    srpc->send_end(); 
    err = (VestaSource::errorCode) srpc->recv_int();
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    return err;
}

Text VDirSurrogate::getMasterHint(const Text &reposHost,
				  const Text &reposPort)
  throw(SRPC::failure)
{
  VDirSurrogateInit();
  SRPC* srpc;
  Text master_hint;
  MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, reposHost, reposPort);
  try
    {
      srpc->start_call(VestaSourceSRPC::GetMasterHint,
		       VestaSourceSRPC::version);
      srpc->send_end();
      srpc->recv_Text(master_hint);
      srpc->recv_end();
    }
  catch(SRPC::failure f)
    {
      if((f.r == SRPC::version_skew) &&
	 (f.msg == "VestaSourceSRPC: Unknown proc_id"))
	{
	  // Fall back on the "host:port" answer for old servers that
	  // don't support this call.
	  master_hint = Text::printf("%s:%s", reposHost.cchars(),
				     reposPort.cchars());
	}
      else
	{
	  VestaSourceSRPC::End(id);
	  throw;
	}
    }
  VestaSourceSRPC::End(id);
  
  return master_hint;
}

VestaSource::errorCode
VDirSurrogate::readWhole(std::ostream &out, AccessControl::Identity who)
  throw (SRPC::failure)
{
  // Compression methods supported by the client (just zlib for now).
  static Basics::int16 compression_methods[] = {
    VestaSourceSRPC::compress_zlib_deflate
  };
  static unsigned int compression_method_count =
    (sizeof(compression_methods) / sizeof(compression_methods[0]));

  VestaSource::errorCode err;
  VDirSurrogateInit();
  SRPC* srpc;
  MultiSRPC::ConnId id;

  id = VestaSourceSRPC::Start(srpc, host_, port_);

  srpc->start_call(VestaSourceSRPC::ReadWholeCompressed,
		   VestaSourceSRPC::version);
  srpc->send_bytes((const char*) &longid.value,
		   sizeof(longid.value));
  VestaSourceSRPC::send_identity(srpc, who); 
  srpc->send_int16_array(compression_methods, compression_method_count);
  srpc->send_int32(readWhole_recv_bufsiz);
  srpc->send_end(); 
  err = (VestaSource::errorCode) srpc->recv_int();
  if (err == VestaSource::ok)
    {
      Basics::int16 method_used = srpc->recv_int16();
      if(method_used != VestaSourceSRPC::compress_zlib_deflate)
	srpc->send_failure(SRPC::not_implemented,
			   "unsupported compression method");
	
      // Buffers used to hold the compressed bytes we receive and the
      // uncompressed data given to us by zlib.
      char *recv_buf = 0, *inflate_buf = 0;

      // Set up to use zlib inflation
      z_stream zstrm;
      zstrm.zalloc = Z_NULL;
      zstrm.zfree = Z_NULL;
      zstrm.opaque = Z_NULL;
      zstrm.avail_in = 0;
      zstrm.next_in = Z_NULL;
      if (inflateInit(&zstrm) != Z_OK)
	srpc->send_failure(SRPC::internal_trouble,
			   "zlib inflateInit failed");

      try
	{
	  srpc->recv_seq_start();
	  recv_buf = NEW_PTRFREE_ARRAY(char, readWhole_recv_bufsiz);
	  inflate_buf = NEW_PTRFREE_ARRAY(char, readWhole_inflate_bufsiz);
	  while(1)
	    {
	      bool got_end;
	      int count = readWhole_recv_bufsiz;
	      srpc->recv_bytes_here(recv_buf, count, &got_end);
	      // If this is the end of the sequence, we're done.
	      if(got_end) break;

	      // Feed the compressed bytes we received to zlib for
	      // inflation.
	      zstrm.next_in = (Bytef *) recv_buf;
	      zstrm.avail_in = count;
	      do
		{
		  // zlib will inflate into this buffer.
		  zstrm.avail_out = readWhole_inflate_bufsiz;
		  zstrm.next_out = (Bytef *) inflate_buf;

		  // Uncompress some of the bytes we received.
		  int inf_status = inflate(&zstrm, Z_NO_FLUSH);

		  // Check for an error from inflate
		  switch (inf_status)
		    {
		      // If inflate fails in any way, we throw SRPC
		      // failure with a message including the zlib
		      // error code
#define INFLATE_ECASE(val) case val:\
		  srpc->send_failure(SRPC::internal_trouble,\
				     "zlib inflate error: " #val)
		      INFLATE_ECASE(Z_STREAM_ERROR);
		      INFLATE_ECASE(Z_NEED_DICT);
		      INFLATE_ECASE(Z_DATA_ERROR);
		      INFLATE_ECASE(Z_MEM_ERROR);
		    }

		  // Count the number of uncompressed bytes produced
		  // in the output buffer.
		  int bytes = readWhole_inflate_bufsiz - zstrm.avail_out;
		  if(bytes > 0)
		    {
		      // Write the bytes to the output stream.
		      out.write(inflate_buf, bytes);
		      // If anything has gone wrong with writing,
		      // abort the transfer.
		      if(out.fail())
			{
			  srpc->send_failure(SRPC::internal_trouble,
					     "readWhole write error");
			}
		    }
		}
	      // Repeat until inflate can't fill the output buffer (which
	      // means it doesn't have any more uncompressed data).
	      while(zstrm.avail_out == 0);
	    }

	  // We've received all chunks of compressed bytes.
	  srpc->recv_seq_end();
	}
      catch(...)
	{
	  // Clean up in the event of an exception
	  (void)inflateEnd(&zstrm);
	  if(recv_buf) delete [] recv_buf;
	  if(inflate_buf) delete [] inflate_buf;
	  throw;
	}

      // Clean up in the event of an exception now that we're done
      // receiving compressed data.
      (void)inflateEnd(&zstrm);
      if(recv_buf) delete [] recv_buf;
      if(inflate_buf) delete [] inflate_buf;
    }

  // Make sure we flush any buffered writes and check for a failure
  // caused by that.
  out.flush();
  if(out.fail())
    {
      srpc->send_failure(SRPC::internal_trouble,
			 "readWhole write error (final flush)");
    }

  srpc->recv_end();
  VestaSourceSRPC::End(id);
  return err;
}

VestaSource *VDirSurrogate::copy() throw()
{
  VestaSource *result = NEW_CONSTR(VDirSurrogate, (*this));
  return result;
}
