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


#include <Basics.H>
#include <VestaConfig.H>

// SRPC stuff
#include <TCP_sock.H>
    // for name_to_sockaddr

// Repository stuff
#include <VDirSurrogate.H>
    // for getNFSInfo

#include "RunToolClient.H"

#include <strings.h>
    // for bzero

using std::ostream;
using std::flush;

// Class global variables

pthread_once_t RunTool::init_block = PTHREAD_ONCE_INIT;

MultiSRPC *RunTool::srpc_cache = NULL;
Text default_port;

// By default, we give a RunToolServer a maximum of 2 minutes to
// respond to an inquiry for its status.  This keeps a client from
// getting stuck talking to a misbehaving RunToolServer (such as an
// overloaded or wedged system that fails to schedule the
// RunToolServer to respond).
static unsigned int get_info_read_timeout = 120;

//  ********************
//  *  Initialization  *
//  ********************

extern "C"
{
  void RunTool_init()
  {
    RunTool::init();
  }
}

void RunTool::ensure_init() throw (SRPC::failure)
{
  if (pthread_once(&init_block, RunTool_init) != 0)
    throw(SRPC::failure(implementation_error, "pthread_once failed!"));
}

void RunTool::init() throw(SRPC::failure)
{
  try {
    default_port = VestaConfig::get_Text(RUN_TOOL_CONFIG_SECTION,
					 "SRPC_port");
    Text info_timeout_t;
    if(VestaConfig::get(RUN_TOOL_CONFIG_SECTION, "info_read_timeout", info_timeout_t))
      {
	get_info_read_timeout = strtoul(info_timeout_t.cchars(), NULL, 0);
      }
    srpc_cache = NEW(MultiSRPC);
  } catch (VestaConfig::failure f) {
    throw(SRPC::failure(implementation_error, f.msg));
  }
}

//  ***************
//  *  Utilities  *
//  ***************


void RunTool::send_relay_socket(Tool_Relay &r, SRPC *srpc)
{
  sockaddr_in sock;
  r.get_sockaddr(sock);
  srpc->send_socket(sock);
}

void RunTool::send_vdir(LongId &fsroot, SRPC *srpc)
{
  // The server needs only the information required to establish a mount
  // point for the 'fsroot' directory.  This consists of a socket/port
  // and an NFS file handle understood by the Vesta repository, which it
  // calls a LongId.
  char buffer[100];      // ugh
  char *nfs_sock_and_port = buffer;
  LongId root, muRoot;        // unused here
  sockaddr_in sock;
  VDirSurrogate::getNFSInfo(nfs_sock_and_port, root, muRoot);
  
  TCP_sock::name_to_sockaddr(nfs_sock_and_port, sock);
  srpc->send_socket(sock);
  srpc->send_bytes((char *)&fsroot, sizeof(fsroot));
}


//  ********************
//  *  The main event  *
//  ********************


