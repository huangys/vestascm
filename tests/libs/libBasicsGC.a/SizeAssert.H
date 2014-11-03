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

// Last modified on Wed Nov  7 14:40:45 EST 2001 by ken@xorian.net

// Define an assertion that allows us to check the result of the
// sizeof operator.  This helps identify portability problems with
// size type differences between platforms.

#ifndef __SizeAssert
#define __SizeAssert

// This function will be called in the result of a mismatch between
// the actual and expected data size.
extern void sizeof_assert_fail(const char * typ,
			       unsigned long gotsize, unsigned long targsize,
			       const char * fname, unsigned int line,
			       bool die);

// This is the macro that other code should use.  By default, a
// message is printed but execution continues.  To force a failure on
// a size mismatch, define the SIZE_ASSERT_ABORT macro before this
// file is included (e.g. on the command line).

#ifdef SIZE_ASSERT_ABORT
#define sizeof_assert(a, b) \
   ((sizeof(a) == (b)) ? (b) : \
    (sizeof_assert_fail(#a, sizeof(a), b, __FILE__, __LINE__, true), \
     sizeof(a)))
#else
#define sizeof_assert(a, b) \
   ((sizeof(a) == (b)) ? (b) : \
    (sizeof_assert_fail(#a, sizeof(a), b, __FILE__, __LINE__, false), \
     sizeof(a)))
#endif

// Example use:
//
//   write(fd, &data, sizeof_assert(data, 4))
//
// If we think data is 4 bytes

#endif // __SizeAssert
