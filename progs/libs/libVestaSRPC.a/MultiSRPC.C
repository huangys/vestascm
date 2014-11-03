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

#include "SRPC.H"
#include "MultiSRPC.H"

const int DefaultConnections = 3;

#define LOCK(mu) (mu).lock(); try {
#define UNLOCK(mu) } catch (...) { (mu).unlock(); throw; } (mu).unlock()

MultiSRPC::Entry::~Entry() throw ()
{
    if (this->srpc != (SRPC *)NULL) {
	delete this->srpc;
	this->srpc = (SRPC *)NULL;
    }
}

MultiSRPC::MultiSRPC(MultiSRPC::connLimitKind c_limit, int c_limit_arg)
  : next(0), num(DefaultConnections),
    lru_head(-1), lru_tail(-1), lru_count(0),
    lru_lim_kind(c_limit), lru_lim_arg(c_limit_arg)
{
    this->tbl = NEW_ARRAY(MultiSRPC::EntryPtr, this->num);
    for (int i = 0; i < this->num; i++) {
	this->tbl[i] = (MultiSRPC::EntryPtr)NULL;
    }
    if(c_limit == MultiSRPC::descripFracLimit)
      {
	// If we're limiting to a fraction of the descriptors, call
	// getdtablesize(2) just once now and act like it's a fixed
	// limit.
	lru_lim_kind = MultiSRPC::fixedLimit;
	lru_lim_arg = getdtablesize() / c_limit_arg;
	// If the divisor was so large that we wound up with zero,
	// make sure we will cache at least one.  (A limit of zero
	// would actually be treated like no limit.)
	if(lru_lim_arg < 1)
	  lru_lim_arg = 1;
      }
    this->close_idle_thread.fork(MultiSRPC::close_idle_start, (void *) this);
}

MultiSRPC::~MultiSRPC()
{
    // free up table of connections
    LOCK(this->mu);
    if (this->tbl != (MultiSRPC::EntryPtr *)NULL) {
	for (int i = 0; i < this->next; i++) {
	    delete this->tbl[i];
	    this->tbl[i] = (MultiSRPC::EntryPtr)NULL;
	}
	delete[] this->tbl;
	this->tbl = (MultiSRPC::EntryPtr *)NULL;
    }
    // Tell the idle connection closing thread to stop
    this->close_idle_stop.signal();
    UNLOCK(this->mu);
    // Wait for the idle connection closing thread to exit
    this->close_idle_thread.join();
}

MultiSRPC::ConnId
MultiSRPC::Start(const Text &hostname, const Text &interface, SRPC*& srpc)
  throw(SRPC::failure)
{
    if (interface.Empty())
      throw(SRPC::failure(SRPC::invalid_parameter,
			  "Interface not specified"));

    int res, availEntry = -1;
    Basics::uint32 in_use_now = 0;

    // Search for existing connection not in use.
    this->mu.lock();
    try
      {
	for (res = 0; res < this->next; res++) {
	  if (!(this->tbl[res]->inUse) &&
	      (this->tbl[res]->srpc != (SRPC *)NULL) &&
	      !(this->tbl[res]->srpc->alive())) {
	    // Delete dead connection
	    this->lru_splice(res);
	    delete this->tbl[res]->srpc;
	    this->tbl[res]->srpc = (SRPC *)NULL;
	    this->stats.closed_dead++;
	  }	    
	  if((this->tbl[res]->inUse) && (this->tbl[res]->srpc != 0))
	    {
	      in_use_now++;
	    }
	  if (!(this->tbl[res]->inUse) &&
	      (this->tbl[res]->srpc != (SRPC *)NULL) &&
	      (hostname == this->tbl[res]->hostname) &&
	      (interface == this->tbl[res]->interface)) {
	    // Found existing connection
	    break;
	  }
	  if (!(this->tbl[res]->inUse) &&
	      (this->tbl[res]->srpc == (SRPC *)NULL)) {
	    // Remember available slot
	    availEntry = res;
	  }
	}
      }
    catch(...)
      {
	this->mu.unlock();
	throw;
      }

    // If we found an entry, note that it's in use and return it to
    // the caller.
    if(res < this->next)
      {
	this->tbl[res]->inUse = true;
	srpc = this->tbl[res]->srpc;
	this->lru_splice(res);
	this->mu.unlock();
	return res;
      }

    // no existing connection found -- create a new one
    if (availEntry >= 0) {
      // use an existing entry
      res = availEntry;
      tbl[res]->hostname = hostname;
      tbl[res]->interface = interface;
    } else {
      // use the next available entry in the array
      if (this->next >= this->num) {
	// no more space in table -- make it larger
	this->num *= 2;
	MultiSRPC::EntryPtr *newTbl = 
	  NEW_ARRAY(MultiSRPC::EntryPtr, this->num);
	int i;
	for (i = 0; i < this->next; i++) {
	  newTbl[i] = this->tbl[i]; // pointer copy
	  this->tbl[i] = (MultiSRPC::EntryPtr)NULL;
	}
	while (i < this->num) newTbl[i++] = (MultiSRPC::EntryPtr)NULL;
	delete[] this->tbl;
	this->tbl = newTbl;
      }
      res = this->next++;
      this->tbl[res] = NEW(Entry);
      this->tbl[res]->hostname = hostname;
      this->tbl[res]->interface = interface;
    }
    // Note that this entry is in use.
    this->tbl[res]->inUse = true;
    // Release the lock on this object during connection establishment
    // (as it is potentially long-running).
    this->mu.unlock();

    // Create the new SRPC object.
    SRPC *new_srpc = 0;
    try
      {
	new_srpc = NEW_CONSTR(SRPC, (SRPC::caller, interface, hostname));
      }
    catch(...)
      {
	// If we fail to create the new SRPC object, mark this table
	// entry as not in use.
	this->mu.lock();
	this->tbl[res]->inUse = false;
	this->mu.unlock();
	throw;
      }

    // Re-acquire the lock, record the new SRPC object in the table,
    // and return it to the caller.
    this->mu.lock();
    srpc = this->tbl[res]->srpc = new_srpc;
    this->stats.opened++;
    if(this->stats.max_in_use <= in_use_now)
      {
	// We add one for the new connection we just opened.
	this->stats.max_in_use = in_use_now+1;
	// (This is a little bit of an overestimate.  It's possible
	// that another thread finished with one of the connections
	// while we were opening a new one.)
      }
    this->mu.unlock();
    return res;
}

