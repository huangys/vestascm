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

// A test of the BitVector module

#include <Basics.H>
#include "BitVector.H"

using std::cout;
using std::endl;

static bool SetResetTest() throw ()
{
    BitVector bv;
    int i, j;
    const int step = 3, lo = 20, hi = lo + (2 * step * 20);

    cout << "*** Set/Reset Bit Test ***\n\n";
    cout << "Testing upward...\n";
    cout.flush();
    for (i = lo; i <= hi; i += step) {
	bool set = bv.Set(i); assert(!set);
	for (j = 0; j < bv.Size(); j++) {
	    if (bv.Read(j) != (lo <= j && j <= i && (j-lo) % step == 0)) {
		cout << "  Error: failed at bit " << i << "!\n\n";
		return false;
	    }
	}
    }
    cout << "  Passed!\n\n";
    cout << "Testing downward...\n";
    cout.flush();
    for (i -= step; i >= lo; i -= 2 * step) {
	bool set = bv.Reset(i); assert(set);
	for (j = 0; j < bv.Size(); j++) {
	    if (bv.Read(j) !=
		((lo <= j && j < i && (j-lo) % step == 0) ||
		 (i <= j && (step+j-lo) % (2 * step) == 0))) {
                cout << "  Error: failed at bit " << i << "!\n\n";
		return false;
	    }
	}
    }
    cout << "  Passed!\n\n";
    cout.flush();
    return true;
}

static bool IteratorTest() throw ()
{
    BitVector bv;
    const int lo = 190, hi = 210;
    int i;

    cout << "*** Iterator Test ***\n";
    cout.flush();
    for (i = lo; i < hi; i++) {
	bool set = bv.Set(i * i); assert(!set);
    }
    BVIter iter(bv);
    unsigned int ix;
    for (i=lo; iter.Next(ix); i++) {
	if (ix != (i * i)) {
	    cout << "  Error: failed at bit " << ix << "!\n\n";
	    return false;
	}
    }
    cout << "  Passed!\n\n";
    cout.flush();
    return true;
}

static bool NextAvailTest() throw ()
{
    BitVector bv;
    const int MaxSqrt = 100;
    int i;

    cout << "*** NextAvail() Test ***\n\n";
    cout << "Testing upward...\n";
    cout.flush();
    for (i = 0; i < MaxSqrt * MaxSqrt; i++) {
	if (bv.NextAvail() != i) {
	    cout << "  Error: failed at bit " << i << "!\n\n";
	    return false;
	}
    }
    cout << "  Passed!\n\n";
    cout << "Testing downward by squares...\n";
    cout.flush();
    int sqr;
    for (i = MaxSqrt - 1; i >= 0; i--) {
	sqr = i * i;
	bool set = bv.Reset(sqr); assert(set);
	if (bv.NextAvail() != sqr) {
	    cout << "  Error: failed at bit " << sqr << "!\n\n";
	    return false;
	}
    }
    cout << "  Passed!\n\n";
    cout.flush();
    return true;
}

static bool NextAvailExceptTest() throw ()
{
    BitVector bv1, bv2;
    const int Max = 1000;
    int i;

    cout << "*** NextAvailExcept() Test ***\n\n";
    cout << "Test 1...\n";
    cout.flush();
    bv2.SetInterval(0, Max - 1);
    if (bv2.NextAvail() != Max) {
	cout << "  Error: failed at bit " << Max << "!\n\n";
	return false;
    }
    for (i = 1; i < Max; i++) {
	if (bv1.NextAvailExcept(&bv2) != (Max + i)) {
	    cout << "  Error: failed at bit " << (Max + i) << "!\n\n";
	    return false;
	}
    }
    bool set = bv2.Reset(Max / 2); assert(set);
    if (bv1.NextAvailExcept(&bv2) != (Max / 2)) {
	cout << "  Error: failed at bit " << (Max / 2) << "!\n\n";
	return false;
    }
    cout << "  Passed!\n\n";

    cout << "Test 2...\n";
    cout.flush();
    bv2.ResetAll();
    // set "bv2" so only unset bits are multiple of 7
    for (i = 0; i < Max; i += 7) {
	for (int j = 1; j < 7; j++) {
	    bool set = bv2.Set(i+j); assert(!set);
	}
    }
    for (i = 0; i < Max / 7; i++) {
	if (bv1.NextAvailExcept(&bv2) != (i * 7)) {
	    cout << "  Error: failed at bit " << (i*7) << "!\n\n";
	    return false;
	}
    }
    cout << "  Passed!\n\n";
    cout.flush();
    return true;
}

