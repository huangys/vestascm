// Copyright (C) 2003, Kenneth C. Schalk

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

// Last modified on Sun Apr 17 22:55:47 EDT 2005 by ken@xorian.net        

#ifndef _Basics_Allocator
#define _Basics_Allocator

#include <stddef.h>
#include "Basics.H"

namespace Basics
{
  // This is an STL-style allocator that uses Basics::malloc and
  // Basics::free.  This makes it possible to use STL classes in a
  // program linked with the garbage collector.  At this time it does
  // not support using GC attributes (e.g. for telling the collector
  // that a block is pointer-free).

  template <class T>
  class Allocator {
  public:
    typedef size_t     size_type;
    typedef ptrdiff_t  difference_type;
    typedef T*       pointer;
    typedef const T* const_pointer;
    typedef T&       reference;
    typedef const T& const_reference;
    typedef T        value_type;

    template <class T1> struct rebind {
      typedef Allocator<T1> other;
    };

    Allocator()  {}
    template <class T1> Allocator(const Allocator<T1>&) throw() {}
    ~Allocator() throw() {}

    pointer address(reference x) const { return &x; }
    const_pointer address(const_reference x) const { return &x; }

    T* allocate(size_type n, const void* = 0) {
      return (T *) ::operator new(n*sizeof(T));
    }

    void deallocate(pointer p, size_type n)
    {
      ::operator delete((void *) p);
    }

    size_type max_size() const throw()
    { return size_t(-1) / sizeof(T); }

    void construct(pointer p, const T& __val) { new(p) T(__val); }
    void destroy(pointer p) { p->~T(); }

    bool  operator==(const Allocator<T>&) throw()
    {
      return true;
    }
  };
}

#endif
