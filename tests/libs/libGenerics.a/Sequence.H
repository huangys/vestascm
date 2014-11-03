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

// Last modified on Thu Apr 21 09:26:51 EDT 2005 by irina.furman@intel.com 
//      modified on Mon Jan  3 22:36:08 EST 2005 by ken@xorian.net        
//      modified on Thu Aug  8 13:44:11 EDT 2002 by kcschalk@shr.intel.com
//      modified on Tue Feb  8 11:32:13 PST 2000 by mann   
//      modified on Wed Feb 21 12:41:35 PST 1996 by heydon 
//      modified on Sat Feb 17 11:28:56 PST 1996 by mcjones

#ifndef _SEQUENCE_H
#define _SEQUENCE_H

/* Sequence is a template class defining extensible sequences,
   patterned after the Modula-3 generic Sequence interface.  Elements
   can be added or removed at either end of a sequence; they can also
   be accessed or updated at specific indexes. The expected cost of
   every method of a sequence is constant.

   The Sequence class is parameterized by a class "Elem". The implementation
   uses the "Elem" assignment operator to add new elements to the sequence, so
   you should make sure that an appropriate assignment operator is defined for
   your element type.

   To instantiate a sequence, you use cxx's "define_template"
   pragma. For this to work, you must first include the source file
   "Sequence.C", which provides implementations for the generic
   sequence methods. However, the "Sequence.C" code must be linked into
   your executable exactly once. Therefore, the recommended procedure
   is to write an implementation (.C) file of the form:

     #include "Sequence.C"
     <template-instantiation>
     <template-instantiation>
     ...

   Each <template-instantiation> is a one-line pragma, of the form:

     #pragma define_template Sequence<Elem>

   where "Elem" is the element type of the instantiated sequence. */

// Forward declaration of STL type.
struct std::bidirectional_iterator_tag;

template <class Elem,
	  bool elem_ptrfree = false>
class Sequence {
  public:
    Sequence(int sizeHint = 5) throw ();
    /* Construct a new, empty sequence.  */

    Sequence(const Sequence<Elem, elem_ptrfree>& s) throw ();
    /* Construct a copy of "s". */

    Sequence(const Elem a[], int n) throw ();
    /* Construct a new sequence containing "a[i]" for "i" in the
       half-open interval "[0, n)". */

    ~Sequence() throw ();
    /* Free any memory allocated by this sequence. */

    Sequence<Elem, elem_ptrfree>& operator= (const Sequence<Elem, elem_ptrfree>& s)
      throw ();
    /* Assign to this sequence a copy of "s". */

    int size() const throw () { return sz; }
    /* Return the number of elements in the sequence. */

    void addhi(const Elem &x) throw ();
    /* Append "x" to the end of the sequence, without changing the
       index of any existing element. */

    void addlo(const Elem &x) throw ();
    /* Append "x" to the front of the sequence, increasing the index
       of all existing elements by one. */

    Elem remhi() throw ();
    /* Remove and return the last element of the sequence, without
       changing the index of any other element.  If the sequence
       is empty, "remhi()" causes a checked runtime error. */

    Elem remlo() throw ();
    /* Remove and return the first element of the sequence,
       decreasing the index of all other elements by one.  If the
       sequence is empty, "remlo()" causes a checked runtime
       error. */

    void put(int i, const Elem &x) throw ();
    /* Replace element "i" of the sequence with "x".  Element 0 is the
       first element. It is a checked runtime error unless "i" is
       less than "size()". */

    Elem get(int i) const throw ();
    /* Return element "i" of the sequence.  It is a checked runtime
       error unless "i" is less than "size()". */

    Elem &get_ref(int i) throw ();
    const Elem &get_ref(int i) const throw ();
    /* Return a reference to element "i" of the sequence.  It is a
       checked runtime error unless "i" is less than "size()". */

    Elem gethi() const throw ();
    /* Return the last element of the sequence; equivalent to
       "get(size()-1)". */

    Elem getlo() const throw ();
    /* Return the first element of the sequence; equivalent to
       "get(0)". */
 
    Sequence<Elem, elem_ptrfree> cat(const Sequence<Elem, elem_ptrfree> &s)
      const throw ();
    /* Return a sequence whose elements are the concatenation of
       this sequence and "s". */

    Sequence<Elem, elem_ptrfree> sub(int start, int length = INT_MAX)
      const throw ();
    /* Return a sub-sequence of this sequence: empty if "start >= size()"
       or "length == 0"; otherwise the subsequence ranging from start
       to the minimum of "start + length - 1" and "size() - 1". */

    /* Sequences are unmonitored: a client accessing a sequence
       from multiple threads must ensure that if two operations
       are active concurrently, then neither of them has
       side-effects on the sequence. */

  // Iterator class making it possible to use a Sequence with the
  // Standard Template Library (STL).
  class Iterator
  {
  public:
    // These typedefs are required to be an STL iterator.
    typedef Elem value_type;
    typedef Elem *pointer;
    typedef Elem &reference;
    typedef unsigned int size_type;
    typedef int difference_type;
    typedef std::bidirectional_iterator_tag iterator_category;
    
