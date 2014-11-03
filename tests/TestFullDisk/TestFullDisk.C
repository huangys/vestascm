// Copyright (C) 2001, Compaq Computer Corporation

// This file is part of Vesta.

// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// Last modified on Thu May 27 14:54:35 EDT 2004 by ken@xorian.net
//      modified on Wed Jan 26 17:10:36 PST 2000 by heydon

// TestFullDisk.C
//
// This program tests if the AtomicFile implementation behaves
// correctly in the face of a full disk.
//
// Syntax: TestFullDisk [ -useFS ] filename size
//
// The program opens the file named 'filename', and writes
// 'size' null bytes to it. It then commits the writes using
// the AtomicFile::close method.
//
// If the '-useFS' option is specified, then all writes
// are performed via calls to FS::Write, which should
// halt and report an error whenever any of the writes
// fails.

#include <Basics.H>
#include "FS.H"
#include "AtomicFile.H"

using std::cerr;
using std::endl;
using std::ios;

void Syntax(char *msg) {
    cerr << "Error: " << msg << endl;
    cerr << "Syntax: TestFullDisk [ -useFS ] filename size" << endl;
    exit(1);
}

void Test(bool useFS, char *filename, int size) throw (FS::Failure) {
    // open the file
    AtomicFile ofs;
    ofs.open(filename, ios::out);
    if (ofs.fail()) {
	throw(FS::Failure(Text("AtomicFile::open"), filename));
    }

    // write to it
    const int BuffSz = 1024 * 8;
    char buff[BuffSz];
    while (size > 0) {
	int toWrite = min(BuffSz, size);
	if (useFS) {
	    FS::Write(ofs, buff, toWrite);
	} else {
	    ofs.write(buff, toWrite);
	}
	size -= toWrite;
    }

    // close the file (rename it atomically)
    FS::Close(ofs);
}

int main(int argc, char *argv[]) {
    // parse command-line
    if (argc != 3 && argc != 4) {
	Syntax("incorrect number of arguments");
    }
    int arg = 1;
    bool useFS = false;
    if (strcmp(argv[arg], "-useFS") == 0) {
	arg++;
	useFS = true;
    }
    char *filename = argv[arg++];
    int size;
    if (sscanf(argv[arg++], "%d", &size) != 1) {
	Syntax("'size' argument not an integer");
    }

    // perform the test
    try {
	Test(useFS, filename, size);
    } catch (const FS::Failure &f) {
	// fatal error
	cerr << f;
	assert(false);
    }
    return 0;
}
    
