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

// Created on Sat May 31 16:47:46 PDT 1997 by heydon
// Last modified on Mon May 23 22:09:59 EDT 2005 by ken@xorian.net  
//      modified on Mon Jun  9 15:55:35 EDT 2003 by scott@scooter.cx
//      modified on Sat Feb 12 12:02:53 PST 2000 by mann  
//      modified on Sun Aug 22 16:10:44 PDT 1999 by heydon

// VCacheMonitor -- a program to monitor the Vesta-2 cache server state

#include <stdio.h>
#include <time.h>
// necessary to make up for broken header file
extern "C" char* _Pctime_r(const time_t *timer, char *buffer);

// basics
#include <Basics.H>
#include <SRPC.H>

// cache-common
#include <CacheArgs.H>
#include <CacheState.H>

// local includes
#include "DebugC.H"

using std::cout;
using std::cerr;
using std::endl;

static void Error(char *msg, char *arg = (char *)NULL) throw ()
{
    cerr << "Error: " << msg;
    if (arg != (char *)NULL) cerr << ": `" << arg << "'";
    cerr << endl;
    cerr << "SYNTAX: VCacheMonitor [ -update time ] "
         << "[ -ts time ] [ -n num ] [ -rows num ]" << endl;
    exit(1);
}

static void PrintHeader() throw ()
{
    const int numCols = 15;
    cout << endl;
    cout << "          FREE LOOK  ADD  NUM  NUM  NUM ";
    cout << "   NEW       OLD     NUM  NUM  MPK" << endl;
    cout << "SIZE  RES VARS   UP  ENT VMPK VPKS ENTS ";
    cout << "ENTS PKLS ENTS PKLS   HF  DEL WEED" << endl;
    for (int i = 0; i < numCols; i++) cout << "---- ";
    cout << endl;
}

static void PrintVal(unsigned int val) throw ()
{
    const unsigned int OneK = 1024;
    const unsigned int OneM = OneK * OneK;
    const unsigned int OneG = OneM * OneK;
    char buff[10];
    if (val < OneK) {
	sprintf(buff, "%4d", val);
	cout << buff;
    } else {
      char suffix = 'K';
      if (val >= OneG)
	{
	  val /= OneM;
	  suffix = 'G';
	}
      else if (val >= OneM)
	{
	  val /= OneK;
	  suffix = 'M';
	}

      float ratio = ((float)val) / ((float)OneK);
      if (ratio < 9.95) { // assures won't round up to 10.0
	sprintf(buff, "%3.1f", ratio);
      } else {
	val = (val + (OneK / 2)) / OneK; // round
	sprintf(buff, "%3d", val);
      }
      cout << buff << suffix;
    }
    cout << ' ';
}

/* We want to print a blank line before printing the cache Id information
   except for the very first time "EstablishConnection" is called. */
static bool firstTime = true;

static void EstablishConnection(const DebugC &cache) throw (SRPC::failure)
{
    CacheId id;
    cache.GetCacheId(/*OUT*/ id);
    FP::Tag instance_fp;
    cache.GetCacheInstance(/*OUT*/ instance_fp);
    if (firstTime) firstTime = false;
    else cout << endl;
    id.Print(cout, /*indent=*/ 0);
    cout << "Instance FP:   " << instance_fp << endl;
    cout.flush();
}

static void NewCacheState(const int ts, const DebugC &cache,
  /*INOUT*/ CacheState* &oldState, /*INOUT*/ CacheState* &newState,
  CacheState* spareState, /*INOUT*/ int &rowsPrinted) throw (SRPC::failure)
{
    // time at which to print next time stamp
    static time_t nextStamp = -1;

    // get new state
    cache.GetCacheState(/*OUT*/ *newState);

    // test if cache has been restarted since last sample
    if (oldState != (CacheState *)NULL &&
	(newState->cnt.freeVarsCnt < oldState->cnt.freeVarsCnt ||
	 newState->cnt.lookupCnt < oldState->cnt.lookupCnt ||
	 newState->cnt.addEntryCnt < oldState->cnt.addEntryCnt)) {
	EstablishConnection(cache);
	PrintHeader();
	spareState = oldState;
	oldState = (CacheState *)NULL;
	rowsPrinted = 0;
    }

    // see if it is time to print a timestamp
    if (ts >= 0) {
	time_t now = time((time_t *)NULL);
	assert(now >= 0);
	if (now >= nextStamp) {
	    // don't print a timestamp the first time
	    if (nextStamp >= 0) {
		char buffer[64];
		(void) ctime_r(&now, buffer);
		buffer[strlen(buffer)-1] = '\0'; // suppress '\n'
		cout << "------------------------ " << buffer
		     << " ------------------------" << endl;
	    }
	    nextStamp = now + ts;
	}
    }

    // print row values
    PrintVal(newState->virtualSize);
    PrintVal(newState->physicalSize);
    if (oldState == (CacheState *)NULL) {
	for (int i = 0; i < 3; i++) cout << " N/A ";
    } else {
	MethodCnts &c2 = newState->cnt, &c1 = oldState->cnt;
	PrintVal(c2.freeVarsCnt - c1.freeVarsCnt);
	PrintVal(c2.lookupCnt - c1.lookupCnt);
	PrintVal(c2.addEntryCnt - c1.addEntryCnt);
    }
    PrintVal(newState->vmpkCnt);
    PrintVal(newState->vpkCnt);
    PrintVal(newState->entryCnt);
    PrintVal(newState->s.newEntryCnt);
    PrintVal(newState->s.newPklSize);
    PrintVal(newState->s.oldEntryCnt);
    PrintVal(newState->s.oldPklSize);
    PrintVal(newState->hitFilterCnt);
    PrintVal(newState->delEntryCnt);
    PrintVal(newState->mpkWeedCnt);
    cout << endl;
    rowsPrinted++;

    {   // swap "oldState" and "newState"
	CacheState *tmp = oldState;
	oldState = newState;
	newState = (tmp == (CacheState *)NULL) ? spareState : tmp;
    }
}

