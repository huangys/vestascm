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

// Created on Fri May  2 15:58:40 PDT 1997 by heydon

#ifndef _SHARED_TABLE_H
#define _SHARED_TABLE_H

#include <Basics.H>

/* This interface defines a template for a generic variant of the "Table"
   class that allows key-value pairs to be shared between tables so as to
   minimize heap allocations.

   The class "SharedTable<K,V>" maps keys of type "K" to values of type "V".
   Unlike the "Table<K,V>" class, however, the methods for looking up,
   inserting, deleting, and iterating over table elements treat key-value
   pairs as a single value. Hence, a key-value pair from one table can be
   copied to another table without an extra allocation.

   The requirements on the "K" and "V" parameters of this interface are the
   same as those for the "Table" interface. Since key-value pairs can be
   shared between tables, you should not modify either field of a key-value
   pair once it has been installed in a table.

   This implementation assumes the use of a garbage collector; it does not
   free any memory explicitly. */

template <class K, class V>
class SharedTable {
  public:
    // the class of key-value pairs
    class KVPair {
      public:
	KVPair(const K& k, const V& v) throw ()
	    : key(k), val(v) { /*SKIP*/ }
	K key;
	V val;
    };

    // a pointer to a KVPair
    typedef KVPair *KVPairPtr;

    // Iterator class defined below
    class Iterator;

    // the table class itself
    class T {
      public:
	T(Basics::uint32 sizeHint = 0) throw ()
          : buckets(NULL) { Init(sizeHint); }
        /* Return a new, empty table. See the "Init" method below for a
	    description of the "sizeHint" argument. */

	T(const T *tbl) throw ()
          : buckets(NULL) { Init(tbl->numEntries); Copy(tbl); }
        /* Initialize this table to be a copy of "*tbl". */

	void Init(Basics::uint32 sizeHint = 0U) throw ();
        /* Reset the table to be empty. If "sizeHint" is greater than zero,
           the table is initialized with the expectation that "sizeHint"
           different pairs will eventually be "Put" into the table. These
	   calls may execute somewhat faster than if "sizeHint" was 0. */

        T& operator= (const T& tbl) throw ()
          { Init(tbl.numEntries); Copy(&tbl); return *this; }
        /* Make this table a copy of "tbl". */

	int Size() const throw () { return numEntries; }
        /* Return the number of <k,v> pairs in the table. */

	bool Get(const K& k, /*OUT*/ KVPairPtr& pr) const throw ();
        /* If the key "k" exists in the table, then set "pr" to a pointer
           to the associated key-value pair and return "true". Otherwise,
           leave "pr" unchanged and return "false". */

	bool Put(KVPairPtr pr, bool resize = true) throw ();
        /* Install the key-value pair pointed to by "pr" in the table.
           Return "true" iff there was already a value associated with
           "pr->key" in the table before the method was invoked. */

	bool Put(const K& k, const V& v, /*OUT*/ KVPairPtr& pr,
          bool resize = true) throw ();
        /* If there is no entry in the table with key "k", install a new
	    pair "(k,v)" in the table, set "pr" to that new pair, and return
	    "false". Otherwise, a pair already exists in the table with key
	    "k". In that case, "v" is ignored, "pr" is set to the existing
	    pair, and the method returns "true". */

        void Copy(const T* tbl, bool resize = true) throw ();
        /* Add the key-value pairs in "tbl" to this table. That is, execute
	   "this->Put(pr, resize)" for each key-value pair "pr" in "tbl". */

	bool Delete(const K& k, /*OUT*/ KVPairPtr& pr, bool resize = true)
          throw ();
        /* If the key "k" exists in the table, set "pr" to the pointer to the
	   associated key-value pair, remove the association from the table,
	   and return "true". Otherwise, leave "pr" unchanged and return
	   "false". */

        void Resize() throw ();
        /* Grow or shrink the table as necessary after performing a sequence
           of "Put" or "Delete" operations all of which were passed "resize =
           false". */

      private:
	Basics::uint32 numEntries;      // current number of entries in table
	Basics::uint32 minEntries;      // minimum number of entries
	Basics::uint32 maxEntries;      // maximum number of entries
	Basics::uint16 logBuckets;      // Log_2(numBuckets)
	Basics::uint16 minLogBuckets;   // minimum value for Log_2(initial size)
	KVPairPtr *buckets;    // buckets array

