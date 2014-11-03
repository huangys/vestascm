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

// Last modified on Thu Apr 21 09:31:02 EDT 2005 by irina.furman@intel.com
//      modified on Wed Jun  2 18:16:55 EDT 2004 by ken@xorian.net
//      modified on Mon May  3 16:04:43 PDT 1999 by heydon

#ifndef _TABLE_H
#define _TABLE_H

#include <Basics.H>

/* This interface defines a template for a generic "Table" class. The
   "Table<K,V>" class is a map from keys of type "K" to values of type "V".

   The interface and implementation are based on Modula-3 tables. In
   particular, it defines the classes "Table<K,V>::T", "Table<K,V>::Default",
   and "Table<K,V>::Iterator".

   The class "T" is a class prototype with pure methods, and should not be
   instantiated. In fact, since the class "T" does not provide any state or
   methods, it is not even subclassed.

   The class "Default" provides a default implementation of tables using
   chained hashing.

   The class "Iterator" is for iterating over the elements of a
   "Table<K,V>::Default".

   For efficiency, the default tables and their iterators are unmonitored, so
   a client accessing a table from multiple threads must ensure that if two
   operations are active concurrently, then neither of them has side effects
   on the same table or iterator. The read-only methods are declared "const".

   "K" is required to be a class with the following methods:

     Class "K":
       Word Hash() throw ()
       K& operator= (const K& k) throw ()
       friend bool operator == (const K& k1, const K& k2) throw ()

   "V" may be any type so long as its values can be assigned. If it is a
   class, it should have an assignment method:
  
     Class "V":
       V& operator= (const V& v) throw ()

   To instantiate a table, you use cxx's "define_template" pragma. For this
   to work, you must first include the source file "Table.C", which provides
   implementations for the generic table methods. However, the "Table.C" code
   must be linked into your executable exactly once. Therefore, the
   recommended procedure is to write an implementation (.C) file of the form:

     #include "Table.C"
     <template-instantiation>
     <template-instantiation>
     ...

   Each <template-instantiation> is a one-line pragma, of the form:

     #pragma define_template Table<K,V>

   where "K" and "V" are the key and value types, respectively, of the
   instantiated table.

   To link the Table code successfully, you need to link your program against
   the standard math library, "libm.a".
*/

template <class K, class V>
class Table {
  public:
    // Iterator class defined below
    class Iterator;

    // main table type (prototype)
    // this definition is purely for documentation purposes
    class T {
      public:
	bool Get(const K& k, /*OUT*/ V& v) const throw ();
        /* If the key "k" exists in the table, then set "v" to the
           associated value and return "true". Otherwise, leave "v"
           unchanged and return "false". */

	bool Put(const K& k, const V& v, bool resize = true) throw ();
        /* Set the value associated with key "k" in the table to "v". Return
           "true" iff there was already a value associated with "k" in the
           table before the method was invoked. */

	bool Delete(const K& k, /*OUT*/ V& v, bool resize = true) throw ();
        /* If the key "k" exists in the table, set "v" to its associated
           value, remove the association from the table, and return "true".
           Otherwise, leave "v" unchanged and return "false". */

	int Size() const throw ();
        /* Return the number of <k,v> associations in the table. */
    };

    // The default implementation of tables uses chained hashing
    class Default /* : public T */ {
      public:
        // new methods
	Default(int sizeHint = 0, bool useGC = false) throw ()
          : maxEntries(!useGC), buckets(NULL) { Init(sizeHint); }
        /* Return a new, empty table. See the "Init" method below for a
           description of the "sizeHint" argument. If "useGC" is "false",
           then the client is assumed not to be using a garbage collector;
           in this case, structures allocated by the table are freed so
           as not to produce any memory leaks. Note that if "V" is a pointer
           type, the values pointed to in the table are *not* freed; freeing
           them is the responsibility of the client. If the client is using a
           garbage collector, passing "useGC = false" will not cause an error,
           but passing "useGC = true" will be slightly more efficient. */

        Default(const Default *tbl, bool useGC = false) throw ()
          : maxEntries(!useGC), buckets(NULL)
          { Init(tbl->numEntries); Copy(tbl); }
        /* Initialize this table to be a copy of "*tbl". The meaning of the
           "useGC" argument is the same as for the above constructor. */

        ~Default() throw ();
        /* Free any memory allocated by this table. */

