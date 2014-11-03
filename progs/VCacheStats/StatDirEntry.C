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

// Last modified on Fri Apr 29 00:24:45 EDT 2005 by ken@xorian.net  
//      modified on Sat Feb 12 13:10:05 PST 2000 by mann  
//      modified on Fri Aug  8 19:53:52 PDT 1997 by heydon

#include <sys/types.h>
#include <sys/stat.h>
#if defined(__digital__)
#include <sys/mode.h>
#endif
#include <Basics.H>
#include <FS.H>
#include "StatDirEntry.H"
#include "StatMPKFile.H"
#include "StatDir.H"

int DirEntry::Search(int verbose, /*INOUT*/ Stat::Collection &stats) 
  throw (StatError::UnevenLevels, StatError::BadMPKFile, StatError::EndOfFile,
	 FS::Failure, FS::DoesNotExist)
{
    assert(false);
    return 0; // make compiler happy
}

void DirEntry::FSStat(const Text &path, /*OUT*/ struct stat *buffer)
  throw (FS::Failure, FS::DoesNotExist)
{
    if (stat(path.cchars(), buffer) != 0) {
	if (errno == ENOENT) throw FS::DoesNotExist();
	else throw FS::Failure(Text("stat"), path);
    }
}

DirEntry *DirEntry::Open(const Text &path, struct stat *statBuff)
  throw (StatError::EndOfFile, FS::Failure, FS::DoesNotExist)
{
    DirEntry *res;
    struct stat buffer;
    if (statBuff == (struct stat *)NULL) {
	statBuff = &buffer;
	FSStat(path, /*OUT*/ statBuff);
    }
    if (S_ISDIR(statBuff->st_mode)) {
      res = NEW_CONSTR(DirObj, (path));
    } else {
      assert(S_ISREG(statBuff->st_mode));
      res = NEW_CONSTR(MPKFileObj, (path));
    }
    return res;
}
