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

#include <signal.h>
#include <Basics.H>
#include <sys/poll.h>

#include <BasicsAllocator.H>
#include <BufStream.H>
#include <list>

#include "SRPC.H"
#include "SRPC_impl.H"
#include "LimService.H"

using std::cerr;
using std::cout;
using std::flush;
using Basics::OBufStream;

LimService::LimService(
  const Text &intfName, const int intfVersion,
  int maxRunning, Handler &handler, long stacksize,
  const Text &hostName)
  throw ()
: intfName(intfName), intfVersion(intfVersion), maxRunning(maxRunning),
  handler(handler),
  hostName(hostName),
  sock(NULL)
{
  if(stacksize > 0)
    worker_attributes.set_stacksize(stacksize);

    // initialize synchronization data
}

LimService::LimService(
  const int intfVersion,
  int maxRunning, Handler &handler, long stacksize,
  const Text &hostName)
  throw ()
: intfVersion(intfVersion), maxRunning(maxRunning),
  handler(handler),
  hostName(hostName)
{
  if(stacksize > 0)
    worker_attributes.set_stacksize(stacksize);

    // initialize "intfName" and "sock"
    this->sock = NEW(TCP_sock);   // choose a port number
    sockaddr_in sin;
    this->sock->get_local_addr(/*OUT*/ sin);
    char buff[10];
    sprintf(buff, "%d", ntohs(sin.sin_port));
    this->intfName = Text(buff);
}

LimService::LimService(
  const Text &intfName,
  int maxRunning, Handler &handler, long stacksize,
  const Text &hostName)
  throw ()
: intfName(intfName), maxRunning(maxRunning),
  handler(handler),
  hostName(hostName),
  sock(NULL)
{
  if(stacksize > 0)
    worker_attributes.set_stacksize(stacksize);

    this->intfVersion = SRPC::default_intf_version;
    // initialize synchronization data
}

LimService::LimService(
  const Text &intfName,
  int maxRunning, Handler &handler,
  const Basics::thread_attr &worker_attr,
  const Text &hostName)
  throw ()
: intfName(intfName), maxRunning(maxRunning),
  handler(handler),
  hostName(hostName),
  sock(NULL), worker_attributes(worker_attr)
{
    this->intfVersion = SRPC::default_intf_version;
    // initialize synchronization data
}

LimService::LimService(
  int maxRunning, Handler &handler, long stacksize,
  const Text &hostName)
  throw ()
: maxRunning(maxRunning),
  handler(handler),
  hostName(hostName)
{
  if(stacksize > 0)
    worker_attributes.set_stacksize(stacksize);

    this->intfVersion = SRPC::default_intf_version;
    // initialize "intfName" and "sock"
    this->sock = NEW(TCP_sock);   // choose a port number
    sockaddr_in sin;
    this->sock->get_local_addr(/*OUT*/ sin);
    char buff[10];
    sprintf(buff, "%d", ntohs(sin.sin_port));
    this->intfName = Text(buff);
}

// We use this type for the SRPC objects which are between calls and
// which we need to poll on.
typedef std::list<SRPC *, Basics::Allocator<SRPC *> > socket_list_t;

void* LimService_Worker(void *arg) throw ();
void* LimService_Acceptor(void *arg) throw ();

