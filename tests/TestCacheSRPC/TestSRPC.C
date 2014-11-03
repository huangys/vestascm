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


/* This program is a simple test of the "LimService" and "MultiSRPC"
   interfaces. It implements both the client and server applications: to run
   the client, use the "-client" switch; to run the server, use the "-server"
   switch.

   The syntax for the server application is:

     TestSRPC -server [ -wait ] [ maxRunning ]

   Any omitted argument defaults to a large integer. The "maxRunning"
   argument specifies the maximum number of server threads allowed to
   run at once.  If the "-wait" switch is supplied, then random delays
   are introduced to simulate delays in the server.

   The syntax for the client application is:

     TestSRPC -client [-wait] [-threads n] [-host hostname]

   If the "-wait" switch is supplied, then random delays are introduced to
   simulate delays in the client. If "-threads" is specified, then "n" client
   threads are run; by default, only 1 thread is run. The threads are numbered
   consecutively starting from 1. "hostname" is the name of the server machine
   to connect to; it defaults to the name of the host on which the client
   program is being run. */

#include <stdlib.h>
#if defined(__digital__)
#include <standards.h>
#endif
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <stdio.h>
#include <Basics.H>
#include <TCP_sock.H>
#include <SRPC.H>
#include <LimService.H>
#include <MultiSRPC.H>
#include "Debug.H"

using std::ostream;
using std::endl;
using std::cerr;
using std::flush;
using OS::cio;

const Text ClientSwitch = "-client";
const Text ServerSwitch = "-server";
char* const IntfName = "9874";
int const IntfVersion = 1;

int const FirstProcId = 1;
int const LastProcId = 2;

// pause duration factor (in us)
static const int FactorMicroSecs = 1000; // 1 ms
static const int USecsPerSec = 1000000;

static void Pause(int num) throw ()
{
    int total = num * FactorMicroSecs;
    int secs = total / USecsPerSec;
    int usecs = total % USecsPerSec;
    Basics::thread::pause(secs, usecs);
}

void WriteFailure(const SRPC::failure &f, int threadId = -1) throw ()
{
    ostream& out_stream = cio().start_out();
    out_stream << Debug::Timestamp() << "SRPC Failure";
    if (threadId != -1) out_stream << ", thread " << threadId;
    out_stream << "\n  " << f.msg << " (code = " << f.r << ")\n\n";
    out_stream << flush;
    cio().end_out();
}

void WriteIdInfo(SRPC *srpc, ostream& out) throw (SRPC::failure)
{
    Text local(srpc->local_socket());
    Text remote(srpc->remote_socket());
    out << "  Local socket =  " << local << "\n";
    out << "  Remote socket = " << remote << "\n";
}

// Introduce delays in server?
bool sWait = false;

class ServerHandler : public LimService::Handler
{
public:
  ServerHandler() { }

  void call(SRPC *srpc, int intf_ver, int proc_id) throw(SRPC::failure);

  void call_failure(SRPC *srpc, const SRPC::failure &f) throw()
  {
    // Ignore when the client closes the connection
    if (f.r == SRPC::partner_went_away) return;

    WriteFailure(f);
  }
  void accept_failure(const SRPC::failure &f) throw()
  {
    WriteFailure(f);
  }
  void other_failure(const char *msg) throw()
  {
    cio().start_out()
      << Debug::Timestamp() << msg << endl;
    cio().end_out();
  }
  void listener_terminated() throw()
  {
    cio().start_out()
      << Debug::Timestamp()
      << "Fatal error: unable to accept new connections; exiting"
      << endl;
    cio().end_out();
    abort();
  }
};