  private:
    Sequence<Elem, elem_ptrfree> &seq;
    unsigned int index;
  public:
    Iterator(Sequence<Elem, elem_ptrfree> &s,
	     unsigned int i) throw()
      : seq(s), index(i)
      { }
    Iterator(const Iterator &other) throw()
      : seq(other.seq), index(other.index)
      { }

    bool operator==(const Iterator& other) const
      { return (&seq == &(other.seq)) && (index == other.index); }
    bool operator!=(const Iterator& other) const
      { return (&seq != &(other.seq)) || (index != other.index); }
    bool operator<(const Iterator& other) const
      { return (&seq == &(other.seq)) && (index < other.index); }

    difference_type operator-(const Iterator& other) const
    {
      assert(&seq == &(other.seq));
      return index - other.index;
    }
    Iterator operator-(difference_type d)
    {
      Iterator r(*this);
      r.index -= d;
      return r;
    }
    Iterator operator+(difference_type d)
    {
      Iterator r(*this);
      r.index += d;
      return r;
    }

    Iterator &operator=(const Iterator& other) throw()
    {
      assert(&seq == &(other.seq));
      index = other.index;
      return *this;
    }
    Iterator& operator+=(difference_type d)
      { index += d; return *this; }
    Iterator& operator-=(difference_type d)
      { index -= d; return *this; }

    Elem &operator*() const throw()
      { return seq.get_ref(index); }

    Iterator& operator++() throw()
      { index++; return *this; }
    Iterator operator++(int)
      { Iterator r = *this; ++*this; return r; }
    Iterator& operator--() throw()
      { index--; return *this; }
    Iterator operator--(int)
      { Iterator r = *this; --*this; return r; }
  };

  // Create iterators for the first element and one past the last
  // element.
  Iterator begin() throw()
    { return Iterator(*this, 0); }
  Iterator end() throw()
    { return Iterator(*this, sz); }

  // Note: currently there's no const Iterator, and the above methods
  // can't be used on a const Sequence.  The Iterator was added
  // primarily to make it possible to use std::sort on a Sequence.
  // Some additional work is required to make an iterator that can be
  // used on a const Sequence.

  private:
    int numElem;
    Elem* elem;
    int sz;
    int st;

    /* Element i of s is stored in s.elem[(s.st + i) % s.numElem].

       A sequence s satisfies the invariant that:
         (s.elem != NULL) && (s.sz <= s.numElem)
         && ``s.elem contains numElem elements''
         && (s.size() == s.sz) && (s.numElem > 0)
         && (0 <= s.st) && (s.st < s.numElem)
    */

    // static zero element
    static Elem elemZero;

    void expand() throw ();
    /* Double the size of the sequence so it can hold more elements. */
};

// ========== template member definitions ==========

template <class Elem, bool elem_ptrfree>
Elem Sequence<Elem, elem_ptrfree>::elemZero;

template <class Elem, bool elem_ptrfree>
Sequence<Elem, elem_ptrfree>::Sequence(int sizeHint) throw ()
: numElem(max(sizeHint, 1)), sz(0), st(0)
{
  elem =(elem_ptrfree 
	 ? NEW_PTRFREE_ARRAY(Elem, numElem)
	 : NEW_ARRAY(Elem, numElem)); 
}

template <class Elem, bool elem_ptrfree>
Sequence<Elem, elem_ptrfree>::Sequence(const Sequence<Elem, elem_ptrfree> &s)
  throw ()
: numElem(s.numElem), sz(s.sz), st(0)
{
  elem =(elem_ptrfree 
	 ? NEW_PTRFREE_ARRAY(Elem, s.numElem)
	 : NEW_ARRAY(Elem, s.numElem)); 
  for (int i = 0, j = s.st; i < sz; i++, j++) {
    if (j == s.numElem) j = 0;
    elem[i] = s.elem[j];
  }
}

template <class Elem, bool elem_ptrfree>
Sequence<Elem, elem_ptrfree>::Sequence(const Elem a[], int n) throw ()
: numElem(max(n, 1)), sz(n), st(0)
{
  elem =(elem_ptrfree 
	 ? NEW_PTRFREE_ARRAY(Elem, numElem)
	 : NEW_ARRAY(Elem, numElem)); 
  for (int i = 0; i < n; i++)
	elem[i] = a[i];
}

template <class Elem, bool elem_ptrfree>
Sequence<Elem, elem_ptrfree>::~Sequence() throw ()
{
    delete[] elem; // call elem[i].~Elem() for 0 <= i < numElem
}

