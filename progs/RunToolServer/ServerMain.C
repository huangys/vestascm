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
#include <BufStream.H>
#include <OS.H>
#include <VestaConfig.H>
#include <signal.h>
#include <sys/resource.h>

// SRPC stuff
#include <LimService.H>

// Repository stuff
#include <SourceOrDerived.H>

#include "RunToolClient.H"
#include "RunToolDaemon.H"
extern "C" {
#include "get_load.h"
}

using std::cerr;
using std::endl;
using Basics::OBufStream;

//  ********************
//  *  Initialization  *
//  ********************

void init_repository_access()
{
  char *root = SourceOrDerived::getMetadataRootLocalName();
  if (chdir(root) == SYSERROR) {
    Text msg = Basics::errno_Text(errno);
    cerr << "Can't chdir(" << root << ") because: " << msg << endl;
    exit(1);
  }
  delete[] root;
  SourceOrDerived::setMetadataRootLocalName("");
}


//  ***********************************
//  *  Tool driver support functions  *
//  ***********************************

static Text config_Text(const Text &key)
{
  return VestaConfig::get_Text(RUN_TOOL_CONFIG_SECTION, key);
}

static int config_int(const Text &key)
{
  return VestaConfig::get_int(RUN_TOOL_CONFIG_SECTION, key);
}

static int config_int(const Text &key, int default_value)
{
  Text tval;
  if(VestaConfig::get(RUN_TOOL_CONFIG_SECTION, key, tval))
    {
      return VestaConfig::get_int(RUN_TOOL_CONFIG_SECTION, key);
    }
  return default_value;
}

Text convert_failure(const SRPC::failure &f) {
  OBufStream s;
  s << "SRPC failure (";
  switch (f.r) {
  case SRPC::unknown_host:
    s << "unknown_host"; break;
  case SRPC::unknown_interface:
    s << "unknown_interface"; break;
  case SRPC::version_skew:
    s << "version_skew"; break;
  case SRPC::protocol_violation:
    s << "protocol_violation"; break;
  case SRPC::buffer_too_small:
    s << "buffer_too_small"; break;
  case SRPC::transport_failure:
    s << "transport_failure"; break;
  case SRPC::internal_trouble:
    s << "internal_trouble"; break;
  case SRPC::invalid_parameter:
    s << "invalid_parameter"; break;
  case SRPC::partner_went_away:
    s << "partner_went_away"; break;
  case SRPC::not_implemented:
    s << "not_implemented"; break;
  default:
    s << f.r; break;
  }
  s << "): " << f.msg.chars();
  Text t(s.str());
  return t;
}

class RunToolServerHandler : public LimService::Handler
{
public:
  RunToolServerHandler() { }

  void call(SRPC *srpc, int intf_ver, int proc_id) throw(SRPC::failure)
  {
    RunToolServer(srpc, intf_ver, proc_id);
  }
  void call_failure(SRPC *srpc, const SRPC::failure &f) throw();
  void accept_failure(const SRPC::failure &f) throw()
  {
    cerr << "RunToolServer: failure accepting new connection: "
	 << convert_failure(f).chars()
	 << " (server continuing)" << endl;
  }
  void other_failure(const char *msg) throw()
  {
    cerr << "RunToolServer: " << msg
	 << " (server continuing)" << endl;
  }
  void listener_terminated() throw()
  {
    cerr << ("RunToolServer: Fatal error: unable to accept new "
	     "connections; exiting") << endl;
    abort();
  }
};

void RunToolServerHandler::call_failure(SRPC *srpc, const SRPC::failure &f)
  throw()
{
    // called when a failure occurs during server execution
    if (f.r == SRPC::partner_went_away) {
	// Normal; do not print anything
	return;
    }
    cerr << "RunToolServer: " << convert_failure(f).chars()
	 << " (server continuing)" << endl;
}

// Exit cleanly on SIGINT or SIGTERM
#if defined(__linux__)
// On Linux, we need to remember which is the main thread.
static pthread_t g_main_thread = pthread_self();
#endif
extern "C" void
SigHandler(int sig)
{
#if defined(__linux__)
  // The Linux pthreads implementation uses one process pre thread.
  // The main thread will recieve the signal from a ^C and then
  // proceed to call exit below.  This will in turn kill off the other
  // threads, delivering each of them a signal which will in turn call
  // this handler again.  If any of those threads then call exit, the
  // pthreads library seems to become confused and crashes.  For that
  // reason, we don't call exit unless we're the main thread.
  if(pthread_self() != g_main_thread)
    {
      return;
    }
#endif

  RunToolServerCleanup();
  exit(3);
}

//  ***************************
//  *  Tool driver main loop  *
//  ***************************

int main(int argc, char *argv[])
{
  // Are we exiting because of a fatal error?
  bool l_error = false;

  InitLoadPoint(); // initialization of the load average reading lib
  try
    {
      int maxTools   = config_int("server_max_threads");
      int maxPending = config_int("server_max_pending");
      // adjust the core dump size - setting coredumpsize_limit
      // negative gets the maximum
      if(VestaConfig::is_set(RUN_TOOL_CONFIG_SECTION, "coredumpsize_limit"))
	{
	  int coredumpsize = config_int("coredumpsize_limit");
	  struct rlimit r;
	  int err = getrlimit(RLIMIT_CORE, &r);
	  if(err != 0)
	    {
	      int errno_save = errno;
	      cerr << "Error getting core dump size limit: "
		   << Basics::errno_Text(errno_save)
		   << "(errno = " << errno_save << ")" << endl;
	    }
	  if(r.rlim_max > coredumpsize && coredumpsize >= 0)
	    r.rlim_cur = coredumpsize;
	  else
	    r.rlim_cur = r.rlim_max;
	  err = setrlimit(RLIMIT_CORE, &r);
	  if(err != 0)
	    {
	      int errno_save = errno;
	      cerr << "Error setting core dump size limit:  "
		   << Basics::errno_Text(errno_save)
		   << "(errno = " << errno_save << ")" << endl;
	    }
	}

      init_repository_access();
      RunToolServerInit(maxTools, maxPending);
      // The maximum number of threads allowed to run from
      // LimService's perspective is the maximum number of running
      // tools, plus the maximum number pending, plus one more to make
      // sure we can always service "get_info" queries.
      int maxRunning = maxTools + maxPending + 1;
      signal(SIGINT, SigHandler);
      signal(SIGTERM, SigHandler);

      // Perhaps command line arguments (currently ignored) should
      // someday be accepted to override selected information?

      RunToolServerHandler handler;
      LimService *ls =
	NEW_CONSTR(LimService,
		   (/* intfName */    config_Text("SRPC_port"),
		    /* maxRunning */  maxRunning,
		    /* handler */     handler));
      try
	{
	  ls->Run();
	}
      catch (SRPC::failure f) {
	cerr << convert_failure(f).chars() << '\n';
	if (f.r == SRPC::transport_failure &&
	    f.msg.FindText("already in use") != -1)
	  {
	    cerr << "Is a RunToolServer already running on this machine?"
		 << endl;
	  }
	l_error = true;
      }

      delete ls;
      RunToolServerCleanup();
    }
  catch(VestaConfig::failure f)
    {
      cerr << argv[0] << ": Configuration error: " << f.msg.cchars() << endl;
      l_error = true;
    }

  return (l_error ? 1 : 0);
}

