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
// VestaSourceCommon.C
//
// Some common code between local and remote VestaSource clients
//

#include "VestaSource.H"
#include "VDirSurrogate.H"

const LongId RootLongId =            { 0, };
const LongId MutableRootLongId =     { 0, 1, 0, };
const LongId VolatileRootLongId =    { 0, 2, 0, };
const LongId DirShortIdRootLongId =  { 0, 3, 0, };
const LongId FileShortIdRootLongId = { 0, 4, 0, };
const LongId NullLongId =            { 0, 255, 0, };

// Length of a LongId 
int
LongId::length() const throw()
{
    if (value.byte[0] == 0 && value.byte[1] == 4) {
	// { 0, 4, s0, s1, s2, s3, s4, 0, fp0, ... fp15, 0, ... }
	return 24;
    } else {
      unsigned int len = 1;
      while((len < sizeof(value.byte)) && (value.byte[len] != 0))
	{
	  len++;
	}
      return len;
    }
}

// Construct a LongId from a ShortId
LongId
LongId::fromShortId(ShortId sid, const FP::Tag* fptag) throw ()
{
    if (SourceOrDerived::dirShortId(sid)) {
	return DirShortIdRootLongId.append((unsigned int) sid);
    } else {
	assert(fptag != NULL);
	LongId ret = FileShortIdRootLongId.append((unsigned int) sid);
	unsigned char fpbytes[FP::ByteCnt];
	fptag->ToBytes(fpbytes);
	memcpy((void*) &ret.value.byte[8],
	       (const void*) fpbytes, FP::ByteCnt);
	return ret;
    }
}

// Get the LongId of this source's index'th child.  Return NullLongId
// if there is no room for more children.  Assert false if the input
// was NullLongId.
LongId
LongId::append(unsigned int index) const throw ()
{
    LongId result = *this;
    Bit8* start = &result.value.byte[0];
    Bit8* end =
      (Bit8*) memchr((const void*) (start + 1), 0, sizeof(result.value)-1);
    if (end == NULL) {
	// No more indices will fit
	return NullLongId;
    }
    if (start[0] == 0) {
	switch (start[1]) {
	  case 0:
	    // {0, 0... }: assume it's RootLongId
	    end = start;
	    break;
	  case 1:
	    // {0, 1... }: it's a mutable directory
	    break;
	  case 2:
	    // {0, 2... }: it's a volatile directory
	    break;
	  case 3:
	    // {0, 3... }: it's an immutable directory short id
	    assert((ShortId) index != NullShortId);
	    break;
	  case 4:
	    // {0, 4... }: it's a file short id
	    // Appending beyond the short id itself is invalid
	    assert(start[2] == 0);
	    break;
	  case 255:
	    // {0, 255... }: it's NullLongId or invalid
	    assert(false);
	    break;
	  default:
	    assert(false);
	    break;
	}
    }
    while (index > 0) {
	if (end >= &result.value.byte[sizeof(result.value)]) {
	    // This index won't fit
	    return NullLongId;
	}
	int newbyte = index & 0x7f;
	index >>= 7;
	if (index > 0) newbyte |= 0x80;
	*end++ = newbyte;
    }	    
    return result;
}

// Get the LongId of this source's parent directory.  Return
// NullLongId if this source has no parent.
LongId
LongId::getParent(unsigned int* index) const throw ()
{
    unsigned int idx;
    if (value.byte[0] == 0 && value.byte[1] == 4) {
	// File short id; parent unknown
	return NullLongId;
    }
    LongId result = *this;
    Bit8* end = &result.value.byte[sizeof(result.value) - 1];
    while (*end == 0) {
	end--;
	if (end < &result.value.byte[0]) {
	    // input was RootLongId
	    return NullLongId; // no parent
	}
    }
    idx = *end;
    *end = 0;
    end--;
    while (end >= &result.value.byte[0]) {
	if ((*end & 0x80) == 0) {
	    if (*end == 0) {
		// input was NullLongId, mutable root, volatile root,
                // or shortid root.
		return NullLongId;
	    }
	    break;
	}
	idx = (idx << 7) | (*end & 0x7f);
	*end = 0;
	end--;
    }
    if (index != NULL) *index = idx;
    return result;
}

