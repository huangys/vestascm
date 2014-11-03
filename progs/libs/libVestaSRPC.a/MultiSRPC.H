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

// MultiSRPC -- a cache of SRPC connections to servers on various machines

// A "MultiSRPC" object is a cache of client SRPC connections to
// potentially multiple servers that all export the same interface.
// The object provides two functions, Start and End.  A client calls
// Start and specifies the desired host for the server; Start returns
// a suitable SRPC object, either from its cache or newly created.
// The client calls End to tell MultiSRPC that it is finished with an
// SRPC connection, which is then placed in the cache for future use
// by Start.

// Each MultiSRPC object represents a cache for a collection of
// servers for the same interface.  Here the term "interface" is used
// somewhat loosely, since SRPC supports only a weak notion of
// interface.  SRPC allows its callers to specify a TCP port number
// (or name for a port number) that is used to connect client and
// server, but it is the responsibility of client and server code to
// agree on such a number through other means.

// There are two common styles of assigning port numbers.  In the
// simpler case, a port number is associated with an abstract
// "service" in advance of its deployment.  This number is either
// wired into the code of client and server, or is placed in some
// external configuration database where it is looked up by using some
// other piece of statically shared information.  This technique works
// well when the set of services can be enumerated in advance of
// execution.

// A more complicated scheme is used when the service is, in effect,
// an object, and the number of objects and/or their distribution
// across service machines is not known statically.  In this case, the
// port number is assigned dynamically and communicated between client
// and servers by means external to SRPC.  This technique allows
// multiple object servers to reside on the same host machine; they
// simply use different port numbers.

// MultiSRPC supports both of these styles.  The client specifies both
// a "host" (name or IP address) and an "interface" (port number) on
// each call to Start.

#ifndef _MULTI_SRPC_H
#define _MULTI_SRPC_H

#include <Basics.H>
#include "SRPC.H"

class MultiSRPC {
public:
  // MultiSRPC can place a limit on the maximum number of currently
  // cached connections to prevent resource exhaustion (i.e. file
  // descriptors on the client and server).  When the limit is
  // reached, the least recently used connection will be closed.  This
  // is primarily useful for MultiSRPC instances used when talking to
  // many different servers.  Most limiting methods use an argument
  // which is provided to the constructor.
  enum connLimitKind
    {
      // Default behavior: don't limit the number of cached
      // connections.
      noLimit = 0,

      // Limit based on the maximum number of connections that have
      // actively been in use at once.  Specifically, the limit will
      // be the maximum number that have been in use times the
      // argument, plus 1.  
      maxUsedLimit,

      // Limit to a specific number (the argument provided to the
      // constructor).
      fixedLimit,

      // Limit to a fraction of the file descriptors available to the
      // process.  The available file descriptors will be divided by
      // the argument.
      descripFracLimit
    };

  // Constructors
  MultiSRPC(connLimitKind c_limit = noLimit, int c_limit_arg = 1);
  /* Make a new, empty cache for connections to servers.  The policy
     for limiting the number of cached connections is specified by
     "c_limit".  The exact behavior of the limit is usually controlled
     by "c_limit_arg". */

  // Destructor
  ~MultiSRPC();

  typedef int ConnId;
  /* A connection identifier is ``valid'' iff it is non-negative. */

  // Request a new SRPC connection to a particular server
  ConnId Start(const Text &hostname, const Text &interface, SRPC*& srpc)
    throw(SRPC::failure);
  /* Return the valid identifier of a dedicated SRPC connection to a server
     for the specified interface on the specified hostname, and set "srpc"
     to a pointer to the underlying connection.  When done with the
     connection, either call "End" to make the connection available to other
     clients, or call "Discard" to cause the connection to be closed.

     Throws "SRPC::failure" if "hostname" is an unknown host or if
     that host is not currently exporting the specified
     "interface". */

  // Finish using a connection
  void End(ConnId id) throw();
  /* Make the connection "id" available to other clients. If "id" is invalid,
     this is a no-op. Otherwise, "id" must be a (valid) connection identifier
     returned by an invocation of the "Start" method above. */

  /* Usage Note: Since "End" is a no-op when "id" is an invalid connection
     identifier, no tests are required to handle the case where "Start" throws
     "SRPC::failure". Instead, clients can write code like this:

       MultiSRPC::ConnId id = -1;
       try {
	   SRPC *srpc;
	   id = multi.Start(hostname, srpc);
	   // use "srpc" connection here
	   multi.End(id);
       } catch (SRPC::failure) {
           multi.End(id);
	   // handle failure
       }
  */

