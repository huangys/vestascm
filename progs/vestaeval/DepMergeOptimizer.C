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

#include "DepMergeOptimizer.H"
#include "Val.H"

DPaths *DepMergeOptimizer::maybe_union(DPaths *from, DPaths *into, bool allocate)
{
  // Short-cut: no new dependencies, nothing to do.
  if(from == 0 || from->Empty())
    return into;

  DPaths_set_t *previous_into = 0;
  if(already_merged.Get((PointerInt) from, previous_into))
    {
      assert(previous_into != 0);
      for(int i = 0; i < enclosing_vals.size(); i++)
	{
	  if(previous_into->count(enclosing_vals.get(i)->dps) > 0)
	    {
	      return into;
	    }
	}

      if((into != 0) && (previous_into->count(into) > 0))
	{
	  return into;
	}
    }

  DPaths *result = 0;
  if(into == 0)
    {
      // If we're not supposed to allocate a new one, we're done.
      if(!allocate)
	return 0;

      // The caller is allowed to pass in a NULL "into" set and will
      // get back a new set that's a copy of the "from" set.
      result = NEW_CONSTR(DPaths, (from->Size()));
      result = result->Union(from);
      into = result;
    }
  else
    {
      result = into->Union(from);
    }

  if(previous_into == 0)
    {
      previous_into = NEW(DPaths_set_t);
      (void) already_merged.Put((PointerInt) from, previous_into);
    }
  previous_into->insert(into);

  return result;
}
