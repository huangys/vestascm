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

#ifndef ListT_H
#define ListT_H

/*****************************************************************************
Data types and operations for Lisp-like lists

This interface provides Lisp-like lists and their most useful
operations.  Why lists?  The list has proven to be an extremely flexible
data structure with many powerful operations and acceptable efficiency
for many, many applications.  Sequences, sets, tables, and other sorts
of mappings can all be implemented easily with lists, often with more
than enough efficiency for the particular application.
*****************************************************************************/

#include <Basics.H>

template <class Value>
class PairT {
public:
  Value first;
  PairT<Value> *tail;
  static PairT<Value> *New(const Value& first, PairT<Value> *tail);
};

template <class Value>
class ListT {
private:
  PairT<Value> *list;
  PairT<Value> *last;

public:
  ListT() : list(NULL), last(NULL) { /*SKIP*/ };
  ListT(const Value& x1);
  ListT(const Value& x1, const Value& x2);
  ListT(const Value& x1, const Value& x2, const Value& x3); 
    /*
    Constructors for convenience:
        List()            = NIL
        ListT(x1)         = New(x1, NIL)
	ListT(x1, x2)     = New(x1, New(x2, NIL)) 
	ListT(x1, x2, x3) = New(x1, New(x2, New(x3, NIL))) 
    */

  ListT(const Value& first, const ListT<Value>& tail);
    /* Constructs a new list cell (Lisp CONS). */

  ListT<Value>& operator=(const ListT<Value>& lst);
    /* list assignment */

  bool operator==(const ListT<Value>& lst) const;
    /* list equality */

  bool operator!=(const ListT<Value>& lst) const;
    /* list inequality */

  bool Null() const { return (list == NULL); };
    /* Return false if there is at least one element on the list. */

  void SetEmpty() { list = NULL; last = NULL; };
    /* Set the list to be empty. */

  void Push(const Value& x);  
    /*
    Pushes a new element onto the front of "lst", and sets "lst" to be 
    the new list.  Equivalent to:
    
        lst := New( x, lst );
    */

  Value Pop();
    /*
    Removes one element from the front of "lst", and sets "lst" to the 
    new list.   Equivalent to:
        result := lst^.first
	l      := lst^.tail;
    */

  int Length() const;
    /* Returns the length of a list. */

  Value First() { return list->first; };
  Value Second() { return list->tail->first; };
  Value Third() { return list->tail->tail->first; };
    /* Procedural accessors of list elements. */

  ListT<Value> Tail();
    /* returns the tail of "lst". */

  Value Nth(int n) const;
    /*
    The nth element of a list;  n=0 gets the first element.   Undefined
    if n < 0 or n >= Length( lst ). */

  void SetNth(int n, const Value& x);
   /*
   Destructively changes the nth element of a list to be x.  Undefined
   if n < 0 or n >= Length( lst ). */

  ListT<Value> NthTail(int n);
   /*
   The result of applying Tail to lst n times. Undefined if n < 0 or 
   n >= Length( lst ). */

  void SetNthTail(int n, const ListT<Value>& x);
   /*
   Destructively changes the nth tail of a list to be x.  Undefined
   if n < 0 or n >= Length( lst ). */

  Value Last() { return last->first; };
   /*
   The last element of lst, equivalent to Nth( lst, Length( lst ) - 1 ). 
   */

  ListT<Value> LastTail();
   /*
   The last tail of lst, equivalent to NthTail( lst, Length( lst ) - 1 ). 
   */

  ListT<Value> FirstN(int n);
   /*
   A new list consisting of the first n elements of lst.  Undefined if 
   n < 0 or n > Length( lst ). */

  bool EqualQ(const ListT<Value>& x);
   /*
   Compares two objects for equality.

   Equal is isomorphic structure comparison in which two lists are
   equal if their elements are '=='.

   EqualQ is pointer equality (Lisp EQ or Modula-2 "=").  It is
   included for consistency and application; normally "=" should be
   used instead. */

  bool Member(const Value& x) const;
    /*
    Returns true if for some y at the top level of list lst, y = x;
    returns false otherwise.  Member tests equality using ==. */

  ListT<Value> Append(const ListT<Value>& l2) const;
  ListT<Value> AppendD(const ListT<Value>& l2);
    /*
    Appends two lists together, returning the new list.  AppendD doesn't
    create any new list cells, but it destroys its first argument. */