// Return true if this LongId is an ancestor of the given child.
// A LongId is considered to be its own ancestor.
bool
LongId::isAncestorOf(const LongId& child) const throw ()
{
    int i = 0;
    if (value.byte[0] == 0) {
	// Special cases
	switch(value.byte[1]) {
	  case 0:
	    // Assume this is RootLongId.
	    return (bool) (child.value.byte[0] != 0 ||
			   child.value.byte[1] == 0);
	  case 1:
	  case 2:
	  case 3:
	  case 4:
	    if (child.value.byte[0] != 0 ||
		child.value.byte[1] != value.byte[1]) {
		return false;
	    }
	    // Skip testing the first two bytes
	    i = 2;
	    break;

	  case 255:
	  default:
	    return false;
	}
    }
    for (; i < sizeof(value); i++) {
	// We do not have to extract and re-form the multi-byte
        // values packed into a LongId because the packing is done in
	// a canonical way; we need only scan for a difference and
	// look to see whether it occurs where the putative parent has
	// a zero byte, indicating its end.  Type 4 LongIds add a
	// minor complication; bytes 8-23 are a literal fingerprint,
	// not packed as a multi-byte value, so zeros in it are not
	// terminators.

	if (value.byte[i] != child.value.byte[i]) {
	    if (i >= 8 && value.byte[0] == 0 && value.byte[1] == 4) {
		return false;
	    } else {
		return (bool) (value.byte[i] == 0);
	    }
	}
    }
    return true;
}


// Default methods; mostly return inappropriateOp

static VestaSource::errorCode
badFileOp(VestaSource::typeTag type)
{
    if (type == VestaSource::immutableDirectory ||
	type == VestaSource::evaluatorDirectory ||
	type == VestaSource::evaluatorROEDirectory ||
	type == VestaSource::appendableDirectory ||
	type == VestaSource::mutableDirectory ||
	type == VestaSource::volatileDirectory ||
	type == VestaSource::volatileROEDirectory)
      return VestaSource::isADirectory;
    else 
      return VestaSource::inappropriateOp;
}

static VestaSource::errorCode
badDirectoryOp(VestaSource::typeTag type)
{
    if (type == VestaSource::immutableDirectory ||
	type == VestaSource::evaluatorDirectory ||
	type == VestaSource::evaluatorROEDirectory ||
	type == VestaSource::appendableDirectory ||
	type == VestaSource::mutableDirectory ||
	type == VestaSource::volatileDirectory ||
	type == VestaSource::volatileROEDirectory)
      return VestaSource::inappropriateOp;
    else 
      return VestaSource::notADirectory;
}

VestaSource::errorCode
VestaSource::read(void* buffer, /*INOUT*/int* nbytes,
		  Basics::uint64 offset, AccessControl::Identity who)
     throw (SRPC::failure)
{
    return badFileOp(type);
}

VestaSource::errorCode
VestaSource::readWhole(std::ostream &out, AccessControl::Identity who)
     throw (SRPC::failure)
{
    return badFileOp(type);
}

bool
VestaSource::executable()
     throw (SRPC::failure)
{
    return false;
}


Basics::uint64
VestaSource::size()
     throw (SRPC::failure)
{
    return 0;
}


VestaSource::errorCode
VestaSource::write(const void* buffer, /*INOUT*/int* nbytes,
		   Basics::uint64 offset, AccessControl::Identity who)
     throw (SRPC::failure)
{
    return badFileOp(type);
}


VestaSource::errorCode 
VestaSource::setExecutable(bool x, AccessControl::Identity who)
     throw (SRPC::failure)
{
    return badFileOp(type);
}


VestaSource::errorCode 
VestaSource::setSize(Basics::uint64 s, AccessControl::Identity who)
     throw (SRPC::failure)
{
    return badFileOp(type);
}

void
VestaSource::resync(AccessControl::Identity who)
     throw (SRPC::failure)
{
    // No-op
    return;
}

