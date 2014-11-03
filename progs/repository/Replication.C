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
// Replication.C
//
// Implements Replicate()
//

#include "VestaSource.H"
#include "VDirSurrogate.H"
#include "VRConcurrency.H"
#include "Mastership.H"
#include "Replication.H"
#include "UniqueId.H"
#include "logging.H"
#include "FPShortId.H"

#include "lock_timing.H"

#include <FdStream.H>

using FS::OFdStream;

/*#define COPY_SIZE 8192*/
#define COPY_SIZE (128*1024)
#define REPLICATOR_DIR ".replicator"

//
// Check whether this repository is permitted to take a replica of an
// object from the specified repository.  The vs parameter is either
// the existing local copy of the object (perhaps a stub or ghost), or
// the object's parent directory if no such copy exists.
// Implementation: search upward for the first "which" attribute; if
// found, return true if it lists the source repository or "*".
// If not, or none found, return false.
//
bool
ReplicationAccessCheck(VestaSource* vs, const char* which,
		       const char* srcHost, const char* srcPort) throw ()
{
  bool free_vs = false;
  for (;;) {
    if (vs == NULL) return false;
    if (vs->getAttribConst(which)) break;
    VestaSource *vs_parent = vs->longid.getParent().lookup();
    if(free_vs) 
      delete vs;
    vs = vs_parent;
    // We allocated vs, we need to free it.
    free_vs = true;
  }
  char hostport[2*MAX_ARC_LEN+2];
  sprintf(hostport, "%s:%s", srcHost, srcPort);
  bool result = vs->inAttribs(which, hostport) || vs->inAttribs(which, "*");
  if(free_vs)
    delete vs;
  return result;
}

// We use this to remember hosts that didn't seem to support readWhole
// when we tried it previously.
static Table<Text, time_t>::Default bad_readWhole_peers;

// We'll refrain from trying readWhole for an hour after it fails
#define BAD_READWHOLE_TTL (60*60)

static bool Should_readWhole(const Text &host, const Text &port)
{
  Text hostport = host;
  hostport += ":";
  hostport += port;

  time_t failed;

  // Did an attempt to call readWhole on this peer fail in the past?
  if(bad_readWhole_peers.Get(hostport, failed))
    {
      time_t now = time(0);
      // Was it less than the TTL ago?
      if((now - failed) < BAD_READWHOLE_TTL)
	return false;
    }

  return true;
}

static void Failed_readWhole(const Text &host, const Text &port)
{
  Text hostport = host;
  hostport += ":";
  hostport += port;

  time_t now = time(0);

  // Remember that this peer had problems with readWhole
  (void) bad_readWhole_peers.Put(hostport, now);
}

// If ReplicateImmFile has trouble copying a file from the remote
// server, this function is used to close the file descriptor and
// unlink the partially copied shortid.
static void discard_bad_file(ShortId sid, int fd)
{
  int res;
  do
    { res = close(fd); }
  while ((res == -1) && (errno == EINTR));
  char* name = SourceOrDerived::shortIdToName(sid);
  res = unlink(name);
  int saved_errno = errno;
  if (res < 0)
    {
      Text err_txt = Basics::errno_Text(saved_errno);
      // Note: Message says ReplicateImmFile, which is the only
      // function calling this one
      Repos::dprintf(DBG_ALWAYS, "ReplicateImmFile: unlink %s: %s (errno = %d)\n",
		     name, err_txt.cchars(), saved_errno);
    }
  delete[] name;
}

