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

// Created on Mon Oct  6 14:22:08 PDT 1997 by heydon
// Last modified on Mon May 23 22:28:27 EDT 2005 by ken@xorian.net  
//      modified on Sat Feb 12 13:14:08 PST 2000 by mann  
//      modified on Mon Oct  6 14:47:22 PDT 1997 by heydon

#include <Basics.H>
#include <Sequence.H>
#include "FlushQueue.H"

void FlushQueue::Enqueue() throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    if (this->numRunning > 0 || this->seq.size() > 0) {
	Basics::cond c;
	this->seq.addlo(&c);
	while (this->numRunning > 0 || this->seq.gethi() != &c) {
	    c.wait(*(this->mu));
	}
	(void) this->seq.remhi();
    }
    assert(this->numRunning == 0);
    this->numRunning++;
}

void FlushQueue::Dequeue() throw ()
/* REQUIRES Sup(LL) = SELF.mu */
{
    assert(this->numRunning == 1);
    this->numRunning--;
    if (this->seq.size() > 0) {
	this->seq.gethi()->signal();
    }
}
