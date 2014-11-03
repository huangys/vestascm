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
// FdCache.H
// Last modified on Wed Apr  7 13:50:58 EDT 2004 by ken@xorian.net
//      modified on Wed Feb 10 20:19:09 PST 1999 by mann
//
// Cache of open file descriptors for files named by ShortId
//

#ifndef _FD_CACHE
#define _FD_CACHE 1

#include "Basics.H"
#include "SourceOrDerived.H"
#include "ShortIdKey.H"

namespace FdCache
{
    enum OFlag { ro, rw, any };

    // Obtain an open file descriptor, and reserve it so that other
    // threads can't use it concurrently.  The descriptor's seek
    // pointer location is unspecified.  If -1 is returned, a standard
    // error code is in errno.  The mode may be specified as
    // read/write, read-only, or any.  In the "any" case, modeOut must
    // be supplied to receive the actual mode type returned.
    int open(ShortId sid, OFlag ofl, OFlag* oflOut =NULL) throw();

    // Like open, but if there is no open file descriptor already
    // available in the cache, returns -1 rather than attempting to
    // open a file descriptor.  (If a value other than -1 is returned,
    // the caller is responsible for calling FdCache::close.)
    int tryopen(ShortId sid, OFlag ofl, OFlag* oflOut =NULL) throw();

    // Return an open file descriptor to the cache.  The descriptor
    // need not have been obtained via FdCache::open, but the correct
    // mode must be specified ("any" is not permitted).
    void close(ShortId sid, int fd, OFlag ofl) throw();

    // Flush a ShortId from the cache.  If mode is specified as
    // read-only or read/write, only descriptors in that mode are
    // flushed; if it is specified as any, all descriptors are
    // flushed.
    void flush(ShortId sid, OFlag ofl) throw();

    // Get statistics from the file descriptor cache. "n_in_cache" is
    // set to the number of file descriptors currently in the cache.
    // "hits" is set to the total number of requests for a file
    // descriptor that found a matching one in the cache.
    // "open_misses" is set to the total number of requests for a file
    // descriptor that failed to find a matching one in the cache and
    // subsequently opened a new file descriptor.  "try_misses" is set
    // to the total number of requests for a file descriptor that
    // failed to find a matching one in the cache and caused calling
    // code to proceed without a file descriptor.  "evictions" is set
    // to the total number of times that a file descriptor has been
    // evicted from the cache to make room for another one.
    // "expirations" is set to the number of times that a file
    // descriptor has been removed by the janitor thread (after being
    // unused for some time).
    void getStats(/*OUT*/ Basics::uint32 &n_in_cache,
		  /*OUT*/ Basics::uint64 &hits,
		  /*OUT*/ Basics::uint64 &open_misses,
		  /*OUT*/ Basics::uint64 &try_misses,
		  /*OUT*/ Basics::uint64 &evictions,
		  /*OUT*/ Basics::uint64 &expirations);
}

#endif
