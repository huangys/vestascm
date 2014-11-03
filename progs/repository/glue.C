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
// glue.C
//
// Glue code between nfsd.C and repository implementation
//

#if __linux__
#include <stdint.h>
#endif
#include <pthread.h>
#include <errno.h>
#include <iomanip>
#include "VestaSource.H"
#include "ShortIdBlock.H"
#include "FdCache.H"
#include "VRConcurrency.H"
#include "VLogHelp.H"
#include "VestaConfig.H"
#include "Mastership.H"
#include "nfsd.H"
#include "logging.H"
#include <assert.h>
#include "VestaSourceImpl.H"
#include "CharsSeq.H"
#include "CopyShortId.H"

#include "timing.H"
#include "lock_timing.H"

#include <BufStream.H>

#include "ReposConfig.h"

// On some systems, we need sys/param.h and sys/mount.h to get statfs
#if defined(HAVE_SYS_PARAM_H)
#include <sys/param.h>
#endif
#if defined(HAVE_SYS_MOUNT_H)
#include <sys/mount.h>
#endif
// We prefer statvfs if available
#if defined(HAVE_SYS_STATVFS_H)
#include <sys/statvfs.h>
#elif defined(HAVE_SYS_STATFS_H)
#include <sys/statfs.h>
#endif

using std::ostream;
using std::endl;
using std::setw;
using std::setfill;
using std::hex;
using std::dec;
using Basics::OBufStream;

// Import
extern int nfs_port;

#define VR_FSID nfs_port          // arbitrary value
#define VR_RDEV 9999              // arbitrary value
#define VR_BLOCKSIZE NFS_MAXDATA  // optimal block size for i/o
#define VR_BLOCKUNIT 512          // unit for st_blocks (POSIX standard)
#define VR_DEFAULT_COW_MAX 4      // max copy-on-writes in progress at once

// Global
bool shortIdSymlink = false;  // implement shortids as symlinks
Text shortIdSymlinkPrefix;
int shortIdSymlinkLength = 0;
bool allowSymlink = false;  // allow symlink() through NFS interface
bool bsdChown = false;

// Ensures the same file is not copied twice by different threads.
static Basics::mutex cowLock;
static Basics::cond cowCond;
int cowMax = VR_DEFAULT_COW_MAX;
struct CowInProgress {
  bool active;
  LongId longid;
};
CowInProgress *cowInProgress;

const struct {
	nfsstat error;
	int errno_val;
} nfs_errtbl[]= {
	{ NFS_OK,		0		},
	{ NFSERR_PERM,		EPERM		},
	{ NFSERR_NOENT,		ENOENT		},
	{ NFSERR_IO,		EIO		},
	{ NFSERR_NXIO,		ENXIO		},
	{ NFSERR_ACCES,		EACCES		},
	{ NFSERR_EXIST,		EEXIST		},
	{ NFSERR_NODEV,		ENODEV		},
	{ NFSERR_NOTDIR,	ENOTDIR		},
	{ NFSERR_ISDIR,		EISDIR		},
	{ NFSERR_INVAL,		EINVAL		},
	{ NFSERR_FBIG,		EFBIG		},
	{ NFSERR_NOSPC,		ENOSPC		},
	{ NFSERR_ROFS,		EROFS		},
	{ NFSERR_NAMETOOLONG,	ENAMETOOLONG	},
	{ NFSERR_NOTEMPTY,	ENOTEMPTY	},
#ifdef EDQUOT
	{ NFSERR_DQUOT,		EDQUOT		},
#endif
	{ NFSERR_STALE,		ESTALE		},
	{ NFSERR_WFLUSH,	EIO		},
	{ (nfsstat) -1,		EIO		}
};

/* Lookup a UNIX error code and return NFS equivalent. */
nfsstat
xlate_errno(int errno_val)
{
    int i;

    for (i = 0; nfs_errtbl[i].error != -1; i++) {
	if (nfs_errtbl[i].errno_val == errno_val)
	  return (nfs_errtbl[i].error);
    }
    Text etxt = Basics::errno_Text(errno_val);
    Repos::dprintf(DBG_ALWAYS, "non-standard errno: %d (%s)\n",
		   errno_val, etxt.cchars());
    return (NFSERR_IO);
}

nfsstat
xlate_vserr(VestaSource::errorCode err)
{
    switch (err) {
      case VestaSource::ok:
	return NFS_OK;
      case VestaSource::notFound:
	return NFSERR_NOENT;
      case VestaSource::noPermission:
	return NFSERR_ACCES;
      case VestaSource::nameInUse:
	return NFSERR_EXIST;
      case VestaSource::inappropriateOp:
      case VestaSource::notMaster:
	// Perhaps questionable message choice.  Rationale: these
	// errors are usually an attempt to mutate something immutable.
	return NFSERR_ROFS;
      case VestaSource::nameTooLong:
	return NFSERR_NAMETOOLONG;
      case VestaSource::rpcFailure:
	return NFSERR_IO;
      case VestaSource::notADirectory:
	return NFSERR_NOTDIR;
      case VestaSource::isADirectory:
	return NFSERR_ISDIR;
      case VestaSource::invalidArgs:
      case VestaSource::longIdOverflow:
	return NFSERR_INVAL;
      case VestaSource::outOfSpace:
	return NFSERR_NOSPC;
      default:
	return NFSERR_NXIO;
    }
}


struct ToUnixIdClosure {
  uid_t id;
  bool setid;
  VestaSource* vs;
};

static bool
toUnixUidCallback(void* closure, const char* value)
{
  ToUnixIdClosure* cl = (ToUnixIdClosure*) closure;
  const char* at = strchr(value, '@');
  if (at && strcasecmp(at + 1, AccessControl::realm) == 0) {
    cl->id = AccessControl::globalToUnixUser(value);
    cl->setid = cl->vs->inAttribs("#setuid", value);
    return false;
  }
  return true;
}

static bool
toUnixGidCallback(void* closure, const char* value)
{
  ToUnixIdClosure* cl = (ToUnixIdClosure*) closure;
  const char* at = strchr(value, '@');
  if (at && strcasecmp(at + 1, AccessControl::realm) == 0) {
    cl->id = AccessControl::globalToUnixGroup(value);
    cl->setid = cl->vs->inAttribs("#setgid", value);
    return false;
  }
  return true;
}


void
file_fattr(fattr* attr, struct stat* st, VestaSource* vs)
{
    // Kludge: some side effects on vs->ac
    if (RootLongId.isAncestorOf(vs->longid)) {
	// Immutable; turn off write access
	vs->ac.mode &= ~0222;
    } else if (FileShortIdRootLongId.isAncestorOf(vs->longid) &&
	       (st->st_mode & 0222) == 0) {
	// Old file in volatileROEDirectory, immutable
	vs->ac.mode &= ~0222;
	vs->type = VestaSource::immutableFile;
    }
    attr->mode = NFSMODE_REG | vs->ac.mode;
    if (st->st_mode & 0111) {
	// Set execute access same as read access
	attr->mode = (attr->mode & ~0111) | ((attr->mode & 0444) >> 2);
    } else {
	// Turn off execute access
	attr->mode &= ~0111;
    }

    // Find local #owner if any, and see if it is on #setuid list
    ToUnixIdClosure cl;
    cl.id = AccessControl::vforeignUser;
    cl.setid = false;
    cl.vs = vs;
    RECORD_TIME_POINT;
    vs->ac.owner.getAttrib("#owner", toUnixUidCallback, &cl);
    RECORD_TIME_POINT;
    attr->uid = cl.id;
    if (cl.setid) attr->mode |= 04000;

    // Find local #group if any, and see if it is on #setgid list
    cl.id = AccessControl::vforeignGroup;
    cl.setid = false;
    cl.vs = vs;
    RECORD_TIME_POINT;
    vs->ac.group.getAttrib("#group", toUnixGidCallback, &cl);
    RECORD_TIME_POINT;
    attr->gid = cl.id;
    if (cl.setid) attr->mode |= 02000;

    attr->type = NFREG;
    attr->nlink = vs->linkCount();
    attr->size = st->st_size;
    attr->blocksize = st->st_blksize;
    attr->rdev = VR_RDEV;
    attr->blocks = st->st_blocks;
    attr->fsid = VR_FSID;
    attr->fileid = vs->pseudoInode;
    attr->atime.seconds = st->st_atime;
    attr->mtime.seconds = st->st_mtime;
    attr->ctime.seconds = st->st_ctime;
#if __digital__
    attr->atime.useconds = st->st_spare1;
    attr->mtime.useconds = st->st_spare2;
    attr->ctime.useconds = st->st_spare3;
#else
    attr->atime.useconds = 0;
    attr->mtime.useconds = 0;
    attr->ctime.useconds = 0;
#endif
}

// Utility to get last modified time of attributes
typedef struct {
    time_t time;
} AttrModTimeClosure;

bool
attrModTimeCallback(void* closure, VestaSource::attribOp op, const char* name,
		    const char* value, time_t timestamp)
{
    AttrModTimeClosure *cl = (AttrModTimeClosure *) closure;
    if (cl->time < timestamp) cl->time = timestamp;
    return true;
}

time_t
attrModTime(VestaSource *vs)
{
    AttrModTimeClosure cl;
    cl.time = 2;  // arbitrary nonzero value
    vs->getAttribHistory(attrModTimeCallback, &cl);
    return cl.time;
}

