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

/* File: Signal.H                                              */
/* Last Modified On Wed Jun  2 18:31:46 PDT 1999 by yuanyu     */

static char* sigMsg[] = {
  "OK",         //     0      Ok
  "SIGHUP",     //     1      Hangup.
  "SIGINT",     //     2      Interrupt.
  "SIGQUIT",    //     3      Quit.  (1)
  "SIGILL",     //     4      Invalid instruction (not reset when caught).  (1)
  "SIGTRAP",    //     5      Trace trap (not reset when caught).  (1)
  "SIGABRT",    //     6      End process (see the abort() function).  (1)
  "SIGEMT",     //     7      EMT instruction.
  "SIGFPE",     //     8      Arithmetic exception, integer divide by 0 (zero),
                //            or floating-point exception.  (1)
  "SIGKILL",    //     9      Kill (cannot be caught or ignored).
  "SIGBUS",     //     10     Specification exception.  (1)
  "SIGSEGV",    //     11     Segmentation violation.  (1)
  "SIGSYS",     //     12     Invalid parameter to system call.  (1)
  "SIGPIPE",    //     13     Write on a pipe when there is no process to read it.
  "SIGALRM",    //     14     Alarm clock.
  "SIGTERM",    //     15     Software termination signal.
  "SIGURG",     //     16     Urgent condition on I/O channel.  (2)
  "SIGSTOP",    //     17     Stop (cannot be caught or ignored).  (3)
  "SIGTSTP",    //     18     Interactive stop.  (3)
  "SIGCONT",    //     19     Continue if stopped (cannot be caught or ignored).  (4)
  "SIGCHLD",    //     20     To parent on child stop or exit.  (2)
  "SIGTTIN",    //     21     Background read attempted from control terminal.  (3)
  "SIGTTOU",    //     22     Background write attempted from control terminal.  (3)
  "SIGIO",      //     23     Input/Output possible or completed.  (2)
  "SIGXCPU",    //     24     CPU time limit exceeded (see the setrlimit() function).
  "SIGXFSZ",    //     25     File size limit exceeded (see the setrlimit() function).
  "SIGVTALRM",  //     26     Virtual time alarm (see the setitimer() function).
  "SIGPROF",    //     27     Profiling time alarm (see the setitimer() function).
  "SIGWINCH",   //     28     Window size change.  (2)
  "SIGINFO",    //     29     Information request.  (2)
  "SIGUSR1",    //     30     User-defined signal 1.
  "SIGUSR2",    //     31     User-defined signal 2.
  };

char* getSigMsg(int sig) {
  if (0 <= sig && sig < 32) {
    return sigMsg[sig];
  }
  return "UNKNOWN";
}
