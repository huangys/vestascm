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

// Last modified on Fri Apr 29 00:25:06 EDT 2005 by ken@xorian.net
//      modified on Fri Aug  8 19:53:51 PDT 1997 by heydon

/* StatMPKFile.H 

   Defines objects and iterators at level 3 of the stable cache hierarchy,
   namely, at the level of sets of cache entries with the same primary key
   prefix (PKPrefix). Abstractly, a "MPKFileObj" is a collection of PKFiles.
*/

#ifndef _STAT_MPKFILE_H
#define _STAT_MPKFILE_H

#include <Basics.H>
#include <FS.H>
#include "SMultiPKFileRep.H"
#include "StatError.H"
#include "StatCount.H"
#include "StatDirEntry.H"
#include "StatPKFile.H"
#include "StatCollection.H"

class MPKFileIter; // defined below

class MPKFileObj : public DirEntry {
  public:
    MPKFileObj(const Text& fname) throw ()
      : DirEntry(DirEntry::File), fname(fname)
    {
      loc = NEW_CONSTR(Stat::Location, (fname));
    }
    /* Create a new MPKFileObj for the MultiPKFile named "fname". */

    int Search(int verbose, /*INOUT*/ Stat::Collection &stats)
      throw (StatError::BadMPKFile,
	     StatError::EndOfFile, FS::Failure, FS::DoesNotExist);
    /* Search this MPKFileObj, returning the height of the MPKFileObj
       in the search tree (i.e., 3). Update "entryStats" with statistical
       values at the leaves of the subtree rooted at this MPKFileObj;
       update "fanout" at all levels at and below this MPKFileObj with
       the fanout of each object in the subtree. */

  private:
    Text fname;			   // name of the MultiPKFile
    Stat::Location *loc;
    SMultiPKFileRep::Header *hdr;  // contents of this MultiPKFile
    friend class MPKFileIter;
};

class MPKFileIter {
  public:
    MPKFileIter(const MPKFileObj *mpkf) throw ()
	: mpkf(mpkf), pkIndex(0) { /*SKIP*/ }
    /* Initialize this iterator on the MPKFileObj "mpkf". */

    bool Next(/*OUT*/ PKFileObj* &pkf) throw ();
    /* If the iterator is not finished, set "pkf" to point to the next
       "PKFileObj" in it and return true. Otherwise, return false. */

  private:
    const MPKFileObj *mpkf;	// object to iterate over
    int pkIndex;		// index into "this->mpkf->hdr->pkSeq"
};

#endif // _STAT_MPKFILE_H
