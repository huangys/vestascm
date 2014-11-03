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
// VestaSourceServer.C
//
// Server stubs for VestaSourceSRPC interface
// 

#include <time.h>
#include <assert.h>
#include <zlib.h> // Used for ReadWholeCompressed, which sends
		  // compressed data to the client
#include <SRPC.H>
#include <Thread.H>
#include <LimService.H>
#include <OS.H>
#include "VestaSourceSRPC.H"
#include "VestaSource.H"
#include "VDirVolatileRoot.H"
#include "VRConcurrency.H"
#include "VestaConfig.H"
#include "VestaSourceServer.H"
#include "FPShortId.H"
#include "logging.H"
#include "VestaSourceImpl.H"
#include "Mastership.H"
#include "Replication.H"
#include "ReposStats.H"
#include "FdCache.H"
#include "dupe.H"
#include "VMemPool.H"

#include "lock_timing.H"

#include "nfsStats.H"

#include "ReposConfig.h"

#if defined(REPOS_PERF_DEBUG)
#include "timing.H"
#include "perfDebugControl.H"
#endif

#if defined(__linux__) && defined(__GNUC__) && (__GNUC__ < 3)
// For some reason pread doesn't get declared on older systems
extern "C" ssize_t pread(int fd, void *buf, size_t count, off_t offset);
#endif

#define DEF_MAX_RUNNING 32
#define STACK_SIZE 256000L

// ----------------------------------------------------------------------
// SRPC call statistic helper code
// ----------------------------------------------------------------------

extern "C"
{
  void SRPC_Call_Stats_Helper_stats_init() throw();
}

// An instance of this class gets created per SRPC servicing thread.
// It records the number of calls and total time spent servicing all
// calls over the lifetime of the thread.
class SRPC_Call_Stats
{
private:
  // Single linked list of all instances of this class.
  static Basics::mutex head_mu;
  static SRPC_Call_Stats *head;
  SRPC_Call_Stats *next;

  // Protext local fields.
  Basics::mutex mu;

  // Statistics for one SRPC-servicing thread.
  Basics::uint64 call_count;
  Basics::uint64 elapsed_secs;
  Basics::uint32 elapsed_usecs;

  // Add the statistics from this instance into a running total.
  void accumulateStats(/*OUT*/ Basics::uint64 &calls,
		       /*OUT*/ Basics::uint64 &secs,
		       /*OUT*/ Basics::uint32 &usecs);

  // Record an SRPC call.
  void recordCall(Basics::uint32 secs, Basics::uint32 &usecs);

public:

  SRPC_Call_Stats();
  ~SRPC_Call_Stats();

  static void getStats(/*OUT*/ Basics::uint64 &calls,
		       /*OUT*/ Basics::uint64 &secs,
		       /*OUT*/ Basics::uint32 &usecs);

  // Class whose creation and destruction mark the beginning and end
  // of a call.
  class Helper
  {
  private:
    static pthread_key_t stats_key;
    static pthread_once_t stats_once;

    friend void SRPC_Call_Stats_Helper_stats_init() throw();

    struct timeval call_start;
  public:
    Helper();
    ~Helper();
  };

  friend class Helper;
};

Basics::mutex SRPC_Call_Stats::head_mu;
SRPC_Call_Stats *SRPC_Call_Stats::head = 0;

pthread_key_t SRPC_Call_Stats::Helper::stats_key;
pthread_once_t SRPC_Call_Stats::Helper::stats_once = PTHREAD_ONCE_INIT;

void SRPC_Call_Stats::accumulateStats(/*OUT*/ Basics::uint64 &calls,
				      /*OUT*/ Basics::uint64 &secs,
				      /*OUT*/ Basics::uint32 &usecs)
{
  this->mu.lock();
  calls += this->call_count;
  secs += this->elapsed_secs;
  usecs += this->elapsed_usecs;
  this->mu.unlock();
}

void SRPC_Call_Stats::recordCall(Basics::uint32 secs, Basics::uint32 &usecs)
{
  this->mu.lock();
  // Record that another call has been completed.
  this->call_count++;

  // Add the time for this call to our running total.
  this->elapsed_secs += secs;
  this->elapsed_usecs += usecs;
  if(this->elapsed_usecs > USECS_PER_SEC)
    {
      this->elapsed_secs += this->elapsed_usecs/USECS_PER_SEC;
      this->elapsed_usecs %= USECS_PER_SEC;
    }
  this->mu.unlock();
}

void SRPC_Call_Stats::getStats(/*OUT*/ Basics::uint64 &calls,
			       /*OUT*/ Basics::uint64 &secs,
			       /*OUT*/ Basics::uint32 &usecs)
{
  // Reset our parameters.
  calls = 0;
  secs = 0;
  usecs = 0;

  // Get the current list of instances
  SRPC_Call_Stats *list;
  SRPC_Call_Stats::head_mu.lock();
  list = SRPC_Call_Stats::head;
  SRPC_Call_Stats::head_mu.unlock();

  while(list != 0)
    {
      // Accumulate into our parameters
      list->accumulateStats(calls, secs, usecs);

      // Handle usecs overflow
      if(usecs > USECS_PER_SEC)
	{
	  secs += usecs/USECS_PER_SEC;
	  usecs %= USECS_PER_SEC;
	}

      // Advance to the next instance.  (Note: no lock required to
      // access this member variable as it's set at instantiation and
      // never changed.)
      list = list->next;
    }
}

SRPC_Call_Stats::SRPC_Call_Stats()
  : next(0), call_count(0), elapsed_secs(0), elapsed_usecs(0)
{
  // Insert ourselves into the global list
  SRPC_Call_Stats::head_mu.lock();
  this->next = SRPC_Call_Stats::head;
  SRPC_Call_Stats::head = this;
  SRPC_Call_Stats::head_mu.unlock();
}

SRPC_Call_Stats::~SRPC_Call_Stats()
{
  // We don't ever expect to be destroyed.
  assert(false);
}

extern "C"
{
  void SRPC_Call_Stats_Helper_stats_init() throw()
  {
    int err = pthread_key_create(&SRPC_Call_Stats::Helper::stats_key, NULL);
    assert(err == 0);
  }
}

SRPC_Call_Stats::Helper::Helper()
{
  // Save the time that this call started
  struct timezone unused_tz;
  int err = gettimeofday(&this->call_start, &unused_tz);
  assert(err == 0);
}

SRPC_Call_Stats::Helper::~Helper()
{
  // Get the time that this call ended
  struct timezone unused_tz;
  struct timeval call_end;
  int err = gettimeofday(&call_end, &unused_tz);
  assert(err == 0);

  // Make sure the pthread_key is initialized
  pthread_once(&SRPC_Call_Stats::Helper::stats_once,
	       SRPC_Call_Stats_Helper_stats_init);

  // Get or create the object that holds statistics for this SRPC
  // thread.
  SRPC_Call_Stats *thread_stats =
    ((SRPC_Call_Stats *)
     pthread_getspecific(SRPC_Call_Stats::Helper::stats_key));
  if(thread_stats == 0)
    {
      thread_stats = NEW(SRPC_Call_Stats);

      err = pthread_setspecific(SRPC_Call_Stats::Helper::stats_key,
				(void *) thread_stats);
      assert(err == 0);
    }

  // Compute the time taken by this call.
  Basics::uint32 call_secs, call_usecs;
  if(call_end.tv_sec == this->call_start.tv_sec)
    {
      call_secs = 0;
      if(call_end.tv_usec >= this->call_start.tv_usec)
	call_usecs = call_end.tv_usec - this->call_start.tv_usec;
      else
	// Time went backwards.  This can happen if the system time
	// gets adjusted.  Count this call as having taken 0 time.
	call_usecs = 0;
    }
  else if(call_end.tv_sec > this->call_start.tv_sec)
    {
      call_secs = (call_end.tv_sec - this->call_start.tv_sec) - 1;
      call_usecs = ((call_end.tv_usec + USECS_PER_SEC) -
		    this->call_start.tv_usec);
    }
  else
    {
      // Time went backwards.  This can happen if the system time gets
      // adjusted.  Count this call as having taken 0 time.
      call_secs = 0;
      call_usecs = 0;
    }

  // Record the time for this call in the stats recorder.
  thread_stats->recordCall(call_secs, call_usecs);
}

// ----------------------------------------------------------------------

// By default we give a client a maximum of five minutes to complete a
// call they make.  Of course normally it should be much less than
// this.  The limit is there just to prevent a suspended or
// misbehaving client from causing a server thread to be waiting
// indefinitely.
static unsigned int readTimeout = 300;

#ifndef NGRPS
// Some platforms don't define this macro.  An authunix_parms is
// supposed to max out at 16 supplementary gorups when sent over the
// network.
#define NGRPS 16
#endif

// Unmarshal AccessControl::Identity
AccessControl::Identity
srpc_recv_identity(SRPC* srpc, int intf_ver, bool access_needed)
{
  AccessControl::Identity ret = 0;
  sockaddr_in addr;
  srpc->socket()->get_remote_addr(addr);

  AccessControl::IdentityRep::Flavor flavor;
  if (intf_ver <= 10) {
    flavor = AccessControl::IdentityRep::unix_flavor;
  } else {
    flavor = (AccessControl::IdentityRep::Flavor) srpc->recv_int();
  }
  switch (flavor) {
  case AccessControl::IdentityRep::unix_flavor: {
    authunix_parms* aup = NEW(authunix_parms);
    aup->aup_time = (u_int) srpc->recv_int();
    aup->aup_machname = NEW_PTRFREE_ARRAY(char, MAX_MACHINE_NAME);
    int len = MAX_MACHINE_NAME;
    srpc->recv_chars_here(aup->aup_machname, len);
    aup->aup_uid = srpc->recv_int();
    aup->aup_gid = srpc->recv_int();
    srpc->recv_seq_start((int*) &aup->aup_len);
    if (aup->aup_len > NGRPS)
      srpc->send_failure(SRPC::buffer_too_small, "too many GIDs");
#if defined(AUTHUNIX_PARMS_GID_T_GID)
    aup->aup_gids = NEW_PTRFREE_ARRAY(gid_t, NGRPS);
#elif defined(AUTHUNIX_PARMS_INT_GID)
    aup->aup_gids = NEW_PTRFREE_ARRAY(int, NGRPS);
#else
#error Unknown type for gid field of authunix_parms
#endif
    unsigned int i;
    for (i=0; i<aup->aup_len; i++) {
	aup->aup_gids[i] = srpc->recv_int();
    }
    srpc->recv_seq_end();
    ret = NEW_CONSTR(AccessControl::UnixIdentityRep, (aup, &addr));
    break; }

  case AccessControl::IdentityRep::global: {
    char* user = srpc->recv_chars();
    ret = NEW_CONSTR(AccessControl::GlobalIdentityRep, (user, &addr));
    break; }

  case AccessControl::IdentityRep::gssapi: {
    srpc->send_failure(SRPC::protocol_violation,
		       "gssapi identity flavor not implemented yet");
    break; }

  default:
    srpc->send_failure(SRPC::protocol_violation, "unknown identity flavor");
  }
  if (access_needed && !AccessControl::admit(ret)) {
    // Caller is not allowed access to this repository
    delete ret;
    srpc->send_failure(SRPC::invalid_parameter, "unauthorized user");
  }
  return ret;
}

