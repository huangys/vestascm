// Copyright (C) 2001, Compaq Computer Corporation

// This file is part of Vesta.

// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// This file implements various miscellanous routines.

#include <Basics.H>
#include "OS.H"

#if defined(__digital__)
#include <mach/machine/vm_param.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/procfs.h>
#endif

using std::ifstream;

static Basics::mutex inet_ntoa_lock;

Text inet_ntoa_r(in_addr &in)
{
  Text t;
  inet_ntoa_lock.lock();
  t = inet_ntoa(in);
  inet_ntoa_lock.unlock();
  return t;
}

void OS::GetProcessSize(/*OUT*/ unsigned long &total,
			/*OUT*/ unsigned long &resident) throw()
{
  // in event of error, report zero for process sizes
  total = 0;
  resident = 0;
#if defined(__digital__)
  pid_t pid = getpid();
  char procname[20];
  sprintf(procname, "/proc/%05d", pid);
  int fd = open(procname, O_RDONLY, /*mode=*/ 0 /*ignored*/);
  if (fd >= 0) {
    struct prpsinfo psinfo;
    int ioctl_res = ioctl(fd, PIOCPSINFO, &psinfo);
    if (ioctl_res >= 0) {
      total = (int)(PAGE_SIZE * psinfo.pr_size);
      resident = (int)(PAGE_SIZE * psinfo.pr_rssize);
    }
    (void)close(fd);
  }
#elif defined(__linux__)
  pid_t pid = getpid();
  char procname[30];
  sprintf(procname, "/proc/%d/statm", pid);
  long page_size = sysconf(_SC_PAGESIZE);
  {
    ifstream l_statm(procname);
    if(!l_statm.bad())
      {
	l_statm >> total;
	total *= page_size;
	l_statm >> resident;
	resident *= page_size;
      }
  }
#else
#warning Need virtualSize/physicalSize code for this platform
#warning OS::GetProcessSize will return zeroes
  total = 0;
  resident = 0;
#endif
  
}

void OS::setenv(const Text &var, const Text &val) throw()
{
  char* ev = NEW_PTRFREE_ARRAY(char, var.Length() + 1 + val.Length() + 1);
  sprintf(ev, "%s=%s", var.cchars(), val.cchars());
  putenv(ev);
}