        /* Invariants of an initialized table:
             I1. buckets != NULL => NUMBER(*buckets) = 2^logBuckets
             I2. minEntries < maxEntries
           Note: it is possible for "numEntries < minEntries" or for
           "maxEntries < numEntries". This can happen when "resize = false"
           is used, or when the minimum or maximum bucket size is reached. */

	// see "SharedTable.C" for details about this structure
	class MultiBucket {
	  public:
	    Basics::uint32 num;  // number of elements in bucket
	    Basics::uint32 len;  // total size of the bucket
	};
	typedef MultiBucket *MultiBucketPtr;

        inline static unsigned int MultiBucketSlots() throw()
        {
	  static unsigned int slots = 0;
	  // If we haven't computed the number of slots yet...
	  if(slots == 0)
	    {
	      slots = sizeof(MultiBucket) / sizeof(KVPairPtr);
	      // In case it's not an even multiple, make sure we
	      // reserve enough slots.
	      while(sizeof(MultiBucket) > (slots * sizeof(KVPairPtr)))
		slots++;
	    }
	  return slots;
	}
        /* Return the number of slots in an array of KVPairPtr's that
	   a MultiBucket struct will consume.  (The value depends on
	   the size of pointers on the target architecture). */

	void NewBuckets(bool allocBuckets) throw ();
	/* Initialize the "minEntries" and "maxEntries" fields from the
           "this->logBuckets" field. If "allocBuckets" is true, allocate
           and initialize the "buckets" field to point to an array of
           "2^(logBuckets)" buckets. */

        void AllocBuckets(Basics::uint32 numBuckets) throw ();
        /* Allocate and initialize the "buckets" field to point to an array
           array of "2^(logBuckets)" buckets. */

	void EmptyBuckets(Basics::uint32 numBuckets) throw ();
        /* Forall "0 <= i < numBuckets", set "buckets[i]" to "NULL". */

	void Rehash(Basics::uint16 logBuckets) throw ();
        /* Reallocate "2^logBuckets" buckets, and rehash the entries into
           the new table. */

	inline Basics::uint32 BucketNum(const K &k) const throw ();
	// Return the bucket index for key "k".

	KVPair *InitBucket(KVPair *elt0, KVPair *elt1) throw ();
        /* Return a new multi-bucket containing the two elements "elt0" and
	   "elt1". */

	KVPair *GrowBucket(/*INOUT*/ MultiBucketPtr &mb,
          /*INOUT*/ KVPairPtr* &elt) throw ();
	/* Grow the multi-bucket pointed to by "mb" and "elt" so it can hold
	   more elements. This requires that the bucket is full, i.e., that
	   "mb->num == mb->len". Return a pointer to the new multi-bucket,
           and set "mb" and "elt" to point to the new parts of it. */

	bool PutPrivate(KVPairPtr pr) throw ();
        /* Like "Put", but in the event that an entry did not already exist in
           the current table with key "pr->key", this neither increments the
           number of entries in the table nor attempts to resize the table. */

	// friends
	friend class Iterator;

        // hide copy constructor
        T(const T&);
    };

    // table iterators
    class Iterator {
      public:
	Iterator(const T* tbl) throw () : tbl(tbl) { Reset(); }
        /* Return a new iterator on the "T" table "tbl". While the iterator
           is in use, the client is only allowed to modify the table by
	   invoking the "Put" and "Delete" methods with "resize = false".
           Once the iteration is complete, it is recommended that the "Resize"
	   method be called to adjust the size of the table if necessary. */

        void Reset() throw ();
        /* Resets the iterator so that it will iterate over all the elements
           in its associated table. This can be used to iterate over the
           elements of the same table multiple times. */

	bool Next(/*OUT*/ KVPairPtr& pr) throw ();
        /* If there is a key-value pair in this iterator's table that has
           not already been enumerated, select one, set "pr" to point to it,
           and return "true". Otherwise, leave "pr" unchanged, and return
           "false". */

      private:
	// fields
	const T *tbl;             // this iterator's corresponding table
	Basics::uint32 bucket;	  // next bucket if < "tbl->numBuckets"
	Basics::uint32 next;      // index of next entry in current bucket
	bool done;		  // true if next() has returned "FALSE"

        // hide copy constructor
        Iterator(const Iterator&);
    };
};

// ========== template member definitions ==========

#include "TableUtils.H"
#include "TableConsts.H"

