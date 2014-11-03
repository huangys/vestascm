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

#ifndef _UNITS_H
#define _UNITS_H

#include "Basics.H"

namespace Basics
{
  // Format a possibly large value with a unit multiplier to make it
  // more readable.  If "decimal" is true, base-10 units are used,
  // otherwise base-2 units are used

  // base-10  base-2  unit suffix
  // -------  ------  -----------
  // 10^3             k
  //          2^10    K
  // 10^6     2^20    M
  // 10^9     2^30    G
  // 10^12    2^40    T
  // 10^15    2^50    P
  // 10^18    2^60    E

  // The lowercase k is used for deciaml by convention (as K also
  // means degress Kelvin).
  Text FormatUnitVal(Basics::uint64 val, bool decimal = false) throw ();

  // Exception thrown if parsing a value with units fails
  struct ParseUnitValFailure
  {
    // The input value
    Text val;
    // A message about what went wrong
    Text emsg;
  };

  // Parse a value with a unit multiplier.  This must be of the form
  // [0-9]+[kKmMgGtTpPeE].  Anything else will result in
  // ParseUnitValFailure being thrown.
  Basics::uint64 ParseUnitVal(const char *val, bool decimal = false)
    throw (ParseUnitValFailure);
  inline Basics::uint64 ParseUnitVal(const Text &val, bool decimal = false)
    throw (ParseUnitValFailure)
  {
    return ParseUnitVal(val.cchars(), decimal);
  }
}

#endif