//
// Get a local shortid for a copy of the remote immutableFile svs.
// This can be either an existing shortid with the same fingerprint,
// or a newly created copy.
//
// !How to properly protect this sid from weeding?  Perhaps should
// enter this routine with a lock held, release it if copying is
// needed, then reacquire it.  The repository's internal short-term
// lease would protect the sid between the end of the copying and the
// reacquisition.
//
static VestaSource::errorCode
ReplicateImmFile(VestaSource* svs, AccessControl::Identity swho,
		 ShortId* sid) throw ()
{
  ShortId newsid;
  VestaSource::errorCode err;

  // Check for existing copy
  newsid = GetFPShortId(svs->fptag);
  if (newsid != NullShortId) {
    *sid = newsid;
    return VestaSource::ok;
  }

  // Make new copy
  int newfd;
  try
    {
      newfd = SourceOrDerived::fdcreate(newsid);
    }
  catch (SourceOrDerived::Fatal f)
    {
      Repos::dprintf(DBG_ALWAYS,
		     "ReplicateImmFile: file creation error: %s "
		     "(maybe the repository server is running as "
		     "the wrong user?)\n",
		     f.msg.cchars());

      // Really it would be better to return some kind of "internal
      // error" here.  We use noPermission because the only case where
      // this can happen currently is if the repository is denied
      // permission to create a new shortid, which may mean that it's
      // running as the wrong user.
      return VestaSource::noPermission;
    }
  if (newfd < 0) {
    int saved_errno = errno;
    Text etxt = Basics::errno_Text(saved_errno);
    Repos::dprintf(DBG_REPLICATION,
		   "ReplicateImmFile: file creation error: %s\n", etxt.cchars());
    *sid = NullShortId;
    return Repos::errno_to_errorCode(saved_errno);
  }

  int res;
  bool did_readWhole = false;

  if(Should_readWhole(svs->host(), svs->port()))
    {
      OFdStream new_stream(newfd);
      try
	{
	  // Try the "send whole file" call (which also compresses on the
	  // wire).
	  err = svs->readWhole(new_stream, swho);
	  did_readWhole = true;
	}
      catch(SRPC::failure f)
	{
	  if((f.r == SRPC::version_skew) &&
	     (f.msg == "VestaSourceSRPC: Unknown proc_id"))
	    {
	      // We seem to be talking to an older repository without
	      // support for readWhole.  Fall back on the older (slow)
	      // method.  Remember that this peer doesn't support
	      // readWhole so we won't try it again for a while.
	      Repos::dprintf(DBG_REPLICATION,
			     "ReplicateImmFile: %s:%s doesn't seem to support "
			     "readWhole, falling back to old method\n",
			     svs->host().cchars(), svs->port().cchars());
	      Failed_readWhole(svs->host(), svs->port());
	    }
	  else if(new_stream.fail())
	    {
	      // A local write failure is a fatal error.  The most
	      // common cause is running out of disk space.
	      bool disk_full = false;
	      Text stream_emsg;
	      if(new_stream.previous_error() != 0)
		{
		  disk_full = (new_stream.previous_error() == ENOSPC);
		  stream_emsg = ": ";
		  stream_emsg += Basics::errno_Text(new_stream.previous_error());
		}
	      Repos::dprintf(DBG_REPLICATION,
			     "ReplicateImmFile: file write error%s\n",
			     stream_emsg.cchars());
	      discard_bad_file(newsid, newfd);
	      *sid = NullShortId;
	      return (disk_full
		      ? VestaSource::outOfSpace
		      : VestaSource::rpcFailure);
	    }
	  else
	    {
	      // Treat other failures as fatal.  (zlib problems could even
	      // have given us a partially written or currupted file in
	      // the new shortid.)
	      Repos::dprintf(DBG_REPLICATION,
			     "ReplicateImmFile: readWhole failed at %s:%s: %s\n",
			     svs->host().cchars(), svs->port().cchars(),
			     f.msg.cchars());
	      discard_bad_file(newsid, newfd);
	      *sid = NullShortId;
	      return VestaSource::rpcFailure;
	    }
	}
    }

  // If we should fall back on the old method..
  if(!did_readWhole)
    {
      try
	{
	  Basics::uint64 n = svs->size();
	  Basics::uint64 offset = 0;
	  while (n > 0) {
	    char buf[COPY_SIZE];
	    int len = COPY_SIZE;
	    if (len > n) len = n;
	    err = svs->read(buf, &len, offset, swho);
	    if (err != VestaSource::ok) {
	      Repos::dprintf(DBG_REPLICATION,
			     "ReplicateImmFile: file read error: %s\n",
			     VestaSource::errorCodeString(err));
	      discard_bad_file(newsid, newfd);
	      *sid = NullShortId;
	      return err;
	    }
	    if (len == 0) break;
	    do
	      len = write(newfd, buf, len);
	    while((len == -1) && (errno == EINTR));
	    if (len < 0) {
	      int saved_errno = errno;
	      Text etxt = Basics::errno_Text(saved_errno);
	      Repos::dprintf(DBG_REPLICATION,
			     "ReplicateImmFile: file write error: %s\n",
			     etxt.cchars());
	      discard_bad_file(newsid, newfd);
	      *sid = NullShortId;
	      return Repos::errno_to_errorCode(saved_errno);
	    }
	    n -= len;
	    offset += len;
	  }
	}
      catch (SRPC::failure f)
	{
	  Repos::dprintf(DBG_REPLICATION,
			 "ReplicateImmFile: file read SRPC failure: %s\n",
			 f.msg.cchars());
	  discard_bad_file(newsid, newfd);
	  *sid = NullShortId;
	  return VestaSource::rpcFailure;
	}
    }

  // Set shortid file mode, timestamp, etc.

  struct stat st;
  res = fstat(newfd, &st);
  assert(res != -1);

  // If something went wrong in copying the file we should have
  // already detected it.  However, just to be safe we check that the
  // written file has the same size as the original.
  Basics::uint64 n = svs->size();
  if(n != st.st_size)
    {
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateImmFile: written file has wrong size: "
		     "source was %" FORMAT_LENGTH_INT_64 "u bytes, "
		     "written was %" FORMAT_LENGTH_INT_64 "u bytes\n",
		     n, (Basics::uint64) st.st_size);
      discard_bad_file(newsid, newfd);
      // If the file we wrote is too small, say that we ran out of
      // disk space.  We don't know that's the cause for sure (and if
      // we did run out of disk space we should have detected it while
      // writing), but it's probably the most likely explanation.
      return ((n > st.st_size)
	      ? VestaSource::outOfSpace
	      : VestaSource::rpcFailure);
    }

  st.st_mode &= ~0222;  // make immutable
  if (svs->executable()) {
    st.st_mode |= 0111;  // make executable
  }
  res = fchmod(newfd, st.st_mode);
  assert(res != -1);
  // Before closing the file, flush all the data we just wrote to disk.
  res = fsync(newfd);
  assert(res != -1);
  do
    res = close(newfd);
  while ((res == -1) && (errno == EINTR));
  assert(res != -1);
  time_t ts = svs->timestamp();
  char *path = ShortIdBlock::shortIdToName(newsid);
  struct timeval tvp[2];
  tvp[0].tv_sec = ts;
  tvp[0].tv_usec = 0;
  tvp[1].tv_sec = ts;
  tvp[1].tv_usec = 0;
  res = utimes(path, tvp);
  assert(res != -1);
  *sid = newsid;
  delete [] path;
  return VestaSource::ok;
}

