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


// LimService -- An object for exporting an SRPC interface with limits on
//   the resources it will consume.

#ifndef _LIM_SERVICE_H
#define _LIM_SERVICE_H

#include <Basics.H>
#include <Sequence.H>
#include <Table.H>
#include "SRPC.H"

/* A "LimService" object is an object for exporting an "SRPC"
  interface. The "LimService" constructor takes as arguments the name
  of the interface to export and a handler object with member
  functions to be called in response to a remote procedure call and
  different kinds of failures. Once the remote call completes, the
  "SRPC" connection used for that call is maintained for a future
  connection.

  The "LimService" constructor also takes a numeric parameter that
  allows the client to specify the limit on the resources consumed by
  the service. The parameter is an upper bound on the number of server
  threads allowed to run simultaneously. If that number of threads are
  already running, and another call comes in, the latter call will
  block until one of the other running threads completes.

  The handler object passed to the constructor must be a concrete
  sub-class of LimService::Handler.  It's "call" member function is
  used for each remote procedure call and can throw the
  "SRPC::failure" exception. It is imperative that if that exception
  is thrown during execution of the callback procedure as a result of
  a method call on its "srpc" argument that the exception is also
  thrown by the callback. Hence, if the callback procedure catches
  that exception internally, it must re-throw it. Otherwise, the
  LimService machinery cannot de-allocate the resources associated
  with the failed (and hence, dead) SRPC connection.

  It is important that the "LimService" object not be deleted by the main
  server thread so long as the server is in operation. */

// The "LimService" object ----------------------------------------------------

class LimService {
  public:

    // Users must define a sub-class of LimService::Handler, which
    // provides a mechanism for LimService to call back to the client.
    // A reference to one is passed to the LimService constructor.
    class Handler
    {
    public:
      Handler() {}

      // Note: member functions must be defined in a concrete
      // sub-class.

      // Called for each remote call.  Must not delete "srpc", as a
      // single connection can make multiple RPC calls.  If an
      // "SRPC::failure" exception is thrown while processing the
      // call, it should be re-thrown even if it is caught internally
      // (e.g. to release locks or other resources).
      virtual void call(SRPC *srpc, int intfVersion, int procId)
	throw(SRPC::failure) = 0;

      // Called in response to an SRPC failure that occurs during the
      // execution of "call" (above).
      virtual void call_failure(SRPC *srpc, const SRPC::failure &f) throw() = 0;

      // Called if there's an error accepting a new connection
      // (e.g. out of file descriptors).
      virtual void accept_failure(const SRPC::failure &f) throw() = 0;

      // Called if we determine there was a fatal error with the
      // listening socket used to accept new connections.  After this
      // is called, no new connections will be accepted.  In most
      // cases, it should be used to exit from the program.
      virtual void listener_terminated() throw() = 0;

      // Called for some other esoteric failures which are unlikely to
      // come up (e.g. poll(2) failing with an unexpected errno like
      // ENOMEM).
      virtual void other_failure(const char *msg) throw() = 0;
    };

    LimService(const Text &intfName, const int intfVersion,
	       int maxRunning, Handler &handler,
	       long stacksize = -1,
	       const Text &hostName = "")
      throw ();
    /* Initialize a "LimService" object that, when run, will export the
       service named "intfName" on the host named "hostName". If "stacksize"
       is not defaulted, it specifies the stack size (in bytes) of each
       thread forked by the "LimService" object. Use the "Run" method below
       to run an initialized "LimService" object. */

    LimService(const int intfVersion,
	       int maxRunning, Handler &handler,
	       long stacksize = -1,
	       const Text &hostName = "")
      throw ();
    /* Initialize a "LimService" object that, when run, will export an
       anonymous service (i.e., an arbitrarily chosen unused port number)
       on the host named "hostName". If "stacksize" is not defaulted,
       it specifies the stack size (in bytes) of each thread forked by
       the "LimService" object. Use the "Run" method below to run an
       initialized "LimService" object. */

    LimService(const Text &intfName,
	       int maxRunning, Handler &handler,
	       long stacksize = -1,
	       const Text &hostName = "")
      throw ();
    /* Initialize a "LimService" object that, when run, will export
       the service named "intfName" on the host named "hostName". If
       "stacksize" is not defaulted, it specifies the stack size (in
       bytes) of each thread forked by the "LimService" object. Use
       the "Run" method below to run an initialized "LimService"
       object.  Calls with any interface version will be accepted and
       passed on to handler's "call" member function. */
    LimService(const Text &intfName,
	       int maxRunning, Handler &handler,
	       const Basics::thread_attr &worker_attr,
	       const Text &hostName = "")
      throw ();

