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
// VDirChangeable.C
//
// Derived class for in-memory representation of Vesta directories
// that can change, including appendable and mutable.  The
// representation is also adequate for immutable directories.  (The
// original plan was to use a separate class for immutable
// directories, but that has not been done.)
//
// The purpose of this class is to provide methods that operate on the
// special packed representation.  Most of the directories that exist
// at any moment will not have objects of this class corresponding to
// them; they will exist only in the packed representation.
//

#if __linux__
#include <stdint.h>
#endif
#include <assert.h>
#include <stdlib.h>
#include <BufStream.H>
#include <FdStream.H>
#include "VDirChangeable.H"
#include "VDirEvaluator.H"
#include "VLeaf.H"
#include "VestaLog.H"
#include "VForward.H"
#include "VMemPool.H"
#include "VRConcurrency.H"
#include "VestaAttribsRep.H"
#include "logging.H"
#include "DirShortId.H"
#include "UniqueId.H"
#include "FdCache.H"
#include "VLogHelp.H"
#include "FPShortId.H"
#include "CopyShortId.H"
#include "timing.H"
#include "DebugDumpHelp.H"

using std::fstream;
using std::hex;
using std::dec;
using std::endl;
using Basics::OBufStream;

const FP::Tag nullFPTag("");
const FP::Tag uidFPTag("Textd"); // Same prefix used in evaluator (FP by UID)

// The prefix TextD is also used in the evaluator for the same
// purpose; TextX is not used in the evaluator.

const FP::Tag contentsFPTag("TextD");    // FP a nonexecutable file by content
const FP::Tag executableFPTag("TextX");  // FP an executable file by content

// Set to true once we build the shortid reference counts for the
// mutable root.  After that point, all mutable directories should
// have a non-NULL sidref.
static bool mutable_sidref_built = false;

///// High-level methods that know little about the representation /////

ShortId
VDirChangeable::shortId() throw ()
{
    if (VestaSource::type == VestaSource::immutableDirectory) {
	return (ShortId) getID();
    } else {
	return NullShortId;
    }
}

/*static*/ VestaSource*
VDCLookupResult(VDirChangeable* dir, Bit8* entry, unsigned int index)
{
    VestaSource* result;
    switch (dir->type(entry)) {
      case VestaSource::immutableFile:
      case VestaSource::mutableFile:
	result = NEW_CONSTR(VLeaf,
			    (dir->type(entry), (ShortId) dir->value(entry),
			     dir->getRefCount((ShortId) dir->value(entry))));
	result->attribs = (Bit8*) dir->attribAddr(entry);
	result->fptag = dir->efptag(entry);

	if (dir->VestaSource::type == VestaSource::volatileROEDirectory) {
	    result->longid = LongId::fromShortId(result->shortId(),
						 &result->fptag);
	    result->pseudoInode = result->shortId();
	} else if (RootLongId.isAncestorOf(dir->longid)) {
	    result->longid = dir->longid.append(index);
	    result->pseudoInode = result->shortId();
	} else {
	    result->longid = dir->longid.append(index);
	    result->pseudoInode = dir->indexToPseudoInode(index);
	}
	break;
      case VestaSource::ghost:
      case VestaSource::stub:
      case VestaSource::device:
	result = NEW_CONSTR(VLeaf,
			    (dir->type(entry), (ShortId) dir->value(entry)));
	result->attribs = (Bit8*) dir->attribAddr(entry);
	result->fptag = nullFPTag;
	result->longid = dir->longid.append(index);
	result->pseudoInode = dir->indexToPseudoInode(index);
	break;
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
	{
	    VDirChangeable* vdc =
	      NEW_CONSTR(VDirChangeable,
			 (dir->type(entry),
			  (Bit8*) VMemPool::lengthenPointer(dir->value(entry))));
	    result = vdc;
	    result->attribs = (Bit8*) dir->attribAddr(entry);
	    result->VestaSource::fptag = vdc->fptag();
	    result->longid = dir->longid.append(index);
	    result->pseudoInode = dir->indexToPseudoInode(index);
	}
	break;
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
	{
	    VDirChangeable* vdc =
	      NEW_CONSTR(VDirChangeable, 
			 (dir->type(entry),
			  (Bit8*) VMemPool::lengthenPointer(dir->value(entry))));
	    result = vdc;
	    result->attribs = (Bit8*) dir->attribAddr(entry);
	    result->VestaSource::fptag = vdc->fptag();
	    result->longid = dir->longid.append(index);
	    result->pseudoInode = vdc->getID();
	    vdc->sidref = dir->sidref;
	}
	break;
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
	result = NEW_CONSTR(VDirEvaluator,
			    (dir->type(entry),
			     (Bit8*) VMemPool::lengthenPointer(dir->value(entry))));
	result->attribs = (Bit8*) dir->attribAddr(entry);
	result->fptag = nullFPTag;
	result->longid = dir->longid.append(index);
	result->pseudoInode = dir->indexToPseudoInode(index);
	break;
      case VestaSource::deleted:
      case VestaSource::outdated:
      case VestaSource::gap:
	return NULL;
      default:
	assert(false); // cannot happen
	break;
    }
    switch (dir->VestaSource::type) {
      case VestaSource::appendableDirectory:
	// Use own master flag
	result->VestaSource::master = dir->masterFlag(entry);
	break;
      case VestaSource::immutableDirectory:
	// Inherit parent master flag
	result->VestaSource::master = dir->VestaSource::master;
	break;
      case VestaSource::mutableDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
	// Nonreplicated -- always master
	result->VestaSource::master = true;
	break;
      default:
	assert(false); // cannot happen
	break;
    } 

    const char* val;
    if ((val = result->getAttribConst("#owner")) == NULL) {
	result->ac.owner = dir->ac.owner;
    } else {
	result->ac.owner = *result;
    }
    if ((val = result->getAttribConst("#group")) == NULL) {
	result->ac.group = dir->ac.group;
    } else {
	result->ac.group = *result;
    }
    // Note that special treatment of search/execute bits
    // for files is not handled here.  A file's executable
    // flag is recorded in the underlying file system as a 
    // property of the SourceOrDerived.  Through the 
    // VestaSource interface, the 111 bits of the mode are 
    // always search permission.
    if ((val = result->getAttribConst("#mode")) == NULL) {
	// setuid and setgid bits are not inherited
	result->ac.mode = dir->ac.mode & 0777;
    } else {
	result->ac.mode = AccessControl::parseModeBits(val);
    }
    return result;
}

VestaSource::errorCode
VDirChangeable::lookup(Arc arc, VestaSource*& result,
		       AccessControl::Identity who, unsigned int indexOffset)
  throw ()
{
  // Start by initializing the result, in case we fail before setting
  // it to something useful.
  result = 0;

  if (!ac.check(who, AccessControl::search))
    return VestaSource::noPermission;

  Bit8* entry;
  unsigned int rawIndex;
  VDirChangeable cur = *this; // so that we can change cur.rep
  for (;;) {
    entry = cur.findArc(arc, rawIndex, true);
    if (entry == NULL) {
      // Look in base directory
      Bit8* baseRep = cur.base();
      if (baseRep == NULL) {
	result = NULL;
	return VestaSource::notFound;
      }
      if (VestaSource::type == VestaSource::volatileDirectory ||
	  VestaSource::type == VestaSource::volatileROEDirectory) {

	// Recurse: base has different object type
	VestaSource* baseVS =
	  NEW_CONSTR(VDirEvaluator,
		     (VestaSource::type == volatileDirectory ?
		      VestaSource::evaluatorDirectory :
		      VestaSource::evaluatorROEDirectory, baseRep));
	baseVS->longid = longid;
	baseVS->pseudoInode = pseudoInode;
	baseVS->ac = ac;
	baseVS->VestaSource::master = VestaSource::master;
	VestaSource::errorCode err;
	assert(indexOffset == 0);
	err = baseVS->lookup(arc, result, NULL, 0);
	delete baseVS;
	return err;

      } else {
	// Iterate: base has same object type.
	// This is essentially an optimized tail recursion, to avoid
	// stack overflow when the base chain is long.
	if (cur.VestaSource::type == VestaSource::immutableDirectory) {
	  indexOffset += 2*(cur.nextRawIndex() - 1);
	}
	cur.rep = baseRep;
	cur.repEndCache = NULL;
	cur.VestaSource::type = VestaSource::immutableDirectory;
	continue;
      }
    }
    break;
  }
  unsigned int index;
  if (cur.VestaSource::type == VestaSource::immutableDirectory) {
    index = indexOffset + 2*rawIndex;
  } else {
    assert(indexOffset == 0);
    index = 2*rawIndex - 1;
  }
  result = VDCLookupResult(&cur, entry, index);
  if (result == NULL) return VestaSource::notFound;
  // Check for LongId overflow
  if (result->longid ==  NullLongId)
    {
      delete result;
      result = 0;
      return VestaSource::longIdOverflow;
    }
  if (sameAsBase(entry)) {
    // Have to look in base to get the old longid and use that.
    // Avoids having two different longids (NFS file handles) for
    // the same file, which would cause NFS cache incoherency.
    // Recursion is OK here because we go at most one level deep.
    assert(cur.VestaSource::type != VestaSource::immutableDirectory);
    assert(indexOffset == 0);
    assert(cur.rep == rep); // i.e., we are at the top of the base chain
    Bit8* baseRep = base(); 
    assert(baseRep != NULL);
    VestaSource* baseVS;
    VestaSource* baseResult;
    if (VestaSource::type == VestaSource::volatileDirectory) {
      baseVS = NEW_CONSTR(VDirEvaluator,
			  (VestaSource::evaluatorDirectory, baseRep));
    } else if (VestaSource::type == VestaSource::volatileROEDirectory) {
      baseVS = NEW_CONSTR(VDirEvaluator,
			  (VestaSource::evaluatorROEDirectory, baseRep));
    } else {
      baseVS = NEW_CONSTR(VDirChangeable,
			  (VestaSource::immutableDirectory, baseRep)); 
    }
    baseVS->longid = longid;
    baseVS->pseudoInode = pseudoInode;
    baseVS->ac = ac;
    baseVS->VestaSource::master = VestaSource::master;
    VestaSource::errorCode err;
    err = baseVS->lookup(arc, baseResult, NULL);
    if (err == VestaSource::ok) {
      result->longid = baseResult->longid;
      unsigned int baseIndex;
      (void) result->longid.getParent(&baseIndex);
      result->pseudoInode = indexToPseudoInode(baseIndex);
      delete baseResult;
    }
    delete baseVS;
    return err;
  }
  return VestaSource::ok;
}


VestaSource::errorCode
VDirChangeable::lookupIndex(unsigned int index, VestaSource*& result,
			    char* arcbuf) throw ()
{
  // Start by initializing the result, in case we fail before setting
  // it to something useful.
  result = 0;

  Bit8* entry;
  Bit8* repBlock; // storage for unused return value
  VestaSource* replaced = NULL;
  unsigned int indexOffset = 0;
  if (VestaSource::type == VestaSource::immutableDirectory) {
    // An immutableDirectory has only even indices
    if (index & 1) {
      // Odd: invalid index
      entry = NULL;
    } else {
      // Even:
      VDirChangeable cur = *this; // so we can change cur.rep
      for (;;) {
	entry = cur.findRawIndex((index - indexOffset) >> 1,
				 repBlock);
	if (entry != NULL) break;

	Bit8* baseRep = cur.base();
	if (baseRep == NULL) {
	  return VestaSource::notFound;
	}

	indexOffset += 2*(cur.nextRawIndex() - 1);

	// This is essentially an optimized tail recursion, to
	// avoid stack overflow when the base chain is long.
	cur.rep = baseRep;
	cur.repEndCache = NULL;
      }
    }
  } else {
    if (index & 1) {
      // Odd index; look in the rep
      entry = findRawIndex((index + 1) >> 1, repBlock);
    } else {
      // Even index; look in the base
      Bit8* baseRep = base();
      if (baseRep == NULL) {
	return VestaSource::notFound;
      }
      VestaSource* baseVS;
      if (VestaSource::type == VestaSource::volatileDirectory) {
	baseVS =
	  NEW_CONSTR(VDirEvaluator,
		     (VestaSource::evaluatorDirectory, baseRep));
      } else if (VestaSource::type == VestaSource::volatileROEDirectory) {
	baseVS =
	  NEW_CONSTR(VDirEvaluator,
		     (VestaSource::evaluatorROEDirectory, baseRep));
      } else {
	baseVS =
	  NEW_CONSTR(VDirChangeable,
		     (VestaSource::immutableDirectory, baseRep));
      }
      baseVS->longid = longid;
      baseVS->pseudoInode = pseudoInode;
      baseVS->ac = ac;
      baseVS->VestaSource::master = VestaSource::master;
      char myarcbuf[MAX_ARC_LEN+1];
      VestaSource::errorCode err =
	baseVS->lookupIndex(index, result, myarcbuf);
      delete baseVS;
      if (err != VestaSource::ok) return err;

      // Correct the pseudoInode if needed
      if ((result->type == VestaSource::immutableFile ||
	   result->type == VestaSource::mutableFile) &&
	  !RootLongId.isAncestorOf(longid) &&
	  VestaSource::type != VestaSource::volatileROEDirectory) {
	result->pseudoInode = indexToPseudoInode(index);
      }

      // Is the entry found in the base shadowed by a sameAsBase entry
      // in the current rep?
      unsigned int dummyRawIndex;
      entry = findArc(myarcbuf, dummyRawIndex, true, true);
      if (entry != NULL && sameAsBase(entry)) {
	// Yes, need to use the value from the new entry together
	// with the longid and pseudoInode for the replaced entry.
	// The case where the replacement is a forwarding pointer
	// must also be handled (below).
	replaced = result;
      } else {
	// No, result from base is correct.
	if (arcbuf != NULL) strcpy(arcbuf, myarcbuf);
	return VestaSource::ok;
      }
    }
  }
  if (entry == NULL) {
    result = NULL;
    assert(replaced == NULL);
    return VestaSource::notFound;
  }
  if ((type(entry) == VestaSource::deleted ||
       type(entry) == VestaSource::outdated) && value(entry) != 0) {
    // Use old longid, checking for overflow
    LongId result_longid = longid.append(index);
    if(result_longid == NullLongId)
      {
	return VestaSource::longIdOverflow;
      }
    // Follow forwarding pointer
    VForward* vf = (VForward*) VMemPool::lengthenPointer(value(entry));
    result = vf->longid()->lookup();
    if (replaced != NULL) delete replaced;
    if (result == NULL) return VestaSource::notFound;
    result->longid = result_longid;
    // Correct the pseudoInode if needed
    if ((result->type == VestaSource::immutableFile ||
	 result->type == VestaSource::mutableFile) &&
	!RootLongId.isAncestorOf(longid) &&
	VestaSource::type != VestaSource::volatileROEDirectory) {
      result->pseudoInode = indexToPseudoInode(index);
    }
    return VestaSource::ok;
  }
  result = VDCLookupResult(this, entry, index);
  if (result == NULL) {
    if (replaced != NULL) delete replaced;
    return VestaSource::notFound;
  }
  // Check for LongId overflow
  if (result->longid ==  NullLongId)
    {
      delete result;
      result = 0;
      return VestaSource::longIdOverflow;
    }
  if (arcbuf != NULL) {
    memcpy(arcbuf, arc(entry), arcLen(entry));
    arcbuf[arcLen(entry)] = '\000';
  }
  if (replaced != NULL) {
    result->longid = replaced->longid;
    result->pseudoInode = replaced->pseudoInode;
    delete replaced;
  }
  return VestaSource::ok;
}

VestaSource::errorCode
VDirChangeable::setTimestamp(time_t timestamp, AccessControl::Identity who)
  throw ()
{
    if (VestaSource::type != VestaSource::mutableDirectory &&
	VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory &&
	VestaSource::type != VestaSource::appendableDirectory) {
	return VestaSource::inappropriateOp;
    }

    // Access check
    if (!ac.check(who, AccessControl::write) &&
	!ac.check(who, AccessControl::ownership))
      return VestaSource::noPermission;

    // Log the operation
    if (doLogging && VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory) {
	char logrec[512];
	OBufStream ost(logrec, sizeof(logrec));
	// ('time' longid timestamp)
	ost << "(time " << longid << " " << timestamp << ")\n";
	VRLog.start();
	VRLog.put(ost.str());
	VRLog.commit();
    }

    setTimestampField(timestamp);
    if (VestaSource::type == VestaSource::mutableDirectory) setSnapshot(0);
    return VestaSource::ok;
}

static void
unlinkSid(ShortId sid)
{
  FdCache::flush(sid, FdCache::any);
  char* name = SourceOrDerived::shortIdToName(sid);
  Repos::dprintf(DBG_SIDDISP, "unlinking: 0x%08x\n", sid);
  if (!Repos::isDebugLevel(DBG_SIDNODEL)) {
    int ret = unlink(name);
    int saved_errno = errno;
    if (ret < 0 && saved_errno != ENOENT) {
      Text err_txt = Basics::errno_Text(saved_errno);
      Repos::dprintf(DBG_ALWAYS, "unlink %s: %s (errno = %d)\n", name,
		     err_txt.cchars(), saved_errno);
    }
  }
  delete[] name;
}

