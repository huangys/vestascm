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


// The Vesta-2 cache server

#include <Basics.H>
#include <CacheArgs.H>
#include <CacheConfig.H>
#include <Debug.H>
#include <ReadConfig.H>
#include <CacheConfigServer.H>

#include "VCacheVersion.H"
#include "CacheS.H"
#include "ExpCache.H"

using std::ostream;
using std::endl;
using std::cerr;
using OS::cio;

static void ExitServer() throw ()
{
  cerr << "Syntax: VCache [ -debug <level> ] [ -noHits ]" << endl;
  cerr << "The allowed values for <level> are:";
  for (int i = 0, lineCnt = 0; i <= CacheIntf::All; i++, lineCnt++) {
    if (i == CacheIntf::WeederScans) continue;
    if (i > 0) cerr << ", ";
    if (lineCnt % 5 == 0) cerr << endl << "  ";
    cerr << CacheIntf::DebugName(i);
  }
  cerr << endl;
  exit(1);
}

static void Server(int argc, char *argv[]) throw ()
{
    CacheIntf::DebugLevel debug = CacheIntf::None;
    bool noHits = false;
    int arg = 1;

    // parse arguments
    while (arg < argc && argv[arg][0] == '-') {
	if (CacheArgs::StartsWith(argv[arg], "-debug")) {
	    if (argc <= ++arg) {
	      cerr << "Error: expecting level" << endl;
	      ExitServer();
	    }
	    if (sscanf(argv[arg], "%d", &debug) != 1) {
                int i;
		for (i = 0; i <= CacheIntf::All; i++) {
		    if (strcmp(argv[arg], CacheIntf::DebugName(i)) == 0) break;
		}
		debug = (CacheIntf::DebugLevel)i;
	    }
	    if (debug < CacheIntf::None || debug > CacheIntf::All) {
	      cerr << "Error: unrecognized debug-level '"
		   << argv[arg] << "'" << endl;
	      ExitServer();
	    }
	    arg++;
        } else if (CacheArgs::StartsWith(argv[arg], "-noHits")) {
	    noHits = true;
	    arg++;
	} else if (CacheArgs::StartsWith(argv[arg], "-help")
                   || CacheArgs::StartsWith(argv[arg], "-?")) {
	    ExitServer();
	} else {
	  cerr << "Error: Unrecognized switch '" << argv[arg] << "'" << endl;
	  ExitServer();
	}
    }
    if (arg < argc) {
      cerr << "Error: Too many arguments" << endl;
      ExitServer();
    }
    
    // start server
    CacheS *cs = NEW_CONSTR(CacheS, (debug, noHits));
    if (debug >= CacheIntf::StatusMsgs) {
      ostream& out_stream = cio().start_out();
      out_stream << Debug::Timestamp() << "Starting server:" << endl;
      out_stream << "  Version = " << VCacheVersion << endl;
      out_stream << "  Interface version = " << CacheIntf::Version << endl;
      out_stream << "  Config file = " << ReadConfig::Location() << endl;
      out_stream << "  Cache root = " << Config_CacheMDPath << endl;
      out_stream << "  Debug level = " << CacheIntf::DebugName(debug) << endl;
      out_stream << "  Disable cache hits = " << BoolName[noHits] << endl;
      out_stream << "  Maximum running threads = " << Config_MaxRunning << endl;
      out_stream << endl;
      cio().end_out();
    }
    ExpCache *exp =
      NEW_CONSTR(ExpCache,
		 (cs, Config_MaxRunning));

    // Run the cache server
    try {
      exp->Run();
    }
    catch (SRPC::failure f) {
      ostream& out_stream = cio().start_out();
      out_stream << Debug::Timestamp();
      out_stream << "Cache Server Failure:" << endl;
      out_stream << "  SRPC failure (code " << f.r << ')' << endl;
      out_stream << "  " << f.msg << endl;
      out_stream << "Exiting" << endl;
      cio().end_out();
    }
}

int main(int argc, char *argv[])
{
    // rename any existing core file so it won't be overwritten
    system("if [ -e core ] ; then mv core core.`date +%m-%d-%y.%T` ; fi");

    // start the server
    Server(argc, argv);
    return(0);
}