// If vs is a file, fd may be either -1 or an open file descriptor
nfsstat
any_fattr(fattr* attr, VestaSource* vs, int fd)
{
    struct stat st;
    int res;
    char *path;
    const char *linkval;
    ToUnixIdClosure cl;
    
    switch (vs->type) {
      case VestaSource::immutableFile:
      case VestaSource::mutableFile:
	{
	  bool close_fd = false;
	  FdCache::OFlag ofl;
	  if (fd == -1)
	    {
	      fd = FdCache::tryopen(vs->shortId(), FdCache::any, &ofl);
	      close_fd = (fd != -1);
	    }
	  if (fd == -1)
	    {
	      // No file descriptor provided and none in the FdCache:
	      // use stat.
	      char *sid_fname = ShortIdBlock::shortIdToName(vs->shortId());
	      RECORD_TIME_POINT;
	      res = stat(sid_fname, &st);
	      RECORD_TIME_POINT;
	      delete [] sid_fname;
	    }
	  else
	    {
	      // We have a file descriptor: use fstat
	      RECORD_TIME_POINT;
	      res = fstat(fd, &st);
	      RECORD_TIME_POINT;
	    }
	  // Return the file descriptor to the FdCache if we got it
	  // from there.
	  if(close_fd)
	    FdCache::close(vs->shortId(), fd, ofl);
	}
	if (res < 0) {
	    return xlate_errno(errno);
	}

	if (shortIdSymlink &&
	    FileShortIdRootLongId.isAncestorOf(vs->longid) &&
	    (st.st_mode & 0222) == 0) {
	    // Manifest as symlink
	    attr->type = NFLNK;
	    attr->mode = NFSMODE_LNK | 0444;
	    attr->nlink = 1;
	    attr->uid = 0;
	    attr->gid = 0;
	    attr->size = shortIdSymlinkLength;
	    attr->blocksize = VR_BLOCKSIZE;
	    attr->rdev = VR_RDEV;
	    attr->blocks = (attr->size + VR_BLOCKUNIT - 1)/VR_BLOCKUNIT;
	    attr->fsid = VR_FSID;
	    attr->fileid = vs->pseudoInode;
	    attr->atime.seconds = attr->mtime.seconds = 
	      attr->ctime.seconds = 2;    // arbitrary nonzero value
	    attr->atime.useconds = attr->mtime.useconds =
	      attr->ctime.useconds = 0;
	    break;
	}
	file_fattr(attr, &st, vs);
	break;
	
      case VestaSource::device:
	attr->type = NFCHR;
	attr->mode = NFSMODE_CHR | 0666;
	attr->nlink = 1;
	attr->uid = 0;
	attr->gid = 0;
	attr->size = 0;
	attr->blocksize = VR_BLOCKSIZE;
	attr->rdev = vs->shortId(); // device number, not a real shortId
	attr->blocks = 0;
	attr->fsid = VR_FSID;
	attr->fileid = vs->pseudoInode;
	attr->atime.seconds = attr->mtime.seconds = 
	  attr->ctime.seconds = 2;    // arbitrary nonzero value
	attr->atime.useconds = attr->mtime.useconds =
	  attr->ctime.useconds = 0;
	break;
	
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
	attr->type = NFDIR;
	attr->mode = NFSMODE_DIR | vs->ac.mode;
	if (vs->type == VestaSource::immutableDirectory
	    && RootLongId.isAncestorOf(vs->longid)) {
	    // Turn off write access
	    attr->mode &= ~0222;  
	}
	attr->nlink = 1;

	// Find local #owner if any
	cl.id = AccessControl::vforeignUser;
	cl.setid = false;
	cl.vs = vs;
	vs->ac.owner.getAttrib("#owner", toUnixUidCallback, &cl);
	attr->uid = cl.id;

	// Find local #group if any
	cl.id = AccessControl::vforeignGroup;
	cl.setid = false;
	cl.vs = vs;
	vs->ac.group.getAttrib("#group", toUnixGidCallback, &cl);
	attr->gid = cl.id;

	attr->size = 1 * VR_BLOCKUNIT;  // arbitrary value
	attr->blocksize = VR_BLOCKSIZE;
	attr->rdev = VR_RDEV;
	attr->blocks = 1;               // must be consistent with attr->size
	attr->fsid = VR_FSID;
	attr->fileid = vs->pseudoInode;
	attr->atime.seconds = attr->mtime.seconds = 
	  attr->ctime.seconds = vs->timestamp();
	attr->atime.useconds = attr->mtime.useconds =
	  attr->ctime.useconds = 0;
	break;
	
      case VestaSource::ghost:
      case VestaSource::stub:
	if ((linkval = vs->getAttribConst("symlink-to")) != NULL) {
	    // Manifest as symlink.  Make mode bits reflect the right
	    // to read/change attributes.
	    attr->type = NFLNK;
	    attr->mode = NFSMODE_LNK | 0444 |
	      (vs->ac.mode & (S_IWUSR|S_IWGRP|S_IWOTH));
	    if (strcmp(linkval, "$LAST") == 0) {
		// Allow for $LAST to be expanded to a 32-bit decimal
		// number.  Set the timestamps to those of the parent
		// directory to help prevent outdated values from
		// being cached.
		attr->size = 10;
		VestaSource* parent = vs->longid.getParent().lookup();
		attr->atime.seconds = attr->mtime.seconds =
		    attr->ctime.seconds = parent->timestamp();
		attr->atime.useconds = attr->mtime.useconds =
		    attr->ctime.useconds = 0;
		delete parent;
	    } else {
		attr->size = strlen(linkval);
		attr->atime.seconds = attr->mtime.seconds =
		    attr->ctime.seconds = attrModTime(vs);
		attr->atime.useconds = attr->mtime.useconds =
		    attr->ctime.useconds = 0;
	    }
	} else if (vs->type == VestaSource::ghost) {
	    // Manifest as file with mode ---------T
	    // Should ghosts be optionally invisible?
	    attr->type = NFREG;
	    attr->mode = NFSMODE_REG | S_ISVTX;
	    attr->size = 0;
	    attr->atime.seconds = attr->mtime.seconds = 
	      attr->ctime.seconds = 2;    // arbitrary nonzero value
	    attr->atime.useconds = attr->mtime.useconds =
	      attr->ctime.useconds = 0;
	} else if (vs->master) {
	    // Manifest as file with mode --wS-w--w-
	    // using real write perms.
	    attr->type = NFREG;
	    attr->mode = NFSMODE_REG |
	      (vs->ac.mode & (S_IWUSR|S_IWGRP|S_IWOTH)) | S_ISUID;
	    attr->size = 0;
	    attr->atime.seconds = attr->mtime.seconds = 
	      attr->ctime.seconds = 2;    // arbitrary nonzero value
	    attr->atime.useconds = attr->mtime.useconds =
	      attr->ctime.useconds = 0;
	} else {
	    // Manifest as file with mode ------S---
	    attr->type = NFREG;
	    attr->mode = NFSMODE_REG | S_ISGID;
	    attr->size = 0;
	    attr->atime.seconds = attr->mtime.seconds = 
	      attr->ctime.seconds = 2;    // arbitrary nonzero value
	    attr->atime.useconds = attr->mtime.useconds =
	      attr->ctime.useconds = 0;
	}
	attr->nlink = 1;

	// Find local #owner if any
	cl.id = AccessControl::vforeignUser;
	cl.setid = false;
	cl.vs = vs;
	vs->ac.owner.getAttrib("#owner", toUnixUidCallback, &cl);
	attr->uid = cl.id;

	// Find local #group if any
	cl.id = AccessControl::vforeignGroup;
	cl.setid = false;
	cl.vs = vs;
	vs->ac.group.getAttrib("#group", toUnixGidCallback, &cl);
	attr->gid = cl.id;

	attr->blocksize = VR_BLOCKSIZE;
	attr->rdev = VR_RDEV;
	attr->blocks = (attr->size + VR_BLOCKUNIT - 1)/VR_BLOCKUNIT;
	attr->fsid = VR_FSID;
	attr->fileid = vs->pseudoInode;
	break;
	
      default:
	assert(false);
	break;
    }
    if (!vs->master) {
	// Turn off write access
	attr->mode &= ~0222;  
    }
    return NFS_OK;
}

static void
stalemsg(const char* what, const LongId* longid)
{
    if (Repos::isDebugLevel(DBG_STALENFS)) {
	char lid[256];
	OBufStream ost(lid, sizeof(lid));
	ost << what << " on stale handle " << *longid << endl;
	Repos::dprintf(DBG_ALWAYS, "%s", ost.str());
    }
}

//
// The immutableFile vs needs to be copied; do so.  Caller does not
// hold a lock, but vs is known to be a VLeaf, so its fields do not
// need to be protected.  A new VestaSource is returned, again with no
// lock held.
//
VestaSource* do_cow(VestaSource* vs, /*OUT*/nfsstat* status, 
		    Basics::uint64 len= ((Basics::uint64)-1))
{
  VestaSource* tmpvs1 = NULL;
  VestaSource* tmpvs2 = NULL;
  VestaSource* newvs = NULL;
  nfsstat st = NFS_OK;
  ReadersWritersLock* lock = NULL;
  int cowi = -1;

  // Enter the longid into the cowInProgress structure to ensure that
  // only one thread tries to do the copy.  This logical lock must be
  // acquired before the readLock or writeLock to avoid deadlock.
  cowLock.lock();
  for (;;) {
    int i;
    for (i=0; i<cowMax; i++) {
      if (cowInProgress[i].active) {
	if (cowInProgress[i].longid == vs->longid) {
	  cowi = -1;
	  break;
	}
      } else {
	if (cowi == -1) cowi = i;
      }
    }
    if (cowi != -1) break;
    cowCond.wait(cowLock);
  }
  cowInProgress[cowi].active = true;
  cowInProgress[cowi].longid = vs->longid;
  cowLock.unlock();

  // Acquire the read lock to check that the copy is still needed
  // and the file is still in the directory structure.
  tmpvs1 = vs->longid.lookup(LongId::readLock, &lock);
  if (tmpvs1 == NULL) {
    st = NFSERR_STALE;
    stalemsg("write", &vs->longid);
    goto error;
  }

  RWLOCK_LOCKED_REASON(lock, "do_cow:checking");

  // COW still needed?
  if (tmpvs1->type == VestaSource::immutableFile) {
    // Still needed.  Drop the readLock while doing the copy.
    lock->releaseRead();
    lock = NULL;

    // Copy to a new sid
    int copy_errno;
    ShortId newsid = CopyShortId(tmpvs1->shortId(), copy_errno,
				 len, &len);
    if (newsid == NullShortId) {
      st = xlate_errno(copy_errno);
      goto error;
    }

    // Log a debugging message
    if (Repos::isDebugLevel(DBG_COW)) {
      char msg[256];
      OBufStream ost(msg, sizeof(msg));
      ost << "copy on write: longid " << vs->longid
	  << ", from sid 0x" << hex << tmpvs1->shortId()
	  << ", to sid 0x" << newsid
	  << ", length " << dec << len << endl;
      Repos::dprintf(DBG_ALWAYS, "%s", ost.str());
    }

    // Acquire the writeLock.  Must redo lookup because releasing
    // the read lock allows the directory structure to change.
    tmpvs2 = vs->longid.lookup(LongId::writeLock, &lock);
    if (tmpvs2 == NULL) {
      st = NFSERR_STALE;
      stalemsg("write", &vs->longid);
      goto error;
    }

    RWLOCK_LOCKED_REASON(lock, "do_cow:copying");

    // Update the directory structure to point to the new sid
    VestaSource::errorCode err = vs->makeMutable(newvs, newsid);
    if (err != VestaSource::ok) {
      st = xlate_vserr(err);
      goto error;
    }

  } else if (tmpvs1->type != VestaSource::mutableFile) {
    // Type changed.  Can this really happen?
    st = NFSERR_ISDIR;
  } else {
    // It's mutable, which means another thread must have done the
    // copy just before us.
    newvs = tmpvs1;
    tmpvs1 = NULL;
  }

 error:
  if (lock) lock->release();
  cowLock.lock();
  cowInProgress[cowi].active = false;
  cowLock.unlock();
  cowCond.broadcast();
  *status = st;
  if (tmpvs1) delete tmpvs1;
  if (tmpvs2) delete tmpvs2;
  if (st != NFS_OK && newvs) {
    delete newvs;
    newvs = NULL;
  }
  return newvs;
}

