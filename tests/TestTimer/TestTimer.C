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

// Last modified on Thu Aug  5 10:18:06 EDT 2004 by ken@xorian.net  
//      modified on Sat Feb 12 17:31:31 PST 2000 by mann  
//      modified on Fri Jul 19 09:38:16 PDT 1996 by heydon

// Program to test the "Timer.H" interface

#include <iostream>
#include <iomanip>

#include <Basics.H>
#include "Timer.H"

using std::ios;
using std::cout;
using std::cerr;
using std::endl;
using std::setprecision;
using std::setiosflags;

const int NumPasses    = 1000;
const int InnerLoopCnt = 15000;

void Error(char *msg) throw ()
{
    cerr << "Error: " << msg << '\n';
    cerr << "Exiting...\n";
    exit(1);
}

int main() 
{
    Timer::T timer;

    // verify default timer
    if (timer.Elapsed() != 0UL) {
	Error("default timer not initialized with 0 elapsed time");
    }

    // print granularity
    cout << "Timer granularity = " << Timer::Grain << " usecs\n";

    // try many empty timer runs
    Timer::MicroSecs total = 0UL, t, maxT, minT;
    int nonZeroCnt = 0;
    int i;
    for (i = 0; i < NumPasses; i++) {
	timer.Start();
	t = timer.Stop();
	if (t != 0UL) {
	    nonZeroCnt++; // set breakpoint here
	}
	total += t;
	if (i == 0) {
	    maxT = minT = t;
	} else {
	    maxT = max(maxT, t);
	    minT = min(minT, t);
	}
    }
    if (total != timer.Elapsed()) {
	Error("total elapsed time not recorded properly");
    }
    cout << "Average time for empty run = "
      << setprecision(1) << setiosflags(ios::fixed)
      << ((float)total/(float)NumPasses) << " usecs";
    cout << "; min/max = " << minT << '/' << maxT << " usecs\n";

    // test "Reset"
    Timer::MicroSecs elapsed = timer.Reset();
    if (elapsed != total) {
	Error("incorrect value returned by 'Reset'");
    }
    if (timer.Elapsed() != 0UL) {
	Error("'Reset' failed");
    }
    total = 0UL;

    // try many non-empty timer runs
    for (i = 0; i < NumPasses; i++) {
	timer.Start();
	for (int j = 0; j < InnerLoopCnt; j++) {
	    int k = j / 2; k *= 3;
	}
	t = timer.Stop();
	total += t;
	if (i == 0) {
	    maxT = minT = t;
	} else {
	    maxT = max(maxT, t);
	    minT = min(minT, t);
	}
    }
    if (total != timer.Elapsed()) {
	Error("total elapsed time not recorded properly");
    }
    cout << "Average time for non-empty run = "
      << setprecision(1) << setiosflags(ios::fixed)
      << ((float)total/(float)NumPasses) << " usecs";
    cout << "; min/max = " << minT << '/' << maxT << " usecs\n";

    cout << "All tests passed!\n";
    return 0;
}
