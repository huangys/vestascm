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

// Last modified on Sun May 22 23:03:38 EDT 2005 by ken@xorian.net
//      modified on Wed Jan 15 17:38:11 PST 1997 by heydon

#include <time.h>
#include <Basics.H>
#include <OS.H>
#include "FP.H"
#include "UniqueId.H"

using std::cerr;
using std::endl;

// const after initialization
static FP::Tag UniqueId_Prefix;

// Invocation counter.  (Becuase of the way this is used, it's
// important that this be 64 bits in size.)
static Bit64 UniqueId_count = 0UL;

// Mutex protecting the above count.
Basics::mutex UniqueId_count_mu;

FP::Tag UniqueId() throw ()
{
    FP::Tag res(UniqueId_Prefix);

    UniqueId_count_mu.lock();
    Bit64 UniqueId_num = UniqueId_count++;
    UniqueId_count_mu.unlock();

#if BYTE_ORDER == BIG_ENDIAN
    // We really want the bits that change frequently to come first in
    // what we extend the prefix by.  So, on big-endian systems, we
    // reverse the bytes before extending the tag.
    UniqueId_num = Basics::swab64(UniqueId_num);
#endif

    res.Extend((char *)(&UniqueId_num), sizeof(UniqueId_num));

    return res;
}

class UniqueIdInit {
  public:
    UniqueIdInit() throw ();
};

// invoke the constructor to initialize this module
static UniqueIdInit UniqueId_init;

#if defined(__digital__) && defined(__DECCXX_VER)
# if __DECCXX_VER >= 60290033
// Workaround for mysteriously missing declararion in new Tru64 build
// environment.  See messages with subjects "cxx problems" and
// "Summary: cxx problems (not resolved)" from the alpha-osf-managers
// mailing list:

// http://groups.yahoo.com/group/alpha-osf-managers/message/12093
// http://www.ornl.gov/cts/archives/mailing-lists/tru64-unix-managers/1998/07/msg00569.html
extern "C"
{
  long gethostid(void);
}
# endif
#endif

UniqueIdInit::UniqueIdInit() throw ()
{
    // (Using gethostid() and gethostname() and the IP address it
    // resolves to may seem like overkill, but in some cases even all
    // three of these may not be enough to guarantee that different
    // hosts will get a different prefix.  gethostid() may return a
    // default value if the system hasn't been explicitly given a
    // hostid.  gethostname() could return something generic like
    // "localhost.localdomain", which could in turn resolve to an
    // equally generic IP address.  Hopefully not all three of these
    // will be the case, so we try to include all three values in
    // UniqueId_Prefix.)

    // extend by unique host identifier
    long int hostid = gethostid();
    UniqueId_Prefix.Extend((char *)(&hostid), sizeof(hostid));

    // Try to get our hostname.  Continue anyway even if this fails.
    char my_hostname[MAXHOSTNAMELEN+1];
    if (gethostname(my_hostname, sizeof(my_hostname)-1) == 0)
      {
	// Extend by hostname
	UniqueId_Prefix.Extend(my_hostname, strlen(my_hostname));

	// Try to look up the IP address of our hotname.
	try
	  {
	    in_addr my_ip = TCP_sock::host_to_addr(my_hostname);

	    // Extend by the IP address our hostname resolves to.
	    UniqueId_Prefix.Extend((char *)(&my_ip), sizeof(my_ip));
	  }
	catch(...)
	  {
	    // Couldn't resolve our hostname?
	  }
      }

    // (Similarly, it may seem like overkill to use the pid, ppid, and
    // pgid.  In the event that two hosts happen to have the same
    // prefix up to this point, this makes it slightly less likely
    // that they will have the same prefix after this point.)

    // extend by unique process identifier
    pid_t pid = getpid();
    UniqueId_Prefix.Extend((char *)(&pid), sizeof(pid));

    // extend by parent process identifier
    pid_t ppid = getppid();
    UniqueId_Prefix.Extend((char *)(&ppid), sizeof(ppid));

    // extend by process group identifier
    pid_t pgid = getpgrp();
    UniqueId_Prefix.Extend((char *)(&pgid), sizeof(pgid));

    // extend by current time of day
    time_t tm;
    if (time(&tm) < 0) {
	cerr << "UniqueId: fatal error " << errno <<
          " getting time of day; aborting..." << endl;
	assert(false);
    }
    UniqueId_Prefix.Extend((char *)(&tm), sizeof(time_t));
}