// Server stub for Lookup
static void
VestaSourceLookup(SRPC* srpc, int intf_ver)
{
    LongId longid;
    char arc[MAX_ARC_LEN+1];
    int len;
    VestaSource::errorCode err;

    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(arc, len);
    AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::readLock, &lock);
    try {
	if (vs == NULL) {
	    err = VestaSource::invalidArgs;
	} else {
	  RWLOCK_LOCKED_REASON(lock, "SRPC:lookup");
	    err = VestaSource::ok;
	
	    if (len > 0) {
		VestaSource* childvs;
		err = vs->lookup(arc, childvs, who);
		delete vs;
		vs = childvs;
	    }
	}
    
	// Send the results
	srpc->send_int((int) err);
	if (err == VestaSource::ok) {
	    srpc->send_int((int) vs->type);
	    srpc->send_bytes((const char *) &vs->longid, sizeof(vs->longid));
	    srpc->send_int((int) vs->master);
	    srpc->send_int((int) vs->pseudoInode);
	    srpc->send_int((int) vs->shortId());
	    srpc->send_int((int) vs->timestamp());
	    srpc->send_int((int) (vs->hasAttribs() ||
				  MutableRootLongId.isAncestorOf(vs->longid)));
	    vs->fptag.Send(*srpc);
	    delete vs;
	}
    } catch (SRPC::failure f) {
	if (lock != NULL) lock->releaseRead();
	delete who;
	throw;
    }
    if (lock != NULL) lock->releaseRead();
    delete who;
    srpc->send_end();
}


// Server stub for LookupPathname
static void
VestaSourceLookupPathname(SRPC* srpc, int intf_ver)
{
  LongId longid;
  char* pathname;
  char pathnameSep;
  VestaSource::errorCode err;
  int len;
    
  // Receive the arguments
  len = sizeof(longid.value);
  srpc->recv_bytes_here((char *) &longid.value, len);
  pathname = srpc->recv_chars();
  AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
  pathnameSep = (char) srpc->recv_int();
  srpc->recv_end();
    
  // Do the work
  ReadersWritersLock* lock;
  VestaSource* vs = longid.lookup(LongId::readLock, &lock);
  try {
    if (vs == NULL) {
      err = VestaSource::invalidArgs;
    } else {
      RWLOCK_LOCKED_REASON(lock, "SRPC:lookupPathname");
      VestaSource* childvs;
      err = vs->lookupPathname(pathname, childvs, who, pathnameSep);
      delete vs;
      vs = childvs;
    }
    
    // Send the results
    srpc->send_int((int) err);
    if (err == VestaSource::ok) {
      srpc->send_int((int) vs->type);
      srpc->send_bytes((const char *) &vs->longid, sizeof(vs->longid));
      srpc->send_int((int) vs->master);
      srpc->send_int((int) vs->pseudoInode);
      srpc->send_int((int) vs->shortId());
      srpc->send_int((int) vs->timestamp());
      srpc->send_int((int) (vs->hasAttribs() ||
			    MutableRootLongId.isAncestorOf(vs->longid)));
      vs->fptag.Send(*srpc);
      delete vs;
    }
  } catch (SRPC::failure f) {
    if (lock != NULL) lock->releaseRead();
    delete [] pathname;
    delete who;
    throw;    
  }
  if (lock != NULL) lock->releaseRead();
  delete [] pathname;
  delete who;
  srpc->send_end();
}


// Server stub for LookupIndex
static void
VestaSourceLookupIndex(SRPC* srpc, int intf_ver)
{
    LongId longid;
    unsigned int index;
    VestaSource::errorCode err;
    int len;
    char arcbuf[MAX_ARC_LEN+1];
    bool sendarc;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    index = (unsigned int) srpc->recv_int();
    sendarc = (bool) srpc->recv_int();
    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::readLock, &lock);
    try {
	if (vs == NULL) {
	    err = VestaSource::invalidArgs;
	} else {
	  RWLOCK_LOCKED_REASON(lock, "SRPC:lookupIndex");
	    VestaSource* childvs;
	    err = vs->lookupIndex(index, childvs, sendarc ? arcbuf : NULL);
	    delete vs;
	    vs = childvs;
	}
    
	// Send the results
	srpc->send_int((int) err);
	if (err == VestaSource::ok) {
	    srpc->send_int((int) vs->type);
	    srpc->send_bytes((const char *) &vs->longid, sizeof(vs->longid));
	    srpc->send_int((int) vs->master);
	    srpc->send_int((int) vs->pseudoInode);
	    srpc->send_int((int) vs->shortId());
	    srpc->send_int((int) vs->timestamp());
	    srpc->send_int((int) (vs->hasAttribs() ||
				  MutableRootLongId.isAncestorOf(vs->longid)));
	    vs->fptag.Send(*srpc);
	    delete vs;
	    if (sendarc) srpc->send_chars(arcbuf);
	}
    } catch (SRPC::failure f) {
	if (lock != NULL) lock->releaseRead();
	throw;
    } 
    if (lock != NULL) lock->releaseRead();
    srpc->send_end();
}


// Server stub for CreateVolatileDirectory
static void
VestaSourceCreateVolatileDirectory(SRPC* srpc, int intf_ver)
{
    char host[MAX_ARC_LEN+1];
    char port[MAX_ARC_LEN+1];
    int len;
    VestaSource::errorCode err;
    Bit64 handle;
    time_t timestamp;
    bool readOnlyExisting;

    // Receive the arguments
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(host, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(port, len);
    len = sizeof(handle);
    srpc->recv_bytes_here((char*) &handle, len);
    timestamp = (time_t) srpc->recv_int();
    readOnlyExisting = (bool) srpc->recv_int();
    srpc->recv_end();
    
    // Do the work
    VestaSource* vs;
    ReadersWritersLock* lock;
    err = ((VDirVolatileRoot*) VestaSource::volatileRoot())->
      createVolatileDirectory(host, port, handle, vs, timestamp,
			      LongId::readLock, &lock, readOnlyExisting);
    try {
	// Send the results
	srpc->send_int((int) err);
	if (err == VestaSource::ok) {
	    srpc->send_int((int) vs->type);
	    srpc->send_bytes((const char *) &vs->longid, sizeof(vs->longid));
	    srpc->send_int((int) vs->master);
	    srpc->send_int((int) vs->pseudoInode);
	    srpc->send_int((int) vs->shortId());
	    srpc->send_int((int) vs->timestamp());
	    srpc->send_int((int) vs->hasAttribs());
	    vs->fptag.Send(*srpc);
	    delete vs;
	}
    } catch (SRPC::failure f) {
	if (lock != NULL) lock->releaseRead();
	throw;
    } 
    if (lock != NULL) lock->releaseRead();
    srpc->send_end();
}

// Server stub for DeleteVolatileDirectory
static void
VestaSourceDeleteVolatileDirectory(SRPC* srpc, int intf_ver)
{
    LongId longid, plongid;
    int len;
    VestaSource::errorCode err;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    srpc->recv_end();
    
    // Do the work
    unsigned int index;
    plongid = longid.getParent(&index);
    if (memcmp(&plongid, &VolatileRootLongId, sizeof(LongId)) != 0) {
	err = VestaSource::inappropriateOp; // client coding error!
    } else {
	ReadersWritersLock *lock;
	err = ((VDirVolatileRoot*)
	       VestaSource::volatileRoot(LongId::writeLock, &lock))->
		 deleteIndex(index);
	RWLOCK_LOCKED_REASON(lock, "SRPC:DeleteVolatileDirectory");
	lock->releaseWrite();
    }
    
    // Send the results
    srpc->send_int((int) err);
    srpc->send_end();
}

// Server stub for getBase
static void
VestaSourceGetBase(SRPC* srpc, int intf_ver)
{
  LongId longid;	
  int len;
  VestaSource::errorCode err;

  // Receive the arguments
  len = sizeof(longid.value);
  srpc->recv_bytes_here((char *) &longid.value, len);
  AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
  srpc->recv_end();
    
  // Do the work
  ReadersWritersLock* lock;
  VestaSource* vs = longid.lookup(LongId::readLock, &lock);
  try {
    VestaSource* mvs;
    if (vs == NULL) {
      err = VestaSource::invalidArgs;
    } else {
      RWLOCK_LOCKED_REASON(lock, "SRPC:GetBase");
      err = vs->getBase(mvs, who);
      delete vs;
    }
    // Send the results
    srpc->send_int((int) err);
    if (err == VestaSource::ok) {
      srpc->send_int((int) mvs->type);
      srpc->send_bytes((const char *) &mvs->longid, sizeof(mvs->longid));
      srpc->send_int((int) mvs->master);
      srpc->send_int((int) mvs->pseudoInode);
      srpc->send_int((int) mvs->shortId());
      srpc->send_int((int) mvs->timestamp());
      srpc->send_int((int) mvs->hasAttribs());
      mvs->fptag.Send(*srpc);
      delete mvs;
    }
  } catch (SRPC::failure f) {
    if (lock != NULL) lock->releaseRead();
    delete who;
    throw;
  }
  if (lock != NULL) lock->releaseRead();
  delete who;
  srpc->send_end();
}


struct VSLClosure {
    SRPC* srpc;
    int cost, limit, overhead;
    int intf_ver;
};

// Internal callback for List stub below
static bool
VSLCallback(void* closure, VestaSource::typeTag type, Arc arc,
	    unsigned int index, Bit32 pseudoInode, ShortId filsid, bool master)
{
    VSLClosure* cl = (VSLClosure*) closure;
    cl->cost += cl->overhead + strlen(arc);
    if (cl->cost > cl->limit) return false; // no room for this entry
    cl->srpc->send_chars(arc);
    cl->srpc->send_int((int) type);
    cl->srpc->send_int((int) index);
    if (cl->intf_ver >= 10) {
      cl->srpc->send_int((int) pseudoInode);
      cl->srpc->send_int((int) filsid);
    } else {
      // Rough backward compatibility
      cl->srpc->send_int(((int)filsid) ? ((int)filsid) : ((int)pseudoInode));
    }
    cl->srpc->send_int((int) master);
    return true;
}


// Server stub for List
static void
VestaSourceList(SRPC* srpc, int intf_ver)
{
    LongId longid;
    unsigned int sindex;
    bool delta;
    int limit, overhead;
    VestaSource::errorCode err;
    VSLClosure cl;
    int len;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    sindex = (unsigned int) srpc->recv_int();
    AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
    delta = (bool) srpc->recv_int();
    limit = srpc->recv_int();
    overhead = srpc->recv_int();
    srpc->recv_end();
    
    // Do the work and send the results
    srpc->send_seq_start();
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::readLock, &lock);
    try {
	if (vs == NULL) {
	    err = VestaSource::notFound;
	} else {
	  RWLOCK_LOCKED_REASON(lock, "SRPC:List");
	    cl.srpc = srpc;
	    cl.cost = 0;
	    cl.limit = limit;
	    cl.overhead = overhead;
	    cl.intf_ver = intf_ver;
	    err = vs->list(sindex, VSLCallback, &cl, who, delta, 0);
	    delete vs;
	}
	if (err != VestaSource::ok || cl.cost <= cl.limit) {
	    // Error or the real end of the directory; 
	    // send terminating entry, including error code.
	    srpc->send_chars("");
	    srpc->send_int((int) VestaSource::unused);
	    srpc->send_int((int) err);
	}
    } catch (SRPC::failure f) {
	if (vs != NULL) delete vs;
	if (lock != NULL) lock->releaseRead();
	delete who;
	throw;
    }
    if (lock != NULL) lock->releaseRead();
    delete who;
    srpc->send_seq_end();
    srpc->send_end();
}


