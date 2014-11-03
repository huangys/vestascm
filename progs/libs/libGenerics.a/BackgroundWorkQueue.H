// Copyright (C) 2008, Vesta Free Software Project
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

#ifndef BACKGROUNDWORKQUEUE_H
#define BACKGROUNDWORKQUEUE_H

#include <Basics.H>
#include <Sequence.H>

namespace Generics
{
  // A simple class that creates a background thread for processing
  // units of work one at a time.  Other threads can queue work to be
  // done.

  template<class Work,
	   bool work_ptrfree = false>
  class BackgroundWorkQueue
  {
  private:
    // The quque of work to do
    Sequence<Work, work_ptrfree> to_do;

    // Mutex protecting the queue of work
    Basics::mutex mu;

    // Condition variable signaled when work is available, or when the
    // background thread should shut down
    Basics::cond cond;

  protected:
    // Function that does the work.  Must be defined by sub-classes.
    virtual void do_work(Work w) throw() = 0;

  private:
    // The background thread which does the work
    Basics::thread thread;

    // "running" becomes true once the thread has been started.
    // "shut_down" becomes true when the background thread should exit
    // (because instance is being destroyed)
    bool running, shut_down;

    // The main body of the background thread
    void thread_body()
    {
      // Note: no mutex used for shut_down.  It only ever makes a single
      // transition from false to true, so we don't bother with locking.
      while(!this->shut_down)
	{
	  this->mu.lock();
	  while((this->to_do.size() < 1) &&
		// We may get woken up to shut down
		!this->shut_down)
	    {
	      this->cond.wait(this->mu);
	    }
	  this->mu.unlock();
	  // If we were woken up to shut down
	  if(this->shut_down) break;

	  // Do one unit of work
	  Work w = to_do.remlo();
	  this->do_work(w);
	}
    }

    // thread start wrapper needed for the pthreads library
    static void *thread_start(void *arg) throw()
    {
      BackgroundWorkQueue<Work,work_ptrfree> *self =
	(BackgroundWorkQueue<Work,work_ptrfree> *) arg;
      self->thread_body();
      return 0;
    }

  public:
    // Start the background thread right away if "start" is true.
    BackgroundWorkQueue(bool start = true)
      : running(false), shut_down(false)
    {
      if(start) run();
    }

    // Start the thread running in the background.
    void run() throw()
    {
      if(!running)
	{
	  running = true;
	  thread.fork(BackgroundWorkQueue<Work,work_ptrfree>::thread_start, (void *) this);
	}
    }

    ~BackgroundWorkQueue()
    {
      // If the background thread is running...
      if(running)
	{
	  // ...tell it to exit
	  shut_down = true;
	  // ...make sure it wakes up
	  cond.signal();
	  // ...and wait for it to do so.
	  thread.join();
	}
    }

    // Queue a unit of work to be done
    void queue(const Work &w)
    {
      mu.lock();
      to_do.addhi(w);
      cond.signal();
      mu.unlock();
    }
  };
}

#endif
