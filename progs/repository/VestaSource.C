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
// VestaSource.C
//
// Server-side implementation for VestaSource.H
//

#include <assert.h>
#include <iostream>
#include <iomanip>
#include "VestaSource.H"
#include "VDirChangeable.H"
#include "VDirVolatileRoot.H"
#include "VDirEvaluator.H"
#include "VForward.H"
#include "SourceOrDerived.H"
#include "Recovery.H"
#include "VRWeed.H"
#include "VRConcurrency.H"
#include "VestaConfig.H"
#include "VLeaf.H"
#include "DirShortId.H"
#include "FdCache.H"
#include "VLogHelp.H"
#include "FPShortId.H"
#include "logging.H"
#include "UniqueId.H"
#include "VestaSourceImpl.H"
#include "CopyShortId.H"
#include "MutableSidref.H"

#include "timing.H"
#include "lock_timing.H"

#include <BufStream.H>
#include <FdStream.H>

using std::cerr;
using std::endl;
using std::fstream;
using Basics::OBufStream;

// Exported globals
VestaLog VRLog;
int VRLogVersion = 1; // for recovering logs from the beginning of time
static const int LogMaxVersion = 4;  // version 2 avoids some "deleted" entries
                                     // version 3 avoids more of them
                                     // version 4 always adds "maki" log entries before "insi"
ReadersWritersLock StableLock(true);
ReadersWritersLock VolatileRootLock(true);

// Module globals
static VestaSource* repositoryRoot; // the /vesta directory
static VestaSource* mutableRoot;    // directory of mutable directories
static VDirVolatileRoot* volatileRoot;  // directory of volatile directories
static Bit32 repositoryRootAttribsRep = 0;  // really a VestaAttribsRep@
static Bit32 mutableRootAttribsRep = 0;     // really a VestaAttribsRep@
static Bit32 volatileRootAttribsRep = 0;
static Bit32 emptyAttribsRep = 0;

// The prefix (PATHNAME) is not used in the evaluator.  (PATHNAME) is
// used instead of something like TextP for historical reasons.
const FP::Tag pathnameFPTag("(PATHNAME)/vesta");  // FP by initial pathname

// A LongId encodes a Name.  Each Arc is represented as the 1-origin
// index of the Arc within the underlying Vesta directory
// representation.  (Thus that index must remain constant at a given
// site.)  Indices are assigned in a somewhat strange way to deal with
// the fact that some kinds of directory consist of a base immutable
// part plus a mutable part that records modifications:  All indices
// in an immutableDirectory are even, and all indices within (the
// mutable part of) any other type of directory are odd.
//
// The indices are packed into the LongId using a
// variable-length code.  An index is stored in a sequence of bytes,
// with the low-order 7 bits in the first byte, the next 7 bits in the
// second byte, etc.  The high-order bit of each byte is 0 if this is
// the last byte in the given index, 1 otherwise. 
//
// LongIds for names in the repository (descendents of /vesta) have
// the Arc within /vesta as their first index, and are terminated by
// the first 0 index.  LongIds for names in the mutable directory tree
// at a site have 0, 1 as their first two indices and are terminated
// by the next 0 index.  Those in the volatile tree have 0, 2, etc.

bool VestaSource::doLogging = false;