struct ReplicateImmDirClosure {
  VestaSource* spar;
  AccessControl::Identity swho;
  VestaSource* mpar;
  VestaSource::errorCode err;
};

/*forward*/ static VestaSource::errorCode
ReplicateImmDir(VestaSource* svs, AccessControl::Identity swho,
		VestaSource* mpar, Arc arc, VestaSource** mvsret) throw ();

static bool
ReplicateImmDirCallback(void* closure, VestaSource::typeTag type,
			Arc arc, unsigned int index, Bit32 pseudoInode,
			ShortId filesid, bool master) throw ()
{
  ReplicateImmDirClosure* cl = (ReplicateImmDirClosure*) closure;
  VestaSource* svs = NULL;
  VestaSource* mvs = NULL;
  bool ret = false;
  ReadersWritersLock* lock = NULL;

  if (type != VestaSource::deleted) {
    try {
      cl->err = cl->spar->lookupIndex(index, svs);
    } catch (SRPC::failure f) {
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateImmDir: lookupIndex SRPC failure: %s\n",
		     f.msg.cchars());
      cl->err = VestaSource::rpcFailure;
      goto done;
    }
    if (cl->err != VestaSource::ok) {
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateImmDirCallback: lookupIndex error: %s\n",
		     VestaSource::errorCodeString(cl->err));
      goto done;
    }
  }

  switch (type) {
  case VestaSource::immutableFile: {
    ShortId sid;
    cl->err = ReplicateImmFile(svs, cl->swho, &sid);
    if (cl->err != VestaSource::ok) {
      goto done;
    }
    // Need to relookup mpar here since lock was not held
    VestaSource* nmpar = cl->mpar->longid.lookup(LongId::writeLock, &lock);
    RWLOCK_LOCKED_REASON(lock,
			 "ReplicateImmDirCallback (relookup mpar for immutableFile)");
    if (nmpar == NULL) {
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateImmDirCallback: mutable parent relookup error\n");
      cl->err = VestaSource::notFound;
      goto done;
    }
    cl->err = nmpar->insertFile(arc, sid, false, NULL,
				VestaSource::replaceDiff,
				NULL, svs->timestamp(), &svs->fptag);
    delete nmpar;
    if (cl->err != VestaSource::ok) {
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateImmDirCallback: insert file error %s\n",
		     VestaSource::errorCodeString(cl->err));
      goto done;
    }
    break; }

  case VestaSource::immutableDirectory: {
    cl->err = ReplicateImmDir(svs, cl->swho, cl->mpar, arc, &mvs);
    if (cl->err != VestaSource::ok) {
      goto done;
    }

    // Make the replica immutable and set its fingerprint
    // Need to relookup both cl->mpar and mvs
    VestaSource* nmpar = cl->mpar->longid.lookup(LongId::writeLock, &lock);
    RWLOCK_LOCKED_REASON(lock,
			 "ReplicateImmDirCallback (make immutable, set fingerprint)");
    if (nmpar == NULL) {
      cl->err = VestaSource::notFound;
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateImmDirCallback: mutable parent relookup error: %s\n", 
		     VestaSource::errorCodeString(cl->err));
      goto done;
    }
    VestaSource* nmvs = mvs->longid.lookup(LongId::checkLock, &lock);
    if (nmvs == NULL) {
      cl->err = VestaSource::notFound;
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateImmDir: mutable child relookup error: %s\n", 
		     VestaSource::errorCodeString(cl->err));
      delete nmpar;
      goto done;
    }
    cl->err = nmpar->insertImmutableDirectory(arc, nmvs, false, NULL,
					      VestaSource::replaceDiff, NULL,
					      svs->timestamp(), &svs->fptag);
    delete nmpar;
    delete nmvs;
    if (cl->err != VestaSource::ok) {
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateImmDirCallback: reinsert directory error: %s\n",
		     VestaSource::errorCodeString(cl->err));
      goto done;
    }
    break; }

  case VestaSource::deleted: {
    // Need to relookup mpar here since lock was not held
    VestaSource* nmpar = cl->mpar->longid.lookup(LongId::writeLock, &lock);
    RWLOCK_LOCKED_REASON(lock, "ReplicateImmDirCallback (relookup mpar for deleted)");
    if (nmpar == NULL) {
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateImmDirCallback: mutable parent relookup error\n");
      cl->err = VestaSource::notFound;
      goto done;
    }
    cl->err = nmpar->reallyDelete(arc, NULL, false);
    delete nmpar;
    if (cl->err != VestaSource::ok) {
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateImmDirCallback: deletion error %s\n",
		     VestaSource::errorCodeString(cl->err));
      goto done;
    }
    break;}

  default:
    assert(false);
  }
  ret = true;
 done:
  if (svs) delete svs;
  if (mvs) delete mvs;
  if (lock) lock->release();
  return ret;
}

