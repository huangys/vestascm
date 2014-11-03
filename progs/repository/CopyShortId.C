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

#include "system.h"

#include "CopyShortId.H"
#include "ShortIdBlock.H"
#include "FdCache.H"

#define COPY_SIZE 8192

ShortId CopyShortId(ShortId oldsid, /*OUT*/ int &errno_out,
		    Basics::uint64 n, Basics::uint64 *copied,
		    bool writable)
{
    FdCache::OFlag oldofl;
    int oldfd = FdCache::open(oldsid, FdCache::any, &oldofl);
    if (oldfd < 0) {
	errno_out = errno;
	return NullShortId;
    }
    if (lseek(oldfd, 0L, SEEK_SET) < 0) {
	errno_out = errno;
	FdCache::close(oldsid, oldfd, oldofl);
	return NullShortId;
    }	
    ShortId newsid;
    int newfd = SourceOrDerived::fdcreate(newsid);
    if (newfd < 0) {
	errno_out = errno;
	FdCache::close(oldsid, oldfd, oldofl);
	return NullShortId;
    }
    struct stat st;
    if (fstat(oldfd, &st) < 0) {
	errno_out = errno;
	FdCache::close(oldsid, oldfd, oldofl);
	close(newfd);
	return NullShortId;
    }
    Basics::uint64 i = n;
    while (i > 0) {
	char buf[COPY_SIZE];
	int len = COPY_SIZE;
	if (len > i) len = i;
	len = read(oldfd, buf, len);
	if (len < 0) {
	    errno_out = errno;
	    FdCache::close(oldsid, oldfd, oldofl);
	    close(newfd);
	    return NullShortId;
	}
	if (len == 0) break;
	len = write(newfd, buf, len);
	if (len < 0) {
	    errno_out = errno;
	    FdCache::close(oldsid, oldfd, oldofl);
	    close(newfd);
	    return NullShortId;
	}
	i -= len;
	if (len < COPY_SIZE) break;
    }
    if(copied)
      *copied = (n - i);

    if(writable)
      fchmod(newfd, st.st_mode | 0200); // make writable
    else
      fchmod(newfd, st.st_mode & ~0222); // make read-only
    struct timeval tvp[2];
    tvp[0].tv_sec = st.st_atime;
    tvp[1].tv_sec = st.st_mtime;
#if __digital__
    tvp[0].tv_usec = st.st_spare1;
    tvp[1].tv_usec = st.st_spare2;
#else
    tvp[0].tv_usec = 0;
    tvp[1].tv_usec = 0;
#endif
    char* path = ShortIdBlock::shortIdToName(newsid);
    if (utimes(path, tvp) < 0) {
	errno_out = errno;
	FdCache::close(oldsid, oldfd, oldofl);
	close(newfd);
	return NullShortId;
    }
    delete [] path;
    FdCache::close(oldsid, oldfd, oldofl);
    FdCache::close(newsid, newfd, FdCache::rw);
    return newsid;
  
}
