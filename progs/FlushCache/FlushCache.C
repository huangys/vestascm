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

// Last modified on Wed Aug  4 17:49:12 EDT 2004 by ken@xorian.net  
//      modified on Mon Feb 14 18:20:53 PST 2000 by mann  
//      modified on Mon Mar 30 13:11:28 PST 1998 by heydon

/* This is a simple program that establishes a connection to the Vesta-2
   cache server, and then invokes the "Cache::FlushAll" method to
   synchronously flush all of its volatile cache entries to the stable
   cache. */

// basics
#include <Basics.H>

// cache-common
#include <CacheConfig.H>
#include <Debug.H>

// local includes
#include "DebugC.H"

using std::ostream;
using std::cout;
using std::cerr;
using std::endl;

static void ExitClient() throw ()
{
    cerr << "SYNTAX: FlushCache [ -n ]" << endl;
    cerr.flush();
    exit(1);
}

static void PrintSRPCFailure(ostream &os, SRPC::failure &f) throw ()
{
    os << "SRPC Failure (code " << f.r << "):" << endl;
    os << "  " << f.msg << endl << endl;
    os.flush();
}

static void FlushAll() throw ()
{
    DebugC debug;

    // print args
    cout << Debug::Timestamp() << "CALLING FlushAll" << endl;
    cout << endl;

    // make call
    try {
	debug.FlushAll();
    } catch (SRPC::failure &f) {
	PrintSRPCFailure(cerr, f);
    }

    // print result
    cout << Debug::Timestamp() << "RETURNED FlushAll" << endl;
    cout << endl;
}

int main(int argc, char *argv[]) 
{
    bool doIt = true;

    // process command-line
    if (argc > 4) {
	cerr << "Error: too many arguments" << endl;
	ExitClient();
    }
    for (int arg = 1; arg < argc; arg++) {
        if (!strcmp(argv[arg], "-n")) {
	    doIt = false;
	} else if (*argv[arg] == '-') {
	    cerr << "Error: unrecognized option: " << argv[arg] << endl;
	    ExitClient();
	} else {
	    cerr << "Error: invalid command-line" << endl;
	    ExitClient();
	}
    }

    // start program
    cout << Debug::Timestamp() << "Flushing cache:" << endl;
    cout << "  Host = " << Config_Host << endl;
    cout << "  Port = " << Config_Port << endl << endl;
    cout.flush();

    // make "FlushAll" call
    if (doIt) {
	FlushAll();
	cout << Debug::Timestamp() << "Done!" << endl;
	cout.flush();
    } else {
	cout << "NOTE: No action taken at your request." << endl;
	cout.flush();
    }
    return 0;
}