// Look up LongId to get a VestaSource.  Return NULL if the LongId
// does not correspond to any extant VestaSource.  Assumes the LongId
// is of valid format.
VestaSource*
LongId::lookup(LongId::lockKindTag lockKind, ReadersWritersLock** lock)
  throw (SRPC::failure /*can't really happen*/)
{
    VestaSource* vs;
    VestaSource* child;
    const Bit8* next = &value.byte[0];
    int kind;
    if (*next == 0) {
	kind = next[1];
	switch (kind) {
	  case 0:
	    // RepositoryRoot
	    vs = VestaSource::repositoryRoot(lockKind, lock);
	    if (vs == NULL) return NULL;
	    break;
	  case 1:
	    // Mutable
	    vs = VestaSource::mutableRoot(lockKind, lock);
	    if (vs == NULL) return NULL;
	    next += 2;
	    break;
	  case 2:
	    // Volatile
	    if (next[2] == 0) {
	      // Looking up the root itself
	      vs = VestaSource::volatileRoot(lockKind, lock);
	    } else {
	      ReadersWritersLock* dummy;
	      // Below the volatile root
	      switch (lockKind) {
	      case LongId::writeLock:
	      case LongId::readLock:
		// We need a read lock on the root, but that is not
		// the lock our caller is interested in.  We don't
		// need to save the pointer; we know it will be
		// &VolatileRootLock.  We will release this lock
		// after traversing into the root of the subtree.
		vs = VestaSource::volatileRoot(LongId::readLock, &dummy);
		RWLOCK_LOCKED_REASON(dummy, "LongId::lookup below volatile");
		break;
	      case LongId::writeLockV:
	      case LongId::readLockV:
	      case LongId::checkLock:
	      case LongId::noLock:
		// We already have a lock on the root
		vs = VestaSource::volatileRoot(LongId::noLock);
		break;
	      }
	    }
	    if (vs == NULL) return NULL;
	    next += 2;
	    break;
	  case 3:
	    // ShortId for an immutable directory
	    // Lock the repository root.
	    vs = VestaSource::repositoryRoot(lockKind, lock);
	    if (vs == NULL) return NULL;
	    next += 2;
	    break;
	  case 4:
	    // ShortId for a file
	    // No lock needed
	    vs = NULL;
	    if (lockKind == LongId::readLock ||
		lockKind == LongId::writeLock ||
		lockKind == LongId::readLockV ||
		lockKind == LongId::writeLockV) {
		*lock = NULL;
	    }
	    next += 2;

	    break;
	  case 255:
	    // Must be NullLongId
	  default:
	    if (lockKind == LongId::readLock ||
		lockKind == LongId::writeLock ||
		lockKind == LongId::readLockV ||
		lockKind == LongId::writeLockV) {
		*lock = NULL;
	    }
	    return NULL;
	}
    } else {
	// Must be a child of RepositoryRoot
	kind = 0;
	vs = VestaSource::repositoryRoot(lockKind, lock);
	if (vs == NULL) return NULL;
    }
    for (;;) {
	// Extract the next index
	unsigned int index = 0;
	int shift = 0;
	while (next < &value.byte[sizeof(value)]) {
	    index |= (*next & 0x7f) << shift;
	    shift += 7;
	    if ((*next++ & 0x80) == 0) break;
	}
	if (index == 0) break;
	VestaSource::errorCode err;
	switch (kind) {
	  default:
	    err = vs->lookupIndex(index, child, NULL);
	    break;

	  case 2:
	    // Special code to look up in the volatile root
	    RECORD_TIME_POINT;
	    err = ((VDirVolatileRoot*)vs)->
	      lookupIndexAndLock(index, child, lockKind, lock);
	    RECORD_TIME_POINT;
	    if (lockKind == LongId::readLock ||
		lockKind == LongId::writeLock) {
	      VolatileRootLock.releaseRead();
	      RECORD_TIME_POINT;
	    }
	    kind = -2; // force into default case on further iterations
	    break;

	  case 3:
	    // Special code to look up dir shortid
	    if (SourceOrDerived::dirShortId((ShortId) index)) {
	      Bit32 childSP = GetDirShortId((ShortId) index);
	      if (childSP == 0) {
		err = VestaSource::notFound;
	      } else {
		VDirChangeable* vdc = 
		  NEW_CONSTR(VDirChangeable,
			     (VestaSource::immutableDirectory,
			      (Bit8*) VMemPool::lengthenPointer(childSP)));
		child = vdc;
		memset(child->longid.value.byte, 0, sizeof(value));
		memcpy(child->longid.value.byte, value.byte, next - value.byte);
		child->master = true;
		child->fptag = vdc->fptag();
		child->ac.owner.attribs = (Bit8*) &emptyAttribsRep;
		child->ac.group.attribs = (Bit8*) &emptyAttribsRep;
		child->ac.mode = 0555;
		child->pseudoInode = index;
		err = VestaSource::ok;
	      }
	      kind = -2; // force into default case on further iterations
	    } else {
	      err = VestaSource::notFound;
	    }
	    break;

	  case 4:
	    // Special code to look up file shortid
	    if (!SourceOrDerived::dirShortId((ShortId) index)) {
	      // Assume mutable by default; glue.C corrects if needed
	      child = NEW_CONSTR(VLeaf,
				 (VestaSource::mutableFile, (ShortId)index));
	      child->longid = *this;
	      child->master = true;
	      child->fptag.FromBytes(&this->value.byte[8]);
	      child->ac.owner.attribs = (Bit8*) &volatileRootAttribsRep;
	      child->ac.group.attribs = (Bit8*) &volatileRootAttribsRep;
	      child->ac.mode = 0666;
	      child->pseudoInode = index;
	      err = VestaSource::ok;
	      next = &this->value.byte[24]; // end loop
	    } else {
	      err = VestaSource::notFound;
	    }
	    break;
	}
	if (vs != ::repositoryRoot && vs != ::mutableRoot &&
	    vs != ::volatileRoot && vs != NULL)
	  delete vs;
	if (err != VestaSource::ok) {
	    if (lock && *lock &&
		(lockKind == LongId::readLock ||
		 lockKind == LongId::writeLock ||
		 lockKind == LongId::readLockV ||
		 lockKind == LongId::writeLockV)) {
	      (*lock)->release();
	      *lock = NULL;
	    }
	    return NULL;
	}
	vs = child;
    }
    if (vs == ::repositoryRoot || vs == ::mutableRoot || vs == ::volatileRoot) {
	// Avoid sharing, so caller can safely delete return value
        vs = vs->copy();
    }
    return vs;
}


bool
LongId::valid()
  throw (SRPC::failure /*can't really happen*/)
{
  // It would be nicer if this routine avoided doing an allocation and
  // free in the case where the longid is valid.  However, at present
  // this implementation is not actually ever called, so there is no
  // point in optimizing it.  It is included only for completeness.
  // The client-side implementation does avoid an allocation within
  // the client address space, for what that's worth.
  VestaSource *vs = lookup();
  if (vs) {
    delete vs;
    return true;
  } else {
    return false;
  }
}