void MultiSRPC::End(MultiSRPC::ConnId id) throw ()
{
    if (id >= 0) {
	LOCK(this->mu);
	this->tbl[id]->inUse = false;
	this->tbl[id]->idle = false;
	if (!(this->tbl[id]->srpc->alive()) ||
	    !(this->tbl[id]->srpc->start_call_ok())) {
	    // A failure has occurred on this SRPC or something else
	    // went wrong and it's still in the middle of a call, so
	    // delete the SRPC resource and set the "srpc" field to
	    // NULL to indicate that this entry is available for
	    // re-use.
	    delete this->tbl[id]->srpc;
	    this->tbl[id]->srpc = (SRPC *)NULL;
	    this->stats.closed_dead++;
	} else {
	  this->lru_add(id);
	  // Get the limit on cached connections and enforce it if
	  // there is one
	  int limit = this->lru_limit();
	  if(limit > 0)
	    {
	      // As long as we have too many idle connections, close
	      // another
	      while(this->lru_count > limit)
		{
		  MultiSRPC::ConnId to_close = this->lru_tail;
		  assert((to_close >= 0) && (to_close < this->next));
		  assert(this->tbl[to_close] != 0);
		  assert(!this->tbl[to_close]->inUse);
		  assert(this->tbl[to_close]->srpc != 0);
		  this->lru_splice(to_close);
		  delete this->tbl[to_close]->srpc;
		  this->tbl[to_close]->srpc = 0;
		  this->stats.closed_limit++;
		}
	    }
	}
	UNLOCK(this->mu);
    }
}

void MultiSRPC::Discard(MultiSRPC::ConnId id) throw ()
{
    if (id >= 0) {
	LOCK(this->mu);
	this->tbl[id]->inUse = false;
	delete this->tbl[id]->srpc;
	this->tbl[id]->srpc = (SRPC *)NULL;
	this->stats.closed_discard++;
	UNLOCK(this->mu);
    }
}

void
MultiSRPC::Purge(const Text &hostname, const Text &interface) throw(SRPC::failure)
{
    if (interface.Empty())
      throw(SRPC::failure(SRPC::invalid_parameter,
			  "Interface not specified"));

    int res, availEntry = -1;

    // search for existing connections not in use
    LOCK(this->mu);
    for (res = 0; res < this->next; res++) {
	if ((hostname == this->tbl[res]->hostname) &&
            (interface == this->tbl[res]->interface) &&
            !(this->tbl[res]->inUse)) {
	    if ((this->tbl[res]->srpc != (SRPC *)NULL)) {
	        this->lru_splice(res);
	        delete this->tbl[res]->srpc;
	        this->tbl[res]->srpc = (SRPC *)NULL;
		this->stats.closed_purge++;
	    }
	}
    }
    UNLOCK(this->mu);
}

MultiSRPC::Stats MultiSRPC::GetStats() throw()
{
  this->mu.lock();
  Stats result = this->stats;
  this->mu.unlock();
  return result;
}

void *MultiSRPC::close_idle_start(void *arg) throw ()
{
  MultiSRPC *self = (MultiSRPC *) arg;
  self->close_idle_body();
  return 0;
}

