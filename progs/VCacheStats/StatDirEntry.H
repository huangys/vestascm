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

// Last modified on Mon Nov 10 17:35:09 EST 2003 by ken@xorian.net
//      modified on Fri Aug  8 19:52:15 PDT 1997 by heydon

/* StatDirEntry.H - Defines an object type intended to be subclassed to
   represent an entry in a directory. The class has a "kind" field indicating
   the type of the entry. */

#ifndef _STAT_DIR_ENTRY_H
#define _STAT_DIR_ENTRY_H

#include <sys/types.h>
#include <sys/stat.h>
#include <Basics.H>
#include <FS.H>
#include "StatError.H"
#include "StatCount.H"
#include "StatCollection.H"

class DirEntry {
  public:
    enum Kind { Directory, File };
    Kind kind;

    // constructor
    DirEntry(Kind kind) throw () : kind(kind) { /*SKIP*/ }

    // search
    virtual int Search(int verbose, /*INOUT*/ Stat::Collection &stats)
      throw (StatError::UnevenLevels,
	     StatError::BadMPKFile, StatError::EndOfFile,
	     FS::Failure, FS::DoesNotExist);

    static DirEntry *Open(const Text &path, struct stat *statBuff = NULL)
      throw (StatError::EndOfFile, FS::Failure, FS::DoesNotExist);
    /* Open "path" and return the resulting "DirEntry", which will either
       be a "DirObj" or a "MPKFileObj". If "statBuff" is non-NULL, it points
       to the "struct stat" structure for "path". */

    static void FSStat(const Text &path, /*OUT*/ struct stat *buffer)
      throw (FS::Failure, FS::DoesNotExist);
};

#endif // _STAT_DIR_ENTRY_H