void LimService::Run() throw (SRPC::failure)
{
  unsigned int poll_data_size = maxRunning+1;
  struct pollfd *poll_data = NEW_PTRFREE_ARRAY(struct pollfd, poll_data_size);

  if(pipe(this->between_call_wakeup) < 0)
    {
      int errno_save = errno;
      OBufStream msg;
      msg << "LimService: Unexpected error from pipe(2): "
	  << Basics::errno_Text(errno_save)
	  << " (errno = " << errno_save << ")";
      // We throw an exception in this case, as we can't actually
      // start the server.
      throw SRPC::failure(SRPC::internal_trouble, msg.str());
    }

  // We always poll the pipe the worker threads to know when they
  // complete a call.
  poll_data[0].fd = this->between_call_wakeup[0];
  poll_data[0].events = POLLIN;

  // Start the connection accepting thread
  Basics::thread acceptor;
  acceptor.fork_and_detach(LimService_Acceptor,
			   (void *)this, this->worker_attributes);

  // Wait for the connection accepting thread to determine whether we
  // can listen on that specified port.
  {
    this->mu.lock();
    while (!(this->server_state_change)) {
      this->state_change.wait(this->mu);
    }
    // save failure with "mu" held
    SRPC::failure f = this->server_failure; // save failure with "mu" held
    this->mu.unlock();
    if(f.r != 0)
      {
	// The connection accepting thread could not listen (maybe
	// another process is already lsitening on the same port).
	// Throw the exception up to the caller.
	throw(f);
      }
  }

  // Start all the worker threads.  (We wait until after the acceptor
  // thread is started to do this so that if the acceptor fails we
  // don't leave the idle worker threads around.)
  Basics::thread *workers = NEW_ARRAY(Basics::thread, maxRunning);
  for(unsigned int i = 0; i < maxRunning; i++)
    {
      workers[i].fork_and_detach(LimService_Worker,
				 (void *)this, this->worker_attributes);
    }

  // List of SRPC objects which are between calls (either have
  // completed a call or haven't made their first one yet).  We poll
  // to see if they send bytes to us.

  // A couple things to note about between_call_list:

  // - We use std::list so that we can remove elements out of the
  // middle.  (When we find a connection that has incoming data, we
  // remove it from between_call_list and place it on
  // this->work_queue.)

  // - Connections with incoming data are queued for the work threads
  // starting at the beginning of this list.  Connections returning to
  // this list are placed at the end of this list.  This ensures that
  // when there are more active clients than worker threads that we
  // fairly distribute the different connections to the worker
  // threads.
  socket_list_t between_call_list;

  while(true)
    {
      if(between_call_list.size() + 1 > poll_data_size)
	{
	  // Allocate a new larger struct pollfd array.  (Note that we
	  // only ever re-allocate to make it larger.)
	  unsigned int new_poll_data_size =
	    (between_call_list.size() + maxRunning + 1);
	  struct pollfd *new_poll_data =
	    NEW_PTRFREE_ARRAY(struct pollfd, new_poll_data_size);

	  // Copy the one we always keep (the "wake up for a returned
	  // connection" pipe).
	  new_poll_data[0] = poll_data[0];

	  delete [] poll_data;
	  poll_data = new_poll_data;
	  poll_data_size = new_poll_data_size;
	}
      unsigned int nfds = 1;
      for(socket_list_t::iterator i = between_call_list.begin();
	  i != between_call_list.end();)
	{
	  SRPC *srpc = *i;
	  assert(srpc != 0);
	  assert(srpc->socket() != 0);
	  // If we had already received bytes for a new call, the
	  // worker thread would have recycled it back into
	  // work_queue.
	  assert(srpc->socket()->recv_buff_bytes() == 0);

	  // Prepare to poll for data from this socket.
	  poll_data[nfds].fd = srpc->socket()->get_fd();
	  poll_data[nfds].events = POLLIN;
	  poll_data[nfds].revents = 0;
	  nfds++;
	  i++;
	}
      assert(nfds <= poll_data_size);

      poll_data[0].revents = 0;

      int nready = poll(poll_data, nfds, -1);

      // Just go around again on non-fatal errors
      if(nready < 0)
	{
	  if((errno != EAGAIN) && (errno != EINTR))
	    {
	      // We don't expect any errors other than EAGAIN and
	      // EINTR from poll.  We could stop rather than going
	      // around the loop again if we get one, but we'll
	      // instead hope for the best.  However, we will give the
	      // application a chance to print something.
	      int errno_save = errno;
	      OBufStream msg;
	      msg << "LimService: Unexpected error from poll(2): "
		  << Basics::errno_Text(errno_save)
		  << " (errno = " << errno_save << ")";
	      handler.other_failure(msg.str());
	    }
	  continue;
	}

      {
	socket_list_t::iterator it = between_call_list.begin();
	for(unsigned int i = 1; i < nfds; i++)
	  {
	    assert(it != between_call_list.end());
	    // If this connection has data waiting to be read...
	    if(poll_data[i].revents & POLLIN)
	      {
		SRPC *srpc = *it;
		assert(srpc != 0);
		assert(srpc->socket() != 0);
		assert(poll_data[i].fd == srpc->socket()->get_fd());

		// Send this connection to a worker thread.
		this->queue_work(srpc);

		// Remove it from the list we poll
		between_call_list.erase(it++);
	      }
	    else
	      {
		it++;
	      }
	  }
      }

      // Did a worker thread tell us to wake up and start polling on a
      // socket that completed an RPC?
      if(poll_data[0].revents & POLLIN)
	{
	  // Propagate everything from the return queue to the between
	  // call list
	  unsigned int return_count = 0;
	  this->return_queue_mu.lock();
	  while(this->return_queue.size() > 0)
	    {
	      between_call_list.push_back(this->return_queue.remlo());
	      return_count++;
	    }
	  this->return_queue_mu.unlock();

	  // Drain bytes from the pipe.  Each pointer from the return
	  // queue should have one byte in the pipe, so we read all
	  // those in a small number of read(2) calls.  
	  while(return_count > 0)
	  {
	    char pipe_drain_buffer[50];
	    int read_count;
	    char c;
	    do
	      read_count = read(this->between_call_wakeup[0],
				(void *) pipe_drain_buffer,
				(return_count > sizeof(pipe_drain_buffer))
				? sizeof(pipe_drain_buffer) : return_count);
	    while((read_count == -1) && (errno == EINTR));
	    assert(read_count > 0);
	    assert(read_count <= return_count);
	    assert(read_count <= sizeof(pipe_drain_buffer));
	    return_count -= read_count;
	  }
	}
    }
}