/* Implementation

   Within a table, the pointers in the "buckets" array are overloaded.
   There are three cases:

   1) If the pointer is NULL, the bucket is empty.

   2) If the least significant bit of the pointer is unset, the bucket
      contains a single KVPair pointed to by the pointer.

   3) If the least significant bit of the pointer is set, the bucket
      contains more than one entry. The pointer (with its LSB reset)
      points to an array of KVPairPtr's. The first element of the array
      is actually not a pointer, but a "MultiBucket" structure containing
      a pair of 32-bit integers representing the number of significant
      pointers in the succeeding elements of the array and the total number
      of slots in the array (not counting the first entry).
*/

// ----------------------------------------------- Default table methods -----

template <class K, class V>
void SharedTable<K,V>::T::Copy(const T *tbl, bool resize) throw ()
/* Iterate over the elements of "tbl", and put each (key,value) pair
   in this table. This could probably be done more efficiently without
   using an iterator. */
{
    Iterator iter(tbl);
    KVPairPtr pr;
    while (iter.Next(/*OUT*/ pr)) {
	// add the pair to this table
	(void) this->Put(pr, resize);
    }
    if (!resize) Resize();
}

template <class K, class V>
void SharedTable<K,V>::T::Init(Basics::uint32 sizeHint) throw ()
{
    Basics::uint32 idealBuckets;
    float idealF = (float)sizeHint / TableConsts::IdealDensity;
    idealBuckets = (idealF > (float)TableConsts::MaxBuckets) ? TableConsts::MaxBuckets : Ceiling(idealF);
    idealBuckets = max(TableConsts::MinBuckets, idealBuckets);
    this->numEntries = 0U;
    if (this->buckets != NULL && idealBuckets < (TableConsts::One32U << this->logBuckets)) {
	// enough buckets already exist; just empty them
	this->EmptyBuckets(TableConsts::One32U << this->logBuckets);
    } else {
	// allocate new buckets
	this->logBuckets = this->minLogBuckets = Log_2(idealBuckets);
	this->NewBuckets(sizeHint > 0);
    }
}

template <class K, class V>
void SharedTable<K,V>::T::NewBuckets(bool allocBuckets) throw ()
{
    // calculate "minEntries" and "maxEntries"
    Basics::uint32 numBuckets = (TableConsts::One32U << this->logBuckets);
    this->minEntries = Round(TableConsts::MinDensity * (float)numBuckets);
    this->maxEntries = Round(TableConsts::MaxDensity * (float)numBuckets);

    // allocate buckets array if necessary
    if (allocBuckets) {
	this->AllocBuckets(numBuckets);
    } else {
	this->buckets = NULL;
    }
}

template <class K, class V>
void SharedTable<K,V>::T::AllocBuckets(Basics::uint32 numBuckets) throw ()
{
    this->buckets =  NEW_ARRAY(KVPairPtr, numBuckets);
    assert(this->buckets != NULL);
    this->EmptyBuckets(numBuckets);
}

template <class K, class V>
void SharedTable<K,V>::T::EmptyBuckets(Basics::uint32 numBuckets) throw ()
{
    for (Basics::uint32 i = 0U; i < numBuckets; i++) {
	this->buckets[i] = (KVPair *)NULL;
    }
}

template <class K, class V>
inline Basics::uint32 SharedTable<K,V>::T::BucketNum(const K& k) const throw ()
{
    return ((TableConsts::Multiplier * k.Hash()) >> (TableConsts::BitsPerWd - this->logBuckets));
}

template <class K, class V>
typename SharedTable<K,V>::KVPair*
SharedTable<K,V>::T::InitBucket(KVPair *elt0, KVPair *elt1) throw ()
{
    unsigned int mb_slots = MultiBucketSlots();
    KVPairPtr *elt = 
      NEW_ARRAY(KVPairPtr, mb_slots + TableConsts::InitBucketSize);
    MultiBucket *mb = (MultiBucket *)(elt);
    elt += mb_slots;

    // set multi bucket fields
    mb->num = 2U;
    mb->len = TableConsts::InitBucketSize;
    elt[0] = elt0, elt[1] = elt1;

    // NULL out all but first two bucket entries (for GC safety)
    for (int i = 2; i < mb->len; i++) {
	elt[i] = (KVPair *)NULL;
    }
    return (KVPair *)((PointerInt)mb | TableConsts::LSBMask);
}