// Server stub for GetNFSInfo
static void
VestaSourceGetNFSInfo(SRPC* srpc, int intf_ver)
{
    VestaSource::errorCode err;
    extern char* MyNFSSocket; // set by NFS server initialization
    
    // No arguments to receive
    srpc->recv_end();
    
    // Send the results
    srpc->send_chars(MyNFSSocket);
    srpc->send_bytes((const char *) &RootLongId, sizeof(LongId));
    srpc->send_bytes((const char *) &MutableRootLongId, sizeof(LongId));
    srpc->send_end();
}


// Server stub for GetNFSInfo
static void
VestaSourceFPToShortId(SRPC* srpc, int intf_ver)
{
    VestaSource::errorCode err;
    FP::Tag fptag;
    ShortId sid;

    fptag.Recv(*srpc);
    srpc->recv_end();

    // Do the work
    sid = GetFPShortId(fptag);

    srpc->send_int((int)sid);
    srpc->send_end();
}

static void
VestaSourceReallyDelete(SRPC* srpc, int intf_ver)
{
    int len;
    LongId longid;
    char arc[MAX_ARC_LEN+1];
    bool existCheck;
    time_t timestamp;
    VestaSource::errorCode err;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(arc, len);
    AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
    existCheck = (bool) srpc->recv_int();
    timestamp = (time_t) srpc->recv_int();
    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::writeLock, &lock);
    if (vs == NULL) {
      err = VestaSource::invalidArgs;
    } else {
      RWLOCK_LOCKED_REASON(lock, "SRPC:reallyDelete");
      err = vs->reallyDelete(arc, who, existCheck, timestamp);
      delete vs;
    }
    if (lock != NULL) lock->releaseWrite();
    delete who;
    
    // Send the results
    srpc->send_int((int) err);
    srpc->send_end();
}

static void
VestaSourceInsertFile(SRPC* srpc, int intf_ver)
{
    int len;
    LongId longid, rlongid;
    char arc[MAX_ARC_LEN+1];
    ShortId sid;
    bool master;
    VestaSource::dupeCheck chk;
    time_t timestamp;
    VestaSource::errorCode err;
    FP::Tag fptag;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(arc, len);
    sid = (ShortId) srpc->recv_int();
    master = (bool) srpc->recv_int();
    AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
    chk = (VestaSource::dupeCheck) srpc->recv_int();
    timestamp = (time_t) srpc->recv_int();

    len = FP::ByteCnt;
    unsigned char fpbytes[FP::ByteCnt];
    srpc->recv_bytes_here((char*)fpbytes, len);
    bool have_fp = (len == FP::ByteCnt);
    if(have_fp)
      {
	fptag.FromBytes(fpbytes);
      }

    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::writeLock, &lock);
    VestaSource* newvs;
    if (vs == NULL) {
	err = VestaSource::invalidArgs;
    } else {
      RWLOCK_LOCKED_REASON(lock, "SRPC:insertFile");
	err = vs->insertFile(arc, sid, master, who, chk, &newvs, timestamp,
			     have_fp ? &fptag : NULL);
	delete vs;
    }
    if (lock != NULL) lock->releaseWrite();
    delete who;
    
    // Send the results
    srpc->send_int((int) err);
    if (err == VestaSource::ok) {
	srpc->send_bytes((const char*) &newvs->longid.value, sizeof(LongId));
	srpc->send_int((int) newvs->pseudoInode);
	newvs->fptag.Send(*srpc);
	srpc->send_int((int) newvs->shortId());
	delete newvs;
    }
    srpc->send_end();
}

static void
VestaSourceInsertMutableFile(SRPC* srpc, int intf_ver)
{
    int len;
    LongId longid, rlongid;
    char arc[MAX_ARC_LEN+1];
    ShortId sid;
    bool master;
    VestaSource::dupeCheck chk;
    time_t timestamp;
    VestaSource::errorCode err;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(arc, len);
    sid = (ShortId) srpc->recv_int();
    master = (bool) srpc->recv_int();
    AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
    chk = (VestaSource::dupeCheck) srpc->recv_int();
    timestamp = (time_t) srpc->recv_int();
    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::writeLock, &lock);
    VestaSource* newvs;
    if (vs == NULL) {
	err = VestaSource::invalidArgs;
    } else {
      RWLOCK_LOCKED_REASON(lock, "SRPC:insertMutableFile");
	err = vs->insertMutableFile(arc, sid, master, who, chk, &newvs,
				    timestamp);
	delete vs;
    }
    if (lock != NULL) lock->releaseWrite();
    delete who;
    
    // Send the results
    srpc->send_int((int) err);
    if (err == VestaSource::ok) {
	srpc->send_bytes((const char*) &newvs->longid.value, sizeof(LongId));
	srpc->send_int((int) newvs->pseudoInode);
	newvs->fptag.Send(*srpc);
	srpc->send_int((int) newvs->shortId());
	delete newvs;
    }
    srpc->send_end();
}

static void
VestaSourceInsertImmutableDirectory(SRPC* srpc, int intf_ver)
{
    int len;
    LongId longid, dlongid, rlongid;
    char arc[MAX_ARC_LEN+1];
    bool master;
    VestaSource::dupeCheck chk;
    time_t timestamp;
    VestaSource::errorCode err;
    FP::Tag fptag;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(arc, len);
    len = sizeof(dlongid.value);
    srpc->recv_bytes_here((char *) &dlongid.value, len);
    master = (bool) srpc->recv_int();
    AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
    chk = (VestaSource::dupeCheck) srpc->recv_int();
    timestamp = (time_t) srpc->recv_int();

    len = FP::ByteCnt;
    unsigned char fpbytes[FP::ByteCnt];
    srpc->recv_bytes_here((char*)fpbytes, len);
    bool have_fp = (len == FP::ByteCnt);
    if(have_fp)
      {
	fptag.FromBytes(fpbytes);
      }

    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* vlock = NULL;
    VestaSource* vs;
    ReadersWritersLock* lock;
    if (VolatileRootLongId.isAncestorOf(longid)) {
      // Must retain VolatileRootLock.read across both longid lookups,
      // not acquire and release it within the first.
      vlock = &VolatileRootLock;
      vlock->acquireRead();
      RWLOCK_LOCKED_REASON(vlock,
			   "SRPC:insertImmutableDirectory (in volatile)");
      vs = longid.lookup(LongId::writeLockV, &lock);
    } else {
      vs = longid.lookup(LongId::writeLock, &lock);
    }      
    VestaSource* newvs;
    if (vs == NULL) {
	err = VestaSource::invalidArgs;
	if (vlock != NULL) vlock->releaseRead();
    } else {
      RWLOCK_LOCKED_REASON(lock, "SRPC:insertImmutableDirectory");
	VestaSource* dirvs = dlongid.lookup(LongId::checkLock, &lock);
	if (vlock != NULL) vlock->releaseRead();
	err = vs->insertImmutableDirectory(arc, dirvs, master, who, chk,
					   &newvs, timestamp,
					   have_fp ? &fptag : NULL);
	delete vs;
	if (dirvs != NULL) delete dirvs;
    }
    if (lock != NULL) lock->releaseWrite();
    delete who;
    
    // Send the results
    srpc->send_int((int) err);
    if (err == VestaSource::ok) {
	srpc->send_bytes((const char*) &newvs->longid.value, sizeof(LongId));
	srpc->send_int((int) newvs->pseudoInode);
	newvs->fptag.Send(*srpc);
	srpc->send_int(newvs->shortId());
	delete newvs;
    }
    srpc->send_end();
}

static void
VestaSourceInsertAppendableDirectory(SRPC* srpc, int intf_ver)
{
    int len;
    LongId longid, rlongid;
    char arc[MAX_ARC_LEN+1];
    bool master;
    VestaSource::dupeCheck chk;
    time_t timestamp;
    VestaSource::errorCode err;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(arc, len);
    master = (bool) srpc->recv_int();
    AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
    chk = (VestaSource::dupeCheck) srpc->recv_int();
    timestamp = (time_t) srpc->recv_int();
    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::writeLock, &lock);
    VestaSource* newvs;
    if (vs == NULL) {
	err = VestaSource::invalidArgs;
    } else {
      RWLOCK_LOCKED_REASON(lock, "SRPC:insertAppendableDirectory");
	err = vs->insertAppendableDirectory(arc, master, who, chk,
					    &newvs, timestamp);
	if((err == VestaSource::ok) && master && !vs->master &&
	   !myMasterHint.Empty())
	  {
	    // Inserting a master appendable directory into a
	    // non-master directory (probably a top-leve directory
	    // under /vesta): add a master-repository attribute to the
	    // new directory.
	    newvs->setAttrib("master-repository", myMasterHint.cchars(), NULL);
	  }
	delete vs;
    }
    if (lock != NULL) lock->releaseWrite();
    delete who;
    
    // Send the results
    srpc->send_int((int) err);
    if (err == VestaSource::ok) {
	srpc->send_bytes((const char*) &newvs->longid.value, sizeof(LongId));
	srpc->send_int((int) newvs->pseudoInode);
	newvs->fptag.Send(*srpc);
	delete newvs;
    }
    srpc->send_end();
}

static void
VestaSourceInsertMutableDirectory(SRPC* srpc, int intf_ver)
{
    int len;
    LongId longid, dlongid, rlongid;
    char arc[MAX_ARC_LEN+1];
    bool master;
    VestaSource::dupeCheck chk;
    time_t timestamp;
    VestaSource::errorCode err;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(arc, len);
    len = sizeof(dlongid.value);
    srpc->recv_bytes_here((char *) &dlongid.value, len);
    master = (bool) srpc->recv_int();
    AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
    chk = (VestaSource::dupeCheck) srpc->recv_int();
    timestamp = (time_t) srpc->recv_int();
    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* vlock = NULL;
    VestaSource* vs;
    ReadersWritersLock* lock;
    if (VolatileRootLongId.isAncestorOf(longid)) {
      // Must retain VolatileRootLock.read across both longid lookups,
      // not acquire and release it within the first.
      vlock = &VolatileRootLock;
      vlock->acquireRead();
      RWLOCK_LOCKED_REASON(vlock, "SRPC:insertMutableDirectory (in volatile)");
      vs = longid.lookup(LongId::writeLockV, &lock);
    } else {
      vs = longid.lookup(LongId::writeLock, &lock);
    }      
    VestaSource* newvs;
    if (vs == NULL) {
	err = VestaSource::invalidArgs;
	if (vlock != NULL) vlock->releaseRead();
    } else {
      RWLOCK_LOCKED_REASON(lock, "SRPC:insertMutableDirectory");
	VestaSource* dirvs = dlongid.lookup(LongId::checkLock, &lock);
	if (vlock != NULL) vlock->releaseRead();
	err = vs->insertMutableDirectory(arc, dirvs, master, who, chk,
					 &newvs, timestamp);
	delete vs;
	if (dirvs != NULL) delete dirvs;
    }
    if (lock != NULL) lock->releaseWrite();
    delete who;
    
    // Send the results
    srpc->send_int((int) err);
    if (err == VestaSource::ok) {
	srpc->send_bytes((const char*) &newvs->longid.value, sizeof(LongId));
	srpc->send_int((int) newvs->pseudoInode);
	newvs->fptag.Send(*srpc);
	delete newvs;
    }
    srpc->send_end();
}

