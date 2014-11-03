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

// Last modified on Mon Jul 25 17:00:54 EDT 2005 by ken@xorian.net

// Constants used by both Table.C and SharedTable.C.

#ifndef _TABLECONSTS_H
#define _TABLECONSTS_H

#include <Basics.H>

namespace TableConsts
{
  // Since initialization order is not defined, initialization of one
  // global constant from another won't always work.  So, we use a
  // bunch of macros to repeat things.

  // ---------- Table ----------

  static const int BitsPerWd = sizeof(Word) * 8;
#define ONE_32_U 1U
  static const Bit32 One32U = ONE_32_U;

  // the Multiplier is (2^BitsPerWd) / Phi, where Phi = (1 + Sqrt(5)) / 2
  static const Word Multiplier = CONST_INT_64(0x9e3779b97f4a7c15);

  // bounds on number of buckets
#define MAX_LOG_BUCKETS ((sizeof(Bit32) * 8) - 4)
  static const Bit16 MaxLogBuckets = MAX_LOG_BUCKETS;
  static const Bit32 MaxBuckets = (ONE_32_U << MAX_LOG_BUCKETS);
#define MIN_LOG_BUCKETS 4
  static const Bit16 MinLogBuckets = MIN_LOG_BUCKETS;
  static const Bit32 MinBuckets = (ONE_32_U << MIN_LOG_BUCKETS);

  // density = (#entries)/(#buckets)
#define IDEAL_DENSITY 0.5
  static const float IdealDensity = IDEAL_DENSITY;
  static const float MaxDensity = IDEAL_DENSITY * 1.7;
  static const float MinDensity = IDEAL_DENSITY * 0.3;

  // Mask for storing "doDeletions" in "maxEntries" field
  static const Bit32 DoDeletions = ONE_32_U;
  static const Bit32 DoDeletionsBar = ~ONE_32_U;

  // ---------- SharedTable ----------

  // initial size of a MultiBucket (must be >= 2)
  static const Bit32 InitBucketSize = 2;

  // factor by which to grow a bucket (must be > 1.0)
  static const float BucketGrowFactor = 3.0;

  // Constants used for overloading the meaning of the "buckets"
  // pointers (single KVPair or MultiBucket)
#define LSB_MASK 0x1UL
  static const PointerInt LSBMask = LSB_MASK;
  static const PointerInt LSBMaskBar = ~LSB_MASK;

}

#endif // _TABLECONSTS_H