void ServerHandler::call(SRPC *srpc, int intf_ver, int procId)
  throw (SRPC::failure)
{
    int tid, arg, res, waitTime;
    
    // receive integer
    try {
	// receive
	tid = srpc->recv_int();
	arg = srpc->recv_int();
	srpc->recv_end();

	// compute result
	switch (procId) {
	  case 1: res = arg + 2;
	  case 2: res = arg * 2;
	}
	if (sWait) { waitTime = Debug::MyRand(arg, res); }

	// write log
	ostream& out_stream = cio().start_out();
	out_stream << Debug::Timestamp() << "Server CALLED -- Client thread "
          << tid << ":\n";
	out_stream << "  Procedure ID = " << procId << "\n";
        out_stream << "  Received value = " << arg << "\n";
	WriteIdInfo(srpc, out_stream);
	if (sWait) { out_stream << "  Waiting " << waitTime << " seconds\n"; }
	out_stream << endl;
	out_stream << flush;
	cio().end_out();

	// pause
	if (sWait) {
	    Pause(waitTime);
	    // (void)sleep((unsigned int)waitTime);
	}

	// send result
	ostream& out = cio().start_out();
	out << Debug::Timestamp() 
	    << "Server RESULT -- Client thread " << tid << endl;
	out << "  Result value = " << res << endl;
	WriteIdInfo(srpc, out); out << endl;
	cio().end_out();

	srpc->send_int(res);
	srpc->send_end();
    } catch (SRPC::failure f) {
	WriteFailure(f);
	throw;
    }
}

void ExitServer() throw ()
{
  cerr << "SYNTAX: server [-wait] [maxRunning]" << endl;
  exit(1);
}

void Server(int argc, char *argv[]) throw ()
{
    int maxRunning;
    int arg = 2; // skip over "-server" arg

    // parse arguments
    if (argc > 4) {
      cerr << "Error: Too many arguments" << endl;
      ExitServer();
    }
    maxRunning = 20;
    if (arg < argc && !strcmp(argv[arg], "-wait")) { sWait = true; arg++; }
    if (arg < argc) { sscanf(argv[arg], "%d", &maxRunning); arg++; }

    // start server
    ostream& out_stream = cio().start_out();
    out_stream << "Starting server:" << endl;
    out_stream << "  Exporting '" << IntfName << "', version " 
	       << IntfVersion << endl;
    out_stream << "  Maximum number of running threads = " << maxRunning
	       << endl << endl;
    cio().end_out();

    ServerHandler handler;
    LimService ls(IntfName, IntfVersion, maxRunning,
		  handler);
    try {
	ls.Run();
    } catch (SRPC::failure f) {
	WriteFailure(f);
    }
}

void ExitClient() throw ()
{
  cerr << "SYNTAX: client " ;
  cerr << "[-wait] [-threads n] [-base baseThreadNum] [-host hostname]" 
       << endl;
  exit(1);
}

struct ClientArgs {
    char *hostname;
    MultiSRPC *conns;
    int id;			// thread id
    unsigned int waitTime;	// initial wait time; 0 means don't wait
    bool cWait;			// should this client thread wait?
};

void *MainClientProc(void *ptr) throw ()
{
    ClientArgs *args = (ClientArgs *)ptr;
    SRPC *srpc;
    MultiSRPC::ConnId id;
    int proc, arg, res, waitTime;

    // ignore SIGPIPE
    struct sigaction dummy;
    struct sigaction action;
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    (void)sigaction(SIGPIPE, &action, &dummy);

    ostream& out_stream = cio().start_out();
    out_stream << Debug::Timestamp() << "Starting client thread "
	       << args->id << endl;
    if (args->waitTime > 0) {
      out_stream << "  Waiting " << args->waitTime << " second(s)" << endl;
    } else {
      out_stream << "  No waiting" << endl;
    }
    out_stream << endl;
    cio().end_out();
    
    if (args->waitTime > 0) {
	Pause(args->waitTime);
	// (void)sleep(args->waitTime);
    }

    try {
	while (true) {
	    id = args->conns->Start(args->hostname, IntfName, srpc);
	    proc = Debug::MyRand(FirstProcId, LastProcId);
	    arg = Debug::MyRand(1, 4);
	    
	    ostream& out_stream = cio().start_out();
	    out_stream << Debug::Timestamp() << "Client thread " << args->id << endl;
	    out_stream << "  Calling procedure " << proc << endl;
	    out_stream << "  Argument = " << arg << endl;
	    WriteIdInfo(srpc, out_stream); 
	    out_stream << endl;
	    cio().end_out();
	    
	    srpc->start_call(proc, IntfVersion);
	    srpc->send_int(args->id);
	    srpc->send_int(arg);
	    srpc->send_end();

	    res = srpc->recv_int();
	    srpc->recv_end();
	    if (args->cWait) { waitTime = Debug::MyRand(arg, res); }

	    ostream& out = cio().start_out();
	    out << Debug::Timestamp() << "Client thread " << args->id << endl;
	    out << "  Received result = " << res << endl;
	    WriteIdInfo(srpc, out);
	    if (args->cWait) {
	      out << "  Waiting " << waitTime << " seconds" << endl;
	    }
	    out << endl;
	    cio().end_out();

	    args->conns->End(id);
	    if (args->cWait) {
		Pause(waitTime);
		// (void)sleep((unsigned int)waitTime);
	    }
	}
    } catch (SRPC::failure f) {
	WriteFailure(f, args->id);
    }

    cio().start_out() << Debug::Timestamp() << "Exiting client thread "
		      << args->id << endl << endl;
    cio().end_out();
    return (void *)NULL;
}