RunTool::Tool_result
RunTool::do_it(
		const Text &host,
		chars_seq *argv,
		LongId &fsroot,
		const Text &wd,
		chars_seq *ev,
		OS::ThreadIOHelper *report_out,
		ostream *value_out,
		OS::ThreadIOHelper *report_err,
		ostream *value_err,
		const Text &label,
		const Text &stdin_name
		) throw(SRPC::failure, FS::Failure)
{
  MultiSRPC::ConnId host_conn = -1;
  Tool_Relay out;
  Tool_Relay err;
  Tool_result res;
  SRPC *srpc;
  SRPC::failure f(0, "");

  bool write_failed = false; // Has there been a write failure?
  FS::Failure write_failure; // Saved write failure

  ensure_init();

  res.status = res.sigval = 0;
  res.stdout_written = res.stderr_written = false;

  try {
    int colon = host.FindChar(':');
    if (colon == -1) {
      // Host name only
      host_conn = srpc_cache->Start(host, default_port, srpc);
    } else {
      Text host_only = host.Sub(0, colon);
      Text port = host.Sub(colon + 1);
      host_conn = srpc_cache->Start(host_only, port, srpc);
    }
    
    out.setup(report_out, value_out, label);
    err.setup(report_err, value_err, label);

    // Establish call
    srpc->start_call(RUN_TOOL_DOIT, RUN_TOOL_INTERFACE_VERSION);

    // Send arguments
    srpc->send_Text(stdin_name);
    send_relay_socket(out, srpc);
    send_relay_socket(err, srpc);
    send_vdir(fsroot, srpc);
    srpc->send_Text(wd);
    if (ev == NULL) {
      ev = NEW(chars_seq);
      srpc->send_chars_seq(*ev);
      delete ev;
    } else
      srpc->send_chars_seq(*ev);
    srpc->send_chars_seq(*argv);
    srpc->send_end();

    // Receive results
    res.status = srpc->recv_int();
    res.sigval = srpc->recv_int();
    res.dumped_core = srpc->recv_bool();
    srpc->recv_end();
  } catch(SRPC::failure ff) {
    // srpc is no longer usable; MultiSRPC will deal with it.
    f = ff;
  } catch(TCP_sock::failure tf) {
    // happens only on 'setup' and 'send_vdir' calls
    f = SRPC::convert_TCP_failure(tf);
  }

  try {
    res.stdout_written = out.finish(f.r == 0);
  } catch (TCP_sock::failure tf) {
    if (f.r == 0) f = SRPC::convert_TCP_failure(tf);
  } catch (FS::Failure f) {
    // Write failure (e.g. disk full writing value stream)
    if(!write_failed)
      {
	// Note that we only save the first write failure.
	write_failure = f;
	write_failed = true;
      }
  }
  try {
    res.stderr_written = err.finish(f.r == 0);
  } catch (TCP_sock::failure tf) {
    if (f.r == 0) f = SRPC::convert_TCP_failure(tf);
  } catch (FS::Failure f) {
    // Write failure (e.g. disk full writing value stream)
    if(!write_failed)
      {
	// Note that we only save the first write failure.
	write_failure = f;
	write_failed = true;
      }
  }
  srpc = NULL;                    // cleanliness
  srpc_cache->End(host_conn);
  if (f.r != 0) throw f;
  if(write_failed) throw write_failure;

  return res;
}

void 
RunTool::get_info(const Text &host,
		  /*OUT*/ Host_info& hinfo) throw(SRPC::failure)
{
  MultiSRPC::ConnId host_conn = -1;
  SRPC* srpc;
  ensure_init();

  try {
    int colon = host.FindChar(':');
    if (colon == -1) {
      // Host name only
      host_conn = srpc_cache->Start(host, default_port, srpc);
    } else {
      Text host_only = host.Sub(0, colon);
      Text port = host.Sub(colon + 1);
      host_conn = srpc_cache->Start(host_only, port, srpc);
    }

    // Avoid RunToolServer "tar pits" by setting a limit on how long
    // we'll wait for a response when trying to get information.
    srpc->enable_read_timeout(get_info_read_timeout);

    // Establish call
    srpc->start_call(RUN_TOOL_INFO, RUN_TOOL_INTERFACE_VERSION);

    // No arguments
    srpc->send_end();

    // Receive results
    srpc->recv_Text(hinfo.sysname);
    srpc->recv_Text(hinfo.release);
    srpc->recv_Text(hinfo.version);
    srpc->recv_Text(hinfo.machine);
    hinfo.cpus = srpc->recv_int();
    hinfo.cpuMHz = srpc->recv_int();
    hinfo.memKB = srpc->recv_int();
    hinfo.max_tools = srpc->recv_int();
    hinfo.cur_tools = srpc->recv_int();
    hinfo.load = srpc->recv_float();
    hinfo.cur_pending = srpc->recv_int();
    hinfo.max_pending = srpc->recv_int();
    hinfo.uniqueid.Recv(*srpc);
    srpc->recv_end();

    // Turn the read tiemout back off, as we don't want to put a bound
    // on the time until the RunToolServer sends us the result of a
    // tool invocation.
    srpc->disable_read_timeout();

  } catch(SRPC::failure f) {
    // srpc is no longer usable; MultiSRPC will deal with it.
    srpc_cache->End(host_conn);
    throw;
  }
  srpc_cache->End(host_conn);
}


// =====================================================================

//  *******************************
//  *  Tool_Relay implementation  *
//  *******************************

Tool_Relay::Tool_Relay():tcp_f(0, ""), label("")
{
  listener = NULL;
  s = NULL;
  buffer = NULL;
  report = NULL;
  value = NULL;
  buff_len = 0;
  remaining = 0;
  write_failed = false;
}

Tool_Relay::~Tool_Relay()
{
  if (buffer != NULL) delete[] buffer;
  if (s != NULL) delete s;
  if (listener != NULL) delete listener;
}