//
// Insert a copy of immutable directory svs into mpar under the name
// arc, and return a VestaSource object for it in mvs.
// Lock is not held on entry; it is acquired and released as needed.
//
static VestaSource::errorCode
ReplicateImmDir(VestaSource* svs, AccessControl::Identity swho,
		VestaSource* mpar, Arc arc, VestaSource** mvsret) throw ()
{
  VestaSource::errorCode err = VestaSource::ok;
  VestaSource* evs = NULL;
  VestaSource* mvs = NULL;
  ReadersWritersLock* lock = &StableLock;
  VestaSource* dbase = NULL;

  lock->acquireWrite();
  RWLOCK_LOCKED_REASON(lock, "ReplicateImmDir (use existing)");
  // Look for existing copy of directory
  ShortId sid = GetFPShortId(svs->fptag);
  if (sid != NullShortId) {
    // Insert existing copy of directory
    evs = LongId::fromShortId(sid).lookup(LongId::checkLock, &lock);
    assert(evs != NULL);
    // Need to relookup mpar here since lock was not held
    VestaSource* nmpar = mpar->longid.lookup(LongId::checkLock, &lock);
    if (nmpar == NULL) {
      err = VestaSource::notFound;
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateImmDir: mutable parent relookup error: %s\n", 
		     VestaSource::errorCodeString(err));
      goto done;
    }
    err = nmpar->insertImmutableDirectory(arc, evs, false, NULL,
					  VestaSource::replaceDiff,
					  &mvs, svs->timestamp(), &svs->fptag);
    delete nmpar;
  } else {
    // Copy the directory 

    // Does source have a base?
    VestaSource* sbase;
    // Release lock while doing remote operation
    lock->release();
    lock = NULL;
    try {
      err = svs->getBase(sbase, swho);
    } catch (SRPC::failure f) {
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateImmDir: getBase SRPC failure: %s\n", f.msg.cchars());
      err = VestaSource::rpcFailure;
      goto done;
    }
    lock = &StableLock;
    lock->acquireWrite();
    RWLOCK_LOCKED_REASON(lock, "ReplicateImmDir (copy)");
    if (err == VestaSource::ok) {
      // Yes, check to see if we have a copy of the base already
      sid = GetFPShortId(sbase->fptag);
      delete sbase;
      if (sid != NullShortId) {
	// Yes, set dbase to it
	dbase = LongId::fromShortId(sid).lookup(LongId::checkLock, &lock);
	assert(dbase != NULL);
      }
    }
    // Need to relookup mpar here since lock was not held
    VestaSource* nmpar = mpar->longid.lookup(LongId::checkLock, &lock);
    if (nmpar == NULL) {
      err = VestaSource::notFound;
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateImmDir: mutable parent relookup error: %s\n", 
		     VestaSource::errorCodeString(err));
      goto done;
    }
    err = nmpar->insertMutableDirectory(arc, dbase, false, NULL,
					VestaSource::replaceDiff, &mvs,
					svs->timestamp());
    delete nmpar;
    if (err != VestaSource::ok) {
      Repos::dprintf(DBG_REPLICATION, "ReplicateImmDir: insert directory error: %s\n",
		     VestaSource::errorCodeString(err));
      goto done;
    }

    // Release lock while doing remote operation
    lock->release();
    lock = NULL;

    // Copy the children
    ReplicateImmDirClosure cl;
    cl.spar = svs;
    cl.swho = swho;
    cl.mpar = mvs;
    cl.err = VestaSource::ok;
    try {
      err = svs->list(0, ReplicateImmDirCallback, &cl, swho, (dbase != NULL));
    } catch (SRPC::failure f) {
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateImmDir: list directory SRPC failure: %s\n",
		     f.msg.cchars());
      err = VestaSource::rpcFailure;
      goto done;
    }
    if (err != VestaSource::ok) {
      Repos::dprintf(DBG_REPLICATION, "ReplicateImmDir: list directory error: %s\n",
		     VestaSource::errorCodeString(err));
      goto done;
    }
    if (cl.err != VestaSource::ok) {
      err = cl.err;
      goto done;
    }
  }
 done:
  if (lock) lock->release();
  if (evs) delete evs;
  if (dbase) delete dbase;
  if (err == VestaSource::ok) {
    *mvsret = mvs;
  } else {
    if (mvs) delete mvs;
  }
  return err;
}

