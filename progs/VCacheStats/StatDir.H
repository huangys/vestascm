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

// Last modified on Mon Nov 10 17:35:20 EST 2003 by ken@xorian.net
//      modified on Fri Aug  8 19:53:51 PDT 1997 by heydon

/* StatDir.H

   Defines objects and iterators at levels 4 and above of the stable cache
   hierarchy, namely, at the level of directories of either MPKFiles or sub-
   directories.
*/

#ifndef _STAT_DIR_H
#define _STAT_DIR_H

#include <sys/types.h>
#include <dirent.h>
#include <Basics.H>
#include <FS.H>
#include "StatError.H"
#include "StatDirEntry.H"
#include "StatCount.H"
#include "StatCollection.H"

class DirIter; // defined below

class DirObj : public DirEntry {
  public:
    DirObj(const Text& path) throw ()
      : DirEntry(DirEntry::Directory), path(path) { /*SKIP*/ }
    /* Create a new DirObj for the directory name "path". */

    int Search(int verbose, /*INOUT*/ Stat::Collection &stats)
      throw (StatError::UnevenLevels,
	     StatError::BadMPKFile, StatError::EndOfFile,
	     FS::Failure, FS::DoesNotExist);
    /* Search this DirObj, returning the height of the DirObj
       in the search tree. Update "entryStats" with statistical
       values at the leaves of the subtree rooted at this DirObj;
       update "fanout" at all levels at and below this DirObj with
       the fanout of each object in the subtree. */

  private:
    Text path; // pathname of the directory
    friend class DirIter;
};

class DirIter {
  public:
    DirIter(const DirObj *dirObj) throw (FS::Failure, FS::DoesNotExist);
    /* Initialize this iterator on the DirObj "dir". */

    bool Next(/*OUT*/ DirEntry* &entry)
      throw (StatError::EndOfFile, FS::Failure, FS::DoesNotExist);
    /* If the iterator is not finished, set "entry" to point to the next
       "DirEntry" in it and return true. Otherwise, return false. "entry"
       will actually point to one of the subtypes of "DirEntry", namely,
       "DirObj" or "MPKFileObj". The two types can be distinguished by
       the value of their common "kind" field. */

  private:
    Text path;			// pathname for this directory ('/' at end)
    bool done;			// indicates if iteration already finished
    DIR *dir;			// underlying directory
};

#endif // _STAT_DIR_H
