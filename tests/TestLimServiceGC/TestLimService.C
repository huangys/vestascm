#include <Basics.H>
#include <Generics.H>
#include <SRPC.H>
#include <MultiSRPC.H>
#include <LimService.H>
#include <ThreadIO.H>

using std::ostream;
using std::endl;
using std::cout;
using std::cerr;
using OS::cio;
using Basics::OBufStream;

// SRPC constants
const Text IntfName("1235");
const int  IntfVersion = 1;
const int  ProcIdStart = 1;
const int  ProcIdMiddle = 2;
const int  ProcIdEnd = 3;

ostream& operator << (ostream &os, const SRPC::failure &f) throw ()
/* Print a "SRPC::failure" object to "os" .*/
{
    int i;
    for (i = 0; i < 70; i++) os << '*'; os << endl;
    os << "SRPC failure (error " << f.r << ") :" << endl;
    os << "  " << f.msg << endl;
    for (i = 0; i < 70; i++) os << '*'; os << endl << endl;
    return os;
}

MultiSRPC *client_multi = 0;
Text server_host("localhost");

void ClientCall(int proc, int id, int count)
  throw (SRPC::failure)
{
  SRPC *srpc;
  MultiSRPC::ConnId connId;
  assert(client_multi != 0);
  connId = client_multi->Start(server_host, IntfName, /*OUT*/ srpc);

  srpc->start_call(proc, IntfVersion);
  srpc->send_int(id);
  srpc->send_int(count);
  srpc->send_end();

  // empty result
  srpc->recv_end();

  // close connection
  client_multi->End(connId);
}

struct ClientData
{
  int id, total;
};

void* Client_Worker_Thread(void *arg) throw ()
{
  ClientData *cd = (ClientData *) arg;
  int count = 0;
  try
    {
      while(count < cd->total)
	{
	  count++;
	  ClientCall(ProcIdMiddle, cd->id, count);
	}
      ClientCall(ProcIdEnd, cd->id, count);
    }
  catch(SRPC::failure f)
    {
      cio().start_err() << "(Client) " << f;
      cio().end_err();
    }
  return 0;
}

unsigned int client_thread_count = 10;
unsigned int client_call_count = 1000;

void* Client_Start_thread(void *unused) throw()
{
  ClientData *worker_data = NEW_ARRAY(ClientData, client_thread_count);
  Basics::thread *worker_threads = NEW_ARRAY(Basics::thread, client_thread_count);

  try
    {
      for(unsigned int i = 0; i < client_thread_count; i++)
	{
	  worker_data[i].id = random();
	  worker_data[i].total = (client_call_count +
				  (((unsigned int) random()) % client_call_count));
	  ClientCall(ProcIdStart, worker_data[i].id, worker_data[i].total);
	}
    }
  catch(SRPC::failure f)
    {
      cerr << f;
      return 0;
    }

  for(unsigned int i = 0; i < client_thread_count; i++)
    {
      worker_threads[i].fork(Client_Worker_Thread, &(worker_data[i]));
    }

  for(unsigned int i = 0; i < client_thread_count; i++)
    {
      worker_threads[i].join();
    }
  return 0;
}

struct ServerData
{
  IntIntTbl counts, targets;
  Basics::mutex mu;
};

void ServerProcStart(SRPC *srpc, ServerData *sd)
  throw (SRPC::failure)
{
    int id = srpc->recv_int();
    int target = srpc->recv_int();
    srpc->recv_end();

    sd->mu.lock();
    bool inTbl = sd->targets.Put(id, target);
    sd->mu.unlock();

    if(inTbl)
      {
	OBufStream msg;
	msg << "Second start call for id=" << id;
	srpc->send_failure(SRPC::invalid_parameter, msg.str());
      }

    srpc->send_end();
}

struct MiddleArgs
{
  int id;
  int count;
  MiddleArgs(SRPC *srpc)
  {
    id = srpc->recv_int();
    count = srpc->recv_int();
  }
};

