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
#include <BitVector.H>
#include <Debug.H>

#include "Leases.H"

using std::ostream;
using OS::cio;
using std::endl;

void* Leases_TimeoutProc(void *arg) throw ()
/* REQUIRES Sup(LL) < mu[SELF] */
/* Repeatedly expire leases every "timeout" seconds. */
{
    Leases *leases = (Leases *)arg;

    while (true) {
	// block
	Basics::thread::pause(leases->timeout, 0);

	// expire leases
	leases->Expire();
    }

    //assert(false);  // not reached
    //return (void *)NULL;
}

Leases::Leases(Basics::mutex *mu, int timeout, bool debug) throw ()
/* REQUIRES *mu IN LL */
  : timeout(timeout), debug(debug), mu(mu), expiring(true)
{
  // set object state
  this->oldLs = NEW(BitVector);
  this->newLs = NEW(BitVector);

  // fork and detach background thread
  Basics::thread th;
  th.fork_and_detach(Leases_TimeoutProc, (void *)this);
}

BitVector *Leases::LeaseSet() const throw ()
/* REQUIRES mu[SELF] IN LL */
{
    BitVector *res;
    // initialize "res" to copy of "oldLs"
    res = NEW_CONSTR(BitVector, (this->oldLs));
    *res |= *(this->newLs);	         // OR in "newLs"
    return res;
}

void Leases::Debug(ostream &os) const throw ()
{
    os << "  new = " << *(this->newLs) << endl;
    os << "  old = " << *(this->oldLs) << endl;
}

void Leases::Expire() throw ()
/* REQUIRES Sup(LL) < mu[SELF] */
{
    BitVector *tempLs;

    this->mu->lock();
    if (this->expiring) {
	// debugging
	if (this->debug) {
	  ostream& out_stream = cio().start_out();
	  out_stream << Debug::Timestamp() << "Expiring leases:" << endl;
	  this->Debug(out_stream);
	  out_stream << endl;
	  cio().end_out();
	}

	// Swap old and new, then reset new. This technique
        // saves an allocation
	tempLs = this->oldLs;
	this->oldLs = this->newLs;
	this->newLs = tempLs;
	this->newLs->ResetAll();
    }
    this->mu->unlock();
}
