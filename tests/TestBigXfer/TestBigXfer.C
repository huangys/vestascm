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


/* This program tests large data transfers using SRPC. The same program is
   used for both the server and client; the first command-line switch must
   be either "-client" or "-server". Both the client and server programs
   should be run on the same machine.

   The program sends increasingly larger buffers from the client to the server
   over a succession of rounds. The size of the buffers send in round "i" is
   "2^i". The program stops after the 20th round (when the buffer size is
   1MB). 

   Each round is a collection of iterations, where an iteration consists of
   sending a single buffer of the size appropraite for that round. By default,
   there are 10 iterations per round. However, when invoking the client, you
   can specify a different number of iterations per round using the "-iters"
   switch. The syntax of the client and server commands lines are:

     Client: TestBigXfer -client [ -iters n ] [ serverhost ]
     Server: TestBigXfer -server

   If an SRPC failure occurs at the client, the program prints an error
   message and immediately exits. When an SRPC failure occurs on the server,
   an error message is printed, but the server continues listening for new
   requests. */

#include <Basics.H>
#include <BufStream.H>
#include <SRPC.H>
#include <MultiSRPC.H>
#include <LimService.H>

using std::ostream;
using std::endl;
using std::cout;
using std::cerr;
using Basics::IBufStream;

// SRPC constants
const Text IntfName("1234");
const int  IntfVersion = 1;
const int  ProcId1 = 1;

// protects writing to "cout"
static Basics::mutex mu;

// transfer constants
const unsigned FirstSize = 1U << 1;
const unsigned LastSize  = 1U << 20;
const int NumIterations  = 8;

void SyntaxError(const char *msg) throw ()
{
    cerr << "Error: " << msg << '\n';
    cerr << "SYNTAX: TestBigXfer [ -client | -server ] [ -iters n ]\n";
    exit(1);
}

ostream& operator << (ostream &os, const SRPC::failure &f) throw ()
{
    mu.lock();
    int i;
    for (i = 0; i < 70; i++) os << '*'; os << '\n';
    os << "SRPC failure (" << f.r << ") :\n";
    os << "  " << f.msg << '\n';
    for (i = 0; i < 70; i++) os << '*'; os << "\n\n";
    os.flush();
    mu.unlock();
    return os;
}

void ClientCall(SRPC *srpc, int numIterations, unsigned len)
  throw (SRPC::failure)
{
    mu.lock();
    cout << "Calling Proc1\n";
    cout << "  len = " << len << '\n';
    cout << "  buffs = " << numIterations << '\n';
    cout << "  total = " << numIterations * len << '\n';
    (cout << '\n').flush();
    mu.unlock();

    char* buff = NEW_PTRFREE_ARRAY(char, len);
    srpc->start_call(ProcId1, IntfVersion);
    srpc->send_int(numIterations);
    for (int i = 0; i < numIterations; i++) {
	srpc->send_bytes(buff, len);
    }
    srpc->send_end();
    unsigned res = srpc->recv_int();
    assert(res == (len * numIterations));
    srpc->recv_end();
    delete buff;

    mu.lock();
    cout << "Returned Proc1\n";
    (cout << '\n').flush();
    mu.unlock();
}

void Client(int numIterations, const char *server) throw ()
{
    MultiSRPC multi;
    try {
	for (unsigned len = FirstSize; len <= LastSize; len <<= 1) {
	    // establish SRPC connection
	    SRPC *srpc;
	    MultiSRPC::ConnId connId;
	    connId = multi.Start(server, IntfName, /*OUT*/ srpc);

	    // client call
	    ClientCall(srpc, numIterations, len);

	    // close connection
	    multi.End(connId);
	}
    } catch (SRPC::failure f) {
	cout << f;
    }
}

void Proc1(SRPC *srpc) throw (SRPC::failure)
{
    int len, total = 0;
    char *buff;

    int numIterations = srpc->recv_int();
    for (int i = 0; i < numIterations; i++) {
	buff = srpc->recv_bytes(/*OUT*/ len);
	delete buff;
	total += len;
    }
    srpc->recv_end();

    mu.lock();
    cout << "Called Proc1\n";
    cout << "  len = " << len << '\n';
    cout << "  buffs = " << numIterations << '\n';
    cout << "  total = " << total << '\n';
    (cout << '\n').flush();
    mu.unlock();

    srpc->send_int(total);
    srpc->send_end();

    mu.lock();
    cout << "Returned Proc1\n";
    (cout << '\n').flush();
    mu.unlock();
}

class ServerHandler : public LimService::Handler
{
public:
  ServerHandler() { }

  void call(SRPC *srpc, int intfVersion, int procId) throw(SRPC::failure)
  {
    switch (procId) {
      case ProcId1: Proc1(srpc); break;
      default: assert(false);
    }
  }

  void call_failure(SRPC *srpc, const SRPC::failure &f) throw()
  { cout << f << endl; }
  void accept_failure(const SRPC::failure &f) throw()
  { cout << f << endl; }
  void other_failure(const char *msg) throw()
  { cout << msg << endl; }
  void listener_terminated() throw()
  {
    cout << "Fatal error: unable to accept new connections; exiting" << endl;
    abort();
  }
};

void Server() throw (SRPC::failure)
{
    // start server
    const int MaxRunning = 10;
    ServerHandler handler;
    LimService ls(IntfName, IntfVersion, MaxRunning, handler);
    Basics::thread th = ls.Forked_Run();

    // print ready msg
    mu.lock();
    (cout << "Server ready...\n\n").flush();
    mu.unlock();

    // wait on forked thread
    (void) th.join();
}

int main(int argc, char *argv[]) 
{
    enum Kind { NoKind, ClientKind, ServerKind };
    Kind kind = NoKind;
    int arg = 1;
    int numIterations = NumIterations;
    const char *server = 0;

    while (arg < argc) {
	if (strcmp(argv[arg], "-client") == 0) {
	    kind = ClientKind;
	} else if (strcmp(argv[arg], "-server") == 0) {
	    kind = ServerKind;
	} else if (strcmp(argv[arg], "-iters") == 0) {
	    if (++arg >= argc) SyntaxError("no argument for '-iters' switch");
	    IBufStream istr(argv[arg]);
	    istr >> numIterations;
	} else if (!server && (kind == ClientKind)) {
	  server = argv[arg];
	} else {
	  Text msg = "unrecognized switch: \"";
	  msg += argv[arg];
	  msg += "\"";
	  SyntaxError(msg.cchars());
	}
	arg++;
    }
    if(!server)
      {
	server = "localhost";
      }
    try
      {
	switch (kind) {
	case ClientKind: Client(numIterations, server); break;
	case ServerKind: Server(); break;
	default:
	  SyntaxError("you must specify '-client' or '-server'");
	  break;
	}
      }
    catch(SRPC::failure f)
      {
	cerr << f;
	return 1;
      }
    return 0;
}