    LimService(int maxRunning, Handler &handler,
	       long stacksize = -1,
	       const Text &hostName = "")
      throw ();
    /* Initialize a "LimService" object that, when run, will export an
       anonymous service (i.e., an arbitrarily chosen unused port
       number) on the host named "hostName". If "stacksize" is not
       defaulted, it specifies the stack size (in bytes) of each
       thread forked by the "LimService" object. Use the "Run" method
       below to run an initialized "LimService" object. Calls with any
       interface version will be accepted and passed on to handler's
       "call" member function. */

    ~LimService() throw ()
	{ if (this->sock != (TCP_sock *)NULL) delete this->sock; }

    void Run() throw (SRPC::failure);
    /* In the following, references to variable names like "intf" correspond
       to the values of the parameters to the LimService object's constructor.

       The "Run" method exports the service named "intfName" from the host
       named "hostName". It blocks until a fatal failure occurs, in which case
       "SRPC::failure" will be thrown (see below). To run a LimService object
       in a forked thread, use the "ForkedRun" method below.

       If "hostName" is defaulted, then the interface is exported from the
       current host. However, if the local machine has multiple IP addresses,
       "hostName" can be used to differentiate among them. The interface will
       be exported with the version number "intfVersion".

       "Run" starts one thread to accept new connections and
       "maxRunning" worker threads to handle calls.  The thread
       calling "Run" handles waiting for incoming calls on existing
       connections and distributing them to the worker threads.

       When a worker thread gets a connection with an incoming call,
       it calls the user-supplied handler's "call" member function.
       This function is invoked with three arguments: "srpc" is a
       pointer to the SRPC instance for the connection making the
       call, "intfVer" is the interface version, and "procId" is the
       identifier for the procedure to invoke.  (If a fixed interface
       version was passed to the LimService constructor, then the
       "intfVer" argument will be exactly that version.)  The handler
       object provides a way of communicating data to each worker
       thread executing a call.  In order that the SRPC connection may
       be reused for future calls, the handler's "call" member
       function is required *not* to delete the "srpc" at the end of
       the call.

       In the event that all "maxRunning" worker threads are already
       handling calls, any new incoming calls are queued until one or
       more of those threads becomes available.  Connections
       attempting to make calls are serviced in a fair round-robin
       fashion.

       If an error occurs during initialization (e.g. "SRPC::failure"
       is thrown when attempting to listen for incoming connections),
       the "Run" will throw "SRPC::failure".  Once the listening
       socket is created and the worker and connection accepting
       threads are started, "Run" will not return.

       If the "SRPC::failure" exception is thrown by the handler's
       "call" member function, the exception is caught internally.
       The handler's "call_failure" member function is invoked with
       the current SRPC connection and the failure obect itself.

       If an error occurs when accepting a new connection, the
       handler's "accept_failure" member function is called.  In
       certain other unusual error cases, the handler's
       "other_failure" member function is called.  (Neither of these
       cases the server to stop or "Run" to return or throw an
       exception.) */

    Basics::thread Forked_Run() throw (SRPC::failure);
    /* This function forks a thread to execute a call of Run() and
       returns the thread as its result.  Forked_Run doesn't return
       until the connection accepting thread invoked from Run is ready
       to accept connection requests.  This is a stronger guarantee
       than if a client simply forked the Run() operation directly. */

    Text IntfName() throw () { return this->intfName; }
      /* returns the interface name used by the LimService object.  If
         the first constructor above was used, this is simply its first
         parameter; otherwise, it is the port number (converted to ASCII)
         chosen for the (anonymous) interface. */

  private:
    friend void* LimService_Worker(void *) throw ();
    /* Function used by "Run" as the body of one of the "maxRunning"
       worker threads. */

    friend void* LimService_Acceptor(void *) throw ();
    /* Function used by "Run" as the body of the connection accepting
       thread. */

    void HandleFailure(const SRPC::failure& f) throw ();
    /* Stash the failure "f" in "this->server_failure", and signal a state
       change to "ForkedRun". */

    friend void *LimService_StartServer(void *) throw ();
    /* Function used by "ForkedRun" as the body of the new thread.
       This invokes the "Run" method to run the server. In the event
       of an "SRPC::failure", it invokes the "HandleFailure" method to
       synchronize with the "Forked_Run" method. */

