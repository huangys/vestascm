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

// Last modified on Tue Mar 22 13:01:41 EST 2005 by ken@xorian.net  
//      modified on Sat Feb 12 17:42:46 PST 2000 by mann  
//      modified on Fri Jun 13 11:08:50 PDT 1997 by heydon

// Program to test the "chars_seq.H" interface

#include <Basics.H>
#include <chars_seq.H>

using std::endl;
using std::cout;
using std::cerr;

const int MaxLen = 76;
const int NumStrings = 10000;

static char buff[MaxLen+1];

void UpdateBuff(int len) throw ()
{
    if (len > 0) {
	int modTen = len % 10;
	if (modTen != 0) {
	    buff[len-1] = '0' + modTen;
	} else {
	    for (int j = len - 10; j < len - 1; j++) {
		buff[j] = '.';
		buff[len - 1] = '0' + (len / 10);
	    }
	}
    }
    buff[len] = '\0';
}

void Verify(const chars_seq &cs, char *nm) throw ()
{
    cerr << "Verifying chars_seq " << nm << "...";

    // verify length
    if (cs.length() != NumStrings) {
	cerr << "\n  Error: incorrect length " << cs.length()
	     << "; should be " << NumStrings << endl;
    }

    // verify contents
    for (int i = 0; i < cs.length(); i ++) {
        if (cs[i] == NULL)
	  {
	    cerr << "\n Error: string " << i << " is NULL" << endl;
	  }
	UpdateBuff(i % MaxLen);
	if (strcmp(buff, cs[i]) != 0) {
	  cerr << "\n Error: string " << i << " is incorrect:" << endl
	       << "  is:        " << cs[i] << endl
	       << "  should be: " << buff << endl;
	}
    }
    cerr << "done\n";
}

int main() 
{
    chars_seq cs1;                // zero initial storage
    chars_seq cs2(10, 100);       // small initial storage
    chars_seq cs3(NumStrings);    // correct len, but no bytes
    chars_seq cs4(NumStrings, (MaxLen/2)*NumStrings); // no expansion reqd

    // add strings to sequences
    cerr << "Initializing sequences...";
    for (int i = 0; i < NumStrings; i++) {
        // form string
	UpdateBuff(i % MaxLen);

        // add string to each "chars_seq"
	cs1.append(buff);
	cs2.append(buff);
	cs3.append(buff);
	cs4.append(buff);
    }
    cerr << "done\n";

    // verify results
    Verify(cs1, "cs1");
    Verify(cs2, "cs2");
    Verify(cs3, "cs3");
    Verify(cs4, "cs4");
}
