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

// Last modified on Fri Apr 22 22:32:29 EDT 2005 by ken@xorian.net  
//      modified on Sat Feb 12 17:33:34 PST 2000 by mann  
//      modified on Mon Mar 30 13:12:58 PST 1998 by heydon

#include <Basics.H>
#include "Histogram.H"

using std::ostream;
using std::endl;

void Histogram::AddVal(int t) throw ()
{
    int b = t / this->gran;         // compute bucket index
    int bx = b - this->firstBucket; // adjust for current array
    if (this->numBuckets == 0) {
	this->numBuckets = 1;
	this->bucket = NEW_PTRFREE_ARRAY(int, 1);
	this->firstBucket = b;
	bx = 0;
    } else if (bx < 0) {
	int growBy = -bx;
	int *newBucket = NEW_PTRFREE_ARRAY(int, this->numBuckets + growBy);
	int i;
	for (i = 0; i < growBy; i++) newBucket[i] = 0;
	for (i = 0; i < this->numBuckets; i++)
	    newBucket[i + growBy] = this->bucket[i];
	this->numBuckets += growBy;
	this->bucket = newBucket;
	this->firstBucket = b;
	bx = 0;
    } else if (numBuckets <= bx) {
	int growBy = 1 + bx - this->numBuckets;
	int *newBucket = NEW_PTRFREE_ARRAY(int, this->numBuckets + growBy);
	int i;
	for (i = 0; i < this->numBuckets; i++)
	    newBucket[i] = this->bucket[i];
	for (/*SKIP*/; i < this->numBuckets + growBy; i++) newBucket[i] = 0;
	this->numBuckets += growBy;
	this->bucket = newBucket;
    }
    this->bucket[bx]++;
    this->numVals++;
}

void Histogram::Print(ostream &os, int width) const throw ()
{
    // compute maximum bucket value
    int maxVal = 0;
    int i;
    for (i = 0; i < this->numBuckets; i++) {
	maxVal = max(maxVal, this->bucket[i]);
    }

    // compute value for each '*'
    float starVal = ((float)maxVal) / ((float)(width - 12));

    // print histogram bars
    int bVal = this->firstBucket * this->gran;
    for (i = 0; i < this->numBuckets; i++) {
	char buff[8];
	int printLen = sprintf(buff, "%7d", bVal);
	assert(printLen == 7);
	bVal += this->gran;
	float sz = (float)(this->bucket[i]);
	for (float j = 0.0; j < sz; j += starVal) os << '*';
	if (sz > 0.0) os << ' ';
	os << this->bucket[i] << endl;
    }

    // print total
    os << "TOTAL = " << this->numVals << endl;
}
