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

// Created on Thu May 29 15:30:19 PDT 1997 by heydon

// PrintCacheVal -- Write the values pickled in cache entries to stdout.

#include <sys/types.h>
#include <sys/stat.h>
#if defined(__digital__)
#include <sys/mode.h>
#endif
#include <dirent.h>
#include <Basics.H>
#include <FS.H>
#include <AtomicFile.H>
#include <FP.H>
#include <CacheArgs.H>
#include <CacheConfig.H>
#include <VestaVal.H>
#include <CacheEntry.H>
#include <SPKFile.H>
#include <SMultiPKFileRep.H>

using std::ostream;
using std::ifstream;
using std::cout;
using std::cerr;
using std::endl;

void Error(char *msg, char *arg = (char *)NULL) throw ()
{
    cout << "Error: " << msg;
    if (arg != (char *)NULL) cout << ": `" << arg << "'";
    cout << endl;
    cout << "SYNTAX: PrintCacheVal [ -n | -N ] [ -ci num ] [ path ]" << endl;
    exit(1);
}

bool OpenFile(const Text fname, /*OUT*/ ifstream &ifs) throw (FS::Failure)
/* Open the file named "fname" for reading. If successful, set "ifs" to a
   stream on the file and return true. If the file does not exist, print an
   error message and return false. If any other error occurs, throw
   "FS::Failure".
*/
{
    try {
	FS::OpenReadOnly(fname, ifs);
    }
    catch (FS::DoesNotExist) {
	cerr << "Error: unable to open '" << fname << "' for reading." << endl;
	return false;
    }
    return true;
}

static void SearchMPK(ostream &os, const Text &fname, int nSwitch, int ciOnly)
  throw (SMultiPKFileRep::BadMPKFile, FS::EndOfFile, FS::Failure)
{
    // print the filename if "nSwitch"
    if (nSwitch > 0) {
	os << "MultiPKFile " << fname << ':';
	if (nSwitch > 1) os << endl;
    }

    // open the file
    ifstream ifs;
    if (!OpenFile(fname, /*OUT*/ ifs)) return;

    try {
	// read it (including the PKFiles)
	SMultiPKFileRep::Header hdr(ifs);
	hdr.ReadEntries(ifs);
	hdr.ReadPKFiles(ifs);
	FS::Close(ifs);

	// walk over the PKFiles in the MultiPKFile
	int entryCnt = 0;
	for (int i = 0; i < hdr.num; i++) {
	    SMultiPKFileRep::HeaderEntry *he;
	    bool inTbl = hdr.pkTbl.Get(*(hdr.pkSeq[i]), he);
	    assert(inTbl);

	    // walk over the entries in the PKFile
	    SPKFile *pkFile = he->pkfile;
	    SPKFile::CFPEntryIter it(pkFile->OldEntries());
	    FP::Tag fp; CE::List *entries;
	    while (it.Next(/*OUT*/ fp, /*OUT*/ entries)) {
		// walk over the entries in the CFP group
		while (entries != (CE::List *)NULL) {
		    CE::T *ent = entries->Head();
		    if (ciOnly < 0 || ciOnly == ent->CI()) {
			// act on this entry
			entryCnt++;
			const VestaVal::T *val = ent->Value();
			switch (nSwitch) {
			  case 0:
			    // print the entry's pickled value to 'os'
			    FS::Write(os, (char *)(&(val->len)),
                              sizeof(val->len));
			    FS::Write(os, val->bytes, val->len);
			    break;
			  case 1:
			    // do nothing
			    break;
			  case 2:
			    // print a summary of the entry to 'os'
			    os << "  ci = " << ent->CI()
			       << "; value length = " << val->len << endl;
			    break;
			}
		    }
		    entries = entries->Tail();
		}
	    }
	}
	if (nSwitch == 1) os << " entries = " << entryCnt << endl;
    } catch (...) {
	if (nSwitch == 1) os << endl;
	FS::Close(ifs);
	throw;
    }
}