ShortId
VestaSource::shortId()
     throw (SRPC::failure)
{
    return NullShortId;
}


VestaSource::errorCode
VestaSource::lookup(Arc arc, VestaSource*& result, 
		    AccessControl::Identity who, unsigned int indexOffset)
     throw (SRPC::failure)
{
  result = 0;

    return badDirectoryOp(type);
}


VestaSource::errorCode
VestaSource::reallyDelete(Arc arc, AccessControl::Identity who,
			  bool existCheck, time_t timestamp)
     throw (SRPC::failure)
{
    return badDirectoryOp(type);
}


VestaSource::errorCode
VestaSource::insertFile(Arc arc, ShortId sid, bool master, 
			AccessControl::Identity who,
			dupeCheck chk, VestaSource** newvs, 
			time_t timestamp, FP::Tag* fptag)
     throw (SRPC::failure)
{
    return badDirectoryOp(type);
}

VestaSource::errorCode
VestaSource::insertMutableFile(Arc arc, ShortId sid, bool master, 
			       AccessControl::Identity who,
			       dupeCheck chk, VestaSource** newvs,
			       time_t timestamp)
     throw (SRPC::failure)
{
    return badDirectoryOp(type);
}

VestaSource::errorCode
VestaSource::insertImmutableDirectory(Arc arc, VestaSource* dir,
				      bool master, AccessControl::Identity who,
				      dupeCheck chk, VestaSource** newvs,
				      time_t timestamp, FP::Tag* fptag)
     throw (SRPC::failure)
{
    return badDirectoryOp(type);
}

VestaSource::errorCode
VestaSource::insertAppendableDirectory(Arc arc, bool master, 
				       AccessControl::Identity who,
				       dupeCheck chk, VestaSource** newvs,
				       time_t timestamp)
     throw (SRPC::failure)
{
    return badDirectoryOp(type);
}

VestaSource::errorCode
VestaSource::insertMutableDirectory(Arc arc, VestaSource* dir,
				    bool master, AccessControl::Identity who,
				    dupeCheck chk, VestaSource** newvs,
				    time_t timestamp)
     throw (SRPC::failure)
{
    return badDirectoryOp(type);
}


VestaSource::errorCode
VestaSource::insertGhost(Arc arc, bool master, AccessControl::Identity who,
			 dupeCheck chk, VestaSource** newvs, time_t timestamp)
     throw (SRPC::failure)
{
    return badDirectoryOp(type);
}


VestaSource::errorCode
VestaSource::insertStub(Arc arc, bool master, AccessControl::Identity who,
			dupeCheck chk, VestaSource** newvs, time_t timestamp)
     throw (SRPC::failure)
{
    return badDirectoryOp(type);
}


VestaSource::errorCode
VestaSource::renameTo(Arc arc, VestaSource* fromDir, Arc fromArc,
		      AccessControl::Identity who,
		      dupeCheck chk, time_t timestamp)
     throw (SRPC::failure)
{
    return badDirectoryOp(type);
}


VestaSource::errorCode
VestaSource::getBase(VestaSource*& result, AccessControl::Identity who)
     throw (SRPC::failure)
{
	return badDirectoryOp(type);
}


VestaSource::errorCode
VestaSource::list(unsigned int firstIndex,
		  VestaSource::listCallback callback, void* closure,
		  AccessControl::Identity who,
		  bool deltaOnly, unsigned int indexOffset)
{
    return badDirectoryOp(type);
}

VestaSource::errorCode
VestaSource::lookupIndex(unsigned int index, VestaSource*& result,
			 char *arcbuf)
     throw (SRPC::failure)
{
  result = 0;

    return badDirectoryOp(type);
}

VestaSource::errorCode
VestaSource::setIndexMaster(unsigned int index, bool state,
			    AccessControl::Identity who)
     throw (SRPC::failure)
{
    return badDirectoryOp(type);
}


time_t
VestaSource::timestamp()
     throw (SRPC::failure)
{
    return 2;  // arbitrary nonzero value used for ghosts, etc.
}