// Recovery code

static void
VersCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    long lversion;
    rr->getLong(c, lversion);
    VRLogVersion = lversion;
    if (VRLogVersion > LogMaxVersion) {
      Repos::dprintf(DBG_ALWAYS, "log version is %d; max supported version is %d\n",
		     VRLogVersion, LogMaxVersion);
      exit(2);
    }
}

static void
DelCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId longid;
    char arc[MAX_ARC_LEN+1];
    long ltimestamp;
    rr->getLongId(c, longid.value);
    rr->getQuotedString(c, arc, sizeof(arc));
    rr->getLong(c, ltimestamp);
    VestaSource* vs = longid.lookup();
    // Sanity check for validity of the LongId
    if(vs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid LongId in \"del\":" << endl
	     << "\tlongid = " << longid << endl;
	abort();
      }
    VestaSource::errorCode
      err = vs->reallyDelete(arc, NULL, false, ltimestamp);
    assert(err == VestaSource::ok);
    delete vs;
}

static void
InsfCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId longid;
    char arc[MAX_ARC_LEN+1];
    unsigned long ulsid;
    long lmaster;
    long ltimestamp;
    FP::Tag fptag;
    FP::Tag* pfptag;

    rr->getLongId(c, longid.value);
    rr->getQuotedString(c, arc, sizeof(arc));
    rr->getULong(c, ulsid);
    rr->getLong(c, lmaster);
    rr->getLong(c, ltimestamp);
    rr->skipWhite(c);
    if (c == ')') {
	pfptag = NULL;
    } else {
	GetFPTag(rr, c, fptag);
	pfptag = &fptag;
    }
    VestaSource* vs = longid.lookup();
    // Sanity check for validity of the LongId
    if(vs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid LongId in \"insf\":" << endl
	     << "\tlongid = " << longid << endl;
	abort();
      }
    VestaSource::errorCode err = 
      vs->insertFile(arc, (ShortId) ulsid, (bool) lmaster,
		     NULL, VestaSource::replaceDiff, NULL, ltimestamp, pfptag);
    assert(err == VestaSource::ok);
    delete vs;
}

static void
InsuCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId longid;
    char arc[MAX_ARC_LEN+1];
    unsigned long ulsid;
    long lmaster;
    long ltimestamp;
    rr->getLongId(c, longid.value);
    rr->getQuotedString(c, arc, sizeof(arc));
    rr->getULong(c, ulsid);
    rr->getLong(c, lmaster);
    rr->getLong(c, ltimestamp);
    VestaSource* vs = longid.lookup();
    // Sanity check for validity of the LongId
    if(vs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid LongId in \"insu\":" << endl
	     << "\tlongid = " << longid << endl;
	abort();
      }
    VestaSource::errorCode err =
      vs->insertMutableFile(arc, (ShortId) ulsid, (bool) lmaster,
			    NULL, VestaSource::replaceDiff, NULL, ltimestamp);
    assert(err == VestaSource::ok);
    delete vs;
}

static void
InsiCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId pLongid, cLongid;
    char arc[MAX_ARC_LEN+1];
    long lmaster;
    long ltimestamp;
    FP::Tag fptag;
    FP::Tag* pfptag;

    rr->getLongId(c, pLongid.value);
    rr->getQuotedString(c, arc, sizeof(arc));
    rr->getLongId(c, cLongid.value);
    rr->getLong(c, lmaster);
    rr->getLong(c, ltimestamp);
    rr->skipWhite(c);
    if (c == ')') {
	pfptag = NULL;
    } else {
	GetFPTag(rr, c, fptag);
	pfptag = &fptag;
    }
    VestaSource* pvs = pLongid.lookup();
    // Sanity check for validity of the parent LongId
    if(pvs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid parent LongId in \"insi\":"
	     << endl
	     << "\tparent longid = " << pLongid << endl;
	abort();
      }
    VestaSource* cvs = cLongid.lookup();
    // Sanity check for validity of the LongId
    if(!(cLongid == NullLongId) && (cvs == 0))
      {
	cerr << "FATAL ERROR in recovery: invalid child LongId in \"insi\":"
	     << endl
	     << "\tchild longid = " << cLongid << endl;
	abort();
      }
    VestaSource::errorCode err =
      pvs->insertImmutableDirectory(arc, cvs, (bool) lmaster,
        NULL, VestaSource::replaceDiff, NULL, ltimestamp, pfptag);
    assert(err == VestaSource::ok);
    delete pvs;
    delete cvs;
}

static void
InsmCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId pLongid, cLongid;
    char arc[MAX_ARC_LEN+1];
    long lmaster;
    long ltimestamp;
    rr->getLongId(c, pLongid.value);
    rr->getQuotedString(c, arc, sizeof(arc));
    rr->getLongId(c, cLongid.value);
    rr->getLong(c, lmaster);
    rr->getLong(c, ltimestamp);
    VestaSource* pvs = pLongid.lookup();
    // Sanity check for validity of the parent LongId
    if(pvs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid parent LongId in \"insm\":"
	     << endl
	     << "\tparent longid = " << pLongid << endl;
	abort();
      }
    VestaSource* cvs = cLongid.lookup();
    // Sanity check for validity of the LongId
    if(!(cLongid == NullLongId) && (cvs == 0))
      {
	cerr << "FATAL ERROR in recovery: invalid child LongId in \"insm\":"
	     << endl
	     << "\tchild longid = " << cLongid << endl;
	abort();
      }
    VestaSource::errorCode err =
      pvs->insertMutableDirectory(arc, cvs, (bool) lmaster, NULL,
				  VestaSource::replaceDiff, NULL, ltimestamp);
    assert(err == VestaSource::ok);
    delete pvs;
    delete cvs;
}

