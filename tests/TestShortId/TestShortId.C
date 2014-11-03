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
// TestShortId.C
// Last modified on Tue Aug  3 12:52:14 EDT 2004 by ken@xorian.net
//      modified on Thu Jul 20 14:24:28 PDT 2000 by mann
//
// Simple client to test the ShortId layer of the repository server
//

#include "Basics.H"
#include "SourceOrDerived.H"
#include "SRPC.H"
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>

using std::cout;
using std::cin;
using std::endl;
using std::hex;
using std::dec;
using std::ios;

int main(int argc, char *argv[])
{
    for (;;) {
	try {
	    char typein[1024];
	    char junk;
	    long ltmp;
	    int mode_read;
	    ios::openmode mode_used;
	    bool leafflag, force;
	    SourceOrDerived sd;
	    ShortId sid;
	    time_t time;

	    cout << "(q)uit, (c)reate, (o)pen, (l)eafShortId, (s)hortIdToName,\n"
	      << " (k)eepDerived, check(p)oint, (g)etWeedingInfo, to(u)ch: ";
	    cin.get(typein, sizeof(typein));
	    cin.get(junk);
	    switch (typein[0]) {
	    case 'c':
		for (;;) {
		    cout << "(l)eaf, (n)onleaf: ";
		    cin.get(typein, sizeof(typein));
		    cin.get(junk);
		    switch (typein[0]) {
		    case 'l':
			leafflag = true;
			break;
		    case 'n':
			leafflag = false;
			break;
		      default:
			continue;
		    }
		    break;
		}
		cout <<
		  "mode (in=1 out=2 ate=4 app=8 trunc=16): ";
		cin >> mode_read;
		cin.get(junk);
		if(mode_read & 1)
		  {
		    mode_used = ios::in;
		    if(mode_read & 2)
		      {
			mode_used |= ios::out;
		      }
		  }
		else if(mode_read & 2)
		  {
		    mode_used = ios::out;
		  }
		if(mode_read & 4)
		  mode_used |= ios::ate;
		if(mode_read & 8)
		  mode_used |= ios::app;
		if(mode_read & 16)
		  mode_used |= ios::trunc;
		cout << hex << sd.create(leafflag, mode_used) << dec << endl;
		if (sd.fail()) {
		  int saved_errno = errno;
		  cout << "error: " << Basics::errno_Text(saved_errno) << endl;
		}
		sd.close();
		break;
	    
	    case 'o':
		cout << "ShortId: ";
		cin >> hex >> ltmp >> dec;
		cout <<
		  "mode (in=1 out=2 ate=4 app=8 trunc=16): ";
		cin >> mode_read;
		cin.get(junk);
		if(mode_read & 1)
		  {
		    mode_used = ios::in;
		    if(mode_read & 2)
		      {
			mode_used |= ios::out;
		      }
		  }
		else if(mode_read & 2)
		  {
		    mode_used = ios::out;
		  }
		if(mode_read & 4)
		  mode_used |= ios::ate;
		if(mode_read & 8)
		  mode_used |= ios::app;
		if(mode_read & 16)
		  mode_used |= ios::trunc;
		sd.open((ShortId) ltmp, mode_used);
		if (sd.fail()) {
		  cout << "error: " << Basics::errno_Text(errno) << endl;
		}
		sd.close();
		break;

	    case 'l':
		cout << "ShortId: ";
		cin >> hex >> ltmp >> dec;
		cin.get(junk);
		if (sd.leafShortId((ShortId) ltmp)) {
		    cout << "leaf\n";
		} else {
		    cout << "nonleaf\n";
		}
		break;

	    case 's':
		cout << "ShortId: ";
		cin >> hex >> ltmp >> dec;
		cin.get(junk);
		cout << sd.shortIdToName((ShortId) ltmp) << endl;
		break;

	    case 'q':
		exit(0);

	    case 'k':
		cout << "ds (shortId): ";
		cin >> hex >> sid >> dec;
		cout << "dt (time_t): ";
		cin >> time;
		cout << "force (0 or 1): ";
		cin >> force;
		ltmp = SourceOrDerived::keepDerived(sid, time, force);
		if (ltmp == 0)
		  cout << "ok" << endl;
		else
		  cout << Basics::errno_Text(ltmp) << endl;
		break;

	    case 'p':
		SourceOrDerived::checkpoint();
		break;

	    case 'g':
		{
		    ShortIdsFile ds, ss;
		    time_t dt, st;
		    bool swip, dip, ddun, cip;
		    SourceOrDerived::getWeedingState(ds, dt, ss, st,
						     swip, dip, ddun, cip);
		    cout << "ds = " << hex << ds << dec << ", dt = " << dt 
		      << ", ss = " << hex << ss << dec << ", st = " << st
			<< endl
			  << "sourceWeedInProgress = " << (int)swip
			    << ", deletionsInProgress = " << (int)dip
			      << ", deletionsDone = " << (int)ddun
				<< ", checkpointInProgress = " << (int)cip
				  << endl;
		}
		break;
	    case 'u':
		cout << "ShortId: ";
		cin >> hex >> sid >> dec;
		cin.get(junk);
		if (SourceOrDerived::touch(sid)) {
		    cout << "ok" << endl;
		} else {
		  cout << "error: " << Basics::errno_Text(errno) << endl;
		}
		break;

	      default:
		break;
	    }
	} catch (SRPC::failure f) {
	    cout << "SRPC::failure " << f.msg << endl;
	    exit(1);
	}
    }

    //exit(0); // not reached
}
