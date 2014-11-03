// TestAtomicFile.C

// Copyright (C) 2001, Compaq Computer Corporation

// This file is part of Vesta.

// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// Last modified on Thu Feb 16 10:02:14 EST 2006 by ken@xorian.net
//      modified on Mon May  3 15:55:17 PDT 1999 by heydon
//      modified on Thu Jan 19 12:19:25 PST 1995 by mann
//
// Test program for AtomicFile interface
//

#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "AtomicFile.H"

using std::cout;
using std::cerr;
using std::cin;
using std::endl;
using std::ios;

int main()
{
    char reply1[999];
    cout << "open filename: ";
    cin >> reply1; 
    AtomicFile foo;
    foo.open(reply1, ios::out);
    if (!foo) {
      cerr << "error on open: " << strerror(errno)
	   << " (" << errno << ")" << endl;
      return 1;
    }
    cout << "write data: ";
    cin >> reply1; 
    foo << reply1;
    if (!foo) {
      cerr << "error on write: " << strerror(errno)
	   << " (" << errno << ")" << endl;
      return 1;
    }
    cout << "cleanup? ";
    cin >> reply1; 
    if (reply1[0] == 'y') {
	AtomicFile::cleanup(".");
    }
    cout << "flush? ";
    cin >> reply1; 
    if (reply1[0] == 'y') {
	foo.flush();
	if (foo.fail()) {
	  cerr << "error on flush: " << strerror(errno)
	       << " (" << errno << ")" << endl;
	  return 1;
	}
    }
    cout << "close? ";
    cin >> reply1; 
    if (reply1[0] == 'y') {
	foo.close();
	if (!foo) {
	  cerr << "error on close: " << strerror(errno)
	       << " (" << errno << ")" << endl;
	  return 1;
	}
    }
    do {
	cout << "return? ";
	cin >> reply1; 
    } while (reply1[0] != 'y');
    return 0;
}