static int MyRand(int lower, int upper) throw ()
{
    int res;
    res = lower + (rand() % (upper-lower+1));
    return res;
}

static bool RdWrIntvlTest() throw ()
{
    BitVector bv;
    const int WdSize = 64;

    cout << "*** Read/Write Interval Test ***\n\n";

    // test writing/reading within word boundaries
    (cout << "Testing intra-words...\n").flush();
    Word mask = ~((Word) 0);
    int i;
    for (i = 0; i < WdSize; i++) {
	bv.WriteWord(i * WdSize, WdSize, mask);
    }
    mask = 1UL;
    for (i = 0; i < WdSize; i++) {
	bv.WriteWord(i * WdSize, WdSize, mask);
	mask <<= 1;
    }
    int len = WdSize;
    for (i = 0; i < WdSize; i++) {
	Word res = bv.ReadWord(i * WdSize + i, len);
	if (res != 1UL) {
	    cout << "  Error: failed on word " << i << "!\n\n";
	    cout.flush();
	    return false;
	}
	len--;
    }
    (cout << "  Passed!\n\n").flush();

    // test writing/reading across word boundaries
    (cout << "Testing small inter-words...\n").flush();
    len = WdSize - 1;
    Word val = (((Word) 1) << len) | ((Word) 1);
    for (i = 0; i < WdSize * WdSize; i++) {
	bv.WriteWord(i * len, len, val);
	val++;
    }
    val = 1UL;
    for (i = 0; i < WdSize * WdSize; i++) {
	Word res = bv.ReadWord(i * len, len);
	if (res != val) {
	    cout << "  Error: failed on sub-word " << i << "!\n\n";
	    cout.flush();
	    return false;
	}
	val++;
    }
    val = bv.ReadWord(i * WdSize + WdSize / 2, WdSize);
    if (val != 0UL) {
	cout << "  Error: failed to read 0 past end of bit vector!\n\n";
	cout.flush();
	return false;
    }
    // Now try again with most bits set
    (cout << "Testing large inter-words...\n").flush();
    val = ((~(Word) 1) >> 1);
    for (i = 0; i < WdSize * WdSize; i++) {
	bv.WriteWord(i * len, len, val);
	val--;
    }
    val = ((~(Word) 1) >> 1);
    for (i = 0; i < WdSize * WdSize; i++) {
	Word res = bv.ReadWord(i * len, len);
	if (res != val) {
	    cout << "  Error: failed on sub-word " << i << "!\n\n";
	    cout.flush();
	    return false;
	}
	val--;
    }
    val = bv.ReadWord(i * WdSize + WdSize / 2, WdSize);
    if (val != 0UL) {
	cout << "  Error: failed to read 0 past end of bit vector!\n\n";
	cout.flush();
	return false;
    }
    (cout << "  Passed!\n\n").flush();
    return true;
}

static bool SetIntvlTest() throw ()
{
    BitVector bv;
    const int WdSize = 64;
    int i, j, k, lo, hi;

    cout << "*** Set Interval Test ***\n\n";
    cout << "All intra-word intervals:\n";
    cout.flush();
    for (i = 1; i <= WdSize; i++) {
	for (j = 0; j < WdSize - i; j ++) {
	    lo = WdSize +j;
	    hi = lo + i - 1;
	    bv.ResetAll();
	    bv.SetInterval(lo, hi);
	    for (k = 0; k < bv.Size() + WdSize; k++) {
		if (bv.Read(k) != (lo <= k && k <= hi)) {
		    cout << "  Error: failed on interval [" 
                         << lo << ", " << hi << "]!\n\n";
		    cout.flush();
		    return false;
		}
	    }
	}
    }
    cout << "  Passed!\n\n";
    cout << "Inter-word intervals:\n";
    cout.flush();
    for (i = 0; i < 1000; i++) {
	lo = MyRand(0, 1000);
	hi = lo + MyRand(0, 4 * WdSize);
	bv.ResetAll();
	bv.SetInterval(lo, hi);
	for (k = 0; k < bv.Size(); k++) {
	    if (bv.Read(k) != (lo <= k && k <= hi)) {
		cout << "  Error: failed on interval [" 
		     << lo << ", " << hi << "]!\n\n";
		cout.flush();
		return false;
	    }
	}
    }
    cout << "  Passed!\n\n";
    cout.flush();
    return true;
}