static void Monitor(int update, int ts, int num, int rows, int check) throw ()
{
    // establish connection to cache
    DebugC cache(CacheIntf::None, true, update*2);

    // connection status
    enum ConnState { Disconnected, Connected };
    ConnState connState = Disconnected;

    // cache state
    CacheState state1, state2;
    CacheState *oldState = (CacheState *)NULL;
    CacheState *newState = &state1, *spareState = &state2;

    // loop until enough rows have been printed
    int rowsPrinted = 0;
    while (num < 0 || rowsPrinted < num) {
	try {
	    switch (connState) {
	      case Disconnected:
		EstablishConnection(cache);
		connState = Connected;
		assert(rowsPrinted == 0);
		// fall through immediately!
	      case Connected:
		if (rowsPrinted == 0 || (rows > 0 && rowsPrinted % rows == 0))
		    PrintHeader();
		NewCacheState(ts, cache, /*INOUT*/ oldState, 
                  /*INOUT*/ newState, spareState, /*INOUT*/ rowsPrinted);
		break;
	    }
	}
	catch (SRPC::failure) {
	    if(check) exit(1);
	    cout << "Error contacting cache server; retrying..." << endl;
	    firstTime = false;
	    connState = Disconnected;
	    oldState = (CacheState *)NULL;
	    spareState = (newState == &state1) ? &state2 : &state1;
	    rowsPrinted = 0;
	}

	// pause for "update" seconds
	if(check) exit(0);
	Basics::thread::pause(update);
    }
}

/* Return the number of seconds denoted by "tm". Except for an optional
   suffix character, "tm" must be a non-negative integer value. If
   present, the suffix character specifies a time unit as follows:

     s  seconds (default)
     m  minutes
     h  hours
     d  days
*/
static int ParseTime(char *flag, char *tm) throw ()
{
    char errBuff[80];
    int len = strlen(tm);
    if (len == 0) {
	sprintf(errBuff, "argument to `%s' is empty", flag);
        Error(errBuff, tm);
    }
    char lastChar = tm[len - 1];
    int multiplier = 1;
    switch (lastChar) {
      case 'd':
	multiplier *= 24;
	// fall through
      case 'h':
	multiplier *= 60;
	// fall through
      case 'm':
	multiplier *= 60;
	// fall through
      case 's':
	tm[len - 1] = '\0';
	break;
      default:
	if (!isdigit(lastChar)) {
	    sprintf(errBuff, "illegal unit specifier `%c' in `%s' argument",
		    lastChar, flag);
	    Error(errBuff, tm);
	}
    }
    int res;
    if (sscanf(tm, "%d", &res) != 1) {
	sprintf(errBuff, "argument to `%s' not an integer", flag);
	Error(errBuff, tm);
    }
    return multiplier * res;
}

int main(int argc, char *argv[])
{
    int update = 10; // default: update every 10 seconds
    int ts = -1;     // default: do not print any timestamps
    int num = -1;    // default: update indefinitely
    int rows = -1;   // default: only write header lines once
    int arg = 1;
    int check = 0;

    // process command-line
    while (arg < argc && *argv[arg] == '-') {
	if (CacheArgs::StartsWith(argv[arg], "-update")) {
	    if (++arg < argc) {
		update = ParseTime("-update", argv[arg++]);
	    } else {
		Error("no argument supplied for `-update'");
	    }
	} else if (CacheArgs::StartsWith(argv[arg], "-ts")) {
	    if (++arg < argc) {
		ts = ParseTime("-ts", argv[arg++]);
	    } else {
		Error("no argument supplied for `-ts'");
	    }
	} else if (strcmp(argv[arg], "-n") == 0) {
	    if (++arg < argc) {
		if (sscanf(argv[arg], "%d", &num) != 1) {
		    Error("argument to `-n' not an integer", argv[arg]);
		}
		arg++;
	    } else {
		Error("no argument supplied for `-n'");
	    }
	} else if (CacheArgs::StartsWith(argv[arg], "-rows")) {
	    if (++arg < argc) {
		if (sscanf(argv[arg], "%d", &rows) != 1) {
		    Error("argument to `-rows' not an integer", argv[arg]);
		}
		arg++;
	    } else {
		Error("no argument supplied for `-rows'");
	    }
	} else if (CacheArgs::StartsWith(argv[arg], "-check")) {
		check = 1;
		arg++;
	} else {
	    Error("unrecognized option", argv[arg]);
	}
    }
    if (arg < argc) Error("too many command-line arguments");
    Monitor(update, ts, num, rows, check);
    return(0);
}