  ListT<Value> Append1(const Value& x);
  ListT<Value> Append1D(const Value& x);
    /*
    Appends an element onto the end of a list, returning the new list. */

  ListT<Value> Copy();
    /*
    Copy returns a copy of a list, where only the top level of the list is
    copied (the elements are not copied). */

  ListT<Value> Reverse();
  ListT<Value> ReverseD();
    /* Reverses a list. */
};

// ----------------------------------------------------------------------
// Template method definitions
// ----------------------------------------------------------------------

template <class Value>
PairT<Value> *PairT<Value>::New(const Value& first, PairT<Value> *tail) {
  PairT<Value> *result;

  result = NEW(PairT<Value>);
  result->first = first;
  result->tail = tail;
  return result;
}

template <class Value>
ListT<Value>& ListT<Value>::operator=(const ListT<Value>& lst) {
  if (this == &lst) return *this;
  list = lst.list;
  last = lst.last;
  return *this;
}

template <class Value>
ListT<Value>::ListT(const Value& x) {
  list = PairT<Value>::New(x, NULL);
  last = list;
}

template <class Value>
ListT<Value>::ListT(const Value& x1, const Value& x2) {
  PairT<Value> *l;

  l       = PairT<Value>::New(x1, NULL); list = l;
  l->tail = PairT<Value>::New(x2, NULL); last = l->tail;
}

template <class Value>
ListT<Value>::ListT(const Value& x1, const Value& x2, const Value& x3) {
  PairT<Value> *l;

  l       = PairT<Value>::New(x1, NULL); list = l;
  l->tail = PairT<Value>::New(x2, NULL); l    = l->tail;
  l->tail = PairT<Value>::New(x3, NULL); last = l->tail;
}

template <class Value>
ListT<Value>::ListT(const Value& first, const ListT<Value>& tail) {
    list = PairT<Value>::New(first, tail.list);
    last = (tail.last) ? tail.last : list;
}

template <class Value>
bool ListT<Value>::operator==(const ListT<Value>& l) const {
  PairT<Value> *rest1, *rest2;
  
  if (this == &l) return true;
  rest1 = list;
  rest2 = l.list;
  while (true) {
    if (rest1 == NULL)
      return (rest2 == NULL);
    else if (rest2 == NULL)
       return false;
    if (rest1->first != rest2->first)
      return false;
    else
      rest1 = rest1->tail;
      rest2 = rest2->tail;
  }
}

template <class Value>
bool ListT<Value>::operator!=(const ListT<Value>& l) const {
  return (!(*this == l));
}

template <class Value>
void ListT<Value>::Push(const Value& x) {
  *this = ListT<Value>(x, *this);
}

template <class Value>
Value ListT<Value>::Pop() {
  Value x;

  x = list->first;
  list = list->tail;
  if (!list) last = NULL;
  return x;
}

template <class Value>
int ListT<Value>::Length() const {
  int len = 0;
  PairT<Value> *rest = list;

  while (rest) {
    rest = rest->tail;
    len++;
  }
  return len;
}

template <class Value>
ListT<Value> ListT<Value>::Tail() {
  ListT<Value> result;
  
  result.list = list->tail;
  result.last = (result.list) ? last : NULL;
  return result;
}

template <class Value>
Value ListT<Value>::Nth(int n) const {
  int i = 0;
  PairT<Value> *rest = list;

  while (i++ < n)
    {
      assert(rest != 0);
      rest = rest->tail;
    }
  return rest->first;
}

template <class Value>
void ListT<Value>::SetNth(int n, const Value& x) {
  PairT<Value> *rest = list;

  while (n-- > 0)
    {
      assert(rest != 0);
      rest = rest->tail;
    }
  rest->first = x;
}

template <class Value>
ListT<Value> ListT<Value>::NthTail(int n) {
  int i = 0; 
  PairT<Value> *rest = list;
  ListT<Value> result;

  while (i++ < n)
    {
      assert(rest != 0);
      rest = rest->tail;
    }
  result.list = rest;
  result.last = (result.list) ? last : NULL;
  return result;
}