void LimService::queue_work(SRPC *srpc)
{
  // Queue this connection (which presumably has sent data to start an
  // RPC) for a worker thread.

  sockaddr_in addr;
  srpc->socket()->get_remote_addr(addr);
  host_work_info *info = 0;

  this->work_queue_mu.lock();
  bool inTbl = this->work_table.Get(addr.sin_addr, info);
  assert(inTbl);
  assert(info != 0);
  assert(info->work_queue.size() < info->open_conneciton_count);
  info->work_queue.addhi(srpc);
  if(info->work_queue.size() == 1)
    {
      // Put this host on the queue of client hosts to be serviced.
      this->work_host_queue.addhi(addr.sin_addr);
    }
  this->work_queue_mu.unlock();
  this->work_avail.signal();
}

SRPC *LimService::get_work()
{
  SRPC *srpc = 0;
  host_work_info *info = 0;
  this->work_queue_mu.lock();
  // Wait for some work to do
  while(this->work_host_queue.size() < 1)
    {
      this->work_avail.wait(this->work_queue_mu);
    }
  // Get the client host to service next and look up its queue of work
  in_addr work_addr = this->work_host_queue.remlo();
  bool inTbl = this->work_table.Get(work_addr, info);
  assert(inTbl);
  assert(info != 0);
  assert(info->work_queue.size() <= info->open_conneciton_count);
  assert(info->work_queue.size() > 0);
  // Get the next connection from this host to be serviced.
  srpc = info->work_queue.remlo();
  if(info->work_queue.size() > 0)
    {
      // More work to do from this client host, put it back on the
      // host queue
      this->work_host_queue.addhi(work_addr);
    }
  this->work_queue_mu.unlock();
  return srpc;
}

void LimService::new_conneciton(SRPC *srpc)
{
  sockaddr_in addr;
  srpc->socket()->get_remote_addr(addr);
  host_work_info *info = 0;
  this->work_queue_mu.lock();
  if(!this->work_table.Get(addr.sin_addr, info))
    {
      info = NEW(host_work_info);
      this->work_table.Put(addr.sin_addr, info);
    }
  info->open_conneciton_count++;
  this->work_queue_mu.unlock();
}

void LimService::dead_conneciton(SRPC *srpc)
{
  sockaddr_in addr;
  srpc->socket()->get_remote_addr(addr);
  host_work_info *info = 0;
  this->work_queue_mu.lock();
  bool inTbl = this->work_table.Get(addr.sin_addr, info);
  assert(inTbl);
  assert(info != 0);
  assert(info->open_conneciton_count > 0);
  info->open_conneciton_count--;
  if(info->open_conneciton_count == 0)
    {
      // No more open connections from this host, get rid of its
      // host_work_info.
      this->work_table.Delete(addr.sin_addr, info, false);
      delete info;
    }
  this->work_queue_mu.unlock();
}

void LimService::return_idle(SRPC *srpc)
{
  // Return this to the set of connections we listen for additional
  // calls on.
  this->return_queue_mu.lock();
  this->return_queue.addhi(srpc);
  int written_count;
  char c = 1;
  do
    written_count = write(this->between_call_wakeup[1],
			  (void *) &c, 1);
  while((written_count == -1) && (errno == EINTR));
  assert(written_count == 1);
  this->return_queue_mu.unlock();
}