template <class K, class V>
typename SharedTable<K,V>::KVPair*
SharedTable<K,V>::T::GrowBucket(/*INOUT*/ MultiBucketPtr &mb, /*INOUT*/ KVPairPtr* &elt) throw ()
{
    // grow bucket
    assert(mb->num == mb->len);
    mb->len = Ceiling((float)(mb->len) * TableConsts::BucketGrowFactor);
    unsigned int mb_slots = MultiBucketSlots();
    KVPairPtr *eltNew = NEW_ARRAY(KVPairPtr, mb_slots + mb->len);
    MultiBucket *mbNew = (MultiBucket *)(eltNew);
    eltNew += mb_slots;
    *mbNew = *mb;
    assert(mbNew->len > mbNew->num);

    // copy existing entries
    int k;
    for (k = 0; k < mb->num; k++) {
	eltNew[k] = elt[k];
	elt[k] = (KVPair *)NULL; // drop on floor
    }

    // NULL out remaining entries (for GC safety)
    for (/*SKIP*/; k < mbNew->len; k++) {
	eltNew[k] = (KVPair *)NULL;
    }

    // update INOUT args
    mb = mbNew; elt = eltNew;
    return (KVPair *)((PointerInt)mbNew | TableConsts::LSBMask);
}

template <class K, class V>
void SharedTable<K,V>::T::Rehash(Basics::uint16 logBuckets) throw ()
{
    assert(logBuckets <= TableConsts::MaxLogBuckets);
    assert(logBuckets >= this->minLogBuckets);

    KVPairPtr *ob = this->buckets;	             // old buckets
    Basics::uint16 oldLogBuckets = this->logBuckets;          // log of their old size
    Basics::uint32 oldNumBuckets = (TableConsts::One32U << oldLogBuckets); // their old size

    // allocate new buckets
    this->logBuckets = logBuckets;
    this->NewBuckets(ob != (KVPairPtr *)NULL);

    if (ob != (KVPairPtr *)NULL) {
	// rehash entries from old table into new table
	for (Basics::uint32 i = 0U; i < oldNumBuckets; i++) {
	    KVPair *b = ob[i];
	    if (b != (KVPair *)NULL) {
		// bucket is non-empty
		if (!((PointerInt)b & TableConsts::LSBMask)) {
		    // single-entry bucket
		    this->PutPrivate(b);
		    ob[i] = (KVPair *)NULL; // drop on floor
		} else {
		    // multi-entry bucket
		    KVPairPtr *elt = (KVPairPtr *)((PointerInt)b & TableConsts::LSBMaskBar);
		    unsigned int mb_slots = MultiBucketSlots();
		    MultiBucket *mb = (MultiBucket *)(elt);
		    elt += mb_slots;
		    register Basics::uint32 j;
		    for (j = 0U; j < mb->num; j++) {
			if (elt[j] != (KVPair *)NULL) {
			    this->PutPrivate(elt[j]);
			    elt[j] = (KVPair *)NULL; // drop on floor
			}
		    }
		}
	    }
	}
    }
}

template <class K, class V>
void SharedTable<K,V>::T::Resize() throw ()
{
    if (this->numEntries < this->minEntries
        && this->logBuckets > this->minLogBuckets) {
	// too sparse
	Rehash(this->logBuckets - 1);
    } else if (this->numEntries > this->maxEntries
               && this->logBuckets < TableConsts::MaxLogBuckets) {
	// too crowded
	Rehash(this->logBuckets + 1);
    }
}

template <class K, class V>
bool SharedTable<K,V>::T::Get(const K& k, /*OUT*/ KVPairPtr& pr) const throw ()
{
    if (this->buckets == NULL) return false;
    KVPair *b = this->buckets[this->BucketNum(k)];
    if (b == (KVPair *)NULL) return false;
    if (!((PointerInt)b & TableConsts::LSBMask)) {
	// singleton case
	if (b->key == k) {
	    pr = b;
	    return true;
	}
    } else {
	// multibucket case
	KVPairPtr *elt = (KVPairPtr *)((PointerInt)b & TableConsts::LSBMaskBar);
	unsigned int mb_slots = MultiBucketSlots();
	MultiBucket *mb = (MultiBucket *)(elt);
	elt += mb_slots;
	register Basics::uint32 i;
	for (i = 0; i < mb->num; i++) {
	    if (elt[i] != (KVPair *)NULL && elt[i]->key == k) break;
	}
	if (i < mb->num) {
	    pr = elt[i];
	    return true;
	}
    }
    return false;
}