static void
InsaCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId longid;
    char arc[MAX_ARC_LEN+1];
    long lmaster;
    long ltimestamp;
    rr->getLongId(c, longid.value);
    rr->getQuotedString(c, arc, sizeof(arc));
    rr->getLong(c, lmaster);
    rr->getLong(c, ltimestamp);
    VestaSource* vs = longid.lookup();
    // Sanity check for validity of the LongId
    if(vs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid LongId in \"insa\":" << endl
	     << "\tlongid = " << longid << endl;
	abort();
      }
    VestaSource::errorCode err =
      vs->insertAppendableDirectory(arc, (bool) lmaster, NULL,
			    VestaSource::replaceDiff, NULL, ltimestamp);
    assert(err == VestaSource::ok);
    delete vs;
}

static void
InsgCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId longid;
    char arc[MAX_ARC_LEN+1];
    long lmaster;
    long ltimestamp;
    rr->getLongId(c, longid.value);
    rr->getQuotedString(c, arc, sizeof(arc));
    rr->getLong(c, lmaster);
    rr->getLong(c, ltimestamp);
    VestaSource* vs = longid.lookup();
    // Sanity check for validity of the LongId
    if(vs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid LongId in \"insg\":" << endl
	     << "\tlongid = " << longid << endl;
	abort();
      }
    VestaSource::errorCode err =
      vs->insertGhost(arc, (bool) lmaster, NULL,
		      VestaSource::replaceDiff, NULL, ltimestamp);
    assert(err == VestaSource::ok);
    delete vs;
}

static void
InssCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId longid;
    char arc[MAX_ARC_LEN+1];
    long lmaster;
    long ltimestamp;
    rr->getLongId(c, longid.value);
    rr->getQuotedString(c, arc, sizeof(arc));
    rr->getLong(c, lmaster);
    rr->getLong(c, ltimestamp);
    VestaSource* vs = longid.lookup();
    // Sanity check for validity of the LongId
    if(vs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid LongId in \"inss\":" << endl
	     << "\tlongid = " << longid << endl;
	abort();
      }
    VestaSource::errorCode err =
      vs->insertStub(arc, (bool) lmaster, NULL,
		     VestaSource::replaceDiff, NULL, ltimestamp);
    assert(err == VestaSource::ok);
    delete vs;
}

static void
RenCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId tLongid, fLongid;
    char tArc[MAX_ARC_LEN+1], fArc[MAX_ARC_LEN+1];
    long ltimestamp;
    rr->getLongId(c, tLongid.value);
    rr->getQuotedString(c, tArc, sizeof(tArc));
    rr->getLongId(c, fLongid.value);
    rr->getQuotedString(c, fArc, sizeof(fArc));
    rr->getLong(c, ltimestamp);
    VestaSource* tvs = tLongid.lookup();
    // Sanity check for validity of the LongId
    if(tvs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid 'to' LongId in \"ren\":"
	     << endl
	     << "\t'to' longid = " << tLongid << endl;
	abort();
      }
    VestaSource* fvs = fLongid.lookup();
    // Sanity check for validity of the LongId
    if(fvs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid 'from' LongId in \"ren\":"
	     << endl
	     << "\t'from' longid = " << fLongid << endl;
	abort();
      }
    VestaSource::errorCode err =
      tvs->renameTo(tArc, fvs, fArc, NULL,
		    VestaSource::replaceDiff, ltimestamp);
    assert(err == VestaSource::ok);
    delete tvs;
    delete fvs;
}

static void
MakmCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId longid;
    unsigned long ulindex, ulsid;
    rr->getLongId(c, longid.value);
    rr->getULong(c, ulindex);
    rr->getULong(c, ulsid);
    VestaSource* vs = longid.lookup();
    // Sanity check for validity of the LongId
    if(vs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid LongId in \"makm\":" << endl
	     << "\tlongid = " << longid << endl;
	abort();
      }
    VestaSource* newvs;
    VestaSource::errorCode err =
      ((VDirChangeable*) vs)->makeIndexMutable((unsigned int) ulindex, newvs,
					       (ShortId) ulsid);
    assert(err == VestaSource::ok);
    delete vs;
    delete newvs;
}

