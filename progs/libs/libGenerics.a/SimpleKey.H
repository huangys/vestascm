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

// SimpleKey -- a template class to make any integer type usable as
// the key with the Table class.  (This avoids duplication between key
// classes for signed and unsigned integers and integers of different
// sizes.)

#ifndef _SIMPLE_KEY_H
#define _SIMPLE_KEY_H

#include <Basics.H>

template<class T>
class SimpleKey {
public:
  // constructor
  SimpleKey() throw () : value(0) { }
  SimpleKey(T val) throw () : value(val) { }

  // methods required by "Table.H"
  Word Hash() const throw () { return (Word)value; }
  T& operator = (const T& val) { return (value = val); }
  bool operator == (const SimpleKey<T>& other) const throw ()
    { return value == other.value; }

  // misc methods
  operator T() const throw() { return value; }
  T Val() const throw() { return value; }
  bool operator != (const SimpleKey<T>& other) const throw ()
    { return value != other.value; }
  bool operator < (const SimpleKey<T>& other) const throw ()
    { return value < other.value; }
  bool operator <= (const SimpleKey<T>& other) const throw ()
    { return value <= other.value; }
  bool operator > (const SimpleKey<T>& other) const throw ()
    { return value > other.value; }
  bool operator >= (const SimpleKey<T>& other) const throw ()
    { return value >= other.value; }
  friend std::ostream& operator << (std::ostream& s,
				    const SimpleKey<T>& k) throw ()
    { return s << k.value; }
protected:
  T value;
};

#endif // _SIMPLE_KEY_H