//
// Get NFS attributes for a file or directory in the repository
//
nfsstat
do_getattr(nfs_fh* fh, fattr* attr, AccessControl::Identity cred)
{
    nfsstat status = NFS_OK;
    ReadersWritersLock* lock;
    RECORD_TIME_POINT;
    VestaSource* vs = ((LongId*) fh)->lookup(LongId::readLock, &lock);
    RECORD_TIME_POINT;

    RWLOCK_LOCKED_REASON(lock, "NFS:getattr");

    if (vs == NULL) {
      if(*((LongId*) fh) == NullLongId)
	{
	  status = NFSERR_INVAL;
	}
      else
	{
	  status = NFSERR_STALE;
	  stalemsg("getattr", (LongId*) fh);
	}
      goto finish;
    }
    // If this is a VLeaf, it's safe to release the lock before
    // calling any_fattr.
    if((lock != NULL) &&
       ((vs->type == VestaSource::immutableFile) ||
	(vs->type == VestaSource::mutableFile)))
      {
	lock->releaseRead();
	lock = NULL;
      }
    status = any_fattr(attr, vs, -1);
    RECORD_TIME_POINT;
  finish:
    if (lock != NULL) lock->releaseRead();
    if (vs != NULL) delete vs;
    RECORD_TIME_POINT;
    return status;
}

// Support for removeOldFromRealm; see below
struct RemoveOldFromRealmClosure {
  VestaSource* vs;
  CharsSeq oldvals;
};

static bool
removeOldFromRealmCallback(void* closure, const char* value)
{
  RemoveOldFromRealmClosure* cl = (RemoveOldFromRealmClosure*) closure;
  const char* at = strrchr(value, '@');
  if (at && strcasecmp(at + 1, AccessControl::realm) == 0) {
    cl->oldvals.addhi(value);
  }
  return true;
}

struct CopyInheritedClosure {
  VestaSource* vs;
  const char* name;
};

static bool
copyInheritedCallback(void* closure, VestaAttribs::attribOp op,
		      const char* name, const char* value,
		      time_t timestamp)
{
  CopyInheritedClosure* cl = (CopyInheritedClosure*) closure;
  if (strcmp(name, cl->name) == 0) {
    cl->vs->writeAttrib(op, name, value, NULL, timestamp);
  }
  return true;
}

// 
// Remove old owner (or group) attrib in local realm, if any.  Also
// gives the object its own owner (or group) attribute if it had been
// inheriting; this is needed since the caller is about to do an
// addAttrib.
//
static VestaSource::errorCode
removeOldFromRealm(VestaSource* vs, bool owner /* vs. group */,
		   const char* newval, AccessControl::Identity cred)
{
  if (!vs->hasAttribs()) return VestaSource::invalidArgs;
  const char* name = owner ? "#owner" : "#group";
  const char* name2 = owner ? "#setuid" : "#setgid";
  if (vs->getAttribConst(name) == NULL) {
    // Copy inherited attribs before modifying own attribs
    CopyInheritedClosure cic;
    cic.vs = vs;
    cic.name = name;
    if (owner) {
      vs->ac.owner.getAttribHistory(copyInheritedCallback, &cic);
    } else {
      vs->ac.group.getAttribHistory(copyInheritedCallback, &cic);
    }
  }
  // Strategy: we can't modify vs's own attributes while listing them,
  // so instead we make a list of the needed changes, then do them.
  RemoveOldFromRealmClosure cl;
  cl.vs = vs;
  vs->getAttrib(name, removeOldFromRealmCallback, &cl);
  int i;
  for (i=0; i<cl.oldvals.size(); i++) {
    const char* value = cl.oldvals.get(i);
    VestaSource::errorCode err;
    if (strcmp(value, newval) == 0) continue; // don't remove if about to add
    // First remove from the corresponding #setuid/#setgid attrib if present
    if (vs->inAttribs(name2, value)) {
      err = vs->removeAttrib(name2, value, cred);
      if (err != VestaSource::ok) return err;
    }
    // Then remove from the #owner/#group attribute
    err = vs->removeAttrib(name, value, cred);
    if (err != VestaSource::ok) return err;
  }
  return VestaSource::ok;
}

// Clamp the microseconds sent by the client to a valid range.  (On
// some systems, utimes(2) can fail with EINVAL for invalid
// microseconds.)  Log a debug message in the event of clamping.
static long clamp_usecs(long val, const char *msg)
{
  long result = val;
  if(val < 0)
    result = 0;
  else if(val > 999999)
    result = 999999;
  if(result != val)
    {
      Repos::dprintf(DBG_NFS,
		     "%s usecs: %ld -> %ld\n",
		     msg, val, result);
    }
  return result;
}

// There's a convention that passing the invalid value 1000000 for the
// microseconds portion of a time change in an NFS v2 call should be
// interpreted as "whatever the current time on the server is".  Sun
// and IRIX NFS implementations have used this for a long time, and
// recent versions of the Linux kernel (since some time after 2.6.8)
// use it as well.
#define USECS_SERVER_TIME 1000000

