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

// Created on Wed Aug 20 17:42:54 PDT 1997 by heydon
// Last modified on Mon Aug  9 18:05:10 EDT 2004 by ken@xorian.net  
//      modified on Sat Feb 12 17:57:32 PST 2000 by mann  
//      modified on Thu Aug 21 23:43:20 PDT 1997 by heydon

#include <sys/types.h> // for stat(2)
#include <sys/stat.h>  // for stat(2)

#include <Basics.H>
#include <FS.H>
#include <Generics.H>
#include <SourceOrDerived.H>

#include "HexReader.H"

using std::ostream;
using std::cout;
using std::cerr;
using std::endl;

static void Syntax(char *msg, char *arg = NULL) throw ()
{
    cerr << "Error: " << msg;
    if (arg != NULL) cerr << ": '" << arg << "'";
    cerr << endl;
    cerr << "Syntax: CountShortIds file ..." << endl;
    exit(1);
}

static char *FileName(char *name) throw ()
{
    char *res = name;
    if (strncmp(name, "0x", 2) == 0) {
	// skip initial "0x"
	char *arg = name + 2;

	// parse numeric value
	ShortId sid;
	if (sscanf(arg, "%x", /*OUT*/ &sid) != 1) {
	    Syntax("bad 'shortId' argument", name);
	}

	// convert to filename
	res = SourceOrDerived::shortIdToName(sid, /*tailOnly=*/ false);
    }
    return res;
}

static Text FormattedSz(double sz) throw ()
{
    const double OneK = 1024.0;
    char buff[20];
    if (sz < OneK) {
	sprintf(buff, "%.1f", sz);
    } else {
	sz /= OneK;
	char suffix = 'K';
	if (sz >= OneK) {
	    sz /= OneK;
	    suffix = 'M';
	}
	sprintf(buff, "%.1f%c", sz, suffix);
    }
    return Text(buff);
}

static void Process(ostream &os, char *path, bool warn)
  throw (FS::Failure, FS::DoesNotExist)
{
    // print filename to process
    os << path << ":" << endl;

    // open the file using a HexReader
    HexReader hexrd(path);

    // process the file
    IntIntTbl visited(/*sizeHint=*/ 1000);
    int lineNum = 1, dummy;
    unsigned totalCnt = 0U, uniqueCnt = 0U, skipCnt = 0U;
    unsigned long totalSz = 0UL;
    ShortId sid;
    while (true) {
	try {
	    sid = hexrd.Next();
	} catch (const HexReader::BadValue &v) {
	    if (warn) {
		os << "  Warning: skipping illegal ShortId on line "
                   << v.lineNum << ": '" << v.str << "'" << endl;
	    }
	    continue;
	}
	if (sid == 0) break;

	// test if the file is new
	totalCnt++;
	if (!visited.Get(sid, /*OUT*/ dummy)) {
	    // update table
	    bool inTbl = visited.Put(sid, 1); assert(!inTbl);

	    // stat file to get its size
	    char *nm = SourceOrDerived::shortIdToName(sid, /*tailOnly=*/false);
	    struct stat stat_buff;
	    if (stat(nm, /*OUT*/ &stat_buff) != 0) {
		if (warn) {
		    os << "  Warning: stat(2) failed on '" << nm
                       << "'; skipping..." << endl;
		}
		skipCnt++;
	    } else {
		uniqueCnt++;
		totalSz += stat_buff.st_size;
	    }
	    delete nm;
	}
	lineNum++;
    }

    // print results
    os << "  Total files processed  = " << totalCnt << endl;
    os << "  Unique files processed = " << uniqueCnt << endl;
    os << "  Unique files skipped   = " << skipCnt << endl;
    os << "  Total size of unique files = "
       << FormattedSz((double)totalSz) << endl;
    os << "  Mean size of unique files  =  "
       << FormattedSz(((double)totalSz)/((double)uniqueCnt)) << endl;
}

int main(int argc, char *argv[]) 
{
    // parse command-line
    int arg = 1;
    bool warn = false;
    if (argc <= 1) Syntax("too few arguments");
    while (*argv[arg] == '-') {
	if (strcmp(argv[arg], "-w") == 0) {
	    warn = true;
	} else {
	    Syntax("unrecognized option", argv[arg]);
	}
	arg++;
    }

    // process files
    for (/*SKIP*/; arg < argc; arg++) {
	char *path = FileName(argv[arg]);
	try {
	    Process(cout, path, warn);
	} catch (FS::DoesNotExist) {
	    cerr << "Error: file " << path
                 << " does not exist; continuing..." << endl;
	} catch (const FS::Failure &f) {
	    cerr << "Error: " << f << "; continuing..." << endl;
	}
    }
    return 0;
}