template <class Elem, bool elem_ptrfree>
Sequence<Elem, elem_ptrfree>& Sequence<Elem, elem_ptrfree>::operator= (const Sequence<Elem, elem_ptrfree> &s)
  throw ()
{
    delete[] elem; // call elem[i].~Elem() for 0<=i<numElem
    numElem = s.numElem;
    elem =(elem_ptrfree 
	   ? NEW_PTRFREE_ARRAY(Elem, numElem)
	   : NEW_ARRAY(Elem, numElem)); 
    sz = s.sz;
    st = 0;
    for (int i = 0, j = s.st; i < sz; i++, j++) {
        if (j == s.numElem) j = 0;
        elem[i] = s.elem[j];
    }
    return *this;
}

template <class Elem, bool elem_ptrfree>
void Sequence<Elem, elem_ptrfree>::expand() throw ()
{
  Elem* elemNew =(elem_ptrfree 
		  ? NEW_PTRFREE_ARRAY(Elem, 2*numElem)
		  : NEW_ARRAY(Elem, 2*numElem)); 
    int m = numElem - st;
    int i,j;
    for (i = 0, j = st; i < m; i++, j++)       elemNew[i] = elem[j];
    for (i = m, j = 0 ; i < numElem; i++, j++) elemNew[i] = elem[j];

    numElem = 2 * numElem;
    delete[] elem; // call elem[i].~Elem() for 0 <= i < numElem
    elem = elemNew;
    st = 0;
}

template <class Elem, bool elem_ptrfree>
void Sequence<Elem, elem_ptrfree>::addhi(const Elem &x) throw ()
{
    if (sz == numElem) expand();
    int i = st + sz;
    if (numElem <= i) i -= numElem;
    elem[i] = x;
    sz++;
}

template <class Elem, bool elem_ptrfree>
void Sequence<Elem, elem_ptrfree>::addlo(const Elem &x) throw ()
{
    if (sz == numElem) expand();
    int i = st;
    if (i == 0) i = numElem;
    i--;
    elem[i] = x;
    st = i;
    sz++;
}

template <class Elem, bool elem_ptrfree>
Elem Sequence<Elem, elem_ptrfree>::remhi() throw ()
{
    assert(0 < sz);
    int i = st+sz-1;
    if (numElem <= i) i -= numElem;
    Elem res(elem[i]);
    elem[i] = elemZero;
    sz--;
    return res;
}

template <class Elem, bool elem_ptrfree>
Elem Sequence<Elem, elem_ptrfree>::remlo() throw ()
{
    assert(0 < sz);
    Elem res(elem[st]);
    elem[st] = elemZero;
    st++; if (st == numElem) st = 0;
    sz--;
    return res;
}

template <class Elem, bool elem_ptrfree>
void Sequence<Elem, elem_ptrfree>::put(int i, const Elem &x) throw ()
{
    assert((0 <= i) && (i < sz));
    int j = st + i;
    if (numElem <= j) j -= numElem;
    elem[j] = x;
}

template <class Elem, bool elem_ptrfree>
Elem Sequence<Elem, elem_ptrfree>::get(int i) const throw ()
{
    assert((0 <= i) && (i < sz));
    int j = st + i;
    if (numElem <= j) j -= numElem;
    return elem[j];
}

template <class Elem, bool elem_ptrfree>
Elem &Sequence<Elem, elem_ptrfree>::get_ref(int i) throw ()
{
    assert((0 <= i) && (i < sz));
    int j = st + i;
    if (numElem <= j) j -= numElem;
    return elem[j];
}

template <class Elem, bool elem_ptrfree>
const Elem &Sequence<Elem, elem_ptrfree>::get_ref(int i) const throw ()
{
    assert((0 <= i) && (i < sz));
    int j = st + i;
    if (numElem <= j) j -= numElem;
    return elem[j];
}

template <class Elem, bool elem_ptrfree>
Elem Sequence<Elem, elem_ptrfree>::gethi() const throw ()
{
    assert(0 < sz);
    int j = st + sz - 1;
    if (numElem <= j) j -= numElem;
    return elem[j];
}

template <class Elem, bool elem_ptrfree>
Elem Sequence<Elem, elem_ptrfree>::getlo() const throw ()
{
    assert(0 < sz);
    return elem[st];
}

template <class Elem, bool elem_ptrfree>
Sequence<Elem, elem_ptrfree> Sequence<Elem, elem_ptrfree>::cat(const Sequence<Elem, elem_ptrfree> &s)
  const throw ()
{
    int i;
    Sequence<Elem, elem_ptrfree> u(sz + s.sz);
    u.sz = sz + s.sz;
    for (i = 0; i < sz;   i++) u.elem[i] = get(i);
    for (i = 0; i < s.sz; i++) u.elem[sz+i] = s.get(i);
    return u;
}

template <class Elem, bool elem_ptrfree>
Sequence<Elem, elem_ptrfree> Sequence<Elem, elem_ptrfree>::sub(int start,
							       int length)
  const throw ()
{
    int n = (sz <= start) ? 0 : min(length, sz - start);
    Sequence<Elem, elem_ptrfree> u(n);
    u.sz = n;
    for (int i = 0; i < n; i++)
	u.elem[i] = get(start + i);
    return u;
}

#endif // _SEQUENCE_H