static void
MakiCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId longid;
    unsigned long ulindex;
    unsigned long ulsid;
    FP::Tag fptag;
    FP::Tag* pfptag;

    rr->getLongId(c, longid.value);
    rr->getULong(c, ulindex);
    rr->skipWhite(c);
    pfptag = NULL;
    ulsid = NullShortId;
    if (c != ')') {
	GetFPTag(rr, c, fptag);
	pfptag = &fptag;
	rr->skipWhite(c);
	if (c != ')') {
	    rr->getULong(c, ulsid);
	}
    }
    VestaSource* vs = longid.lookup();
    // Sanity check for validity of the LongId
    if(vs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid LongId in \"maki\":" << endl
	     << "\tlongid = " << longid << endl;
	abort();
      }
    ((VDirChangeable*) vs)->
      makeIndexImmutable((unsigned int) ulindex, pfptag, (ShortId) ulsid);
    delete vs;
}

static void
Copy2mCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId longid;
    unsigned long ulindex, ulsid;
    rr->getLongId(c, longid.value);
    rr->getULong(c, ulindex);
    VestaSource* vs = longid.lookup();
    // Sanity check for validity of the LongId
    if(vs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid LongId in \"copy2m\":" << endl
	     << "\tlongid = " << longid << endl;
	abort();
      }
    VestaSource* newvs;
    VestaSource::errorCode err =
      ((VDirChangeable*) vs)->copyIndexToMutable((unsigned int) ulindex, newvs);
    assert(err == VestaSource::ok);
    delete vs;
    delete newvs;
}

static void
MastCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId longid;
    unsigned long ulindex, ulstate;

    rr->getLongId(c, longid.value);
    rr->getULong(c, ulindex);
    rr->getULong(c, ulstate);
    VestaSource* vs = longid.lookup();
    // Sanity check for validity of the LongId
    if(vs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid LongId in \"mast\":" << endl
	     << "\tlongid = " << longid << endl;
	abort();
      }
    VestaSource::errorCode err =
      ((VDirChangeable*) vs)->
      setIndexMaster((unsigned int) ulindex, (bool)ulstate);
    assert(err == VestaSource::ok);
    delete vs;
}

static void
AttrCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId longid;
    long lop, ltimestamp;
    char* name;
    char* value;
    rr->getLongId(c, longid.value);
    rr->getLong(c, lop);
    rr->getNewQuotedString(c, name);
    rr->getNewQuotedString(c, value);
    rr->getLong(c, ltimestamp);
    VestaSource* vs = longid.lookup();
    // Sanity check for validity of the LongId
    if(vs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid LongId in \"attr\":" << endl
	     << "\tlongid = " << longid << endl;
	abort();
      }

    // Handle a log entry created by an old repository with a bug that
    // could add attributes to obejcts in an immutable base directory.
    // If this is an object under the mutable root that doesn't have
    // attributes, just print a message and continue.  (Unfortunately,
    // we can't do anything to fix this, as making the object mutable
    // could change indexes of other directory entries added later,
    // and screw up the rest of the log replay!)
    if(!vs->hasAttribs() && MutableRootLongId.isAncestorOf(vs->longid))
      {
	char longid_buf[65];
	OBufStream longid_ost(longid_buf, sizeof(longid_buf));
	longid_ost << longid;
	Repos::dprintf(DBG_ALWAYS, 
		       "WARNING: skipping replay of attribute write"
		       "on object below mutable root without attributes: "
		       "(attr %s %d \"%s\" \"%s\" %d)\n",
		       longid_ost.str(), lop, name, value, ltimestamp);
      }
    else
      {
	VestaSource::errorCode err =
	  vs->writeAttrib((VestaSource::attribOp) lop, name, value, NULL,
			  ltimestamp);
	assert(err == VestaSource::ok);
      }
    delete [] name;
    delete [] value;
    delete vs;
}

static void
TimeCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId longid;
    unsigned long ultime;

    rr->getLongId(c, longid.value);
    rr->getULong(c, ultime);
    VestaSource* vs = longid.lookup();
    // Sanity check for validity of the LongId
    if(vs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid LongId in \"time\":" << endl
	     << "\tlongid = " << longid << endl;
	abort();
      }
    VestaSource::errorCode err =
      ((VDirChangeable*) vs)->setTimestamp((time_t)ultime);
    assert(err == VestaSource::ok);
    delete vs;
}

static void
ColbCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    LongId cLongid;

    rr->getLongId(c, cLongid.value);
    VestaSource *cvs = cLongid.lookup();
    // Sanity check for validity of the LongId
    if(cvs == 0)
      {
	cerr << "FATAL ERROR in recovery: invalid LongId in \"colb\":"
	     << endl
	     << "\tlongid = " << cLongid << endl;
	abort();
      }
    VestaSource::errorCode err = cvs->collapseBase();
    assert(err == VestaSource::ok);
    delete cvs;
}

static void 
CorrectRootModes(VestaSource* dir)
{
    const char* val;
    if ((val = dir->getAttribConst("#mode")) == NULL) {
	dir->ac.mode = 0755;
    } else {
	dir->ac.mode = AccessControl::parseModeBits(val);
    }
}

// Real methods

