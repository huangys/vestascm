// Copyright (C) 2007, Vesta Free Software Project
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
#include <Table.H>

namespace Generics
{
  // A table to keep from performing identical work in parallel.
  // This checks for a key and waits on a condition if that key
  // is already in progress.

  // "Key" must be a type usable as a key with the Table template
  // class (i.e. it mush have a Hash member function).

  // "Annotation" is a type used to pass information to the virtual
  // functions "BeforeWaiting" and "AfterWaiting".  It can be any type
  // useful for helping those functions (e.g. a text string to be
  // included in a message about the fact that one thread is waiting
  // on work being done by another thread).

  template <class Key, class Annotation>
  class WaitDuplicateTable
  {
  private:
    // Class used for one key.  Threads need only wait on the condition
    // for the key they are interested in.
    class rpt_cond {
      Basics::cond cond;
      Basics::mutex &mu;
      int count; // number of in progress calls with this PK
      bool dec_and_del() {
	assert(count > 0);
	if(--count == 0) {
	  delete this;
	  return false;
	} else {
	  return true;
	}
      };
    public:
      const Annotation info;
      rpt_cond(Basics::mutex &mu, const Annotation &info)
	: mu(mu), count(1), info(info)
      { }
      void wait() {
	count++;
	cond.wait(mu);
	(void) dec_and_del();
      };
      void wake() {
	if(dec_and_del()) {
	  cond.broadcast();
	}
      };
    };

    // Protects the table and rpt_cond instances
    Basics::mutex mu;

    // Table of work in progress
    typename Table<Key, rpt_cond *>::Default tbl;

  protected:

    virtual void BeforeWaiting(const Key &key, const Annotation &info)
    {
      // Override in a sub-class to take some action (e.g. print a
      // messgae) just before we start waiting for another thread to
      // finish the work
    }

    virtual void AfterWaiting(const Key &key, const Annotation &info)
    {
      // Override in a sub-class to take some action when we we stop
      // waiting because another thread has finished the work
    }

  public:

    // Checks "tbl" and sleeps if another thread is currently working
    // on the same key.  Returns true if another thread might have
    // done the work for us, false if we need to do the work ourself.
    // When the result is false, "info" is stored and is passed to the
    // BeforeWaiting/AfterWaiting functions in any later threads that
    // wait on the same key.  When the result is true, "info" is set
    // to the annotation of the thread which may have completed the
    // work.
    bool WaitForDuplicate(const Key &key, /*IN/OUT*/ Annotation &info) {
      rpt_cond *rptCond = 0;
      mu.lock();
      bool found = tbl.Get(key, /*OUT*/ rptCond);
      if(!found) {
	rptCond = NEW_PTRFREE_CONSTR(rpt_cond, (this->mu, info));
	tbl.Put(key, rptCond);
	mu.unlock();
	return false;
      } else {
	//wait
	BeforeWaiting(key, rptCond->info);
	assert(rptCond != 0);
	rptCond->wait();
	//woken up.  the other run must have finished
	mu.unlock();
	AfterWaiting(key, rptCond->info);
	info = rptCond->info;
	return true;
      }
    }
  
    // After the work is done, wake up any remaining threads that
    // might be waiting for it.
    void WakeWaiting(const Key &key) {
      rpt_cond *rptCond = 0;
      mu.lock();
      bool found = tbl.Delete(key, /*OUT*/ rptCond);
      assert(found);
      assert(rptCond != 0);
      rptCond->wake();
      mu.unlock();
    }
  };

}
