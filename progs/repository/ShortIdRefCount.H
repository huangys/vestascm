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

//
// ShortIdRefCount.H
//
// Reference counts for shortids in a volatile directory tree
//

#ifndef _SID_REF_COUNT
#define _SID_REF_COUNT

#include "Basics.H"
#include "Table.H"
#include "ShortIdKey.H"

class ShortIdRefCount : public Table<ShortIdKey, int>::Default
{
public:
  typedef Table<ShortIdKey, int>::Iterator Iterator;

  ShortIdRefCount() : Table<ShortIdKey, int>::Default() { }

  // Avoid the need for the caller to create a ShortIdKey.  Optioanlly
  // provide a default value if no reference is found.  (In some cases
  // 0 is desired, and in others 1 is.)
  inline int GetCount(ShortId sid, int count = 0)
  {
    ShortIdKey sidkey(sid);
    bool found = this->Get(sidkey, count);
    assert(!found || (count > 0));
    return count;
  }

  // Increment the reference count for "sid", returning its new count.
  inline int Increment(ShortId sid)
  {
    ShortIdKey sidkey(sid);
    int count;
    if(this->Get(sidkey, count))
      {
	assert(count > 0);
	count++;
      }
    else
      count = 1;
    this->Put(sidkey, count);
    return count;
  }

  // Decrement the reference count for "sid", returning its new count.
  // If "missing_ok" is false we assert that "sid" must already have
  // an entry in the table.
  inline int Decrement(ShortId sid, bool missing_ok)
  {
    ShortIdKey sidkey(sid);
    int count = 0;
    bool present = this->Get(sidkey, count);
    assert(missing_ok || present);
    if(present)
      {
	assert(count > 0);
	count--;
	if(count > 0)
	  this->Put(sidkey, count);
	else
	  // Note that we don't resize when we delete an entry.  This
	  // means that we never shrink the table, which should result
	  // in fewer overall table resizes.  This is good, since they
	  // can be expensive and happen while locks are held.
	  this->Delete(sidkey, count, false);
	return count;
      }
    return 0;
  }

  // Compare with another reference counter, returning true if the two
  // match exactly.  If there are any mis-matches, messages will be
  // printed.  (This is intended for paranoid double-checking used
  // during development and testing.)
  bool Compare(const ShortIdRefCount &other);

private:
  ShortIdRefCount(const ShortIdRefCount &other) : Table<ShortIdKey, int>::Default(&other) { }
};

#endif
