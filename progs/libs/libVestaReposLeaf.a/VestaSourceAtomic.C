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
// VestaSourceAtomic.C
// Last modified on Fri Apr 22 13:23:19 EDT 2005 by ken@xorian.net  
//      modified on Thu Jul 19 15:36:15 PDT 2001 by mann  
//
// Facility for grouping several VestaSource actions into an atomic
// unit, from outside the repository address space.  The implementation
// shares SRPC objects with VDirSurrogate.C.
//

#include "SRPC.H"
#include "MultiSRPC.H"
#include "VestaSource.H"
#include "VestaSourceSRPC.H"
#include "VDirSurrogate.H"
#include "VestaSourceAtomic.H"

// Use the default repository
VestaSourceAtomic::VestaSourceAtomic(AccessControl::Identity who)
     throw (SRPC::failure)
{
  id = VestaSourceSRPC::Start(srpc);
  srpc->start_call(VestaSourceSRPC::Atomic,
		   VestaSourceSRPC::version);
  VestaSourceSRPC::send_identity(srpc, who);
  nsteps = 0;
  nobjects = 0;
  target1 = VestaSource::ok;
  target2 = VestaSource::ok;
  ndone = 0;
  lasterr = VestaSource::ok;
  okreplace = VestaSource::ok;
  names = NEW(NameSeq);
}

// Specify the repository by host and port
VestaSourceAtomic::VestaSourceAtomic(const Text& host, const Text& port,
				     AccessControl::Identity who)
     throw (SRPC::failure)
{
  id = VestaSourceSRPC::Start(srpc, host, port);
  srpc->start_call(VestaSourceSRPC::Atomic,
		   VestaSourceSRPC::version);
  VestaSourceSRPC::send_identity(srpc, who);
  nsteps = 0;
  nobjects = 0;
  target1 = VestaSource::ok;
  target2 = VestaSource::ok;
  ndone = 0;
  lasterr = VestaSource::ok;
  okreplace = VestaSource::ok;
  names = NEW(NameSeq);
}


// Free resources consumed by the program
VestaSourceAtomic::~VestaSourceAtomic() throw ()
{
  try {
    cancel();
  } catch (SRPC::failure) {
    // ignore
  }
}

//
// Run the program.
//
bool
VestaSourceAtomic::run()
     throw (SRPC::failure)
{
  assert(srpc != NULL);
  srpc->send_int(VestaSourceSRPC::AtomicRun);
  srpc->send_end();
  ndone = srpc->recv_int();
  lasterr = (VestaSource::errorCode) srpc->recv_int();
  okreplace = (VestaSource::errorCode) srpc->recv_int();
  bool ret = (bool) srpc->recv_int();
  srpc->recv_end();
  VestaSourceSRPC::End(id);
  srpc = NULL;
  return ret;
}


// 
// Choose not to run the program.  The resources associated with the
// program are freed and it cannot be executed again.  Called
// automatically by the destructor if run() has not yet been called.
//
void
VestaSourceAtomic::cancel() 
     throw (SRPC::failure)
{
  if (names) {
    delete names;
    names = NULL;
  }
  if (srpc) {
    srpc->send_int(VestaSourceSRPC::AtomicCancel);
    srpc->send_end();
    srpc->recv_end();
    VestaSourceSRPC::End(id);
    srpc = NULL;
  }
}


// Return a step name
Text
VestaSourceAtomic::name(int step) throw ()
{
  return names->get(step);
}

// Set the target error code(s).
void
VestaSourceAtomic::setTarget(const Text& stepname,
			     VestaSource::errorCode target1,
			     VestaSource::errorCode target2,
			     VestaSource::errorCode okreplace)
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::AtomicTarget);
  srpc->send_int((int) target1);
  srpc->send_int((int) target2);
  srpc->send_int((int) okreplace);
  this->target1 = target1;
  this->target2 = target2;
  this->okreplace = okreplace;
  nsteps++;
}


// Add a VestaSource to the list and return its index number.  The
// program will halt here with VestaSource::notFound if vs->longid
// is invalid.
VestaSourceAtomic::VSIndex
VestaSourceAtomic::decl(const Text& stepname, VestaSource* vs) 
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::AtomicDeclare);
  srpc->send_bytes((const char*) &vs->longid.value,
		   sizeof(vs->longid.value));
  nsteps++;
  return nobjects++;
}


void
VestaSourceAtomic::resync(const Text& stepname, VestaSourceAtomic::VSIndex vsi)
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::AtomicResync);
  srpc->send_int((int) vsi);
  nsteps++;
}


void
VestaSourceAtomic::setTimestamp(const Text& stepname,
				VestaSourceAtomic::VSIndex vsi, time_t ts) 
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::SetTimestamp);
  srpc->send_int((int) vsi);
  srpc->send_int((int) ts);
  nsteps++;
}