void* LimService_Acceptor(void *arg) throw ()
{
  LimService *ls = (LimService *) arg;

  SRPC_listener *listener = 0;
  try
    {
      if(ls->sock == NULL)
	{
	  u_short port = TCP_sock::parse_port(ls->intfName);
	  listener = SRPC_listener::create(port);
	}
      else
	{
	  sockaddr_in addr;
	  ls->sock->get_local_addr(addr);
	  listener = SRPC_listener::create(addr.sin_port, ls->sock);
	}
    }
  catch (TCP_sock::failure f)
    {
      // Couldn't create the listener: Store the failure and arrange
      // for this excepetion to be re-thrown from Run or Forked_Run.
      ls->HandleFailure(SRPC::convert_TCP_failure(f));

      // Exit this thread because without a listener we have nothing
      // to do.
      return (void *)0;
    }

  // Note that we're ready and wake up waiting threads.
  ls->mu.lock();
  ls->server_state_change = true;
  ls->state_change.broadcast();
  ls->mu.unlock();

  while(1)
    {
      try
	{
	  // Perform the accept(2) and create a new SRPC instance
	  // for the new socket.
	  SRPC *srpc = NEW_CONSTR(SRPC, (listener->sock->wait_for_connect()));

	  // Register this new connection.
	  ls->new_conneciton(srpc);

	  // Check to see if the client has already sent bytes to us
	  // (i.e. the first call).
	  if(srpc->socket()->recv_data_ready())
	    {
	      // We can now pass this off to a worker thread
	      // immediately (without going through the polling
	      // thread).
	      ls->queue_work(srpc);
	    }
	  else
	    {
	      // Send this to the polling thread until it gives us
	      // some data.  (This avoids tying up a worker thread
	      // waiting for the first byte of data(.
	      ls->return_idle(srpc);
	    }
	}
      catch (TCP_sock::failure f)
	{
	  // invoke the user-supplied failure callback
	  ls->handler.accept_failure(SRPC::convert_TCP_failure(f));
	}
      catch (SRPC::failure f)
	{
	  // invoke the user-supplied failure callback
	  ls->handler.accept_failure(f);
	}

      // Check to see if the listener has died (e.g. from a fatal
      // error on accepting a new connection).
      if(!listener->sock->alive())
	{
	  // Fatal error: call the user-supplied callback for ceasing
	  // to listen for new connections and exit the thread.
	  ls->handler.listener_terminated();
	  break;
	}
    }

  // Not reached
  return (void *)0;
}

void* LimService_Worker(void *arg) throw ()
{
  LimService *ls = (LimService *) arg;

  while(1)
    {
      // Get an SRPC connection which needs serivcing.
      SRPC *srpc = ls->get_work();

      // As long as we actually got a connection that wants to make an
      // RPC...
      if(srpc)
	{
	  assert(srpc->socket() != 0);
	  try
	    {
	      // Get the call
	      int procId = SRPC::default_proc_id;
	      int intfVer = ls->intfVersion;
	      srpc->await_call(/*INOUT*/ procId, /*INOUT*/ intfVer);

	      try {
		ls->handler.call(srpc, intfVer, procId);
	      }
	      catch (...) {
		throw;
	      }
	      {
		SRPC::failure f;
		if(srpc->previous_failure(&f))
		  {
		    // Client should have re-thrown the exception, but
		    // we'll let them get away with not doing so.
		    // (Note: older versions of LimService used to
		    // abort in this case, which seems unecessary.)
		    ls->handler.call_failure(srpc, f);
		  }
	      }
	    }
	  catch (SRPC::failure f) {
	    // Invoke the user-supplied failure callback.
	    ls->handler.call_failure(srpc, f);

	    // Note that worker threads invoke call_failure directly,
	    // which means until call_failure returns we have one less
	    // thread to process calls.  In the old implementation,
	    // threads handling a failure didn't count against the
	    // running limit.  We could introduce another thread which
	    // will call the failure callback in the background and
	    // queue connections with failures for it.
	  }

	  if(srpc->alive())
	    {
	      // Check to see if the client has already sent more
	      // bytes to us (i.e. another call).
	      if(srpc->socket()->recv_data_ready())
		{
		  // Recycle to a worker thread a little bit faster if
		  // the client has already sent another call.
		  ls->queue_work(srpc);
		}
	      else
		{
		  // Return this to the set of connections we listen for
		  // additional calls on.
		  ls->return_idle(srpc);
		}
	    }
	  else
	    {
	      // The conenction is dead, so get rid of the object.
	      ls->dead_conneciton(srpc);
	      delete srpc;
	    }
	}
    }

  // Not reached
  return (void *)0;
}

void LimService::HandleFailure(const SRPC::failure& f) throw ()
{
    this->mu.lock();
    if(!this->server_state_change)
      {
	this->server_failure = f;
	this->server_state_change = true;
	this->state_change.broadcast();
      }
    this->mu.unlock();
    // Should probably call client's failure proc, but only
    // if failure occurred after Forked_Run has gone away.
}

void* LimService_StartServer(void *arg) throw ()
// "Forked_Run" thread closure
{
    LimService *ls = (LimService *)arg;
    try {
	ls->Run();
    } catch (SRPC::failure f) {
	ls->HandleFailure(f);
    }
    return (void *)NULL;
}

Basics::thread LimService::Forked_Run() throw (SRPC::failure)
{
    Basics::thread th;
    this->mu.lock();
    this->server_failure = SRPC::failure(0, "");
    this->server_state_change = false;
    this->mu.unlock();
    Basics::thread_attr listener_attr(worker_attributes);
    if(listener_attr.get_stacksize() < 100000)
      listener_attr.set_stacksize(100000);
    th.fork(LimService_StartServer, (void *)this, listener_attr);
    this->mu.lock();
    while (!(this->server_state_change)) {
	this->state_change.wait(this->mu);
    }

    SRPC::failure f = this->server_failure; // save failure with "mu" held
    this->mu.unlock();
    if (f.r != 0) throw(f);
    return th;
}
