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

// Last modified on Sat May 29 14:09:08 EDT 2004 by ken@xorian.net

#include <iostream>	// cerr
#include <stdlib.h>	// abort
#include <assert.h>	// assert

#include "SizeAssert.H"

using std::cerr;
using std::endl;

// sizeof_assert_fail:

// Handle a sizeof assertion.  Assume that the actual size doesn't
// match the expected size (if not, this function won't be called).
// Print a message to standard error.  Optionally, abort execution
// (which usually results in a core dump as well).

// Parameters:

//   typ:      a string representation of the type
//   gotsize:  the actual size of the type
//   targsize: the expected size
//   fname:    the source file name of the sizeof assertion
//   line:     the source file line of the sizeof assertion
//   die:      should we abort?

void sizeof_assert_fail(const char * typ,
			unsigned long gotsize, unsigned long targsize,
			const char * fname, unsigned int line,
			bool die)
{
  // The only way we should get here is if we got a size other than
  // what we expected.
  assert(gotsize != targsize);

  // Issue a message about it
  cerr << "Bad size for type '" << typ << "': got " << gotsize
       << ", expected " << targsize
       << " at " << fname << ":" << line << endl;

  // If we are supposed to abort on a size mismatch (the same thing as
  // a failed assertion), do so.
  if(die)
    {
      abort();
    }
}