static void
VestaSourceInsertGhost(SRPC* srpc, int intf_ver)
{
    int len;
    LongId longid, rlongid;
    char arc[MAX_ARC_LEN+1];
    bool master;
    VestaSource::dupeCheck chk;
    time_t timestamp;
    VestaSource::errorCode err;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(arc, len);
    master = (bool) srpc->recv_int();
    AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
    chk = (VestaSource::dupeCheck) srpc->recv_int();
    timestamp = (time_t) srpc->recv_int();
    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::writeLock, &lock);
    VestaSource* newvs;
    if (vs == NULL) {
	err = VestaSource::invalidArgs;
    } else {
      RWLOCK_LOCKED_REASON(lock, "SRPC:insertGhost");
	err = vs->insertGhost(arc, master, who, chk, &newvs, timestamp);
	delete vs;
    }
    if (lock != NULL) lock->releaseWrite();
    delete who;
    
    // Send the results
    srpc->send_int((int) err);
    if (err == VestaSource::ok) {
	srpc->send_bytes((const char*) &newvs->longid.value, sizeof(LongId));
	srpc->send_int((int) newvs->pseudoInode);
	newvs->fptag.Send(*srpc);
	delete newvs;
    }
    srpc->send_end();
}

static void
VestaSourceInsertStub(SRPC* srpc, int intf_ver)
{
    int len;
    LongId longid, rlongid;
    char arc[MAX_ARC_LEN+1];
    bool master;
    VestaSource::dupeCheck chk;
    time_t timestamp;
    VestaSource::errorCode err;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(arc, len);
    master = (bool) srpc->recv_int();
    AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
    chk = (VestaSource::dupeCheck) srpc->recv_int();
    timestamp = (time_t) srpc->recv_int();
    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::writeLock, &lock);
    VestaSource* newvs;
    if (vs == NULL) {
	err = VestaSource::invalidArgs;
    } else {
      RWLOCK_LOCKED_REASON(lock, "SRPCinsertStub");
	err = vs->insertStub(arc, master, who, chk, &newvs, timestamp);
	delete vs;
    }
    if (lock != NULL) lock->releaseWrite();
    delete who;
    
    // Send the results
    srpc->send_int((int) err);
    if (err == VestaSource::ok) {
	srpc->send_bytes((const char*) &newvs->longid.value, sizeof(LongId));
	srpc->send_int((int) newvs->pseudoInode);
	newvs->fptag.Send(*srpc);
	delete newvs;
    }
    srpc->send_end();
}

static void
VestaSourceRenameTo(SRPC* srpc, int intf_ver)
{
    int len;
    LongId longid, flongid, rlongid;
    char arc[MAX_ARC_LEN+1], fromArc[MAX_ARC_LEN+1];
    VestaSource::dupeCheck chk;
    time_t timestamp;
    VestaSource::errorCode err;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(arc, len);
    len = sizeof(flongid.value);
    srpc->recv_bytes_here((char *) &flongid.value, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(fromArc, len);
    AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
    chk = (VestaSource::dupeCheck) srpc->recv_int();
    timestamp = (time_t) srpc->recv_int();
    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* vlock = NULL;
    ReadersWritersLock* lock;
    VestaSource* vs;
    if (VolatileRootLongId.isAncestorOf(longid)) {
      // Must retain VolatileRootLock.read across both longid lookups,
      // not acquire and release it within the first.
      vlock = &VolatileRootLock;
      vlock->acquireRead();
      RWLOCK_LOCKED_REASON(vlock, "SRPC:renameTo (in volatile)");
      vs = longid.lookup(LongId::writeLockV, &lock);
    } else {
      vs = longid.lookup(LongId::writeLock, &lock);
    }      
    if(lock != 0)
      {
	RWLOCK_LOCKED_REASON(lock, "SRPC:renameTo");
      }
    VestaSource* fvs = flongid.lookup(LongId::checkLock, &lock);
    if (vlock != NULL) vlock->releaseRead();
    if (vs == NULL || fvs == NULL) {
	err = VestaSource::invalidArgs;
    } else {
	err = vs->renameTo(arc, fvs, fromArc, who, chk, timestamp);
    }
    delete who;
    if (vs != NULL) delete vs;
    if (fvs != NULL) delete fvs;
    if (lock != NULL) lock->releaseWrite();
    
    // Send the results
    srpc->send_int((int) err);
    srpc->send_end();
}

// Server stub for MakeMutable
static void
VestaSourceMakeMutable(SRPC* srpc, int intf_ver)
{
    int len;
    LongId longid;
    ShortId sid;
    VestaSource::errorCode err;
    AccessControl::Identity who;
    Basics::uint64 copyMax;

    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    sid = (ShortId) srpc->recv_int();
    if (intf_ver <= 8) {
      copyMax = (Basics::uint64) -1;
      who = NULL;
    } else {
      copyMax = (unsigned int) srpc->recv_int();
      copyMax += ((Basics::uint64) srpc->recv_int()) << 32;
      who = srpc_recv_identity(srpc, intf_ver);
    }
    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::writeLock, &lock);
    try {
	VestaSource* mvs;
	if (vs == NULL) {
	    err = VestaSource::invalidArgs;
	} else {
	  RWLOCK_LOCKED_REASON(lock, "SRPC:makeMutable");
	    err = vs->makeMutable(mvs, sid, copyMax, who);
	    delete vs;
	}
	// Send the results
	srpc->send_int((int) err);
	if (err == VestaSource::ok) {
	    srpc->send_int((int) mvs->type);
	    srpc->send_bytes((const char *) &mvs->longid, sizeof(mvs->longid));
	    srpc->send_int((int) mvs->master);
	    srpc->send_int((int) mvs->pseudoInode);
	    srpc->send_int((int) mvs->shortId());
	    srpc->send_int((int) mvs->timestamp());
	    srpc->send_int((int) mvs->hasAttribs());
	    vs->fptag.Send(*srpc);
	    delete mvs;
	}
    } catch (SRPC::failure f) {
	if (lock != NULL) lock->releaseWrite();
	delete who;
	throw;
    }
    if (lock != NULL) lock->releaseWrite();
    delete who;
    srpc->send_end();
}


static void
VestaSourceInAttribs(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    char* name;
    char* value;
    bool retval;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    name = srpc->recv_chars();
    value = srpc->recv_chars();
    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::readLock, &lock);
    if (vs == NULL) {
	retval = false;
    } else {
      RWLOCK_LOCKED_REASON(lock, "SRPC:inAttribs");
	retval = vs->inAttribs(name, value);
	delete vs;
    }
    if (lock != NULL) lock->releaseRead();
    
    // Send the results
    delete [] name;
    delete [] value;
    srpc->send_int((int) retval);
    srpc->send_end();
}

static void
VestaSourceGetAttrib(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    char* name;
    const char* value;
    bool retval;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    name = srpc->recv_chars();
    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::readLock, &lock);
    if (vs == NULL) {
	value = NULL;
    } else {
      RWLOCK_LOCKED_REASON(lock, "SRPC:getAttribConst");
	value = vs->getAttribConst(name);
	delete vs;
    }
    
    // Send the results
    // Hold lock until done to keep value valid
    try {
	if (value == NULL) {
	    srpc->send_int((int) true);
	    srpc->send_chars("");
	} else {
	    srpc->send_int((int) false);
	    srpc->send_chars(value);
	}
    } catch (SRPC::failure f) {
	if (lock != NULL) lock->releaseRead(); 
	delete [] name;
	throw;
    }
    if (lock != NULL) lock->releaseRead(); 
    delete [] name;
    srpc->send_end();
}

static bool
valueCallback(void* cl, const char* value)
{
    SRPC* srpc = (SRPC*) cl;
    srpc->send_chars(value);
    return true;
}

static void
VestaSourceGetAttrib2(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    char* name;
    bool retval;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    name = srpc->recv_chars();
    srpc->recv_end();
    
    // Do the work and send the results
    srpc->send_seq_start();
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::readLock, &lock);
    if (vs != NULL) {
      RWLOCK_LOCKED_REASON(lock, "SRPC:getAttrib");
	try {
	    vs->getAttrib(name, valueCallback, (void*) srpc);
	} catch (SRPC::failure f) {
	    delete vs;
	    if (lock != NULL) lock->releaseRead();
	    delete [] name;
	    throw;
	}
	delete vs;
    }
    if (lock != NULL) lock->releaseRead();
    delete [] name;
    srpc->send_seq_end();
    srpc->send_end();
}

static void
VestaSourceListAttribs(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    bool retval;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    srpc->recv_end();
    
    // Do the work and send the results
    srpc->send_seq_start();
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::readLock, &lock);
    if (vs != NULL) {
      RWLOCK_LOCKED_REASON(lock, "SRPC:listAttribs");
	try {
	    vs->listAttribs(valueCallback, (void*) srpc);
	} catch (SRPC::failure f) {
	    delete vs;
	    if (lock != NULL) lock->releaseRead();
	    throw;
	}
	delete vs;
    }
    if (lock != NULL) lock->releaseRead();
    srpc->send_seq_end();
    srpc->send_end();
}

static bool
historyCallback(void* cl, VestaSource::attribOp op, const char* name,
		const char* value, time_t timestamp)
{
    SRPC* srpc = (SRPC*) cl;
    srpc->send_int((int) op);
    srpc->send_chars(name);
    srpc->send_chars(value);
    srpc->send_int((int) timestamp);
    return true;
}

static void
VestaSourceGetAttribHistory(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    bool retval;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    srpc->recv_end();
    
    // Do the work and send the results
    srpc->send_seq_start();
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::readLock, &lock);
    if (vs != NULL) {
      RWLOCK_LOCKED_REASON(lock, "SRPC:getAttribHistory");
	try {
	    vs->getAttribHistory(historyCallback, (void*) srpc);
	} catch (SRPC::failure f) {
	    delete vs;
	    if (lock != NULL) lock->releaseRead();
	    throw;
	}
	delete vs;
    }
    if (lock != NULL) lock->releaseRead();
    srpc->send_seq_end();
    srpc->send_end();
}

