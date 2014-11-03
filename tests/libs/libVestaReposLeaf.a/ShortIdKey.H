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
// ShortIdKey.H
// Last modified on Mon Nov 12 18:37:56 EST 2001 by ken@xorian.net
//      modified on Sat Feb 12 14:35:00 PST 2000 by mann
//

#ifndef _SHORT_ID_KEY
#define _SHORT_ID_KEY 1

#include "SourceOrDerived.H"

class ShortIdKey {
  public:
    ShortId sid;
    Word Hash() const throw() { return (Word) sid; };
    inline ShortIdKey() { sid = NullShortId; };
    inline ShortIdKey(ShortId s) { sid = s; };
    friend int operator==(ShortIdKey k1, ShortIdKey k2) 
      { return k1.sid == k2.sid; };
};

#endif
