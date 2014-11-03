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

/* A lease is an abstract object containing a timestamp and named by a
   non-negative integer. A "Leases" object is a set of leases together with a
   background thread that automatically removes lease objects from the set
   whose timestamps are older than some fixed amount of time before the
   current time. It provides operations for creating a new lease, renewing the
   timestamp of an existing lease, testing for the existence of a lease, etc.

   Each "Leases" object has an associated mutex (passed to the constructor)
   that must be used as a monitor on the calls of its methods. This mutex is
   denoted in "REQUIRES" clauses below by "mu[SELF]". When a method below
   "REQUIRES mu[SELF] IN LL", the mutex associated with a "Leases" object must
   be held by the thread invoking the method. The mutex is needed to
   synchronize with the background thread that periodically expires leases. */

#ifndef _LEASES_H
#define _LEASES_H

#include <Basics.H>
#include <BitVector.H>

class Leases {
  public:
    class NoLease {};

    Leases(Basics::mutex *mu, int timeout, bool debug = false) throw ();
    /* REQUIRES *mu IN LL */
    /* Create a new "Leases" object protected by the mutex "*mu" whose leases
       time out after at least "timeout" seconds. The mutex "*mu" is said to
       be the mutex associated with this "Leases" object, and is denoted by
       "mu[SELF]" in the "REQUIRES" clauses below. If "debug" is true, then a
       debugging message is written to stdout when the background thread
       expires leases. By default, lease expiration is enabled for a new
       "Leases" object. */

    BitVector *LeaseSet() const throw ();
    /* REQUIRES mu[SELF] IN LL */
    /* Return a newly-allocated bit vector whose bits correspond to the
       indices of leased cache entries. */

    void NewLease(unsigned int li) throw () { (void) newLs->Set(li); }
    /* REQUIRES mu[SELF] IN LL */
    /* Take out a new lease on the lease object with index "li". */

    void RenewLease(unsigned int li) throw (NoLease)
	{ if (!IsLeased(li)) throw NoLease(); NewLease(li); }
    /* REQUIRES mu[SELF] IN LL */
    /* Renew the lease of the lease object "li"; "NoLease" is thrown if "li"
       does not presently have a lease, in which case, its lease is not
       renewed. */

    bool IsLeased(unsigned int li) throw ()
	{ return (newLs->Read(li) || oldLs->Read(li)); }
    /* REQUIRES mu[SELF] IN LL */
    /* Return "true" iff "li" is leased. */

    void DisableExpiration() throw () { expiring = false; }
    /* REQUIRES mu[SELF] IN LL */
    /* Disable lease expiration. No leases will expire until
       "EnableExpiration" is called. */

    void EnableExpiration() throw () { expiring = true; }
    /* REQUIRES mu[SELF] IN LL */
    /* Re-enable lease expiration. */

    bool ExpirationIsEnabled() const throw() { return expiring; }
    /* REQUIRES mu[SELF] IN LL */
    /* Return true iff lease expiration is enabled (i.e., if lease
       expiration is *not* frozen). */

    void Debug(std::ostream &os) const throw ();
    /* REQUIRES mu[SELF] IN LL */
    /* Write debugging information for the leases to "os". The output is
       indented and ends with a newline. */

  private:
    // read-only after initialization
    int timeout;              // lease time (in seconds)
    bool debug;		      // print debugging info at lease expiration?

    // data fields
    Basics::mutex *mu;	      // protects following fields
    bool expiring;	      // is lease expiration enabled?
    BitVector *oldLs, *newLs; // old and new lease sets

    // methods
    void Expire() throw ();
    /* REQUIRES Sup(LL) < mu[SELF] */
    /* Expire the old leases, make the new leases old, and reset the new
       leases. */

    // friends
    friend void* Leases_TimeoutProc(void*) throw ();
    /* REQUIRES Sup(LL) < mu[SELF] */

    // hide copy constructor from clients
    Leases(const Leases&);
};

#endif // _LEASES_H
