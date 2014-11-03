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

// Read the cache log and print its complete contents

#include <Basics.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <FP.H>
#include <CacheArgs.H>
#include <CacheIntf.H>
#include <CacheConfig.H>
#include <Intvl.H>

using std::fstream;
using std::cout;
using std::cerr;
using std::endl;

/* Note: This is a single-threaded program, so it doesn't respect
   the locking level requirements of the log files. */

// A pared down version of from CacheS::RecoverCILog(): get the set of
// used CIs recorded by the cache server on disk.

void Print_used_cis(bool verbose)
{
  BitVector used_cis;

  // Disk log of usedCI's
  VestaLog l_ci_log;
  l_ci_log.open(Config_CILogPath.chars(), -1, true);

  // Reead the used CIs from the checkpoint.
  try
    {
      fstream *l_chkpt = l_ci_log.openCheckpoint();
      if(l_chkpt != NULL)
	{
	  RecoveryReader l_reader(l_chkpt);
	  used_cis.Recover(l_reader);
	  FS::Close(*l_chkpt);
	}
    }
  catch(...)
    {
      cerr << "Exception while reading used CIs from checkpoint." << endl;
      throw;
    }

  if(verbose)
    {
      cout << ">>>>>> Used CIs from checkpoint <<<<<<" << endl << endl;
      used_cis.PrintAll(cout, 4);
      cout << endl
	   << "  (" << used_cis.Cardinality() << " total)" << endl;
    }

  // Recover any CI allocations/deallocations since the checkpoint
  // from logs
  try
    {
      RecoveryReader l_reader(&l_ci_log);
      if(verbose)
	{
	  cout << endl << ">>>>>> Updates from log <<<<<<" << endl << endl;
	}
      do
	{
	  while (!l_reader.eof()) {
	    Intvl::T l_intvl(l_reader);
	    used_cis.WriteInterval(l_intvl.lo, l_intvl.hi,
				   (l_intvl.op == Intvl::Add));
	    if(verbose)
	      {
		l_intvl.Debug(cout);
	      }
	  }
	} while (l_ci_log.nextLog());
    }
  catch(...)
    {
      cout << "Exception while recovering used CIs from logs." << endl;
      throw;
    }

  if(verbose) cout << endl;
  cout << ">>>>>> Used CIs <<<<<<" << endl << endl;
  used_cis.PrintAll(cout, 4);
  cout << endl
       << "  (" << used_cis.Cardinality() << " total)" << endl;
}

void ExitProgram(char *msg) throw ()
{
    cerr << "Fatal error: " << msg << endl;
    cerr << "Syntax: PrintCacheLog [ -verbose ]" << endl;
    exit(1);
}

int main(int argc, char *argv[]) 
{
    bool verbose = false;

    // process command-line
    if (argc > 2) ExitProgram("too many command-line arguments");
    if (argc == 2) {
	if (CacheArgs::StartsWith(argv[1], "-verbose")) {
	    verbose = true;
	} else {
	    ExitProgram("unrecognized command-line option");
	}
    }

    // print logs
    try {
	Print_used_cis(verbose);
    }
    catch (VestaLog::Error e) {
      cerr << "VestaLog fatal error: " << e.msg << endl;
      exit(1);
    }
    catch (VestaLog::Eof) {
      cerr << "VestaLog fatal error: "
	   << "unexpected EOF while reading used CIs; exiting..." << endl;
      exit(1);
    }
    return 0;
}