//
// Common code for setting NFS attributes
// newattr: new attributes requested (in)
// vs: VestaSource object (in/out)
// fd: If vs is a file, fd may be either -1 or a file descriptor
//       open for writing; otherwise fd is unused (in)
// attr: resulting attributes after changes (out)
//
nfsstat
apply_sattr(sattr* newattr, VestaSource* vs, int fd,
	    AccessControl::Identity cred, fattr* attr)
{
    bool isfile = (vs->type == VestaSource::immutableFile ||
		   vs->type == VestaSource::mutableFile);
    bool closefd = false;
    bool commit = false;
    struct stat st;
    nfsstat status = NFS_OK;
    bool setctime = false;
    timeval now;
    int ok = gettimeofday(&now, NULL);
    assert(ok != -1);
    FdCache::OFlag ofl;

    if (RootLongId.isAncestorOf(vs->longid) ||
	MutableRootLongId.isAncestorOf(vs->longid)) {
      // Make changes as failure-atomic as we can.  Some changes are
      // made immediately to the sid file, however.  Also, changes
      // aren't error-atomic; i.e., there can be a permission error
      // partway through, after some changes have already been made, and
      // those changes will be committed anyway.
      VRLog.start();
      commit = true;
    }

    // Get current attributes.  Also opens file if needed.
    if (isfile) {
	if (fd == -1) {
	    fd = FdCache::open(vs->shortId(), FdCache::any, &ofl);
	    if (fd < 0) {
		// Most likely file isn't really there, though
		// normally this shouldn't happen.
		status = xlate_errno(errno);
		goto finish;
	    }
	    closefd = true;
	}
	if (fstat(fd, &st) < 0) {
	    status = xlate_errno(errno);
	    goto finish;
	}
	RECORD_TIME_POINT;
	file_fattr(attr, &st, vs);
	RECORD_TIME_POINT;
    } else {
	status = any_fattr(attr, vs, -1);
	if (status != NFS_OK) goto finish;
    }

    // size changes
    if (isfile && newattr->size != (u_int) -1 &&
	newattr->size != attr->size) {
      RECORD_TIME_POINT;
	// Access check
	if (!vs->master) {
	    status = NFSERR_ROFS;
	    goto finish;
	}
	if (!vs->ac.check(cred, AccessControl::write)) {
	    status = NFSERR_ACCES;
	    goto finish;
	}
	if (closefd && ofl == FdCache::ro) {
	    // Oops, needed it open for writing
	    FdCache::close(vs->shortId(), fd, ofl);
	    fd = FdCache::open(vs->shortId(), FdCache::rw, &ofl);
	    if (fd < 0) {
		closefd = false;
		status = xlate_errno(errno);
		goto finish;
	    }
	}
	if (ftruncate(fd, newattr->size) < 0) {
	    status = xlate_errno(errno);
	    goto finish;
	}
	// Modify attr to reflect the change
	attr->size = newattr->size;
	setctime = true;
    }

    // uid changes
    if (newattr->uid != (u_int) -1 && newattr->uid != attr->uid) {
      RECORD_TIME_POINT;
      // Ignore error if setting uid on something with no attribs
      if (bsdChown) {
	if (!vs->ac.check(cred, AccessControl::administrative)) {
	    status = NFSERR_PERM;
	    goto finish;
	}
      }
      const char* newOwner = AccessControl::unixToGlobalUser(newattr->uid);
      VestaSource::errorCode err =
	removeOldFromRealm(vs, true, newOwner, cred);
      if (err == VestaSource::ok) {
	err = vs->addAttrib("#owner", newOwner, cred);
      }
      // Ignore error if setting uid on something with no attribs
      if (err != VestaSource::ok && err != VestaSource::invalidArgs) {
	status = xlate_vserr(err);
	goto finish;
      }
      vs->ac.owner = *vs;
      // Modify attr to reflect the change
      attr->uid = newattr->uid;
      attr->mode &= ~04000; // turn off setuid
      setctime = true;
    }

    // gid changes
    if (newattr->gid != (u_int) -1 && newattr->gid != attr->gid) {
      RECORD_TIME_POINT;
      const char* newGroup = AccessControl::unixToGlobalGroup(newattr->gid);
      VestaSource::errorCode err =
	removeOldFromRealm(vs, false, newGroup, cred);
      if (err == VestaSource::ok) {
	err = vs->addAttrib("#group", newGroup, cred);
      }
      // Ignore error if setting gid on something with no attribs
      if (err != VestaSource::ok && err != VestaSource::invalidArgs) {
	status = xlate_vserr(err);
	goto finish;
      }
      vs->ac.group = *vs;
      // Modify attr to reflect the change
      attr->gid = newattr->gid;
      attr->mode &= ~02000; // turn off setgid
      setctime = true;
    }

    // mode changes
    unsigned int newmode;
    if (newattr->mode != (unsigned) -1 &&
	(newmode = newattr->mode & 07777) != (attr->mode & 07777)) {
        RECORD_TIME_POINT;
	// Sticky bit is not supported
	if (newmode & 01000) {
	    status = NFSERR_PERM;
	    goto finish;
	}
	// Change in setuid bit?
	if ((newmode ^ attr->mode) & 04000) {
	    VestaSource::errorCode err;
	    if (newmode & 04000) {
		err = vs->addAttrib("#setuid", 
			AccessControl::unixToGlobalUser(attr->uid), cred);
	    } else {
		err = vs->removeAttrib("#setuid", 
			AccessControl::unixToGlobalUser(attr->uid), cred);
	    }
	    // Make setting mode on something with no attribs a no-op
	    if (err != VestaSource::ok && err != VestaSource::invalidArgs) {
		status = xlate_vserr(err);
		goto finish;
	    }
	    setctime = true;
	}
	// Change in setgid bit?
	if ((newmode ^ attr->mode) & 02000) {
	    VestaSource::errorCode err;
	    if (newmode & 02000) {
		err = vs->addAttrib("#setgid", 
			AccessControl::unixToGlobalGroup(attr->gid), cred);
	    } else {
		err = vs->removeAttrib("#setgid", 
			AccessControl::unixToGlobalGroup(attr->gid), cred);
	    }
	    // Make setting mode on something with no attribs a no-op
	    if (err != VestaSource::ok && err != VestaSource::invalidArgs) {
		status = xlate_vserr(err);
		goto finish;
	    }
	    setctime = true;
	}
	// Access check
	if (!vs->ac.check(cred, AccessControl::ownership)) {
	    status = NFSERR_PERM;
	    goto finish;
	}
	// Change in executability?
	bool old_exc = false, new_exc = false;
	if (isfile) {
	    if (!vs->master) {
		status = NFSERR_ROFS;
		goto finish;
	    }
	    old_exc = (st.st_mode & 0111) != 0;
	    new_exc = (newmode & 0111) != 0;
	    if (old_exc != new_exc) {
		if (vs->type == VestaSource::immutableFile) {
		    status = NFSERR_PERM;
		    goto finish;
		}
		int res =
		  fchmod(fd, (st.st_mode & ~0111) | (new_exc ? 0111 : 0));
		if (res < 0) {
		    status = xlate_errno(errno);
		    goto finish;
		}
		setctime = true;
	    }
	    // Modify newmode to reflect the effective result
	    newmode = (newmode & ~0111) |
	      (new_exc ? (newmode & 0444) >> 2 : 0);
	}
	// Change in other mode bits?
	if ((newmode ^ attr->mode) & (isfile ? 0666 : 0777)) {
	    const char* val =
	      AccessControl::formatModeBits(newmode & (isfile ? 0666 : 0777));
	    VestaSource::errorCode err =
	      vs->setAttrib("#mode", val, cred);
	    delete [] val;
	    // Making setting mode on something with no attribs a no-op
	    if (err != VestaSource::ok && err != VestaSource::invalidArgs) {
		status = xlate_vserr(err);
		goto finish;
	    }
	    vs->ac.mode = newmode;
	    setctime = true;
	}
	// Modify attr to reflect the change
	attr->mode = (attr->mode & ~07777) | (newmode & 07777);
    }

    // time changes
    if ((newattr->atime.seconds != (unsigned) -1 &&
	 newattr->atime.seconds != attr->atime.seconds) ||
	(newattr->mtime.seconds != (unsigned) -1 &&
	 newattr->mtime.seconds != attr->mtime.seconds)) {
        RECORD_TIME_POINT;

	// Access check
	if (!vs->ac.check(cred, AccessControl::ownership)) {
	    status = NFSERR_PERM;
	    goto finish;
	}
	struct timeval tvp[2];
	if (newattr->atime.seconds != (unsigned) -1) {
	  if(newattr->atime.useconds == USECS_SERVER_TIME)
	    {
	      // special (invalid) microseconds => currrent time
	      tvp[0].tv_sec = now.tv_sec;
	      tvp[0].tv_usec = now.tv_usec;
	    }
	  else
	    {
	      tvp[0].tv_sec = newattr->atime.seconds;
	      tvp[0].tv_usec = clamp_usecs(newattr->atime.useconds,
					   "apply_sattr: Clamped new atime");
	    }
	} else { 
	    tvp[0].tv_sec = attr->atime.seconds;
	    tvp[0].tv_usec = clamp_usecs(attr->atime.useconds,
					 "apply_sattr: Clamped old atime");
	}
	if (newattr->mtime.seconds != (unsigned) -1) {
	  if(newattr->mtime.useconds == USECS_SERVER_TIME)
	    {
	      // special (invalid) microseconds => currrent time
	      tvp[1].tv_sec = now.tv_sec;
	      tvp[1].tv_usec = now.tv_usec;
	    }
	  else
	    {
	      tvp[1].tv_sec = newattr->mtime.seconds;
	      tvp[1].tv_usec = clamp_usecs(newattr->mtime.useconds,
					   "apply_sattr: Clamped new mtime");
	    }
	} else { 
	    tvp[1].tv_sec = attr->mtime.seconds;
	    tvp[1].tv_usec = clamp_usecs(attr->mtime.useconds,
					 "apply_sattr: Clamped old mtime");
	}
	if (isfile) {
	    char *path = ShortIdBlock::shortIdToName(vs->shortId());
	    int ok = utimes(path, tvp);
	    delete [] path;
	    if (ok < 0) {
		Repos::dprintf(DBG_NFS,
			       "apply_sattr: utimes failed, atime = %ld s + %ld us, mtime = %ld s + %ld us\n",
			       tvp[0].tv_sec, tvp[0].tv_usec,
			       tvp[1].tv_sec, tvp[1].tv_usec);
		status = xlate_errno(errno);
		goto finish;
	    }
	    // This is a cheat; "now" may not be exactly the new
            // ctime, but it will be very close.  Avoids call to stat.
	    attr->ctime.seconds = now.tv_sec;
	    attr->ctime.useconds = now.tv_usec;
	} else {
	    if (!vs->master ||
		vs->type == VestaSource::immutableDirectory) {
		status = NFSERR_ROFS;
		goto finish;
	    }
	    vs->setTimestamp(tvp[1].tv_sec);
	    // Modify attr to reflect the actual change
	    attr->atime.seconds = attr->mtime.seconds =
	      attr->ctime.seconds = vs->timestamp();
	    attr->atime.useconds = attr->mtime.useconds =
	      attr->ctime.useconds = 0;
	}
	setctime = false; 
    }

    // ctime changes as a side-effect.  Note that case where atime
    // or mtime change was requested is handled above, not here.
    if (setctime) {
        RECORD_TIME_POINT;
	if (isfile) {
	    // This is a cheat; "now" may not be exactly the new
            // ctime, but it will be very close.  Avoids call to stat.
	    attr->ctime.seconds = now.tv_sec;
	    attr->ctime.useconds = now.tv_usec;
	} else {	    
	    vs->setTimestamp(now.tv_sec);
	    // Modify attr to reflect the actual change
	    attr->atime.seconds = attr->mtime.seconds =
	      attr->ctime.seconds = vs->timestamp();
	    attr->atime.useconds = attr->mtime.useconds =
	      attr->ctime.useconds = 0;
	}
    }
    status = NFS_OK;

  finish:
    RECORD_TIME_POINT;
    if (commit) VRLog.commit();
    RECORD_TIME_POINT;
    if (closefd) FdCache::close(vs->shortId(), fd, ofl);
    RECORD_TIME_POINT;
    return status;
}

//
// Set NFS attributes.
//
nfsstat
do_setattr(sattrargs* argp, fattr* attr, AccessControl::Identity cred)
{
  ReadersWritersLock* lock;
  nfsstat status = NFS_OK;
  VestaSource* vs = NULL;
    
  // Repeat until the file is mutable and we have the directory write
  // lock, or we detect an error.
  for (;;) {
    // First get the write lock
    RECORD_TIME_POINT;
    vs = ((LongId*) &argp->file)->lookup(LongId::writeLock, &lock);
    RECORD_TIME_POINT;
    if (vs == NULL) {
      if(*((LongId*) &argp->file) == NullLongId)
	{
	  status = NFSERR_INVAL;
	}
      else
	{
	  status = NFSERR_STALE;
	  stalemsg("setattr", (LongId*) &argp->file);
	}
      goto finish;
    }

    RWLOCK_LOCKED_REASON(lock, "NFS:setattr");
    
    // Copy-on-write the file if needed
    if (vs->type == VestaSource::immutableFile &&
	(MutableRootLongId.isAncestorOf(vs->longid) ||
	 VolatileRootLongId.isAncestorOf(vs->longid))) {

      // Copying is needed; release the lock to avoid deadlock
      lock->releaseWrite();
      lock = NULL;

      RECORD_TIME_POINT;
      VestaSource* newvs =
	do_cow(vs, &status, (Basics::uint64) argp->attributes.size);
      RECORD_TIME_POINT;
      if (newvs == NULL) {
	goto finish;
      }

      delete vs;
      delete newvs;

      // Loop back to reacquire the lock
      continue;

    } else {
      // Copying is not needed; done
      break;
    }
  }

  // If this is an object under the mutable root that doesn't have
  // attributes, it must still be in the immutable base directory
  // (i.e. an immutable directory unmodified since checkout).  To
  // change its attributes, we need to copy it to the mutable portion.
  if(!vs->hasAttribs() && MutableRootLongId.isAncestorOf(vs->longid))
    {
      RECORD_TIME_POINT;
      VestaSource* newvs = 0;
      VestaSource::errorCode err =
	vs->copyToMutable(newvs, cred);
      if (err != VestaSource::ok) {
	assert(newvs == 0);
	status = xlate_vserr(err);
	goto finish;
      }
      delete vs;
      vs = newvs;
      RECORD_TIME_POINT;
    }

  RECORD_TIME_POINT;
  status = apply_sattr(&argp->attributes, vs, -1, cred, attr);
  RECORD_TIME_POINT;
    
 finish:
  if (lock != NULL) lock->releaseWrite();
  if (vs != NULL) delete vs;
  return status;
}

