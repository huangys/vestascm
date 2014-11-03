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

// basics
#include <Basics.H>

// fp
#include <FP.H>

// local includes
#include "BitVector.H"
#include "FV.H"
#include "Model.H"
#include "CacheIndex.H"
#include "Derived.H"
#include "VestaVal.H"
#include "Debug.H"
#include "NewVal.H"

void NewVal::NewNames(/*OUT*/ FV::List& names, unsigned int num) throw ()
{
    if (num < 1) num = Debug::MyRand(1, 5);
    names.len = num;
    names.name = NEW_ARRAY(FV::T, num);
    for (int i = 0; i < num; i++) {
	names.name[i] = FV::T("name-");
	char buff[20];
	int printLen = sprintf(buff, "%d", i);
	assert(printLen < 20);
	names.name[i] += FV::T(buff);
    }
}

void NewVal::NewFP(/*OUT*/ FP::Tag& fp, int vals) throw ()
/* Initialize "fp" to a new, random fingerprint. */
{
    int pk = Debug::MyRand(0, vals-1);
    fp = FP::Tag((char *)(&pk), sizeof(pk));
}

void NewVal::NewFPs(/*OUT*/ FP::List& fps, unsigned int num, int vals) throw ()
{
    if (num < 1) num = Debug::MyRand(1, 5);
    fps.len = num;
    fps.fp = NEW_PTRFREE_ARRAY(FP::Tag, num);
    for (int i = 0; i < num; i++) {
	NewVal::NewFP(fps.fp[i], vals);
    }
}

void NewVal::NewModel(/*OUT*/ Model::T &model) throw ()
{
    model = (Model::T)Debug::MyRand(0, 99999);
}

void NewVal::NewCI(/*OUT*/ CacheEntry::Index &index) throw ()
{
    index = (CacheEntry::Index)Debug::MyRand(0, 9999);
}

void NewVal::NewCIs(/*OUT*/ CacheEntry::Indices &kids, unsigned int num)
  throw ()
{
    if (num < 1) num = Debug::MyRand(1, 6);
    kids.len = num;
    kids.index = NEW_PTRFREE_ARRAY(CacheEntry::Index, num);
    for (int i = 0; i < num; i++) {
	NewVal::NewCI(kids.index[i]);
    }
}

void NewVal::NewDI(/*OUT*/ Derived::Index &index) throw ()
{
    index = (Derived::Index)Debug::MyRand(0, 9999);
}

void NewVal::NewDIs(/*OUT*/ Derived::Indices &kids, unsigned int num) throw ()
{
    if (num < 1) num = Debug::MyRand(1, 6);
    kids.len = num;
    kids.index = NEW_PTRFREE_ARRAY(Derived::Index, num);
    for (int i = 0; i < num; i++) {
	NewVal::NewDI(kids.index[i]);
    }
}

void NewVal::NewValue(/*OUT*/ VestaVal::T &value) throw ()
{
    NewVal::NewFP(value.fp);
    NewVal::NewDIs(value.dis);
    value.len = (int)Debug::MyRand(1, 20);
    value.bytes = NEW_PTRFREE_ARRAY(char, value.len);
    for (int i = 0; i < value.len; i++) {
	value.bytes[i] = (char)Debug::MyRand(0, 255);
    }
}

void NewVal::NewBV(/*OUT*/ BitVector &bv) throw ()
{
    int num = Debug::MyRand(0, 5), curr = -1;
    while (num-- > 0) {
	int skip = Debug::MyRand(1, 10);
	curr += skip;
	assert(curr >= 0);
	bv.Set(curr);
    }
}

void NewVal::NewBool(/*OUT*/ bool &b) throw ()
{
    b = (Debug::MyRand(0, 1) == 0) ? false : true;
}