VestaSource::errorCode
VDirChangeable::reallyDelete(Arc arc, AccessControl::Identity who,
			     bool existCheck, time_t timestamp) throw ()
{
  // If the current binding of arc is recorded in the mutable part
  // of its directory, reallyDelete modifies the directory entry to
  // have type tag VestaSource::deleted, with null forwarding
  // pointer.  If the current binding is recorded in the base
  // directory, a new entry of type deleted is created with
  // the sameAsBase flag set.  A deleted entry is not a ghost; it is
  // strictly an artifact of the representation, invisible at higher
  // layers.  
  //
  // If the current binding was to a mutable file and the directory is
  // mutable or volatile, decrement the reference count for this
  // mutable shortid.  If the reference count reaches zero, unlink the
  // underlying sid file.  This supports multiple hard links to the
  // same mutable file, which are permitted in mutable and volatile
  // directories.
  //
  // If VRLogVersion is high enough, we always look in the base
  // (even if existCheck is off), and if this arc does not exist
  // there, we set the entry in the mutable rep to outdated (or
  // don't create it).  This is a trifle slower than the old way but
  // saves memory.  It also removes the oddity that list() in
  // deltaOnly mode can return deleted entries for things that
  // existed transiently but were not in the base.  We need to
  // preserve the old behavior if VRLogVersion says that we are
  // replaying a log from before the change, since otherwise
  // replaying an old log would sometimes create fewer entries in a
  // directory (either because we did not create an entry here, or
  // because we marked an entry as outdated here causing
  // copyMutableToImmutable not to copy it), so objects later in
  // that directory would have different LongIds than before.
  //
  if (VestaSource::type != VestaSource::mutableDirectory &&
      VestaSource::type != VestaSource::volatileDirectory &&
      VestaSource::type != VestaSource::volatileROEDirectory &&
      VestaSource::type != VestaSource::appendableDirectory) {
    return VestaSource::inappropriateOp;
  }

  // Access check on parent directory
  if (VestaSource::master &&
      VestaSource::type == VestaSource::appendableDirectory) {
    // Violates the agreement invariant if any other repository
    // has a replica of this name (even as a ghost or stub).
    if (!ac.check(who, AccessControl::agreement))
      return VestaSource::nameInUse;
  } else {
    if (!ac.check(who, AccessControl::write))
      return VestaSource::noPermission;
  }

  // Look for the arc in the rep
  unsigned int rawIndex;
  Bit8* entry = findArc(arc, rawIndex, true);

  // Look for the arc in the base
  VestaSource::errorCode inbase = VestaSource::notFound;
  Bit8* baseRep = base();
  if (baseRep != NULL) {
    VestaSource* baseVS;
    if (VestaSource::type == VestaSource::volatileDirectory) {
      baseVS = NEW_CONSTR(VDirEvaluator,
			  (VestaSource::evaluatorDirectory, baseRep));
    } else if (VestaSource::type==VestaSource::volatileROEDirectory) {
      baseVS = NEW_CONSTR(VDirEvaluator,
			  (VestaSource::evaluatorROEDirectory, baseRep));
    } else {
      baseVS = NEW_CONSTR(VDirChangeable,
			  (VestaSource::immutableDirectory, baseRep));
    }
    baseVS->longid = longid;
    baseVS->ac = ac;
    baseVS->VestaSource::master = VestaSource::master;
    VestaSource* vs;
    inbase = baseVS->lookup(arc, vs, NULL);
    delete baseVS;
    if (inbase == VestaSource::ok) {
      delete vs;
    }
  }
    
  if (entry == NULL) {
    if (existCheck) {
      if (inbase != VestaSource::ok) return inbase;
    } else {
      if (VRLogVersion >= 2 && inbase == VestaSource::notFound) {
	// Avoid making a needless "deleted" entry
	return VestaSource::ok;
      }
    }
  } else {
    if (type(entry) == VestaSource::deleted) {
      if (existCheck)
	return VestaSource::notFound;
      else
	return VestaSource::ok;
    }
  }
  if (timestamp == 0) timestamp = time(0);

  // Log the deletion
  if (doLogging && VestaSource::type != VestaSource::volatileDirectory &&
      VestaSource::type != VestaSource::volatileROEDirectory) {
    char logrec[512];
    OBufStream ost(logrec, sizeof(logrec));
    // ('del' longid arc timestamp)
    ost << "(del " << longid << " ";
    PutQuotedString(ost, arc);
    ost << " " << timestamp << ")\n";
    VRLog.start();
    VRLog.put(ost.str());
    VRLog.commit();
  }
    
  VestaSource::typeTag dtype = VestaSource::deleted;
  if (VRLogVersion >= 2 && inbase == VestaSource::notFound) {
    // If the entry is not shadowing anything, set it to outdated
    // right away.  This allows it to be compressed as part of a gap
    // when we next take a checkpoint.
    dtype = VestaSource::outdated;
  }
  if (entry == NULL) {
    appendEntry(true, true, dtype, 0, 0, NULL, arc, strlen(arc));
  } else {
    VestaSource::typeTag etype = type(entry);
    if (etype == VestaSource::mutableFile &&
	VestaSource::type == VestaSource::mutableDirectory) {
      // mutable_sidref_built implies that we're past recovery and the
      // mutable tree should have a shortid reference counter.  (See
      // VestaSource::recoveryDone and
      // VDirChangeable::buildMutableSidref.)
      assert(!mutable_sidref_built || (sidref != 0));

      // Decrement reference count on shortid and if it goes to 0...
      if((sidref != 0) &&
	 (sidref->Decrement(value(entry), false) == 0))
	{
	 // Unlink the unused mutable shortid.  We must not do this
	 // unlink unless we're out of recoovery and until after the
	 // log entry is committed.  If we're still inside a
	 // transaction, the log entry is not committed yet; otherwise
	 // it is.
	  if(doLogging && (VRLog.nesting() == 0))
	    unlinkSid((ShortId) value(entry));
	}
    } else if (etype == VestaSource::mutableFile &&
	       (VestaSource::type == VestaSource::volatileDirectory ||
		VestaSource::type == VestaSource::volatileROEDirectory)) {
      // Decrement reference count on shortid and unlink if it goes to 0
      if(sidref->Decrement(value(entry), false) == 0)
	{
	  unlinkSid(value(entry));
	}
    } else if (etype == VestaSource::volatileDirectory ||
	       etype == VestaSource::volatileROEDirectory ||
	       etype == VestaSource::mutableDirectory) {
      // Recursively free the memory used by this volatile/mutable
      // directory, decrement the reference counts of any mutable
      // shortids, and unlink any which reach 0.  Note that this
      // assumes that this is the only reference to this directory,
      // which should be the case for mutable and volatile
      // directories.
      VDirChangeable vdc(etype,
			 (Bit8*)VMemPool::lengthenPointer(value(entry)));
      vdc.sidref = this->sidref;
      vdc.freeTree();
    }
    setEntry(entry, true, sameAsBase(entry), dtype, 0, 0, NULL);
  }
  if (timestamp > this->timestamp()) {
    setTimestampField(timestamp);
  }
  if (VestaSource::type == VestaSource::mutableDirectory) setSnapshot(0);

  return VestaSource::ok;
}


// Returns != ok if there is a problem.  May modify the data
// structure, so do not call if any error checking remains to be done.
VestaSource::errorCode
VDirChangeable::insertCommon(Arc arc, bool mast,
			     VestaSource::typeTag newtype,
			     AccessControl::Identity who, 
			     VestaSource::dupeCheck chk,
			     const char** setOwner, ShortId* delsid,
			     Bit32* attribs) throw ()
{
    unsigned int rawIndex;
    VestaSource::errorCode err;

    *delsid = NullShortId;
    if (arc[0] == '\0') return VestaSource::notFound; // matches Unix

    // Require write access to the directory being inserted into.
    // Do we really want to require this for replacing stubs, though?
    if (!ac.check(who, AccessControl::write))
      return VestaSource::noPermission;

    // Lookup arc.
    // Note: if the directory is appendable, entry != NULL iff there
    // is an existing entry for arc.  Otherwise there could also be a
    // base to look in.
    Bit8* entry = findArc(arc, rawIndex, true);

    // Require agreement access for changes that could break the
    // agreement invariant unless checked against another repository.
    if (VestaSource::type == appendableDirectory) {
      if (entry) {
	// Replacing an entry in an appendable directory is safe only
	// in the following cases:
	// - Old entry is a master stub and new is master anything
	// - Old entry is a master anything and new is a master ghost
	// - Old entry is a nonmaster anything and new is a nonmaster
	//   ghost or nonmaster stub
	if (masterFlag(entry) != mast) {
	  if (!ac.check(who, AccessControl::agreement))
	    return VestaSource::notMaster;
	}
	if (!((mast && type(entry) == stub) ||
	      (mast && newtype == ghost) ||
	      (!mast && newtype == stub) ||
	      (!mast && newtype == ghost))) {
	  if (!ac.check(who, AccessControl::agreement))
	    return VestaSource::nameInUse;
	}
	// If an existing entry is being replaced with a ghost, the
	// user must have delete permission.  (This ensures that the
	// policy set with [Repository]restrict_delete is applied.)
	if(newtype == ghost)
	  {
	    if (!ac.check(who, AccessControl::del))
	      return VestaSource::noPermission;
	  }
	// Provide the attributes of the object being replaced to the
	// caller if it is a stub or if the type of the new object is
	// stub or ghost
	if(attribs &&
	   (type(entry) == stub || newtype == stub || newtype == ghost)) {
	    *attribs = attrib(entry);
	}
      } else {
	// Inserting a new entry into an appendable directory is safe
	// only if the directory and new entry are both master.
	if (!(this->VestaSource::master && mast)) {
	  if (!ac.check(who, AccessControl::agreement))
	    return VestaSource::notMaster;
	}
      }
    }

    // Handle dupeCheck actions, and if necessary
    // delete/outdate existing entry.
    switch (chk) {
      case VestaSource::replaceDiff:
      case VestaSource::replaceNonMaster:
	// If an entry was found, need to delete and outdate it
	if (entry != NULL) {
	    if (chk == VestaSource::replaceNonMaster && masterFlag(entry)) {
	      return VestaSource::nameInUse;
	    }
	    // Check if old sid can be deleted
	    if (type(entry) == VestaSource::mutableFile) {
	      if (VestaSource::type == mutableDirectory) {
		// mutable_sidref_built implies that we're past
		// recovery and the mutable tree should have a shortid
		// reference counter.  (See VestaSource::recoveryDone
		// and VDirChangeable::buildMutableSidref.)
		assert(!mutable_sidref_built || (sidref != 0));

		// Decrement reference count on shortid, and if it
		// goes to 0...
		if((sidref != 0) &&
		   (sidref->Decrement(value(entry), false) == 0))
		  {
		    if(doLogging && VRLog.nesting() == 0)
		      // Delete after transaction commits
		      *delsid = value(entry);
		  }

	      } else if (VestaSource::type == volatileDirectory ||
			 VestaSource::type == volatileROEDirectory) {
		assert(sidref != 0);

		// Decrement reference count on shortid and unlink
		// if it goes to 0
		if(sidref->Decrement(value(entry), false) == 0)
		  {
		    unlinkSid(value(entry));
		  }
	      }
	    }
	    // Mark entry outdated
	    if (type(entry) != VestaSource::deleted) {
	      setValue(entry, 0);  // if deleted, this is a VForward
	      setAttrib(entry, 0); // if deleted, this is already 0
	    }
	    setType(entry, VestaSource::outdated);
	}
	break;
	
      case VestaSource::dontReplace:
	// Error if arc is bound
	if (entry == NULL) {
	    // Must check if arc is bound in base
	    Bit8* baseRep = base();
	    if (baseRep != NULL) {
		VestaSource* baseVS;
		if (VestaSource::type == VestaSource::volatileDirectory) {
		    baseVS =
		      NEW_CONSTR(VDirEvaluator,
				 (VestaSource::evaluatorDirectory, baseRep));
		} else if (VestaSource::type ==
			   VestaSource::volatileROEDirectory) {
		    baseVS =
		      NEW_CONSTR(VDirEvaluator,
				 (VestaSource::evaluatorROEDirectory,
				  baseRep));
		} else {
		    baseVS =
		      NEW_CONSTR(VDirChangeable,
				 (VestaSource::immutableDirectory,
				  baseRep));
		}
		baseVS->longid = longid;
		baseVS->ac = ac;
		baseVS->VestaSource::master = VestaSource::master;
		VestaSource* vs;
		err = baseVS->lookup(arc, vs, NULL);
		delete baseVS;
		if (err == VestaSource::ok) {
		    delete vs;		    
		    return VestaSource::nameInUse;
		} else if (err != VestaSource::notFound) {
		    return err;
		}
	    }
	} else {
	    if (type(entry) == VestaSource::deleted) {
		// Need to outdate the deleted entry
		setType(entry, VestaSource::outdated);
	    } else {
		assert(type(entry) != VestaSource::outdated);
		return VestaSource::nameInUse;
	    }
	}
	break;
    }

    // See if owner needs to be set.  !!Current semantics might not be
    // the best; each choice point is marked with "!?": If user(0)
    // (other users are not checked!?) is a co-owner of the parent,
    // let the same ownership be inherited by the new child (including
    // other co-owners!?).  Otherwise, make user(0) the sole (!?)
    // owner of the new child.
    if (setOwner) {
      if (mast && who && !ac.owner.inAttribs("#owner", who->user(0))) {
	*setOwner = who->user(0);
      } else {
	*setOwner = NULL;
      }
    }
    if (VestaSource::type == VestaSource::mutableDirectory) {
	setSnapshot(0);
    }
    return VestaSource::ok;
}

VestaSource::errorCode
VDirChangeable::insertFile(Arc arc, ShortId sid, bool mast,
			   AccessControl::Identity who,
			   VestaSource::dupeCheck chk,
			   VestaSource** newvsp, time_t timestamp,
			   FP::Tag* forceFPTag) throw ()
{
    if (newvsp) *newvsp = NULL; // in case of error
    if (VestaSource::type != VestaSource::appendableDirectory &&
	VestaSource::type != VestaSource::mutableDirectory &&
	VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory) {
	return VestaSource::inappropriateOp;
    }
    int arcLen = strlen(arc);
    if (arcLen > MAX_ARC_LEN) return VestaSource::nameTooLong;
    if (sid == NullShortId) return VestaSource::invalidArgs;

    VestaSource::errorCode err;
    const char* setOwner;
    ShortId delsid;
    Bit32 attribs = 0;

    // Check for LongId overflow.  (We can skip this if our type is
    // volatileROEDirectory, as then the inserted object will get a
    // ShortId derived LongId.)
    LongId new_longid;
    unsigned int index = 2*nextRawIndex() - 1;
    if(VestaSource::type != VestaSource::volatileROEDirectory) {
      new_longid = longid.append(index);
      if(new_longid == NullLongId)
	{
	  return VestaSource::longIdOverflow;
	}
    }

    err = insertCommon(arc, mast, immutableFile, who, chk,
		       &setOwner, &delsid, &attribs);
    if (err != VestaSource::ok) return err;
    if (timestamp == 0) timestamp = time(0);
    
    FP::Tag childFPTag;
    if (forceFPTag != NULL) {
	childFPTag = *forceFPTag;
    } else if (VestaSource::type == VestaSource::appendableDirectory) {
	childFPTag = VestaSource::fptag;
	childFPTag.Extend("/");
	childFPTag.Extend(arc);
    } else if (VestaSource::type == VestaSource::volatileDirectory ||
	       VestaSource::type == VestaSource::volatileROEDirectory) {
	childFPTag = uidFPTag;
	childFPTag.Extend(UniqueId()); // nonreplayable OK because not logged
    } else {
	childFPTag = nullFPTag;
    }
    Bit8* entry = appendEntry(mast, false, VestaSource::immutableFile,
			      (Bit32) sid, attribs, &childFPTag, arc, arcLen);
    if (VestaSource::type == VestaSource::appendableDirectory) {
	SetFPFileShortId(entry);
    }
    if (timestamp > this->timestamp()) {
	setTimestampField(timestamp);
    } else {
	setTimestampField(this->timestamp() + 1);
    }
    VestaSource* newvs = NULL;
    if (newvsp || setOwner) {
	newvs = NEW_CONSTR(VLeaf, (VestaSource::immutableFile, sid));
	if (VestaSource::type == VestaSource::volatileROEDirectory) {
	    newvs->longid = LongId::fromShortId(sid, &childFPTag);
	    newvs->pseudoInode = sid;
	} else if (RootLongId.isAncestorOf(longid)) {
	    newvs->longid = new_longid; // LongId computed during error checks
	    newvs->pseudoInode = sid;
	} else {
	    newvs->longid = new_longid; // LongId computed during error checks
	    newvs->pseudoInode = indexToPseudoInode(index);
	}
	newvs->master = mast;
	newvs->attribs = (Bit8*) attribAddr(entry);
	newvs->ac = this->ac;
	newvs->fptag = childFPTag;
    }

    // Log the operation
    if (doLogging && VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory) {
	char logrec[512];
	OBufStream ost(logrec, sizeof(logrec));
	// ('insf' longid arc sid master timestamp) or
	// ('insf' longid arc sid master timestamp fptag)
	ost << "(insf " << longid << " ";
	PutQuotedString(ost, arc);
	ost << " 0x" << hex << sid << dec << " " 
	  << (int) mast << " " << timestamp;
	if (forceFPTag) {
	    ost << " ";
	    PutFPTag(ost, *forceFPTag);
	}
	ost << ")\n";
	VRLog.start();
	VRLog.put(ost.str());
	if (setOwner) {
	  // generates another log record, handled separately by recovery
	  newvs->setAttrib("#owner", setOwner, NULL);
	  newvs->ac.owner = *newvs;
	}
	VRLog.commit();
    }

    if (delsid != NullShortId) unlinkSid(delsid);
    if (newvsp) {
	*newvsp = newvs;
    } else {
	if (newvs) delete newvs;
    }
    return VestaSource::ok;
}


VestaSource::errorCode
VDirChangeable::insertMutableFile(Arc arc, ShortId sid, bool mast,
				  AccessControl::Identity who,
				  VestaSource::dupeCheck chk,
				  VestaSource** newvsp, time_t timestamp)
  throw ()
{
    if (newvsp) *newvsp = NULL; // in case of error
    if (VestaSource::type != VestaSource::mutableDirectory &&
	VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory) {
	return VestaSource::inappropriateOp;
    }
    int arcLen = strlen(arc);
    if (arcLen > MAX_ARC_LEN) return VestaSource::nameTooLong;
    VestaSource::errorCode err;
    const char* setOwner;
    ShortId delsid;

    // Check for LongId overflow.  (We can skip this if our type is
    // volatileROEDirectory, as then the inserted object will get a
    // ShortId derived LongId.)
    LongId new_longid;
    unsigned int index = 2*nextRawIndex() - 1;
    if(VestaSource::type != VestaSource::volatileROEDirectory) {
      new_longid = longid.append(index);
      if(new_longid == NullLongId)
	{
	  return VestaSource::longIdOverflow;
	}
    }

    err = insertCommon(arc, mast, mutableFile, who, chk,
		       &setOwner, &delsid);
    if (err != VestaSource::ok) return err;
    if (timestamp == 0) timestamp = time(0);
    if (sid == NullShortId) {
	// Assign a sid.  Note that this is done before logging the
        // arguments, so upon replay, the same sid will be used again.
	int fd = SourceOrDerived::fdcreate(sid);
	close(fd);
    }
    
    FP::Tag childFPTag;
    if (VestaSource::type == VestaSource::volatileDirectory ||
	VestaSource::type == VestaSource::volatileROEDirectory) {
	childFPTag = uidFPTag;
	childFPTag.Extend(UniqueId()); // nonreplayable OK because not logged
    } else {
	childFPTag = nullFPTag;
    }
    unsigned int link_count = 1;
    if(sidref != 0)
      {
        RECORD_TIME_POINT;
	link_count = sidref->Increment(sid);
        RECORD_TIME_POINT;
      }
    Bit8* entry = appendEntry(mast, false, VestaSource::mutableFile,
			      (Bit32) sid, 0, &childFPTag, arc, arcLen);

    if (timestamp > this->timestamp()) {
	setTimestampField(timestamp);
    } else {
	setTimestampField(this->timestamp() + 1);
    }
    VestaSource* newvs = NULL;
    if (newvsp || setOwner) {
	newvs = NEW_CONSTR(VLeaf, (VestaSource::mutableFile, sid, link_count));
	if (VestaSource::type == VestaSource::volatileROEDirectory) {
	    newvs->longid = LongId::fromShortId(sid, &childFPTag);
	    newvs->pseudoInode = sid;
	} else {
	    newvs->longid = new_longid; // LongId computed during error checks
	    newvs->pseudoInode = indexToPseudoInode(index);
	}
	newvs->master = mast;
	newvs->attribs = (Bit8*) attribAddr(entry);
	newvs->ac = this->ac;
	newvs->fptag = childFPTag;
    }

    // Log the operation
    if (doLogging && VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory) {
	char logrec[512];
	OBufStream ost(logrec, sizeof(logrec));
	// ('insu' longid arc sid master timestamp)
	ost << "(insu " << longid << " ";
	PutQuotedString(ost, arc);
	ost << " 0x" << hex << sid << dec << " "
	  << (int) mast << " " << timestamp << ")\n";
	VRLog.start();
	VRLog.put(ost.str());
	if (setOwner) {
	  // generates another log record, handled separately by recovery
	  newvs->setAttrib("#owner", setOwner, NULL);
	  newvs->ac.owner = *newvs;
	}
	VRLog.commit();
    }
    if (delsid != NullShortId) unlinkSid(delsid);
    if (newvsp) {
	*newvsp = newvs;
    } else {
	if (newvs) delete newvs;
    }
    return VestaSource::ok;
}