// 
// Look up a name in a directory
//
nfsstat
do_lookup(diropargs *dopa, diropokres* dp, AccessControl::Identity cred)
{
    ReadersWritersLock* lock = 0;
    nfsstat status = NFS_OK;
    VestaSource::errorCode err;

    RECORD_TIME_POINT;
    VestaSource* vs = ((LongId*) &dopa->dir)->lookup(LongId::readLock, &lock);
    RECORD_TIME_POINT;

    RWLOCK_LOCKED_REASON(lock, "NFS:lookup");

    VestaSource* vs2 = NULL;
    if (vs == NULL) {
      if(*((LongId*) &dopa->dir) == NullLongId)
	{
	  status = NFSERR_INVAL;
	}
      else
	{
	  status = NFSERR_STALE;
	  stalemsg("lookup", (LongId*) &dopa->dir);
	}
      goto finish;
    }
    switch (vs->type) {
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
	if (strcmp(dopa->name, "") == 0 || strcmp(dopa->name, ".") == 0) {
	    // return self
	    dp->file = dopa->dir;
	    vs2 = vs;
	} else if (strcmp(dopa->name, "..") == 0) {
	    // return parent
	    LongId plongid = vs->longid.getParent();
	    *(Byte32*)& dp->file = plongid.value;
	    RECORD_TIME_POINT;
	    vs2 = plongid.lookup(); // ugh, but needed to get attributes
	    RECORD_TIME_POINT;
	} else {
	  RECORD_TIME_POINT;
	  TIMING_RECORD_LONGID(vs->longid);
	    err = vs->lookup(dopa->name, vs2, cred);
	    if (err != VestaSource::ok) {
	      RECORD_TIME_POINT;
		status = xlate_vserr(err);
		goto finish;
	    } else {
	      RECORD_TIME_POINT;
		*(Byte32*)& dp->file = vs2->longid.value;
	    }
	}
	break;
	
      default:
	status = NFSERR_NOTDIR;
	goto finish;
    }

    // If this is a VLeaf, it's safe to release the lock before
    // calling any_fattr.
    if((lock != NULL) &&
       ((vs2->type == VestaSource::immutableFile) ||
	(vs2->type == VestaSource::mutableFile)))
      {
	lock->releaseRead();
	lock = NULL;
      }
    
    // Get fattr
    RECORD_TIME_POINT;
    status = any_fattr(&(dp->attributes), vs2, -1);
    RECORD_TIME_POINT;

  finish:
    if (lock != NULL) lock->releaseRead();
    if (vs2 != vs && vs2 != NULL) delete vs2;
    if (vs != NULL) delete vs;
    return status;
}

//
// Given a file handle, return an open file descriptor and a
// VestaSource*.
// Also does access checking and handles copy-on-write.  Seek pointer
// location is unspecified.
//
int
fh_fd(nfs_fh* fh, nfsstat* status, int omode, VestaSource** vsout, int* oflout,
      AccessControl::Identity cred)
{
  int fd = -1;
  bool writing = (bool) (omode == O_WRONLY || omode == O_RDWR);
  ReadersWritersLock* lock = NULL;
  FdCache::OFlag ofl = FdCache::any;
  nfsstat st = NFS_OK;

  // Optimistically get only a read lock.  If copy-on-write is
  // required, do_cow will go through some gyrations to do
  // it without holding the read or write lock for too long.
  RECORD_TIME_POINT;
  VestaSource* vs = ((LongId*) fh)->lookup(LongId::readLock, &lock);
  RECORD_TIME_POINT;
  if (vs == NULL) {
    st = NFSERR_STALE;
    stalemsg(writing ? "write" : "read", (LongId*) fh);
    goto error;
  }

  RWLOCK_LOCKED_REASON(lock, (writing ? "NFS:write" : "NFS:read"));

  // Check access.  FileShortIdRootLongIds will pass here but
  //  may fail when FdCache::open is called.
  if (!vs->ac.check(cred, (writing ? AccessControl::write :
			   AccessControl::read)) &&
      !vs->ac.check(cred, AccessControl::ownership)) {
    st = NFSERR_ACCES;
    goto error;
  }
  RECORD_TIME_POINT;

  // OK to release the readLock or writeLock here, as the vs
  // is of type VLeaf; it doesn't point into the directory structure.
  if (lock) lock->release();
  lock = NULL;

  // If copy-on-write is needed...
  if (writing && vs->type == VestaSource::immutableFile &&
      (MutableRootLongId.isAncestorOf(vs->longid) ||
       VolatileRootLongId.isAncestorOf(vs->longid))) {

    RECORD_TIME_POINT;
    VestaSource *newvs = do_cow(vs, &st);
    RECORD_TIME_POINT;
    if (newvs == NULL) {
      assert(st != NFS_OK);
      goto error;
    }
    delete vs;
    vs = newvs;

    // Could redo access check here, as directory structure could
    // have changed, but it seems harmless to let it through; we
    // would have allowed access if the copy had not been needed.
    // We don't want to move the first access check to after the
    // copy-on-write, because we don't want users who never had
    // write access to be able to force a copy to happen.
  }

  switch (vs->type) {
  case VestaSource::immutableFile:
    if (writing) {
      st = NFSERR_ACCES;
      goto error;
    } else {
      ofl = FdCache::any;
      RECORD_TIME_POINT;
      fd = FdCache::open(vs->shortId(), ofl, &ofl);
      RECORD_TIME_POINT;
      //Repos::dprintf(DBG_NFS, "imm sid:%08x\n", vs->shortId());
      if (fd < 0) {
	st = xlate_errno(errno);
	goto error;
      }
    }
    break;
	
  case VestaSource::mutableFile:
    if (writing) {
      ofl = FdCache::rw;
    } else {
      ofl = FdCache::any;
    }
    RECORD_TIME_POINT;
    fd = FdCache::open(vs->shortId(), ofl, &ofl);
    RECORD_TIME_POINT;
    //Repos::dprintf(DBG_NFS, "mut sid:%08x\n", vs->shortId());
    if (fd < 0) {
      st = xlate_errno(errno);
      goto error;
    }
    break;
	
  case VestaSource::device:
    fd = DEVICE_FAKE_FD;
    ofl = FdCache::rw;
    //Repos::dprintf(DBG_NFS, "device 0x%x\n", vs->shortId());
    break;
	
  default:
    st = NFSERR_ISDIR;
    goto error;
  }
    
  *status = st;
  *vsout = vs;
  *oflout = (int) ofl;
  return fd;

 error:
  if (lock) lock->release();
  if (vs) delete vs;
  *status = st;
  *vsout = NULL;
  *oflout = -1;
  return -1;
}

//
// Return fd to cache
//
void
fd_inactive(void* vsin, int fd, int ofl)
{
    VestaSource* vs = (VestaSource*) vsin;
    if (fd != DEVICE_FAKE_FD) 
      FdCache::close(vs->shortId(), fd, (FdCache::OFlag) ofl);
    delete vs;
}

struct readdirClosure {
    VestaSource* vs;
    entry** e;
    int res_size;
    int count;
    bool first;
    bool full;
    unsigned int cookie_incr;
};

#define DP_SLOP	16

static bool
readdirCallback(void* closure, VestaSource::typeTag type, Arc arc,
		unsigned int index, unsigned int pseudoInode,
		ShortId filesid, bool master)
{
    readdirClosure* cl = (readdirClosure*) closure;
    entry* e;
    int esize = sizeof(entry) + strlen(arc) + DP_SLOP;
    if (cl->res_size + esize < cl->count) {
	e = (entry*) malloc(sizeof(entry));
	assert(e != NULL);
	*(cl->e) = e;
	e->fileid = pseudoInode;
	e->name = strdup(arc);
	// Linux client treats this field as a big-endian integer.
	// Some kernel versions sign-extend it and some do not, which
	// causes problems with some glibc versions.  So we avoid setting
	// what looks like the sign bit under the big-endian interpretation.
	index += cl->cookie_incr;
	e->cookie[0] = index >> 24;
	e->cookie[1] = index >> 16;
	e->cookie[2] = index >> 8;
	e->cookie[3] = index >> 0;
	cl->e = &(e->nextentry);
	cl->res_size += esize;
	return true;
    } else {
	cl->full = true;
	return false;
    }
}

nfsstat
do_readdir(readdirargs* argp, result_types* resp, AccessControl::Identity cred)
{
    ReadersWritersLock* lock;
    nfsstat status = NFS_OK;
    unsigned int cookie;

    RECORD_TIME_POINT;
    VestaSource* vs = ((LongId*) &argp->dir)->lookup(LongId::readLock, &lock);
    RECORD_TIME_POINT;

    RWLOCK_LOCKED_REASON(lock, "NFS:readdir");

    int fd;
    if (vs == NULL) {
      if(*((LongId*) &argp->dir) == NullLongId)
	{
	  status = NFSERR_INVAL;
	}
      else
	{
	  status = NFSERR_STALE;
	  stalemsg("readdir", (LongId*) &argp->dir);
	}
      goto finish;
    }
    switch (vs->type) {
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
	break;
      default:
	status = NFSERR_NOTDIR;
	goto finish;
    }
    
    readdirClosure cl;
    cl.vs = vs;
    cl.res_size = 0;
    cl.e = &(resp->r_readdirres.readdirres_u.reply.entries);
    cl.count = argp->count;
    cl.first = true;
    cl.full = false;
    cl.cookie_incr = (vs->longid == VolatileRootLongId) ? 1 : 2;
    // Undo big-endian packing; see comment in callback above
    cookie = ((((unsigned char) argp->cookie[0]) << 24) +
	      (((unsigned char) argp->cookie[1]) << 16) +
	      (((unsigned char) argp->cookie[2]) << 8) +
	      (((unsigned char) argp->cookie[3]) << 0));
    //
    // Kludge warning.  VestaSource assigns cookies in this order: 
    //   2, 4, 6, ..., 1, 3, 5, ...
    // Cookie 0 is unused on output and equivalent to 2 on input.
    // The problem is that we need *two* unused cookies to represent
    // starting at the "." or ".." entry of the directory, neither of
    // which exist at the VestaSource interface.  We kludge this by
    // assigning:
    //   0           = start at "."  (required by NFS interface)
    //   0x7fffffff  = start at ".." (!)
    //   2           = start just after ".."
    // I used to not have a cookie for starting at ".."; I just
    // filled in 2 as the next cookie for both of the first two
    // entries.  For some reason this causes the Linux NFS client
    // to hang when listing an empty directory!
    // Later I used 0xffffffff as the cookie to start at "..".
    // This caused problems with a later Linux + glibc combo
    // where glibc expected the kernel to sign-extend the cookie
    // but the kernel didn't do so.
    //
    // KCS 2004-03-10: The volatile root is a bit of a special case,
    // as it assigns indicies in this order:
    //   1, 2, 3, 4, 5, ...
    // We hadle this by using cl.cookie_incr.
    //
    if (cookie == 0) {
	// Put in "."
	// Set the index in this fake callback so the next cookie 
	//  is computed as 0x7fffffff.
	readdirCallback((void*) &cl, vs->type, ".", 0x7fffffff-cl.cookie_incr,
			vs->pseudoInode, NullShortId, true);
    }
    if (!cl.full && (cookie == 0 || cookie == 0x7fffffff)) {
	// Put in ".."
	// Set the index so the next cookie is the first real index.
	VestaSource* vs2 = vs->longid.getParent().lookup();
	if (vs2 == NULL) {
  	    readdirCallback((void*) &cl, vs->type, "..", 0,
			    vs->pseudoInode, NullShortId, true);
	} else {
	    readdirCallback((void*) &cl, vs2->type, "..", 0,
			    vs2->pseudoInode, NullShortId, true);
	    delete vs2;
	}
	// Arrange to continue at the first real entry.
	cookie = 0;
    }
    if (!cl.full) {
      RECORD_TIME_POINT;
      TIMING_RECORD_LONGID(vs->longid);
	status = xlate_vserr(vs->list(cookie, readdirCallback, &cl, cred));
      RECORD_TIME_POINT;
    }
    *(cl.e) = NULL;
    resp->r_readdirres.readdirres_u.reply.eof = !cl.full;
  finish:
    resp->r_readdirres.status = status;
    if (lock != NULL) lock->releaseRead();
    if (vs != NULL) delete vs;
    return status;
}