static bool ResetIntvlTest() throw ()
{
    BitVector bv;
    const int WdSize = 64;
    int i, j, k, lo, hi;

    cout << "*** Reset Interval Test ***\n\n";
    cout << "All intra-word intervals:\n";
    cout.flush();
    for (i = 1; i <= WdSize; i++) {
	for (j = 0; j < WdSize - i; j ++) {
	    lo = WdSize + j;
	    hi = lo + i - 1;
	    bv.ResetAll();
	    bv.SetInterval(0, 3 * WdSize - 1);
	    bv.ResetInterval(lo, hi);
	    for (k = 0; k < bv.Size(); k++) {
		if (bv.Read(k) == (lo <= k && k <= hi)) {
		    cout << "  Error: failed on interval [" 
                         << lo << ", " << hi << "]!\n\n";
		    cout.flush();
		    return false;
		}
	    }
	}
    }
    cout << "  Passed!\n\n";
    cout << "Inter-word intervals:\n";
    cout.flush();
    for (i = 0; i < 1000; i++) {
	lo = MyRand(0, 1000);
	hi = lo + MyRand(0, 4 * WdSize);
	bv.ResetAll();
	bv.SetInterval(0, hi + WdSize);
	bv.ResetInterval(lo, hi);
	for (k = 0; k < bv.Size(); k++) {
	    if (bv.Read(k) == (lo <= k && k <= hi)) {
		cout << "  Error: failed on interval [" 
		     << lo << ", " << hi << "]!\n\n";
		cout.flush();
		return false;
	    }
	}
    }
    cout << "  Passed!\n\n";
    cout.flush();
    return true;
}

static bool CopyTest() throw ()
{
    cout << "*** Copy Test ***\n";

    // initialize bv0
    // (set bits with indexes that are perfect squares in [0,100))
    BitVector bv0(30);
    unsigned int i;
    for (i = 0; i < 10; i++) {
	bool set = bv0.Set(i*i); assert(!set);
    }

    // copy bv0 -> bv1
    BitVector bv1(&bv0);

    // test that they are identical
    for (i = 0; i < 10; i++) {
	int j = i * i;
	if (!bv1.Read(j)) {
	    cout << "  Error: bit " << j << " was not set!\n\n";
	    return false;
	}
	bool set = bv1.Reset(j); assert(set);
    }

    // bv1 should now be empty
    BVIter iter(bv1);
    if (iter.Next(i)) {
	cout << "  Error: bit " << i << " should not be set!\n\n";
	return false;
    }
    cout << "  Passed!\n\n";
    cout.flush();
    return true;
}

static bool PackTest() throw ()
{
    cout << "*** Pack Test ***\n\n";
    BitVector bv, packMask;

    // test "bv" empty case
    cout << "Empty bit-vector case:\n"; cout.flush();
    int wordSize = 8 * sizeof(Word);
    packMask.SetInterval(0, wordSize - 1);
    bv.Pack(packMask);
    if (!bv.IsEmpty()) {
	cout << "  Error: some bits were set on an empty bit vector!\n\n";
	return false;
    }
    cout << "  Passed!!\n\n";

    // test "bv == packMask" case
    cout << "Identical bit-vector case:\n"; cout.flush();
    bv = packMask;
    bv.Pack(packMask);
    if (bv != packMask) {
	cout << "  Error: the bit vector was changed!\n\n";
	return false;
    }
    cout << "  Passed!!\n\n";

    // test random cases
    cout << "Random packing tests:\n"; cout.flush();
    cout.flush();
    for (int cnt = 0; cnt < 500; cnt++) {
	bv.ResetAll(); packMask.ResetAll();
	int numBits = MyRand(50, 250);
	for (int j = 0, k = 0; k < numBits; k++) {
	    int skip = MyRand(1, 10);
	    if (skip == 10) skip = MyRand(50, 100);
	    j += skip;
	    bool set = bv.Set(j); assert(!set);
	    set = packMask.Set(j); assert(!set);
	}
	bv.Pack(packMask);
	BitVector expected;
	expected.SetInterval(0, numBits - 1);
	if (bv != expected) {
	    cout << "  Error: packing result different from expected!\n\n";
	    return false;
	}
    }
    cout << "  Passed!!\n\n";
    cout.flush();
    return true;
}

