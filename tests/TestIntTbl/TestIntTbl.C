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

// Last modified on Thu Apr 21 09:33:32 EDT 2005 by irina.furman@intel.com 
//      modified on Thu May 27 00:47:48 EDT 2004 by ken@xorian.net        
//      modified on Mon Jul 29 14:51:55 EDT 2002 by kcschalk@shr.intel.com
//      modified on Mon May  3 16:04:41 PDT 1999 by heydon

#include <Basics.H>
#include "Table.H"
#include "IntKey.H"
#include "Generics.H"

const Word Factor = 2;
const Word MinEntries = 10;
const Word MaxEntries = 10000;

void RunTest(IntIntTbl &tbl, Word numEntries) throw ()
{
    Word i;
    bool inTable;

    // fill table
    for (i = 0; i < numEntries; i++) {
	IntKey k(i);
	(void)tbl.Put(k, i);
    }

    // print size
    assert(tbl.Size() == numEntries);

    // print expected entries
    for (i = 0; i < numEntries; i++) {
	IntKey k(i); int v;
	inTable = tbl.Get(k, v);
	assert(inTable && (i == v));
    }

    // iterate over table
    IntIntIter it(&tbl);
    IntKey k; int v;
    bool *eltInTbl = NEW_PTRFREE_ARRAY(bool, numEntries);
    for (i = 0; i < numEntries; i++) eltInTbl[i] = false;
    while (it.Next(k, v)) {
	i = k.Val();
	assert(i < numEntries); // Note: i >= 0 because it's unsigned
	assert((i == v) && !eltInTbl[i]);
	eltInTbl[i] = true;
    }
    for (i = 0; i < numEntries; i++) assert(eltInTbl[i]);
    delete[] eltInTbl;

    // iterate again (test "Reset")
    it.Reset();
    for (i = 0; it.Next(k, v); i++) /*SKIP*/;
    assert(i == tbl.Size());

    // delete entries
    for (i = 0; i < numEntries; i++) {
	IntKey k(i); int v;
	inTable = tbl.Delete(k, v);
	assert(inTable && (v == i));
	inTable = tbl.Get(k, v);
	assert(!inTable);
    }
    assert(tbl.Size() == 0);
}

using std::cout;
using std::endl;

int main()
{
    IntIntTbl tbl(MinEntries);
    Word numEntries = MinEntries;

    while (1) {
      cout << "Testing table of size " << numEntries << "..." << endl;
	RunTest(tbl, numEntries);
	numEntries *= Factor;
	if (numEntries > MaxEntries) break;
	tbl.Init();
    }
    cout << "Passed all tests!" << endl;
    return 0;
}