// Create a mutable file
nfsstat
do_create(createargs* argp, diropokres* dp, AccessControl::Identity cred)
{
    ReadersWritersLock* lock;
    nfsstat status = NFS_OK;
    int fd = -1;
    VestaSource* vs2 = NULL;
    VestaSource* newvs = NULL;

    // Find the directory
    RECORD_TIME_POINT;
    VestaSource* vs =
      ((LongId*) &argp->where.dir)->lookup(LongId::writeLock, &lock);
    RECORD_TIME_POINT;

    RWLOCK_LOCKED_REASON(lock, "NFS:create");

    if (vs == NULL) {
      if(*((LongId*) &argp->where.dir) == NullLongId)
	{
	  status = NFSERR_INVAL;
	}
      else
	{
	  status = NFSERR_STALE;
	  stalemsg("create", (LongId*) &argp->where.dir);
	}
      goto finish;
    }
    
    switch (vs->type) {
      case VestaSource::immutableDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
	{
	    // Try to do copy-on-write
	    VestaSource* newvs;
	    RECORD_TIME_POINT;
	    VestaSource::errorCode err = vs->makeMutable(newvs);
	    RECORD_TIME_POINT;
	    if (err != VestaSource::ok) {
		status = xlate_vserr(err);
		goto finish;
	    }
	    delete vs;
	    vs = newvs;
	}
	break;
	
      case VestaSource::appendableDirectory:
	status = NFSERR_ACCES;
	goto finish;
	
      default:
	status = NFSERR_NOTDIR;
	goto finish;
	
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
	break;
    }
    
    RECORD_TIME_POINT;
    // Check that name isn't already in use
    VestaSource::errorCode err;
    err = vs->lookup(argp->where.name, vs2, cred);
    if (err == VestaSource::ok ||
	strcmp(argp->where.name, "") == 0 ||
	strcmp(argp->where.name, ".") == 0 ||
	strcmp(argp->where.name, "..") == 0) {
	if (vs2 != NULL) delete vs2;
	status = NFSERR_EXIST;
	goto finish;
    } else if (err != VestaSource::notFound) {
	status = xlate_vserr(err);
	goto finish;
    }
    
    // (Next comment and code adapted from old nfsd.)
    // Compensate for a really bizarre bug in SunOS derived clients.
    if ((argp->attributes.mode & S_IFMT) == 0) {
	argp->attributes.mode |= S_IFREG;
    }
    
    // Can't create devices, etc.
    if (!S_ISREG(argp->attributes.mode)) {
	status = NFSERR_ACCES;
	goto finish;
    }
    
    // Make a new file named by ShortId
    ShortId sid;
    RECORD_TIME_POINT;
    fd = SourceOrDerived::fdcreate(sid);
    if (fd < 0) {
	status = xlate_errno(errno);
	goto finish;
    }
    
    // Insert file into the directory
    RECORD_TIME_POINT;
    err = vs->insertMutableFile(argp->where.name, sid, true, cred,
				VestaSource::dontReplace, &newvs);
    if (err != VestaSource::ok) {
	status = xlate_vserr(err);
	goto finish;
    }

    // Apply requested attributes and build return value
    *(LongId*) &dp->file = newvs->longid;
    RECORD_TIME_POINT;
    status = apply_sattr(&argp->attributes, newvs, fd, NULL,
			 &dp->attributes);
    RECORD_TIME_POINT;

  finish:
    if (lock != NULL) lock->releaseWrite();
    if (newvs != NULL) delete newvs;
    if (fd != -1) FdCache::close(sid, fd, FdCache::rw);
    if (vs != NULL) delete vs;
    return status;
}


// Stuff to help us remember the names in a deleted appendable directory
struct NamelistClosure {
  ostream* val;
};

bool
namelistCallback(void* closure, VestaSource::typeTag type, Arc arc,
		 unsigned int index, Bit32 pseudoInode, ShortId filesid,
		 bool master)
{
  NamelistClosure *cl = (NamelistClosure *)closure;
  *cl->val << arc << "/";
  return true;
}

bool
dirEmptyCallback(void* closure, VestaSource::typeTag type, Arc arc,
		 unsigned int index, Bit32 pseudoInode, ShortId filesid,
		 bool master)
{
  bool *empty = (bool *) closure;
  *empty = false;
  return false;
}

// Delete a file or directory, replacing with a ghost if parent is
// appendable.
nfsstat
do_remove(diropargs* argp, AccessControl::Identity cred)
{
    ReadersWritersLock* lock;
    nfsstat status = NFS_OK;
    OBufStream val;

    // Find the parent directory
    RECORD_TIME_POINT;
    VestaSource* vs = ((LongId*) &argp->dir)->lookup(LongId::writeLock, &lock);
    RECORD_TIME_POINT;
    if (vs == NULL) {
      if(*((LongId*) &argp->dir) == NullLongId)
	{
	  status = NFSERR_INVAL;
	}
      else
	{
	  status = NFSERR_STALE;
	  stalemsg("remove", (LongId*) &argp->dir);
	}
      goto finish;
    }

    RWLOCK_LOCKED_REASON(lock, "NFS:remove/rmdir");
    
    VestaSource::errorCode err;
    switch (vs->type) {
      case VestaSource::immutableDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
	{
	    // Try to do copy-on-write
	    VestaSource* newvs;
	    RECORD_TIME_POINT;
	    VestaSource::errorCode err = vs->makeMutable(newvs);
	    RECORD_TIME_POINT;
	    if (err != VestaSource::ok) {
		status = xlate_vserr(err);
		goto finish;
	    }
	    delete vs;
	    vs = newvs;
	}
	// fall through

      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
	{
	  // Look up the object to be deleted
	  VestaSource* child_vs;
	  RECORD_TIME_POINT;
	  err = vs->lookup(argp->name, child_vs);
	  RECORD_TIME_POINT;
	  if(err != VestaSource::ok) {
            status = xlate_vserr(err);
            goto finish;
	  }
	  // If the child is a non-empty directory, set this to false.
	  bool empty = true;
	  switch(child_vs->type)
	    {
	    case VestaSource::immutableDirectory:
	      // We can optimize a little for immutableDirectory,
	      // because the first child will always be index 2.
	      // (That's actually also true for evaluator directories,
	      // but it's not safe to call lookupIndex on them, as it
	      // assumes lookupIndex can be serviced from cached
	      // information, and there might not be any in this
	      // case.)
	      {
		VestaSource* child_child_vs;
		RECORD_TIME_POINT;
		err = child_vs->lookupIndex(2, child_child_vs);
		RECORD_TIME_POINT;
		if(err == VestaSource::ok)
		  {
		    // This directory's no empty.
		    delete child_child_vs;
		    empty = false;
		  }
		if(err != VestaSource::notFound)
		  {
		    delete child_vs;
		    status = xlate_vserr(err);
		    goto finish;
		  }
	      }
	      break;
	    case VestaSource::mutableDirectory:
	    case VestaSource::evaluatorDirectory:
	    case VestaSource::evaluatorROEDirectory:
	    case VestaSource::volatileDirectory:
	    case VestaSource::volatileROEDirectory:
	      // Check if this directory is empty by listing it.
	      RECORD_TIME_POINT;
	      err = child_vs->list(0, dirEmptyCallback, (void *) &empty);
	      RECORD_TIME_POINT;
	      if(err != VestaSource::ok)
		{
		  delete child_vs;
		  status = xlate_vserr(err);
		  goto finish;
		}
	      break;
	    }

	  // We're done with the child now.
	  delete child_vs;

	  if(!empty)
	    {
	      // Can't delete a non-empty directory via NFS.
	      status = NFSERR_NOTEMPTY;
	      goto finish;
	    }
	  else
	    {
	      // Do the deletion
	      RECORD_TIME_POINT;
	      err = vs->reallyDelete(argp->name, cred, true);
	      RECORD_TIME_POINT;
	    }
	}
	break;
	
      case VestaSource::appendableDirectory:
	// Check if delete-like operations are restricted
	if (!vs->ac.check(cred, AccessControl::del)) {
	    status = NFSERR_ACCES;
	    goto finish;
	}
	// Check that this isn't already a ghost
	VestaSource* newvs;
	RECORD_TIME_POINT;
	err = vs->lookup(argp->name, newvs);
	RECORD_TIME_POINT;
	if (err != VestaSource::ok) {
	    status = xlate_vserr(err);
	    goto finish;
	}
	if (newvs->type == VestaSource::ghost) {
	    delete newvs;
	    status = NFSERR_ACCES;
	    goto finish;
	}
	// If a master appendable directory, remember the name list; else
	// remember the fingerprint if any.
	val << newvs->typeTagChar(newvs->type) << ":";
	if (newvs->type == VestaSource::appendableDirectory) {
	  if (newvs->master) {
	    NamelistClosure cl;
	    cl.val = &val;
	    RECORD_TIME_POINT;
	    err = newvs->list(0, namelistCallback, &cl);
	    RECORD_TIME_POINT;
	    if (err != VestaSource::ok) {
	      status = xlate_vserr(err);
	      delete newvs;
	      goto finish;
	    }
	  }
	} else if (newvs->type != VestaSource::stub) {
	  val << setw(2) << setfill('0') << hex;
	  unsigned char fpbytes[FP::ByteCnt];
	  newvs->fptag.ToBytes(fpbytes);
	  int i;
	  for (i=0; i<FP::ByteCnt; i++) {
	    val << (int) (fpbytes[i] & 0xff);
	  }
	}
	// Replace with a ghost
	VestaSource* ghostvs;
	RECORD_TIME_POINT;
	err = vs->insertGhost(argp->name, newvs->master, cred,
			      VestaSource::replaceDiff, &ghostvs);
	RECORD_TIME_POINT;
	if (err == VestaSource::ok) {
	  const char *valstr = val.str();
	  err = ghostvs->setAttrib("#formerly", valstr);
	  delete ghostvs;
	}
	delete newvs;
	break;
	
      default:
	status = NFSERR_NOTDIR;
	goto finish;
    }
    status = xlate_vserr(err);

  finish:
    if (lock != NULL) lock->releaseWrite();
    if (vs != NULL) delete vs;
    return status;
}