static bool MSBTest() throw ()
{
    cout << "*** MSB Test ***\n\n";
    BitVector bv; int i;
    
    cout << "Testing upward...\n"; cout.flush();
    for (i = 0; i < 1000; i++) {
	bool set = bv.Set(i); assert(!set);
	if (bv.MSB() != i) {
	    cout << "  Failed on bit " << i << "\n";
	    return false;
	}
    }
    cout << "  Passed!\n\n";
    cout.flush();

    cout << "Testing downward...\n"; cout.flush();
    for (i--; i >= 0; i--) {
	bool set = bv.Reset(i); assert(set);
	if (bv.MSB() != (i-1)) {
	    cout << "  Failed on bit " << i-1 << "\n";
	    return false;
	}
    }
    cout << "  Passed!\n\n";
    cout.flush();
    return true;
}

static bool TestLTBug() throw()
{
    cout << "*** '<' comparator bug Test ***\n\n";

    // commonNames = { 0-135 }
    BitVector commonNames;
    commonNames.SetInterval(0, 135);
    cout << "commonNames = ";
    commonNames.PrintAll(cout);

    // newCommonNames = { 0-33, 136-301 }
    BitVector newCommonNames;
    newCommonNames.SetInterval(0, 33);
    newCommonNames.SetInterval(136, 301);
    cout << "newCommonNames = ";
    newCommonNames.PrintAll(cout);

    // bothCommon = (commonNames & newCommonNames)
    //            = { 0-33 }
    BitVector bothCommon(&(commonNames));
    bothCommon &= newCommonNames;
    cout << "bothCommon = ";
    bothCommon.PrintAll(cout);
    cout << endl;

    bool l_compare = (bothCommon < newCommonNames);

    cout << "(bothCommon < newCommonNames) == "
	 << (l_compare?"true":"false") << endl;

    return l_compare;
}

static bool TestMadeEmptyBugs() throw()
{
    cout << "*** ReduceSz/MSB bug ***\n\n";

    BitVector a, b;

    // a = {19,73}; a &= {};
    a.Set(19);
    a.Set(73);

    a &= b;

    unsigned int cardinality = a.Cardinality();
    bool l_result = (cardinality == 0);

    cout << "a.Cardinality() == " << cardinality << endl;

    if(!l_result) return false;

    int msb = a.MSB();
    l_result = (msb == -1);
    cout << "a.MSB() == " << msb << endl << endl;

    if(!l_result) return false;

    // a = {50-70}; a &= {100-150};

    a.SetInterval(50,70);
    b.SetInterval(100,150);

    a &= b;

    cardinality = a.Cardinality();
    l_result = (cardinality == 0);

    cout << "a.Cardinality() == " << cardinality << endl;

    if(!l_result) return false;

    msb = a.MSB();
    l_result = (msb == -1);
    cout << "a.MSB() == " << msb << endl << endl;

    if(!l_result) return false;

    // ResetInterval with a already empty
    a.ResetInterval(50,70);

    cardinality = a.Cardinality();
    l_result = (cardinality == 0);

    cout << "a.Cardinality() == " << cardinality << endl;

    if(!l_result) return false;

    msb = a.MSB();
    l_result = (msb == -1);
    cout << "a.MSB() == " << msb << endl << endl;

    if(!l_result) return false;

    return l_result;
}

int main(int argc, char *argv[]) 
{
    if (SetResetTest() && IteratorTest() && NextAvailTest()
        && NextAvailExceptTest() && RdWrIntvlTest() && SetIntvlTest()
        && ResetIntvlTest() && CopyTest() && PackTest() && MSBTest()
	&& TestLTBug() && TestMadeEmptyBugs()) {
	cout << "All tests passed!\n";
    } else {
	cout << "Test failed!\n";
    }
    cout.flush();
    return 0;
}