VestaSource::errorCode
VDirChangeable::insertImmutableDirectory(Arc arc, VestaSource* dir,
					 bool mast,
					 AccessControl::Identity who,
					 VestaSource::dupeCheck chk,
					 VestaSource** newvsp,
					 time_t timestamp, FP::Tag* forceFPTag)
  throw ()
{
    if (newvsp) *newvsp = NULL; // in case of error
    if ((VestaSource::type != VestaSource::appendableDirectory &&
	 VestaSource::type != VestaSource::mutableDirectory) ||
	(dir != NULL &&
	 dir->VestaSource::type != VestaSource::mutableDirectory &&
	 dir->VestaSource::type != VestaSource::immutableDirectory)) {
	// Wrong parent type, or wrong type for child prototype
	return VestaSource::inappropriateOp;
    }
    
    if (dir != NULL && !dir->ac.check(who, AccessControl::read))
      return VestaSource::noPermission;
    
    // Perform dupeCheck and find existing entry to modify if any.
    int arcLen = strlen(arc);
    if (arcLen > MAX_ARC_LEN) return VestaSource::nameTooLong;
    VestaSource::errorCode err;
    const char* setOwner;
    ShortId delsid;
    Bit32 attribs = 0;

    // Check for LongId overflow.
    unsigned int index = 2*nextRawIndex() - 1;
    LongId new_longid = longid.append(index);
    if(new_longid == NullLongId)
      {
	return VestaSource::longIdOverflow;
      }

    err = insertCommon(arc, mast, immutableDirectory, who, chk,
		       &setOwner, &delsid, &attribs);
    if (err != VestaSource::ok) return err;
    if (timestamp == 0) timestamp = time(0);
    
    VestaSource* newvs;
    if (dir && dir->VestaSource::type == VestaSource::immutableDirectory) {
	VDirChangeable* newvdc =
	  NEW_CONSTR(VDirChangeable,
		     (VestaSource::immutableDirectory, dir->rep));
	newvs = newvdc;
	newvs->fptag = newvdc->fptag();
	if (forceFPTag && *forceFPTag != newvs->fptag) {
	  delete newvdc;
	  return VestaSource::invalidArgs;
	}
    } else {
	FP::Tag childFPTag;
	if (forceFPTag != NULL) {
	    childFPTag = *forceFPTag;
	} else {
	    childFPTag = VestaSource::fptag;
	    childFPTag.Extend("/");
	    childFPTag.Extend(arc);
	}
	if (dir == NULL) {
	    // Make a new, empty immutable directory.
	    VDirChangeable* newvdc =
	      NEW_CONSTR(VDirChangeable,
			 (VestaSource::immutableDirectory, 0));
	    newvdc->setTimestampField(timestamp);
	    newvdc->setID(NewDirShortId(childFPTag,
				    VMemPool::shortenPointer(newvdc->rep)));
	    newvdc->setFPTag(childFPTag);
	    newvs = newvdc;
	    newvs->fptag = childFPTag;
	    SetFPDirShortId(newvdc->rep);
	} else /*(dir->VestaSource::type == VestaSource::mutableDirectory)*/ {
	    // Deep copy dir to a new immutable directory.
	    // Keep the same immutable bases, and don't make a new immutable
	    // directory at all if there are no changes to a base.
	    VDirChangeable* newvdc =
	      ((VDirChangeable*) dir)->copyMutableToImmutable(childFPTag);
	    newvs = newvdc;
	    newvs->fptag = newvdc->fptag();
	}
    }
    Bit8* entry = appendEntry(mast, false, newvs->type,
			      VMemPool::shortenPointer(newvs->rep),
			      attribs, NULL, arc, arcLen);
    if (timestamp > this->timestamp()) {
	setTimestampField(timestamp);
    } else {
	setTimestampField(this->timestamp() + 1);
    }
    newvs->longid = new_longid; // LongId computed during error checks
    newvs->master = mast;
    newvs->attribs = (Bit8*) attribAddr(entry);

    newvs->ac = this->ac;
    newvs->pseudoInode = indexToPseudoInode(index);
    
    // Log the operation
    if (doLogging && VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory) {
	char logrec[512];
	OBufStream ost(logrec, sizeof(logrec));
	// dirlongid = (dir == NULL) ? NullLongId : dir->longid
        // ('insi' longid arc dirlongid master timestamp) or
        // ('insi' longid arc dirlongid master timestamp fptag)
	ost << "(insi " << longid << " ";
	PutQuotedString(ost, arc);
	ost << " " << ((dir == NULL) ? NullLongId : dir->longid) << " "
	  << (int) mast << " " << timestamp;
	if (forceFPTag) {
	    ost << " ";
	    PutFPTag(ost, *forceFPTag);
	}
	ost << ")\n";
	VRLog.start();
	VRLog.put(ost.str());
	if (setOwner) {
	    // generates another log record, handled separately by recovery
	    newvs->setAttrib("#owner", setOwner, NULL);
	    newvs->ac.owner = *newvs;
	}
	VRLog.commit();
    }
    
    if (delsid != NullShortId) unlinkSid(delsid);
    if (newvsp) {
	*newvsp = newvs;
    } else {
	delete newvs;
    }
    return VestaSource::ok;
}


VestaSource::errorCode
VDirChangeable::insertAppendableDirectory(Arc arc, bool mast,
					  AccessControl::Identity who,
					  VestaSource::dupeCheck chk,
					  VestaSource** newvsp,
					  time_t timestamp) throw ()
{
    if (newvsp) *newvsp = NULL; // in case of error
    if (VestaSource::type != VestaSource::appendableDirectory) {
	return VestaSource::inappropriateOp;
    }
    int arcLen = strlen(arc);
    if (arcLen > MAX_ARC_LEN) return VestaSource::nameTooLong;
    VestaSource::errorCode err;
    const char* setOwner;
    ShortId delsid;
    Bit32 attribs = 0;

    // Check for LongId overflow.
    unsigned int index = 2*nextRawIndex() - 1;
    LongId new_longid = longid.append(index);
    if(new_longid == NullLongId)
      {
	return VestaSource::longIdOverflow;
      }

    err = insertCommon(arc, mast, appendableDirectory, who, chk,
		       &setOwner, &delsid, &attribs);
    if (err != VestaSource::ok) return err;
    if (timestamp == 0) timestamp = time(0);
    
    VDirChangeable* newvs =
      NEW_CONSTR(VDirChangeable,
		 (VestaSource::appendableDirectory, defaultRepSize));
    
    FP::Tag childFPTag = VestaSource::fptag;
    childFPTag.Extend("/");
    childFPTag.Extend(arc);

    Bit8* entry =
      appendEntry(mast, false, VestaSource::appendableDirectory,
		  VMemPool::shortenPointer(newvs->rep), attribs, NULL,
		  arc, arcLen);
    if (timestamp > this->timestamp()) {
	setTimestampField(timestamp);
    } else {
	setTimestampField(this->timestamp() + 1);
    }
    newvs->setTimestampField(timestamp);
    
    newvs->longid = new_longid; // LongId computed during error checks
    newvs->VestaSource::master = mast;
    newvs->attribs = (Bit8*) attribAddr(entry);
    newvs->ac = this->ac;
    newvs->pseudoInode = indexToPseudoInode(index);
    newvs->VestaSource::fptag = childFPTag;
    newvs->setFPTag(childFPTag);
    
    // Log the operation
    if (doLogging && VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory) {
	char logrec[512];
	OBufStream ost(logrec, sizeof(logrec));
	// ('insa' longid arc master timestamp)
	ost << "(insa " << longid << " ";
	PutQuotedString(ost, arc);
	ost << " " << (int) mast << " " << timestamp << ")\n";
	VRLog.start();
	VRLog.put(ost.str());
	if (setOwner) {
	    // generates another log record, handled separately by recovery
	    newvs->VestaSource::setAttrib("#owner", setOwner, NULL);
	    newvs->ac.owner = *newvs;
	}
	VRLog.commit();
    }
    
    assert(delsid == NullShortId);
    if (newvsp) {
	*newvsp = newvs;
    } else {
	delete newvs;
    }
    return VestaSource::ok;
}


VestaSource::errorCode
VDirChangeable::insertMutableDirectory(Arc arc, VestaSource* dir,
				       bool mast,
				       AccessControl::Identity who,
				       VestaSource::dupeCheck chk,
				       VestaSource** newvsp, time_t timestamp)
  throw ()
{
    if (newvsp) *newvsp = NULL; // in case of error
    // Parent or prototype child of wrong type?
    switch (VestaSource::type) {
      case VestaSource::mutableDirectory:
	if (dir != NULL &&
	    dir->VestaSource::type != VestaSource::immutableDirectory) {
	    return VestaSource::inappropriateOp;	    
	}
	break;
      case VestaSource::volatileDirectory:
	if (dir != NULL &&
	    dir->VestaSource::type != VestaSource::evaluatorDirectory) {
	    return VestaSource::inappropriateOp;
	}
	break;
      case VestaSource::volatileROEDirectory:
	if (dir != NULL &&
	    dir->VestaSource::type != VestaSource::evaluatorROEDirectory) {
	    return VestaSource::inappropriateOp;	    
	}
	break;
      default:
	return VestaSource::inappropriateOp;	    
    }
    if (dir != NULL && !dir->ac.check(who, AccessControl::read))
      return VestaSource::noPermission;
    
    int arcLen = strlen(arc);
    if (arcLen > MAX_ARC_LEN) return VestaSource::nameTooLong;
    VestaSource::errorCode err;
    const char* setOwner;
    ShortId delsid;

    // Check for LongId overflow.
    unsigned int index = 2*nextRawIndex() - 1;
    LongId new_longid = longid.append(index);
    if(new_longid == NullLongId)
      {
	return VestaSource::longIdOverflow;
      }

    err = insertCommon(arc, mast, mutableDirectory, who, chk,
		       &setOwner, &delsid);
    if (err != VestaSource::ok) return err;
    if (timestamp == 0) timestamp = time(0);
    
    VDirChangeable* newvs =
      NEW_CONSTR(VDirChangeable,
		 (VestaSource::type, defaultRepSize));
    if (dir == NULL) {
	newvs->setIsMoreOrBase(newvs->rep, isNeither);
	newvs->setTimestampField(timestamp);
	newvs->setSnapshot(0);
    } else {
	newvs->setIsMoreOrBase(newvs->rep, isBase); // base
	Bit32 baseSP = VMemPool::shortenPointer(dir->rep);
	newvs->setMoreOrBase(newvs->rep, baseSP);
	newvs->baseCache = dir->rep;
	newvs->setTimestampField(dir->timestamp());
	newvs->setSnapshot(baseSP);
    }
    newvs->VestaSource::fptag = nullFPTag;
    
    newvs->pseudoInode = indexToPseudoInode(index);
    newvs->setID(newvs->pseudoInode);
    Bit8* entry =
      appendEntry(mast, false,
		  newvs->VestaSource::type,
		  VMemPool::shortenPointer(newvs->rep), 0,
		  NULL, arc, arcLen);
    if (timestamp > this->timestamp()) {
	setTimestampField(timestamp);
    } else {
	setTimestampField(this->timestamp() + 1);
    }    
    newvs->longid = new_longid; // LongId computed during error checks
    newvs->VestaSource::master = mast;
    newvs->attribs = (Bit8*) attribAddr(entry);
    newvs->ac = this->ac;
    newvs->sidref = this->sidref;

    // Log the operation
    if (doLogging && VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory) {
	char logrec[512];
	OBufStream ost(logrec, sizeof(logrec));
	// dir==NULL: ('insm' longid arc NullLongId master timestamp)
	// otherwise: ('insm' longid arc dir->longid master timestamp)
	ost << "(insm " << longid << " ";
	PutQuotedString(ost, arc);
	ost << " " << ((dir == NULL) ? NullLongId : dir->longid);
	ost << " " << (int) mast << " " << timestamp << ")\n";
	VRLog.start();
	VRLog.put(ost.str());
	if (setOwner) {
	    // generates another log record, handled separately by recovery
	    newvs->VestaSource::setAttrib("#owner", setOwner, NULL);
	    newvs->ac.owner = *newvs;
	}
	VRLog.commit();
    }
    
    if (delsid != NullShortId) unlinkSid(delsid);
    if (newvsp) {
	*newvsp = newvs;
    } else {
	delete newvs;
    }
    return VestaSource::ok;
}

VestaSource::errorCode
VDirChangeable::insertGhost(Arc arc, bool mast,
			    AccessControl::Identity who,
			    VestaSource::dupeCheck chk, VestaSource** newvsp,
			    time_t timestamp) throw ()
{
    if (newvsp) *newvsp = NULL; // in case of error
    if (VestaSource::type != VestaSource::appendableDirectory) {
	return VestaSource::inappropriateOp;
    }
    int arcLen = strlen(arc);
    if (arcLen > MAX_ARC_LEN) return VestaSource::nameTooLong;
    VestaSource::errorCode err;
    const char* setOwner;
    ShortId delsid;
    Bit32 attribs = 0;

    // Check for LongId overflow.
    unsigned int index = 2*nextRawIndex() - 1;
    LongId new_longid = longid.append(index);
    if(new_longid == NullLongId)
      {
	return VestaSource::longIdOverflow;
      }

    err = insertCommon(arc, mast, ghost, who, chk, 
		       &setOwner, &delsid, &attribs);
    if (err != VestaSource::ok) return err;
    if (timestamp == 0) timestamp = time(0);
    
    Bit8* entry =
      appendEntry(mast, false, VestaSource::ghost, 0, attribs, NULL,
		  arc, arcLen);
    if (timestamp > this->timestamp()) {
	setTimestampField(timestamp);
    } else {
	setTimestampField(this->timestamp() + 1);
    }    
    VestaSource* newvs = NULL;
    if (newvsp || setOwner) {
	newvs = NEW_CONSTR(VLeaf,
			   (VestaSource::ghost, NullShortId));
	newvs->longid = new_longid; // LongId computed during error checks
	newvs->master = mast;
	newvs->attribs = (Bit8*) attribAddr(entry);
	newvs->ac = this->ac;
	newvs->pseudoInode = indexToPseudoInode(index);
	newvs->fptag = nullFPTag;
    }
    
    // Log the operation
    if (doLogging && VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory) {
	char logrec[512];
	OBufStream ost(logrec, sizeof(logrec));
	// ('insg' longid arc master timestamp)
	ost << "(insg " << longid << " ";
	PutQuotedString(ost, arc);
	ost << " " << (int) mast << " " << timestamp << ")\n";
	VRLog.start();
	VRLog.put(ost.str());
	if (setOwner) {
	    // generates another log record, handled separately by recovery
	    newvs->setAttrib("#owner", setOwner, NULL);
	    newvs->ac.owner = *newvs;
	}
	VRLog.commit();
    }
    
    assert(delsid == NullShortId);
    if (newvsp) {
	*newvsp = newvs;
    } else {
	if (newvs) delete newvs;
    }
    return VestaSource::ok;
}

VestaSource::errorCode
VDirChangeable::insertStub(Arc arc, bool mast, AccessControl::Identity who,
			   VestaSource::dupeCheck chk,
			   VestaSource** newvsp, time_t timestamp) throw ()
{
    if (newvsp) *newvsp = NULL; // in case of error
    if (VestaSource::type != VestaSource::appendableDirectory) {
	return VestaSource::inappropriateOp;
    }
    int arcLen = strlen(arc);
    if (arcLen > MAX_ARC_LEN) return VestaSource::nameTooLong;
    VestaSource::errorCode err;
    const char* setOwner;
    ShortId delsid;
    Bit32 attribs = 0;

    // Check for LongId overflow.
    unsigned int index = 2*nextRawIndex() - 1;
    LongId new_longid = longid.append(index);
    if(new_longid == NullLongId)
      {
	return VestaSource::longIdOverflow;
      }

    err = insertCommon(arc, mast, stub, who, chk, 
		       &setOwner, &delsid, &attribs);
    if (err != VestaSource::ok) return err;
    if (timestamp == 0) timestamp = time(0);
    
    Bit8* entry =
      appendEntry(mast, false, VestaSource::stub, 0, attribs, NULL,
		  arc, arcLen);
    if (timestamp > this->timestamp()) {
	setTimestampField(timestamp);
    } else {
	setTimestampField(this->timestamp() + 1);
    }
    VestaSource* newvs = NULL;
    if (newvsp || setOwner) {
	newvs = NEW_CONSTR(VLeaf, (VestaSource::stub, NullShortId));
	newvs->longid = new_longid; // LongId computed during error checks
	newvs->master = mast;
	newvs->attribs = (Bit8*) attribAddr(entry);
	newvs->ac = this->ac;
	newvs->pseudoInode = indexToPseudoInode(index);
	newvs->fptag = nullFPTag;
    }
    
    // Log the operation
    if (doLogging && VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory) {
	char logrec[512];
	OBufStream ost(logrec, sizeof(logrec));
	// ('inss' longid arc master timestamp)
	ost << "(inss " << longid << " ";
	PutQuotedString(ost, arc);
	ost << " " << (int) mast << " " << timestamp << ")\n";
	VRLog.start();
	VRLog.put(ost.str());
	if (setOwner) {
	    // generates another log record, handled separately by recovery
	    newvs->setAttrib("#owner", setOwner, NULL);
	    newvs->ac.owner = *newvs;
	}
	VRLog.commit();
    }
    
    assert(delsid == NullShortId);
    if (newvsp) {
	*newvsp = newvs;
    } else {
	if (newvs) delete newvs;
    }
    return VestaSource::ok;
}


bool
copyOwnerCallback(void* closure, const char* value)
{
  ((VestaSource*)closure)->addAttrib("#owner", value);
  return true;
}

