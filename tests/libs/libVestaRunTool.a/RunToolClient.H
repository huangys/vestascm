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

// ----------------------------------------------------------------------

// This interface defines a mechanism for running a tool (i.e.,
// a Unix program) as part of a Vesta evaluation.  The tool runs
// in an encapsulated environment in which most facilities are
// controlled by the Vesta evaluator.  Specifically, the evaluator
// controls:
//   * stdin, stdout, stderr
//   * the environment variables
//   * the file system
// The interface allows the tool to be executed on a specified platform.
// Thus, it gives functionality similar to 'rsh', but with the additional
// controls provided by the encapsulation of the environment.

#ifndef _RUN_TOOL_CLIENT_H
#define _RUN_TOOL_CLIENT_H

#include <Basics.H>
#include <OS.H>
#include <UniqueId.H>
#include <ThreadIO.H>

// SRPC stuff
#include <SRPC.H>
#include <MultiSRPC.H>

// Repository stuff
#include <VestaSource.H>

// Internal stuff
#include "RunToolClientPrivate.H"

#define DEFAULT_TOOL_WD ".WD"

// The following are additional values for SRPC::failure.r that
// can arise from calls of RunTool::do_it.

enum RunToolError {
  client_error = 10,                             // probable client error
  configuration_error = 11,                      // probably Vesta set-up error
  implementation_error = SRPC::internal_trouble  // RunTool/tool_launcher bug
};

extern "C" void RunTool_init();

class RunTool {
public:

  struct Tool_result {     // see explanation below
    int status;
    int sigval;
    bool stdout_written;
    bool stderr_written;
    bool dumped_core;
  };

  static Tool_result do_it(
        const Text &host,  // hostname, or hostname:port
	chars_seq *argv,
	LongId &fsroot,    // must be under the VolatileRoot
	const Text &wd = DEFAULT_TOOL_WD,
	chars_seq *ev = NULL,
	OS::ThreadIOHelper *report_out = NULL,
	std::ostream *value_out = NULL,
	OS::ThreadIOHelper *report_err = NULL,
	std::ostream *value_err = NULL,
	const Text &label = "",
	const Text &stdin_name = ""
	) throw(SRPC::failure, FS::Failure);

  /* This single operation causes a tool to be executed.  The name of
     the host on which execution is to occur is given by 'host'; this
     host must be running the server portion of the RunTool implementation.
     If no port is given, the default RunToolServer port is assumed.

     The tool and its command line are specified by 'argv', which is a
     pointer to a vector of null-terminated strings.  As usual, argv[0]
     is the name of the tool to be executed.  This name and any other
     file names present on the command line or used during the tool
     execution are interpreted in the encapsulated file system environment
     given by the 'fsroot' parameter.

     'fsroot' encapsulates the file system presented to the tool.  It
     corresponds to a Vesta binding that is to be used as the root of
     the file system seen by the tool.  An attempt by the tool to
     read the file named /a/b/c is translated to an evaluator reference to
     fsroot/a/b/c.  Thus, "a" and "b" should be bindings, and "c" should
     be a file.  'wd' is the path relative to 'fsroot' that is to be used
     as the working directory for the tool.
    
     The environment variables visible to the tool during its execution
     are specified by 'ev', which is a pointer to a vector of null-terminated
     strings.  Each string is of the form "var=value" (without the quotes),
     where "var" is the environment variable name and "value" is its
     associated value.  By default, no environment variables are defined.
    
     report_out and value_out are streams to which the output on
     stdout is to be written.  If either is NULL, it is not used.
     Writing on report_out requires holding mu.

     report_err and value_err are streams to which the output on
     stderr is to be written.  If either is NULL, it is not used.
     Writing on report_err requires holding mu.
    
     label is a text that is prepended to every line of output to
     report_out and report_err.

     stdin_name is the full path name of a file to be used as the input
     stream for the tool.  This file is expected to reside in the Vesta
     repository.  If the path name is empty (default), the tool's stdin
     stream is set to /dev/null.
    
     After tool execution completes, the results are returned as a value
     of type 'Tool_result', whose fields have the following meanings:
    
       'status' is the exit status returned by the tool.  It is meaningful
       only if 'sigval' is 0.
    
       'sigval' identifies the signal, if any, that terminated the execution
       of the tool.  If the tool execution ended voluntarily, this value is
       0 (and 'status' is meaningful).
    
       stdout_written (resp. stderr_written) is true iff one or more bytes
       were written on stdout_fd (resp. stderr_fd).
    
     Any files written to the file system are reflected in the 'fsroot'
     directory and may be discovered by the caller upon return from this
     function (using fsroot.list). */

  struct Host_info {
    /* Unique id for this server instance */
    FP::Tag uniqueid;
    /* Information about the host platform */
    Text sysname;   // Operating system implementation (uname -s)
    Text release;   // Operating system release (uname -r)
    Text version;   // Operating system version within release (uname -v)
    Text machine;   // Type of machine (uname -m)
    int cpus;       // Number of cpus
    int cpuMHz;     // Clock rate of cpu(s) in MHz
    int memKB;      // Physical memory in kilobytes
    /* Information about currently running tools */
    int max_tools;  // Maximum number of concurrent tools allowed
    int cur_tools;  // Number of tools currently running
    float load;     // Current load average on the machine
    int cur_pending;// Number of tools currently in the pending queue
    int max_pending;// Maximum number of tools allowed in the pending queue
  };

  static void get_info(const Text &host,  // hostname or hostname:port
		       /*OUT*/ Host_info& hinfo) throw(SRPC::failure);
  /* Get information about the host platform and how busy the server
     currently is, useful for the client in deciding whether to run a
     tool on this host.  Of course, cur_tools and max_tools might change
     between calls to get_info and do_it, so the client cannot be
     certain that a subsequent call to do_it will not block or throw
     SRPC::failure. */

private:

  // static data; filled in by "init"
  static pthread_once_t init_block;
  static MultiSRPC *srpc_cache;

  // initialization
  static void init() throw(SRPC::failure);
  static void ensure_init() throw(SRPC::failure);

  // private member functions
  static void send_relay_socket(Tool_Relay &r, SRPC *srpc);
  static void send_vdir(LongId &fsroot, SRPC *srpc);

  friend void RunTool_init();
};

#endif  /* _RUN_TOOL_CLIENT_H */
