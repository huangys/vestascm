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

// Last modified on Wed Aug 14 16:51:09 EDT 2002 by kcschalk@shr.intel.com
//      modified on Thu May 16 23:48:33 EDT 2002 by scott@scooter.cx
//      modified on Tue Nov 13 12:22:33 EST 2001 by ken@xorian.net
//      modified on Thu Apr 11 10:20:40 PDT 1996 by levin

//  ****************
//  *  Launcher.h  *
//  ****************

#ifndef _Launcher
#define _Launcher

// This file describes the use of the program 'tool_launcher'.

// This program launches a tool in a controlled file system environment
// provided by Vesta.  To do so, it runs the tool with its root directory
// at a mount point controlled by Vesta (specifically, an NFS server
// associated with a Vesta evaluator).  All file system accesses are thus
// relative to that mount point.

// This program must run as root in order to execute the chroot system
// call, after which it gives up its root privilege.  The name of the
// mount point for chroot is passed as a command line parameter.  The
// mount point is actually created by the program that invokes this
// one, which is a non-privileged program (RunToolServer).  As a result,
// no setuid programs can be run by the tool being forked.

// This division of responsibilities makes the amount of privileged code
// very small, so that it's easy to determine by inspection that the code
// is well-behaved.

// 'tool_launcher' is invoked by an 'exec' system call.  Its command
// line is as follows:
//
//     <launcher> [-dbg fd] [-t] -err fd -root root [-env ev] -cmd argv
//   where:
//     -dbg fd     if present, specifies that debugging output is to be
//                 written to the file descriptor 'fd'.
//     -t          if present, specifies testing mode.  The 'chroot'
//                 operation is not performed, although the "-root" argument
//                 must still be present.
//     -err fd     'fd' is the file descriptor to be used to report errors
//                 during the launching process, including the final 'exec'
//                 of the specified command.  See description of error
//                 handling, below.
//     -root dir   'dir' specifies the path to be used as the tool's root
//                 directory.  This directory is the subject of the 'chroot'
//                 system call.
//     -env ev     if present, 'ev' is a sequence of environment variable
//                 definitions.  That is, 'ev' is a sequence of arguments
//                 each of which is a string in the form specified by the
//                 'execve' system call.
//     -cmd argv   'argv' is a sequence of arguments that form the command line
//                 for the tool.  The first of these is the tool name itself,
//                 interpreted as a path in the file system rooted at the
//                 the directory specified by the "-root" argument, above.

//  Error handling:
//
//  The invoker of tool_launcher uses an 'exec' system call, and tool_launcher
//  uses an 'exec' system call for the tool specified on its command line.
//  Normally, the invoker of tool_launcher expects the exit status to reflect
//  the tool's outcome, but it needs to recognize and report cases in which
//  tool_launcher was unable to launch the tool.  This works as follows:
//  *  The invoker of tool_launcher specifies a file descriptor for error
//     reporting purposes.  This file descriptor is the argument to "-err",
//     above.  This file descriptor should be the "write" end of a pipe
//     that the invoker can read, called the "error pipe".
//  *  After 'exec-ing' tool_launcher, the invoker does a 'wait' system
//     call.
//  *  When 'wait' returns, the invoker first tries to read the error pipe.
//     By this time, the write end of the pipe will have been closed.  If
//     the read returns 0 (i.e., no data), then the tool_launcher successfully
//     launcher the tool specified on its command line, and the exit status
//     reported by 'wait' reflects that tool's outcome.  If the read of the
//     error pipe returns data, then the entire contents of the pipe is an
//     error message from the tool_launcher reporting its inability to
//     launch the requested tool.  In this case, the exit status from
//     'wait' is one of the values of 'launcher_failure', below.

enum launcher_failure {
  missing_switch = 1,                // implementation bug
  bogus_fd = 2,                      // implementation bug
  bogus_root_directory = 3,          // implementation bug
  missing_command = 4,               // implementation bug
  seteuid_failure = 5,               // implementation bug
  fcntl_failure = 6,                 // implementation bug
  unrecognized_switch = 7,           // possible error in vesta.cfg
                                     // [Run_Tool]helper_switches
  chroot_failure = 8,                // possible configuration problem
                                     // perhaps tool_launcher isn't setuid?
  execve_failure = 9,                // probable user error: tool name
                                     // invalid or file system rooted at
                                     // "-root root" doesn't have necessary
                                     // files to launch tool (e.g. dynamic
                                     // loader or shared libraries

  getcwd_failure = 10,               // getcwd failed (i.e. WD not
				     // readable)

  wd_not_in_root = 11,               // the working directory is
				     // outside the root directory
				     // (disallowed)

  chdir_after_chroot_failure = 12,   // Couldn't chdir to the working
				     // directory after the chroot.

  mount_proc_failure = 13,

  mount_tmpfs_failure = 14,

  launcher_failure_enum_end          // new kinds of launcher failure
                                     // should be defined before
                                     // launcher_failure_enum_end value  
  };

//  The comments on the elements of 'launcher_failure' indicate whether
//  the error is likely to be an implementation problem in Vesta, or a
//  configuration problem or client error.  In the latter case, the error
//  should be reported to the user in a sensible fashion.  In the former case,
//  the invoker (RunToolServer) should either log the problem or perhaps
//  crash.



#endif  /* _Launcher */