VestaSource*
VestaSource::repositoryRoot(LongId::lockKindTag lockKind,
			    ReadersWritersLock** lock)
  throw (SRPC::failure /*can't really happen*/)
{
    switch (lockKind) {
      case LongId::noLock:
	if (lock != NULL) *lock = NULL;
	break;
      case LongId::readLock:
      case LongId::readLockV:
	RECORD_TIME_POINT;
	StableLock.acquireRead();
	RECORD_TIME_POINT;
	*lock = &StableLock;
	break;
      case LongId::writeLock:
      case LongId::writeLockV:
	RECORD_TIME_POINT;
	StableLock.acquireWrite();
	RECORD_TIME_POINT;
	*lock = &StableLock;
	break;
      case LongId::checkLock:
	if (*lock != &StableLock) return NULL;
	break;
    }	
    ::repositoryRoot->resync();
    CorrectRootModes(::repositoryRoot);
    return ::repositoryRoot;
}

VestaSource*
VestaSource::mutableRoot(LongId::lockKindTag lockKind,
			 ReadersWritersLock** lock)
  throw (SRPC::failure /*can't really happen*/)
{
    switch (lockKind) {
      case LongId::noLock:
	if (lock != NULL) *lock = NULL;
	break;
      case LongId::readLock:
      case LongId::readLockV:
	RECORD_TIME_POINT;
	StableLock.acquireRead();
	RECORD_TIME_POINT;
	*lock = &StableLock;
	break;
      case LongId::writeLock:
      case LongId::writeLockV:
	RECORD_TIME_POINT;
	StableLock.acquireWrite();
	RECORD_TIME_POINT;
	*lock = &StableLock;
	break;
      case LongId::checkLock:
	if (*lock != &StableLock) return NULL;
	break;
    }	
    ::mutableRoot->resync();
    CorrectRootModes(::mutableRoot);
    return ::mutableRoot;
}

VestaSource*
VestaSource::volatileRoot(LongId::lockKindTag lockKind,
			  ReadersWritersLock** lock)
  throw (SRPC::failure /*can't really happen*/)
{
    switch (lockKind) {
      case LongId::noLock:
      case LongId::readLockV:
      case LongId::writeLockV:
	if (lock != NULL) *lock = NULL;
	break;
      case LongId::readLock:
	RECORD_TIME_POINT;
	VolatileRootLock.acquireRead();
	RECORD_TIME_POINT;
	*lock = &VolatileRootLock;
	break;
      case LongId::writeLock:
	RECORD_TIME_POINT;
	VolatileRootLock.acquireWrite();
	RECORD_TIME_POINT;
	*lock = &VolatileRootLock;
	break;
      case LongId::checkLock:
	if (*lock != &VolatileRootLock) return NULL;
	break;
    }	
    return ::volatileRoot;
}

void 
VestaSource::init() throw ()
{
    // Don't log during recovery
    doLogging = false;

    // Create RepositoryRoot and MutableRoot empty, to be populated
    // by recovery.
    ::repositoryRoot = NEW_CONSTR(VDirChangeable,
				  (VestaSource::appendableDirectory));
    ::repositoryRoot->longid = RootLongId;
    ::repositoryRoot->pseudoInode = 1;
    ::repositoryRoot->attribs = (Bit8*) &repositoryRootAttribsRep;
    ::repositoryRoot->ac.owner.attribs = (Bit8*) &repositoryRootAttribsRep;
    ::repositoryRoot->ac.group.attribs = (Bit8*) &repositoryRootAttribsRep;
    ::repositoryRoot->fptag = pathnameFPTag;
    
    ::mutableRoot = NEW_CONSTR(VDirChangeable,
			       (VestaSource::mutableDirectory));
    ::mutableRoot->longid = MutableRootLongId;
    ::mutableRoot->pseudoInode = 2;
    ::mutableRoot->attribs = (Bit8*) &mutableRootAttribsRep;
    ::mutableRoot->ac.owner.attribs = (Bit8*) &mutableRootAttribsRep;
    ::mutableRoot->ac.group.attribs = (Bit8*) &mutableRootAttribsRep;
    ::mutableRoot->fptag = FP::Tag("");
    ::mutableRoot->master = true;
    
    VDirVolatileRoot::init();
    ::volatileRoot = NEW(VDirVolatileRoot);
    ::volatileRoot->type = VestaSource::volatileDirectory; //not exactly...
    ::volatileRoot->longid = VolatileRootLongId;
    ::volatileRoot->pseudoInode = 3;
    ::volatileRoot->fptag = FP::Tag("");
    ::volatileRoot->attribs = (Bit8*) &volatileRootAttribsRep;
    ::volatileRoot->ac.owner.attribs = (Bit8*) &volatileRootAttribsRep;
    ::volatileRoot->ac.group.attribs = (Bit8*) &volatileRootAttribsRep;
    ::volatileRoot->ac.mode = 0755;
    ::volatileRoot->master = true;

    // Register recovery routines
    RegisterRecoveryCallback("del", DelCallback);
    RegisterRecoveryCallback("insf", InsfCallback);
    RegisterRecoveryCallback("insu", InsuCallback);
    RegisterRecoveryCallback("insi", InsiCallback);
    RegisterRecoveryCallback("insm", InsmCallback);
    RegisterRecoveryCallback("insa", InsaCallback);
    RegisterRecoveryCallback("insg", InsgCallback);
    RegisterRecoveryCallback("inss", InssCallback);
    RegisterRecoveryCallback("ren", RenCallback);
    RegisterRecoveryCallback("makm", MakmCallback);
    RegisterRecoveryCallback("maki", MakiCallback);
    RegisterRecoveryCallback("copy2m", Copy2mCallback);
    RegisterRecoveryCallback("mast", MastCallback);
    RegisterRecoveryCallback("attr", AttrCallback);
    RegisterRecoveryCallback("time", TimeCallback);
    RegisterRecoveryCallback("vers", VersCallback);
    RegisterRecoveryCallback("colb", ColbCallback);

    // Initialize weeding (garbage collection) machinery
    InitVRWeed();
}

