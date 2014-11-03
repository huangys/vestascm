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

// CopyShortId.H

// Make a copy of a shortid (for copy-on-write, and when making a file
// immutable that has multiple hard links)

#ifndef COPYSHORTID_H
#define COPYSHORTID_H

#include <Basics.H>
#include "SourceOrDerived.H"

extern ShortId CopyShortId(ShortId oldsid, /*OUT*/ int &errno_out,
			   Basics::uint64 n= ((Basics::uint64)-1),
			   Basics::uint64 *copied = 0,
			   bool writable = true);

#endif
