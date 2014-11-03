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
// VLeaf.C
// Last modified on Thu Jul 29 14:20:50 EDT 2004 by ken@xorian.net
//      modified on Tue Aug  7 17:41:12 PDT 2001 by mann
//
// Derived class for in-memory representation of Vesta sources of
// type immutableFile, mutableFile, ghost, and stub. 
//

#if defined(__linux__)
// Several methods in this file (VLeaf::read, VLeaf::write,
// VLeaf::setSize, VLeaf::size) use 64-bit file offsets/sizes.

// This macro asks the Linux system headers for 64-bit file operations
// (lseek, fstat, ftruncate, and others).
#define _FILE_OFFSET_BITS 64
#endif

#include "VLeaf.H"
#include "FdCache.H"
#include "ShortIdBlock.H"
#include "logging.H"
#include <unistd.h>

VestaSource::errorCode
VLeaf::read(void* buffer, /*INOUT*/int* nbytes, Basics::uint64 offset,
	    AccessControl::Identity who) throw ()
{
    switch (type) {
      case VestaSource::immutableFile:
      case VestaSource::mutableFile:
	break;
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::volatileROEDirectory:
      case VestaSource::evaluatorROEDirectory:
	assert(false);
	return VestaSource::isADirectory;
      case VestaSource::ghost:
      case VestaSource::stub:
	return VestaSource::invalidArgs;
      case VestaSource::deleted:
      case VestaSource::outdated:
	assert(false);
	return VestaSource::invalidArgs;
      case VestaSource::device:
	*nbytes = 0;
	return VestaSource::ok;
    }
    if (!ac.check(who, AccessControl::read))
      return VestaSource::noPermission;

    FdCache::OFlag ofl;
    int fd = FdCache::open(shortId(), FdCache::any, &ofl);
    if (fd == -1) {
	return Repos::errno_to_errorCode(errno);
    }
    errno = 0;
    if(lseek(fd, offset, SEEK_SET) != -1) {
      *nbytes = ::read(fd, buffer, *nbytes);
    }
    VestaSource::errorCode err = Repos::errno_to_errorCode(errno);
    FdCache::close(shortId(), fd, ofl);
    return err;
}


bool
VLeaf::executable() throw ()
{
    switch (type) {
      case VestaSource::immutableFile:
      case VestaSource::mutableFile:
	break;
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::volatileROEDirectory:
      case VestaSource::evaluatorROEDirectory:
	assert(false);
	return false;
      case VestaSource::ghost:
      case VestaSource::stub:
	return false;
      case VestaSource::deleted:
      case VestaSource::outdated:
	assert(false);
	return false;
      case VestaSource::device:
	return false;
    }
    FdCache::OFlag ofl;
    int fd = FdCache::open(shortId(), FdCache::any, &ofl);
    if (fd == -1) {
	return false;
    }
    bool ret = false;
    struct stat st;
    if (fstat(fd, &st) != -1) {
	ret = (st.st_mode & 0111) != 0;
    }
    FdCache::close(shortId(), fd, ofl);
    return ret;
}


Basics::uint64
VLeaf::size() throw ()
{
    switch (type) {
      case VestaSource::immutableFile:
      case VestaSource::mutableFile:
	break;
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::volatileROEDirectory:
      case VestaSource::evaluatorROEDirectory:
	assert(false);
	return 0;
      case VestaSource::ghost:
      case VestaSource::stub:
	return 0;
      case VestaSource::deleted:
      case VestaSource::outdated:
	assert(false);
	return 0;
      case VestaSource::device:
	return 0;
    }
    FdCache::OFlag ofl;
    int fd = FdCache::open(shortId(), FdCache::any, &ofl);
    if (fd == -1) {
	return 0;
    }
    struct stat st;
    Basics::uint64 ret = 0;
    if (fstat(fd, &st) != -1) {
	ret = st.st_size;
    }
    FdCache::close(shortId(), fd, ofl);
    return ret;
}


VestaSource::errorCode
VLeaf::write(const void* buffer, /*INOUT*/int* nbytes, Basics::uint64 offset,
	     AccessControl::Identity who) throw ()
{
    switch (type) {
      case VestaSource::immutableFile:
	return VestaSource::inappropriateOp;
      case VestaSource::mutableFile:
	break;
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::volatileROEDirectory:
      case VestaSource::evaluatorROEDirectory:
	assert(false);
	return VestaSource::isADirectory;
      case VestaSource::ghost:
      case VestaSource::stub:
	return VestaSource::invalidArgs;
      case VestaSource::deleted:
      case VestaSource::outdated:
	assert(false);
	return VestaSource::invalidArgs;
      case VestaSource::device:
	*nbytes = 0;
	return VestaSource::ok;
    }
    if (!ac.check(who, AccessControl::write))
      return VestaSource::noPermission;
    FdCache::OFlag ofl;
    int fd = FdCache::open(shortId(), FdCache::rw, &ofl);
    if (fd == -1) {
	return Repos::errno_to_errorCode(errno);
    }
    errno = 0;
    if (lseek(fd, offset, SEEK_SET) != -1) {
      *nbytes = ::write(fd, buffer, *nbytes);
    }
    VestaSource::errorCode err = Repos::errno_to_errorCode(errno);
    FdCache::close(shortId(), fd, ofl);
    return err;
}