template <class K, class V>
bool SharedTable<K,V>::T::PutPrivate(KVPairPtr pr) throw ()
{
    Basics::uint32 bucketNum = this->BucketNum(pr->key);
    if (this->buckets == NULL) AllocBuckets(TableConsts::One32U << this->logBuckets);
    KVPair *b = this->buckets[bucketNum];

    if (b == (KVPair *)NULL) {
	// bucket currently empty
	this->buckets[bucketNum] = pr;
    } else if (!((PointerInt)b & TableConsts::LSBMask)) {
	// single-entry bucket
	if (b->key == pr->key) {
	    // replace existing entry
	    this->buckets[bucketNum] = pr;
	    // assert(b->val == pr->val);
	    return true;
	} else {
	    // create a new multibucket
	    this->buckets[bucketNum] = (KVPair *)InitBucket(b, pr);
	}
    } else {
	// multi-entry bucket
	KVPairPtr *elt = (KVPairPtr *)((PointerInt)b & TableConsts::LSBMaskBar);
	unsigned int mb_slots = MultiBucketSlots();
	MultiBucket *mb = (MultiBucket *)(elt);
	elt += mb_slots;
	int emptySlot = -1;
	register Basics::uint32 i;
	for (i = 0; i < mb->num; i++) {
	    if (elt[i] == (KVPair *)NULL) {
	      if (emptySlot < 0) emptySlot = i;
	    } else if (elt[i]->key == pr->key) {
		break;
	    }
	}
	if (i < mb->num) {
	    // match found -- replace existing value
	    elt[i] = pr;
	    // assert(elt[i]->val == pr->val);
	    return true;
	} else {
	    // insert new entry in table
	    if (emptySlot >= 0) {
		// use empty slot without growing bucket
		elt[emptySlot] = pr;
	    } else {
		// add to end of bucket
		if (mb->num >= mb->len) {
		    this->buckets[bucketNum] = 
                      (KVPair *)(this->GrowBucket(/*INOUT*/mb, /*INOUT*/elt));
		}
		elt[mb->num++] = pr;
	    }
	}
    }
    return false;
}

template <class K, class V>
bool SharedTable<K,V>::T::Put(KVPairPtr pr, bool resize) throw ()
{
    bool res = this->PutPrivate(pr);
    if (!res) {
	/* If PutPrivate returned "false", the number of entries in the
           table has increased. See if the table needs to be resized. */
	this->numEntries++;
	if (resize && this->numEntries > this->maxEntries
	    && this->logBuckets < TableConsts::MaxLogBuckets) {
	    // too crowded
	    Rehash(this->logBuckets + 1);
	}
    }
    return res;
}

template <class K, class V>
bool SharedTable<K,V>::T::Put(const K& k, const V& v,
  /*OUT*/ KVPairPtr& pr, bool resize) throw ()
{
    Basics::uint32 bucketNum = this->BucketNum(k);
    if (this->buckets == NULL) AllocBuckets(TableConsts::One32U << this->logBuckets);
    KVPair *b = this->buckets[bucketNum];

    if (b == (KVPair *)NULL) {
	// bucket currently empty
        pr = NEW_CONSTR(KVPair, (k,v));
	this->buckets[bucketNum] = pr;
    } else if (!((PointerInt)b & TableConsts::LSBMask)) {
	// single-entry bucket
	if (b->key == k) {
	    // match found -- remember pair
	    pr = this->buckets[bucketNum];
	    return true;
	} else {
	    // create a new multibucket
	    pr = NEW_CONSTR(KVPair, (k,v));
	    this->buckets[bucketNum] = (KVPair *)InitBucket(b, pr);
	}
    } else {
	// multi-entry bucket
	KVPairPtr *elt = (KVPairPtr *)((PointerInt)b & TableConsts::LSBMaskBar);
	unsigned int mb_slots = MultiBucketSlots();
	MultiBucket *mb = (MultiBucket *)(elt);
	elt += mb_slots;
	int emptySlot = -1;
	register Basics::uint32 i;
	for (i = 0; i < mb->num; i++) {
	    if (elt[i] == (KVPair *)NULL) {
	      if (emptySlot < 0) emptySlot = i;
	    } else if (elt[i]->key == k) {
		break;
	    }
	}
	if (i < mb->num) {
	    // match found -- remember pair
	    pr = elt[i];
	    return true;
	} else {
	    // insert new entry in table
	    pr = NEW_CONSTR(KVPair, (k,v));
	    if (emptySlot >= 0) {
		// use empty slot without growing bucket
		elt[emptySlot] = pr;
	    } else {
		// add to end of bucket
		if (mb->num >= mb->len) {
		    this->buckets[bucketNum] = 
                      (KVPair *)(this->GrowBucket(/*INOUT*/mb, /*INOUT*/elt));
		}
		elt[mb->num++] = pr;
	    }
	}
    }
    /* If we have not yet returned "true", the number of entries in the
       table has increased. See if the table needs to be resized. */
    this->numEntries++;
    if (resize && this->numEntries > this->maxEntries
	&& this->logBuckets < TableConsts::MaxLogBuckets) {
	// too crowded
	Rehash(this->logBuckets + 1);
    }
    return false;
}

