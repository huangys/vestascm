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

// Created on Wed Mar 12 15:56:34 PST 1997 by heydon

// Pathname.H -- defines space-efficient storage of a collection
//   of pathnames with common prefixes.

#ifndef _PATHNAME_H
#define _PATHNAME_H

#include <Basics.H>

/* The methods of this interface are unmonitored. */

namespace Pathname
{
    // a Pathname::T is an index into a private table
    typedef int T;

    T New(T prefix, const char *arc) throw ();
    /* Return a new pathname consisting of the concatenation of the pathname
       represented by "prefix" and the text "arc". If this is the first arc
       of a pathname, "prefix" should be negative. In general, "arc" should
       not contain any slashes, except in the case where "prefix" is negative
       and "arc" begins with a slash to denote the start of an absolute
       pathname. */

    void Print(std::ostream &os, T path) throw ();
    /* Print the pathname "path" to the output stream "os". */

    Text Get(T path) throw ();
    /* Convert the pathname "path" to a text string. */
}

#endif // _PATHNAME_H
