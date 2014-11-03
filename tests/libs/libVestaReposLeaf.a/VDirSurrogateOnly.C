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
// VDirSurrogateOnly.C
//
// Methods used when you are definitely not inside any repository.
//

#include <pthread.h>
#include "VestaSource.H"
#include "VDirSurrogate.H"
#include "MultiSRPC.H"
#include "VestaSourceSRPC.H"

static pthread_once_t once2 = PTHREAD_ONCE_INIT;
static VestaSource* repositoryRoot; // the /vesta directory (default repos)
static VestaSource* mutableRoot;    // directory of mutable directories (")
static VestaSource* volatileRoot;   // directory of volatile directories (")

extern "C"
{
  void 
  VDirSurrogate_init2()
  {
    ::repositoryRoot = NEW_CONSTR(VDirSurrogate,
				  (2, NullShortId,
				   VDirSurrogate::defaultHost(),
				   VDirSurrogate::defaultPort()));
    ::repositoryRoot->longid = RootLongId;
    ::repositoryRoot->resync();
    ::mutableRoot = NEW_CONSTR(VDirSurrogate,
			       (2, NullShortId,
				VDirSurrogate::defaultHost(),
				VDirSurrogate::defaultPort()));
    ::mutableRoot->longid = MutableRootLongId;
    ::mutableRoot->resync();
    ::volatileRoot = NEW_CONSTR(VDirSurrogate,
				(2, NullShortId,
				 VDirSurrogate::defaultHost(),
				 VDirSurrogate::defaultPort()));
    ::volatileRoot->longid = VolatileRootLongId;
    ::volatileRoot->resync();
  }
}

VestaSource*
LongId::lookup(lockKindTag lockKind, ReadersWritersLock** lock)
     throw (SRPC::failure)
{
    assert(lockKind == LongId::noLock);
    assert(lock == NULL);
    return VDirSurrogate::LongIdLookup(*this, VDirSurrogate::defaultHost(),
				       VDirSurrogate::defaultPort());
}

bool
LongId::valid()
     throw (SRPC::failure)
{
    return VDirSurrogate::LongIdValid(*this, VDirSurrogate::defaultHost(),
				       VDirSurrogate::defaultPort());
}

VestaSource*
VestaSource::repositoryRoot(LongId::lockKindTag lockKind,
			    ReadersWritersLock** lock)
     throw (SRPC::failure)
{
    assert(lockKind == LongId::noLock);
    assert(lock == NULL);
    pthread_once(&once2, VDirSurrogate_init2);
    return ::repositoryRoot;
}

VestaSource*
VestaSource::mutableRoot(LongId::lockKindTag lockKind,
			 ReadersWritersLock** lock)
     throw (SRPC::failure)
{
    assert(lockKind == LongId::noLock);
    assert(lock == NULL);
    pthread_once(&once2, VDirSurrogate_init2);
    return ::mutableRoot;
}

VestaSource*
VestaSource::volatileRoot(LongId::lockKindTag lockKind,
			  ReadersWritersLock** lock)
     throw (SRPC::failure)
{
    assert(lockKind == LongId::noLock);
    assert(lock == NULL);
    pthread_once(&once2, VDirSurrogate_init2);
    return ::volatileRoot;
}

VestaSource::errorCode 
VestaSource::lookupPathname(const char* pathname, VestaSource*& result,
			    AccessControl::Identity who, char pathnameSep)
     throw (SRPC::failure)
{
    assert(false);
    return VestaSource::inappropriateOp;
}

VestaSource::errorCode 
VestaSource::makeMutable(VestaSource*& result, ShortId sid,
			 Basics::uint64 copyMax, AccessControl::Identity who)
     throw (SRPC::failure)
{
    assert(false);
    return VestaSource::inappropriateOp;
}

VestaSource::errorCode 
VestaSource::copyToMutable(VestaSource*& result,
			   AccessControl::Identity who)
     throw (SRPC::failure)
{
    assert(false);
    return VestaSource::inappropriateOp;
}

VestaSource::errorCode
VestaSource::makeFilesImmutable(unsigned int threshold,
				AccessControl::Identity who)
     throw (SRPC::failure)
{
    assert(false);
    return VestaSource::inappropriateOp;
}

VestaSource* VestaSource::getParent() throw (SRPC::failure)
{
  LongId parent_lid = this->longid.getParent();
  if(!(parent_lid == NullLongId)) 
    return VDirSurrogate::LongIdLookup(parent_lid, this->host(), this->port());
  return NULL;    
}

bool
VestaAttribs::inAttribs(const char* name, const char* value)
     throw (SRPC::failure)
{
    assert(false);
    return false;
}

char*
VestaAttribs::getAttrib(const char* name)
     throw (SRPC::failure)
{
    assert(false);
    return NULL;
}

void
VestaAttribs::getAttrib(const char* name,
		       VestaSource::valueCallback cb, void* cl)
{
    assert(false);
}

void
VestaAttribs::listAttribs(VestaSource::valueCallback cb, void* cl)
{
    assert(false);
}

void
VestaAttribs::getAttribHistory(VestaSource::historyCallback cb, void* cl)
{
    assert(false);
}

VestaSource::errorCode
VestaAttribs::writeAttrib(VestaSource::attribOp op,
			  const char* name, const char* value,
			  time_t &timestamp) throw ()
{
    assert(false);
    return VestaSource::inappropriateOp;
}

VestaSource::errorCode
VestaSource::writeAttrib(VestaSource::attribOp op,
			 const char* name, const char* value,
			 AccessControl::Identity who, time_t timestamp)
     throw (SRPC::failure)
{
    assert(false);
    return VestaSource::inappropriateOp;
}

VestaSource::errorCode
VestaSource::cedeMastership(const char* requestid, const char** grantidOut,
			    AccessControl::Identity who) throw (SRPC::failure)
{
    assert(false);
    return VestaSource::inappropriateOp;
}

const char*
AccessControl::GlobalIdentityRep::user(int n) throw ()
{
  if (n == 0) {
    return user_;
  } else {
    // This is kind of a fib; on the server side, we might acquire
    // more user names from the alias table.
    return NULL;
  }
}

const char*
AccessControl::GlobalIdentityRep::group(int n) throw ()
{
  assert("server side only" && false);
  return NULL;
}

void AccessControl::GlobalIdentityRep::fill_caches() throw()
{
  assert("server side only" && false);
}

uid_t
AccessControl::globalToUnixUser(const char* name) throw ()
{
  assert("server side only" && false);
  return (uid_t) -1;
}

gid_t
AccessControl::globalToUnixGroup(const char* name) throw ()
{
  assert("server side only" && false);
  return (gid_t) -1;
}

const char*
AccessControl::UnixIdentityRep::user(int n) throw ()
{
  assert("server side only" && false);
  return NULL;
}

const char*
AccessControl::UnixIdentityRep::group(int n) throw ()
{
  assert("server side only" && false);
  return NULL;
}

void AccessControl::UnixIdentityRep::fill_caches() throw()
{
  assert("server side only" && false);
}