void ServerProcMiddle(SRPC *srpc, ServerData *sd)
  throw (SRPC::failure)
{
    // It's a little pointless to put this in a heap block, but in the
    // GC case we want to make sure that we perform allocations.
    MiddleArgs *args = NEW_PTRFREE_CONSTR(MiddleArgs, (srpc));

    srpc->recv_end();

    int ocount = 0;
    sd->mu.lock();
    (void) sd->counts.Get(args->id, ocount);
    (void) sd->counts.Put(args->id, args->count);
    sd->mu.unlock();

    if(args->count != (ocount + 1))
      {
	OBufStream msg;
	msg << "Incorrect count on middle call for id=" << args->id
	    << ": expcted " << (ocount + 1) << ", received "
	    << args->count;
	delete args;
	srpc->send_failure(SRPC::invalid_parameter, msg.str());
      }

    delete args;

    srpc->send_end();
}

void ServerProcEnd(SRPC *srpc, ServerData *sd)
  throw (SRPC::failure)
{
    int id = srpc->recv_int();
    int count = srpc->recv_int();
    srpc->recv_end();

    int ocount = 0;
    int target = 0;
    sd->mu.lock();
    bool inTbl1 = sd->counts.Get(id, ocount);
    bool inTbl2 = sd->targets.Get(id, target);
    sd->mu.unlock();

    if(!inTbl1)
      {
	OBufStream msg;
	msg << "End call but no middle calls id=" << id
	    << count;
	srpc->send_failure(SRPC::invalid_parameter, msg.str());
      }

    if(count != ocount)
      {
	OBufStream msg;
	msg << "Incorrect count on end call for id=" << id
	    << ": expcted " << ocount << ", received "
	    << count;
	srpc->send_failure(SRPC::invalid_parameter, msg.str());
      }

    if(!inTbl2)
      {
	OBufStream msg;
	msg << "End call but no start call id=" << id
	    << count;
	srpc->send_failure(SRPC::invalid_parameter, msg.str());
      }

    if(count != target)
      {
	OBufStream msg;
	msg << "Incorrect final count on end call for id=" << id
	    << ": expcted " << target << ", received "
	    << count;
	srpc->send_failure(SRPC::invalid_parameter, msg.str());
      }

    srpc->send_end();
}

unsigned int server_running = 7;

LimService *ls = 0;

class ServerHandler : public LimService::Handler
{
private:
  ServerData *sd;
public:
  ServerHandler(ServerData *sd) : sd(sd) { }

  void call(SRPC *srpc, int intfVersion, int procId) throw(SRPC::failure)
  {
    switch (procId) {
      case ProcIdStart: ServerProcStart(srpc, sd); break;
      case ProcIdMiddle: ServerProcMiddle(srpc, sd); break;
      case ProcIdEnd: ServerProcEnd(srpc, sd); break;
      default: assert(false);
    }
  }

  void call_failure(SRPC *srpc, const SRPC::failure &f) throw()
  {
    // Ignore when the client closes the connection
    if (f.r == SRPC::partner_went_away) return;

    cio().start_err() << "(Server, in call) "  << f << endl;
    cio().end_err();
  }
  void accept_failure(const SRPC::failure &f) throw()
  {
    cio().start_err() << "(Server, accept) "  << f << endl;
    cio().end_err();
  }
  void other_failure(const char *msg) throw()
  {
    cio().start_err() << "(Server) "  << msg << endl;
    cio().end_err();
  }
  void listener_terminated() throw()
  {
    cio().start_err()
      << "(Server) Fatal error: unable to accept new connections; exiting"
      << endl;
    cio().end_err();
    abort();
  }
};

Basics::thread StartServer() throw (SRPC::failure)
{
  // start server
  ServerData *sd = NEW(ServerData);
  ServerHandler *handler = NEW_CONSTR(ServerHandler, (sd));

  ls = NEW_CONSTR(LimService,
		  (IntfName, IntfVersion,
		   server_running, *handler));
  Basics::thread th = ls->Forked_Run();

  // print ready msg
  cio().start_out() << "Server ready..." << endl
		    << endl;
  cio().end_out();

  return th;
}

ostream& operator << (ostream &os, const MultiSRPC::Stats &stats) throw ()
{
  os << "opened         = " << stats.opened << endl
     << "max_in_use     = " << stats.max_in_use << endl
     << "closed_dead    = " << stats.closed_dead << endl
     << "closed_discard = " << stats.closed_discard << endl
     << "closed_purge   = " << stats.closed_purge << endl
     << "closed_idle    = " << stats.closed_idle << endl
     << "closed_limit   = " << stats.closed_limit << endl;
  return os;
}