VestaSource::errorCode 
VLeaf::setExecutable(bool x, AccessControl::Identity who) throw ()
{
    switch (type) {
      case VestaSource::immutableFile:
	return VestaSource::inappropriateOp;
      case VestaSource::mutableFile:
	break;
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::volatileROEDirectory:
      case VestaSource::evaluatorROEDirectory:
	assert(false);
	return VestaSource::isADirectory;
      case VestaSource::ghost:
      case VestaSource::stub:
	return VestaSource::invalidArgs;
      case VestaSource::deleted:
      case VestaSource::outdated:
	assert(false);
	return VestaSource::invalidArgs;
      case VestaSource::device:
	return VestaSource::ok;
    }
    if (!ac.check(who, AccessControl::ownership))
      return VestaSource::noPermission;

    FdCache::OFlag ofl;
    int fd = FdCache::open(shortId(), FdCache::any, &ofl);
    if (fd == -1) {
	return Repos::errno_to_errorCode(errno);
    }
    errno = 0;
    struct stat st;
    if (fstat(fd, &st) != -1) {
	fchmod(fd, (st.st_mode & ~0111) | (x ? 0111 : 0));
    }
    VestaSource::errorCode err = Repos::errno_to_errorCode(errno);
    FdCache::close(shortId(), fd, ofl);
    return err;
}


VestaSource::errorCode 
VLeaf::setSize(Basics::uint64 s, AccessControl::Identity who) throw ()
{
    switch (type) {
      case VestaSource::immutableFile:
	return VestaSource::inappropriateOp;
      case VestaSource::mutableFile:
	break;
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::volatileROEDirectory:
      case VestaSource::evaluatorROEDirectory:
	assert(false);
	return VestaSource::isADirectory;
      case VestaSource::ghost:
      case VestaSource::stub:
	return VestaSource::invalidArgs;
      case VestaSource::deleted:
      case VestaSource::outdated:
	assert(false);
	return VestaSource::invalidArgs;
      case VestaSource::device:
	return VestaSource::ok;
    }
    if (!ac.check(who, AccessControl::write))
      return VestaSource::noPermission;
    FdCache::OFlag ofl;
    int fd = FdCache::open(shortId(), FdCache::rw, &ofl);
    if (fd == -1) {
	return Repos::errno_to_errorCode(errno);
    }
    errno = 0;
    ftruncate(fd, s);
    VestaSource::errorCode err = Repos::errno_to_errorCode(errno);
    FdCache::close(shortId(), fd, ofl);
    return err;
}


time_t 
VLeaf::timestamp() throw ()
{
    switch (type) {
      case VestaSource::immutableFile:
      case VestaSource::mutableFile:
	break;
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::volatileROEDirectory:
      case VestaSource::evaluatorROEDirectory:
	assert(false);
	return 2;
      case VestaSource::ghost:
      case VestaSource::stub:
	return 2;
      case VestaSource::deleted:
      case VestaSource::outdated:
	assert(false);
	return -1;
      case VestaSource::device:
	return 2;
    }
    FdCache::OFlag ofl;
    int fd = FdCache::open(shortId(), FdCache::any, &ofl);
    if (fd == -1) {
	return -1;
    }
    errno = 0;
    time_t ts = -1;
    struct stat st;
    if (fstat(fd, &st) != -1) {
	ts = st.st_mtime;
    }
    FdCache::close(shortId(), fd, ofl);
    return ts;
}


VestaSource::errorCode
VLeaf::setTimestamp(time_t ts, AccessControl::Identity who) throw ()
{
    switch (type) {
      case VestaSource::immutableFile:
	return VestaSource::inappropriateOp;
      case VestaSource::mutableFile:
	break;
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
      case VestaSource::mutableDirectory:
      case VestaSource::volatileDirectory:
      case VestaSource::evaluatorDirectory:
      case VestaSource::volatileROEDirectory:
      case VestaSource::evaluatorROEDirectory:
	assert(false);
	return VestaSource::isADirectory;
      case VestaSource::ghost:
      case VestaSource::stub:
	return VestaSource::invalidArgs;
      case VestaSource::deleted:
      case VestaSource::outdated:
	assert(false);
	return VestaSource::invalidArgs;
      case VestaSource::device:
	return VestaSource::invalidArgs;
    }
    if (!ac.check(who, AccessControl::write) &&
	!ac.check(who, AccessControl::ownership))
      return VestaSource::noPermission;
    char *path = ShortIdBlock::shortIdToName(shortId());
    struct timeval tvp[2];
    tvp[0].tv_sec = ts;
    tvp[0].tv_usec = 0;
    tvp[1].tv_sec = ts;
    tvp[1].tv_usec = 0;
    errno = 0;
    utimes(path, tvp);
    delete path;
    return Repos::errno_to_errorCode(errno);
}
