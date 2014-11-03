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

// Created on Fri Jun 13 10:57:17 PDT 1997 by heydon

/* This program is used to measure the performance of a null SRPC call. The
   same program is used for both the server and client; the first command-line
   switch must be either "-client" or "-server". When invoked as a client, the
   name of the host on which the server program is running may also be
   supplied.

   Here is the syntax of the client and server variants of the program:

   client: TestNullSRPC -client [ -blocks b ] [ -sz s ] [ -calls n ] [ host ]
   server: TestNullSRPC -server [ -blocks b ] [ -sz s ]

   By default, the client makes null SRPC calls on the server indefinitiely.
   If "-calls n" is specified, only "n" calls are made, and the client then
   exits. By default, the client tries to contact a server on the same host;
   this can be overridden by specifying an explicit "host" to contact.

   If "-blocks b" is specified on either the client or the server, then "b"
   data blocks are sent. For the client, these will be RPC arguments; for the
   server, they will be RPC results. By default, the blocks are 1K bytes. If
   "-sz s" is specfied, the blocks are "s" bytes long.

   The server continues to listen for connections indefinitely. If using etp
   to monitor the server, set the ETPFLAGS environment variable to the string
   "s2", and interrupt the server by typing "^C". This setting of ETPFLAGS
   causes the etp machinery to catch signal 2 (SIGINT) and flush the etp logs
   before exiting. */

#include <Basics.H>
#include <SRPC.H>
#include <MultiSRPC.H>
#include <LimService.H>

using std::ostream;
using std::endl;
using std::cout;
using std::cerr;

// SRPC constants
const Text IntfName("1235");
const int  IntfVersion = 1;
const int  ProcId1 = 1;

// protects writing to "cout"
static Basics::mutex mu;

typedef char *String;

ostream& operator << (ostream &os, const SRPC::failure &f) throw ()
/* Print a "SRPC::failure" object to "os" .*/
{
    mu.lock();
    int i;
    for (i = 0; i < 70; i++) os << '*'; os << endl;
    os << "SRPC failure (error " << f.r << ") :" << endl;
    os << "  " << f.msg << endl;
    for (i = 0; i < 70; i++) os << '*'; os << endl << endl;
    mu.unlock();
    return os;
}

// ------------------------------- Client -------------------------------------

void ClientCall(SRPC *srpc, int blocks, int sz, String *buff)
  throw (SRPC::failure)
{
    // null call
    srpc->start_call(ProcId1, IntfVersion);
    srpc->send_int(blocks);
    srpc->send_int(sz);
    if (blocks > 0) {
	for (int i = 0; i < blocks; i++) {
	    srpc->send_bytes(buff[i], sz);
	}
    }
    srpc->send_end();

    // null result
    int recv_blocks = srpc->recv_int();
    int recv_sz = srpc->recv_int();
    if (recv_blocks > 0) {
	assert(recv_blocks <= blocks);
	assert(recv_sz <= sz);
	for (int i = 0; i < recv_blocks; i++) {
	    int len = recv_sz;
	    srpc->recv_bytes_here(buff[i], /*INOUT*/ len);
	    assert(len == recv_sz);
	}
    }
    srpc->recv_end();
}

void Client(int blocks, int sz, int numCalls, char *hostname) throw ()
{
    MultiSRPC multi;
    try {
	// establish SRPC connection
	SRPC *srpc;
	MultiSRPC::ConnId connId;
	connId = multi.Start(hostname, IntfName, /*OUT*/ srpc);

	// make client calls
	String *buff = (String *)NULL;
	if (blocks > 0) {
	    buff = NEW_ARRAY(String, blocks);
	    for (int i=0; i < blocks; i++) {
	     	buff[i] = NEW_PTRFREE_ARRAY(char, sz);
		memset(buff[i], 0, sz);
	    }
	}
	for (int callCnt = 0; numCalls < 0 || callCnt < numCalls; callCnt++) {
	    ClientCall(srpc, blocks, sz, buff);
	}

	// close connection
	multi.End(connId);
    } catch (const SRPC::failure &f) {
	cout << f;
    }
}

// ------------------------------- Server -------------------------------------

class ServerArgs {
  public:
    int blocks;
    int sz;
    String *buff; // array of strings
    ServerArgs(int blocks, int sz) throw ();
};

ServerArgs::ServerArgs(int blocks, int sz) throw ()
: blocks(blocks), sz(sz), buff((String *)NULL)
{
    if (this->blocks > 0) {
        this->buff = NEW_ARRAY(String, this->blocks);
	for (int i = 0; i < this->blocks; i++) {
	    this->buff[i] = NEW_PTRFREE_ARRAY(char, this->sz);
	}
    }
}