Text program_name;

void SyntaxError(char *msg, char *arg = (char *)NULL) throw ()
{
  ostream &err = cio().start_err();

  err << "Error: " << msg;
  if (arg != (char *)NULL) cerr << ": '" << arg << "'";
  err << endl;
  err << "SYNTAX: " << program_name << " -client "
      << " [ -calls n ] [ host ]" << endl;
  err << "SYNTAX: " << program_name << " -server "
      << " " << endl;
  cio().end_err();

  exit(1);
}

int main(int argc, char *argv[]) 
{
  bool run_server = true, run_client = true, use_msrpc_limit = false;

  program_name = argv[0];

  unsigned int seed = time(0);
  int msrpc_limit;

  for(int i = 1; i < argc && *argv[i] == '-'; i++)
    {
      if (strcmp(argv[i], "-client") == 0)
	{
	  run_client = true;
	  run_server = false;
	}
      else if (strcmp(argv[i], "-server") == 0)
	{
	  run_server = true;
	  run_client = false;
	}
      else if (strcmp(argv[i], "-calls") == 0)
	{
	  if (++i >= argc) SyntaxError("no argument for '-calls'");
	  if (sscanf(argv[i], "%u", &client_call_count) != 1)
	    SyntaxError("illegal argument to '-calls'", argv[i]);
	}
      else if (strcmp(argv[i], "-cthreads") == 0)
	{
	  if (++i >= argc) SyntaxError("no argument for '-cthreads'");
	  if (sscanf(argv[i], "%u", &client_thread_count) != 1)
	    SyntaxError("illegal argument to '-cthreads'", argv[i]);
	}
      else if (strcmp(argv[i], "-sthreads") == 0)
	{
	  if (++i >= argc) SyntaxError("no argument for '-sthreads'");
	  if (sscanf(argv[i], "%u", &server_running) != 1)
	    SyntaxError("illegal argument to '-sthreads'", argv[i]);
	}
      else if (strcmp(argv[i], "-seed") == 0)
	{
	  if (++i >= argc) SyntaxError("no argument for '-seed'");
	  if (sscanf(argv[i], "%u", &seed) != 1)
	    SyntaxError("illegal argument to '-seed'", argv[i]);
	}
      else if (strcmp(argv[i], "-msrpc-limit") == 0)
	{
	  use_msrpc_limit = true;
	  if (++i >= argc) SyntaxError("no argument for '-msrpc-limit'");
	  if (sscanf(argv[i], "%d", &msrpc_limit) != 1)
	    SyntaxError("illegal argument to '-msrpc-limit'", argv[i]);
	}
      else
	{
	  SyntaxError("unrecognized switch", argv[i]);
	}
    }

  cout << "To reproduce: -seed " << seed
       << " -calls " << client_call_count
       << " -cthreads " << client_thread_count
       << " -sthreads " << server_running;
  if(run_client && !run_server)
    cout << " -client";
  else if(!run_client && run_server)
    cout << " -server";
  if(run_client && use_msrpc_limit)
    cout << " -msrpc-limit " << msrpc_limit;
  cout << endl;

  if(run_client)
    {
      if(use_msrpc_limit)
	{
	  client_multi = NEW_CONSTR(MultiSRPC,
				    (MultiSRPC::fixedLimit, msrpc_limit));
	}
      else
	{
	  client_multi = NEW(MultiSRPC);
	}
    }

  srandom(seed);

  Basics::thread server_thread;

  try
    {
      if(run_server)
	server_thread = StartServer();
    }
  catch(SRPC::failure f)
    {
      cerr << f;
      return 1;
    }

  if(run_client)
    {
      Basics::thread client_thread;

      client_thread.fork(Client_Start_thread, 0);

      client_thread.join();
    }
  else
    {
      server_thread.join();
    }

  if(run_client)
    {
      assert(client_multi != 0);
      cio().start_out()
	<< "---------- Client MultiSRPC stats ----------" << endl
	<< client_multi->GetStats();
      cio().end_out();
      delete client_multi;
    }

  return 0;
}
  
