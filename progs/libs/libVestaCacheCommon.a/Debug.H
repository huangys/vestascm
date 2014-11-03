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

// Debug.H -- common functions for debugging, etc.

#ifndef _DEBUG_H
#define _DEBUG_H

#include <ThreadIO.H>

namespace Debug {
   Text Timestamp() throw ();
  /* Returns a string for the current timestamp of the form "HH:MM:SS.sss",
     where "HH" are hours, "MM" are minutes, "SS" are seconds, and "sss" are
     milliseconds. */

  int MyRand(int lo, int hi) throw ();
  // Return a random integer in the closed interval [lo, hi].

  void BlockForever() throw ();
  // The calling thread never returns; it blocks on a condition
  // variable that is never signaled.
};

#endif // _DEBUG_H