static bool
ReplicationCleanupCallback(void* closure, VestaSource::typeTag type,
			   Arc arc, unsigned int index, Bit32 pseudoInode,
			   ShortId filesid, bool master) throw ()
{
  VestaSource* mpar = (VestaSource*) closure;
  mpar->reallyDelete(arc);
  return true;
}

// Called during repository initialization, after recovery but before
// VestaSourceRPC interface is exported.  Deletes any mutable
// directories left over from unfinished Replicate calls.
void
ReplicationCleanup() throw ()
{
  VestaSource* mroot;
  VestaSource* mpar;
  ReadersWritersLock* lock;
  VestaSource::errorCode err;
  
  mroot = VestaSource::mutableRoot(LongId::writeLock, &lock);
  RWLOCK_LOCKED_REASON(lock, "ReplicationCleanup");
  err = mroot->lookup(REPLICATOR_DIR, mpar, NULL);
  if (err == VestaSource::ok) {
    mpar->list(0, ReplicationCleanupCallback, mpar);
  }
  lock->release();
  delete mpar;
}

VestaSource::errorCode
Replicate(const char* pathname, bool asStub, bool asGhost,
	  const char* srcHost, const char* srcPort, char pathnameSep,
	  AccessControl::Identity dwho, AccessControl::Identity swho) throw ()
{
  if (asStub && asGhost) return VestaSource::invalidArgs;
  VestaSource::errorCode err = VestaSource::ok;
  VestaSource* droot = NULL;
  VestaSource* dpar = NULL;
  VestaSource* dvs = NULL;
  VestaSource* sroot = NULL;
  VestaSource* svs = NULL;
  VestaSource* mroot = NULL;
  VestaSource* mpar = NULL;
  VestaSource* mvs = NULL;
  char mname[FP::ByteCnt*2+1];
  ShortId sid = NullShortId;
  ReadersWritersLock* lock = NULL;  

  //
  // Split pathname into head (name of parent) and tail (new arc)
  //
  char* head = strdup(pathname);
  const char* tail;
  char* p = strrchr(head, pathnameSep);
  if (p) {
    // Head is nonempty
    *p = '\000';
    tail = p + 1;
  } else {
    // Head is empty
    *head = '\000';
    tail = pathname;
  }

  //
  // Initial lookup of dpar and dvs under read lock to check for
  // permissions and simple caller errors.
  //
  droot = VestaSource::repositoryRoot(LongId::readLock, &lock);
  RWLOCK_LOCKED_REASON(lock, "Replicate (initial lookup, permissions check)");
  if (*head == '\000') {
    dpar = droot;
  } else {
    err = droot->lookupPathname(head, dpar, dwho, pathnameSep);
    if (err != VestaSource::ok) {
      Repos::dprintf(DBG_REPLICATION,
		     "Replicate: \"%s\" destination parent lookup error: %s\n", 
		     pathname, VestaSource::errorCodeString(err));
      goto done;
    }
  }
  err = dpar->lookup(tail, dvs, dwho);
  if (err != VestaSource::ok && err != VestaSource::notFound) {
    Repos::dprintf(DBG_REPLICATION,
		   "Replicate: \"%s\" old object lookup error: %s\n", 
		   pathname, VestaSource::errorCodeString(err));
    goto done;
  }    

  //
  // Check for caller errors
  //
  if (dpar->type == VestaSource::immutableDirectory) {
    err = VestaSource::inappropriateOp;
    Repos::dprintf(DBG_REPLICATION,
		   "Replicate: \"%s\" destination parent is immutable\n", pathname);
    goto done;
  }
  if (dvs) {
    if ((asStub && (dvs->type != VestaSource::ghost)) || asGhost ||
	(dvs->type != VestaSource::stub && dvs->type != VestaSource::ghost) ||
	(dvs->type == VestaSource::stub && dvs->master)) {
      err = VestaSource::nameInUse;
      Repos::dprintf(DBG_REPLICATION,
		     "Replicate: \"%s\" destination object exists\n", pathname);
      goto done;
    }
  }

  //
  // Check permission
  //
  if (!ReplicationAccessCheck((dvs ? dvs : dpar),
			      "#replicate-from", srcHost, srcPort) &&
      !ReplicationAccessCheck((dvs ? dvs : dpar),
			      "#replicate-from-noac", srcHost, srcPort)) {
    Repos::dprintf(DBG_REPLICATION,
		   "Replicate: \"%s\" no permission to replicate from %s:%s\n",
		   pathname, srcHost, srcPort);
    err = VestaSource::noPermission;
    goto done;
  }

  //
  // Release lock during remote operations
  //
  if (dpar != droot) delete dpar;
  dpar = NULL;
  if (dvs) delete dvs;
  dvs = NULL;
  lock->release();
  lock = NULL;

  //
  // Look up the object in src, yielding svs.
  //
  try {
    sroot = VDirSurrogate::repositoryRoot(srcHost, srcPort);
    err = sroot->lookupPathname(pathname, svs, swho, pathnameSep);

  } catch (SRPC::failure f) {
    Repos::dprintf(DBG_REPLICATION,
		   "Replicate: \"%s\" initial RPC to %s:%s failed: %s\n", 
		   pathname, srcHost, srcPort, f.msg.cchars());
    err = VestaSource::rpcFailure;
    goto done;
  }
  if (err != VestaSource::ok) {
    Repos::dprintf(DBG_REPLICATION, "Replicate: \"%s\" remote lookup failed: %s\n", 
		   pathname, VestaSource::errorCodeString(err));
    goto done;
  }
  
  //
  // Determine type of new object in dst
  //
  VestaSource::typeTag ntype;
  if (asStub) {
    ntype = VestaSource::stub;
  } else if (asGhost) {
    ntype = VestaSource::ghost;
  } else {
    ntype = svs->type;
  }

  //
  // Copy svs if needed
  //
  if (ntype == VestaSource::immutableFile) {
    // Look up the fingerprint to check for an existing local
    // replica; if none, copy the remote file.

    err = ReplicateImmFile(svs, swho, &sid);
    if (err != VestaSource::ok) goto done;

  } else if (ntype == VestaSource::immutableDirectory) {
    // 
    // Walk over the tree below svs and make a local copy as
    // a mutable directory tree.  For each node, look up the
    // fingerprint to check for an existing local replica before
    // copying.  For immutable directories that must be copied, look
    // up the fingerprint of the base to decide whether to do a full
    // copy or delta copy.  We need to avoid holding the local lock
    // during remote operations, especially file copies, so we do an
    // acquire/lookup/insert/release cycle for each change to the
    // local tree.
    //

    //
    // Determine where to put the copy
    //
    mroot = VestaSource::mutableRoot(LongId::writeLock, &lock);
    RWLOCK_LOCKED_REASON(lock, "Replicate (determine where to put the copy)");
    err = mroot->lookup(REPLICATOR_DIR, mpar, NULL);
    if (err == VestaSource::notFound) {
      err = mroot->insertMutableDirectory(REPLICATOR_DIR, NULL, true, NULL,
					  VestaSource::dontReplace, &mpar);
      if (err == VestaSource::ok) {
	err = mpar->setAttrib("#mode", "000");
      }
    }
    if (err != VestaSource::ok) {
      Repos::dprintf(DBG_REPLICATION,
		     "Replicate: \"%s\" lookup or creation error: %s\n", 
		     REPLICATOR_DIR, VestaSource::errorCodeString(err));
      goto done;
    }
    lock->release();
    lock = NULL;
    
    FP::Tag fp = UniqueId();
    sprintf(mname,
	    "%016" FORMAT_LENGTH_INT_64 "x"
	    "%016" FORMAT_LENGTH_INT_64 "x",
	    fp.Word0(), fp.Word1());

    err = ReplicateImmDir(svs, swho, mpar, mname, &mvs);
    if (err != VestaSource::ok) goto done;
  }

  // Take write lock, lookup destination parent dpar
  droot = VestaSource::repositoryRoot(LongId::writeLock, &lock);
  RWLOCK_LOCKED_REASON(lock, "Replicate (destination parent checks)");
  if (*head == '\000') {
    dpar = droot;
  } else {
    err = droot->lookupPathname(head, dpar, dwho, pathnameSep);
    if (err != VestaSource::ok) {
      Repos::dprintf(DBG_REPLICATION,
		     "Replicate: \"%s\" destination parent lookup error: %s\n", 
		     pathname, VestaSource::errorCodeString(err));
      goto done;
    }
  }

  // Before inserting the new dvs, look up the old dvs under the lock
  // to do final checking.
  err = dpar->lookup(tail, dvs, dwho);
  if (err != VestaSource::ok && err != VestaSource::notFound) {
    Repos::dprintf(DBG_REPLICATION,
		   "Replicate: \"%s\" old object relookup error: %s\n", 
		   pathname, VestaSource::errorCodeString(err));
    goto done;
  }    

  //
  // Check for svs vs. dvs agreement violations -- not done earlier
  //
  if (dvs &&
      ((svs->master && dvs->master) ||
       (svs->type == VestaSource::stub && svs->master &&
	dvs->type != VestaSource::stub && dvs->type != VestaSource::ghost) ||
       (dvs->type == VestaSource::stub && dvs->master &&
	svs->type != VestaSource::stub && svs->type != VestaSource::ghost) ||
       (svs->type != VestaSource::stub && svs->type != VestaSource::ghost &&
	dvs->type != VestaSource::stub && dvs->type != VestaSource::ghost &&
	svs->type != dvs->type))) {
    err = VestaSource::inappropriateOp;
    Repos::dprintf(DBG_REPLICATION,
		   "Replicate: \"%s\" agreement violation, source %s %s vs. dest %s %s\n",
		   pathname, svs->master ? "master" : "nonmaster",
		   VestaSource::typeTagString(svs->type),
		   dvs->master ? "master" : "nonmaster",
		   VestaSource::typeTagString(dvs->type));
    goto done;    
  }

  //
  // Finally, insert the new object in its appendable parent
  //
  switch (ntype) {
  case VestaSource::stub:
    err = dpar->insertStub(tail, false, NULL,
			   VestaSource::replaceDiff, NULL, svs->timestamp());
    break;
  case VestaSource::ghost:
    err = dpar->insertGhost(tail, (dvs && dvs->master), NULL,
			    VestaSource::replaceDiff, NULL, svs->timestamp());
    break;
  case VestaSource::immutableFile:
    err = dpar->insertFile(tail, sid, (dvs && dvs->master), NULL,
			   VestaSource::replaceDiff, NULL, svs->timestamp(),
			   &svs->fptag);
    break;
  case VestaSource::immutableDirectory: {
    // Need to relookup mvs because we released and reacquired the lock
    VestaSource* nmvs = mvs->longid.lookup(LongId::checkLock, &lock);
    if (nmvs == NULL) {
      err = VestaSource::notFound;
      Repos::dprintf(DBG_REPLICATION,
		     "Replicate: \"%s\" temporary copy relookup error: %s\n", 
		     pathname, VestaSource::errorCodeString(err));
      goto done;
    }
    err = dpar->insertImmutableDirectory(tail, nmvs, (dvs && dvs->master),
					 NULL, VestaSource::replaceDiff,
					 NULL, svs->timestamp(), &svs->fptag);
    delete nmvs;
    // Need to relookup mpar because we released and reacquired the lock
    VestaSource* nmpar = mpar->longid.lookup(LongId::checkLock, &lock);
    if (nmpar == NULL) {
      err = VestaSource::notFound;
      Repos::dprintf(DBG_REPLICATION,
		     "Replicate: \"%s\" relookup error: %s\n", 
		     REPLICATOR_DIR, VestaSource::errorCodeString(err));
      goto done;
    }
    nmpar->reallyDelete(mname);
    delete nmpar;
    break; }
  case VestaSource::appendableDirectory:
    err = dpar->insertAppendableDirectory(tail, false, NULL,
					  VestaSource::replaceDiff,
					  NULL, svs->timestamp());
    break;
  default:
    assert(false);
    break;
  }      
  if (err != VestaSource::ok) {
    Repos::dprintf(DBG_REPLICATION,
		   "Replicate: \"%s\" destination insert error: %s\n", 
		   pathname, VestaSource::errorCodeString(err));
    goto done;
  }

 done:
  if (mvs) delete mvs;
  if (mpar) delete mpar;
  if (svs) delete svs;
  if (sroot) delete sroot;
  if (dvs) delete dvs;
  if (dpar && dpar != droot) delete dpar;
  // do not delete droot
  if (lock) lock->release();
  free(head);
  return err;
}


