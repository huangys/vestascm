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

// Created on Mon Mar  3 16:31:49 PST 1997 by heydon

// Last modified on Mon Aug  9 17:27:09 EDT 2004 by ken@xorian.net  
//      modified on Tue Jun 27 17:23:16 PDT 2000 by mann  
//      modified on Mon Jun  7 13:50:10 PDT 1999 by heydon

// VestaWeed.C -- the Vesta-2 weeder

#include <Basics.H>
#include <FS.H>
#include <VestaConfig.H>
#include <CacheIntf.H>
#include <CacheConfig.H>
#include <Debug.H>

#include "WeederConfig.H"
#include "WeedArgs.H"
#include "RootTbl.H"
#include "CommonErrors.H"
#include "Weeder.H"

using std::cout;
using std::cerr;
using std::endl;

// ----------------------------------------------------------------------
// version string

// 1.4: Fixed the keep duration weeding bug that tried to keep all
//      roots for a given build even though only those newer than the
//      keepTime had their children marked.

static const char *Version = "1.4, 09-May-2002";
// ----------------------------------------------------------------------

static void SyntaxError(char *msg) throw ()
{
    cerr << "Error: " << msg << "; exiting..." << endl;
    cerr << "Syntax: VestaWeed " << endl;
    cerr << "  [-nodelete | -query] [-models] [-roots] [-keep dur] [-debug level] [file]" << endl;
    exit(1);
}

static void ProcessArgs(int argc, char *argv[], /*OUT*/ WeedArgs &args)
  throw ()
{
    // set defaults
    args.instrFile = (char *)NULL;
    args.debug = CacheIntf::None;
    args.delStatus = WeedArgs::DoDeletions;
    args.globInstrs = false;
    args.printRoots = false;
    args.keepSecs = 0;

    // process arguments
    int arg;
    bool sawNorQ = false;
    bool sawK = false;
    for (arg = 1; arg < argc && *argv[arg] == '-'; arg++) {
	if (!strcmp(argv[arg], "-d") || !strcmp(argv[arg], "-debug")) {
	    arg++;
	    if (arg >= argc) {
		SyntaxError("no 'level' specified for '-debug'");
	    }
	    if (sscanf(argv[arg], "%d", &(args.debug)) != 1) {
                int i;
		for (i = 0; i <= CacheIntf::All; i++) {
		  if (strcasecmp(argv[arg], CacheIntf::DebugName(i)) == 0)
		    break;
		}
		args.debug = (CacheIntf::DebugLevel)i;
	    }
	    if (args.debug < CacheIntf::None || args.debug > CacheIntf::All) {
		SyntaxError("unrecognized debug level");
	    }
	} else if (strcmp(argv[arg], "-n") == 0 ||
		   strcmp(argv[arg], "-nodelete") == 0) {
	    if (sawNorQ) {
		SyntaxError("-nodelete and -query are mutually exclusive");
	    }
	    sawNorQ = true;
	    args.delStatus = WeedArgs::NoDeletions;
	} else if (strcmp(argv[arg], "-q") == 0 ||
		   strcmp(argv[arg], "-query") == 0) {
	    if (sawNorQ) {
		SyntaxError("-nodelete and -query are mutually exclusive");
	    }
	    sawNorQ = true;
	    args.delStatus = WeedArgs::QueryDeletions;
	} else if (strcmp(argv[arg], "-m") == 0 ||
		   strcmp(argv[arg], "-models") == 0) {
	    args.globInstrs = true;
	} else if (strcmp(argv[arg], "-r") == 0 ||
		   strcmp(argv[arg], "-roots") == 0) {
	    args.printRoots = true;
	} else if (!strcmp(argv[arg], "-k") || !strcmp(argv[arg], "-keep")) {
	    arg++;
	    if (arg >= argc) {
		SyntaxError("no duration argument specified for '-keep'");
	    }
	    sawK = true;
	    char *dur = argv[arg];
            int last = strlen(dur) - 1;
	    int units = 60 * 60; // default is "hours"
	    switch (dur[last]) {
	      case 'd':	units = 60 * 60 * 24; break;
	      case 'h':	units = 60 * 60; break;
	      case 'm':	units = 60; break;
	      default:
		if (!isdigit(dur[last])) {
		    SyntaxError("unrecognized unit specifier for '-keep' arg");
		    exit(1);
		}
	    }
	    if (sscanf(argv[arg], "%d", &(args.keepSecs)) != 1) {
		SyntaxError("argument following '-keep' is not an integer");
	    }
	    if (args.keepSecs < 0) {
		SyntaxError("argument following '-keep' cannot be negative");
	    }
	    args.keepSecs *= units; // convert to appropriate units
	} else {
	    SyntaxError("unrecognized command-line switch");
	}
    }
    if (arg < argc - 1) {
	SyntaxError("too many arguments");
    } else if (arg < argc) {
	assert(arg == argc - 1);
	args.instrFile = argv[arg];
	args.noNew = false;
    } else {
        args.noNew = true;  // finish an old weed but don't start a new one
	if (sawK) {
 	  SyntaxError("-keep flag is meaningless with no input filename");
        }
	if (args.globInstrs) {
 	  SyntaxError("-models flag is meaningless with no input filename");
        }
	if (args.printRoots) {
 	  SyntaxError("-roots flag is meaningless with no input filename");
        }
    }

    // echo command-line if necessary
    if (args.debug >= CacheIntf::StatusMsgs) {
	cout << Debug::Timestamp() << "Command-line:" << endl << "  ";
	for (int i = 0; i < argc; i++) {
	    cout << argv[i] << ' ';
	}
	cout << endl << endl;
    }
}

