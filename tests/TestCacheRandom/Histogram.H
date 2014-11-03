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

// Last modified on Thu Aug  5 10:48:12 EDT 2004 by ken@xorian.net
//      modified on Tue Apr 30 11:15:20 PDT 1996 by heydon

#ifndef HISTOGRAM_H
#define HISTOGRAM_H

#include <Basics.H>

// A class for storing a histogram; its methods are unmonitored
class Histogram {
public:
  Histogram(int gran) throw ()
    : numVals(0), gran(gran), bucket(NULL), numBuckets(0), firstBucket(0)
  { /*SKIP*/ }
  /* Initialize a new histogram with a granularity of "gran" units. */

  void AddVal(int t) throw ();
  /* Add a new value to the histogram corresponding to "t" units. */

  void Print(std::ostream &os, int width = 78) const throw ();
  /* Print a representation of the histogram to "os" using a screen
     width of "width". */

private:
  int numVals;      // total number of values added to this histogram
  int gran;         // histogram granularity
  int numBuckets;   // size of "bucket" array
  int *bucket;      // array of counts
  int firstBucket;  // min val of first bucket / gran
};

#endif // HISTOGRAM_H
