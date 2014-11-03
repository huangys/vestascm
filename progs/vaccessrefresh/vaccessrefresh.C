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
// vaccessrefresh.C
// Last modified on Sun Jun  5 21:50:08 EDT 2005 by ken@xorian.net
//      modified on Wed Jul 11 22:03:49 PDT 2001 by mann
//
// Ask the repository to refresh its access tables.
//

#include <Basics.H>
#include <Text.H>
#include <VestaConfig.H>
#include <VestaSource.H>
#include <VDirSurrogate.H>
#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif

using std::cout;
using std::cerr;
using std::endl;

Text program_name;

void
Usage()
{
    cerr << "Usage: " << program_name <<
      " [-R repos]" << endl << endl;
    exit(3);
}

int
main(int argc, char* argv[])
{
    try {
	program_name = argv[0];
	Text repos;

	int c;
	for (;;) {
	    c = getopt(argc, argv, "h?R:");
	    if (c == EOF) break;
	    switch (c) {
	      case 'R':	repos = optarg;	break;
	      case 'h':
	      case '?':
	      default:
		Usage();
	    }
	}

	Text lhost(VDirSurrogate::defaultHost());
	Text lport(VDirSurrogate::defaultPort());
	if (repos != "") {
	  int colon = repos.FindCharR(':');
	  if (colon == -1) {
	    lhost = repos;
	    repos = repos + ":" + lport;
	  } else {
	    lhost = repos.Sub(0, colon);
	    lport = repos.Sub(colon+1);
	  }
	}

	// Note that we always pass NULL for "who", so there's no way
	// to override the user who's requesting the refresh.
	VDirSurrogate::refreshAccessTables(NULL, lhost, lport);

	// If we don't get an SRPC::failure, we succeeded.
	cout << "Repository access tables refreshed." << endl;

    } catch (VestaConfig::failure f) {
	cerr << program_name << ": " << f.msg << endl;
	exit(1);
    } catch (SRPC::failure f) {
	cerr << program_name
	  << ": SRPC failure; " << f.msg << " (" << f.r << ")" << endl;
	exit(2);
    }

    return 0;
}

