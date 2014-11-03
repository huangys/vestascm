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

//
// Append stdin to a log
//

#include "VestaLog.H"
#include <stdlib.h>
#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif

using std::cin;
using std::cerr;
using std::endl;

char* program_name;

void
Usage()
{
    cerr << "Usage: " << program_name << " [-n lognum] [logdir]" << endl;
    exit(2);
}

int
main(int argc, char* argv[]) 
{
    int lognum = -1;
    VestaLog VRLog;

    program_name = argv[0];

    int c;
    for (;;) {
	c = getopt(argc, argv, "n:");
	if (c == EOF) break;
	switch (c) {
	  case 'n':
	    lognum = strtol(optarg, NULL, 0);
	    break;
	  case '?':
	  default:
	    Usage();
	}
    }

    char* dir;
    switch (argc - optind) {
      case 0:
	dir = ".";
	break;
      case 1:
	dir = argv[optind];
	break;
      default:
	Usage();
    }

    try {
	VRLog.open(dir, lognum, false, true);
    } catch (VestaLog::Error) {
	cerr << program_name << ": error opening log\n";
	exit(1);
    }

    char ch;

    // Read past existing part of log
    for (;;) {
	try {
	    VRLog.get(ch);
	} catch (VestaLog::Error) {
	    cerr << program_name << ": error getting character from log\n";
	    exit(1);
	} catch (VestaLog::Eof) {
	    if (!VRLog.nextLog()) break;
	}
    }

    VRLog.loggingBegin();
    VRLog.start();

    // Append new stuff
    while (cin.get(ch)) {
	try {
	    VRLog.put(ch);
	} catch (VestaLog::Error) {
	    cerr << program_name << ": error putting character to log\n";
	    exit(1);
	}
    }

    VRLog.commit();
    VRLog.close();

    return 0;
}
