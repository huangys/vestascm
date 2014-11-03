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

// Last modified on Mon May  2 11:12:19 EDT 2005 by ken@xorian.net        
//      modified on Sun Jul 28 11:40:14 EDT 2002 by lken@remote.xorian.net
//      modified on Wed Dec 27 13:58:29 PST 1995 by heydon

#include <sys/types.h> // for "size_t"

#include <new> // std::bad_alloc

#include "Basics.H"    // for Basics::gc_attribs

Basics::gc_no_pointers_t Basics::gc_no_pointers;

// Define our allocation function as a pass-through to the normal new
// operator.

void *operator new(size_t size, Basics::gc_no_pointers_t unused,
		   const char *file, unsigned int line)
{
  void *result;
  try
    {
      result = ::operator new(size);
    }
  catch (std::bad_alloc)
    {
      std::cerr << "Out of memory trying to allocate "
                << size << " bytes at "
                << file << ":" << line << std::endl;
      abort();
    }
  return result;
}

void *operator new[](size_t size, Basics::gc_no_pointers_t unused,
		     const char *file, unsigned int line)
{
  void *result;
  try
    {
      result = ::operator new[](size);
    }
  catch (std::bad_alloc)
    {
      std::cerr << "Out of memory trying to allocate "
                << size << " bytes at "
                << file << ":" << line << std::endl;
      abort();
    }
  return result;
}

void *operator new(size_t size, const char *file, unsigned int line)
{
  void *result;
  try
    {
      result = ::operator new(size);
    }
  catch (std::bad_alloc)
    {
      std::cerr << "Out of memory trying to allocate "
                << size << " bytes at "
                << file << ":" << line << std::endl;
      abort();
    }
  return result;
}

void *operator new[](size_t size, const char *file, unsigned int line)
{
  void *result;
  try
    {
      result = ::operator new[](size);
    }
  catch (std::bad_alloc)
    {
      std::cerr << "Out of memory trying to allocate "
                << size << " bytes at "
                << file << ":" << line << std::endl;
      abort();
    }
  return result;
}