static void FSStat(const Text &path, /*OUT*/ struct stat *buffer)
  throw (FS::Failure, FS::DoesNotExist)
{
    if (stat(path.cchars(), buffer) != 0) {
	if (errno == ENOENT) throw FS::DoesNotExist();
	else throw FS::Failure(Text("stat"), path);
    }
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

static void SearchPath2(ostream &os, const Text &path, int nSwitch,
  int ciOnly, struct stat *statBuff) throw (FS::EndOfFile, FS::Failure)
{
    // handle files and directories differently
    if (S_ISDIR(statBuff->st_mode)) {
	// iterate over children and recurse
	DIR *dir;
	if ((dir = opendir(path.cchars())) == NULL) {
	    assert(errno != ENOENT);
	    throw FS::Failure(Text("opendir"), path);
	}
	Text pathSlash(path + '/');
	struct dirent *sysDirEnt;
	while ((sysDirEnt = readdir(dir)) != (struct dirent *)NULL) {
	    // skip "." and ".."
	    if (IsDotDir(sysDirEnt->d_name)) continue;

	    // skip if this is a temporary file created by VestaAtomicLog
	    Text fullname(pathSlash + sysDirEnt->d_name);
	    struct stat buffer;
	    FSStat(fullname, /*OUT*/ &buffer);
	    if (S_ISREG(buffer.st_mode) && IsTempFile(fullname)) {
		continue;
	    }

	    // search recursively
	    SearchPath2(os, fullname, nSwitch, ciOnly, &buffer);
	}
	if (closedir(dir) < 0) {
	    throw FS::Failure(Text("closedir"), path);
	}
    } else {
	// read MultiPKFile
	assert(S_ISREG(statBuff->st_mode));
	try {
	    SearchMPK(os, path, nSwitch, ciOnly);
	} catch (SMultiPKFileRep::BadMPKFile) {
	    cerr << "Warning: ignoring bad MultiPKFile:" <<endl << path <<endl;
	}
    }
}

static void SearchPath(ostream &os, const char *path, int nSwitch, int ciOnly)
  throw (FS::EndOfFile, FS::Failure)
{
    // make path absolute
    Text fullpath(path);
    if (path[0] != '/') {
	// make path absolute
	if (fullpath.Length() > 0) {
	    fullpath = Config_SCachePath + ('/' + fullpath);
	} else {
	    fullpath = Config_SCachePath;
	}
    }

    // search recursively
    struct stat buffer;
    FSStat(fullpath, /*OUT*/ &buffer);
    SearchPath2(os, fullpath, nSwitch, ciOnly, &buffer);
}

int main(int argc, char *argv[])
{
    // command-line switch values
    int nSwitch = 0;
    int ci = -1;

    // process command-line
    int arg = 1;
    while (arg < argc && *argv[arg] == '-') {
	if (strcmp(argv[arg], "-n") == 0) {
	    nSwitch = 1; arg++;
	} else if (strcmp(argv[arg], "-N") == 0) {
	    nSwitch = 2; arg++;
	} else if (CacheArgs::StartsWith(argv[arg], "-ci")) {
	    arg++;
	    if (arg < argc-1) {
		int res = sscanf(argv[arg], "%d", &ci);
		if (res != 1 || ci < 0) {
		    Error("argument to `-ci' is not a non-negative integer",
			  argv[arg]);
		}
		arg++;
	    } else {
		Error("no argument for `-ci'");
	    }
	} else {
	    Error("unrecognized switch", argv[arg]);
	}
    }
    if (arg < argc - 1) {
	Error("at most one `path' argument allowed", argv[arg]);
    }
    const char *path = (arg < argc) ? argv[arg] : "";

    // print the pickled values in "path"
    try {
	SearchPath(cout, path, nSwitch, ci);
    }
    catch (FS::EndOfFile) {
	cerr << "Fatal error: unexpected end-of-file" << endl;
	exit(3);
    }
    catch (FS::Failure &f) {
	cerr << f;
	exit(4);
    }
    return 0;
}