void 
VestaSource::recoveryDone() throw ()
{
    ::repositoryRoot->resync();
    ::mutableRoot->resync();

    // Set volatile root owner and group -- not logged, so we have to
    // reset them after each recovery.  They do go into the volatile
    // portion of a checkpoint, so we don't have to do this after
    // checkpointing.
    ::volatileRoot->writeAttrib(VestaAttribs::opSet, "#owner",
				AccessControl::runtoolUser);
    ::volatileRoot->writeAttrib(VestaAttribs::opSet, "#group",
				AccessControl::vadminGroup);

    doLogging = true;

    // Rebuild the mutable root shortid reference count if it wasn't
    // rebuilt earlier.

    // Note that we must do this after enabling logging, as this step
    // also involves some cleanup for a bug in older versions of the
    // repository by adding additional "maki" log entries.  We must
    // also do this before moving to the most recent log version, as
    // we need to perform the cleanup before adding a "vers" for the
    // current log version (in case we crash before finishing the
    // cleanup).
    if(!MutableSidrefRecoveryCheck())
      {
	MutableSidrefInit();
      }

    if (VRLogVersion < LogMaxVersion) {
      // use latest version of semantics for new entries
      VRLogVersion = LogMaxVersion;
      char logrec[512];
      OBufStream ost(logrec, sizeof(logrec));
      // ('vers' version)
      ost << "(vers " << VRLogVersion << ")\n";
      VRLog.start();
      VRLog.put(ost.str());
      VRLog.commit();
    }

}

VestaSource::errorCode
VestaSource::makeMutable(VestaSource*& result, ShortId sid,
			 Basics::uint64 copyMax,
			 AccessControl::Identity who)
  throw (SRPC::failure /*can't really happen*/)
{
    if (!ac.check(who, AccessControl::write))
      return VestaSource::noPermission;
    if (!(MutableRootLongId.isAncestorOf(longid) ||
	  VolatileRootLongId.isAncestorOf(longid))) {
	return VestaSource::inappropriateOp;
    }
    unsigned int index;
    VestaSource* parent = longid.getParent(&index).lookup();
    if (parent == NULL)
      return VestaSource::inappropriateOp;
    VestaSource::errorCode err;
    if (parent->type == VestaSource::immutableDirectory ||
	parent->type == VestaSource::evaluatorDirectory ||
	parent->type == VestaSource::evaluatorROEDirectory) {
	VestaSource* muparent;
	err = parent->makeMutable(muparent, NullShortId,
				  (Basics::uint64) -1, who);
	if (err != VestaSource::ok) return err;
	delete parent;
	parent = muparent;
    }
    err = ((VDirChangeable*) parent)->
      makeIndexMutable(index, result, sid, copyMax, who);
    delete parent;
    return err;
}

VestaSource::errorCode
VestaSource::copyToMutable(VestaSource*& result,
			   AccessControl::Identity who)
  throw (SRPC::failure /*can't really happen*/)
{
    if (!ac.check(who, AccessControl::write))
      return VestaSource::noPermission;
    if (!MutableRootLongId.isAncestorOf(longid)) {
	return VestaSource::inappropriateOp;
    }
    unsigned int index;
    VestaSource* parent = longid.getParent(&index).lookup();
    if (parent == NULL)
      return VestaSource::inappropriateOp;
    VestaSource::errorCode err;
    if (parent->type != VestaSource::mutableDirectory) {
	VestaSource* muparent;
	err = parent->makeMutable(muparent, NullShortId,
				  (Basics::uint64) -1, who);
	if (err != VestaSource::ok) return err;
	delete parent;
	parent = muparent;
    }
    err = ((VDirChangeable*) parent)->
      copyIndexToMutable(index, result, who);
    delete parent;
    return err;
}

struct MakeFilesImmutableClosure {
    VestaSource *vs;
    VestaSource::errorCode err;
    unsigned int fpThreshold;
};

static bool
MakeFilesImmutableCallback(void* closure, VestaSource::typeTag type, Arc arc,
			   unsigned int index, Bit32 pseudoInode,
			   ShortId filesid, bool master)
{
    MakeFilesImmutableClosure *cl = (MakeFilesImmutableClosure *) closure;
    MakeFilesImmutableClosure newcl;

    switch (type) {
      case VestaSource::mutableFile:
	((VDirChangeable*)cl->vs)->makeIndexImmutable(index, cl->fpThreshold);
	break;

      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
	cl->err = cl->vs->lookupIndex(index, newcl.vs); 
	if (cl->err != VestaSource::ok) return false;
	newcl.err = VestaSource::ok;
	newcl.fpThreshold = cl->fpThreshold;
	cl->err =
	  newcl.vs->list(0, MakeFilesImmutableCallback, &newcl, NULL, true);
	delete newcl.vs;
	if (cl->err != VestaSource::ok) return false;
	if (newcl.err != VestaSource::ok) {
	    cl->err = newcl.err;
	    return false;
	}
	break;

      default:
	break;
    }
    return true;
}


