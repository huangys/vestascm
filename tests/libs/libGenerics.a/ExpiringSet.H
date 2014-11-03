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

#ifndef EXPIRINGSET_H
#define EXPIRINGSET_H

// A set that expires elements after a period of time.  One use would
// be for temporary blacklisting.

// Currently it only has a granularity of seconds.

#include <time.h>
#include <Table.H>

namespace Generics
{
  // Note that Value must be usable as a key in the Table class
  // (i.e. have a Hash member function)

  template<class Value>
  class ExpiringSet
  {
  private:
    // The set is represented by a table.  The values in the table is
    // the expiration time.
    typename Table<Value, time_t>::Default set;

    // This mutex protects the set.
    Basics::mutex mu;

    // Default duration before an elemtn expires.
    unsigned int default_expiry;
  public:
    ExpiringSet(unsigned int def_exp = 60)
      : default_expiry(def_exp)
    {
    }

    void set_default_expiry(unsigned int def_exp)
    {
      default_expiry = def_exp;
    }

    // Add a value to the set with a default duration
    void add(const Value &v, unsigned int expiry)
    {
      time_t when = time(0) + expiry;
      mu.lock();
      set.Put(v, when);
      mu.unlock();
    }

    // Add a value to the set with the defualt expiration
    void add(const Value &v)
    {
      add(v, default_expiry);
    }

    // Check whether the set contains a value
    bool contains(const Value &v)
    {
      bool result = false;
      time_t now = time(0), expiration;
      mu.lock();
      if(set.Get(v, expiration))
	{
	  if(now < expiration)
	    {
	      // It's in the set and hasn't expired yet
	      result = true;
	    }
	  else
	    {
	      // Expired, so drop it from the set
	      set.Delete(v, expiration);
	    }
	}
      mu.unlock();
      return result;
    }
  };
}

#endif
