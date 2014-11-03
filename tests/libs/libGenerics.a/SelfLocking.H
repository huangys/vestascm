// Copyright (C) 2006, Vesta Free Software Project
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

#ifndef _SELFLOCKING_H
#define _SELFLOCKING_H

// This file contains a couple template classes which wrap a mutex
// around accesses to another object.  These are intended for simple
// uses like an atomically readable/writable variable.

#include <Basics.H>

// SelfLocking is a simple wrapper around an object that only allows
// read access and simple assignment.

template<class T>
class SelfLocking
{
protected:
  Basics::mutex mu;
  T val;
public:
  SelfLocking(const T &init) : val(init) { }

  T operator=(const T &other)
  {
    mu.lock();
    T result = (val = other);
    mu.unlock();
    return result;
  }

  operator T()
  {
    mu.lock();
    T result = val;
    mu.unlock();
    return result;
  }
};

// SelfLockingInt adds the ability to increment, decrement, as well as
// the += and -= operators.  (This obviously doesn't cover all integer
// operations, but it's sufficient for simple atomic counters.)

template<class T>
class SelfLockingInt : public SelfLocking<T>
{
public:
  SelfLockingInt(const T &init) : SelfLocking<T>(init) { }

  // Note the use of this-> to access protected member variables.
  // Some compilers (notably gcc 4) don't resolve references to
  // members of template base classes without being explicit.
  // (SelfLockingInt:: or SelfLocking<T>:: might work too.)

  T operator++()
  {
    this->mu.lock();
    T result = ++this->val;
    this->mu.unlock();
    return result;
  }

  T operator++(int)
  {
    this->mu.lock();
    T result = this->val++;
    this->mu.unlock();
    return result;
  }

  T operator--()
  {
    this->mu.lock();
    T result = --this->val;
    this->mu.unlock();
    return result;
  }

  T operator--(int)
  {
    this->mu.lock();
    T result = this->val--;
    this->mu.unlock();
    return result;
  }

  T operator+=(const T &other)
  {
    this->mu.lock();
    T result = (this->val += other);
    this->mu.unlock();
    return result;
  }

  T operator-=(const T &other)
  {
    this->mu.lock();
    T result = (this->val -= other);
    this->mu.unlock();
    return result;
  }
};

#endif