	void Init(Bit32 sizeHint = 0U) throw ();
        /* Reset the table to be empty. If "sizeHint" is greater than zero,
           the table is initialized with the expectation that "sizeHint"
           different keys will eventually be "Put" into the table. These calls
           may execute somewhat faster than if "sizeHint" was 0. */

        Default& operator = (const Default& tbl) throw ()
            { Init(tbl.numEntries); Copy(&tbl); return *this; }
        /* Make this table a copy of "tbl". */

        // method overrides (see specifications above)
	int Size() const throw () { return numEntries; }
	bool Get(const K& k, /*OUT*/ V& v) const throw ();
	bool Put(const K& k, const V& v, bool resize = true) throw ();
	bool Delete(const K& k, /*OUT*/ V& v, bool resize = true) throw ();
        /* If the "Put" and "Delete" methods are passed "resize = false",
           the underlying bucket array will not be resized. This allows
           modifications to be made to the table while it is being enumerated
           by an iterator (see the "Iterator" class below). Once the iteration
           is complete, you should use the "Resize" method below. */

        // explicit resize
        void Resize() throw ();
        /* Grow or shrink the table as necessary after performing a sequence
           of "Put" or "Delete" operations all of which were passed "resize =
           false". */
        
      private:
	// bucket entries
	class EntryList {
	  public:
	    EntryList(const K& k, const V& v, EntryList *next)
              : key(k), val(v), tail(next) { /*SKIP*/ }
	    EntryList *tail;
	    K key;
	    V val;
	};
	typedef EntryList *EntryListPtr;

	Bit32 numEntries;      // current number of entries in table
	Bit32 minEntries;      // minimum number of entries
	Bit32 maxEntries;      // maximum number of entries
	Bit16 logBuckets;      // Log_2(numBuckets)
	Bit16 minLogBuckets;   // minimum value for Log_2(initial size)
	EntryListPtr *buckets; // buckets array

        /* Invariants of an initialized table:
             I1. buckets != NULL => NUMBER(*buckets) = 2^logBuckets
             I2. minEntries < maxEntries
             I3. maxEntries & 0x1 = ``do deletions explicitly''
           Note: it is possible for "numEntries < minEntries" or for
           "maxEntries < numEntries". This can happen when "resize = false"
           is used, or when the minimum or maximum bucket size is reached.
           By I3, the low-order bit of "maxEntries" is used to store whether
           explicit deletions are required or not. */

	void NewBuckets(bool allocBuckets) throw ();
	/* Initialize the "minEntries" and "maxEntries" fields from the
           "this->logBuckets" field. If "allocBuckets" is true, allocate
           and initialize the "buckets" field to point to an array of
           "2^(logBuckets)" buckets. */

        void AllocBuckets(Bit32 numBuckets) throw ();
        /* Allocate and initialize the "buckets" field to point to an array
           array of "2^(logBuckets)" buckets. */

	void EmptyBuckets(Bit32 numBuckets) throw ();
        /* Forall "0 <= i < numBuckets", set "buckets[i]" to "NULL". */

	void Rehash(Bit16 logBuckets) throw ();
        /* Reallocate "2^logBuckets" buckets, and rehash the entries into
           the new table. */

	inline Bit32 Bucket(const K &k) const throw ();
	// Return the bucket index for key "k".

        void Copy(const Default* tbl) throw ();
        /* Copy the (key,value) pairs in "tbl" to this table. This table is
           not initially emptied, so any pairs originally in this table will
           be maintained (or have their values overwritten). */

	// friends
	friend class Iterator;

        // hide copy constructor
        Default(const Default&);
    };

    class Iterator {
      public:
	Iterator(const Default* tbl) throw ()
          : tbl(tbl) { Reset(); }
        /* Return a new iterator on the "Default" table "tbl". While the
           iterator is in use, the client is only allowed to modify the table
           by invoking the "Put" and "Delete" methods with "resize = false".
           Once the iteration is complete, it is recommended that the "Resize"
           method be called to adjust the size of the table if necessary. */

        void Reset() throw ();
        /* Resets the iterator so that it will iterate over all the elements
           in its associated table. This can be used to iterate over the
           elements of the same table multiple times. */