  void Discard(ConnId id) throw();
  /* Close the connection "id" and free the resources it consumed. Call this
     instead of End if you know the connection will not be useful to another
     client in the future.  If "id" is invalid, this is a no-op. */

  void Purge(const Text &hostname, const Text &interface) throw(SRPC::failure);
  /* If there are any cached SRPC connections to the specified interface on 
     the specified hostname that are not in use, close and discard them. */

  // Data structure for recording and reporting statistics about a
  // MultiSRPC instance.
  struct Stats
  {
    // (Why a 64-bit integer?  Some programs can open and close a lot
    // of connections.  Thousands per second is unusual but not
    // impossible.  Programs, particularly daemons, can run for a
    // months at a time.  This makes passing 2^32 possible.)
    Basics::uint64

      // Number of connections opened
      opened,

      // Number of connections closed because they were dead
      // (e.g. other end closed the connection).
      closed_dead,

      // Number of connections closed by Discard
      closed_discard,

      // Number of connections closed by Purge
      closed_purge,

      // Number of connections closed because they were idle for a
      // while
      closed_idle,

      // Number of connections closed because we had too many open
      // unused connections
      closed_limit;

    // Max number of connections concurrently in use
    Basics::uint32 max_in_use;

    Stats()
      : opened(0), max_in_use(0),
	closed_dead(0), closed_discard(0),
	closed_purge(0), closed_idle(0)
    {
    }
  };

  Stats GetStats() throw();
  /* Get statistics about this MultiSRPC. */

  // A helper class to make sure a connection is returned (by calling
  // End) even if an exception is thrown.  Instantiate it in a local
  // scope instead of calling Start, and the connection will be
  // returned when leaving that scope.
  class Connection
  {
  private:
    // The MultiSRPC we need to return the connection to
    MultiSRPC &multi;
    // The connection ID
    ConnId id;

    // Hide the copy constructor
    Connection(const Connection&other)
      : multi(other.multi), id(-1)
    { }
  public:
    // The constructor calls Start for you, so it takes the same
    // arguments
    Connection(MultiSRPC &multi,
	       const Text &hostname, const Text &interface,
	       SRPC*& srpc)
      : multi(multi), id(-1)
    {
      id = multi.Start(hostname, interface, srpc);
    }
    ~Connection()
    {
      // This is the important part.  Note that we don't check whether
      // id is valid, as End does that.
      multi.End(id);
    }
    // The connection can be explicitly released or discarded with
    // these
    void End() throw()
    {
      multi.End(id);
      id = -1;
    }
    void Discard() throw()
    {
      multi.Discard(id);
      id = -1;
    }
  };

private:
  // The connections are stored in a table indexed by server host name.
  // The entire table is protected by a mutex.
  Basics::mutex mu;

  // For now, the table is a simple array of entries. If the "srpc" entry is
  // NULL, then a failure was detected on some previous SRPC connection that
  // used this entry in the table, so the entry is available for a new
  // connection.
  struct Entry {
      Text hostname;
      Text interface;           // empty iff intf is non-empty
      bool inUse;		// connection in use?
      bool idle;		// has gone unused for some time?
      SRPC *srpc;		// NULL means this entry is not used

      // LRU linked list pointers (actually indexes)
      ConnId lru_prev, lru_next;

      // default constructor/destructor
      Entry() throw ()
	: inUse(false), idle(false), srpc(0),
	  lru_prev(-1), lru_next(-1)
        { /*SKIP*/ }
      ~Entry() throw ();
  };
  typedef Entry *EntryPtr;

  // The "tbl" is capable of holding "num" Entry's. The current entries are
  // "tbl[0]".."tbl[next-1]". All entries "tbl[next]".."tbl[num-1]" are NULL.
  int next, num;
  EntryPtr *tbl;

  // LRU list management used for connection limiting.
  ConnId lru_head, lru_tail;    // List head and tail
  int lru_count;                // Count of entries on the LRU list 
  void lru_splice(ConnId id);   // Remove an entry from the LRU list
  void lru_add(ConnId id);      // Add an entry to the head of the LRU list
  connLimitKind lru_lim_kind;   // Kind of connection limiting to use
  int lru_lim_arg;              // Argument that controls the limit
  int lru_limit();              // Function that returns the current
				// numeric limit

  // Statistics about this instance
  Stats stats;

  // Background thread which closes idle connections
  Basics::thread close_idle_thread;
  // Body of the thread.
  void close_idle_body() throw();
  // Function needed to start the thread.
  static void *close_idle_start(void *arg) throw ();

  // We signal this condition variable when the MultiSRPC is being
  // destroyed to wake up the thread and tell it to stop.
  Basics::cond close_idle_stop;
};

#endif // _MULTI_SRPC_H