template <class Value>
void ListT<Value>::SetNthTail(int n, const ListT<Value>& x) {
  PairT<Value> *rest = list;

  while (n-- > 0)
    {
      assert(rest != 0);
      rest = rest->tail;
    }
  rest->tail = x.list;
  last = (x.last) ? x.last : rest;
}

template <class Value>
ListT<Value> ListT<Value>::LastTail() {
  PairT<Value> *rest = list;
  ListT<Value> result;

  if (rest == NULL) return result;
  while (rest->tail) rest = rest->tail;
  result.list = rest;
  result.last = rest;
  return result;
}

template <class Value>
ListT<Value> ListT<Value>::FirstN(int n) {
  ListT<Value> result;
  PairT<Value> *rest, *resultEnd;

  if (n <= 0) return result;
  resultEnd = PairT<Value>::New(list->first, NULL);
  result.list = resultEnd;
  rest = list->tail;
  int i = 1;
  while (i++ < n) {
    assert(rest != 0);
    resultEnd->tail = PairT<Value>::New(rest->first, NULL);
    resultEnd = resultEnd->tail;
    rest = rest->tail;
  }
  return result;
}

template <class Value>
bool ListT<Value>::EqualQ(const ListT<Value>& x) {
  return (list == x.list);
}

template <class Value>
bool ListT<Value>::Member(const Value& x) const {
  PairT<Value> *rest = list;

  while (rest) {
    if (rest->first == x) return true;
    rest = rest->tail;
  }
  return false;
}

template <class Value>
ListT<Value> ListT<Value>::Append(const ListT<Value>& l2) const {
  ListT<Value> result;
  PairT<Value> *rest, *end;

  if (list == NULL) return l2;
  if (l2.list == NULL) return *this;

  result = ListT<Value>(list->first);
  end = result.list;
  rest = list->tail;
  while (rest) {
    end->tail = PairT<Value>::New(rest->first, NULL);
    end = end->tail;
    rest = rest->tail;
  }
  end->tail = l2.list;
  result.last = l2.last;

  return result;
}

template <class Value>
ListT<Value> ListT<Value>::AppendD(const ListT<Value>& l2) {
  if (l2.list == NULL) return *this;
  if (list == NULL) {
    list = l2.list;
    last = l2.last;
  }
  else {
    last->tail = l2.list;
    last = l2.last;
  }
  return *this;
}

template <class Value>
ListT<Value> ListT<Value>::Append1(const Value& x) {
  ListT<Value> result;
  PairT<Value> *rest, *end;

  if (list == NULL) return ListT<Value>(x);
  result = ListT<Value>(list->first);
  end = result.list;
  rest = list->tail;
  while (rest) {
    end->tail = PairT<Value>::New(rest->first, NULL);
    end = end->tail;
    rest = rest->tail;
  }
  end->tail = PairT<Value>::New(x, NULL);
  result.last = end->tail;

  return result;
}

template <class Value>
ListT<Value> ListT<Value>::Append1D(const Value& x) {
  if (list == NULL) {
    list = PairT<Value>::New(x, NULL);
    last = list;
  }
  else {
    last->tail = PairT<Value>::New(x, NULL);
    last = last->tail;
  }
  return *this;
}

template <class Value>
ListT<Value> ListT<Value>::Copy() {
  ListT<Value> result;
  PairT<Value> *end, *rest;

  if (list == NULL) return *this;
  result = ListT<Value>(list->first);
  end = result.list;
  rest = list->tail;
  while (rest) {
    end->tail = PairT<Value>::New(rest->first, NULL);
    end = end->tail;
    rest = rest->tail;
  }
  result.last = end;
  return result;
}

template <class Value>
ListT<Value> ListT<Value>::Reverse() {
  ListT<Value> result;
  PairT<Value> *rest;

  rest = list;
  while (rest != NULL) {
    result.list = PairT<Value>::New(rest->first, result.list);
    rest = rest->tail;
  }
  result.last = list;
  return result;
}

template <class Value>
ListT<Value> ListT<Value>::ReverseD() {
  PairT<Value> *current, *next, *nextTail;  
  ListT<Value> result;

  if (list == NULL) return *this;
  current = list;
  next = list->tail;
  current->tail = NULL;
  while (next) {
    nextTail = next->tail;
    next->tail = current;
    current = next;
    next = nextTail;
  }
  result.list = current;
  result.last = list;
  return result;
}

#endif /* ListT_H */