	bool Next(/*OUT*/ K& k, /*OUT*/ V& v) throw ();
        /* If there is an association in this iterator's table that has not
	   already been enumerated, select one, set "k" to its key, set "v" to
	   its value, and return "true". Otherwise, leave "k" and "v"
	   unchanged, and return "false". */
      private:
	// fields
	const Default *tbl;       // this iterator's corresponding table
        typedef typename Default::EntryList EntryList;
	EntryList *curr;          // next entry to visit if non-NULL
	Bit32 bucket;		  // next bucket if < "tbl->numBuckets"
	bool done;		  // true if next() has returned "FALSE"

        // hide copy constructor
        Iterator(const Iterator&);
    };
};

// ========== template member definitions ==========

#include "TableUtils.H"
#include "TableConsts.H"

// ----------------------------------------------- Default table methods -----

template <class K, class V>
void Table<K,V>::Default::Copy(const Default *tbl) throw ()
/* Iterate over the elements of "tbl", and put each (key,value) pair
   in this table. This could probably be done more efficiently without
   using an iterator. */
{
    Iterator iter(tbl);
    K key; V value;
    while (iter.Next(/*OUT*/ key, /*OUT*/ value)) {
	// add the pair to this table
	(void) Put(key, value, /*resize=*/ false);
    }
    Resize();
}

template <class K, class V>
void Table<K,V>::Default::Init(Bit32 sizeHint) throw ()
{
    Bit32 idealBuckets;
    float idealF = (float)sizeHint / TableConsts::IdealDensity;
    idealBuckets = (idealF > (float)TableConsts::MaxBuckets) ? TableConsts::MaxBuckets : Ceiling(idealF);
    idealBuckets = max(TableConsts::MinBuckets, idealBuckets);
    this->numEntries = 0U;
    if (this->buckets != NULL && idealBuckets < (TableConsts::One32U << this->logBuckets)) {
	// enough buckets already exist; just empty them
	EmptyBuckets(TableConsts::One32U << this->logBuckets);
    } else {
	// allocate new buckets
	this->logBuckets = this->minLogBuckets = Log_2(idealBuckets);
	NewBuckets(sizeHint > 0);
    }
}

template <class K, class V>
Table<K,V>::Default::~Default() throw ()
{
    if (this->buckets != (EntryListPtr *)NULL) {
	if ((this->maxEntries & TableConsts::DoDeletions) != 0) {
	    Bit32 numBuckets = (TableConsts::One32U << this->logBuckets);
	    for (Bit32 i = 0U; i < numBuckets; i++) {
		EntryList *curr = this->buckets[i];
		while (curr != (EntryList *)NULL) {
		    EntryList *prev = curr;
		    curr = curr->tail;
		    prev->tail = (EntryList *)NULL; // drop on floor for GC
		    delete prev;
		}
	    }
	}
	delete[] this->buckets;
	this->buckets = (EntryListPtr *)NULL; // drop on floor for GC
    }
}

template <class K, class V>
void Table<K,V>::Default::NewBuckets(bool allocBuckets) throw ()
{
    // calculate "minEntries" and "maxEntries"
    Bit32 numBuckets = (TableConsts::One32U << this->logBuckets);
    this->minEntries = Round(TableConsts::MinDensity * (float)numBuckets);
    Bit32 doDeletions = this->maxEntries & TableConsts::DoDeletions;
    this->maxEntries = TableConsts::DoDeletionsBar &
      Round(TableConsts::MaxDensity * (float)numBuckets);
    this->maxEntries |= doDeletions;

    // allocate buckets array if necessary
    if (allocBuckets) {
	AllocBuckets(numBuckets);
    } else {
	this->buckets = NULL;
    }
}

template <class K, class V>
void Table<K,V>::Default::AllocBuckets(Bit32 numBuckets) throw ()
{
    this->buckets = NEW_ARRAY(EntryListPtr, numBuckets);
    assert(this->buckets != NULL);
    EmptyBuckets(numBuckets);
}

template <class K, class V>
void Table<K,V>::Default::EmptyBuckets(Bit32 numBuckets) throw ()
{
    for (Bit32 i = 0U; i < numBuckets; i++) {
	this->buckets[i] = (EntryList *)NULL;
    }
}

template <class K, class V>
inline Bit32 Table<K,V>::Default::Bucket(const K& k) const throw ()
{
    return ((TableConsts::Multiplier * k.Hash()) >> (TableConsts::BitsPerWd - this->logBuckets));
}

