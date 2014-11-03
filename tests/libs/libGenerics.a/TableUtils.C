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
//      modified on Mon May  3 16:04:42 PDT 1999 by heydon

#include <Basics.H>
#include "TableUtils.H"

const Bit32 One32U = 1U;

Bit16 Log_2(Bit32 x) throw ()
/* Return "ceiling(log_2(x))". */
{
    assert(x > 0U);
    Bit16 log = 0;
    for (Bit32 n = One32U; n < x; n <<= 1) log++;
    return log;
}