VestaSourceAtomic::VSIndex
VestaSourceAtomic::lookup(const Text& stepname, VestaSourceAtomic::VSIndex vsi,
			  const Text& arc) 
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::Lookup);
  srpc->send_int((int) vsi);
  srpc->send_chars(arc.cchars());
  nsteps++;
  return nobjects++;
}


VestaSourceAtomic::VSIndex
VestaSourceAtomic::lookupPathname(const Text& stepname,
				  VestaSourceAtomic::VSIndex vsi,
				  const Text& pathname,
				  char pathnameSep) 
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::LookupPathname);
  srpc->send_int((int) vsi);
  srpc->send_chars(pathname.cchars());
  srpc->send_int((int)pathnameSep);
  nsteps++;
  return nobjects++;
}


VestaSourceAtomic::VSIndex
VestaSourceAtomic::lookupIndex(const Text& stepname,
			       VestaSourceAtomic::VSIndex vsi,
			       unsigned int index) 
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::LookupIndex);
  srpc->send_int((int) vsi);
  srpc->send_int((int) index);
  nsteps++;
  return nobjects++;
}


void
VestaSourceAtomic::reallyDelete(const Text& stepname,
				VestaSourceAtomic::VSIndex vsi,
				const Text& arc,
				bool existCheck, time_t timestamp) 
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::ReallyDelete);
  srpc->send_int((int) vsi);
  srpc->send_chars(arc.cchars());
  srpc->send_int((int) existCheck);
  srpc->send_int((int) timestamp);
  nsteps++;
}


VestaSourceAtomic::VSIndex
VestaSourceAtomic::insertFile(const Text& stepname,
			      VestaSourceAtomic::VSIndex vsi,
			      const Text& arc,
			      ShortId sid, bool master,
			      VestaSource::dupeCheck chk,
			      time_t timestamp, FP::Tag* fptag) 
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::InsertFile);
  srpc->send_int((int) vsi);
  srpc->send_chars(arc.cchars());
  srpc->send_int((int) sid);
  srpc->send_int((int) master);
  srpc->send_int((int) chk);
  srpc->send_int((int) timestamp);
  if (fptag == NULL) {
    srpc->send_bytes(NULL, 0);
  } else {
    fptag->Send(*srpc);
  }
  nsteps++;
  return nobjects++;
}


VestaSourceAtomic::VSIndex
VestaSourceAtomic::insertMutableFile(const Text& stepname,
				     VestaSourceAtomic::VSIndex vsi,
				     const Text& arc, ShortId sid, bool master,
				     VestaSource::dupeCheck chk,
				     time_t timestamp, FP::Tag* fptag)
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::InsertMutableFile);
  srpc->send_int((int) vsi);
  srpc->send_chars(arc.cchars());
  srpc->send_int((int) sid);
  srpc->send_int((int) master);
  srpc->send_int((int) chk);
  srpc->send_int((int) timestamp);
  nsteps++;
  return nobjects++;
}


// If dir is to be NULL in the method, use -1 as diri.
VestaSourceAtomic::VSIndex
VestaSourceAtomic::insertImmutableDirectory(const Text& stepname,
					    VestaSourceAtomic::VSIndex parenti,
					    const Text& arc,
					    VestaSourceAtomic::VSIndex diri,
					    bool master,
					    VestaSource::dupeCheck chk,
					    time_t timestamp, FP::Tag* fptag)
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::InsertImmutableDirectory);
  srpc->send_int((int) parenti);
  srpc->send_chars(arc.cchars());
  srpc->send_int((int) diri);
  srpc->send_int((int) master);
  srpc->send_int((int) chk);
  srpc->send_int((int) timestamp);
  if (fptag == NULL) {
    srpc->send_bytes(NULL, 0);
  } else {
    fptag->Send(*srpc);
  }
  nsteps++;
  return nobjects++;
}

VestaSourceAtomic::VSIndex
VestaSourceAtomic::insertAppendableDirectory(const Text& stepname,
					     VestaSourceAtomic::VSIndex vsi,
					     const Text& arc, bool master,
					     VestaSource::dupeCheck chk,
					     time_t timestamp)
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::InsertAppendableDirectory);
  srpc->send_int((int) vsi);
  srpc->send_chars(arc.cchars());
  srpc->send_int((int) master);
  srpc->send_int((int) chk);
  srpc->send_int((int) timestamp);
  nsteps++;
  return nobjects++;
}


// If dir is to be NULL in the method, use -1 as diri.
VestaSourceAtomic::VSIndex
VestaSourceAtomic::insertMutableDirectory(const Text& stepname,
					  VestaSourceAtomic::VSIndex parenti,
					  const Text& arc,
					  VestaSourceAtomic::VSIndex diri,
					  bool master,
					  VestaSource::dupeCheck chk,
					  time_t timestamp)
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::InsertMutableDirectory);
  srpc->send_int((int) parenti);
  srpc->send_chars(arc.cchars());
  srpc->send_int((int) diri);
  srpc->send_int((int) master);
  srpc->send_int((int) chk);
  srpc->send_int((int) timestamp);
  nsteps++;
  return nobjects++;
}


