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

// Last modified on Tue May 24 01:25:54 EDT 2005 by ken@xorian.net
//      modified on Mon May  3 16:04:42 PDT 1999 by heydon

// Utilities required by all table instantiations

#ifndef _TABLE_UTILS_H
#define _TABLE_UTILS_H

#include <math.h>
#include <Basics.H>

Bit16 Log_2(Bit32 x) throw ();
/* Return "ceiling(log_2(x))". */

#define Ceiling(x) ((Bit32)ceil(x))
#define Round(x) ((Bit32)rint(x))

// The above used to be defined like this:

// inline Bit32 Ceiling(float x) throw () { return (Bit32)ceilf(x); }
// inline Bit32 Round(float x) throw () { return (Bit32)rintf(x); }

// However that causes some compilers to give "multiply defined"
// link-time errors.  This seems to be in violation of the C++ ARM,
// which says:

//    For a nonmember function an inline specifier is equivalent to a
//    static specifier for linkage purposes

#endif // _TABLE_UTILS_H
