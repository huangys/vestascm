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
// Dump the contents of a log
//

#include "VestaLog.H"
#include <stdlib.h>
#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif

using std::cin;
using std::cout;
using std::cerr;
using std::endl;

char* program_name;

void
Usage()
{
    cerr << "Usage: " << program_name << " [-n lognum] [-l] [logdir]" << endl;
    exit(2);
}

int
main(int argc, char* argv[]) 
{
    int lognum = -1;
    bool loop = false;  // loop through logfiles >= lognum
    VestaLog VRLog;

    program_name = argv[0];

    int c;
    for (;;) {
	c = getopt(argc, argv, "n:l");
	if (c == EOF) break;
	switch (c) {
	  case 'n':
	    lognum = strtol(optarg, NULL, 0);
	    break;
	  case 'l':
	    loop = true;
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
	VRLog.open(dir, lognum, true);
    } catch (VestaLog::Error) {
	cerr << program_name << ": error opening log\n";
	exit(1);
    }

    for (;;) {
	char c;

	try {
	    VRLog.get(c);
	    cout.put(c);
	} catch (VestaLog::Error) {
	    cerr << program_name << ": error getting character from log\n";
	    exit(1);
	} catch (VestaLog::Eof) {
	    if (!loop || !VRLog.nextLog()) break;
	}
    }
    VRLog.close();

    return 0;
}
