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

//  **************
//  *  Basics.H  *
//  **************

//  This header file contains definitions that are closely connected
//  to the language definition.

#ifndef _Basics
#define _Basics

#include <climits>
// Local configuration results
#include "BasicsConfig.h"

// Provide sizeof assertion capability
#include "SizeAssert.H"

#if defined(__DECCXX) && !defined(_BOOL_EXISTS)
typedef char bool;
const bool true = 1;
const bool false = 0;
#endif

#if defined (HAVE_STDINT_H)
#include <stdint.h>
#endif

#include <sys/types.h>

// Conversions between host and network byte order.
#if defined (HAVE_MACHINE_ENDIAN_H)
#include <machine/endian.h>
#endif
#include <netinet/in.h>

// the names of the boolean values
extern const char* const BoolName[];

// Character string manipulation (char *)
//  (The use of String is deprecated.)

#include <ctype.h>
#include <string.h>

// More standard libraries

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

// C++ stream library.  Some of this is quasi-OS-dependent, but
// it seems pedantic to try to split it between Basics.H and OS.H.

#include <iostream>
#include <iosfwd>
#include <fstream>

// Miscellaneous utility functions (arithmetic, random, etc.)

inline int min(int a, int b) { return ((a < b) ? a : b); }
inline int max(int a, int b) { return ((a > b) ? a : b); }

// The macro CONST_INT_64 is used for 64-bit integer constants, which
// are used in a few places.  These tend to require special handling
// on non-64-bit platforms.

#if (SIZEOF_UNSIGNED_LONG == 8)
// Any 64-bit platform should probably use this clause.  Modify the
// #if when porting to a new platform.
#define CONST_INT_64(x) x##L
#define CONST_UINT_64(x) x##UL

#elif defined(SIZEOF_UNSIGNED_LONG_LONG) && (SIZEOF_UNSIGNED_LONG_LONG == 8)
// The GNU compiler uses the LL suffix to denote a 64-bit integer
// constant.  This should work on platforms where long is also 64
// bits.
#define CONST_INT_64(x) x##LL
#define CONST_UINT_64(x) x##ULL

#else
#error Need 64-bit constant macro for this platform
#endif

// ----------------------------------------------------------------------
// Integer types for use when the specific number of bits is
// important.  Similar typedefs are often defined by system headers,
// but unfortunately the names of the types and the header files which
// define them are inconsist between platforms.  We isolate these in a
// namespace to avoid the possibility of conflicting with
// similarly-named system-defined types.
// ----------------------------------------------------------------------

namespace Basics
{
  // On all platforms tested, these typedefs are the same.  However,
  // porting to new platforms may require wrapping some of these in
  // #if's.  Be sure to run the TestSizes program to check that these
  // are all the right size.
  typedef char            int8;
  typedef unsigned char  uint8;

#if (SIZEOF_UNSIGNED_SHORT == 2)
  typedef short           int16;
  typedef unsigned short uint16;
#else
#error Need 16-bit integer typedefs
#endif

#if (SIZEOF_UNSIGNED_INT == 4)
  typedef int             int32;
  typedef unsigned int   uint32;
#else
#error Need 32-bit integer typedefs
#endif

#if (SIZEOF_UNSIGNED_LONG == 8)
  // Any platform where a long is 64 bits can probably use these
  // typedefs.  Modify the #if when porting to a new 64-bit platform.

  typedef long            int64;
  typedef unsigned long  uint64;

#elif defined(SIZEOF_UNSIGNED_LONG_LONG) && (SIZEOF_UNSIGNED_LONG_LONG == 8)
  // Fall through for the GNU compiler on any platform: long long
  // should be a 64-bit integer.  This should work on platforms where
  // long is also 64 bits.

  typedef long long           int64;
  typedef unsigned long long uint64;

#else
  // Force a compilation failure if we don't know what to do.

#error Need 64-bit typedefs for this platform
#endif

  // Swap bytes in integers of different sizes.  This allows
  // big-endian machines to read littl-eendian values and vice-versa.

  inline uint16 swab16(uint16 p_x)
  {
    return (((p_x & 0x00ff) << 8)  |
	    ((p_x & 0xff00) >> 8));
  }

  inline uint32 swab32(uint32 p_x)
  {
    return (((p_x & 0x000000ff) << 24)  |
	    ((p_x & 0x0000ff00) << 8)  |
	    ((p_x & 0x00ff0000) >> 8)  |
	    ((p_x & 0xff000000) >> 24));
  }

  inline uint64 swab64(uint64 p_x)
  {
    return (((p_x & CONST_INT_64(0xff00000000000000)) >> 56) |
	    ((p_x & CONST_INT_64(0x00ff000000000000)) >> 40) |
	    ((p_x & CONST_INT_64(0x0000ff0000000000)) >> 24) |
	    ((p_x & CONST_INT_64(0x000000ff00000000)) >>  8) |
	    ((p_x & CONST_INT_64(0x00000000ff000000)) <<  8) |
	    ((p_x & CONST_INT_64(0x0000000000ff0000)) << 24) |
	    ((p_x & CONST_INT_64(0x000000000000ff00)) << 40) |
	    ((p_x & CONST_INT_64(0x00000000000000ff)) << 56));
  }

  // htons/ntohs operate on 16-bit values.

  inline uint16 hton16(uint16 p_x)
  {
    return htons(p_x);
  }

  inline uint16 ntoh16(uint16 p_x)
  {
    return ntohs(p_x);
  }

  // htonl/ntohl operate on 32-bit values