static bool CentralCache() throw ()
{
    return (Config_Host == WeedConfig_CacheHost)
      && (Config_Port == WeedConfig_CachePort)
      && (Config_MDRoot == WeedConfig_CacheMDRoot)
      && (Config_MDDir == WeedConfig_CacheMDDir)
      && (Config_GraphLogDir == WeedConfig_CacheGLDir);
}

int main(int argc, char *argv[]) 
{
    // process command-line
    WeedArgs args;
    ProcessArgs(argc, argv, /*OUT*/ args);

    // check that we are not running on local cache
    if (!CentralCache()) {
	cerr << "Fatal error: weeder must be run against the central cache!";
	cerr << " Exiting..." << endl;
	exit(1);
    }

    try {
	// initialize the weeder
	Weeder weeder(args.debug);

	// status message
	if (args.debug >= CacheIntf::StatusMsgs) {
	    cout << Debug::Timestamp() << "Weeder started:" << endl;
	    cout << "  Version: " << Version << endl;
	    cout << "  Configuration file: " <<
              VestaConfig::get_location() << endl;
	    cout << "  Graph log directory: " << Config_GraphLogPath << endl;
	    cout << "  Keeping all builds less than "
                 << args.keepSecs / 3600 << " hours old" << endl;
            cout << "  Debugging level = "
                 << CacheIntf::DebugName(args.debug) << endl;
	    cout << endl;
	}

	// do the weed, or resume an old weed
	bool resumed = weeder.Weed(args);

	if (resumed) {
           // finished an old weed; now need to do the new one
	   Weeder weeder2(args.debug);
	   (void) weeder2.Weed(args);
        }

    }
    catch (SRPC::failure &f) {
	cerr << "SRPC failure:" << endl;
        cerr << "  " << f.msg << endl;
        cerr << "Exiting..." << endl;
	exit(1);
    }
    catch (FS::DoesNotExist) {
	cerr << "Error: file '" << args.instrFile << "' does not exist"<< endl;
	cerr << "Exiting..." << endl;
	exit(1);
    }
    catch (FS::EndOfFile) {
	cerr << "Error: premature end-of-file; exiting..." << endl;
	exit(1);
    }
    catch (FS::Failure &f) {
	cerr << "Error: reading input file:" << endl;
	cerr << "  " << f << endl;
	cerr << "Exiting..." << endl;
	exit(1);
    }
    catch (VestaLog::Error &err) {
	cerr << "VestaLog fatal error -- failed reading graph log:" << endl;
        cerr << "  " << err.msg << endl;
        cerr << "Exiting..." << endl;
	exit(1);
    }
    catch (VestaLog::Eof) {
	cerr << "VestaLog fatal error: ";
        cerr << "unexpected EOF while reading graph log; exiting..." << endl;
	exit(1);
    }
    catch (InputError &err) {
	cerr << "Error in input file '" << args.instrFile << "':" << endl;
	cerr << err;
	cerr << "Exiting..." << endl;
	exit(1);
    }
    catch (SysError &err) {
	cerr << err;
	cerr << "Exiting..." << endl;
	exit(1);
    }
    catch (ReposError &err) {
	cerr << err;
	cerr << "Exiting..." << endl;
	exit(1);
    }
    return 0;
}
