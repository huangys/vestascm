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

#ifndef _BYTEMODTABLE_H
#define _BYTEMODTABLE_H

#include "Poly.H"

// The table for computing raw fingerprints
extern Poly ByteModTable[8][256];

// Function which should be called before accessing ByteModTable to
// ensure that it is initialized.  (It's safe to call this funciton
// multiple times and from multiple threads.)
void ByteModTableInit() throw();

#endif // _BYTEMODTABLE_H
