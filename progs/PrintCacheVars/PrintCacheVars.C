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

// Read the cache stable variables and print their contents

#include <Basics.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <FP.H>
#include <CacheArgs.H>
#include <CacheIntf.H>
#include <CacheConfig.H>
#include <BitVector.H>
#include <PKPrefix.H>
#include <VestaLog.H>

using std::ifstream;
using std::cout;
using std::cerr;
using std::endl;

void PrintDeleting() throw (FS::Failure) 
{
  bool deleting;

  try
    {
      ifstream ifs; 
      FS::OpenReadOnly(Config_DeletingFile, /*OUT*/ ifs); 
      FS::Read(ifs, (char *)(&(deleting)), sizeof(deleting));
      FS::Close(ifs);
    }
  catch(FS::DoesNotExist)
    {
      deleting = false;
    }

  cout << "deleting = " << (deleting ? "true" : "false")
       << endl << endl;
} 

void PrintHitFilter(bool verbose) throw (FS::Failure)
{
  BitVector hitFilter;

  try
    {
      ifstream ifs;
      FS::OpenReadOnly(Config_HitFilterFile, /*OUT*/ ifs);
      hitFilter.Read(ifs);
      FS::Close(ifs);
    }
  catch(FS::DoesNotExist)
    {
      // Ignore (hitFilter is empty)
    }

  cout << "hitFilter = ";
  if(verbose)
    {
      cout << endl;
      hitFilter.PrintAll(cout, 4);
      if(hitFilter.Cardinality() > 0)
	{
	  cout << "  (" << hitFilter.Cardinality() << " total)" << endl;
	}
    }
  else
    {
      hitFilter.Print(cout);
      cout << endl;
    }
  cout << endl;
}

void PrintMPKsToWeed(bool verbose) throw (FS::Failure, VestaLog::Eof, VestaLog::Error)
{
  int nextMPKToWeed = 0;
  {
    VestaLog weededMPKsLog;
    weededMPKsLog.open(Config_WeededLogPath.chars(), -1, /*readonly=*/ true);
    do {
      while (!weededMPKsLog.eof()) {
	int delta;
	weededMPKsLog.readAll((char *)(&delta), sizeof(delta));
	nextMPKToWeed += delta;
      }
    } while (weededMPKsLog.nextLog());
  }

  cout << "nextMPKToWeed = " << nextMPKToWeed << endl << endl;

  PKPrefix::List mpksToWeed;

  ifstream ifs;
  try
    {
      FS::OpenReadOnly(Config_MPKsToWeedFile, /*OUT*/ ifs);
      mpksToWeed.Read(ifs);
      FS::Close(ifs);
    }
  catch (FS::DoesNotExist)
    {
      /* SKIP -- use initial (empty) value for "mpksToWeed" */
    }

  cout << "mpksToWeed = ";
  if(mpksToWeed.len > 0)
    {
      cout << "(" << mpksToWeed.len << " total)";
    }
  if(verbose || (mpksToWeed.len == 0))
    {
      mpksToWeed.Print(cout, 2);
    }
  else if(nextMPKToWeed < mpksToWeed.len)
    {
      cout << endl;

      // The cache logs every 10th weeded MPK, so in summary mode we
      // show a window of 11 around the current one
#define MPK_WEED_WINDOW 11

      if(nextMPKToWeed > MPK_WEED_WINDOW)
	{
	  cout << "    ..." << endl;
	}
      for(unsigned int i = max(0, nextMPKToWeed-MPK_WEED_WINDOW);
	  i < min(mpksToWeed.len, nextMPKToWeed+MPK_WEED_WINDOW+1); 
	  i++)
	{
	  if(i == nextMPKToWeed)
	    {
	      cout << " >> ";
	    }
	  else
	    {
	      cout << "    ";
	    }
	  cout << "pfx[" << i << "] = "
	       << mpksToWeed.pfx[i] << endl;
	}
      if((nextMPKToWeed+MPK_WEED_WINDOW+1) < (mpksToWeed.len))
	{
	  cout << "    ..." << endl;
	}
    }
  else
    {
      cout << " <<None Left>>" << endl;
    }
  cout << endl;
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

    // print variables
    try
      {
	PrintDeleting();
	PrintHitFilter(verbose);
	PrintMPKsToWeed(verbose);
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
