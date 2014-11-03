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

// Created on Mon Oct  6 14:09:58 PDT 1997 by heydon
// Last modified on Mon May 23 22:28:00 EDT 2005 by ken@xorian.net  
//      modified on Sat Feb 12 13:13:55 PST 2000 by mann  
//      modified on Mon Oct  6 14:41:45 PDT 1997 by heydon

// FlushQueue.H -- an object for synchronizing flusher threads

#ifndef _FLUSH_QUEUE_H
#define _FLUSH_QUEUE_H

#include <Basics.H>
#include <Sequence.H>

class FlushQueue {
  public:
    FlushQueue(Basics::mutex *mu) throw () : mu(mu), numRunning(0) { /*SKIP*/ }
    /* Initialize the queue to be protected by the mutex "mu". This mutex is
       denoted in the method preconditions below by "SELF.mu". */

    void Enqueue() throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Add the calling thread to this queue. The thread will block until the
       threads ahead of it in the queue have been dequeued. */

    void Dequeue() throw ();
    /* REQUIRES Sup(LL) = SELF.mu */
    /* Signal the next thread in the queue that it can run. */

  private:
    typedef Sequence<Basics::cond*> CondSeq;

    Basics::mutex *mu;
    int numRunning;
    CondSeq seq;

    /* The field "numRunning" counts the number of threads that have returned
       from "Enqueue" but that have yet to call "Dequeue". It will always be
       either 0 or 1, so it could have been represented by the type "bool",
       but representing it by an integer allows additional assertion checking.
    */
};

#endif // _FLUSH_QUEUE_H
