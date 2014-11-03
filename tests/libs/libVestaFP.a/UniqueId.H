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

// Last modified on Fri Nov  9 14:11:52 EST 2001 by ken@xorian.net
//      modified on Fri Oct 18 00:19:14 PDT 1996 by heydon

// UniqueId.H -- constructs fingerprints that are (probabilistically) unique

#ifndef _UNIQUE_ID_H
#define _UNIQUE_ID_H

#include <Basics.H>
#include "FP.H"

/* The method(s) in this interface are unmonitored. */

FP::Tag UniqueId() throw ();
/* Return a new fingerprint that is (probabilistically)
   unique (across all sites, machines, processes, and time). */

#endif // _UNIQUE_ID_H