static void
VestaSourceWriteAttrib(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    VestaSource::attribOp op;
    char* name;
    char* value;
    time_t timestamp;
    VestaSource::errorCode err = VestaSource::ok;
    
    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    op = (VestaSource::attribOp) srpc->recv_int();
    name = srpc->recv_chars();
    value = srpc->recv_chars();
    AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
    timestamp = (time_t) srpc->recv_int();
    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* lock;
    VestaSource* vs = longid.lookup(LongId::writeLock, &lock);
    if (vs != NULL) {
      RWLOCK_LOCKED_REASON(lock, "SRPC:writeAttrib");
      if(!vs->hasAttribs() && MutableRootLongId.isAncestorOf(vs->longid))
	{
	  VestaSource *new_vs = 0;
	  err = vs->copyToMutable(new_vs);
	  delete vs;
	  vs = new_vs;
	}
      if(err == VestaSource::ok)
	{
	  err = vs->writeAttrib(op, name, value, who, timestamp);
	  delete vs;
	}
    } else {
	err = VestaSource::invalidArgs;
    }
    if (lock != NULL) lock->releaseWrite();
    delete who;
    
    // Send the results
    delete [] name;
    delete [] value;
    srpc->send_int((int) err);
    srpc->send_end();
}

void
MakeFilesImmutable(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    unsigned int threshold;
    VestaSource::errorCode err;
    AccessControl::Identity who = NULL;

    try {
      // Receive the arguments
      len = sizeof(longid.value);
      srpc->recv_bytes_here((char *) &longid.value, len);
      threshold = srpc->recv_int();
      if (intf_ver > 8) {
	who = srpc_recv_identity(srpc, intf_ver);
      }
      srpc->recv_end();

      // Do the work
      ReadersWritersLock* lock = NULL;
      VestaSource *vs = longid.lookup(LongId::writeLock, &lock);
      if (vs == NULL) {
	err = VestaSource::invalidArgs;
      } else {
	RWLOCK_LOCKED_REASON(lock, "SRPC:makeFilesImmutable");
	err = vs->makeFilesImmutable(threshold, who);
	delete vs;
      }
      if (lock != NULL) lock->releaseWrite();

      // Send the results
      srpc->send_int((int) err);
      srpc->send_end();
    } catch (...) {
      if (who) delete who;
      throw;
    }
    if (who) delete who;
}    

void
SetIndexMaster(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    unsigned int index;
    bool state;
    VestaSource::errorCode err;
    AccessControl::Identity who = NULL;

    try {
      // Receive the arguments
      len = sizeof(longid.value);
      srpc->recv_bytes_here((char *) &longid.value, len);
      index = (unsigned int) srpc->recv_int();
      state = (bool) srpc->recv_int();
      if (intf_ver <= 8) {
	who = NULL;
      } else {
	who = srpc_recv_identity(srpc, intf_ver);
      }
      srpc->recv_end();
    
      // Do the work
      ReadersWritersLock* lock = NULL;
      VestaSource* vs = longid.lookup(LongId::writeLock, &lock);
      if (vs == NULL) {
	err = VestaSource::invalidArgs;
      } else {
	RWLOCK_LOCKED_REASON(lock, "SRPC:setIndexMaster");
	err = vs->setIndexMaster(index, state, who);
	delete vs;
      }
      if (lock != NULL) lock->releaseWrite();

      // Send the results
      srpc->send_int((int) err);
      srpc->send_end();
    } catch (...) {
      if (who) delete who;
      throw;
    }
    if (who) delete who;
}    

