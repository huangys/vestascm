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

// Last modified on Fri Apr 29 00:26:30 EDT 2005 by ken@xorian.net        
//      modified on Mon Jul 15 17:10:29 EDT 2002 by kcschalk@shr.intel.com
//      modified on Fri Mar 28 17:13:18 PST 1997 by heydon

/* StatPKFile.H 

   Defines objects and iterators at level 2 of the stable cache hierarchy,
   namely, at the level of sets of cache entries with the same primary
   key (PK). Abstractly, a "PKFileObj" is a collection of "CFPObj" objects.
*/

#ifndef _STAT_PKFILE_H
#define _STAT_PKFILE_H

#include <Basics.H>
#include "SPKFileRep.H"
#include "SMultiPKFileRep.H"
#include "SPKFile.H"
#include "StatCFP.H"
#include "StatCollection.H"

class PKFileIter; // defined below

class PKFileObj {
  public:
    PKFileObj(const Stat::Location &parent_loc,
	      const SMultiPKFileRep::HeaderEntry *he) throw ()
      : len(he->pkLen), pkHdr(he->pkhdr), pkFile(he->pkfile)
    {
      loc = NEW_CONSTR(Stat::Location,
		       (parent_loc.add_pk(pkFile->PK(),
					  pkFile->SourceFunc())));
    }
    /* Create a new "PKFileObj" from the MultiPKFile header entry "he". */

    int Search(int verbose, /*INOUT*/ Stat::Collection &stats) const throw ();
    /* Search this PKFileObj, returning the height of the PKFileObj
       in the search tree (i.e., 2). Update "entryStats" with statistical
       values at the leaves of the subtree rooted at this PKFileObj;
       update "fanout" at all levels at and below this PKFileObj with
       the fanout of each object in the subtree. */

  private:
    int len;
    const SPKFileRep::Header *pkHdr;
    const SPKFile *pkFile;
    Stat::Location *loc;

    friend class PKFileIter;
};

class PKFileIter {
  public:
    PKFileIter(const PKFileObj *pkf) throw ();
    /* Initialize this iterator on the PKFileObj "pkf". */

    bool Next(/*OUT*/ CFPObj* &cfp) throw ();
    /* If the iterator is not finished, set "cfp" to point to the next
       "CFPObj" in it and return true. Otherwise, return false. */

  private:
    const PKFileObj* pkf;
    const SPKFileRep::Header *pkHdr;
    const SPKFile::CFPEntryMap *cfpEntryMap;
    int cfpIndex;
};

#endif // _STAT_PKFILE_H