VestaSource::errorCode
VDirChangeable::renameTo(Arc arc, VestaSource* fromDir, Arc fromArc,
			 AccessControl::Identity who,
			 VestaSource::dupeCheck chk, time_t timestamp) throw ()
{
    switch (VestaSource::type) {
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
	if (fromDir->type != VestaSource::type ||
	    fromDir->master != VestaSource::master) {
	    return VestaSource::inappropriateOp;
	}
	break;
      default:
	return VestaSource::inappropriateOp;
    }
    
    if (memcmp(&fromDir->longid.value, &this->longid.value,
	       sizeof(LongId)) == 0) {
	// fromDir and "this" are the same directory
	fromDir->resync(); // discard cache for our caller's benefit
	fromDir = this;
    }
    
    // Look up the existing object
    VestaSource::errorCode err;
    VestaSource* target;
    err = fromDir->lookup(fromArc, target, who);
    if (err != VestaSource::ok) return err;
    
    // Check that fromDir is writeable
    if (!fromDir->ac.check(who, AccessControl::write)) {
	delete target;
	return VestaSource::noPermission;
    }
    
    // Be sure we are not creating a loop.  We need consider only
    // loops of mutable directories and loops of appendable
    // directories: An immutable directory cannot contain an
    // appendable or mutable child, an appendable directory cannot
    // contain a mutable child, and a mutable directory cannot contain
    // an appendable child.  A mutable or appendable directory can
    // have only one parent, because there is no operation that gives
    // a second parent to an existing one.  So we can test for
    // ancestry simply by checking whether one directory's longid is a
    // prefix of the other.
    if (target->longid.isAncestorOf(longid)) {
	delete target;
	return VestaSource::invalidArgs;
    }

    // Information needed to compute the LongId of the newly inserted
    // object.
    unsigned int rawIndex;
    VDirChangeable* fromVDC = (VDirChangeable*) fromDir;
    Bit8* oldEntry = fromVDC->findArc(fromArc, rawIndex);
    unsigned int index = 2*nextRawIndex() - 1;
    if((fromDir == this) && (oldEntry == NULL)) {
      // In this case, we'll be inserting a forwarding pointer first,
      // so the index of the renamed object will actually be the next
      // one after this.
      index += 2;
    }

    // Predict longid to be used for the new entry and check for
    // LongId overflow.
    LongId new_longid = longid.append(index);
    if(new_longid == NullLongId)
      {
	return VestaSource::longIdOverflow;
      }

    // Find where the new entry goes and do more error checking
    int arcLen = strlen(arc);
    if (arcLen > MAX_ARC_LEN) return VestaSource::nameTooLong;
    ShortId delsid;
    err = insertCommon(arc, target->master, target->VestaSource::type,
		       who, chk, NULL, &delsid);
    if (err != VestaSource::ok) return err;
    if (timestamp == 0) timestamp = time(0);

    // Log the operation
    bool needOwner = false, needCommit = false;
    if (doLogging && VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory) {

        // Copy the owner(s) in case those that would be inherited
        // from the new parent are different.
        needOwner = (fromDir != this &&
		     target->ac.owner.attribs != target->attribs &&
		     this->ac.owner.attribs != target->ac.owner.attribs);
	assert(!needOwner || (target->ac.owner.attribs == fromDir->ac.owner.attribs));
	char logrec[1024];
	OBufStream ost(logrec, sizeof(logrec));
	// ('ren' longid arc fromLongid fromArc timestamp)
	ost << "(ren " << longid << " ";
	PutQuotedString(ost, arc);
	ost << " " << fromDir->longid << " ";
	PutQuotedString(ost, fromArc);
	ost << " " << timestamp << ")\n";
	VRLog.start();
	VRLog.put(ost.str());
	// We need to commit this log entry before returning.
	needCommit = true;
    }
    Bit32 oldAttrib = target->firstAttrib();
    
    // Delete from the old parent, possibly with forwarding pointer
    if (fromDir->type == VestaSource::appendableDirectory) {
	if (oldEntry != NULL) {
	    // assert(type(oldEntry) != VestaSource::deleted);
	    fromVDC->setEntry(oldEntry, true, sameAsBase(oldEntry),
			      VestaSource::outdated, 0, 0, NULL);
	}
	// Leave a ghost if fromDir was master, else leave nothing.
	// Maybe latter case should leave a stub as a matter of policy?
	if (fromDir->master) {
	    fromVDC->appendEntry(true, false, VestaSource::ghost, 0, 0,
				 NULL, fromArc, strlen(fromArc));
	}
    } else {
	if (oldEntry == NULL) {
	    fromVDC->appendEntry(true, true, VestaSource::deleted, 
				 VForward::create(new_longid),
				 0, NULL, fromArc, strlen(fromArc));
	} else {
	  VestaSource::typeTag dtype = VestaSource::deleted;
	  if (VRLogVersion >= 2) {
	      Bit8* baseRep = fromVDC->base();
	      if (baseRep == NULL) {
		  // If there is no base, the entry can't be shadowing
		  // anything, so set it to outdated right away.  This
		  // allows it to be compressed as part of a gap when
		  // we next take a checkpoint.
		  dtype = VestaSource::outdated;
	      } else if (VRLogVersion >= 3) {
		  // If there is a base, look in it to see if the
		  // entry is shadowing anything.  Tedious, but it
		  // helps us give clean semantics to a _run_tool that
		  // deletes things from its directory: only names
		  // that existed in the initial binding and were
		  // deleted/renamed are bound to FALSE in the result,
		  // never names that were created and then deleted.
		  // It also helps a little with compression, as in
		  // the simpler-to-check NULL case just above.
		  VestaSource* baseVS;
		  if (fromDir->type == VestaSource::volatileDirectory) {
		    baseVS =
		      NEW_CONSTR(VDirEvaluator,
				 (VestaSource::evaluatorDirectory, baseRep));
		  } else if (fromDir->type ==
			     VestaSource::volatileROEDirectory) {
		    baseVS =
		      NEW_CONSTR(VDirEvaluator,
				 (VestaSource::evaluatorROEDirectory, baseRep));
		  } else {
		    baseVS =
		      NEW_CONSTR(VDirChangeable,
				 (VestaSource::immutableDirectory, baseRep));
		  }
		  baseVS->longid = fromDir->longid;
		  baseVS->ac = fromDir->ac;
		  baseVS->master = fromDir->master;
		  VestaSource* tmpVS;
		  VestaSource::errorCode inbase =
		      baseVS->lookup(fromArc, tmpVS, NULL);
		  delete baseVS;
		  if (inbase == VestaSource::ok) {
		      delete tmpVS;
		  } else {
		      dtype = VestaSource::outdated;
		  }
	      }
	  }
	  fromVDC->setEntry(oldEntry, true, sameAsBase(oldEntry), dtype, 
			    VForward::create(new_longid),
			    0, NULL);
	}
	if (fromDir->type == VestaSource::mutableDirectory) {
	    fromVDC->setSnapshot(0);
	}
    }
    if (timestamp > fromVDC->timestamp()) {
	fromVDC->setTimestampField(timestamp);
    } else {
	fromVDC->setTimestampField(fromVDC->timestamp() + 1);
    }
    // copy-on-write target if needed
    if ((target->type == VestaSource::immutableDirectory &&
	 fromDir->type == VestaSource::mutableDirectory) ||
	(target->type == VestaSource::evaluatorDirectory &&
	 fromDir->type == VestaSource::volatileDirectory) ||
	(target->type == VestaSource::evaluatorROEDirectory &&
	  fromDir->type == VestaSource::volatileROEDirectory)) {
	VDirChangeable* newtarget =
	  NEW_CONSTR(VDirChangeable, (fromDir->type, defaultRepSize));
	newtarget->setIsMoreOrBase(newtarget->rep, isBase); // base
	newtarget->setMoreOrBase(newtarget->rep,
				 VMemPool::shortenPointer(target->rep));
	newtarget->setID(target->pseudoInode);
	newtarget->setTimestampField(target->timestamp());
	delete target;
	target = newtarget;
    }
    
    // Insert in the new parent
    Bit32 newval;
    switch (target->type) {
      case VestaSource::immutableFile:
      case VestaSource::mutableFile:
      case VestaSource::device:
	newval = target->shortId();
	break;
      case VestaSource::ghost:
      case VestaSource::stub:
	newval = 0;
	break;
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::volatileROEDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::evaluatorROEDirectory:
	newval = VMemPool::shortenPointer(target->rep);
	break;
      default:
	assert(false); // cannot happen
	break;
    }
    FP::Tag newFPTag;
    if (target->type == VestaSource::appendableDirectory) {
	newFPTag = VestaSource::fptag;
	newFPTag.Extend("/");
	newFPTag.Extend(arc);
    } else {
	newFPTag = target->fptag;
    }
    Bit8* newEntry =
      appendEntry(target->master, false, target->type, newval, oldAttrib,
		  (target->type == VestaSource::immutableFile ||
		   target->type == VestaSource::mutableFile) ?
		  &newFPTag : NULL, arc, arcLen);
    if (VestaSource::type == VestaSource::appendableDirectory &&
	target->type == VestaSource::immutableFile) {
	SetFPFileShortId(newEntry);
    }
    if (timestamp > this->timestamp()) {
	setTimestampField(timestamp);
    } else {
	setTimestampField(this->timestamp() + 1);
    }    
    if (delsid != NullShortId) unlinkSid(delsid);

    // If we need to commit the change to the log..
    if(needCommit)
      {
	// If the object needs to have an #owner attribute added, do
	// that now.  We do this after the rename, as we're sure the
	// object will have attributes now (i.e. not be an obejct that
	// exists only in the immutable base).  We do this before
	// committing the rename to the log, as this will generate
	// additional log records handled separately by recovery.
	if(needOwner)
	  {
	    VestaSource *new_target = VDCLookupResult(this, newEntry, index);
	    // This is only done in mutable and appendable
	    // directories, so this object must have attributes.
	    assert(new_target->hasAttribs());
	    fromDir->ac.owner.getAttrib("#owner", copyOwnerCallback,
					new_target);
	    delete new_target;
	  }
	// Commit the change (or changes) to the log.
	VRLog.commit();
      }

    delete target;
    return VestaSource::ok;
}


struct FilterListClosure {
    ArcTable* hidden;
    VestaSource::listCallback callback;
    void* closure;
};

bool
filterListCallback(void* closure, VestaSource::typeTag type,
		   Arc arc, unsigned int index, Bit32 pseudoInode,
		   ShortId filesid, bool mast) throw ()
{
    FilterListClosure* cl = (FilterListClosure*) closure;
    unsigned int dummyRawIndex;
    
    ArcKey k;
    ArcValue v;
    k.s = arc;
    k.slen = strlen(arc);
    if (cl->hidden->Get(k, v)) {
	// An entry in the main rep hides this one in the base; skip it
	return true;
    } else {
	// Invoke the original callback
	return (cl->callback)(cl->closure, type, arc, index, pseudoInode,
			      filesid, mast);
    }	
}

VestaSource::errorCode
VDirChangeable::list(unsigned int firstIndex,
		     VestaSource::listCallback callback, void* closure,
		     AccessControl::Identity who,
		     bool deltaOnly, unsigned int indexOffset) throw ()
{
  unsigned int index, baseIndexOffset;
  Bit8* entry;
  Bit8* repBlock;
  assert((indexOffset & 1) == 0);
    
  // Access check
  if (!ac.check(who, AccessControl::read))
    return VestaSource::noPermission;
    
  VDirChangeable cur = *this; // so that we can change cur.rep
  ArcTable hidden;
  // Iterate down the base chain (tail recursion elimination)
  for (;;) {
    // An immutableDirectory has only even indices; a mutableDirectory
    // has odd indices for its rep, even indices in its base chain.
    // The odd part of the index space precedes the even part.
    if (cur.VestaSource::type == VestaSource::immutableDirectory) {
      index = 2;
    } else {
      index = 1;
    }
    // Iterate through the rep blocks in this rep
    repBlock = cur.rep;
    for (;;) {
      // Iterate through the entries in this rep block
      entry = firstEntry(repBlock);
      while (!isEndMark(entry)) {
	bool skip = false;

	// Check if we need to skip this entry due to its
	// being deleted or outdated.
	switch (type(entry)) {
	default:
	  break;
	case VestaSource::deleted:
	  if (!deltaOnly) skip = true;
	  break;
	case VestaSource::outdated:
	case VestaSource::gap:
	  skip = true;
	  break;
	}

	// Eliminate duplicates found in the base chain
	ArcKey k;
	ArcValue v;
	k.s = arc(entry);
	k.slen = arcLen(entry);
	if (hidden.Get(k, v)) {
	  skip = true;
	} else if (type(entry) != VestaSource::outdated &&
		   type(entry) != VestaSource::gap) {
	  hidden.Put(k, ArcValue());
	}

	// Skip entries before firstIndex
	if (firstIndex != 0) {
	  if (index & 1) {
	    // In a mutable rep.  Skip if firstIndex is even, or
	    // odd but beyond where we are.
	    if ((firstIndex & 1) == 0 ||
		firstIndex > (index + indexOffset)) {
	      skip = true;
	    }
	  } else {
	    // In an immutable rep.  Skip if firstIndex is even
	    // and beyond where we are.
	    if ((firstIndex & 1) == 0 &&
		firstIndex > (index + indexOffset)) {
	      skip = true;
	    }
	  }
	}

	if (!skip) {
	  char arcbuf[512];
	  memcpy(arcbuf, arc(entry), arcLen(entry));
	  arcbuf[arcLen(entry)] = '\000';
		    
	  // Get the correct pseudo-inode and shortid
	  Bit32 pseudi;
	  ShortId filesid;
	  switch (type(entry)) {
	  case VestaSource::mutableDirectory:
	  case VestaSource::volatileDirectory:
	  case VestaSource::volatileROEDirectory:
	    {
	      filesid = NullShortId;
	      VDirChangeable childVS(type(entry), (Bit8*) VMemPool::
				     lengthenPointer(value(entry)));
	      pseudi = childVS.getID();
	    }
	    break;
	  case VestaSource::immutableFile:
	  case VestaSource::mutableFile:
	    filesid = (ShortId) value(entry); 
	    if (VestaSource::type == VestaSource::volatileROEDirectory ||
		RootLongId.isAncestorOf(longid)) {
	      pseudi = filesid;
	    } else {
	      pseudi = indexToPseudoInode(index + indexOffset);
	    }
	    break;
	  default:
	    filesid = NullShortId;
	    pseudi = indexToPseudoInode(index + indexOffset);
	    break;
	  }
	  if (sameAsBase(entry)) {
	    // Ugh, need to find pseudo-inode of entry in base
	    assert(cur.VestaSource::type != VestaSource::immutableDirectory);
	    assert(indexOffset == 0);
	    assert(cur.rep == rep); // i.e., at top of base chain
	    Bit8* baseRep = base();
	    VestaSource* baseVS;
	    VestaSource* baseResult = 0;
	    if (VestaSource::type == VestaSource::volatileDirectory) {
	      baseVS = NEW_CONSTR(VDirEvaluator,
				  (VestaSource::evaluatorDirectory,
				   baseRep));
	    } else if (VestaSource::type ==
		       VestaSource::volatileROEDirectory) {
	      baseVS = NEW_CONSTR(VDirEvaluator,
				  (VestaSource::evaluatorROEDirectory,
				   baseRep));
	    } else {
	      baseVS = NEW_CONSTR(VDirChangeable,
				  (VestaSource::immutableDirectory,
				   baseRep));
	    }
	    baseVS->longid = longid;
	    baseVS->pseudoInode = pseudoInode;
	    baseVS->ac = ac;
	    baseVS->VestaSource::master = VestaSource::master;
	    VestaSource::errorCode err;
	    err = baseVS->lookup(arcbuf, baseResult, NULL);
	    if (err != VestaSource::ok) {
              if(baseResult != 0)
		{
		  delete baseResult;
		}
              delete baseVS;
	      return err;
	    }
	    unsigned int baseIndex;
	    (void) baseResult->longid.getParent(&baseIndex);
	    pseudi = indexToPseudoInode(baseIndex);
	    delete baseResult;
	    delete baseVS;
	  }
	  // Call the client back with this entry
	  if (!callback(closure, type(entry), arcbuf, index + indexOffset,
			pseudi, filesid, masterFlag(entry))) {
	    // we are requested to stop
	    return VestaSource::ok;
	  }
	}
	if (type(entry) == VestaSource::gap) {
	  index += 2*value(entry);
	} else {
	  index += 2;
	}
	entry = nextEntry(entry);
      }
      // end of rep block loop

      if (isMoreOrBase(repBlock) == isMore) {
	// Loop back to continue with next rep block
	repBlock =
	  (Bit8*) VMemPool::lengthenPointer(moreOrBase(repBlock));
      } else {
	// Exit from rep loop
	break;
      }
    }
    // end of rep loop

    if (cur.VestaSource::type == VestaSource::immutableDirectory) {
      // Indices in rep are prepended to index space of base
      baseIndexOffset = indexOffset + ((cur.nextRawIndex() << 1) - 2);
    } else {	    
      assert(indexOffset == 0);
      baseIndexOffset = 0;
    }
    
    // List the part in the base, if any.  We have to filter out
    // names that are hidden by something in the current rep.  This
    // is taken care of by the "hidden" table.
    
    if (deltaOnly &&
	(VestaSource::type != VestaSource::immutableDirectory)) {
      return VestaSource::ok;
    }

    Bit8* baseRep = cur.base();
    if (baseRep == NULL) return VestaSource::ok;
    VestaSource* baseVS;
    if (VestaSource::type == VestaSource::volatileDirectory ||
	VestaSource::type == VestaSource::volatileROEDirectory) {

      // Recurse: base has different object type
      VestaSource* baseVS =
	NEW_CONSTR(VDirEvaluator,
		   (VestaSource::type == volatileDirectory ?
		    VestaSource::evaluatorDirectory :
		    VestaSource::evaluatorROEDirectory, baseRep));

      baseVS->longid = longid;
      baseVS->ac = ac;
      baseVS->pseudoInode = pseudoInode;
      baseVS->VestaSource::master = VestaSource::master;
      FilterListClosure cl;
      cl.hidden = &hidden;
      cl.callback = callback;
      cl.closure = closure;
      VestaSource::errorCode err =
	baseVS->list(((firstIndex & 1) ? 0 : firstIndex),
		     filterListCallback, &cl, NULL,
		     false, baseIndexOffset);
      delete baseVS;
      return err;

    } else {
      // Iterate: base has same object type.
      // This is essentially an optimized tail recursion, to
      // avoid stack overflow when the base chain is long.
      cur.rep = baseRep;
      cur.repEndCache = NULL;
      cur.VestaSource::type = VestaSource::immutableDirectory;
      indexOffset = baseIndexOffset;
      if (deltaOnly && cur.shortId() != NullShortId) {
	return VestaSource::ok;
      }
    }
  }

  //return VestaSource::ok;    // Not reached
}


VestaSource::errorCode 
VDirChangeable::getBase(VestaSource*& result, AccessControl::Identity who)
  throw ()
{
    VestaSource::errorCode ret = VestaSource::ok;
    
    // Access check
    if (!ac.check(who, AccessControl::read))	
      return VestaSource::noPermission;
    
    Bit8* baseRep = base();
    if (baseRep == NULL) return VestaSource::notFound;
    
    if (VestaSource::type == VestaSource::volatileDirectory) {
	result = NEW_CONSTR(VDirEvaluator,
			    (VestaSource::evaluatorDirectory, baseRep));
    } else if (VestaSource::type == VestaSource::volatileROEDirectory) {
	result = NEW_CONSTR(VDirEvaluator,
			    (VestaSource::evaluatorROEDirectory,baseRep));
    } else {
	VDirChangeable *vdc =
	  NEW_CONSTR(VDirChangeable,
		     (VestaSource::immutableDirectory, baseRep));
	result = vdc;
	while (result->shortId() == NullShortId) {
	    baseRep = vdc->base();
	    if (baseRep == NULL) {
		delete vdc;
		result = NULL;
		return VestaSource::notFound;
	    }
	    vdc->rep = baseRep;
	    vdc->repEndCache = NULL;
	}
	FP::Tag newtag;
	memcpy(newtag.Words(), &baseRep[VDIRCH_FPTAG], FP::ByteCnt);
	result->VestaSource::fptag = newtag;
	result->longid = LongId::fromShortId(result->shortId(), NULL);
	result->pseudoInode = result->shortId();
    }
    return ret;
}

VestaSource::errorCode
VDirChangeable::makeIndexMutable(unsigned int index,
				 VestaSource*& result, ShortId sid,
				 Basics::uint64 copyMax,
				 AccessControl::Identity who) throw ()
{
    if (VestaSource::type != VestaSource::mutableDirectory &&
	VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory) {
	return VestaSource::inappropriateOp;
    }
    
    if (!ac.check(who, AccessControl::write))
      return VestaSource::noPermission;

    Bit8* entry;
    Bit8* repBlock;
    VestaSource::typeTag oldType, newType;
    Bit32 oldValue;
    bool oldMaster;
    char arc[MAX_ARC_LEN+1];
    
    if (index & 1) {
	// Odd index; look in the rep
	entry = findRawIndex((index + 1) >> 1, repBlock);
	if (entry == NULL) return VestaSource::notFound;
	oldType = type(entry);
	oldValue = value(entry);
	oldMaster = masterFlag(entry);
    } else {
	// Even index; look in the base first
	Bit8* baseRep = base();
	if (baseRep == NULL) return VestaSource::notFound;
	
	VestaSource* baseVS;
	if (VestaSource::type == VestaSource::volatileDirectory) {
	    baseVS =
	      NEW_CONSTR(VDirEvaluator,
			 (VestaSource::evaluatorDirectory, baseRep));
	} else if (VestaSource::type == VestaSource::volatileROEDirectory) {
	    baseVS =
	      NEW_CONSTR(VDirEvaluator,
			 (VestaSource::evaluatorROEDirectory, baseRep));
	} else {
	    baseVS =
	      NEW_CONSTR(VDirChangeable,
			 (VestaSource::immutableDirectory, baseRep));
	}
	baseVS->longid = longid;
	baseVS->ac = ac;
	baseVS->pseudoInode = pseudoInode;
	baseVS->VestaSource::master = VestaSource::master;
	VestaSource* baseres;
	VestaSource::errorCode err =
	  baseVS->lookupIndex(index, baseres, arc);
	delete baseVS;
	if (err != VestaSource::ok) return err;
	assert(baseres->type != VestaSource::mutableDirectory &&
	       baseres->type != VestaSource::volatileDirectory &&
	       baseres->type != VestaSource::volatileROEDirectory);

	oldType = baseres->type;
	if (oldType == VestaSource::immutableFile) {
	    oldValue = NullShortId; //unused
	} else {
	    oldValue = VMemPool::shortenPointer(baseres->rep);
	}
	oldMaster = baseres->master;
	delete baseres;
	
	// Check whether the name in arc has a sameAsBase entry
	// in the current rep, and if so, use the latter entry.
	unsigned int dummyRawIndex;
	entry = findArc(arc, dummyRawIndex, true, true);
	if (entry != NULL && !sameAsBase(entry)) {
	    entry = NULL;
	}
	if (entry != NULL) {
	    oldType = type(entry);
	    oldValue = value(entry);
	    oldMaster = masterFlag(entry);
	}
    }
    if (oldType != VestaSource::immutableFile &&
	oldType != VestaSource::immutableDirectory &&
	oldType != VestaSource::evaluatorDirectory &&
	oldType != VestaSource::evaluatorROEDirectory) {
	return VestaSource::inappropriateOp;
    }
    if (oldType == VestaSource::immutableFile && sid == NullShortId) {
	// Assign a sid.  Note that this is done before logging the
	// arguments, so upon replay, the same sid will be used again
	// and no copying will be done.
	int err;
	sid = CopyShortId(oldValue, err, copyMax);
	assert(sid != NullShortId);
    }

    // If entry is NULL, we need to make a new sameAsBase entry;
    // otherwise we need to modify the existing entry.
    
    // First log the change
    if (doLogging && VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory) {
	char logrec[512];
	OBufStream ost(logrec, sizeof(logrec));
	// ('makm' longid index sid)
	ost << "(makm " << longid << " " << index
	  << " 0x" << hex << sid << dec << ")\n";
	VRLog.start();
	VRLog.put(ost.str());
	VRLog.commit();
    }
    
    // Then make the change
    Bit32 newValue;
    if (oldType == VestaSource::immutableFile) {
	newType = VestaSource::mutableFile;
	newValue = (Bit32) sid;
    } else {
	//	assert(oldType == VestaSource::immutableDirectory ||
	//	       oldType == VestaSource::evaluatorDirectory ||
	//	       oldType == VestaSource::evaluatorROEDirectory);
	newType = VestaSource::type;
	VDirChangeable* dest = NEW_CONSTR(VDirChangeable,
					  (newType, defaultRepSize));
	dest->setIsMoreOrBase(dest->rep, isBase); // base
	dest->setMoreOrBase(dest->rep, oldValue);
	dest->setTimestampField(timestamp());
	dest->setID(indexToPseudoInode(index));
	newValue = VMemPool::shortenPointer(dest->rep);
	delete dest;
    }
    if (entry == NULL) {
	entry = appendEntry(oldMaster, true, newType, newValue, 0,
			    (newType == VestaSource::mutableFile ?
			     &nullFPTag : NULL), arc, strlen(arc));
    } else {
	setType(entry, newType);
	setValue(entry, newValue);
    }
    if (VestaSource::type == VestaSource::mutableDirectory) setSnapshot(0);

    // If this directory has a shortid reference count, update it.
    if(sidref)
      {
	// If this used to be an immutable file, then its shortid
	// should not be present in the reference counter
	assert((oldType != VestaSource::immutableFile) ||
	       (sidref->GetCount(oldValue) == 0));

	// The old type should not be a mutable file.
	assert(oldType != VestaSource::mutableFile);

	// If the new type is a mutable file, increment the reference
	// count for the mutable shortid.
	if(newType == VestaSource::mutableFile)
	  sidref->Increment(newValue);
      }
    
    result = VDCLookupResult(this, entry, index);
    if (result == NULL) return VestaSource::notFound;
    // Check for LongId overflow
    if (result->longid ==  NullLongId)
      {
	delete result;
	result = 0;
	return VestaSource::longIdOverflow;
      }
    return VestaSource::ok;
}

