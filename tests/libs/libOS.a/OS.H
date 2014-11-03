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

// Last modified on Sat May 28 17:07:17 EDT 2005 by ken@xorian.net
//      modified on Fri Oct 18 10:18:57 PDT 1996 by heydon
//      modified on Tue Aug  8 09:40:13 PDT 1995 by levin

//  ******************************
//  *  Combined "OS" interfaces  *
//  ******************************


//  This header file collects together all the bits and pieces of
//  the Unix kernel interface.

#ifndef _OS
#define _OS

//  Basic stuff

#include <sys/types.h>
#include <errno.h>

//  System calls

#include <unistd.h>
#include <sys/param.h>

//  File system

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mount.h>

//  Process control

#include <sys/wait.h>

//  Timing

#include <sys/time.h>

//  Interprocess communication

#include <sys/ioctl.h>

#include <sys/socket.h>
  // generic socket declarations
#include <netinet/in.h>
  // internet-specific declarations
#include <arpa/inet.h>
  // internet-specific declarations

Text inet_ntoa_r(in_addr &in);
  // Re-entrant version of inet_ntoa


typedef int fd_t;

const fd_t NO_FD = -1;
const fd_t no_fd = NO_FD;

const int SYSOK = 0;
const int SYSERROR = -1;

namespace OS
{
  // Get the total memory size and resident set size of the calling
  // process.  Intended for use by long-running servers to report on
  // their memory usage.
  void GetProcessSize(/*OUT*/ unsigned long &total,
		      /*OUT*/ unsigned long &resident) throw();

  // Set an environmnet variable.
  void setenv(const Text &var, const Text &val) throw();
}

#endif  /* _OS */
