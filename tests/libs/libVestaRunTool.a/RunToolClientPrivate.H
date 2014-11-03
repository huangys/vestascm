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

// This file contains definitions relevant only to the implementation
// of the RunTool facilities.

/*****************************/
/*  Configuration Parameters */
/*****************************/

#define RUN_TOOL_CONFIG_SECTION "Run_Tool"
#define RUN_TOOL_INTERFACE_VERSION 4
//first versions of the interface that support features
#define RUN_TOOL_LOAD_VERSION 3
#define RUN_TOOL_CORE_VERSION 4

// Procedure numbers in interface
#define RUN_TOOL_DOIT    1
#define RUN_TOOL_OLDINFO 3
#define RUN_TOOL_INFO    4

// The Tool_Relay class handles relaying output from stdout or stderr
// on a remotely-executing tool.

class Tool_Relay {
public:
  Tool_Relay();
  ~Tool_Relay();

  void setup(OS::ThreadIOHelper* report, std::ostream* value, const Text& label)
    throw(TCP_sock::failure);
    // report and value are the streams to which the data is to be written.  
    // If either is NULL, it is not used.
    // mu must be held to write on report, and label is prepended to
    // each line of report.

  bool finish(bool wait) throw(TCP_sock::failure, FS::Failure);
    // Closes down the relay.  Returns true if the relay has written
    // any data.  If wait is true, waits for the tool to finish writing
    // data and close its end of the socket; if wait is false, does not.
    // Must not be called concurrently with setup().  No-op if called
    // before setup().

  void get_sockaddr(sockaddr_in &sock);
    // computes the socket address to transmit to the server

  void body();
    // The main body of the thread that relays output from socket 's'
    // to file 'out_fd'.
    // (for internal use only, but can't be private)

private:
  Basics::mutex m;  // synchronization between finish() and body thread.
  TCP_sock *listener;
  TCP_sock *s;
  Basics::thread relay;
  OS::ThreadIOHelper* report;
  std::ostream *value;
  Text label;
  char *buffer;
  int buff_len;
  int remaining;
  bool written;
  TCP_sock::failure tcp_f;

  bool write_failed;         // Has there been a write failure?
  FS::Failure write_failure; // Saved write failure

  // A Tool_Relay object should neither be copied nor assigned.

  Tool_Relay(Tool_Relay &);

  void operator=(Tool_Relay &);

};

const int RELAY_BUFFER_SIZE = 512;

// Calling sequences, marshalled type in parentheses:

// Run a tool
// Procid:
//   RUN_TOOL_DOIT
// Arguments:
//   stdin_file  (Text):       "" => /dev/null
//   stdout_sock (chars):      socket for stdout (all 0's => discard)
//   stderr_sock (chars):      socket for stderr (all 0's => discard)
//   fsroot_sock (chars):      socket for fsroot server
//   fsroot_fh   (bytes):      32-byte NFS file handle for fsroot
//   wd          (Text):       fsroot-relative path for working directory
//   ev          (chars_seq):  environment variables
//   argv        (chars_seq):  command line
// Results:
//   status      (int):        exit status from tool
//   sigval      (int):        signal number, if terminated by signal

// Get server and platform information
// Procid:
//  RUN_TOOL_INFO
// Arguments:
//   None
// Results:
//   sysname     (Text):       Operating system implementation (uname -s)
//   release     (Text):       Operating system release (uname -r)
//   version     (Text):       Operating system version (uname -v)
//   machine     (Text):       Type of machine (uname -m)
//   cpus        (int):        Number of cpus
//   cpuMHz      (int):        Clock rate of cpu(s) in MHz
//   memKB       (int):        Physical memory in kilobytes
//   max_tools   (int):        Maximum number of concurrent tools allowed
//   cur_tools   (int):        Number of tools currently running
//   load	 (float):      32-bit fp load average
//   cur_pending (int):	       Number of tools currently on the pending queue
//   max_pending (int):        Maximum number of tools allowed on the pending queue
//   id          (bytes):      16-byte UniqueId for this server instance