VestaSourceAtomic::VSIndex
VestaSourceAtomic::insertGhost(const Text& stepname,
			       VestaSourceAtomic::VSIndex vsi,
			       const Text& arc,
			       bool master, VestaSource::dupeCheck chk,
			       time_t timestamp)
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::InsertGhost);
  srpc->send_int((int) vsi);
  srpc->send_chars(arc.cchars());
  srpc->send_int((int) master);
  srpc->send_int((int) chk);
  srpc->send_int((int) timestamp);
  nsteps++;
  return nobjects++;
}


VestaSourceAtomic::VSIndex
VestaSourceAtomic::insertStub(const Text& stepname,
			      VestaSourceAtomic::VSIndex vsi,
			      const Text& arc,
			      bool master, VestaSource::dupeCheck chk,
			      time_t timestamp)
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::InsertStub);
  srpc->send_int((int) vsi);
  srpc->send_chars(arc.cchars());
  srpc->send_int((int) master);
  srpc->send_int((int) chk);
  srpc->send_int((int) timestamp);
  nsteps++;
  return nobjects++;
}


void
VestaSourceAtomic::renameTo(const Text& stepname,
			    VestaSourceAtomic::VSIndex vsi,
			    const Text& arc,
			    VestaSourceAtomic::VSIndex fromDirI,
			    const Text& fromArc,
			    VestaSource::dupeCheck chk, time_t timestamp) 
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::RenameTo);
  srpc->send_int((int) vsi);
  srpc->send_chars(arc.cchars());
  srpc->send_int((int) fromDirI);
  srpc->send_chars(fromArc.cchars());
  srpc->send_int((int) chk);
  srpc->send_int((int) timestamp);
  nsteps++;
}


void
VestaSourceAtomic::makeFilesImmutable(const Text& stepname,
				      VestaSourceAtomic::VSIndex vsi,
				      unsigned int threshold) 
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::MakeFilesImmutable);
  srpc->send_int((int) vsi);
  srpc->send_int((int) threshold);
  nsteps++;
}


void
VestaSourceAtomic::testMaster(const Text& stepname,
			      VestaSourceAtomic::VSIndex vsi, bool state,
			      VestaSource::errorCode err)
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::AtomicTestMaster);
  srpc->send_int((int) vsi);
  srpc->send_int((int) state);
  srpc->send_int((int) err);
  nsteps++;
}

void
VestaSourceAtomic::setMaster(const Text& stepname,
			     VestaSourceAtomic::VSIndex vsi, bool state) 
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::AtomicSetMaster);
  srpc->send_int((int) vsi);
  srpc->send_int((int) state);
  nsteps++;
}


void
VestaSourceAtomic::inAttribs(const Text& stepname,
			     VestaSourceAtomic::VSIndex vsi,
			     const Text& name, const Text& value,
			     bool expected, VestaSource::errorCode err) 
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::InAttribs);
  srpc->send_int((int) vsi);
  srpc->send_chars(name.cchars());
  srpc->send_chars(value.cchars());
  srpc->send_int((int) expected);
  srpc->send_int((int) err);
  nsteps++;
}


void
VestaSourceAtomic::writeAttrib(const Text& stepname,
			       VestaSourceAtomic::VSIndex vsi,
			       VestaSource::attribOp op,
			       const Text& name, const Text& value,
			       time_t timestamp)
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::WriteAttrib);
  srpc->send_int((int) vsi);
  srpc->send_int((int) op);
  srpc->send_chars(name.cchars());
  srpc->send_chars(value.cchars());
  srpc->send_int((int) timestamp);
  nsteps++;
}

void
VestaSourceAtomic::mergeAttrib(const Text& stepname,
			       VestaSourceAtomic::VSIndex fromvsi,
			       VestaSourceAtomic::VSIndex tovsi,
			       const Text& name,
			       time_t timestamp)
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::AtomicMergeAttrib);
  srpc->send_int((int) fromvsi);
  srpc->send_int((int) tovsi);
  srpc->send_chars(name.cchars());
  srpc->send_int((int) timestamp);
  nsteps++;
}

void
VestaSourceAtomic::accessCheck(const Text& stepname,
			       VestaSourceAtomic::VSIndex vsi,
			       AccessControl::Class cls, bool expected,
			       VestaSource::errorCode err)
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::AtomicAccessCheck);
  srpc->send_int((int) vsi);
  srpc->send_int((int) cls);
  srpc->send_int((int) expected);
  srpc->send_int((int) err);
  nsteps++;
}

void 
VestaSourceAtomic::typeCheck(const Text& stepname,
			     VSIndex vsi, unsigned int allowed,
			     VestaSource::errorCode err)
     throw (SRPC::failure)
{
  names->addhi(stepname);
  srpc->send_int(VestaSourceSRPC::AtomicTypeCheck);
  srpc->send_int((int) vsi);
  srpc->send_int((int) allowed);
  srpc->send_int((int) err);
  nsteps++;
}

