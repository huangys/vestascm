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


#include <sys/types.h>
#include <sys/stat.h>
#if defined(__digital__)
#include <sys/mode.h>
#endif
#include <dirent.h>
#include <Basics.H>
#include <FS.H>
#include <AtomicFile.H>
#include "StatError.H"
#include "StatDir.H"

using std::cout;
using std::endl;

int DirObj::Search(int verbose, /*INOUT*/ Stat::Collection &stats)
  throw (StatError::UnevenLevels,
	 StatError::BadMPKFile, StatError::EndOfFile,
	 FS::Failure, FS::DoesNotExist)
{
    if (verbose >= 1) {
	cout << "  " << this->path << endl;
    }

    // iterate over children
    int chLevel = -1;
    int cnt;
    {
      DirEntry *entry = (DirEntry *) NULL;
      DirIter it(this);
      for (cnt = 0; it.Next(/*OUT*/ entry); cnt++) {
	int l;
	try {
	  l = entry->Search(verbose, /*INOUT*/ stats);
	} 
	catch(FS::DoesNotExist) {
	  continue;
	}
	if (chLevel < 0) chLevel = l;
	else if (chLevel != l) throw (StatError::UnevenLevels(this->path));
      }
      entry = (DirEntry *) NULL; // drop on floor for GC
    }

    // update fan-out for this level
    int thisLevel = chLevel + 1;
    StatCount *sc;
    if (stats.fanout.size() <= thisLevel) {
	assert(stats.fanout.size() == thisLevel);
	sc = NEW(StatCount);
	stats.fanout.addhi(sc);
    } else {
	sc = stats.fanout.get(thisLevel);
    }
    sc->AddVal(cnt, NEW_CONSTR(Stat::Location, (this->path)));
    return thisLevel;
}

DirIter::DirIter(const DirObj *dirObj) throw (FS::Failure, FS::DoesNotExist)
  : path(dirObj->path), done(false)
{
    if ((this->dir = opendir(this->path.cchars())) == NULL) {
	if (errno == ENOENT) throw FS::DoesNotExist();
	else throw FS::Failure(Text("opendir"), dirObj->path);
    }
    this->path += '/'; // add terminating slash
}

static bool IsDotDir(char *arc) throw ()
{
    return (!strcmp(arc, ".") || !strcmp(arc, ".."));
}

static bool IsTempFile(const Text &path) throw ()
/* Return true iff "path" ends in an arc of the form "<hex-digit>+;*". */
{
    int slashIx = path.FindCharR('/');
    int semiIx = path.FindChar(AtomicFile::reserved_char, slashIx);
    if (semiIx <= 0) return false;
    for (int i = slashIx + 1; i < semiIx; i++) {
	if (!isxdigit(path[i])) return false;
    }
    return true;
}

bool DirIter::Next(/*OUT*/ DirEntry* &entry)
  throw (StatError::EndOfFile, FS::Failure, FS::DoesNotExist)
{
    if (done) return false;
    while (true) {
	// read directory entry; if no more, close and return
	struct dirent *sysDirEnt;
	if ((sysDirEnt = readdir(this->dir)) == (struct dirent *)NULL) {
	    done = true;
	    if (closedir(this->dir) < 0) {
		throw FS::Failure(Text("closedir"), this->path);
	    }
	    return false;
	}

	// skip "." and ".."
	if (IsDotDir(sysDirEnt->d_name)) continue;

	// skip if this is a temporary file created by VestaAtomicLog
	Text fullname(this->path + sysDirEnt->d_name);
	struct stat statBuff;
	try {
	  DirEntry::FSStat(fullname, /*OUT*/ &statBuff);
	  if (S_ISREG(statBuff.st_mode) && IsTempFile(fullname)) {
	    continue;
	  }
	  
	  // otherwise, open the entry and return
	  entry = DirEntry::Open(fullname, &statBuff);
	  break; // exit loop
	}
	catch(FS::DoesNotExist) {
	  continue;
	}
    }
    return true;
}
