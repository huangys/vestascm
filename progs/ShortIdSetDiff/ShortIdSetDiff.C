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

// Created on Thu Aug 21 22:35:17 PDT 1997 by heydon
// Last modified on Mon Aug  9 18:06:25 EDT 2004 by ken@xorian.net  
//      modified on Sat Feb 12 17:57:32 PST 2000 by mann  
//      modified on Thu Aug 21 23:43:21 PDT 1997 by heydon

#include <Basics.H>
#include <FS.H>
#include <Generics.H>
#include <SourceOrDerived.H>

#include "HexReader.H"

using std::ios;
using std::ostream;
using std::cout;
using std::cerr;
using std::endl;

static void Syntax(char *msg, char *arg = NULL) throw ()
{
    cerr << "Error: " << msg;
    if (arg != NULL) cerr << ": '" << arg << "'";
    cerr << endl;
    cerr << "Syntax: ShortIdSetDiff file1 file2" << endl;
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

static void ReadFile2(char *path, /*INOUT*/ IntIntTbl &elts)
  throw (FS::DoesNotExist, FS::Failure, HexReader::BadValue)
{
    ShortId sid, totalCnt = 0, uniqCnt = 0;
    HexReader hexrd(path);
    while ((sid = hexrd.Next()) != 0) {
	int dummy;
	totalCnt++;
	if (!elts.Get(sid, /*OUT*/ dummy)) {
	    uniqCnt++;
	    bool inTbl = elts.Put(sid, 0); assert(!inTbl);
	}
    }
    cerr << "  Total lines = " << totalCnt;
    cerr << "; unique lines = " << uniqCnt << endl;
}

static void ProcessFile1(char *path, ostream &os, const IntIntTbl &elts)
  throw (FS::DoesNotExist, FS::Failure, HexReader::BadValue)
{
    // put "os" in correct mode for printing hex results
    os.setf(ios::hex,ios::basefield);

    // do work
    HexReader hexrd(path);
    ShortId sid, totalCnt = 0, uniqCnt = 0, writtenCnt = 0;
    IntIntTbl myElts(/*sizeHint=*/ 5000);
    while ((sid = hexrd.Next()) != 0) {
	totalCnt++;
	int dummy;
	if (!myElts.Get(sid, /*OUT*/ dummy)) {
	    uniqCnt++;
	    bool inTbl = myElts.Put(sid, 0); assert(!inTbl);
	    if (!elts.Get(sid, /*OUT*/ dummy)) {
		writtenCnt++;
		os.width(8); os << sid << endl;
	    }
	}
    }
    cerr << "  Total lines = " << totalCnt;
    cerr << "; unique lines = " << uniqCnt;
    cerr << "; written lines = " << writtenCnt << endl;
}

int main(int argc, char *argv[]) 
{
    if (argc != 3) Syntax("incorrect number of arguments");
    try {
	char *file2 = FileName(argv[2]);
	cerr << "Reading " << file2 << "..." << endl;
	IntIntTbl elts(/*sizeHint=*/ 5000);
	ReadFile2(file2, /*INOUT*/ elts);

	char *file1 = FileName(argv[1]);
	cerr << "Reading " << file1 << "..." << endl;
	ProcessFile1(file1, cout, elts);
    } catch (const FS::DoesNotExist) {
	cerr << "Fatal error: file does not exist" << endl;
    } catch (const FS::Failure &f) {
	cerr << "Fatal error: " << f << endl;
    } catch (const HexReader::BadValue &v) {
	cerr << "Fatal error: bad value '" << v.str
             << "', line " << v.lineNum << endl;
    }
    return 0;
}
