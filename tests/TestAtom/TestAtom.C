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

// Last modified on Thu May 27 00:26:34 EDT 2004 by ken@xorian.net
//      modified on Mon May  3 16:04:42 PDT 1999 by heydon

// Program to test the implementation of the Atom interface

#include <Basics.H>
#include "Atom.H"

using std::cerr;
using std::endl;

static int error_cnt = 0;

static void failure(char *msg) throw ()
{
    cerr << "Failure: " << msg << endl;
    error_cnt++;
}

int main()
{
    // Test equality of empty strings.
    Atom a1, a2, a3(""), a4("");
    if (a1 != a2) failure("default (empty) atoms not equal");
    if (a1 != a3) failure("default atom and empty atom not equal");
    if (a3 != a4) failure("empty atoms not equal");

    // Test general equality and inequality
    const char *str = "atom test";
    Atom a5(str), a6(str), a7("ATOM TEST");
    if (a5 != a6) failure("equal atoms not considered equal");
    if (!(a5 == a6)) failure("equal atoms not considered equal");
    if (a5 == a7) failure("unequal atoms considered equal");
    if (!(a5 != a7)) failure("unequal atoms considered equal");

    // Test various constructors
    Text txt(str);
    Atom a8(a5), a9(txt), a10('c'), a11("c"), a12(str), a13(str, strlen(str));
    if (a5 != a8) failure("atom constructor from atom failed");
    if (a5 != a9) failure("atom constructor from text failed");
    if (a10 != a11) failure("atom constructor from char failed");
    if (a5 != a12) failure("atom constructor from string failed");
    if (a5 != a13) failure("atom constructor from bytes failed");

    // Test assignment
    Atom a14;
    a14 = str;
    if (a5 != a14) failure("atom assignment operator from string failed");
    a14 = txt;
    if (a5 != a14) failure("atom assignment operator from text failed");
    a14 = a5;
    if (a5 != a14) failure("atom assignment operator from atom failed");

    // Test destructive concatenation
    char *cat = "cat", *dog = "dog";
    Text t1(cat), t2(dog), t3 = t1 + t2;
    Atom a15(t3), a16(cat), a17(cat), a18(cat), a19(dog);
    a16 += dog;
    if (a15 != a16) failure("atom += on string failed");
    a17 += t2;
    if (a15 != a17) failure("atom += on text failed");
    a18 += a19;
    if (a15 != a18) failure("atom += on atom failed");

    if (error_cnt > 0)
	cerr << error_cnt << " failures reported\n";
    else
	cerr << "All tests passed!\n";
    return error_cnt;
}
