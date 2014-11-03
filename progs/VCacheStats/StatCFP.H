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

// Last modified on Mon Nov 10 17:34:26 EST 2003 by ken@xorian.net
//      modified on Fri May 23 15:43:29 PDT 1997 by heydon

/* StatCFP.H 

   Defines objects and iterators at level 1 of the stable cache hierarchy,
   namely, at the level of sets of cache entries with a common fingerprint
   (CFP). Abstractly, a "CFPObj" is a collection of cache entries.
*/

#ifndef _STAT_CFP_H
#define _STAT_CFP_H

#include <Basics.H>
#include <FP.H>
#include "SPKFile.H"
#include "CacheEntry.H"
#include "StatCount.H"
#include "StatCollection.H"

class CFPIter; // defined below

class CFPObj {
  public:
    CFPObj(Stat::Location *loc, const SPKFile *pkFile,
	   FP::Tag cfp, CE::List *entries) throw ()
      : loc(loc), pkFile(pkFile), cfp(cfp), entries(entries) { /*SKIP*/ }
    /* Initialize this CFPObj for the common fingerprint "cfp" with
       corresponding cache entries "entries". */

    int Search(int verbose, /*INOUT*/ Stat::Collection &stats) const throw ();
    /* Search this CFPObj, returning the height of the CFPObj in the search
       tree (i.e., 1). Update "entryStats" with statistical values at the
       leaves of the subtree rooted at this CFPObj; update "fanout" at all
       levels at and below this CFPObj with the fanout of each object in the
       subtree. */

  private:
    // The PKFile we belong to.  Used for access to free variable
    // names.
    const SPKFile *pkFile;

    FP::Tag cfp;
    CE::List *entries;
    Stat::Location *loc;

    friend class CFPIter;
};

class CFPIter {
  public:
    CFPIter(const CFPObj *cfp) throw ()
	: next(cfp->entries) { /*SKIP*/ }
    /* Initialize this iterator on the CFPObj "cfp". */

    bool Next(/*OUT*/ CE::T* &ce) throw ();
    /* If the iterator is not finished, set "ce" to point to the next cache
       entry in it and return true. Otherwise, return false. */

  private:
    CE::List *next;
};

#endif // _STAT_CFP_H