void ServerWork(const ServerArgs *args) throw ()
{
    if (args->buff != (String *)NULL) {
	for (int i = 0; i < args->blocks; i++) {
	    memset(args->buff[i], 0, args->sz);
	}
    }
}

void ServerProc1(SRPC *srpc, const ServerArgs *args)
  throw (SRPC::failure)
{
    // null call
    int recv_blocks = srpc->recv_int();
    int recv_sz = srpc->recv_int();
    if (recv_blocks > 0) {
	assert(recv_blocks <= args->blocks);
	assert(recv_sz <= args->sz);
	for (int i = 0; i < recv_blocks; i++) {
	    int len = recv_sz;
	    srpc->recv_bytes_here(args->buff[i], /*INOUT*/ len);
	    assert(len == recv_sz);
	}
    }
    srpc->recv_end();

    // null work
    ServerWork(args);

    // null result
    srpc->send_int(args->blocks);
    srpc->send_int(args->sz);
    if (args->blocks > 0) {
	for (int i = 0; i < args->blocks; i++) {
	    srpc->send_bytes(args->buff[i], args->sz);
	}
    }
    srpc->send_end();
}

class ServerHandler : public LimService::Handler
{
private:
  ServerArgs *args;
public:
  ServerHandler(ServerArgs *args) : args(args) { }

  void call(SRPC *srpc, int intfVersion, int procId) throw(SRPC::failure)
  {
    switch (procId) {
      case ProcId1: ServerProc1(srpc, args); break;
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

void Server(int blocks, int sz) throw (SRPC::failure)
{
    // start server
    const int MaxRunning = 10;
    ServerArgs *args = NEW_CONSTR(ServerArgs, (blocks, sz));
    ServerHandler handler(args);
    LimService ls(IntfName, IntfVersion, MaxRunning, handler);
    Basics::thread th = ls.Forked_Run();

    // print ready msg
    mu.lock();
    cout << "Server ready..." << endl << endl;
    mu.unlock();

    // wait on forked thread
    (void) th.join();
}

// -------------------------------- Main --------------------------------------

void SyntaxError(char *msg, char *arg = (char *)NULL) throw ()
{
    cerr << "Error: " << msg;
    if (arg != (char *)NULL) cerr << ": '" << arg << "'";
    cerr << endl;
    cerr << "SYNTAX: TestNullSRPC -client "
      << " [ -blocks b ] [ -sz s ] [ -calls n ] [ host ]" << endl;
    cerr << "SYNTAX: TestNullSRPC -server "
      << " [ -blocks b ] [ -sz s ]" << endl;
    exit(1);
}

int main(int argc, char *argv[]) 
{
    enum Role { ClientRole, ServerRole };
    Role role;
    if (argc < 2) SyntaxError("too few arguments");
    if (strcmp(argv[1], "-client") == 0) {
	role = ClientRole;
    } else if (strcmp(argv[1], "-server") == 0) {
	role = ServerRole;
    } else {
	SyntaxError("first argument must be '-client' or '-server'");
    }

    // determine other arguments
    int blocks = 0, sz = 1024;
    int numCalls = -1;
    char *hostname = "localhost";
    int arg;
    for (arg = 2; arg < argc && *argv[arg] == '-'; arg++) {
	if (strcmp(argv[arg], "-blocks") == 0) {
	    if (++arg >= argc) SyntaxError("no argument for '-blocks'");
	    if (sscanf(argv[arg], "%d", &blocks) != 1)
		SyntaxError("illegal argument to '-blocks'", argv[arg]);
	} else if (strcmp(argv[arg], "-sz") == 0) {
	    if (++arg >= argc) SyntaxError("no argument for '-sz'");
	    if (sscanf(argv[arg], "%d", &sz) != 1)
		SyntaxError("illegal argument to '-sz'", argv[arg]);
	} else if (strcmp(argv[arg], "-calls") == 0) {
	    if (role == ServerRole)
		SyntaxError("'-calls' not understood by server");
	    if (++arg >= argc) SyntaxError("no argument for '-calls'");
	    if (sscanf(argv[arg], "%d", &numCalls) != 1)
		SyntaxError("illegal argument to '-calls'", argv[arg]);
	} else {
	    SyntaxError("unrecognized switch", argv[arg]);
	}
    }
    try
      {
	switch (role) {
	case ClientRole:
	  if (arg < argc) hostname = argv[arg++];
	  if (arg < argc) SyntaxError("too many arguments");
	  Client(blocks, sz, numCalls, hostname);
	  break;
	case ServerRole:
	  if (argc > arg) SyntaxError("too many arguments");
	  Server(blocks, sz);
	  break;
	default:
	  assert(false);
	}
      }
    catch(SRPC::failure f)
      {
	cerr << f;
	return 1;
      }
    return 0;
}
