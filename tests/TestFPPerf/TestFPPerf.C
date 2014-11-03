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

// Last modified on Fri Jul 16 17:19:13 EDT 2004 by ken@xorian.net  
//      modified on Sat Feb 12 17:36:01 PST 2000 by mann  
//      modified on Fri Dec 12 08:57:40 PST 1997 by heydon

#include <stdlib.h>

extern "C" {
#include <sys/time.h>
}

#if defined (__linux__)
#include <unistd.h>
#endif // __linux

#include "Basics.H"
#include "FP.H"

using std::ostream;
using std::cout;
using std::endl;

ostream& operator << (ostream &os, const RawFP &fp) throw ()
{
    char buff[17];
    for (int i = 0; i < PolyVal::WordCnt; i++) {
	sprintf(buff, "%016" FORMAT_LENGTH_INT_64 "x", fp.w[i]);
	os << buff;
	if (i < PolyVal::WordCnt - 1) os << " ";
    }
    return os;
}

int main(int argc, char *argv[]) 
{
    int arg1 = (argc > 1) ? atoi(argv[1]) : 10;
    int arg2 = (argc > 2) ? atoi(argv[2]) : 1;
    Text longS("a");
    int i;
    for (i = 0; i < arg1; i++) {
	longS += longS;
    }
    int longSLen = longS.Length();
    struct timeval start;
    int iterCnt = (1 << arg2);
    {
	FP::Tag tag(longS.cchars(), longSLen);
	RawFP rawTag;
	tag.Unpermute(/*OUT*/ rawTag);
	cout << "After first iteration: Raw FP::Tag(longS) = " <<rawTag<<endl;
    }
    FP::Tag tag("");
    (void) gettimeofday(&start, NULL);
    for (i = 0; i < iterCnt; i++) {
	tag.Extend(longS.cchars(), longSLen);
    }
    struct timeval end;
    (void) gettimeofday(&end, NULL);
    RawFP rawTag;
    tag.Unpermute(/*OUT*/ rawTag);
    cout << "After last iteration:  Raw FP::Tag(longS) = " <<rawTag<<endl;
    int totalLen = iterCnt * longSLen;
    cout << "Done fingerprinting " << totalLen << " bytes" << endl;
    int startT = (start.tv_sec * 1000) + (start.tv_usec / 1000);
    int endT = (end.tv_sec * 1000) + (end.tv_usec / 1000);
    int totalTm = (endT - startT) + 1;
    cout << "Elapsed time = " << totalTm << "ms" << endl;
    cout << "Fingerprinted " << ((float)totalLen/(float)totalTm)
	<< " bytes/ms" << endl;
    
}