struct ReplicateAttribsClosure {
  VestaSource::errorCode err;
  VestaSource* dvs;
  bool includeAccess;
};


static bool
ReplicateAttribsCallback(void* closure, VestaAttribs::attribOp op,
			 const char* name, const char* value,
			 time_t timestamp)
{
  ReplicateAttribsClosure* cl = (ReplicateAttribsClosure*) closure;
  VestaSource* ndvs = NULL;
  bool ret = false;
  ReadersWritersLock* lock = NULL;

  if (cl->includeAccess || name[0] != '#') {
    // Need to relookup dvs
    ndvs = cl->dvs->longid.lookup(LongId::writeLock, &lock);
    RWLOCK_LOCKED_REASON(lock, "ReplicateAttribsCallback");
    if (ndvs == NULL) {
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateAttribsCallback: object relookup error\n");
      cl->err = VestaSource::notFound;
      goto done;
    }
    cl->err = ndvs->writeAttrib(op, name, value, NULL, timestamp);
    if (cl->err != VestaSource::ok) {
      Repos::dprintf(DBG_REPLICATION,
		     "ReplicateAttribsCallback: writeAttrib error: %s\n",
		     VestaSource::errorCodeString(cl->err));
      goto done;
    }
  }
  ret = true;
 done:
  if (ndvs) delete ndvs;
  if (lock) lock->release();
  return ret;
}