// Server stub for VDirSurrogate::acquireMastership
static void
VestaSourceAcquireMastership(SRPC* srpc, int intf_ver)
{
  const char* pathname = NULL;
  char srcHost[MAX_ARC_LEN+1];
  char srcPort[MAX_ARC_LEN+1];
  VestaSource::errorCode err;
  char pathnameSep;
  AccessControl::Identity dwho = NULL;
  AccessControl::Identity swho = NULL;

  // Receive the arguments
  try {
    int len;
    pathname = srpc->recv_chars();
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(srcHost, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(srcPort, len);
    pathnameSep = (char) srpc->recv_int();
    dwho = srpc_recv_identity(srpc, intf_ver);
    swho = srpc_recv_identity(srpc, intf_ver);
    srpc->recv_end();
    
    // Do the work
    err = AcquireMastership(pathname, srcHost, srcPort, pathnameSep,
			    dwho, swho);

    // Send the results
    srpc->send_int((int) err);
    srpc->send_end();

  } catch (SRPC::failure f) {
    if (dwho) delete dwho;
    if (swho) delete swho;
    if (pathname) delete[] pathname;
    throw;
  } 
  delete dwho;
  delete swho;
  delete[] pathname;
}

// Server stub for VDirSurrogate::cedeMastership
static void
VestaSourceCedeMastership(SRPC* srpc, int intf_ver)
{
  LongId longid;
  char requestid[MAX_ARC_LEN+1];
  const char* grantid = NULL;
  VestaSource::errorCode err;
  VestaSource *vs = NULL;
  AccessControl::Identity who = NULL;

  // Receive the arguments
  try {
    int len;
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(requestid, len);
    who = srpc_recv_identity(srpc, intf_ver);
    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* lock = NULL;
    vs = longid.lookup(LongId::writeLock, &lock);
    if (vs == NULL) {
      err = VestaSource::invalidArgs;
    } else {
      RWLOCK_LOCKED_REASON(lock, "SRPC:cedeMastership");
      err = vs->cedeMastership(requestid, &grantid, who);
    }
    if (lock != NULL) lock->releaseWrite();

    // Send the results
    srpc->send_int((int) err);
    if (err == VestaSource::ok) {
      srpc->send_chars(grantid);
    }
    srpc->send_end();

  } catch (SRPC::failure f) {
    if (who) delete who;
    if (vs) delete vs;
    if (grantid) delete[] grantid;
    throw;
  } 
  if (who) delete who;
  if (vs) delete vs;
  if (grantid) delete[] grantid;
}

// Server stub for VDirSurrogate::replicate
static void
VestaSourceReplicate(SRPC* srpc, int intf_ver)
{
  char* pathname = NULL;
  bool asStub, asGhost;
  char srcHost[MAX_ARC_LEN+1];
  char srcPort[MAX_ARC_LEN+1];
  VestaSource::errorCode err;
  char pathnameSep;
  AccessControl::Identity dwho = NULL;
  AccessControl::Identity swho = NULL;

  // Receive the arguments
  try {
    int len;
    pathname = srpc->recv_chars();
    asStub = (bool) srpc->recv_int();
    asGhost = (bool) srpc->recv_int();
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(srcHost, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(srcPort, len);
    pathnameSep = (char) srpc->recv_int();
    dwho = srpc_recv_identity(srpc, intf_ver);
    swho = srpc_recv_identity(srpc, intf_ver);
    srpc->recv_end();
    
    // Do the work
    err = Replicate(pathname, asStub, asGhost, srcHost, srcPort, pathnameSep,
		    dwho, swho);

    // Send the results
    srpc->send_int((int) err);
    srpc->send_end();

  } catch (SRPC::failure f) {
    if (dwho) delete dwho;
    if (swho) delete swho;
    if (pathname) delete[] pathname;
    throw;
  } 
  delete dwho;
  delete swho;
  delete[] pathname;
}

// Server stub for VDirSurrogate::replicateAttribs
static void
VestaSourceReplicateAttribs(SRPC* srpc, int intf_ver)
{
  char* pathname = NULL;
  bool includeAccess;
  char srcHost[MAX_ARC_LEN+1];
  char srcPort[MAX_ARC_LEN+1];
  VestaSource::errorCode err;
  char pathnameSep;
  AccessControl::Identity dwho = NULL;
  AccessControl::Identity swho = NULL;

  // Receive the arguments
  try {
    int len;
    pathname = srpc->recv_chars();
    includeAccess = (bool) srpc->recv_int();
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(srcHost, len);
    len = MAX_ARC_LEN+1;
    srpc->recv_chars_here(srcPort, len);
    pathnameSep = (char) srpc->recv_int();
    dwho = srpc_recv_identity(srpc, intf_ver);
    swho = srpc_recv_identity(srpc, intf_ver);
    srpc->recv_end();
    
    // Do the work
    err = ReplicateAttribs(pathname, includeAccess, srcHost, srcPort,
			   pathnameSep, dwho, swho);

    // Send the results
    srpc->send_int((int) err);
    srpc->send_end();

  } catch (SRPC::failure f) {
    if (dwho) delete dwho;
    if (swho) delete swho;
    if (pathname) delete[] pathname;
    throw;
  } 
  delete dwho;
  delete swho;
  delete[] pathname;
}


void
VSStat(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    time_t ts = -1;
    bool x = false;
    Basics::uint64 s = 0;
    VestaSource::errorCode err = VestaSource::invalidArgs;

    // Receive the arguments
    len = sizeof(longid.value);
    srpc->recv_bytes_here((char *) &longid.value, len);
    srpc->recv_end();
    
    // Do the work
    ReadersWritersLock* lock = NULL;
    VestaSource* vs = longid.lookup(LongId::readLock, &lock);
    if (vs != NULL) {
      RWLOCK_LOCKED_REASON(lock, "SRPC:stat");
      err = VestaSource::ok;
      ts = vs->timestamp();
      x = vs->executable();
      s = vs->size();
    }
    // Release lock
    if(lock != NULL) lock->releaseRead();

    // Free vs.  (We do this after releaseing the lock as delete may
    // block in the memory allocation system.)
    if(vs != NULL) delete vs;

    // Send the results
    srpc->send_int((int) err);
    srpc->send_int((int) ts);
    srpc->send_int((int) x);
    srpc->send_int((int) (s & 0xffffffff));
    srpc->send_int((int) (s >> 32ul));
    srpc->send_end();
}

void
VSRead(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    int nbytes;
    Basics::uint64 offset;
    void* buffer = NULL;
    VestaSource::errorCode err;
    AccessControl::Identity who = NULL;

    try {
        // Receive the arguments
        len = sizeof(longid.value);
	srpc->recv_bytes_here((char *) &longid.value, len);
	nbytes = srpc->recv_int();
	offset = (Basics::uint64) srpc->recv_int();
	offset += ((Basics::uint64) srpc->recv_int()) << 32ul;
	who = srpc_recv_identity(srpc, intf_ver);
	srpc->recv_end();

	// Do the work
        ReadersWritersLock* lock = NULL;
	VestaSource* vs = longid.lookup(LongId::readLock, &lock);
	if (vs == NULL) {
	    err = VestaSource::invalidArgs;
	} else {
	  RWLOCK_LOCKED_REASON(lock, "SRPC:read");
	    buffer = (void*) malloc(nbytes);
	    err = vs->read(buffer, &nbytes, offset, who);
	}
	// Release lock
	if (lock != NULL) lock->releaseRead();

	if(vs) delete vs;

    	// Send the results
	srpc->send_int((int) err);
	if (err == VestaSource::ok) {
	    srpc->send_bytes((char*)buffer, nbytes);
	}
	srpc->send_end();
    } catch (SRPC::failure f) {
        if (who) delete who;
	if (buffer) free(buffer);
	throw;
    } 
    if (who) delete who;
    if (buffer) free(buffer);
}

void
VSWrite(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    int nbytes;
    Basics::uint64 offset;
    char* buffer = NULL;
    VestaSource::errorCode err;
    AccessControl::Identity who = NULL;

    try {
      // Receive the arguments
      len = sizeof(longid.value);
      srpc->recv_bytes_here((char *) &longid.value, len);
      offset = (Basics::uint64) srpc->recv_int();
      offset += ((Basics::uint64) srpc->recv_int()) << 32ul;
      buffer = srpc->recv_bytes(nbytes);
      who = srpc_recv_identity(srpc, intf_ver);
      srpc->recv_end();
    
      // Do the work
      ReadersWritersLock* lock = NULL;
      VestaSource* vs = longid.lookup(LongId::writeLock, &lock);
      if (vs == NULL) {
	err = VestaSource::invalidArgs;
      } else {
	RWLOCK_LOCKED_REASON(lock, "SRPC:write");
	err = vs->write((void*)buffer, &nbytes, offset, who);
      }
      // Release lock
      if (lock != NULL) lock->releaseWrite();

      delete[] buffer;
      if(vs) delete vs;

      // Send the results
      srpc->send_int((int) err);
      if (err == VestaSource::ok) {
	srpc->send_int((int) nbytes);
      }
      srpc->send_end();
    } catch (...) {
      if (who) delete who;
      throw;
    }
    if (who) delete who;
}

void
VSSetExecutable(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    bool x;
    VestaSource::errorCode err;
    AccessControl::Identity who = NULL;

    try {
      // Receive the arguments
      len = sizeof(longid.value);
      srpc->recv_bytes_here((char *) &longid.value, len);
      x = (bool) srpc->recv_int();
      who = srpc_recv_identity(srpc, intf_ver);
      srpc->recv_end();
    
      // Do the work
      ReadersWritersLock* lock = NULL;
      VestaSource* vs = longid.lookup(LongId::writeLock, &lock);
      if (vs == NULL) {
	err = VestaSource::invalidArgs;
      } else {
	RWLOCK_LOCKED_REASON(lock, "SRPC:setExecutable");
	err = vs->setExecutable(x, who);
      }
      // Release lock
      if (lock != NULL) lock->releaseWrite();
      if(vs) delete vs;

      // Send the results
      srpc->send_int((int) err);
      srpc->send_end();
    } catch (...) {
      if (who) delete who;
      throw;
    }
    if (who) delete who;
}

void
VSSetSize(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    Basics::uint64 s;
    VestaSource::errorCode err;
    AccessControl::Identity who = NULL;

    try {
      // Receive the arguments
      len = sizeof(longid.value);
      srpc->recv_bytes_here((char *) &longid.value, len);
      s = (Basics::uint64) srpc->recv_int();
      s += ((Basics::uint64) srpc->recv_int()) << 32ul;
      who = srpc_recv_identity(srpc, intf_ver);

      srpc->recv_end();
    
      // Do the work
      ReadersWritersLock* lock = NULL;
      VestaSource* vs = longid.lookup(LongId::writeLock, &lock);
      if (vs == NULL) {
	err = VestaSource::invalidArgs;
      } else {
	RWLOCK_LOCKED_REASON(lock, "SRPC:setSize");
	err = vs->setSize(s, who);
      }
      // Release lock
      if (lock != NULL) lock->releaseWrite();
      if(vs) delete vs;

      // Send the results
      srpc->send_int((int) err);
      srpc->send_end();
    } catch (...) {
      if (who) delete who;
      throw;
    }
    if (who) delete who;
}

void
VSSetTimestamp(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    time_t ts;
    VestaSource::errorCode err;
    AccessControl::Identity who = NULL;

    try {
      // Receive the arguments
      len = sizeof(longid.value);
      srpc->recv_bytes_here((char *) &longid.value, len);
      ts = (time_t) srpc->recv_int();
      who = srpc_recv_identity(srpc, intf_ver);
      srpc->recv_end();
    
      // Do the work
      ReadersWritersLock* lock = NULL;
      VestaSource* vs = longid.lookup(LongId::writeLock, &lock);
      if (vs == NULL) {
	err = VestaSource::invalidArgs;
      } else {
	RWLOCK_LOCKED_REASON(lock, "SRPC:setTimestamp");
	err = vs->setTimestamp(ts, who);
      }
      // Release lock
      if (lock != NULL) lock->releaseWrite();
      if(vs) delete vs;

      // Send the results
      srpc->send_int((int) err);
      srpc->send_end();
    } catch (...) {
      if (who) delete who;
      throw;
    }
    if (who) delete who;
}

static void
VestaSourceGetUserInfo(SRPC* srpc, int intf_ver)
{
  // Receive the arguments.  The first is really just a formality to
  // ensure that only authorized users can inquire about other users.
  AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
  AccessControl::Identity subject = srpc_recv_identity(srpc, intf_ver, false);
  srpc->recv_end();

  AccessControl::IdInfo result;

  unsigned int i = 0;
  const char *val = 0;

  // If the requestor is different from the subject and the requestor
  // is not an administrator...
  if((*who != *subject) &&
     !VestaSource::repositoryRoot()->ac.check(who,
					      AccessControl::administrative))
    {
      // Deny the request.
      srpc->send_failure(SRPC::invalid_parameter,
			 "administrator access required to get information about other users");
    }

  // Get the list of the user's names and aliases.
  while((val = subject->user(i)) != 0)
    {
      result.names.append(val);
      i++;
    }

  // Get the list of the user's group memberships.
  i = 0;
  while((val = subject->group(i)) != 0)
    {
      result.groups.append(val);
      i++;
    }

  // Get the user ID and primary group ID of the user.  (These are
  // significant for NFS access.)
  result.unix_uid = subject->toUnixUser();
  result.unix_gid = subject->toUnixGroup();

  // Determine if the user has administrative access.
  result.is_admin = 
    VestaSource::repositoryRoot()->ac.check(subject,
					    AccessControl::administrative);
  // Determine if the user has agreement access.
  result.is_wizard =
    VestaSource::repositoryRoot()->ac.check(subject,
					    AccessControl::agreement);
  // Root has additional special powers (can make anything
  // setuid/setgid).
  result.is_root = subject->userMatch(AccessControl::rootUser);
  // The runtool user is the special user that owns the volatile tree.
  result.is_runtool = subject->userMatch(AccessControl::runtoolUser);

  // Send the list of global names and groups
  srpc->send_chars_seq(result.names);
  srpc->send_chars_seq(result.groups);

  // Send the NFS uid and primary gid
  srpc->send_int32(result.unix_uid);
  srpc->send_int32(result.unix_gid);

  // Send all the special bits in one 16-bit integer
  srpc->send_int16((result.is_root ? 1 : 0) |
		   ((result.is_admin ? 1 : 0) << 1) |
		   ((result.is_wizard ? 1 : 0) << 2) |
		   ((result.is_runtool ? 1 : 0) << 3));

  srpc->send_end();
}

static void
VestaSourceRefreshAccessTables(SRPC* srpc, int intf_ver)
{
  // Receive the arguments.  The first is really just a formality to
  // ensure that only authorized users can inquire about other users.
  AccessControl::Identity who = srpc_recv_identity(srpc, intf_ver);
  srpc->recv_end();

  // Does the requestor have administrative access?
  if(!VestaSource::repositoryRoot()->ac.check(who,
					      AccessControl::administrative))
    {
      // If not, deny the request.
      srpc->send_failure(SRPC::invalid_parameter,
			 "administrator access required");
    }

  try
    {
      AccessControl::refreshAccessTables();
    }
  catch(AccessControl::ParseError f)
    {
      Text message = ("error parsing " + f.fkind + " " + f.fname +
		      ": " + f.message);

      // Not really an invalid parameter, but one of the access
      // control files was unparseable.
      srpc->send_failure(SRPC::invalid_parameter,
			 message.cchars());
    }

  srpc->send_end();
}

static void
VestaSourceGetStats(SRPC* srpc, int intf_ver)
{
  // This is just a formailty to ensure that only users with read
  // access to the repository can make this call.
  AccessControl::Identity who = 0;
  Basics::int16 *requested_stats = 0;

  try
    {
      who = srpc_recv_identity(srpc, intf_ver);

      int requested_stats_count = 0;
      requested_stats = srpc->recv_int16_array(requested_stats_count);

      srpc->recv_end();

      srpc->send_seq_start();
      for(int i = 0; i < requested_stats_count; i++)
	{
	  switch((ReposStats::StatKind) requested_stats[i])
	    {
	    case ReposStats::fdCache:
	      srpc->send_int16(requested_stats[i]);
	      {
		ReposStats::FdCacheStats fdCacheStats;
		FdCache::getStats(fdCacheStats.n_in_cache,
				  fdCacheStats.hits,
				  fdCacheStats.open_misses,
				  fdCacheStats.try_misses,
				  fdCacheStats.evictions,
				  fdCacheStats.expirations);

		// Send them to the client.
		fdCacheStats.send(srpc);
	      }
	      break;
	    case ReposStats::dupeTotal:
	      srpc->send_int16(requested_stats[i]);
	      {
		// Get the duplicate suppression statistics.
		ReposStats::DupeStats dupeStats;
		dupeStats = get_dupe_stats();

		// Send them to the client.
		dupeStats.send(srpc);
	      }
	      break;
	    case ReposStats::srpcTotal:
	      srpc->send_int16(requested_stats[i]);
	      {
		// Get statistics on SRPC calls
		ReposStats::TimedCalls srpcStats;
		SRPC_Call_Stats::getStats(srpcStats.call_count,
					  srpcStats.elapsed_secs,
					  srpcStats.elapsed_usecs);

		// Send them to the client
		srpcStats.send(srpc);
	      }
	      break;
	    case ReposStats::nfsTotal:
	      srpc->send_int16(requested_stats[i]);
	      {
		// Get statistics on NFS calls
		ReposStats::TimedCalls nfsStats;
		NFS_Call_Stats::getStats(nfsStats.call_count,
					 nfsStats.elapsed_secs,
					 nfsStats.elapsed_usecs);

		// Send them to the client
		nfsStats.send(srpc);
	      }
	      break;
	    case ReposStats::memUsage:
	      srpc->send_int16(requested_stats[i]);
	      {
		// Get memory usage
		unsigned long total, resident;
		OS::GetProcessSize(total, resident);

		// Send them to the client
		ReposStats::MemStats memStats(total, resident);
		memStats.send(srpc);
	      }
	      break;
	    case ReposStats::vMemPool:
	      srpc->send_int16(requested_stats[i]);
	      {
		// Get statistics from VMemPool
		ReposStats::VMemPoolStats vmp_stats;
		VMemPool::getStats(vmp_stats.size,
				   vmp_stats.free_list_blocks,
				   vmp_stats.free_list_bytes,
				   vmp_stats.free_wasted_bytes,
				   vmp_stats.nonempty_free_list_count,
				   vmp_stats.allocate_calls,
				   vmp_stats.allocate_rej_sm_blocks,
				   vmp_stats.allocate_rej_lg_blocks,
				   vmp_stats.allocate_split_blocks,
				   vmp_stats.allocate_new_blocks,
				   vmp_stats.allocate_time,
				   vmp_stats.free_calls,
				   vmp_stats.free_coalesce_before,
				   vmp_stats.free_coalesce_after,
				   vmp_stats.free_time,
				   vmp_stats.grow_calls);

		// Send them to the client
		vmp_stats.send(srpc);
	      }
	      break;
	      // Note: no default switch label.  In the event that the
	      // client asks for a statistic we don't know how to gather,
	      // we just don't send it back to them.
	    }
	}
      srpc->send_seq_end();

      srpc->send_end();
    }
  catch (...)
    {
      if(who) delete who;
      if(requested_stats) delete [] requested_stats;
      throw;
    }
  if (who) delete who;
  if(requested_stats) delete [] requested_stats;
}

static void
VestaSourceMeasureDirectory(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    VestaSource::errorCode err;
    AccessControl::Identity who = NULL;

    try {
      // Receive the arguments
      len = sizeof(longid.value);
      srpc->recv_bytes_here((char *) &longid.value, len);
      who = srpc_recv_identity(srpc, intf_ver);
      srpc->recv_end();
    
      // Do the work
      VestaSource::directoryStats stats;
      ReadersWritersLock* lock = NULL;
      VestaSource* vs = longid.lookup(LongId::readLock, &lock);
      if (vs == NULL) {
	err = VestaSource::invalidArgs;
      } else {
	RWLOCK_LOCKED_REASON(lock, "SRPC:MeasureDirectory");
	err = vs->measureDirectory(stats, who);
      }
      // Release lock
      if (lock != NULL) lock->releaseRead();

      if(vs) delete vs;

      // Send the results
      srpc->send_int((int) err);
      if(err == VestaSource::ok)
      {
	srpc->send_int32(stats.baseChainLength);
	srpc->send_int32(stats.usedEntryCount);
	srpc->send_int32(stats.usedEntrySize);
	srpc->send_int32(stats.totalEntryCount);
	srpc->send_int32(stats.totalEntrySize);
      }
      srpc->send_end();
    } catch (...) {
      if (who) delete who;
      throw;
    }
    if (who) delete who;
}

static void
VestaSourceCollapseBase(SRPC* srpc, int intf_ver)
{
    LongId longid;
    int len;
    VestaSource::errorCode err;
    AccessControl::Identity who = NULL;

    try {
      // Receive the arguments
      len = sizeof(longid.value);
      srpc->recv_bytes_here((char *) &longid.value, len);
      who = srpc_recv_identity(srpc, intf_ver);
      srpc->recv_end();
    
      // Do the work
      ReadersWritersLock* lock = NULL;
      VestaSource* vs = longid.lookup(LongId::writeLock, &lock);
      if (vs == NULL) {
	err = VestaSource::invalidArgs;
      } else {
	RWLOCK_LOCKED_REASON(lock, "SRPC:CollapseBase");
	err = vs->collapseBase(who);
      }
      // Release lock
      if (lock != NULL) lock->releaseWrite();

      if(vs) delete vs;

      // Send the results
      srpc->send_int((int) err);
      srpc->send_end();
    } catch (...) {
      if (who) delete who;
      throw;
    }
    if (who) delete who;
}

static void VestaSourceGetMasterHint(SRPC* srpc, int intf_ver)
{
  // there is no arguments to receive
  srpc->recv_end();
  srpc->send_Text(myMasterHint);
  srpc->send_end();
}


static void
SetPerfDebug(SRPC* srpc, int intf_ver)
{
  AccessControl::Identity who = 0;
  try
    {
      // Receive the arguments
      who = srpc_recv_identity(srpc, intf_ver);
      Basics::uint64 settings = srpc->recv_int64();
      srpc->recv_end();

      if(!VestaSource::repositoryRoot()->ac.check(who,
						  AccessControl::administrative))
	{
	  // If not, deny the request.
	  srpc->send_failure(SRPC::invalid_parameter,
			     "administrator access required");
	}

      Basics::uint64 result = 0;
#if defined(REPOS_PERF_DEBUG)
      timing_control(settings & PerfDebug::nfsCallTiming);
      result |= (settings & PerfDebug::nfsCallTiming);

      rwlock_timing_control(settings & PerfDebug::centralLockTiming);
      result |= (settings & PerfDebug::centralLockTiming);
#endif
      srpc->send_int64(result);
      srpc->send_end();
    }
  catch (...)
    {
      if (who) delete who;
      throw;
    }
  if (who) delete who;
}

// Server version.  This is defined in a file written in progs.ves.
extern const char *Version;

// Server start time, set in main.
extern time_t serverStartTime;

static void
GetServerInfo(SRPC* srpc, int intf_ver)
{
  AccessControl::Identity who = 0;
  try
    {
      // Receive the arguments
      who = srpc_recv_identity(srpc, intf_ver);
      srpc->recv_end();

      srpc->send_chars(Version);
      srpc->send_int64(serverStartTime);
      srpc->send_int32(time((time_t *) 0) - serverStartTime);

      srpc->send_end();
    }
  catch (...)
    {
      if (who) delete who;
      throw;
    }
  if (who) delete who;
}

// These control VSReadWholeCompressed.  They're read from config
// settings in VestaSourceServerExport below.
static int readWhole_raw_bufsiz = (128*1024);
static int readWhole_deflate_bufsiz = (64*1024);
static int readWhole_deflate_level = -1;

static void
VSReadWholeCompressed(SRPC* srpc, int intf_ver)
{
  // Arguments from the client
  LongId longid;
  int len;
  AccessControl::Identity who = 0;
  int maxbytes;

  // Result status sent to the client
  VestaSource::errorCode err;

  // Information on the file we're compressing and sending to the client.
  ShortId filesid;
  int fd = -1;
  Basics::uint64 bytes_left;
  off_t bytes_read = 0;

  // Client's list of supported compression methods
  Basics::int16 *client_methods = 0;
  int client_method_count = 0;

  // zlib state
  z_stream zstrm;
  bool zstrm_initialized = false;

  // Buffers used to hold bytes read from the file and compressed
  // bytes
  char *raw_buf = 0, *send_buf = 0;

  try
    {
      // Receive the arguments
      len = sizeof(longid.value);
      srpc->recv_bytes_here((char *) &longid.value, len);
      who = srpc_recv_identity(srpc, intf_ver);
      client_methods = srpc->recv_int16_array(client_method_count);
      maxbytes = srpc->recv_int32();
      srpc->recv_end();

      bool method_ok = false;
      for(unsigned int i = 0; i < client_method_count; i++)
	if(client_methods[i] == VestaSourceSRPC::compress_zlib_deflate)
	  method_ok = true;
      if(!method_ok)
	{
	  err = VestaSource::invalidArgs;
	}
      else
	{
	  // Do the work
	  ReadersWritersLock* lock = NULL;
	  VestaSource* vs = longid.lookup(LongId::readLock, &lock);
	  if (vs == NULL) {
	    err = VestaSource::invalidArgs;
	  } else {
	    RWLOCK_LOCKED_REASON(lock, "SRPC:readWholeCompressed");
	    if(vs->type != VestaSource::immutableFile) {
	      err = VestaSource::inappropriateOp;
	    } else {
	      filesid = vs->shortId();
	      bytes_left = vs->size();
	      fd = FdCache::open(filesid, FdCache::ro);
	      err = ((fd == -1)
		     ? Repos::errno_to_errorCode(errno)
		     : VestaSource::ok);
	    }
	  }
      
	  // Release lock.  (We don't want to hold it while compressing
	  // and sending data to the client.)
	  if (lock != NULL) lock->releaseRead();
	  // Free memory
	  if (vs) delete vs;
	}

      // Send the results
      srpc->send_int((int) err);
      if (err == VestaSource::ok) {
	assert(fd != -1);

	// For now, we always use zlib's deflate for compression.
	srpc->send_int16(VestaSourceSRPC::compress_zlib_deflate);

	// Now that we've release the lock, we'll actually start up
	// zlib and compress the data.
	zstrm.zalloc = Z_NULL;
	zstrm.zfree = Z_NULL;
	zstrm.opaque = Z_NULL;
	if (deflateInit(&zstrm, readWhole_deflate_level) != Z_OK)
	  srpc->send_failure(SRPC::internal_trouble,
			     "zlib deflateInit failed");
	zstrm_initialized = true;

	raw_buf = NEW_PTRFREE_ARRAY(char, readWhole_raw_bufsiz);
	unsigned int send_size = ((maxbytes < readWhole_deflate_bufsiz)
				  ? maxbytes
				  : readWhole_deflate_bufsiz);
	send_buf = NEW_PTRFREE_ARRAY(char, send_size);

	srpc->send_seq_start();
	while(bytes_left > 0)
	  {
	    zstrm.next_in = (Bytef *) raw_buf;
	    int read_res;
	    do
	      read_res = ::pread(fd, raw_buf, readWhole_raw_bufsiz,
				 bytes_read);
	    while((read_res == -1) && (errno == EINTR));
	    zstrm.avail_in = read_res;

	    if(zstrm.avail_in > 0)
	      {
		assert(bytes_left >= zstrm.avail_in);
		bytes_left -= zstrm.avail_in;
		bytes_read += zstrm.avail_in;
		do
		  {
		    zstrm.avail_out = send_size;
		    zstrm.next_out = (Bytef *) send_buf;
		    int deflate_status = deflate(&zstrm,
						 (bytes_left > 0
						  ? Z_NO_FLUSH
						  : Z_FINISH));
		    if(deflate_status == Z_STREAM_ERROR)
		      srpc->send_failure(SRPC::internal_trouble,
					 "zlib deflate returned Z_STREAM_ERROR");
		    srpc->send_bytes(send_buf,
				     send_size - zstrm.avail_out);
		  }
		while(zstrm.avail_out == 0);
		if(zstrm.avail_in != 0)
		  srpc->send_failure(SRPC::internal_trouble,
				     "zlib deflate left some input unconsumed");
	      }
	    else
	      {
		int errno_save = errno;
		Text msg("error reading file for compression: ");
		msg += Basics::errno_Text(errno_save);
		srpc->send_failure(SRPC::internal_trouble, msg);
	      }
	  }
	srpc->send_seq_end();

	srpc->send_end();
      }
    }
  catch (...)
    {
      if(zstrm_initialized) (void)deflateEnd(&zstrm);
      if(fd != -1) FdCache::close(filesid, fd, FdCache::ro);
      if(raw_buf) delete [] raw_buf;
      if(send_buf) delete [] send_buf;
      if(who) delete who;
      throw;
    }
  if(zstrm_initialized) (void)deflateEnd(&zstrm);
  if(fd != -1) FdCache::close(filesid, fd, FdCache::ro);
  if(raw_buf) delete [] raw_buf;
  if(send_buf) delete [] send_buf;
  if (who) delete who;
}

// What's the minimum interface version we can support?
static const int g_min_intf_ver = 8;

class VestaSourceReceptionist : public LimService::Handler
{
public:
  VestaSourceReceptionist() { }

  void call(SRPC *srpc, int intf_ver, int proc_id) throw(SRPC::failure);
  void call_failure(SRPC *srpc, const SRPC::failure &f) throw();
  void accept_failure(const SRPC::failure &f) throw();
  void other_failure(const char *msg) throw();
  void listener_terminated() throw();
};

// Function to accept RPC calls
void
VestaSourceReceptionist::call(SRPC *srpc, int intf_ver, int proc_id)
  throw(SRPC::failure)
{
  // This objects destruction when we leave this function (either
  // normally or by an exception) will record statistics about this
  // SRPC call.
  SRPC_Call_Stats::Helper stat_recorder;

    // Needed?
    signal(SIGPIPE, SIG_IGN);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGILL, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    // end Needed?

    if (intf_ver < g_min_intf_ver || intf_ver > VestaSourceSRPC::version) {
      Text client = srpc->remote_socket();
      Repos::dprintf(DBG_ALWAYS,
		     "VestaSourceReceptionist got unsupported interface "
		     "version %d; client %s\n", intf_ver, client.cchars());
      srpc->send_failure(SRPC::version_skew,
			 "VestaSourceSRPC: Unsupported interface version");
      return;
    }

    // Don't allow us to block forever waiting for a client.
    srpc->enable_read_timeout(readTimeout);

    switch (proc_id)
      {
      case VestaSourceSRPC::Lookup:
	VestaSourceLookup(srpc, intf_ver);
	break;
      case VestaSourceSRPC::CreateVolatileDirectory:
	VestaSourceCreateVolatileDirectory(srpc, intf_ver);
	break;
      case VestaSourceSRPC::DeleteVolatileDirectory:
	VestaSourceDeleteVolatileDirectory(srpc, intf_ver);
	break;
      case VestaSourceSRPC::GetBase:
	VestaSourceGetBase(srpc, intf_ver);
	break;
      case VestaSourceSRPC::List:
	VestaSourceList(srpc, intf_ver);
	break;
      case VestaSourceSRPC::GetNFSInfo:
	VestaSourceGetNFSInfo(srpc, intf_ver);
	break;
      case VestaSourceSRPC::FPToShortId:
	VestaSourceFPToShortId(srpc, intf_ver);
	break;
      case VestaSourceSRPC::ReallyDelete:
	VestaSourceReallyDelete(srpc, intf_ver);
	break;
      case VestaSourceSRPC::InsertFile:
	VestaSourceInsertFile(srpc, intf_ver);
	break;
      case VestaSourceSRPC::InsertMutableFile:
	VestaSourceInsertMutableFile(srpc, intf_ver);
	break;
      case VestaSourceSRPC::InsertImmutableDirectory:
	VestaSourceInsertImmutableDirectory(srpc, intf_ver);
	break;
      case VestaSourceSRPC::InsertAppendableDirectory:
	VestaSourceInsertAppendableDirectory(srpc, intf_ver);
	break;
      case VestaSourceSRPC::InsertMutableDirectory:
	VestaSourceInsertMutableDirectory(srpc, intf_ver);
	break;
      case VestaSourceSRPC::InsertGhost:
	VestaSourceInsertGhost(srpc, intf_ver);
	break;
      case VestaSourceSRPC::InsertStub:
	VestaSourceInsertStub(srpc, intf_ver);
	break;
      case VestaSourceSRPC::RenameTo:
	VestaSourceRenameTo(srpc, intf_ver);
	break;
      case VestaSourceSRPC::MakeMutable:
	VestaSourceMakeMutable(srpc, intf_ver);
	break;
      case VestaSourceSRPC::InAttribs:
	VestaSourceInAttribs(srpc, intf_ver);
	break;
      case VestaSourceSRPC::GetAttrib:
	VestaSourceGetAttrib(srpc, intf_ver);
	break;
      case VestaSourceSRPC::GetAttrib2:
	VestaSourceGetAttrib2(srpc, intf_ver);
	break;
      case VestaSourceSRPC::ListAttribs:
	VestaSourceListAttribs(srpc, intf_ver);
	break;
      case VestaSourceSRPC::GetAttribHistory:
	VestaSourceGetAttribHistory(srpc, intf_ver);
	break;
      case VestaSourceSRPC::WriteAttrib:
	VestaSourceWriteAttrib(srpc, intf_ver);
	break;
      case VestaSourceSRPC::LookupPathname:
	VestaSourceLookupPathname(srpc, intf_ver);
	break;
      case VestaSourceSRPC::LookupIndex:
	VestaSourceLookupIndex(srpc, intf_ver);
	break;
      case VestaSourceSRPC::MakeFilesImmutable:
	MakeFilesImmutable(srpc, intf_ver);
	break;
      case VestaSourceSRPC::SetIndexMaster:
	SetIndexMaster(srpc, intf_ver);
	break;
      case VestaSourceSRPC::Stat:
	VSStat(srpc, intf_ver);
	break;
      case VestaSourceSRPC::Read:
	VSRead(srpc, intf_ver);
	break;
      case VestaSourceSRPC::Write:
	VSWrite(srpc, intf_ver);
	break;
      case VestaSourceSRPC::SetExecutable:
	VSSetExecutable(srpc, intf_ver);
	break;
      case VestaSourceSRPC::SetSize:
	VSSetSize(srpc, intf_ver);
	break;
      case VestaSourceSRPC::SetTimestamp:
	VSSetTimestamp(srpc, intf_ver);
	break;
      case VestaSourceSRPC::Atomic:
	VSAtomic(srpc, intf_ver);
	break;
      case VestaSourceSRPC::AtomicTarget:
      case VestaSourceSRPC::AtomicDeclare:
      case VestaSourceSRPC::AtomicResync:
      case VestaSourceSRPC::AtomicTestMaster:
      case VestaSourceSRPC::AtomicRun:
      case VestaSourceSRPC::AtomicCancel:
	{
	  Text client = srpc->remote_socket();
	  Repos::dprintf(DBG_ALWAYS,
			 "VestaSourceReceptionist got misplaced Atomic "
			 "proc_id %d; client %s\n", proc_id, client.cchars());
	  srpc->send_failure(SRPC::version_skew,
			     "VestaSourceSRPC: Misplaced Atomic proc_id");
	}
	break;
      case VestaSourceSRPC::AcquireMastership:
        VestaSourceAcquireMastership(srpc, intf_ver);
	break;
      case VestaSourceSRPC::CedeMastership:
        VestaSourceCedeMastership(srpc, intf_ver);
	break;
      case VestaSourceSRPC::Replicate:
        VestaSourceReplicate(srpc, intf_ver);
	break;
      case VestaSourceSRPC::ReplicateAttribs:
        VestaSourceReplicateAttribs(srpc, intf_ver);
	break;
      case VestaSourceSRPC::GetUserInfo:
        VestaSourceGetUserInfo(srpc, intf_ver);
	break;
      case VestaSourceSRPC::RefreshAccessTables:
        VestaSourceRefreshAccessTables(srpc, intf_ver);
	break;
      case VestaSourceSRPC::GetStats:
	VestaSourceGetStats(srpc, intf_ver);
	break;
      case VestaSourceSRPC::MeasureDirectory:
	VestaSourceMeasureDirectory(srpc, intf_ver);
	break;
      case VestaSourceSRPC::CollapseBase:
	VestaSourceCollapseBase(srpc, intf_ver);
	break;
      case VestaSourceSRPC::SetPerfDebug:
	SetPerfDebug(srpc, intf_ver);
	break;
      case VestaSourceSRPC::GetServerInfo:
	GetServerInfo(srpc, intf_ver);
	break;
      case VestaSourceSRPC::ReadWholeCompressed:
	VSReadWholeCompressed(srpc, intf_ver);
	break;
      case VestaSourceSRPC::GetMasterHint:
	VestaSourceGetMasterHint(srpc, intf_ver);
	break;
      default:
	{
	  Text client = srpc->remote_socket();
	  Repos::dprintf(DBG_ALWAYS, "VestaSourceReceptionist got unknown "
			 "proc_id %d; client %s\n", proc_id, client.cchars());
	  srpc->send_failure(SRPC::version_skew,
			     "VestaSourceSRPC: Unknown proc_id");
	}
	break;
      }
}

void
VestaSourceReceptionist::call_failure(SRPC* srpc, const SRPC::failure &f)
  throw()
{
    if (f.r == SRPC::partner_went_away && !Repos::isDebugLevel(DBG_SRPC))
      return;
    Text client;
    try {
	client = srpc->remote_socket();
    } catch (SRPC::failure f) {
	client = "unknown";
    }
    Repos::dprintf(DBG_ALWAYS,
		   "VestaSourceReceptionist got SRPC::failure: %s (%d); "
		   "client %s\n", f.msg.cchars(), f.r, client.cchars());
}

void VestaSourceReceptionist::accept_failure(const SRPC::failure &f) throw()
{
  Repos::dprintf(DBG_ALWAYS,
		 "VestaSourceReceptionist got SRPC::failure "
		 "accepting new connection: %s (%d)\n",
		 f.msg.cchars(), f.r);
}

void VestaSourceReceptionist::other_failure(const char *msg) throw()
{
  Repos::dprintf(DBG_ALWAYS,
		 "VestaSourceReceptionist: %s\n", msg);
}

void VestaSourceReceptionist::listener_terminated() throw()
{
  Repos::dprintf(DBG_ALWAYS,
		 "VestaSourceReceptionist: Fatal error: unable to accept new "
		 "connections; exiting\n");
  abort();
}

void
VestaSourceServerExport()
{
    Text port;
    int max_running=DEF_MAX_RUNNING;
    Text unused;

    try {
	//VestaSourceSRPC_port is required.
	//SRPC_max_* are allowed to be non-existant or ints.
	//anything else is fatal.
	port = VestaConfig::get_Text("Repository", "VestaSourceSRPC_port");
	if(VestaConfig::get("Repository",
			    "VestaSourceSRPC_max_running", unused))
	    max_running = VestaConfig::get_int("Repository",
					       "VestaSourceSRPC_max_running");
	if(VestaConfig::get("Repository",
			    "VestaSourceSRPC_read_timeout", unused))
	    readTimeout = VestaConfig::get_int("Repository",
					       "VestaSourceSRPC_read_timeout");
	if(VestaConfig::get("Repository",
			    "VestaSourceSRPC_readWhole_raw_bufsiz", unused))
	  readWhole_raw_bufsiz =
	      VestaConfig::get_int("Repository",
				   "VestaSourceSRPC_readWhole_raw_bufsiz");
	if(VestaConfig::get("Repository",
			    "VestaSourceSRPC_readWhole_deflate_bufsiz",
			    unused))
	  readWhole_deflate_bufsiz =
	      VestaConfig::get_int("Repository",
				   "VestaSourceSRPC_readWhole_deflate_bufsiz");
	if(VestaConfig::get("Repository",
			    "VestaSourceSRPC_readWhole_deflate_level",
			    unused))
	  readWhole_deflate_level =
	      VestaConfig::get_int("Repository",
				   "VestaSourceSRPC_readWhole_deflate_level");
    } catch (VestaConfig::failure f) {
	Repos::dprintf(DBG_ALWAYS,
		       "VestaConfig::failure in VestaSourceServerExport: %s\n",
		       f.msg.cchars());
	abort();
    }

    static Basics::thread_attr ls_thread_attr(STACK_SIZE);

#if defined (_POSIX_THREAD_PRIORITY_SCHEDULING) && defined(PTHREAD_SCHED_RR_OK) && !defined(__linux__)
    // Linux only allows the superuser to use SCHED_RR
    ls_thread_attr.set_schedpolicy(SCHED_RR);
    ls_thread_attr.set_inheritsched(PTHREAD_EXPLICIT_SCHED);
    ls_thread_attr.set_sched_priority(sched_get_priority_min(SCHED_RR));
#endif

    static VestaSourceReceptionist handler;
    static LimService receptionist(port, max_running, handler, ls_thread_attr);
    receptionist.Forked_Run();
}