VestaSource::errorCode
VestaSource::makeFilesImmutable(unsigned int fpThreshold,
				AccessControl::Identity who)
  throw (SRPC::failure /*can't really happen*/)
{
    if (!ac.check(who, AccessControl::write))
      return VestaSource::noPermission;

    MakeFilesImmutableClosure cl;
    switch (type) {
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
	break;
      case VestaSource::immutableFile:
	return VestaSource::ok;
      case VestaSource::mutableFile:
	{
	    unsigned int index;
	    LongId plongid = this->longid.getParent(&index);
	    cl.vs = plongid.lookup();
	    if (cl.vs == NULL) return VestaSource::inappropriateOp;
	    cl.err = VestaSource::ok;
	    cl.fpThreshold = fpThreshold;
	    MakeFilesImmutableCallback(&cl, type, NULL, index,
				       pseudoInode, shortId(), master);
	    delete cl.vs;
	    return cl.err;
	}
      default:
	return VestaSource::inappropriateOp;
    }
    cl.vs = this;
    cl.err = VestaSource::ok;
    cl.fpThreshold = fpThreshold;
    VestaSource::errorCode err =
      list(0, MakeFilesImmutableCallback, &cl, NULL, true);
    if (err != VestaSource::ok) return err;
    return cl.err;
}    

VestaSource::errorCode
VestaSource::lookupPathname(const char* pathname, VestaSource*& result,
			    AccessControl::Identity who, char pathnameSep)
  throw (SRPC::failure /*can't really happen*/)
{
    VestaSource* cur = this;
    while (*pathname) {
	char arcbuf[MAX_ARC_LEN+1];
	char* slash = strchr(pathname, pathnameSep);
	if (slash != NULL) {
	    strncpy(arcbuf, pathname, slash - pathname);
	    arcbuf[slash - pathname] = '\000';
	    pathname = slash + 1;
	} else {
	    strcpy(arcbuf, pathname);
	    pathname = "";
	}
	if (*arcbuf) {
	    VestaSource* child;
	    VestaSource::errorCode err = cur->lookup(arcbuf, child, who);
	    if (cur != this) delete cur;
	    cur = child;
	    if (err != VestaSource::ok) return err;
	}
    }
    if (cur == this) {
	// Need a fresh copy of this so caller can free it -- ugh
	result = cur->longid.lookup();
    } else {
	result = cur;
    }
    return VestaSource::ok;
}

VestaSource::errorCode
VestaSource::writeAttrib(VestaAttribs::attribOp op, const char* name,
			 const char* value,
			 AccessControl::Identity who, time_t timestamp)
  throw (SRPC::failure /*can't really happen*/)
{
    if (!hasAttribs()) return VestaSource::invalidArgs;

    VestaSource::errorCode err = VestaSource::ok;
    if (name[0] == '#') {
	AccessControl::Class acc;
	if (strcmp(name, "#replicate-from") == 0 ||
	    strcmp(name, "#mastership-from") == 0 ||
	    strcmp(name, "#mastership-to") == 0) {
	  acc = AccessControl::administrative;
	} else if ((op == opSet || op == opAdd) &&
		   strcmp(name, "#setuid") == 0) {
	  acc = AccessControl::setuid;
	} else if ((op == opSet || op == opAdd) &&
		   strcmp(name, "#setgid") == 0) {
	  acc = AccessControl::setgid;
	} else {
	  acc = AccessControl::ownership;
	}
	if (!ac.check(who, acc, value)) {
	  err = VestaSource::noPermission;
	}
    } else {
	if (!ac.check(who, AccessControl::write)) {
	    err = VestaSource::noPermission;
	}
    }
    if (err != VestaSource::ok) {
      // Return a permission error only if this write would
      // actually change the history.
      if (wouldWriteAttrib(op, name, value, timestamp)) {
	return err;
      } else {
	return VestaSource::ok;
      }
    }

    err = VestaAttribs::writeAttrib(op, name, value, timestamp);

    if (err == VestaSource::ok && VestaSource::doLogging &&
	!VolatileRootLongId.isAncestorOf(longid)) {
        // Operation succeeded; log it.
	char buf[512];
	// ('attr' longid op name value timestamp)
	VRLog.start();
	{
	  OBufStream ost(buf, sizeof(buf));
	  ost << "(attr " << longid << " " << ((int) op) << " ";
	  VRLog.put(ost.str());
	}
	LogPutQuotedString(VRLog, name);
	VRLog.put(" ");
	LogPutQuotedString(VRLog, value);
	{
	  OBufStream ost(buf, sizeof(buf));
	  ost << " " << timestamp << ")\n";
	  VRLog.put(ost.str());
	}
	VRLog.commit();
    }
    // Special case: write had no effect so we don't log it; convert
    // to no error.
    if(err == VestaSource::nameInUse)
      err = VestaSource::ok;

    return err;
}