FP::Tag FingerprintShortid(int fd, const struct stat &st)
{
  assert(fd >= 0);
  lseek(fd, 0, SEEK_SET);
  FS::IFdStream fs(fd);
  FP::Tag result = (st.st_mode & 0111) ? executableFPTag : contentsFPTag;
  // exceptions indicate fatal server trouble, so leave uncaught
  FP::FileContents(fs, /*inout*/ result);
  return result;
}

void
VDirChangeable::makeIndexImmutable(unsigned int index,
				   unsigned int fpThreshold)
  throw ()
{
  Bit8* entry;
  Bit8* repBlock;

  entry = findRawIndex((index + 1) >> 1, repBlock);

  this->makeEntryImmutable(entry, index, fpThreshold);
}

void
VDirChangeable::makeEntryImmutable(Bit8 *entry, unsigned int index,
				   unsigned int fpThreshold,
				   const FP::Tag* snapshot_fptag)
  throw ()
{
  assert(VestaSource::type == VestaSource::mutableDirectory ||
	 VestaSource::type == VestaSource::volatileDirectory ||
	 VestaSource::type == VestaSource::volatileROEDirectory);

  // Must be an index in the mutable portion
  assert((index & 1) != 0);
  // Entry must exist and must be a mutableFile;
  assert(entry != NULL);
  assert(type(entry) == VestaSource::mutableFile);

  // Get the shortid
  ShortId filesid = value(entry);

  FP::Tag fptag;
  FP::Tag* newfptag = NULL;

  if(!doLogging)
    {
      // The only way we should get here is when recover the
      // transaction log from an older repository with a bug which
      // would arbitrarily make a shortid read-only even if there were
      // multiple hard links to it.  In this case we just use the
      // shortid as is, relying on other code to clean up the other
      // mutableFile references to the same shortid (if any).
      assert(VRLogVersion <= 3);

      // The caller must have passed a base fptag for the snapshot
      // they're about to take in this case (i.e. the caller must be
      // VDirChangeable::copyMutableToImmutable).
      assert(snapshot_fptag != 0);

      // Form the fingerprint for this file from the snapshot
      // fingerprint and the name of this file.
      fptag = *snapshot_fptag;
      fptag.Extend("/");
      fptag.Extend(arc(entry), arcLen(entry));
      newfptag = &fptag;

      // Make the index immutable
      this->makeEntryImmutable(entry, index, newfptag);

      // Skip the rest of this function.
      return;
    }

  int fd = -1, res;
  FdCache::OFlag ofl;
  struct stat st;
  ShortId dupsid = NullShortId;

  // Note that we need a writable file descriptor so that we can
  // call fsync before making this file immutable (in the case
  // that it's not a duplicate).
  fd = FdCache::open(filesid, FdCache::rw, &ofl);
  if(fd < 0)
    {
      if(errno == ENOENT)
	{
	  // The shortid was missing.  This should never happen, but
	  // we'll recover by creating it now.  
	  char *sid_name = SourceOrDerived::shortIdToName(filesid, false);
	  fd = open(sid_name, O_CREAT|O_RDWR, SourceOrDerived::default_prot);

	  // And now we'll print a message with some details about
	  // this unusual ocurrence (in case an administrator wants to
	  // know about this).
	  char file_name[256];
	  memcpy(file_name, arc(entry), arcLen(entry));
	  file_name[arcLen(entry)] = 0;
	  char fh_buf[100];
	  Repos::pr_nfs_fh(fh_buf, (nfs_fh*) &(this->longid.value));
	  Repos::dprintf(DBG_ALWAYS, 
			 "Missing shortid %s for mutable file \"%s\" in %s\n",
			 sid_name, file_name, fh_buf);

	  // Finally, clean up before going on
	  delete [] sid_name;
	}
      else
	{
	  // Can this actually happen?
	  fd = FdCache::open(filesid, FdCache::any, &ofl);
	}
    }
  assert(fd >= 0);
  res = fstat(fd, &st);
  assert(res >= 0);
  if (st.st_size < fpThreshold) {
    // Fingerprint content and check for duplication.  (Exceptions
    // indicate fatal server trouble, so leave uncaught.)
    fptag = FingerprintShortid(fd, st);
    newfptag = &fptag;
    dupsid = GetFPShortId(fptag);
  }

  if (dupsid == NullShortId) {
    // Not a duplicate.  check to see if there's more than one
    // reference.
    if(this->sidref->GetCount(filesid) > 1)
      {
	// more than one link to this mutable file.

	// We can't make this one immutable, so we'll have to copy
	// it to a new shortid.
	int err;
	dupsid = CopyShortId(filesid, err,
			     ((Basics::uint64)-1), ((Basics::uint64 *) 0),
			     false);
	assert(dupsid != NullShortId);
      }
    else
      {
	// No other references.  Make immutable.
	if(st.st_mode & 0222)
	  {
	    // We must have a writable file descriptor.
	    assert(ofl == FdCache::rw);
	    // Make it read-only on disk.
	    res = fchmod(fd, st.st_mode & ~0222);
	    assert(res >= 0);
	    // Now that it's read-only on disk, get rid of any
	    // writable file descriptors in the FdCache.
	    FdCache::flush(filesid, FdCache::rw);
	    // Flush any pending writes to disk.
	    res = fsync(fd);
	    assert(res != -1);
	  }
      }

    // Dispose of the file descriptor.
    if(ofl == FdCache::rw)
      {
	// Rather than returning this writable file descriptor
	// to the FdCache, close it.
	do
	  res = close(fd);
	while ((res == -1) && (errno == EINTR));
	assert(res != -1);
      }
    else
      {
	// Maybe this shortid was already marked as read-only?
	// I'm not sure this can happen, but if it does it
	// should be OK to return this file descriptor to the
	// FdCache.
	FdCache::close(filesid, fd, ofl);
      }
    if (newfptag == NULL)
      {
	// Not fingerprinted by content.  We must invent a unique
	// fingerprint at this point.

	if(snapshot_fptag != 0)
	  {
	    // We're currently taking a snapshot: base the fingerprint
	    // on the fingerprint of the new enclosing immutable
	    // directory and the name of this file
	    fptag = *snapshot_fptag;
	    fptag.Extend("/");
	    fptag.Extend(arc(entry), arcLen(entry));
	  }
	else
	  {
	    // We can't base it on the name, because the parent directory
	    // is not yet immutable, so we choose a unique ID instead.
	    // For mutable directories the unique ID will be logged, so
	    // recovery will still work, and there is no other particular
	    // advantage to basing fingerprints on names, so this is OK.
	    fptag = uidFPTag;
	    fptag.Extend(UniqueId());
	  }
	newfptag = &fptag;
      }

    // Note: dupsid may have been set above.
    this->makeEntryImmutable(entry, index, newfptag, dupsid);
  } else {
    // We already had a file with this fingerprint; use it instead.
    FdCache::close(filesid, fd, ofl);
    this->makeEntryImmutable(entry, index, newfptag, dupsid);
    // Unlink the newer file.  We must not do this unlink
    // unless the log entry is committed, however.  If
    // the makeIndexMutable above was done as an independent
    // transaction, it is committed now, but if it was done as
    // a nested transaction, it is not committed yet.
    // The latter case doesn't really occur in current repository
    // usage, so we haven't bothered creating machinery to unlink 
    // the file when the transaction commits; instead, if this
    // ever happens, the file will stay around until the next weed.
    // This temporarily wastes some disk space but is harmless.
    if ((VRLog.nesting() == 0) &&
	// Note, we should only unlink the shortid if there
	// are no additional references to it.
	(this->sidref->GetCount(filesid) == 0))
      {
	      
	FdCache::flush(filesid, FdCache::any);
	char* name = SourceOrDerived::shortIdToName(filesid);
	Repos::dprintf(DBG_SIDDISP, "unlinking: 0x%08x\n", filesid);
	int ret = unlink(name);
	if (ret < 0) {
	  int saved_errno = errno;
	  Text err_txt = Basics::errno_Text(saved_errno);
	  Repos::dprintf(DBG_ALWAYS, 
			 "unlink %s: %s (errno = %d)\n", name,
			 err_txt.cchars(), saved_errno);
	}
	delete[] name;
      }
  }
}

void
VDirChangeable::makeIndexImmutable(unsigned int index,
				   const FP::Tag* newfptag, ShortId newsid)
  throw ()
{
  Bit8* entry;
  Bit8* repBlock;

  entry = findRawIndex((index + 1) >> 1, repBlock);

  this->makeEntryImmutable(entry, index, newfptag, newsid);
}

void VDirChangeable::makeEntryImmutable(Bit8 *entry, unsigned int index,
					const FP::Tag* newfptag, ShortId newsid)
  throw ()
{
    assert(VestaSource::type == VestaSource::mutableDirectory ||
	   VestaSource::type == VestaSource::volatileDirectory ||
	   VestaSource::type == VestaSource::volatileROEDirectory);

    assert((index & 1) != 0);
    assert(entry != NULL);
    assert(type(entry) == VestaSource::mutableFile);

    // Work around a past bug: ignore attempts to make a file in a
    // mutable directory immutable without supplying a fingerprint
    // (even from log!).
    if (newfptag == NULL &&
	VestaSource::type == VestaSource::mutableDirectory) {
      return;
    }

    // First log the change
    if (doLogging && VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory) {

        // Make sure that we don't log a transaction that can't be
        // replayed.  This is a bit of paranoia related to deeply
        // nested directory structure.  See the code in
        // VDirChangeable::copyMutableToImmutable below which can
        // recurse past a LongId overflow.
	assert(longid != NullLongId);

	char logrec[512];
	OBufStream ost(logrec, sizeof(logrec));
	// ('maki' longid index) or
	// ('maki' longid index newfptag) or
	// ('maki' longid index newfptag newsid)
	ost << "(maki " << longid << " " << index;
	if (newfptag) {
	    ost << " ";
	    PutFPTag(ost, *newfptag);
	}
	if (newsid != NullShortId) {
	    ost << " ";
	    ost << "0x" << hex << newsid << dec;
	}
	ost << ")\n";
	VRLog.start();
	VRLog.put(ost.str());
	VRLog.commit();
    }
    
    // If this directory has a shortid reference count, update it.
    if(sidref)
      sidref->Decrement(value(entry), false);

    // Then make the change
    setType(entry, VestaSource::immutableFile);
    if (newfptag) {
	setEFPTag(entry, *newfptag);
    }
    if (newsid != NullShortId) {
	setValue(entry, newsid);
    }
    if (VestaSource::type == VestaSource::mutableDirectory) {
	setSnapshot(0);
    }
    return;
}

VestaSource::errorCode
VDirChangeable::copyIndexToMutable(unsigned int index,
				   VestaSource*& result,
				   AccessControl::Identity who) throw ()
{
    if (VestaSource::type != VestaSource::mutableDirectory) {
	return VestaSource::inappropriateOp;
    }
    if (index & 1) {
	// Odd index; already in the mutable rep
	return VestaSource::inappropriateOp;
    }
    
    if (!ac.check(who, AccessControl::write))
      return VestaSource::noPermission;

    Bit8* entry;
    VestaSource::typeTag oldType;
    Bit32 oldValue;
    bool oldMaster;
    FP::Tag oldFptag = nullFPTag;
    char arc[MAX_ARC_LEN+1];
    
    // Find the immutable entry in the base
    Bit8* baseRep = base();
    if (baseRep == NULL) return VestaSource::notFound;
	
    VestaSource* baseVS =
      NEW_CONSTR(VDirChangeable, (VestaSource::immutableDirectory, baseRep));
    baseVS->longid = longid;
    baseVS->ac = ac;
    baseVS->pseudoInode = pseudoInode;
    baseVS->VestaSource::master = VestaSource::master;
    VestaSource* baseres;
    VestaSource::errorCode err =
      baseVS->lookupIndex(index, baseres, arc);
    delete baseVS;
    if (err != VestaSource::ok) return err;
    assert(baseres->type == VestaSource::immutableDirectory ||
	   baseres->type == VestaSource::immutableFile);

    oldType = baseres->type;
    if (oldType == VestaSource::immutableFile) {
      oldValue = baseres->shortId();
      oldFptag = baseres->fptag;
    } else {
      oldValue = VMemPool::shortenPointer(baseres->rep);
    }
    oldMaster = baseres->master;
    delete baseres;
	
    // Check whether the name in arc has an entry in the current rep.
    unsigned int dummyRawIndex;
    entry = findArc(arc, dummyRawIndex, true);
    if (entry != NULL) {
      // If there's an entry in the mutable portion with the same
      // name, the object from the immutable portion has been replaced
      // or deleted.
      return VestaSource::inappropriateOp;
    }

    // (What if there's an outdated sameAsBase entry?  If the file was
    // previously made mutable, then deleted, then replaced with
    // something else with the same name, that could happen.  However,
    // there would still have to be an entry with the same name.)

    // We need to make a new sameAsBase entry.
    
    // First log the change
    if (doLogging) {
	char logrec[512];
	OBufStream ost(logrec, sizeof(logrec));
	// ('copy2m' longid index)
	ost << "(copy2m " << longid << " " << index << ")\n";
	VRLog.start();
	VRLog.put(ost.str());
	VRLog.commit();
    }
    
    entry = appendEntry(oldMaster, true, oldType, oldValue, 0,
			(oldType == VestaSource::immutableFile ?
			 &oldFptag : NULL),
			arc, strlen(arc));

    // The mutable directory has changed, so we can't re-use a
    // previous snapshot of it.
    setSnapshot(0);
    
    result = VDCLookupResult(this, entry, index);
    if (result == NULL) return VestaSource::notFound;
    // Check for LongId overflow
    if (result->longid ==  NullLongId)
      {
	delete result;
	result = 0;
	return VestaSource::longIdOverflow;
      }
    return VestaSource::ok;
}

VestaSource::errorCode
VDirChangeable::setIndexMaster(unsigned int index, bool state,
			       AccessControl::Identity who) throw ()
{
    if (VestaSource::type != VestaSource::appendableDirectory) {
	return inappropriateOp;
    }
    if (!ac.check(who, AccessControl::agreement))
      return VestaSource::noPermission;
    Bit8* entry;
    Bit8* repBlock;

    if ((index & 1) == 0) return VestaSource::notFound;
    entry = findRawIndex((index + 1) >> 1, repBlock);
    if (entry == NULL) return VestaSource::notFound;
    switch (type(entry)) {
    case VestaSource::deleted:
    case VestaSource::outdated:
    case VestaSource::gap:
      return VestaSource::notFound;
    default:
      break;
    }
    // Avoid logging null changes
    if (state == masterFlag(entry)) return VestaSource::ok;

    // First log the change
    if (doLogging && VestaSource::type != VestaSource::volatileDirectory &&
	VestaSource::type != VestaSource::volatileROEDirectory) {
	char logrec[512];
	OBufStream ost(logrec, sizeof(logrec));
	// ('mast' longid index state)
	ost << "(mast " << longid << " " << index << " "
	  << (int) state << ")\n";
	VRLog.start();
	VRLog.put(ost.str());
	VRLog.commit();
    }
    
    // Then make the change
    setMasterFlag(entry, state);
    return VestaSource::ok;
}