void *relay_thread(void *arg)
{
  Tool_Relay *r = (Tool_Relay *)arg;
  r->body();
  return NULL;
}

void Tool_Relay::body()
{
  try {
    TCP_sock *ss = listener->wait_for_connect();
    ss->enable_alerts();
    m.lock();
    // Prevent race between body() and finish()
    s = ss;  // finish will now alert s instead of listener
    bool quit = listener->test_alert();
    m.unlock();
    if (quit) {
	TCP_sock::alerted a;
	throw a;
    }
    while (true) {  // loop exits with TCP_sock::alerted exception
      // Read data and write to out_fd
      if (buff_len - remaining < RELAY_BUFFER_SIZE/2) {
	// Too much stuff left over from last read; grow buffer
	buff_len *= 2;
	char* newbuff = NEW_PTRFREE_ARRAY(char, buff_len);
	memcpy(newbuff, buffer, remaining);
	delete[] buffer;
	buffer = newbuff;
      }
      int len = s->recv_data(buffer + remaining, buff_len - remaining);
      if (value) {
	FS::Write(*value, buffer + remaining, len);
      }
      if (report) {
	std::ostream& stream = report->start();
	if (label == "") {
	  // Single-threaded case, no labeling or locking
	  stream.write(buffer + remaining, len);
	} 
	else {
	  // Multi-threaded case.  Label each line, and don't
	  // output a partial line.
	  char* pos = buffer;
	  char* end;
	  remaining += len;
	  while (remaining) {
	    end = (char *) memchr(pos, '\n', remaining);
	    if (end == NULL) {
	      if (buffer != pos) memmove(buffer, pos, remaining);
	      break;
	    } 
	    else {
	      stream << label;
	      end++;
	      int linelen = end - pos;
	      stream.write(pos, linelen);
	      remaining -= linelen;
	      pos = end;
	    }
	  }
	  stream << flush;
	}
	report->end();
      }
      written = true;
    }
  } catch(TCP_sock::failure f) {
    if (f.reason != TCP_sock::end_of_file) tcp_f = f;
  } catch (TCP_sock::alerted) {
    /* forced termination */
  } catch (FS::Failure f) {
    // Write failure (e.g. disk full writing value stream)
    if(!write_failed)
      {
	// Note that we only save the first write failure.
	write_failure = f;
	write_failed = true;
      }
  }

  // Final cleanup
  if (report) {
    if (label == "") {
      // Single-threaded case; we haven't been flushing
      report->start() << flush;
      report->end();
    } 
    else {
      // Multi-threaded case; each full line was flushed
      if (remaining) {
	// Output remaining partial line, adding a \n for neatness
	std::ostream& stream = report->start();
	stream << label;
	stream.write(buffer, remaining);
	stream << '\n' << flush;
	report->end();
      }
    }
  }
}

void Tool_Relay::setup(OS::ThreadIOHelper* report, ostream* value, 
		       const Text& label)
     throw (TCP_sock::failure)
{
  if (report == NULL && value == NULL) {
      // no relay needed; server will get socket address of
      // zero (see get_sockaddr).
      return;
  }
  listener = NEW(TCP_sock);
  listener->enable_alerts();
  // Make sure that we're listening in case the RunToolServer contacts
  // us before the relay thread starts.
  listener->set_waiters();
  this->report = report;
  this->value = value;
  this->label = label;
  buff_len = RELAY_BUFFER_SIZE;
  buffer = NEW_PTRFREE_ARRAY(char, buff_len);
  written = false;
  relay.fork(&relay_thread, (void *)this);
}

bool Tool_Relay::finish(bool wait) throw(TCP_sock::failure, FS::Failure)
{
  if (listener == NULL) {
      // 'setup' was never called, or fd == NO_FD
      return false;
  }
  if (!wait) {
      // The relay thread might be waiting either to establish the connection
      // or for data to arrive.  In the former case, it is waiting on the
      // listener socket, and the regular socket is NULL.  In the latter case,
      // it is waiting on the regular socket.
      m.lock();
      (s == NULL ? listener : s)->alert();
      m.unlock();
  }
  (void) relay.join();
  if (tcp_f.reason != 0) throw tcp_f;
  if(write_failed) throw write_failure;
  return written;
}

void Tool_Relay::get_sockaddr(sockaddr_in &sock)
{
  if (listener == NULL) bzero((char *)&sock, sizeof(sock));
  else if (s == NULL) listener->get_local_addr(sock);
  else s->get_local_addr(sock);
}