  inline uint32 hton32(uint32 p_x)
  {
    return htonl(p_x);
  }

  inline uint32 ntoh32(uint32 p_x)
  {
    return ntohl(p_x);
  }

  // There's no standard for converting 64-bit values to/from network
  // byte order.

  inline uint64 hton64(uint64 p_x)
  {
#if BYTE_ORDER == BIG_ENDIAN
    return p_x;
#elif BYTE_ORDER == LITTLE_ENDIAN
    return swab64(p_x);
#else
  // Force a compilation failure if we don't know what to do.

#error Unknown byte order
#endif
  }

  inline uint64 ntoh64(uint64 p_x)
  {
    return hton64(p_x);
  }

  // Memory allocation attributes for use with a garbage collection
  // library.

  // The caller promises that this block will not contain pointers to
  // other heap blocks.  This should be used for character strings,
  // bitmaps, etc., to reduce the possibility of heap blocks beaing
  // conservatively kept due to random data being misidentified as
  // pointers.
  struct gc_no_pointers_t { };
  extern gc_no_pointers_t gc_no_pointers;
}

#define NEW(t) new (__FILE__, __LINE__) t
#define NEW_CONSTR(t, args) new (__FILE__, __LINE__) t args
#define NEW_ARRAY(t, size) new (__FILE__, __LINE__) t[size]
#define NEW_PTRFREE(t) new (Basics::gc_no_pointers, __FILE__, __LINE__) t
#define NEW_PTRFREE_CONSTR(t, args) new (Basics::gc_no_pointers, __FILE__, __LINE__) t args
#define NEW_PTRFREE_ARRAY(t, size) new (Basics::gc_no_pointers, __FILE__, __LINE__) t[size]

// Use placement new variants to allow get information about the
// source location of the allocation and add attributes like "no
// pointers in this block".
void *operator new(size_t size, Basics::gc_no_pointers_t unused,
		   const char *file, unsigned int line);
void *operator new[](size_t size, Basics::gc_no_pointers_t unused,
		     const char *file, unsigned int line);

void *operator new(size_t size, const char *file, unsigned int line);
void *operator new[](size_t size, const char *file, unsigned int line);

// The Bit{N} typedefs represent unsigned integers, often used for
// bitfields and the like.  The use of these names pre-dates porting
// efforts that necessitated signed integer types of different sizes.
// It would be harmless but time-consuming to replace them with the
// types they are defined as aliases of.
typedef Basics::uint8 Bit8;
typedef Basics::uint16 Bit16;
typedef Basics::uint32 Bit32;
typedef Basics::uint64 Bit64;
typedef struct { Bit8 byte[32]; } Byte32;

// Word was originally intended to be "the machine's largest unsigned
// integer", analogous to Modula-3's "Word.T" and useful especially in
// library code transliterated from Modula-3.  However, it's tricky to
// use such a type properly, and we ended up with a lot of code that
// assumes a Word is 64 bits.
typedef Bit64 Word;

// There are some places in the code that do fancy pointer math.  For
// those purposes, we use this typedef which should be an unsigned
// integer exactly the same size as a pointer.  On most every
// platform, sizeof(long) == sizeof(void *).  (The TestSizes program
// will report an error message if PointerInt is not the same size as
// a pointer.)
#if (SIZEOF_UNSIGNED_LONG == SIZEOF_VOID_P)
typedef unsigned long PointerInt;
#elif (SIZEOF_UNSIGNED_LONG_LONG == SIZEOF_VOID_P)
typedef unsigned long long PointerInt;
#else
#error Need pointer-sized integer typedef
#endif

// The size of the C++ 'bool' type is not always the same.  (On Tru64
// with the Compaq C++ compiler, it's one byte, on Alpha Linux with
// the GNU compiler it's 8 bytes.)  However, we need a boolean type
// which we can depend on always being just 8 bits in size.  So, we
// define our own bool type here called "bool8" (as in "an 8-bit
// bool").
typedef Basics::int8 bool8;

// When using printf-style formatting of integers, the
// FORMAT_LENGTH_INT_64 macro should be used for the length field of a
// 64-bit integer format specifier, like so:
//
//    printf("%" FORMAT_LENGTH_INT_64 "x\n", var);
//
// Note: The adjacent string constants will be merged by the compiler.

// FORMAT_LENGTH_INT_64 should now be defined by the
// auto-configuration we included from BasicsConfig.h

#if !defined(FORMAT_LENGTH_INT_64)
// See the target platform's printf(3) man page.
#error Need 64-bit integer format length specified for this platform
#endif

// PathnameSep was defined early-on as a nod to potential Windows
// compatibility.  At this point a Windows port of Vesta, at least in
// its current form, seems unlikely.  While this constant is
// vestigial, there would be little point in eliminating it.
#if !defined(__unix) && (defined (_WIN32) || defined (__MSDOS__) || defined (__DJGPP__) || defined (__OS2__))
const char PathnameSep = '\\';
#else
const char PathnameSep = '/';
#endif

// Threads

// #define _PTHREAD_USE_INLINE
#include <pthread.h>
#include "Thread.H"

// Text manipulation (i.e., char* with more automatic storage mgmt.)

#include "Text.H"

namespace Basics
{
  // Given an integer error value taken from errno, return a Text
  // containing the correposning error message.
  Text errno_Text(int err);

  // compare two Texts lexically, but with recognizing embedded integers
  // and comparing them numerically
  bool TextLessThanWithInts(const Text &a, const Text &b);
}

#endif  /* _Basics  */
