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

#if !defined(_DEPMERGEOPTIMIZER_H_)
#define _DEPMERGEOPTIMIZER_H_

#include <assert.h>
#include <set>
#include <BasicsAllocator.H>
#include <Sequence.H>
#include <Table.H>
#include <SimpleKey.H>
#include "Dep.H"

// This class is used to avoid wasting time and memory mergeing the
// same set of dependencies into multiple other sets.

class DepMergeOptimizer
{
private:
  typedef std::set<DPaths *,
		   std::less<DPaths *>,
		   Basics::Allocator<DPaths *> > DPaths_set_t;
  typedef SimpleKey<PointerInt> DPaths_key_t;

  // The keys of this table are dependency sets which have been merged
  // into other dependency sets.  The value is the set of other
  // dependency sets which the key has been merged into.
  Table<DPaths_key_t, DPaths_set_t *>::Default already_merged;

  // When recursing into sub-values of composite values, this records
  // the enclosing values.  This makes it possible to avoid merging a
  // dependency set into a sub-value when it has already been merged
  // into the dependency set of an enclosing value.
  Sequence<Val> enclosing_vals;

public:
  // Use these when recursing for sub-values of composite values to
  // remember the dependency sets of the enclosing values.  This
  // avoids copying the same dependency sets into both the composite
  // value and its sub-values.
  inline void push_enclosing(Val v)
  {
    assert(v != 0);
    enclosing_vals.addhi(v);
  }
  inline void pop_enclosing(Val v)
  {
    assert(v != 0);
    Val removed = enclosing_vals.remhi();
    assert(removed == v);
  }

  // If the dependency set "from" has not previously been merged into
  // the dependency set "into" or the dependency set of an enclosing
  // composite value, form the union of "into" and "from" and return
  // it.  Otherwise, just return "into".

  // If "from" is a null pointer or an empty set, "into" will simply
  // be returned.

  // If "allocate" is true and "into" is a null pointer, a new set may
  // be allocated and returned that is a copy of the entries in "from"
  // (if "from" hasn't already been merged into the dependency sets of
  // any of the enclosing values).
  DPaths *maybe_union(DPaths *from, DPaths *into, bool allocate = true);
};

#endif