template <class K, class V>
void Table<K,V>::Default::Rehash(Bit16 logBuckets) throw ()
{
    assert(logBuckets <= TableConsts::MaxLogBuckets);
    assert(logBuckets >= this->minLogBuckets);

    EntryListPtr *ob = this->buckets;	             // old buckets
    Bit16 oldLogBuckets = this->logBuckets;          // log of their old size
    Bit32 oldNumBuckets = (TableConsts::One32U << oldLogBuckets); // their old size

    // allocate new buckets
    this->logBuckets = logBuckets;
    NewBuckets(ob != NULL);

    if (ob != NULL) {
	// rehash entries from old table into new table
	for (Bit32 i = 0U; i < oldNumBuckets; i++) {
	    EntryList *curr = ob[i], *tail;
	    ob[i] = (EntryList *)NULL;
	    while (curr != (EntryList *)NULL) {
		int ix = Bucket(curr->key);
		tail = curr->tail;
		curr->tail = this->buckets[ix];
		this->buckets[ix] = curr;
		curr = tail;
	    }
	}

	// delete old buckets array
	if (this->maxEntries & TableConsts::DoDeletions)
	    delete[] ob;
    }
}

template <class K, class V>
void Table<K,V>::Default::Resize() throw ()
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
bool Table<K,V>::Default::Get(const K& k, /*OUT*/ V& v) const throw ()
{
    if (this->buckets == NULL) return false;
    register EntryList *curr = this->buckets[Bucket(k)];

    while (curr != (EntryList *)NULL && !(curr->key == k)) {
	curr = curr->tail;
    }
    if (curr != (EntryList *)NULL) {
	v = curr->val;
	return true;
    } else {
	return false;
    }
}

template <class K, class V>
bool Table<K,V>::Default::Put(const K& k, const V& v, bool resize) throw ()
{
    Bit32 b = Bucket(k);
    if (this->buckets == NULL) AllocBuckets(TableConsts::One32U << this->logBuckets);
    register EntryList *curr = this->buckets[b];

    while (curr != (EntryList *)NULL && !(curr->key == k)) {
	curr = curr->tail;
    }
    if (curr != (EntryList *)NULL) {
	curr->val = v;
	return true;
    } else {
        this->buckets[b] = NEW_CONSTR(EntryList, (k, v, this->buckets[b]));
	this->numEntries++;
	if (resize && this->numEntries > this->maxEntries
            && this->logBuckets < TableConsts::MaxLogBuckets) {
	    // too crowded
	    Rehash(this->logBuckets + 1);
	}
	return false;
    }
}

template <class K, class V>
bool Table<K,V>::Default::Delete(const K& k, /*OUT*/ V& v, bool resize)
  throw ()
{
    if (this->buckets == NULL) return false;
    Bit32 b = Bucket(k);
    EntryList *curr =  this->buckets[b];
    EntryList *prev = (EntryList *)NULL;

    while (curr != (EntryList *)NULL && !(curr->key == k)) {
	prev = curr;
	curr = curr->tail;
    }
    if (curr != (EntryList *)NULL) {
	v = curr->val;
	if (prev == (EntryList *)NULL)
	    this->buckets[b] = curr->tail;
	else
	    prev->tail = curr->tail;
	this->numEntries--;
	if (this->maxEntries & TableConsts::DoDeletions) delete curr;
	if (resize && this->numEntries < this->minEntries
            && this->logBuckets > this->minLogBuckets) {
	    // too sparse
	    Rehash(this->logBuckets - 1);
	}
	return true;
    } else {
	return false;
    }
}

// ---------------------------------------------------- Iterator methods -----

template <class K, class V>
void Table<K,V>::Iterator::Reset() throw ()
{
    this->curr = (EntryList *)NULL;
    this->bucket = 0U;
    this->done = false;
}

template <class K, class V>
bool Table<K,V>::Iterator::Next(/*OUT*/ K& k, /*OUT*/ V& v) throw ()
{
    if (this->tbl->buckets != NULL) {
	while (this->curr == (EntryList *)NULL
	       && this->bucket < (TableConsts::One32U << tbl->logBuckets)) {
	    this->curr = this->tbl->buckets[this->bucket];
	    this->bucket++;
	}
    }
    if (this->curr != (EntryList *)NULL) {
	k = curr->key;
	v = curr->val;
	this->curr = this->curr->tail;
	return true;
    } else {
	assert(!(this->done));
	this->done = true;
	return false;
    }
}

#endif // _TABLE_H