    // readonly after initialization (constructor arguments)
    Text intfName;              // first constructor only
    int intfVersion;
    int maxRunning;
    Handler &handler;
    Basics::thread_attr worker_attributes;
    Text hostName;
    TCP_sock *sock;             // used by second constructor only

    // mutable fields
    Basics::mutex mu;              // protects the following fields:

    bool server_state_change;      // true iff "Forked_Run" done
    Basics::cond state_change;     // == server_state_change
    SRPC::failure server_failure;  // saved failure used by "Forked_Run"

    // Queue an SRPC connection to be serviced by a worker thread.
    void queue_work(SRPC *srpc);

    // Get an SRPC connection that needs to be serviced.  (Used by
    // worker threads to get the next connection to service).
    SRPC *get_work();

    // These functions are called when a new connection is opened and
    // when a connection is closed.  These help with book-keeping for
    // the work table below.
    void new_conneciton(SRPC *srpc);
    void dead_conneciton(SRPC *srpc);

    // Queue of connections which are trying to start an RPC and need
    // a thread to process them.  Worker threads wait on work_avail
    // when work_queue is empty.
    Basics::mutex work_queue_mu;
    Basics::cond work_avail;

    // First-level queue: hosts which have some connections which need
    // to be serviced.
    Sequence<in_addr, true> work_host_queue;

    // We allocate one host_work_info for each client host with open
    // connections.
    struct host_work_info
    {
      unsigned int open_conneciton_count;
      Sequence<SRPC *> work_queue;
      host_work_info() : open_conneciton_count(0) { }
    };
    // A simple type used to turn a in_addr into a hash-able key for
    // Table.
    class host_in_key
    {
    private:
      in_addr host;
    public:
      host_in_key(in_addr a) : host(a) { }
      operator in_addr() { return host; }
      Word Hash() const throw () { return (Word)ntohl(host.s_addr); }
      in_addr& operator = (const in_addr& val) { return (host = val); }
      bool operator == (const host_in_key& other) const throw ()
      { return host.s_addr == other.host.s_addr; }
    };

    // This is the per-host table of work.  We separate it by host so
    // that we can round-robin hosts first, then round-robin
    // connections per host.
    Table<host_in_key, host_work_info *>::Default work_table;

    // Return an idle connection to the thread which listens for
    // incoming RPCs.
    void return_idle(SRPC *srpc);

    // Queue of connections which have completed an RPC and we need to
    // start polling for subsequent calls.  Worker threads use this to
    // send connections back to the polling thread.
    Basics::mutex return_queue_mu;
    Sequence<SRPC *> return_queue;

    // Pipe used to wake up the thread polling for incoming RPCs when
    // a connection completes an RPC.  (return_queue_mu should be
    // locked when writing into the pipe.)
    int between_call_wakeup[2];

    /* This is how connections flow through the different threads and
       queues:

      The main thread (LimService::Run) loops forever using poll(2) to
      check for incoming data on all open connections and the read end
      of a pipe (between_call_wakeup).  The pipe is used by other
      threads returning a connection with no bytes available to be
      read (i.e. no RPC currently being made).  Because the main
      thread blocks in poll(2), we must use a file descriptor to cause
      it to wake up and add the now idle connection to those being
      polled for incoming RPCs.

      The connection accepting thread (LimService_Acceptor) loops
      forever accepting new connections.  Any connection which
      immediately has data ready to be read is sent to a worker thread
      (by calling "queue_work").  Any connection which does not
      immediately have data ready to be read is sent to the main
      thread (by calling "return_idle").

      Each worker thread (LimService_Worker) takes the first
      connection from "work_queue" and processes a call from it.
      After the call completes, there are three possibilities:

        1. If the connection has more data ready to be read
        (i.e. another RPC made immediately after the first), it is
        queued for a worker thread (by calling "queue_work").  (We
        don't want to immediately process the next call in the same
        worker thread, as we want to fairly service all connections
        even when there are more in-flight RPCs than worker threads.)

	2. If the connection does not have more data ready to be read,
	it is sent to the main thread (by calling "return_idle").

	3. If the connection is no longer alive, the SRPC instance is
	deleted.
    */

    /* prevent copying */

    LimService(const LimService &);
    LimService(LimService &);
    // Copy constructor; should never be accessed.

    void operator=(LimService &);

};

#endif // _LIM_SERVICE_H
