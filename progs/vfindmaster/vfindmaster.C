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
// vfindmaster.C
//

// Attempt to locate the repository with the master copy of a
// particular object.

#include <Basics.H>
#include <Text.H>
#include <VestaConfig.H>
#include <VestaSource.H>
#include "ReposUI.H"

#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif

using std::cout;
using std::cerr;
using std::endl;

Text program_name;

void usage()
{
  cerr << "Usage: " << program_name << endl
       << "        [-h hints]" << endl
       << "        [path]" << endl << endl;
  exit(1);
}

int
main(int argc, char* argv[])
{
  program_name = argv[0];

  try {
    //
    // Read config file
    //
    Text defpkgpar = VestaConfig::get_Text("UserInterface",
					   "DefaultPackageParent");
    Text defhints;
    (void) VestaConfig::get("UserInterface", "DefaultHints", defhints);

    Text path, hints;

    opterr = 0;
    for (;;) {
      int c = getopt(argc, argv, "h:");
      if (c == EOF) break;
      switch (c) {
      case 'h':
	hints = optarg;
	break;
      case '?':
      default:
	usage();
      }
    }

    hints = hints + " " + defhints;

    switch (argc - optind) {
    case 1:
      path = argv[optind];
      break;
    case 0:
      path = ".";
      break;
    default:
      usage();
      /* not reached */
    }

    // Canonicalize path
    Text cpath;
    cpath = ReposUI::canonicalize(path, defpkgpar);

    // Find the master copy of this object
    VestaSource *vs = ReposUI::filenameToMasterVS(cpath, hints);

    // Display the authoritative address for the repository the master
    // copy was found in
    cout << vs->getMasterHint() << endl;

    // @@@ We might want to add an option to also or alternatively
    // display the address used to contact this repository:

    // cout << vs->host() << ":" << vs->port() << endl;

  } catch (VestaConfig::failure f) {
    cerr << program_name << ": " << f.msg << endl;
    exit(2);
  } catch (SRPC::failure f) {
    cerr << program_name
	 << ": SRPC failure; " << f.msg << " (" << f.r << ")" << endl;
    exit(2);
  } catch (ReposUI::failure f) {
    cerr << program_name << ": " << f.msg << endl;
    exit(2);
  }

  return 0;
}
