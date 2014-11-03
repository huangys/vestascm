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

// Last modified on Mon May 23 22:54:20 EDT 2005 by ken@xorian.net  
//      modified on Sat Feb 12 13:12:48 PST 2000 by mann  
//      modified on Sun Aug 23 11:35:45 PDT 1998 by heydon

// Read the cache log and print its complete contents

#include <Basics.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <FP.H>
#include <CacheArgs.H>
#include <CacheIntf.H>
#include <CacheConfig.H>
#include <EmptyPKLog.H>
#include <CacheLog.H>

using std::fstream;
using std::cout;
using std::cerr;
using std::endl;

/* Note: This is a single-threaded program, so it doesn't respect
   the locking level requirements of the log files. */

void PrintEmptyPKLogEntries(EmptyPKLog &log)
  throw (VestaLog::Error, VestaLog::Eof)
{
    bool empty = true;
    int num = 0;
    cout << ">>>>> EmptyPKLog <<<<<" << endl << endl;
    while (!log.EndOfFile()) {
	FP::Tag pk; PKFile::Epoch logPKEpoch;
	log.Read(/*OUT*/ pk, /*OUT*/ logPKEpoch);
	cout << ++num << ". " << "pk = " << pk;
	cout << ", pkEpoch = " << logPKEpoch << endl;
	empty = false;
    }
    if (empty) cout << "<<Empty>>" << endl;
    cout << endl;
}

void PrintEmptyPKLog() throw (VestaLog::Error, VestaLog::Eof)
/* Open the emptyPKLog, and loop over the checkpoint and all subsequent
   log files. */
{
    Basics::mutex mu; // dummy lock
    EmptyPKLog emptyPKLog(&mu, CacheIntf::None, /*readonly=*/ true);
    do {
	PrintEmptyPKLogEntries(emptyPKLog);
    } while (emptyPKLog.NextLog());
}

void PrintCacheLogEntries(RecoveryReader &rd, bool verbose, /*INOUT*/ int &num)
  throw (VestaLog::Error, VestaLog::Eof)
{
    while (!rd.eof()) {
      CacheLog::Entry *entry = NEW_CONSTR(CacheLog::Entry, (rd));
      if (verbose) {
	cout << "*** Entry " << ++num << " ***" << endl;
	entry->DebugFull(cout);
	cout << endl;
      } else {
	cout << ++num << ". ";
	entry->Debug(cout, /*indent=*/ 0);
      }
      cout.flush();
      delete entry;
    }
}

void PrintCacheLog(bool verbose) throw (VestaLog::Error, VestaLog::Eof)
{
    int num = 0;
    VestaLog cacheLog;
    cacheLog.open(Config_CacheLogPath.chars(), -1, /*readonly=*/ true);
    cout << ">>>>>> CacheLog <<<<<<" << endl << endl;

    fstream *chkpt = cacheLog.openCheckpoint();
    if (chkpt != (fstream *)NULL) {
	RecoveryReader rd(chkpt);
	PrintCacheLogEntries(rd, verbose, /*INOUT*/ num);
	chkpt->close();
    }
    do {
	RecoveryReader rd(&cacheLog);
	PrintCacheLogEntries(rd, verbose, /*INOUT*/ num);
    } while (cacheLog.nextLog());
    if (num == 0) cout << "<<Empty>>" << endl;
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
	PrintEmptyPKLog();
	PrintCacheLog(verbose);
    }
    catch (VestaLog::Error) {
	cerr << "VestaLog fatal error: ";
        cerr << "failed reading cache log; exiting..." << endl;
	exit(1);
    }
    catch (VestaLog::Eof) {
	cerr << "VestaLog fatal error: ";
        cerr << "unexpected EOF while reading cache log; exiting..." << endl;
	exit(1);
    }
    return 0;
}