void *ClientProc(void *ptr) throw ()
/* This procedure simply repeatly calls "MainClientProc" in a new thread. When
   that thread dies, it forks another thread and call the procedure again. */
{
    Basics::thread th;
    ClientArgs *args = (ClientArgs *)ptr;

    while (true) {
	th.fork(MainClientProc, ptr);
	(void)th.join();
	if (args->cWait) {
	    args->waitTime = (unsigned int)Debug::MyRand(5, 10);
	}
    }
    //return (void *)NULL; // not reached
}

void Client(int argc, char *argv[]) throw ()
{
    int numThreads = 1;
    char *hostname = (char *)NULL;
    MultiSRPC *conns;
    bool cWait = false;

    // parse arguments
    if (argc < 2 || argc > 7) {
      cerr << "Error: Incorrect number of arguments" << endl;
      ExitClient();
    }
    for (int arg = 2; arg < argc; arg ++) {
	if (strcmp(argv[arg], "-wait") == 0) {
	    cWait = true;
	} else if (strcmp(argv[arg], "-threads") == 0) {
	    if (sscanf(argv[arg+1], "%d", &numThreads) < 1 || numThreads < 1) {
		cerr << "Error: -threads argument not a positive integer" << endl;
		ExitClient();
	    }
	    arg++;
	} else if (strcmp(argv[arg], "-host") == 0) {
	    hostname = argv[arg+1];
	    arg++;
	} else {
   	    cerr << "Error: argument " << arg << " is bad" << endl;
	    ExitClient();
	}
    }
    if (hostname == (char *)NULL) {
	char buff[MAXHOSTNAMELEN];
	if (gethostname(buff, MAXHOSTNAMELEN) < 0) {
 	    cerr << "Error: gethostname() failed" << endl;
	    exit(2);
	}
	hostname = NEW_PTRFREE_ARRAY(char, strlen(buff) + 1);
	strcpy(hostname, buff);
    }
    
    // start client
    ostream& out_stream = cio().start_out();
    out_stream << "Starting client:" << endl;
    out_stream << "  Server host '" << hostname << "'" << endl;
    out_stream << "  Interface '" << IntfName << "', version "
	       << IntfVersion << endl << endl;
    cio().end_out();

    // initialize connections
    conns = NEW(MultiSRPC);

    // fork threads
    ClientArgs *args;
    Basics::thread th;
    cio().start_out() << Debug::Timestamp() << "Forking " << numThreads 
		      << " thread(s)" << endl << endl;
    cio().end_out();
    for (int i = 0; i < numThreads; i++) {
	args = NEW(ClientArgs);
	args->hostname = hostname;
	args->conns = conns;
	args->id = i + 1;
	args->waitTime = 0;
	args->cWait = cWait;
	th.fork_and_detach(ClientProc, (void *)args);
    }

    // block indefinitely
    cio().start_out() << Debug::Timestamp()
		      << "Main client thread now blocking forever..." << endl << endl;
    cio().end_out();
    Debug::BlockForever();
}

int main(int argc, char *argv[])
{
    // process command-line
    if (argc > 1 && argv[1] == ServerSwitch) {
	Server(argc, argv);
    } else if (argc > 1 && argv[1] == ClientSwitch) {
	Client(argc, argv);
    } else {
        cerr << "You must specify " << ServerSwitch << " or "
	     << ClientSwitch << " as the first argument." << endl;
        exit(3);
    }
    return(0);
}
