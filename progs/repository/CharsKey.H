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

//
// CharsKey.H
// Last modified on Mon Nov 12 18:37:56 EST 2001 by ken@xorian.net
//      modified on Thu Sep 14 14:38:15 PDT 2000 by mann
//

#include <Basics.H>

class CharsKey {
  public:
    const char* s;
    Word Hash() const throw();
    inline CharsKey() { this->s = NULL; };
    inline CharsKey(const char* s) { this->s = s; };
    friend bool operator==(const CharsKey& k1, const CharsKey& k2) throw();
};
