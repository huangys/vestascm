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

// Last modified on Thu Apr 21 09:32:54 EDT 2005 by irina.furman@intel.com 
//      modified on Wed Jun  2 18:21:54 EDT 2004 by ken@xorian.net        
//      modified on Mon Jul 29 14:55:23 EDT 2002 by kcschalk@shr.intel.com
//      modified on Mon May  3 16:04:41 PDT 1999 by heydon

// Test of the "SharedTable" implementation

#include <Basics.H>
#include "IntKey.H"
#include "SharedTable.H"

const Word Factor = 2;
const Word MinEntries = 10;
const Word MaxEntries = 170000;

typedef SharedTable<IntKey,int>::KVPair IntIntPair;
typedef SharedTable<IntKey,int>::T IntIntSTbl;
typedef SharedTable<IntKey,int>::Iterator IntIntSIter;

void RunTest(IntIntSTbl &tbl, Word numEntries) throw ()
{
    Word i;
    bool inTable;

    // fill table
    for (i = 0; i < numEntries; i++) {
	IntKey k(i);
	IntIntPair *pr = NEW_CONSTR(IntIntPair, (k, i));
	(void)tbl.Put(pr);
    }

    // print size
    assert(tbl.Size() == numEntries);

    // print expected entries
    for (i = 0; i < numEntries; i++) {
	IntKey k(i); IntIntPair *pr;
	inTable = tbl.Get(k, /*OUT*/ pr);
	assert(inTable && (i == pr->val));
    }

    // iterate over table
    IntIntSIter it(&tbl);
    IntIntPair *pr;
    bool *eltInTbl =  NEW_PTRFREE_ARRAY(bool, numEntries);
    for (i = 0; i < numEntries; i++) eltInTbl[i] = false;
    while (it.Next(/*OUT*/ pr)) {
	i = pr->key.Val();
	assert(i < numEntries); // Note: i >= 0 because it's unsigned
	assert((i == pr->val) && !eltInTbl[i]);
	eltInTbl[i] = true;
    }
    for (i = 0; i < numEntries; i++) assert(eltInTbl[i]);
    delete[] eltInTbl;

    // iterate again (test "Reset")
    it.Reset();
    for (i = 0; it.Next(/*OUT*/ pr); i++) /*SKIP*/;
    assert(i == tbl.Size());

    // delete entries
    for (i = 0; i < numEntries; i++) {
	IntKey k(i);
	inTable = tbl.Delete(k, /*OUT*/ pr);
	assert(inTable && (pr->val == i));
	inTable = tbl.Get(k, /*OUT*/ pr);
	assert(!inTable);
    }
    assert(tbl.Size() == 0);
}

using std::cout;
using std::endl;

int main()
{
    IntIntSTbl tbl(MinEntries);
    Word numEntries = MinEntries;

    while (true) {
	(cout << "Testing table of size " << numEntries << "...\n").flush();
	RunTest(tbl, numEntries);
	numEntries *= Factor;
	if (numEntries > MaxEntries) break;
	tbl.Init();
    }
    cout << "Passed all tests!" << endl;

    return 0;
}
