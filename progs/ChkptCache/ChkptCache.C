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

// Last modified on Wed Aug  4 17:29:57 EDT 2004 by ken@xorian.net  
//      modified on Sat Feb 12 12:03:46 PST 2000 by mann  
//      modified on Mon Mar 30 13:12:42 PST 1998 by heydon

/* This is a simple program that establishes a connection to the Vesta-2
   cache server, and then invokes the "Cache::Checkpoint" method to
   synchronously flush its logs to disk. */

// basics
#include <Basics.H>

// cache-common
#include <CacheConfig.H>
#include <Model.H>
#include <CacheIndex.H>
#include <Debug.H>

// local includes
#include "CacheC.H"

using std::ostream;
using std::cout;
using std::cerr;
using std::endl;

static void ExitClient() throw ()
{
    cerr << "SYNTAX: ChkptCache [ -n ]" << endl;
    cerr.flush();
    exit(1);
}

static void PrintSRPCFailure(ostream &os, SRPC::failure &f) throw ()
{
    os << "SRPC Failure (code " << f.r << "):" << endl;
    os << "  " << f.msg << endl << endl;
    os.flush();
}

static void Chkpt() throw ()
{
    FP::Tag pkgVersion("", /*len=*/ 0); // fingerprint of empty string
    Model::T model = 0;
    CacheEntry::Indices cis;
    bool done = false;

    // print args
    cout << Debug::Timestamp() << "CALLING Checkpoint" << endl;
    cout << "  pkgVersion = " << pkgVersion << endl;
    cout << "  model = " << model << endl;
    cout << "  cis = " << cis << endl;
    cout << "  done = " << BoolName[done] << endl;
    cout << endl;

    // checkpoint cache
    try {
      CacheC client;

      client.Checkpoint(pkgVersion, model, cis, done);
    } catch (SRPC::failure &f) {
	PrintSRPCFailure(cerr, f);
    }

    cout << Debug::Timestamp() << "RETURNED Checkpoint" << endl;
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
    cout << Debug::Timestamp() << "Checkpointing cache:" << endl;
    cout << "  Host = " << Config_Host << endl;
    cout << "  Port = " << Config_Port << endl << endl;
    cout.flush();

    // make "Checkpoint" call
    if (doIt) {
	Chkpt();
	cout << Debug::Timestamp() << "Done!" << endl;
	cout.flush();
    } else {
	cout << "NOTE: No action taken at your request." << endl;
	cout.flush();
    }
    return 0;
}