template <class K, class V>
bool SharedTable<K,V>::T::Delete(const K& k, /*OUT*/ KVPairPtr& pr,
  bool resize) throw ()
{
    if (this->buckets == NULL) return false;
    Basics::uint32 bucketNum = this->BucketNum(k);
    KVPair *b =  this->buckets[bucketNum];

    if (b == (KVPair *)NULL) {
	// empty bucket
	return false;
    } else if (!((PointerInt)b & TableConsts::LSBMask)) {
	// single-entry bucket
	if (b->key == k) {
	    // match; make bucket empty
	    pr = b;
	    this->buckets[bucketNum] = (KVPair *)NULL;
	} else {
	    return false;
	}
    } else {
	// multi-entry bucket
	KVPairPtr *elt = (KVPairPtr *)((PointerInt)b & TableConsts::LSBMaskBar);
	unsigned int mb_slots = MultiBucketSlots();
	MultiBucket *mb = (MultiBucket *)(elt);
	elt += mb_slots;
	register Basics::uint32 i;
	for (i = 0; i < mb->num; i++) {
	    if (elt[i] != (KVPair *)NULL && elt[i]->key == k) break;
	}
	if (i < mb->num) {
	    pr = elt[i];
	    elt[i] = (KVPair *)NULL;
	} else {
	    return false;
	}
    }

    /* If we have not returned by now, the number of entries in the table
       has decreased. See if the table needs to be resized; return true. */
    this->numEntries--;
    if (resize && this->numEntries < this->minEntries
	&& this->logBuckets > this->minLogBuckets) {
	// too sparse
	Rehash(this->logBuckets - 1);
    }
    return true;
}

// ---------------------------------------------------- Iterator methods -----

template <class K, class V>
void SharedTable<K,V>::Iterator::Reset() throw ()
{
    this->bucket = 0U;
    this->next = 0U;
    this->done = false;
}

template <class K, class V>
bool SharedTable<K,V>::Iterator::Next(/*OUT*/ KVPairPtr& pr) throw ()
{
    const T *t = this->tbl;
    if (t->buckets != NULL) {
	while (this->bucket < (TableConsts::One32U << t->logBuckets)) {
	    KVPair *b = t->buckets[this->bucket];
	    if (b != (KVPair *)NULL) {
		if (!((PointerInt)b & TableConsts::LSBMask)) {
		    // single-entry bucket
		    pr = b;
		    this->bucket++;
		    return true;
		} else {
		    // multi-entry bucket
		    KVPairPtr *elt = (KVPairPtr *)((PointerInt)b & TableConsts::LSBMaskBar);
		    unsigned int mb_slots = T::MultiBucketSlots();
		    typename T::MultiBucket *mb =
		      (typename T::MultiBucket *)(elt);
		    elt += mb_slots;

		    // skip NULL entries in the bucket
		    while (this->next < mb->num &&
			   elt[this->next] == (KVPair *)NULL) {
			this->next++;
		    }
		    if (this->next < mb->num) {
			// non-NULL entry found; return "true"
			pr = elt[this->next++];
			return true;
		    }

		    // end of bucket reached; reset and try again
		    this->next = 0;
		}
	    }
	    this->bucket++;
	}
    }
    assert(!(this->done));
    this->done = true;
    return false;
}

#endif // _SHARED_TABLE_H
