// Copyright (C) 2005, Vesta free software project
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

// ShortIdRefCount.C

#include "ShortIdRefCount.H"
#include "logging.H"

bool ShortIdRefCount::Compare(const ShortIdRefCount &other)
{
  // Make a copy of the other table.  We'll remove entries from them
  // as we process them.
  ShortIdRefCount work(other);

  // Do the counts match?
  bool match = true;

  // Temp vars used while iterating over the two tables
  ShortIdKey sidkey;
  int count, ocount;

  // First pass: iterate over this .
  {
    ShortIdRefCount::Iterator self_it(this);
    while(self_it.Next(sidkey, count))
      {
	ocount = work.GetCount(sidkey.sid);
	if(count != ocount)
	  {
	    match = false;
	    Repos::dprintf(DBG_ALWAYS, 
			   "shortid refcount mismatch on 0x%08x: %d vs. %d\n",
			   sidkey.sid, count, ocount);
	  }

	// We're done with this shortid, so remove it
	(void) work.Delete(sidkey, count, false);
      }
  }

  // Second pass: itetrate over anything left in the other table
  {
    ShortIdRefCount::Iterator work_it(&work);
    while(work_it.Next(sidkey, ocount))
      {
	count = this->GetCount(sidkey.sid);
	// Since we've already processed every entry in this table,
	// this must be a shortid not present in this table, and thus
	// must have a count of zero.
	assert(count == 0);
	if(count != ocount)
	  {
	    match = false;
	    Repos::dprintf(DBG_ALWAYS, 
			   "shortid refcount mismatch on 0x%08x: %d vs. %d\n",
			   sidkey.sid, count, ocount);
	  }
      }
  }

  // Indicate to the caller that we have a match.
  return match;
}