VestaSource::errorCode
VestaSource::setTimestamp(time_t ts, AccessControl::Identity who)
     throw (SRPC::failure)
{
    return badFileOp(type);
}

bool
VestaSource::hasName() throw ()
{
    assert(false);
    return false;
}

void
VestaSource::setHasName(bool val) throw ()
{
    assert(false);
}

bool
VestaSource::visited() throw ()
{
    assert(false);
    return false;
}

void
VestaSource::setVisited(bool val) throw ()
{
    assert(false);
}

void
VestaSource::mark(bool byName, ArcTable* hidden) throw ()
{
    assert(false);
}

Bit32
VestaSource::checkpoint(Bit32& nextSP, std::fstream& ckpt) throw ()
{
    assert(false);
    return 0;
}

void VestaSource::debugDump(std::ostream &dump) throw ()
{
    assert(false);
}

void
VestaSource::freeTree() throw ()
{
    assert(false);
}

static char *errText[] = { "ok", "notFound", "noPermission", "nameInUse",
			   "inappropriateOp", "pathnameTooLong", "rpcFailure",
			   "notADirectory", "isADirectory", "invalidArgs",
			   "outOfSpace", "notMaster", "longIdOverflow",
			   // These would be the next two, but they're
			   // currently not valid.
			   "13?", "14?" };
static char *typeText[] = { "immutableFile", "mutableFile",
			    "immutableDirectory", "appendableDirectory",
			    "mutableDirectory", "ghost", "stub", "deleted",
			    "outdated", "volatileDirectory",
			    "evaluatorDirectory", "device",
			    "volatileROEDirectory", "evaluatorROEDirectory",
			    "gap", "unused" };
static char typeChar[] = { 'f', 'u', 'i', 'a', 'm', 'g', 's', 'd', 'o', 'v',
			   'e', 'n', 'r', 'l', 'p', 'x' };
static char *opText[] = { "opSet", "opClear", "opAdd", "opRemove" };
static char opChar[] = { 's', 'c', 'a', 'r' };

const char*
VestaSource::errorCodeString(VestaSource::errorCode err) throw ()
{
    return errText[(int) err];
}

const char*
VestaSource::typeTagString(VestaSource::typeTag type) throw ()
{
    return typeText[(int) type];
}   

char
VestaSource::typeTagChar(VestaSource::typeTag type) throw ()
{
    return typeChar[(int) type];
}   

const char*
VestaAttribs::attribOpString(VestaSource::attribOp op) throw ()
{
    return opText[(int) op];
}   

char
VestaAttribs::attribOpChar(VestaSource::attribOp op) throw ()
{
    return opChar[(int) op];
}   

VestaSource::errorCode
VestaSource::setMaster(bool state, AccessControl::Identity who)
     throw (SRPC::failure)
{
    unsigned int index;

    VestaSource *parent = longid.getParent(&index).lookup();
    if (parent == NULL) return VestaSource::notFound;
    VestaSource::errorCode err = parent->setIndexMaster(index, state, who);
    delete parent;
    if (err == VestaSource::ok) master = state;
    return err;
}

// If it's not the VDirSurrogate subclass, must be local
Text
VestaSource::host() throw()
{
    return VDirSurrogate::defaultHost();
}

Text
VestaSource::port() throw()
{
    return VDirSurrogate::defaultPort();
}

VestaSource::errorCode
VestaSource::measureDirectory(/*OUT*/VestaSource::directoryStats &result,
			      AccessControl::Identity who)
  throw (SRPC::failure)
{
  return VestaSource::inappropriateOp;
}

VestaSource::errorCode
VestaSource::collapseBase(AccessControl::Identity who)
  throw (SRPC::failure)
{
  return VestaSource::inappropriateOp;
}

unsigned int VestaSource::linkCount()
{
  return 1;
}

VestaSource *VestaSource::copy() throw()
{
  // This operation is for concrete subclasses.
  assert(false);
  return 0;
}

Text VestaSource::getMasterHint() throw(SRPC::failure)
{
  // This operation is for VDirSurrogate only.
  assert("client side only" && false);
  return "";
}