// Rename something in a mutable or appendable directory
nfsstat
do_rename(renameargs* argp, AccessControl::Identity cred)
{
    ReadersWritersLock* vlock = NULL;
    ReadersWritersLock* lock;
    nfsstat status;
    VestaSource* fromVS = NULL;
    VestaSource* toVS = NULL;
    
    // Find the old and new parent directories
    if (VolatileRootLongId.isAncestorOf(*(LongId*) &argp->from.dir)) {
      // Must retain VolatileRootLock.read across both longid lookups,
      // not acquire and release it within the first.
      vlock = &VolatileRootLock;
      vlock->acquireRead();
      RWLOCK_LOCKED_REASON(vlock, "NFS:rename in volatile");
      fromVS = ((LongId*) &argp->from.dir)->lookup(LongId::writeLockV, &lock);
    } else {
      fromVS = ((LongId*) &argp->from.dir)->lookup(LongId::writeLock, &lock);
    }
    if (fromVS == NULL) {
      if(*((LongId*) &argp->from.dir) == NullLongId)
	{
	  status = NFSERR_INVAL;
	}
      else
	{
	  status = NFSERR_STALE;
	  stalemsg("rename fromdir", (LongId*) &argp->from.dir);
	}
      goto finish;
    }

    RWLOCK_LOCKED_REASON(lock, "NFS:rename");

    switch (fromVS->type) {
      case VestaSource::immutableDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
	{
	    // Try to do copy-on-write.
	    // It's not great that we hold vlock through this call, but
	    // it doesn't cause any actual harm.  makeMutable on a directory
	    // should always be very fast.
	    VestaSource* newFromVS;
	    VestaSource::errorCode err = fromVS->makeMutable(newFromVS);
	    if (err != VestaSource::ok) {
		status = xlate_vserr(err);
		goto finish;
	    }
	    delete fromVS;
	    fromVS = newFromVS;
	}
	break;
	
      default:
	status = NFSERR_NOTDIR;
	goto finish;
	
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
	break;

      case VestaSource::appendableDirectory:
#if 0
	// Check if delete-like operations are restricted
	if (!fromVS->ac.check(cred, AccessControl::del)) {
	    status = NFSERR_ACCES;
	    goto finish;
	}
	// Would also need to check master statuses and do the right
        // thing here...
	break;
#else
	// Not supported
	status = NFSERR_INVAL;
	goto finish;
#endif
    }
    
    // Wait until here to look up toVS, because the copy-on-write
    // of fromVS could have also copied toVS.
    toVS = ((LongId*) &argp->to.dir)->lookup(LongId::checkLock, &lock);
    if (vlock != NULL) vlock->releaseRead();
    vlock = NULL;
    if (toVS == NULL) {
      if(*((LongId*) &argp->to.dir) == NullLongId)
	{
	  status = NFSERR_INVAL;
	}
      else
	{
	  status = NFSERR_STALE; // !!or could be cross-device link
	  stalemsg("rename todir", (LongId*) &argp->to.dir);
	}
      goto finish;
    }
    if (toVS->type == VestaSource::immutableDirectory ||
	toVS->type == VestaSource::evaluatorDirectory ||
	toVS->type == VestaSource::evaluatorROEDirectory) {
	// Try to do copy-on-write
	VestaSource* newToVS;
	VestaSource::errorCode err = toVS->makeMutable(newToVS);
	if (err != VestaSource::ok) {
	    status = xlate_vserr(err);
	    goto finish;
	}
	delete toVS;
	toVS = newToVS;
    }	
    
    status =
      xlate_vserr(toVS->renameTo(argp->to.name, fromVS, argp->from.name,
				 cred, VestaSource::replaceDiff));

  finish:    
    if (vlock != NULL) vlock->releaseRead();
    if (lock != NULL) lock->releaseWrite();
    if (fromVS != NULL) delete fromVS;
    if (toVS != NULL) delete toVS;
    return status;
}

// Hard link a file.  The link count is maintained by the directory
// internally.  (See the ShortIdRefCount calss and its use in the
// VDirChangeable class.)  Copy-on-write is performed if possible
// (with both old and new links pointing to the new copy) in case
// there are to be writes in the future.  This operation is allowed
// only in descendents of the volatile root and mutable root, because
// those directory types have the mutable shortid reference counting
// machinery.
nfsstat
do_hardlink(linkargs* argp, AccessControl::Identity cred)
{
    ReadersWritersLock* lock = NULL;
    nfsstat status;
    VestaSource* fromVS = NULL;
    VestaSource* toVS = NULL;
    LongId fromAncestor;
    
 retry:
    // Find the destination directory
    toVS = ((LongId*) &argp->to.dir)->lookup(LongId::writeLock, &lock);
    if (toVS == NULL) {
      if(*((LongId*) &argp->to.dir) == NullLongId)
	{
	  status = NFSERR_INVAL;
	}
      else
	{
	  status = NFSERR_STALE; // !! or cross-device link
	  stalemsg("hardlink todir", (LongId*) &argp->to.dir);
	}
      goto finish;
    }

    RWLOCK_LOCKED_REASON(lock, "NFS:link");

    if(VolatileRootLongId.isAncestorOf(toVS->longid))
      {
	// The "from" file must be in the same volatile directory.
	// This disallows cross-linking between volatile directories.
	// It's neccessary to do this, because each volatile directory
	// keeps its own independent shortid reference count.
	fromAncestor = toVS->longid;
	LongId parent;
	while(!((parent = fromAncestor.getParent()) == VolatileRootLongId))
	  {
	    fromAncestor = parent;
	  }
      }
    else if(MutableRootLongId.isAncestorOf(toVS->longid))
      {
	// The "from" file must also be in the mutable root.
	fromAncestor = MutableRootLongId;
      }
    else
      {
	// Can't create a hard-link outside the volatile and mutable
	// roots.
	status = NFSERR_INVAL;
	goto finish;
      }

    if (toVS->type == VestaSource::immutableDirectory ||
	toVS->type == VestaSource::evaluatorDirectory ||
	toVS->type == VestaSource::evaluatorROEDirectory) {
	// Try to do copy-on-write on destination directory
	VestaSource* newToVS;
	VestaSource::errorCode err = toVS->makeMutable(newToVS);
	if (err != VestaSource::ok) {
	    status = xlate_vserr(err);
	    goto finish;
	}
	delete toVS;
	toVS = newToVS;
    }	
    assert(toVS->type == VestaSource::mutableDirectory ||
	   toVS->type == VestaSource::volatileDirectory ||
	   toVS->type == VestaSource::volatileROEDirectory);

    // Find the existing file
    fromVS = ((LongId*) &argp->from)->lookup(LongId::checkLock, &lock);
    if (fromVS == NULL) {
      if(*((LongId*) &argp->from) == NullLongId)
	{
	  status = NFSERR_INVAL;
	}
      else
	{
	  status = NFSERR_STALE;
	  stalemsg("hardlink from", (LongId*) &argp->from);
	}
      goto finish;
    }
    else if(fromAncestor.isAncestorOf(fromVS->longid)) {
	switch (fromVS->type) {
	  case VestaSource::immutableFile:
	    {
		// Oops, need to do copy-on-write
	        lock->release();
		lock = NULL;
	        VestaSource* newFromVS = do_cow(fromVS, &status);
		if (newFromVS == NULL) goto finish;
		delete toVS;      toVS = 0;
		delete fromVS;    fromVS = 0;
		delete newFromVS; newFromVS = 0;
		goto retry;
	    }
	  case VestaSource::mutableFile:
	    break;
	  default:
	    status = NFSERR_ISDIR;
	    goto finish;
	}
	status =
	  xlate_vserr(toVS->insertMutableFile(argp->to.name, fromVS->shortId(),
					      true, cred,
					      VestaSource::dontReplace, NULL));
    } else if (FileShortIdRootLongId.isAncestorOf(fromVS->longid) &&
	       (toVS->type == VestaSource::volatileROEDirectory)) {
      // This case allows a hard-link to be created to an immutable
      // file in a read-only-existing volatile directory.
	status =
	  xlate_vserr(toVS->insertFile(argp->to.name, fromVS->shortId(),
				       true, cred,
				       VestaSource::dontReplace, NULL, 0,
				       &fromVS->fptag));
    } else {
	status = NFSERR_INVAL;
    }
  finish:    
    if (lock != NULL) lock->releaseWrite();
    if (fromVS != NULL) delete fromVS;
    if (toVS != NULL) delete toVS;
    return status;
}

// Symlinks are permitted only in appendable directories.  The
// evaluator cannot see them, so they are useful only for 
// browsing purposes.  Currently the "latest" link is the only
// use of this feature.
//
// (Why forbid them elsewhere?  It seems useless to allow them
// in immutable directories, because the evaluator can't see
// them.  With that forbidden, it seems useless to allow them
// in mutable directories, because they can't be checked in.
// It seems needless to allow them in volatile directories,
// because _run_tool doesn't know how to represent one in a
// result binding, and no tools we've needed to encapsulate
// so far need to create them.) 