// Garbage collection support
// These methods tend to know more about the packed representation
void
VDirChangeable::mark(bool byName, ArcTable* hidden) throw ()
{
    bool newHidden = (hidden == NULL);
    if (newHidden) hidden = NEW(ArcTable);
    
    // Loop through entries to set inUse flags of entries, set
    //  hasName flags of child directories, and mark attributes.
    Bit8* curRepBlock = rep;
    VMemPool::typeCode t = VMemPool::type(curRepBlock);
    assert(t == VMemPool::vDirChangeable ||
	   t == VMemPool::vDirAppendable ||
	   t == VMemPool::vDirImmutable);
    Bit8* entry;
    for (;;) {
	setVisited(curRepBlock, true); // must set in more blocks for sweep
	entry = firstEntry(curRepBlock);
	while (!isEndMark(entry)) {
	    if (type(entry) != VestaSource::outdated &&
		type(entry) != VestaSource::gap) {
		ArcKey k;
		ArcValue v;
		k.s = arc(entry);
		k.slen = arcLen(entry);
		assert(k.slen != 0);
		if (!hidden->Get(k, v)) {
		    setInUse(entry, true);
		    if(!sameAsBase(entry)) hidden->Put(k, ArcValue());
		    if(VestaSource::type == VestaSource::appendableDirectory ||
		       VestaSource::type == VestaSource::mutableDirectory)
		      {
			VestaAttribsRep* ar = (VestaAttribsRep*)
			  VMemPool::lengthenPointer(attrib(entry));
			if (ar != NULL) ar->mark();
		      }
		    else if(attrib(entry) != 0)
		      {
			// Attributes in a non-appendable non-mutable
			// directory?  Not supposed to happen.
			Repos::dprintf(DBG_ALWAYS,
				       "WARNING: attributes in a non-mutabe "
				       "non-appendable directory; arc = \""
				       "%.*s\", attrib short pointer = 0x%x\n",
				       arcLen(entry), arc(entry),
				       attrib(entry));
			setAttrib(entry, 0);
		      }
		    switch (type(entry)) {
		      case VestaSource::immutableDirectory:
		      case VestaSource::appendableDirectory:
		      case VestaSource::mutableDirectory:
		      case VestaSource::volatileDirectory:
		      case VestaSource::volatileROEDirectory:
			{
			    VDirChangeable
			      childVS(type(entry), (Bit8*)
				      VMemPool::lengthenPointer(value(entry)));
			    if (!childVS.hasName()) {
				childVS.setHasName(true);
				// Must visit later byName.  OK to
				// leave visited true in more blocks
				// of child, because it is set there
				// only to tell the sweep phase the
				// blocks are not garbage.
				childVS.setVisited(false);
			    }
			}
			break;
		      case VestaSource::evaluatorDirectory:
		      case VestaSource::evaluatorROEDirectory:
			{
			    VDirEvaluator
			      childVS(type(entry), (Bit8*)
				      VMemPool::lengthenPointer(value(entry)));
			    if (!childVS.hasName()) {
				childVS.setHasName(true);
				childVS.setVisited(false); // must visit byName
			    }
			}
			break;
		      case VestaSource::immutableFile:
		      case VestaSource::mutableFile:
			assert((ShortId) value(entry) != NullShortId);
			break;
		      default:
			break;
		    }
		}
	    }
	    entry = nextEntry(entry);
	}
	if (isMoreOrBase(curRepBlock) == isMore ||
	    (isMoreOrBase(curRepBlock) == isBase &&
	     (VestaSource::type == VestaSource::immutableDirectory ||
	      VestaSource::type == VestaSource::mutableDirectory))) {
	    // Iterate on more block, or on base directory that is not
	    // already known to have a name of its own.  In the latter
	    // case, we either have visited the directory by name already
	    // or will do so later, so it's OK to skip it here.  (In fact,
	    // we *must* skip it here, or we will incorrectly mark it as
	    // having been visited by name!  This was formerly a bug,
	    // fixed in /vesta/src.dec.com/vesta/repos/91.
	    curRepBlock =
	      (Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
	    VMemPool::typeCode t = VMemPool::type(curRepBlock);
	    assert(t == VMemPool::vDirChangeable ||
		   t == VMemPool::vDirAppendable ||
		   t == VMemPool::vDirImmutable);
	    if (hasName(curRepBlock)) break; // implement above comment
	} else if (isMoreOrBase(curRepBlock) == isBase) {
	    // Recurse on base
	    Bit8* baseRep =
	      (Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
	    if (VestaSource::type == VestaSource::volatileDirectory) {
		VDirEvaluator baseVS(VestaSource::evaluatorDirectory, baseRep);
		baseVS.mark(false, hidden);
	    } else if (VestaSource::type == VestaSource::volatileROEDirectory){
		VDirEvaluator
		  baseVS(VestaSource::evaluatorROEDirectory, baseRep);
		baseVS.mark(false, hidden);
	    } else {
		assert(false);
	    }
	    break;
	} else {
   	    break;
	}
    }

    if (newHidden) delete hidden;
    
    // Loop through entries to recurse on child directories.
    curRepBlock = rep;
    for (;;) {
	entry = firstEntry(curRepBlock);
	while (!isEndMark(entry)) {
	    if (inUse(entry)) {
		switch (type(entry)) {
		  case VestaSource::immutableDirectory:
		  case VestaSource::appendableDirectory:
		  case VestaSource::mutableDirectory:
		  case VestaSource::volatileDirectory:
		  case VestaSource::volatileROEDirectory:
		    {
			VDirChangeable
			  childVS(type(entry), (Bit8*)
				  VMemPool::lengthenPointer(value(entry)));
			if (!childVS.visited()) {
			    childVS.mark(true, NULL);
			}
		    }
		    break;
		  case VestaSource::evaluatorDirectory:
		  case VestaSource::evaluatorROEDirectory:
		    {
			VDirEvaluator
			  childVS(type(entry), (Bit8*)
				  VMemPool::lengthenPointer(value(entry)));
			if (!childVS.visited()) {
			    childVS.mark(true, NULL);
			}
		    }
		    break;
		  case VestaSource::deleted:
		  case VestaSource::outdated:
		    if (value(entry) != 0) {
		      if (VestaSource::type==VestaSource::immutableDirectory) {
			// value should not be here; leftover in checkpoint
			// from a bug that is now fixed.
			setValue(entry, 0);
		      } else {
			VForward* vf =
			  (VForward*) VMemPool::lengthenPointer(value(entry));
			assert(VMemPool::type(vf) == VMemPool::vForward);
			// Check if vf still points to anything useful
			VestaSource* referent = vf->longid()->lookup();
			if (referent == NULL) {
			  setValue(entry, 0);
			} else {
			  delete referent;
			  vf->setVisited(true);
			}
		      }
		    }
		  case VestaSource::gap:
		  default:
		    break;
		}
	    }
	    entry = nextEntry(entry);
	}
	if (isMoreOrBase(curRepBlock) == isMore ||
	    (isMoreOrBase(curRepBlock) == isBase &&
	     (VestaSource::type == VestaSource::immutableDirectory ||
	      VestaSource::type == VestaSource::mutableDirectory))) {
	    // Iterate on more block, or on base directory that is not
	    // already known to have a name of its own.  See comment on
	    // previous loop.
	    curRepBlock =
	      (Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
	    VMemPool::typeCode t = VMemPool::type(curRepBlock);
	    assert(t == VMemPool::vDirChangeable ||
		   t == VMemPool::vDirAppendable ||
		   t == VMemPool::vDirImmutable);
	    if (hasName(curRepBlock)) break;
	} else {
  	    break;
	}
    }

    // Recurse on snapshot if any
    if (VestaSource::type == VestaSource::mutableDirectory) {
	Bit32 ssSP = snapshot();
	if (ssSP != 0) {
	    VDirChangeable ssVS(VestaSource::immutableDirectory,
				(Bit8*) VMemPool::lengthenPointer(ssSP));
	    ssVS.mark();
	}
    }
}

/* Statistics; examine in debugger */
/* Accurate only immediately after a sweep (source weed). */
struct {
    unsigned int entriesInUse;
    unsigned int sizeInUse;
    unsigned int entriesNotInUse;
    unsigned int sizeNotInUse;
    unsigned int sizeSpare;
    unsigned int sizeOverhead;
    unsigned int changeableDirs;
    unsigned int changeableRepBlocks;
    unsigned int immutableDirs;
    unsigned int immutableRepBlocks;
    unsigned int appendableDirs;
    unsigned int appendableRepBlocks;
} VDCStats;
/* end statistics */

void
VDirChangeable::printStats() throw ()
{
  Repos::dprintf(DBG_VMEMPOOL, "VDirChangeable size statistics:\n"
		 "entriesInUse\t%u\n"
		 "sizeInUse\t%u\n"
		 "entriesNotInUse\t%u\n"
		 "sizeNotInUse\t%u\n"
		 "sizeSpare\t%u\n"
		 "sizeOverhead\t%u\n"
		 "changeableDirs\t%u\n"
		 "changeableRepBl\t%u\n"
		 "immutableDirs\t%u\n"
		 "immutableRepBl\t%u\n"
		 "appendableDirs\t%u\n"
		 "appendableRepBl\t%u\n",
		 VDCStats.entriesInUse,
		 VDCStats.sizeInUse,
		 VDCStats.entriesNotInUse,
		 VDCStats.sizeNotInUse,
		 VDCStats.sizeSpare,
		 VDCStats.sizeOverhead,
		 VDCStats.changeableDirs,
		 VDCStats.changeableRepBlocks,
		 VDCStats.immutableDirs,
		 VDCStats.immutableRepBlocks,
		 VDCStats.appendableDirs,
		 VDCStats.appendableRepBlocks);
}

void
VDirChangeable::markCallback(void* closure, VMemPool::typeCode type) throw ()
{
    VestaSource::repositoryRoot()->setHasName(true);
    VestaSource::repositoryRoot()->mark();
    VestaSource::mutableRoot()->setHasName(true);
    VestaSource::mutableRoot()->mark();
    VestaSource::volatileRoot()->mark();
    VDCStats.entriesInUse = VDCStats.sizeInUse = 
      VDCStats.entriesNotInUse = VDCStats.sizeNotInUse = 
	VDCStats.sizeSpare = VDCStats.sizeOverhead = 
	  VDCStats.changeableRepBlocks = VDCStats.changeableDirs =
	    VDCStats.immutableRepBlocks = VDCStats.immutableDirs =
	      VDCStats.appendableRepBlocks = VDCStats.appendableDirs = 0;
}

bool
VDirChangeable::sweepCallback(void* closure, VMemPool::typeCode type,
			      void* addr, Bit32& size) throw ()
{
    FILE* sidfile = *(FILE**) closure;
    bool immutable = (type == VMemPool::vDirImmutable);
    bool appendable = (type == VMemPool::vDirAppendable);
    VDirChangeable vs(immutable ? VestaSource::immutableDirectory :
		      appendable ? VestaSource::appendableDirectory :
		      VestaSource::unused, (Bit8*) addr);
    Bit8* entry = vs.firstEntry(vs.rep);
    Bit32 freelen;
    VDCStats.sizeOverhead += (entry - vs.rep) + 1 + sizeof(freelen);
    while (!vs.isEndMark(entry)) {
	Bit8* next = nextEntry(entry);
	if (vs.inUse(entry)) {
	    if (vs.type(entry) == VestaSource::immutableFile ||
		vs.type(entry) == VestaSource::mutableFile) {
		ShortId sid = (ShortId) vs.value(entry);
		assert(sid != NullShortId);
		int res = fprintf(sidfile, "%08x\n", sid);
		// Quit early if write to sidfile fails
		if (res == EOF) return true;
	    }
	    VDCStats.entriesInUse++;
	    VDCStats.sizeInUse += next - entry;
	} else {
	  if (vs.type(entry) != VestaSource::gap) {
	    setType(entry, VestaSource::outdated);
	    setValue(entry, 0); 
	    setAttrib(entry, 0);
	  }
	  VDCStats.entriesNotInUse++;
	  VDCStats.sizeNotInUse += next - entry;
	}
	setInUse(entry, false);
	entry = next;
    }
    if (immutable) {
	if (vs.hasName()) {
	    ShortId sid = (ShortId) vs.getID();
	    if (sid != NullShortId) {
		// Subtract kept dirsids from table to produce a list
		//  of reusable ones.
		DeleteDirShortId(sid);
	    }
	    VDCStats.immutableDirs++;
	} else {
	    vs.setID(NullShortId);
	}
	VDCStats.immutableRepBlocks++;
    } else if (appendable) {
	if (vs.hasName()) {
	    VDCStats.appendableDirs++;
	}
	VDCStats.appendableRepBlocks++;
    } else {
	if (vs.hasName()) {
	    VDCStats.changeableDirs++;
	}
	VDCStats.changeableRepBlocks++;
    }
    memcpy(&freelen, entry + 1, sizeof(freelen));
    size = (entry - vs.rep) + 1 + sizeof(freelen) + freelen;
    bool ret = vs.visited();
    vs.setHasName(false);
    vs.setVisited(false);
    VDCStats.sizeSpare += freelen;
    return ret;
}

void
VDirChangeable::rebuildCallback(void* closure, VMemPool::typeCode type,
				void* addr, Bit32& size) throw ()
{
    // Rebuild the DirShortId table by adding all extant immutable
    // directories, and rebuild the FPShortId table by adding every
    // immutableDirectory and every immutable or appendable directory
    // entry for an immutableFile.  Because this is a sweep of raw
    // memory, we don't get to process each directory as a whole; we
    // see the more-blocks separately from the first block.

    bool immutable = (type == VMemPool::vDirImmutable);
    bool appendable = (type == VMemPool::vDirAppendable);
    VDirChangeable vs(immutable ? VestaSource::immutableDirectory :
		      appendable ? VestaSource::appendableDirectory :
		      VestaSource::unused, (Bit8*) addr);
    if (immutable) {
	ShortId sid = (ShortId) vs.getID();
	if (sid != NullShortId) {
	    SetDirShortId(sid, VMemPool::shortenPointer(addr));
	    SetFPDirShortId((Bit8*)addr);
	}
    }

    // Rebuild file fp => sid table, and get the size of this block.
    Bit8* entry = vs.firstEntry(vs.rep);
    while (!vs.isEndMark(entry)) {
	if ((immutable || appendable) &&
	    VDirChangeable::type(entry) == VestaSource::immutableFile) {
	    // Safe to point to this entry
	    SetFPFileShortId(entry);
	}
	entry = nextEntry(entry);
    }

    // Compute size
    Bit32 freelen;
    memcpy(&freelen, entry + 1, sizeof(freelen));
    size = (entry - vs.rep) + 1 + sizeof(freelen) + freelen;
}

static void
VDCWriteGap(Bit32& nextSP, fstream& ckpt, Bit32 gapsize) throw ()
{
  ckpt.put((char) (VestaSource::gap << 4));    // type
  ckpt.write((const char*)&gapsize, sizeof(gapsize)); // gapsize
  gapsize = 0;
  ckpt.write((const char*)&gapsize, sizeof(gapsize)); // attribs = 0
  ckpt.put(0);                                        // arcLen = 0
  nextSP += 10;
}

Bit32
VDirChangeable::checkpoint(Bit32& nextSP, fstream& ckpt) throw ()
{
    // Postorder walk of the directory DAG
    if (visited(rep)) return moreOrBase(rep); // reused field
    
    // Loop through entries to recurse on child directories
    //  and to checkpoint attributes.
    Bit8* curRepBlock = rep;
    VMemPool::typeCode t = VMemPool::type(curRepBlock);
    assert(t == VMemPool::vDirChangeable ||
	   t == VMemPool::vDirAppendable ||
	   t == VMemPool::vDirImmutable);
    for (;;) {
	Bit8* entry;
	Bit32 newChildSP;
	entry = firstEntry(curRepBlock);
	while (!isEndMark(entry)) {
	    switch (type(entry)) {
	      case VestaSource::immutableDirectory:
	      case VestaSource::mutableDirectory:
	      case VestaSource::volatileDirectory:
	      case VestaSource::volatileROEDirectory:
	      case VestaSource::appendableDirectory:
		{
		    VDirChangeable* childVS =
		      NEW_CONSTR(VDirChangeable,
				 (type(entry), (Bit8*)
				  VMemPool::lengthenPointer(value(entry))));
		    newChildSP = childVS->checkpoint(nextSP, ckpt);
		    setValue(entry, newChildSP);
		    delete childVS;
		}
		break;
	      case VestaSource::evaluatorDirectory:
	      case VestaSource::evaluatorROEDirectory:
		{
		    VDirEvaluator* childVS =
		      NEW_CONSTR(VDirEvaluator,
				 (type(entry), (Bit8*)
				  VMemPool::lengthenPointer(value(entry))));
		    newChildSP = childVS->checkpoint(nextSP, ckpt);
		    setValue(entry, newChildSP);
		    delete childVS;
		}
		break;
	      case VestaSource::deleted:
	      case VestaSource::outdated:
		if (value(entry) != 0) {
		    VForward* vf =
		      (VForward*) VMemPool::lengthenPointer(value(entry));
		    newChildSP = vf->checkpoint(nextSP, ckpt);
		    setValue(entry, newChildSP);
		}
	      case VestaSource::gap:
	      default:
		break;
	    }
	    if (attrib(entry) != 0) {
	      if(VestaSource::type == VestaSource::appendableDirectory ||
		 VestaSource::type == VestaSource::mutableDirectory)
		{
		  VestaAttribsRep* ar = (VestaAttribsRep*)
		    VMemPool::lengthenPointer(attrib(entry));
		  newChildSP = ar->checkpoint(nextSP, ckpt);
		  setAttrib(entry, newChildSP);
		}
	      else
		{
		  // Attributes in a non-appendable non-mutable
		  // directory?  Not supposed to happen.
		  Repos::dprintf(DBG_ALWAYS,
				 "WARNING: attributes in a non-mutabe "
				 "non-appendable directory; arc = \""
				 "%.*s\", attrib short pointer = 0x%x\n",
				 arcLen(entry), arc(entry),
				 attrib(entry));
		  setAttrib(entry, 0);
		}
	    }
	    entry = nextEntry(entry);
	}
	if (isMoreOrBase(curRepBlock) == isMore) {
	    curRepBlock =
	      (Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
	    VMemPool::typeCode t = VMemPool::type(curRepBlock);
	    assert(t == VMemPool::vDirChangeable ||
		   t == VMemPool::vDirAppendable ||
		   t == VMemPool::vDirImmutable);

	} else {
	    break;
	}
    }
    
    // Recurse on base
    Bit32 newBaseSP;
    if (isMoreOrBase(curRepBlock) == isBase) {
	Bit8* baseRep =
	  (Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
	if (VestaSource::type == VestaSource::volatileDirectory) {
	    VDirEvaluator* baseVS =
	      NEW_CONSTR(VDirEvaluator,
			 (VestaSource::evaluatorDirectory, baseRep));
	    newBaseSP = baseVS->checkpoint(nextSP, ckpt);
	    delete baseVS;
	} else if (VestaSource::type == VestaSource::volatileROEDirectory) {
	    VDirEvaluator* baseVS =
	      NEW_CONSTR(VDirEvaluator,
			 (VestaSource::evaluatorROEDirectory, baseRep));
	    newBaseSP = baseVS->checkpoint(nextSP, ckpt);
	    delete baseVS;
	} else {
	    VDirChangeable* baseVS = 
	      NEW_CONSTR(VDirChangeable,
			 (VestaSource::immutableDirectory, baseRep));
	    newBaseSP = baseVS->checkpoint(nextSP, ckpt);
	    delete baseVS;
	}
	
    } else {
	newBaseSP = 0;
    }
    
    // Recurse on snapshot
    if (VestaSource::type == VestaSource::mutableDirectory) {
	Bit32 ssSP = snapshot();
	if (ssSP != 0) {
	    VDirChangeable* ssVS =
    	      NEW_CONSTR(VDirChangeable,
			 (VestaSource::immutableDirectory,
			  (Bit8*) VMemPool::lengthenPointer(ssSP)));
	    ssSP = ssVS->checkpoint(nextSP, ckpt);
	    delete ssVS;
	    setSnapshot(ssSP);
	}
    }
    
    // Finally, write this object to the checkpoint file
    Bit32 newSP = nextSP; // remember starting point
    
    // Write flags
    Bit8 newFlags = rep[0];
    if (newBaseSP == 0) {
	setIsMoreOrBase(&newFlags, isNeither);
    } else {
	setIsMoreOrBase(&newFlags, isBase);
    }
    ckpt << newFlags;
    
    // Write base pointer
    ckpt.write((char *) &newBaseSP, sizeof(newBaseSP));
    
    // Write other information that comes before the entries
    ckpt.write((char *) &rep[VDIRCH_TIMESTAMP], VDIRCH_ENTRIES - VDIRCH_TIMESTAMP);
    nextSP += VDIRCH_ENTRIES;
    
    // Write all the entries, converting runs of outdated entries
    // with no forwarding pointer into gaps.
    curRepBlock = rep;
    Bit8* entry = firstEntry(curRepBlock);
    Bit32 gapsize = 0;
    for (;;) {
      if (isEndMark(entry)) {
	if (isMoreOrBase(curRepBlock) == isMore) {
	  curRepBlock =
	    (Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
	  entry = firstEntry(curRepBlock);
	  continue;
	} else {
	  if (gapsize > 0) VDCWriteGap(nextSP, ckpt, gapsize);
	  break;
	}
      }
      VestaSource::typeTag etype = type(entry);
      Bit8* next = nextEntry(entry);

      if (etype == VestaSource::gap) {
	// Accumulate existing gaps into current gap.
	gapsize += value(entry);
      } else if (etype == VestaSource::outdated &&
	         value(entry) == 0 && !sameAsBase(entry)) {
	// Accumulate outdated entries with no VForward into current gap.
	gapsize++;
      } else if (VRLogVersion >= 2 && etype == VestaSource::deleted &&
		 value(entry) == 0 && base() == NULL) {
	// No need for a deleted entry if there is no base for it to
	// shadow something in and no VForward.  With VRLogVersion >=
	// 2, the only place where such entries formerly made a
	// difference when replaying a log (copyMutableToImmutable)
	// now ignores them, so we can safely crush them out here.
	// (In fact, such entries can exist only if created under an
	// old log version, so the test here is useful only during the
	// transition period between the old and new versions.)
	gapsize++;
      } else {
	// Write out other entries	
	// First flush the last gap
	if (gapsize > 0) {
	  VDCWriteGap(nextSP, ckpt, gapsize);
	  gapsize = 0;
	}
	// Then write the entry
	ckpt.write((char *) entry, next - entry);
	nextSP += next - entry;
      }
      entry = next;
    }

    // Write the end mark
    ckpt << (Bit8) 0xff;
    nextSP++;
    
    Bit32 newFreeLen;
#if 0
    // Write out some free space
    if (VestaSource::type == VestaSource::immutableDirectory) {
	newFreeLen = 0;
    } else {
	newFreeLen = defaultRepSize / 4; // arbitrary amount
    }
#else
    // Don't write out any free space; saves memory overall
    newFreeLen = 0;
#endif
    // Pad to the required alignment
    // Assumes 2's complement negation
    newFreeLen += (-(nextSP + sizeof(newFreeLen) + newFreeLen - newSP)) &
      VMemPool::alignmentMask;
    ckpt.write((char *) &newFreeLen, sizeof(newFreeLen));
    ckpt.seekp(newFreeLen, fstream::cur);
    nextSP += sizeof(newFreeLen) + newFreeLen;
    
    // Crash if writing to the checkpoint file is failing
    if (!ckpt.good()) {
        int errno_save = errno;
	Text err = Basics::errno_Text(errno_save);
	Repos::dprintf(DBG_ALWAYS, 
		       "write to checkpoint file failed: %s (errno = %d)\n",
		       err.cchars(), errno_save);
	assert(ckpt.good());	// crash
    }
    
    setVisited(rep, true);
    setMoreOrBase(rep, newSP);	// reuse (smash) this field 
    return newSP;
}


void
VDirChangeable::freeTree() throw ()
{
    assert(VestaSource::type == VestaSource::volatileDirectory ||
	   VestaSource::type == VestaSource::volatileROEDirectory ||
	   VestaSource::type == VestaSource::mutableDirectory);
    
    // Loop through entries
    Bit8* curRepBlock = rep;
    Bit8* entry;
    for (;;) {
	entry = firstEntry(curRepBlock);
	while (!isEndMark(entry)) {
	    switch (type(entry)) {
	      case VestaSource::deleted:
	      case VestaSource::outdated:
	      case VestaSource::immutableFile:
	      case VestaSource::gap:
		break;

	      case VestaSource::mutableFile:
		if(sidref &&
		   (sidref->Decrement(value(entry), false) == 0))
		  {
		    // We can unlink this shortid if this is a
		    // volatile directory or if this isn't a nested
		    // log transaction.  (If this is a mutable
		    // directory and we're in a nested log
		    // transaction, it's not safe to unlink it yet, so
		    // we leave it around for the weeder to clean up.)
		    if((VestaSource::type != VestaSource::mutableDirectory) ||
		       (doLogging && (VRLog.nesting() == 0)))
		      unlinkSid((ShortId) value(entry));
		  }
		break;

	      case VestaSource::volatileDirectory:
	      case VestaSource::volatileROEDirectory:
	      case VestaSource::mutableDirectory:
		{
		  // Child should be the same type as this directory
		  assert(type(entry) == VestaSource::type);

		  // Recurse on entry
		  VDirChangeable
		    childVS(type(entry), (Bit8*)
			    VMemPool::lengthenPointer(value(entry)));
		  childVS.sidref = this->sidref;
		  childVS.freeTree();
		}
		break;

	      case VestaSource::evaluatorDirectory:
	      case VestaSource::evaluatorROEDirectory:
		// Can't happen; we never put an entry in volatile
		// directory that points directly to an evalutor directory.
		assert(false);

	      case VestaSource::immutableDirectory:
		assert(VestaSource::type == VestaSource::mutableDirectory);
		break;

	      default:
		assert(false);
	    }
	    entry = nextEntry(entry);
	}
	if (isMoreOrBase(curRepBlock) == isMore) {
	    Bit8* nextRepBlock = 
	      (Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
	    // Free current rep block
	    Bit32 freelen;
	    memcpy(&freelen, entry + 1, sizeof(freelen));
	    int size = (entry - curRepBlock) + 1 + sizeof(freelen) + freelen;
	    VMemPool::free(curRepBlock, size, VMemPool::vDirChangeable);
	    curRepBlock = nextRepBlock;
	} else {
	    break;
	}
    }
    
    // Do not recurse on base; normally base will be referenced from
    // an entry of the parent directory's base and deleted from there.
    
    // Free last rep block
    Bit32 freelen;
    memcpy(&freelen, entry + 1, sizeof(freelen));
    int size = (entry - curRepBlock) + 1 + sizeof(freelen) + freelen;
    VMemPool::free(curRepBlock, size, VMemPool::vDirChangeable);
}

VestaSource::errorCode
VDirChangeable::collapseBase(AccessControl::Identity who)
  throw (SRPC::failure)
{
  // This operation is only appropriate on mutable and immutable
  // directories.
  if(VestaSource::type != VestaSource::immutableDirectory &&
     VestaSource::type != VestaSource::mutableDirectory)
    return VestaSource::inappropriateOp;

  // Access check: ownership required to collapse the base of a
  // directory.
  if(!ac.check(who, AccessControl::ownership))
    return VestaSource::noPermission;

  // If we have no basis directory, there's nothing to do.
  if(base() == 0)
    return VestaSource::ok;

  // This is our original base directory.  We'll be collapsing it and
  // replacing our pointer to it with an identical version with no
  // base behind it.
  VDirChangeable originalBase(VestaSource::immutableDirectory, base());

  // If our base has no base, then collapsing our base won't make for
  // a more efficient representation, so there's nothing to do.
  // (Also, if we actually did anything in this case, we could open
  // ourselves up to a denial of service attack.)
  if(originalBase.base() == 0)
    return VestaSource::ok;

  // Create the collapsed representation of our base.
  VDirChangeable *collapsedBase = originalBase.collapse();

  // Set the base pointer of this object to the collapsed version of
  // the base.
  assert((repEndCache != 0) && (lastRepBlockCache != 0));
  setMoreOrBase(lastRepBlockCache,
		VMemPool::shortenPointer(collapsedBase->rep));

  // Log the operation
  if (doLogging)
    {
      char logrec[128];  // Should actually max out at 73 characters
      OBufStream ost(logrec, sizeof(logrec));
      // ('colb' longid)
      ost << "(colb " << this->longid << ")\n";
      VRLog.start();
      VRLog.put(ost.str());
      VRLog.commit();
    }

  delete collapsedBase;
  return VestaSource::ok;
}

int StatShortid(ShortId sid, struct stat &st, int fd = -1)
{
  int res;
  bool close_fd = false;
  FdCache::OFlag ofl;

  if (fd == -1)
    {
      fd = FdCache::tryopen(sid, FdCache::any, &ofl);
      close_fd = (fd != -1);
    }
  if (fd == -1)
    {
      // No file descriptor provided and none in the FdCache:
      // use stat.
      char *sid_fname = ShortIdBlock::shortIdToName(sid);
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
    FdCache::close(sid, fd, ofl);

  return res;
}

struct buildMutableSidrefClosure
{
  // Directory of the current list call
  VestaSource *vs;
  // Mutable shortid referenc count we're accumulating
  ShortIdRefCount *sidref;
};

static bool
buildMutableSidrefCallback(void* closure, VestaSource::typeTag type, Arc arc,
			   unsigned int index, Bit32 pseudoInode,
			   ShortId filesid, bool master)
{
  buildMutableSidrefClosure *cl = (buildMutableSidrefClosure *) closure;

  switch(type)
    {
    case VestaSource::mutableFile:
      // Mutable files are what we're looking for.  Note the reference
      // ot this one.
      cl->sidref->Increment(filesid);
      if(VestaSource::doLogging && (VRLogVersion <= 3))
	{
	  // We're out of recovery and we the last log we processed
	  // was from an older repository and therefore may have
	  // contained a bug resulting in a mutableFile reference to a
	  // read-only shortid.  Check for this and correct it.  This
	  // will create additional log entries.
	  struct stat st;
	  if(StatShortid(filesid, st) == 0)
	    {
	      if(!(st.st_mode & S_IWUSR))
		{
		  // mutableFile but shortid not writable!

		  // Fingerprint content.  We have to do this, as
		  // makeIndexImmutable requires a fingerprint.  We
		  // arbitrarily decide to fingerprint any such file by
		  // contents, regardless of how large it is.
		  FdCache::OFlag ofl;
		  int fd = FdCache::open(filesid, FdCache::any, &ofl);

		  // Exceptions indicate fatal server trouble, so leave
		  // uncaught.
		  FP::Tag fptag = FingerprintShortid(fd, st);

		  FdCache::close(filesid, fd, ofl);

		  ((VDirChangeable *) cl->vs)->makeIndexImmutable(index, &fptag);
		}
	    }
	  else
	    {
	      // missing shortid!
	      Repos::dprintf(DBG_ALWAYS, "missing: 0x%08x\n", filesid);
	    }
	}
      break;
    case VestaSource::mutableDirectory:
      // Recurse into mutable directories
      {
	buildMutableSidrefClosure newcl;
	newcl.sidref = cl->sidref;
	VestaSource::errorCode err = cl->vs->lookupIndex(index, newcl.vs); 
	assert(err == VestaSource::ok);
	err = newcl.vs->list(0, buildMutableSidrefCallback, &newcl,
			     /*who=*/ NULL, /*deltaOnly=*/ true);
	assert(err == VestaSource::ok);
	delete newcl.vs;
      }
      break;

      // Note that we don't need to process any other types.
      // Immutable files don't go into the reference counter.
      // Immutable directories can't contain mutable files.  Nothing
      // else exists in mutable directories.
    }

  return true;
}

ShortIdRefCount *VDirChangeable::rebuildMutableSidref() throw()
{
  // This can only be used on the mutable root
  assert(this->VestaSource::type == VestaSource::mutableDirectory);
  assert(this->longid == MutableRootLongId);

  // Recursively build the shoritd refcount for this directory
  buildMutableSidrefClosure cl;
  cl.vs = this;
  cl.sidref = NEW(ShortIdRefCount);
  VestaSource::errorCode err = this->list(0, buildMutableSidrefCallback, &cl,
					  /*who=*/ NULL, /*deltaOnly=*/ true);
  assert(err == VestaSource::ok);

  return cl.sidref;
}

void VDirChangeable::buildMutableSidref() throw()
{
  // This can only be used once to initialize sidref
  assert(this->sidref == 0);

  this->sidref = this->rebuildMutableSidref();
  mutable_sidref_built = true;
}

void VDirChangeable::checkMutableSidref(bool correct) throw()
{
  // This can only be used once sidref has already been initialized
  assert(this->sidref != 0);

  ShortIdRefCount *rebuilt = this->rebuildMutableSidref();

  bool match = this->sidref->Compare(*rebuilt);
  if(!match)
    {
      Repos::dprintf(DBG_ALWAYS, 
		     "%s: shortid reference count mismatch!\n",
		     correct?"WARNING":"FATAL ERROR");

      if(correct)
	{
	  delete this->sidref;
	  this->sidref = rebuilt;
	  rebuilt = 0;
	}
      else
	{
	  abort();
	}
    }

  if(rebuilt) delete rebuilt;
}

///// Low-level methods that know about the packed representation /////

VDirChangeable::VDirChangeable(VestaSource::typeTag type, Bit8* existingRep,
			       ShortIdRefCount *sidref)
  throw ()
{
    rep = existingRep;
    repEndCache = NULL;
    VestaSource::type = type;
    VestaSource::master = false;
    VestaSource::attribs = NULL;
    this->sidref = sidref;
}


VDirChangeable::VDirChangeable(VestaSource::typeTag type, int size,
			       ShortIdRefCount *sidref) throw ()
{
    if (size < VDIRCH_MINSIZE) size = VDIRCH_MINSIZE;
    rep = (Bit8*)
      VMemPool::allocate(type == VestaSource::immutableDirectory ?
			 VMemPool::vDirImmutable :
			 type == VestaSource::appendableDirectory ?
			 VMemPool::vDirAppendable :
			 VMemPool::vDirChangeable, size);
    memset(rep + 1, 0, VDIRCH_ENTRIES - 1);
    rep[VDIRCH_ENDMARK] = 0xff; // endMark
    Bit32 freeLen = size - VDIRCH_MINSIZE;
    memcpy(&rep[VDIRCH_FREELEN], &freeLen, sizeof(freeLen));
    repEndCache = &rep[VDIRCH_ENDMARK];
    nextRawIndexCache = 1;
    baseCache = NULL;
    lastRepBlockCache = rep;
    totalRepSizeCache = VDIRCH_MINSIZE;
    VestaSource::type = type;
    VestaSource::master = false;
    VestaSource::attribs = NULL;
    this->sidref = sidref;
}

VestaSource *VDirChangeable::copy() throw()
{
  VDirChangeable *result = NEW_CONSTR(VDirChangeable,
				      (this->VestaSource::type, this->rep, this->sidref));
  *result = *this;
  return result;
}

void
VDirChangeable::fillCaches() throw ()
{
    Bit8* curRepBlock = rep;
    unsigned int rawIndex = 1;
    int totalRepSize = VDIRCH_MINSIZE;
    Bit8* entry;
    for (;;) {
	entry = firstEntry(curRepBlock);
	while (!isEndMark(entry)) {
	  if (type(entry) == VestaSource::gap) {
	    rawIndex += value(entry);
	  } else {
	    rawIndex++;
	  }
	  entry = nextEntry(entry);
	}
	totalRepSize += entry - firstEntry(curRepBlock);
	if (isMoreOrBase(curRepBlock) == isMore) {
	    curRepBlock =
	      (Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
	} else {
	    break;
	}
    }
    repEndCache = entry;
    nextRawIndexCache = rawIndex;
    if (isMoreOrBase(curRepBlock) == isBase) {
	baseCache =
	  (Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
    } else {
	baseCache = NULL;
    }
    lastRepBlockCache = curRepBlock;
    totalRepSizeCache = totalRepSize;
}

void
VDirChangeable::resync(AccessControl::Identity who) throw ()
{
    repEndCache = NULL;
}

Bit8* VDirChangeable::findArc(const char* arc, unsigned int& rawIndex,
			      bool includeDeleted, bool includeOutdated)
  throw ()
{
    if (arc[0] == '\0') {
	rawIndex = 0;
	return NULL;
    }
    Bit8* curRepBlock = rep;
    Bit8* entry;
    int totalRepSize = VDIRCH_MINSIZE;
    int len = strlen(arc);
    rawIndex = 1;
    for (;;) {
	entry = firstEntry(curRepBlock);
	while (!isEndMark(entry)) {
	    if (arcLen(entry) == len
		&& memcmp(VDirChangeable::arc(entry), arc, len) == 0) {
		// Found
		switch (type(entry)) {
		  case VestaSource::deleted:
		    if (includeDeleted) return entry;
		    break;
		  case VestaSource::outdated:
		    if (includeOutdated) return entry;
		    break;
		  case VestaSource::gap:
		    break;
		  default:
		    return entry;
		}
	    }
	    if (type(entry) == VestaSource::gap) {
	      rawIndex += value(entry);
	    } else {
	      rawIndex++;
	    }
	    entry = nextEntry(entry);
	}
	totalRepSize += entry - firstEntry(curRepBlock);
	if (isMoreOrBase(curRepBlock) == isMore) {
	    curRepBlock =
	      (Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
	} else {
	    break;
	}
    }
    // Not found
    // Might as well fill the caches while we're here
    if (!repEndCache) {
	repEndCache = entry;
	nextRawIndexCache = rawIndex;
	if (isMoreOrBase(curRepBlock) == isBase) {
	    baseCache =
	      (Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
	} else {
	    baseCache = NULL;
	}
	lastRepBlockCache = curRepBlock;
	totalRepSizeCache = totalRepSize;
    }
    return NULL;
}

Bit8* 
VDirChangeable::findRawIndex(unsigned int rawIndex, Bit8*& curRepBlock)
  throw ()
{
  Bit8* entry;
  curRepBlock = rep;
  unsigned int ri = 1;
  int totalRepSize = VDIRCH_MINSIZE;
  for (;;) {
    entry = firstEntry(curRepBlock);
    while (!isEndMark(entry)) {
      if (type(entry) == VestaSource::gap) {
	ri += value(entry) - 1;
      }
      if (ri >= rawIndex) {
	// Found
	return entry;
      }
      entry = nextEntry(entry);
      ri++;
    }
    totalRepSize += entry - firstEntry(curRepBlock);
    if (isMoreOrBase(curRepBlock) == isMore) {
      curRepBlock =
	(Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
    } else {
      break;
    }
  }
  // Not found
  // Might as well fill the caches while we're here
  if (!repEndCache) {
    repEndCache = entry;
    nextRawIndexCache = ri;
    if (isMoreOrBase(curRepBlock) == isBase) {
      baseCache =
	(Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
    } else {
      baseCache = NULL;
    }
    lastRepBlockCache = curRepBlock;
    totalRepSizeCache = totalRepSize;
  }
  return NULL;
}


void
VDirChangeable::setEntry(Bit8* entry, bool mast, bool sameAsBase,
			 VestaSource::typeTag newType,
			 Bit32 value, Bit32 attrib, const FP::Tag* efptag) 
 throw ()
{
  // cannot use setEntry to change between gaps and non-gaps
  assert((type(entry) == VestaSource::gap) == (newType == VestaSource::gap));
  // preserve hasEFPTag bit
  entry[VDIRCH_EFLAGS] =
    (entry[VDIRCH_EFLAGS] & 4) | mast | (sameAsBase << 3) | (newType << 4);
  setValue(entry, value);
  setAttrib(entry, attrib);
  if (efptag == NULL) {
    if (hasEFPTag(entry))
      setEFPTag(entry, nullFPTag);
  } else {
    assert(hasEFPTag(entry));
    setEFPTag(entry, *efptag);
  }
}


Bit8*
VDirChangeable::appendEntry(bool mast, bool sameAsBase,
			    VestaSource::typeTag type, Bit32 value,
			    Bit32 attrib, const FP::Tag* efptag,
			    const char* name, int nameLen) throw ()
{
    assert(nameLen <= 255);
    Basics::int64 entryLen =
      nameLen + ((efptag == NULL) ? VDIRCH_E1MINSIZE : VDIRCH_E2MINSIZE);
    Basics::int64 newFreeLen = ((Basics::int64) freeLen()) - entryLen;
    
    if (newFreeLen < 0) {
	// Add another block
        RECORD_TIME_POINT;
	Bit8* newBlock =
	  (Bit8*) VMemPool::allocate(VMemPool::type(rep), defaultRepSize);
	RECORD_TIME_POINT;
	memset(newBlock + 1, 0, VDIRCH_ENTRIES + 1);
	newBlock[VDIRCH_ENTRIES] = 0xff; // endMark
	if (isMoreOrBase(lastRepBlock()) == isBase) {
	    setIsMoreOrBase(newBlock, isBase);
	    setMoreOrBase(newBlock, moreOrBase(lastRepBlock()));
	}
	setIsMoreOrBase(lastRepBlock(), isMore);
	setMoreOrBase(lastRepBlock(), VMemPool::shortenPointer(newBlock));
	assert(repEndCache != NULL); // caches were valid, need update
	repEndCache = &newBlock[VDIRCH_ENTRIES];
	lastRepBlockCache = newBlock;
	newFreeLen = defaultRepSize - VDIRCH_MINSIZE - entryLen;
    }
    
    Bit8* entry = repEnd();
    entry[VDIRCH_EFLAGS] =
      mast | ((efptag != NULL) << 2) | (sameAsBase << 3) | (type << 4);
    setValue(entry, value);
    setAttrib(entry, attrib);
    if (efptag == NULL) {
	entry[VDIRCH_E1ARCLEN] = nameLen;
	memcpy(&entry[VDIRCH_E1ARC], name, nameLen);
    } else {
	setEFPTag(entry, *efptag);
	entry[VDIRCH_E2ARCLEN] = nameLen;
	memcpy(&entry[VDIRCH_E2ARC], name, nameLen);
    }
    repEndCache = entry + entryLen;
    repEndCache[0] = 0xff;	// new endMark
    if (type == VestaSource::gap) {
      nextRawIndexCache += value;
    } else {
      nextRawIndexCache++;
    }
    setFreeLen(newFreeLen);
    totalRepSizeCache += entryLen;
    return entry;
}

// Knows about the packed representation
// Warning: return value does not have a valid LongId, master flag, etc.;
//  of the VestaSource fields, only the rep field is valid.
// fptag is the FP::Tag to use if a new immutable directory is created;
//  it is ignored if an existing immutable directory is reused.
VDirChangeable*
VDirChangeable::copyMutableToImmutable(const FP::Tag& fptag) throw ()
{
    if (nextRawIndex() == 1 && base() != NULL) {
	// This directory has no new entries but has a base, so
	// return the base.  Note: if the timestamp differs from the
	// base, we lose that information.
	return NEW_CONSTR(VDirChangeable,
			  (VestaSource::immutableDirectory, base()));
    }
    
    // Make the new directory.  We have to make it now to save the
    //  results of the recursion.  We will delete it later if it turns
    //  out we can reuse an old snapshot.  (Ugh.)
    int size = totalRepSize();
    VDirChangeable* dest =
      NEW_CONSTR(VDirChangeable, (VestaSource::immutableDirectory, size));
    
    // Copy base and timestamp
    if (base()) {
	dest->setIsMoreOrBase(dest->rep, isBase);
	dest->setMoreOrBase(dest->rep, VMemPool::shortenPointer(base()));
    } else {
	dest->setIsMoreOrBase(dest->rep, isNeither);
    }
    dest->setTimestampField(timestamp());
    dest->setFPTag(fptag);
    
    // Copy the entries.  A deep copy is required, except that it is
    // okay to share an immutable base (or descendent).
    Bit8* repBlock = rep;
    Bit8* p = firstEntry(dest->rep);
    unsigned int ri = 1;
    for (;;) {
	Bit8* entry = firstEntry(repBlock);
	while (!isEndMark(entry)) {
	    Bit8* next = nextEntry(entry);
	    bool skip_entry = false;
	    ShortId dupsid;
	    switch (type(entry)) {
	      case VestaSource::mutableDirectory:
		{
		    // Recursively copy this subdirectory
		    VDirChangeable* thisChild =
		      NEW_CONSTR(VDirChangeable,
				 (type(entry), (Bit8*)
				  VMemPool::lengthenPointer(value(entry)),
				  this->sidref));
		    // LongId::append dies with an assertion if the
		    // base is NullLongId, but we need to handle
		    // NullLongId here for the case of recovering an
		    // old transaction log.
		    thisChild->longid = ((this->longid != NullLongId)
					 ? this->longid.append((ri << 1) - 1)
					 : NullLongId);
		    // Don't recurse if we encounter a LongId
		    // overflow, unless we're recovering from an old
		    // transaction log
		    if((thisChild->longid != NullLongId) ||
		       (VRLogVersion < 4))
		      {
			// The only way we should be proceeding if the
			// child has NullLongId is if we're still
			// recovering an old log
			assert(!doLogging || (thisChild->longid != NullLongId));

			Bit32 childOldSnapshot = thisChild->snapshot();
			FP::Tag childFPTag = fptag;
			childFPTag.Extend("/");
			childFPTag.Extend(arc(entry), arcLen(entry));
			VestaSource* destChild =
			  thisChild->copyMutableToImmutable(childFPTag);
			memcpy(p, entry, next - entry);
			setType(p, VestaSource::immutableDirectory);
			setValue(p, VMemPool::shortenPointer(destChild->rep));
			if (childOldSnapshot != thisChild->snapshot()) {
			  // Recursive call made a new snapshot;
			  // current call must do so too.
			  setSnapshot(0);
			}
			delete destChild;
		      }
		    else
		      {
			// Print a message about this, as we are
			// making an incomplete snapshot
			char longid_str[128];
			OBufStream longid_stream(longid_str, sizeof(longid_str));
			longid_stream << this->longid;
			Repos::dprintf(DBG_ALWAYS, 
				       "WARNING: dropping deeply nested "
				       "directory named \"%.*s\" in logid %s "
				       "from new snapshot\n",
				       arcLen(entry), arc(entry),
				       longid_stream.str());

			// Skip to the next entry
			skip_entry = true;
		      }
		    delete thisChild;
		}			     
		break;
	      case VestaSource::mutableFile:
		{
		  // We need to make this index immutable first.  When
		  // not in recovery, this will create additional
		  // "maki" log entries before before the "insi" log
		  // entry.

		  // It's worth noting that the only way we should get
		  // here during recovery is if we're replaying the
		  // log of an old repository which had a bug in
		  // copyMutableToImmutable.
		  assert(doLogging || (VRLogVersion <= 3));

		  // Also worth noting is that we arbitrarily pass 0
		  // for fpThreshold (which means no files will be
		  // fingerprinted by contents).
		  this->makeEntryImmutable(entry, (ri << 1) - 1, 0, &fptag);

		  // This must now be an immutable file in the mutable
		  // directory which we're copying.
		  assert(type(entry) == VestaSource::immutableFile);

		  // There cannot be a snapshot in this case
		  assert(snapshot() == 0);

		  memcpy(p, entry, next - entry);
		  SetFPFileShortId(p);
		}
		break;
	      case VestaSource::outdated:
		// Don't copy this entry
		skip_entry = true;
		break;
	      case VestaSource::gap:
		// Don't copy this entry
		skip_entry = true;
		ri += value(entry)-1;
		break;
	      case VestaSource::immutableFile:
		// One more chance to notice we have a duplicate file.
		dupsid = GetFPShortId(efptag(entry));
		if (dupsid != NullShortId) {
		    setValue(entry, dupsid);
		}
		memcpy(p, entry, next - entry);
		if (snapshot() == 0) {
		    // Safe to record this mapping only if we are sure
		    // we will not discard the snapshot we are making now
		    // in favor of an older one.  If there is an older
		    // snapshot, this fingerprint should already be mapped
		    // to an entry in that one, so not recording it again
		    // is OK.
		    SetFPFileShortId(p);
		}
		break;
	      case VestaSource::immutableDirectory:
		memcpy(p, entry, next - entry);
		break;
	      case VestaSource::deleted:
		if (VRLogVersion >= 2 && base() == NULL) {
		  // No need to copy a deleted entry if there is no base
		  // for it to shadow something in.
		  skip_entry = true;
		} else {
		  memcpy(p, entry, next - entry);
		  setValue(p, 0);  // drop pointer to VForward if any
		}
		break;
	      default:
		assert(false);
	    }
	    if(!skip_entry)
	      {
		setAttrib(p, 0);
		setSameAsBase(p, false);
		p += next - entry;
	      }
	    ri++;
	    entry = next;
	}
	if (isMoreOrBase(repBlock) == isMore) {
	    repBlock = (Bit8*) VMemPool::lengthenPointer(moreOrBase(repBlock));
	} else {
	    break;
	}
    }
    
    // Is the snapshot we just made identical to the previous one?
    if (snapshot() != 0) {
	// Yes; discard this one and use the old one.
	// Note: we use the old one even if the timestamp has 
	// changed, as long as nothing else has.
	// Note: it is safe to delete the new snapshot only because
	// we know there are no pointers into it.  In particular, the
	// FPShortId table cannot point into it.
	VMemPool::free(dest->rep, size, VMemPool::vDirImmutable);
	dest->rep = (Bit8*) VMemPool::lengthenPointer(snapshot());
	dest->repEndCache = NULL;
    } else {
	// No; complete this one and use it.
	setSnapshot(VMemPool::shortenPointer(dest->rep));
	
	// Allocate a ShortId
	dest->setID(NewDirShortId(fptag, VMemPool::shortenPointer(dest->rep)));

	// Add entry to reverse lookup table (fp => sid)
	SetFPDirShortId(dest->rep);
	
	// Fill in the end
	*p = 0xff; // endMark
	Bit32 freeLen = size - (p + 1 + sizeof(freeLen) - dest->rep);
	memcpy(p + 1, &freeLen, sizeof(freeLen));
	
	dest->repEndCache = p;
	dest->nextRawIndexCache = nextRawIndex();
	dest->baseCache = base();
	dest->lastRepBlockCache = dest->rep;
	dest->totalRepSizeCache = size - freeLen;
    }
    return dest;
}

VestaSource::errorCode
VDirChangeable::measureDirectory(/*OUT*/VestaSource::directoryStats &result,
				 AccessControl::Identity who)
  throw (SRPC::failure)
{
  // Zero the result
  result.baseChainLength = result.usedEntryCount = result.usedEntrySize =
    result.totalEntryCount = result.totalEntrySize = 0;

  // Access check
  if (!ac.check(who, AccessControl::read))	
    return VestaSource::noPermission;

  // Set of names seen so far during our counting.
  ArcTable used;
    
  // Loop over more and base blocks.
  Bit8* curRepBlock = rep;
  for (;;) {
    Bit8 *entry, *next;
    entry = firstEntry(curRepBlock);
    // Loop over entries in this rep block.
    while (!isEndMark(entry))
      {
	result.totalEntryCount++;
	next = nextEntry(entry);
	// If this is an entry type that has a name...
	if (type(entry) != VestaSource::outdated &&
	    type(entry) != VestaSource::gap)
	  {
	    ArcKey k;
	    ArcValue v;
	    k.s = arc(entry);
	    k.slen = arcLen(entry);
	    // If we haven't marked this name as used yet...
	    if (!used.Get(k, v))
	      {
		used.Put(k, ArcValue());
		// We need to record the name of deleted entries as
		// used so we don't count a shadowed entry as used,
		// but we don't count the deleted entry as used.
		if(type(entry) != VestaSource::deleted)
		  {
		    result.usedEntryCount++;
		    result.usedEntrySize += (next-entry);
		  }
	      }
	  }
	entry = next;
      }
    // Add the size of all entries in this rep block to the total
    // entry size.
    result.totalEntrySize += (entry-firstEntry(curRepBlock));
    if (isMoreOrBase(curRepBlock) == isMore ||
	(isMoreOrBase(curRepBlock) == isBase &&
	 (VestaSource::type == VestaSource::immutableDirectory ||
	  VestaSource::type == VestaSource::mutableDirectory))) {
      // When we're following the base chain, increment the base
      // count.
      if(isMoreOrBase(curRepBlock) == isBase)
	result.baseChainLength++;
      // Iterate on more block, or on base directory.
      curRepBlock =
	(Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
    } else {
      // Otherwise no more blocks and no (suitable) base, so we're
      // done.
      break;
    }
  }

  // All is well.
  return VestaSource::ok;
}

VDirChangeable*
VDirChangeable::collapse(unsigned int newSize)
  throw()
{
  // This operation only makes sense on immutable directories.
  assert(VestaSource::type == VestaSource::immutableDirectory);

  // Set of names seen so far during our counting.
  ArcTable used;

  // If we've been told to allocate a block smaller than we know our
  // entries use, ignore the requested size.
  if(repEndCache && (totalRepSizeCache > newSize))
    newSize = totalRepSizeCache;

  // Make the new directory.
  VDirChangeable* dest =
    NEW_CONSTR(VDirChangeable, (VestaSource::immutableDirectory, newSize));

  // Copy the original fptag, directory shortid, and timestamp.
  dest->VestaSource::fptag = this->fptag();
  dest->setFPTag(dest->VestaSource::fptag);
  dest->setID(this->getID()); // Note: identical dir shortid
  dest->setTimestampField(this->timestamp());

  // Update the directory shortid and fp=>rep mapping.
  SetDirShortId(this->getID(), VMemPool::shortenPointer(dest->rep));
  SetFPDirShortId(dest->rep);

  // Loop over more and base blocks.
  Bit8* curRepBlock = rep;
  Bit32 gapsize = 0;
  for (;;) {
    Bit8 *entry;
    entry = firstEntry(curRepBlock);
    // Loop over entries in this rep block.
    while (!isEndMark(entry))
      {
	if(type(entry) == VestaSource::gap)
	  {
	    // Accumulate existing gaps into current gap.
	    gapsize += value(entry);
	  }
	else if(type(entry) == VestaSource::outdated)
	  {
	    // Treat an outdated like a size 1 gap.
	    gapsize++;
	  }
	// If this is an entry type that has a name...
	else
	  {
	    ArcKey k;
	    ArcValue v;
	    k.s = arc(entry);
	    k.slen = arcLen(entry);

	    // If we haven't marked this name as used yet, do so.
	    if (!used.Get(k, v))
	      {
		used.Put(k, ArcValue());

		// If this is not a deleted entry, we need to copy it
		// to dest.
		if(type(entry) != VestaSource::deleted)
		  {
		    // If we need to insert a gap, do so.
		    if(gapsize > 0)
		      {
			dest->appendEntry(false, false, VestaSource::gap,
					  gapsize, 0, 0, "", 0);
			gapsize = 0;
		      }

		    // Append a copy of this entry to the destination.
		    // Note: because this is an immutable directory,
		    // sameAsBase is always false and there are no
		    // attributes.
		    dest->appendEntry(masterFlag(entry), false, type(entry),
				      value(entry), 0,
				      (hasEFPTag(entry)
				       ? (FP::Tag *) &entry[VDIRCH_EFPTAG]
				       : 0),
				      arc(entry), arcLen(entry));
		  }
		else
		  {
		    // Treat deleted entries as a gap of 1.
		    gapsize++;
		  }
	      }
	    else
	      {
		// Treat entries with shadowed names as a gap of 1.
		gapsize++;
	      }
	  }
	entry = nextEntry(entry);
      }
    // If there are no more blocks, we're done.
    if(isMoreOrBase(curRepBlock) == isNeither)
      break;
    // Iterate on more block, or on base directory.
    curRepBlock =
      (Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
  }

  return dest;
}

void VDirChangeable::debugDump(std::ostream &dump) throw ()
{
  VMemPool::typeCode t = VMemPool::type(rep);
  assert(t == VMemPool::vDirChangeable ||
	 t == VMemPool::vDirAppendable ||
	 t == VMemPool::vDirImmutable);

  if(visited(rep)) return;

  // Print header and fields of the VDirChangeableRep

  PrintBlockID(dump, VMemPool::shortenPointer(rep));
  dump << endl
       << "  timestamp = " << ((unsigned int) timestamp()) << endl
       << "  pseudoInode/sid = 0x" << hex << getID() << dec << endl;
  if(this->longid != NullLongId)
    {
      dump << "  longid = " << this->longid << endl;
    }

  if (VestaSource::type == VestaSource::mutableDirectory)
    {
      assert(t == VMemPool::vDirChangeable);
      if(snapshot() != 0)
	{
	  dump << "  snapshot = ";
	  PrintBlockID(dump, snapshot());
	  dump << endl;
	}
    }
  else if(t == VMemPool::vDirImmutable) 
    {
      assert(VestaSource::type == VestaSource::immutableDirectory);
      FP::Tag fp = fptag();
      dump << "  fp = " << fp << endl;
    }

  // Next print all the entries, following more blocks.  Note, we
  // don't recurse on this pass.

  Bit8* curRepBlock = rep;
  unsigned int ri = 1;
  for (;;) {
    Bit8* entry;
    Bit32 newChildSP;
    entry = firstEntry(curRepBlock);
    while (!isEndMark(entry)) {
      dump << "  [" << ri << "]" << endl;
      if(masterFlag(entry)) dump << "   master" << endl;
      if(sameAsBase(entry)) dump << "   sameAsBase" << endl;
      dump << "   " << VestaSource::typeTagString(type(entry)) << endl;
      if(arcLen(entry) > 0)
	{
	  char arcbuf[MAX_ARC_LEN+1];
	  memcpy(arcbuf, arc(entry), arcLen(entry));
	  arcbuf[arcLen(entry)] = 0;
	  dump << "   arc = \"" << arcbuf << "\"" << endl;
	}
      switch (type(entry)) {
      case VestaSource::immutableFile:
      case VestaSource::mutableFile:
	dump << "   filesid = 0x" << hex << value(entry) << dec << endl;
	break;
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
	dump << "   subdir = ";
	PrintBlockID(dump, value(entry));
	dump << endl;
	break;
      case VestaSource::outdated:
      case VestaSource::deleted:
	if(value(entry) != 0)
	  {
	    dump << "   forward = ";
	    PrintBlockID(dump, value(entry));
	    dump << endl;
	  }
	break;
      case VestaSource::gap:
	dump << "   gapsize = " << value(entry) << endl;
	ri += value(entry)-1;
	break;
      }
      if (attrib(entry) != 0) {
	dump << "   attrib = ";
	PrintBlockID(dump, attrib(entry));
	dump << endl;
      }
      if(hasEFPTag(entry))
	{
	  FP::Tag fp = efptag(entry);
	  dump << "   efptag = " << fp << endl;
	}
      entry = nextEntry(entry);
      ri++;
    }
    if (isMoreOrBase(curRepBlock) == isMore)
      {
	Bit32 short_more = moreOrBase(curRepBlock);
	curRepBlock =
	  (Bit8*) VMemPool::lengthenPointer(short_more);
	VMemPool::typeCode t = VMemPool::type(curRepBlock);
	assert(t == VMemPool::vDirChangeable ||
	       t == VMemPool::vDirAppendable ||
	       t == VMemPool::vDirImmutable);
      }
    else
      {
	break;
      }
  }
  if (isMoreOrBase(curRepBlock) == isBase) {
    dump << "  base = ";
    PrintBlockID(dump, moreOrBase(curRepBlock));
    dump << endl;
  }

  // Now recurse on each entry, on the base, and on any snapshot.
  curRepBlock = rep;
  ri = 1;
  for (;;) {
    Bit8* entry;
    Bit32 newChildSP;
    entry = firstEntry(curRepBlock);
    while (!isEndMark(entry)) {
      switch (type(entry)) {
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
	{
	  VDirChangeable* subdir =
	    NEW_CONSTR(VDirChangeable,
		       (type(entry), (Bit8*)
			VMemPool::lengthenPointer(value(entry))));

	  subdir->longid = ((this->longid != NullLongId)
			    ? this->longid.append((VestaSource::type == VestaSource::immutableDirectory)
						  ? (ri << 1)
						  : (ri << 1) - 1)
			    : NullLongId);

	  subdir->debugDump(dump);

	  delete subdir;
	}
	break;
      case VestaSource::outdated:
      case VestaSource::deleted:
	if(value(entry) != 0)
	  {
	    VForward* vf = (VForward*) VMemPool::lengthenPointer(value(entry));
	    vf->debugDump(dump);
	  }
	break;
      case VestaSource::gap:
	ri += value(entry)-1;
	break;
      }
      if (attrib(entry) != 0)
	{
	  VestaAttribsRep* ar = (VestaAttribsRep*)
	    VMemPool::lengthenPointer(attrib(entry));
	  ar->debugDump(dump);
	}
      entry = nextEntry(entry);
      ri++;
    }
    if (isMoreOrBase(curRepBlock) == isMore)
      {
	curRepBlock =
	  (Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
      }
    else
      {
	break;
      }
  }
  if (isMoreOrBase(curRepBlock) == isBase) {
    Bit8* baseRep =
      (Bit8*) VMemPool::lengthenPointer(moreOrBase(curRepBlock));
    VDirChangeable* baseVS = 
      NEW_CONSTR(VDirChangeable,
		 (VestaSource::immutableDirectory, baseRep));
    baseVS->longid = NullLongId;
    baseVS->debugDump(dump);
    delete baseVS;
  }
  if (VestaSource::type == VestaSource::mutableDirectory) {
    Bit32 ssSP = snapshot();
    if (ssSP != 0) {
      VDirChangeable* ssVS =
	NEW_CONSTR(VDirChangeable,
		   (VestaSource::immutableDirectory,
		    (Bit8*) VMemPool::lengthenPointer(ssSP)));
      ssVS->longid = NullLongId;
      ssVS->debugDump(dump);
      delete ssVS;
    }
  }

  // We don't need to dump this rep again
  setVisited(rep, true);
}

