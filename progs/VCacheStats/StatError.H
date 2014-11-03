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

// Last modified on Thu Nov  8 12:48:54 EST 2001 by ken@xorian.net
//      modified on Fri Aug  8 19:48:23 PDT 1997 by heydon

#ifndef _STAT_ERROR_H
#define _STAT_ERROR_H

#include <Basics.H>

class StatError {
  public:
    // exception class
    class UnevenLevels : public Text {
      public:
	UnevenLevels(const Text &t) throw () : Text(t) { /*SKIP*/ }
    };
    class BadMPKFile : public Text {
      public:
	BadMPKFile(const Text &t) throw () : Text(t) { /*SKIP*/ }
    };
    class EndOfFile : public Text {
      public:
	EndOfFile(const Text &t) throw () : Text(t) { /*SKIP*/ }
    };
};

#endif // _STAT_ERROR_H