nfsstat
do_symlink(symlinkargs* argp, AccessControl::Identity cred)
{
    ReadersWritersLock* lock = NULL;
    nfsstat status = NFS_OK;
    VestaSource* vs = NULL, *newvs = NULL;

    // Site-configurable option to forbid making symlinks through
    // the NFS interface with the symlink() system call.  They can
    // still be made by Vesta tools, by putting the symlink-to
    // attribute on a stub.
    if (!allowSymlink) {
        status = NFSERR_ACCES;
	goto finish;
    }

    // Find the parent directory
    RECORD_TIME_POINT;
    vs = ((LongId*) &argp->from.dir)->lookup(LongId::writeLock, &lock);
    RECORD_TIME_POINT;

    RWLOCK_LOCKED_REASON(lock, "NFS:symlink");

    if (vs == NULL) {
      if(*((LongId*) &argp->from.dir) == NullLongId)
	{
	  status = NFSERR_INVAL;
	}
      else
	{
	  status = NFSERR_STALE;
	  stalemsg("symlink", (LongId*) &argp->from.dir);
	}
      goto finish;
    }
    
    if (vs->type != VestaSource::appendableDirectory) {
	status = NFSERR_INVAL;
	goto finish;
    }
    
    // If name is already in use, make sure it's a ghost or stub
    //  with the symlink-to attribute.  (Unfortunately, "ln -s"
    //  thinks that you can never symlink from a name that's in use,
    //  and refuses to even try.)
    VestaSource::errorCode err;
    err = vs->lookup(argp->from.name, newvs, cred);
    if (err == VestaSource::ok) {
	// Stub or ghost is present; make sure it's already a symlink.
	if ((newvs->type != VestaSource::stub && 
	     newvs->type != VestaSource::ghost) ||
	    newvs->getAttribConst("symlink-to") == NULL) {
	    status = NFSERR_EXIST;
	    goto finish;
	}
    } else if (strcmp(argp->from.name, "") == 0 ||
	       strcmp(argp->from.name, ".") == 0 ||
	       strcmp(argp->from.name, "..") == 0) {
	status = NFSERR_EXIST;
	goto finish;
    } else if (err == VestaSource::notFound) {
	if (!vs->master) {
	    status = NFSERR_ROFS;
	    goto finish;
	}
	err = vs->insertStub(argp->from.name, true, 
			     cred, VestaSource::dontReplace, &newvs);
	if (err != VestaSource::ok) {
	    status = xlate_vserr(err);
	    goto finish;
	}
    } else {
	status = xlate_vserr(err);
	goto finish;
    }

    err = newvs->setAttrib("symlink-to", argp->to, cred);

    // Ignore requested attributes (ok per RFC 1094).

  finish:
    if (lock != NULL) lock->releaseWrite();
    if (newvs != NULL) delete newvs;
    if (vs != NULL) delete vs;
    return status;
}

// Create a mutable or appendable directory
nfsstat
do_mkdir(createargs* argp, diropokres* dp, AccessControl::Identity cred)
{
    ReadersWritersLock* lock;
    nfsstat status = NFS_OK;
    bool needMasterHint = false;

    // Find the parent directory
    VestaSource* vs =
      ((LongId*) &argp->where.dir)->lookup(LongId::writeLock, &lock);
    VestaSource* newvs = NULL;
    if (vs == NULL) {
      if(*((LongId*) &argp->where.dir) == NullLongId)
	{
	  status = NFSERR_INVAL;
	}
      else
	{
	  status = NFSERR_STALE;
	  stalemsg("mkdir", (LongId*) &argp->where.dir);
	}
      goto finish;
    }
    RWLOCK_LOCKED_REASON(lock, "NFS:mkdir");
    switch (vs->type) {
      case VestaSource::immutableDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
	{
	    // Try to do copy-on-write
	    VestaSource* newvs;
	    VestaSource::errorCode err = vs->makeMutable(newvs);
	    if (err != VestaSource::ok) {
		status = xlate_vserr(err);
		goto finish;
	    }
	    delete vs;
	    vs = newvs;
	}
	break;
	
      default:
	status = NFSERR_NOTDIR;
	goto finish;
	
      case VestaSource::appendableDirectory:
	if (!vs->master) {
	  // Creating a (master) directory in a nonmaster parent,
	  // typically /vesta.  Note that only vwizard can do this.
	  needMasterHint = true;
	}
	break;

      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
	break;
    }
    
    // Check that name isn't already in use
    VestaSource* vs2;
    VestaSource::errorCode err;
    err = vs->lookup(argp->where.name, vs2, cred);
    if (err == VestaSource::ok ||
	strcmp(argp->where.name, "") == 0 ||
	strcmp(argp->where.name, ".") == 0 ||
	strcmp(argp->where.name, "..") == 0) {
	if (vs2 != NULL) delete vs2;
	status = NFSERR_EXIST;
	goto finish;
    } else if (err != VestaSource::notFound) {
	status = xlate_vserr(err);
	goto finish;
    }
    
    // Insert new directory into the parent
    if (vs->type == VestaSource::appendableDirectory) {
      RECORD_TIME_POINT;
        VRLog.start();
	err = vs->insertAppendableDirectory(argp->where.name, true, 
					    cred, VestaSource::dontReplace,
					    &newvs);
	if (needMasterHint && err == VestaSource::ok) {
	  // Don't set master-repository hint if it's the empty
	  // string.
	  if(!myMasterHint.Empty())
	    newvs->setAttrib("master-repository", myMasterHint.cchars(), NULL);
	}
	VRLog.commit();
    } else {
      RECORD_TIME_POINT;
	err = vs->insertMutableDirectory(argp->where.name, NULL, true, 
					 cred, VestaSource::dontReplace,
					 &newvs);
    }
    RECORD_TIME_POINT;
    if (err != VestaSource::ok) {
	status = xlate_vserr(err);
	goto finish;
    }

    // Apply requested attributes and build return value
    *(LongId*) &dp->file = newvs->longid;
    status = apply_sattr(&argp->attributes, newvs, -1, NULL, &dp->attributes);

  finish:
    if (lock != NULL) lock->releaseWrite();
    if (newvs != NULL) delete newvs;
    if (vs != NULL) delete vs;
    return status;
}


struct FindLastClosure {
    long last;
};

// Helper for expanding the special $LAST token in a symlink; see below
static bool
findLastCallback(void* closure, VestaSource::typeTag type, Arc arc,
		 unsigned int index, unsigned int pseudoInode,
		 ShortId filesid, bool master)
{
    FindLastClosure* cl = (FindLastClosure*) closure;

    const char *p = arc;
    if (type == VestaSource::stub || type == VestaSource::ghost) return true;
    if (*p == '0' && p[1] != '\0') return true;
    while (*p) {
	if (!isdigit(*p)) return true;
	p++;
    }
    long val = strtoul(arc, NULL, 10);
    if (val > cl->last) cl->last = val;
    return true;
}


// Read symbolic link.  
nfsstat
do_readlink(nfs_fh *fh, nfspath np, AccessControl::Identity cred)
{
    nfsstat status = NFS_OK;
    ReadersWritersLock* lock;
    VestaSource* vs = ((LongId*) fh)->lookup(LongId::readLock, &lock);
    if (vs == NULL) {
      if(*((LongId*) fh) == NullLongId)
	{
	  status = NFSERR_INVAL;
	}
      else
	{
	  status = NFSERR_STALE;
	  stalemsg("readlink", (LongId*) fh);
	}
      goto finish;
    }

    RWLOCK_LOCKED_REASON(lock, "NFS:readlink");

    if (shortIdSymlink 
	&& FileShortIdRootLongId.isAncestorOf(vs->longid)) {
	//
	// Optionally manifest an immutable file in a
        // volatileROEDirectory (or evaluatorROEDirectory) as a
        // symbolic link to its underlying shortid file.  Feature
        // currently unused.
	//
	char *name = SourceOrDerived::shortIdToName(vs->shortId());
	strcpy(np, shortIdSymlinkPrefix.cchars());
	strcat(np, name);
	if (shortIdSymlinkLength == 0) shortIdSymlinkLength = strlen(np);
	delete[] name;
    } else if (vs->type == VestaSource::stub ||
	       vs->type == VestaSource::ghost) {
	//
	// A stub or ghost is manifested as a symbolic link
        // when viewed through the NFS interface if it has the mutable
        // attribute "symlink-to"; the attribute's value is either
        // the link's value, or the special token $LAST.
     	//
	const char *value = vs->getAttribConst("symlink-to");
	if (value == NULL) {
	    status = NFSERR_INVAL;
	} else if (strcmp(value, "$LAST") == 0) {
	    //
	    // The symlink's value is the arc in the current directory
	    // that consists entirely of decimal digits, has no leading
	    // zeroes, is not bound to a ghost or stub, and has the
	    // largest numeric value of all such arcs.  If there are no
	    // such arcs, the value is -1.
	    //
	    FindLastClosure cl;
	    cl.last = -1;
	    VestaSource* parent = vs->longid.getParent().lookup();
	    parent->list(0, findLastCallback, &cl, NULL);
	    sprintf(np, "%d", cl.last);
	    delete parent;
	} else {
	    strcpy(np, value);
	}
    } else {
	status = NFSERR_INVAL;
    }

    delete vs;
  finish:
    if (lock != NULL) lock->releaseRead();
    return status;
}

static Text statfs_filename;

// Translate statfs into statfs of the metadata root. This will not
// be too meaningful unless all the metadata and the sid files are
// on the same filesystem.
//
nfsstat
do_statfs(nfs_fh* argp, result_types* resp, AccessControl::Identity cred)
{
    nfsstat status = NFS_OK;
    // Prefer statvfs if available, otherwise use statfs
#if defined(HAVE_STATVFS)
    struct statvfs stfs;
    int ok = statvfs(statfs_filename.cchars(), &stfs);
#elif defined(HAVE_STATFS)
    struct statfs stfs;
    int ok = statfs(statfs_filename.cchars(), &stfs);
#else
#error Need statfs alternative to get filesystem stats
#endif
    if (ok == -1) {
	status = xlate_errno(errno);
	goto finish;
    }
    
    resp->r_statfsres.statfsres_u.reply.tsize = stfs.f_bsize;
    resp->r_statfsres.statfsres_u.reply.bsize = stfs.f_bsize;
    resp->r_statfsres.statfsres_u.reply.blocks = stfs.f_blocks;
    resp->r_statfsres.statfsres_u.reply.bfree = stfs.f_bfree;
    resp->r_statfsres.statfsres_u.reply.bavail = stfs.f_bavail;

  finish:
    resp->r_statfsres.status = status;
    return status;
}

void
GlueInit()
{
    statfs_filename = VestaConfig::get_Text("Repository", "metadata_root")
      + VestaConfig::get_Text("Repository", "sid_dir") + "statfs_target";
    int fd = creat(statfs_filename.cchars(), 0666);
    if (fd < 0) {
      int errno_save = errno;
      Text emsg = Basics::errno_Text(errno_save);
      Repos::dprintf(DBG_ALWAYS,
		     "error creating statfs_target %s: %s (errno = %d)\n",
		     statfs_filename.cchars(), emsg.cchars(), errno_save);
    } else {
	close(fd);
    }
    Text t;
    if (VestaConfig::get("Repository", "ShortId_symlink", t)) {
        shortIdSymlinkPrefix = t;
        shortIdSymlink = true;
    }
    allowSymlink =
      (bool) VestaConfig::get_int("Repository", "allow_symlink");

    if (VestaConfig::get("Repository", "cow_max", t)) {
        cowMax = atoi(t.cchars());
    }
    cowInProgress = NEW_PTRFREE_ARRAY(CowInProgress, cowMax);
    int i;
    for (i=0; i<cowMax; i++) {
      cowInProgress[i].active = false;
    }

    bsdChown =
      (VestaConfig::get_Text("Repository", "chown_semantics")=="BSD");
}