void MultiSRPC::close_idle_body() throw()
{
  // Note that this is hard-coded to 30 seconds.  It doesn't seem
  // worth the complexity of making it configurable and it's not
  // really clear that there's much to be gained by tuning it.
  const int sleep_duration = 30;

  this->mu.lock();
  this->stats.closed_idle = 0;
  while(1)
    {
      struct timespec wakeup_time;
      wakeup_time.tv_sec = time(0) + sleep_duration;
      wakeup_time.tv_nsec = 0;
  
      // Note that the only time we release this->mu is when we are in
      // this timedwait call
      if(this->close_idle_stop.timedwait(this->mu,
					 &wakeup_time) == 0)
	{
	  // We've been told to exit (because our MultiSRPC instance
	  // is being destroyed)
	  this->mu.unlock();
	  return;
	}

      // Loop over all the current entries
      int my_lru_count = 0;
      for(unsigned int i = 0; i < this->next; i++)
	{
	  // If this entry has a connection and is not currently in use...
	  if((this->tbl[i]->srpc != 0) && !(this->tbl[i]->inUse))
	    {
	      if(this->lru_lim_kind != MultiSRPC::noLimit)
		{
		  // Idle connections must be on the LRU list (when we
		  // have one)
		  assert((this->tbl[i]->lru_prev != -1) ||
			 (this->lru_head == i));
		  assert((this->tbl[i]->lru_next != -1) ||
			 (this->lru_tail == i));
		}

	      if(this->tbl[i]->idle)
		{
		  // This connection was marked as idle on our last
		  // pass (and hasn't been used since).  Close it.
		  this->lru_splice(i);
		  delete this->tbl[i]->srpc;
		  this->tbl[i]->srpc = 0;

		  this->stats.closed_idle++;
		}
	      else
		{
		  // This conneciton has been used since our last
		  // pass.  Mark it as idle.
		  this->tbl[i]->idle = true;
		  my_lru_count++;
		}
	    }
	}
      if(this->lru_lim_kind != MultiSRPC::noLimit)
	{
	  assert(this->lru_count == my_lru_count);
	}
    }
}

void MultiSRPC::lru_splice(ConnId id)
{
  // Do nothing if we don't need an LRU list
  if(this->lru_lim_kind == MultiSRPC::noLimit) return;

  assert((id >= 0) && (id < this->next));
  Entry *entry = this->tbl[id];
  assert(entry != 0);
  if(entry->lru_prev == -1)
    {
      // Entry must be the head
      assert(this->lru_head == id);
      this->lru_head = entry->lru_next;
      if(this->lru_head != -1)
	{
	  // There's another entry to be the new head
	  Entry *new_head = this->tbl[this->lru_head];
	  assert(this->lru_count > 1);
	  assert(new_head != 0);
	  assert(new_head->lru_prev == id);
	  new_head->lru_prev = -1;
	}
      else
	{
	  // Entry must be the tail also
	  assert(this->lru_tail == id);
	  this->lru_tail = -1;
	  assert(this->lru_count == 1);
	}
    }
  else if(entry->lru_next == -1)
    {
      // Entry must be the tail
      assert(this->lru_tail == id);
      assert(this->lru_count > 1);
      this->lru_tail = entry->lru_prev;
      assert((this->lru_tail >= 0) && (this->lru_tail < this->next));
      Entry *new_tail = this->tbl[this->lru_tail];
      assert(new_tail != 0);
      assert(new_tail->lru_next == id);
      new_tail->lru_next = -1;
    }
  else
    {
      // This entry has both a prev and next
      assert(this->lru_count > 2);
      assert((entry->lru_prev >= 0) && (entry->lru_prev < this->next));
      assert((entry->lru_next >= 0) && (entry->lru_next < this->next));
      Entry *prev = this->tbl[entry->lru_prev];
      assert(prev != 0);
      assert(prev->lru_next == id);
      Entry *next = this->tbl[entry->lru_next];
      assert(next != 0);
      assert(next->lru_prev == id);
      prev->lru_next = entry->lru_next;
      next->lru_prev = entry->lru_prev;
    }

  entry->lru_prev = -1;
  entry->lru_next = -1;
  this->lru_count--;
}

void MultiSRPC::lru_add(ConnId id)
{
  // Do nothing if we don't need an LRU list
  if(this->lru_lim_kind == MultiSRPC::noLimit) return;

  assert((id >= 0) && (id < this->next));
  Entry *entry = this->tbl[id];
  assert(entry != 0);

  // It must not be on the LRU list currently
  assert(entry->lru_prev == -1);
  assert(entry->lru_next == -1);
  assert(this->lru_head != id);
  assert(this->lru_tail != id);

  if(this->lru_head == -1)
    {
      // The LRU list is empty.  This entry will be the head and the
      // tail.
      assert(this->lru_tail == -1);
      assert(this->lru_count == 0);
      this->lru_head = id;
      this->lru_tail = id;
    }
  else
    {
      // The LRU list is non-empty.  This entry will be the new head.
      assert(this->lru_tail != -1);
      assert(this->lru_count > 0);
      entry->lru_next = this->lru_head;
      this->tbl[this->lru_head]->lru_prev = id;
      this->lru_head = id;
    }
  this->lru_count++;
}

int MultiSRPC::lru_limit()
{
  switch(this->lru_lim_kind)
    {
    case MultiSRPC::maxUsedLimit:
      return (this->lru_lim_arg * this->stats.max_in_use) + 1;
    case MultiSRPC::fixedLimit:
      return this->lru_lim_arg;
    }
  return -1;
}