VestaSource::errorCode
ReplicateAttribs(const char* pathname, bool includeAccess,
		 const char* srcHost, const char* srcPort, char pathnameSep,
		 AccessControl::Identity dwho, AccessControl::Identity swho)
  throw ()
{
  VestaSource::errorCode err = VestaSource::ok;
  VestaSource* droot = NULL;
  VestaSource* dvs = NULL;
  VestaSource* sroot = NULL;
  VestaSource* svs = NULL;
  ReadersWritersLock* lock = NULL;  

  //
  // Look up the object in src, yielding svs.
  //
  try {
    sroot = VDirSurrogate::repositoryRoot(srcHost, srcPort);
    err = sroot->lookupPathname(pathname, svs, swho, pathnameSep);

  } catch (SRPC::failure f) {
    Repos::dprintf(DBG_REPLICATION,
		   "ReplicateAttribs: \"%s\" initial RPC to %s:%s failed: %s\n", 
		   pathname, srcHost, srcPort, f.msg.cchars());
    err = VestaSource::rpcFailure;
    goto done;
  }
  if (err != VestaSource::ok) {
    Repos::dprintf(DBG_REPLICATION,
		   "ReplicateAttribs: \"%s\" source object lookup error: %s\n", 
		   pathname, VestaSource::errorCodeString(err));
    goto done;
  }
 
  //
  // Look up the object locally, yielding dvs
  //
  droot = VestaSource::repositoryRoot(LongId::readLock, &lock);
  RWLOCK_LOCKED_REASON(lock, "ReplicateAttribs");
  err = droot->lookupPathname(pathname, dvs, dwho, pathnameSep);
  if (err != VestaSource::ok) {
    Repos::dprintf(DBG_REPLICATION,
		   "ReplicateAttribs: \"%s\" destination object lookup error: %s\n", 
		   pathname, VestaSource::errorCodeString(err));
    goto done;
  }
  if ((dvs->type != svs->type) &&
      // We'll allow attributes to be replicated from a source
      // anything to a destination stub or ghost.  (Note that ghosts
      // normally preserve the attributes of the original, and real
      // objects normally preserve the attributes of stubs they
      // replace.)
      (dvs->type != VestaSource::ghost) && (dvs->type != VestaSource::stub)) {
    err = VestaSource::inappropriateOp;
    Repos::dprintf(DBG_REPLICATION,
		   "ReplicateAttribs: \"%s\" object type mismatch\n", pathname);
    goto done;
  }

  //
  // Check permission
  //
  if (!ReplicationAccessCheck(dvs, "#replicate-from", srcHost, srcPort) &&
      (includeAccess || !ReplicationAccessCheck(dvs, "#replicate-from-noac",
						srcHost, srcPort))) {
    Repos::dprintf(DBG_REPLICATION,
		   "Replicate: \"%s\" no permission to replicate from %s:%s\n",
		   pathname, srcHost, srcPort);
    err = VestaSource::noPermission;
    goto done;
  }

  // Release lock during remote operation
  lock->release();
  lock = NULL;

  //
  // Enumerate the remote attrib history and replicate each tuple
  //
  ReplicateAttribsClosure cl;
  cl.err = VestaSource::ok;
  cl.dvs = dvs;
  cl.includeAccess = includeAccess;
  try {
    svs->getAttribHistory(ReplicateAttribsCallback, &cl);
  } catch (SRPC::failure f) {
    Repos::dprintf(DBG_REPLICATION,
		   "ReplicateAttribs: \"%s\" getAttribHistory SRPC failure: %s\n",
		   pathname, f.msg.cchars());
    err = VestaSource::rpcFailure;
    goto done;
  }
  err = cl.err;

 done:
  if (lock) lock->release();
  if (svs) delete svs;
  if (sroot) delete sroot;
  if (dvs) delete dvs;
  // do not delete droot
  return err;
}
