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
// ArcTable.H
// Last modified on Mon Nov 12 18:37:56 EST 2001 by ken@xorian.net
//      modified on Tue Sep 19 18:34:13 PDT 1995 by mann
//
// Temporary hash table of arcs.  Representation is a pointer to the
// non-null-terminated characters (not a copy!), plus a copy of the length.
//

#ifndef _ARCTAB
#define _ARCTAB 1

#include "Basics.H"
#include "Table.H"

class ArcKey {
  public:
    const char* s;
    unsigned int slen;
    Word Hash() const throw();
    inline ArcKey() { s = NULL; slen = 0; };
    inline ArcKey(const char* s, unsigned int slen)
      { this->s = s; this->slen = slen; };
    friend bool operator==(const ArcKey& k1, const ArcKey& k2) throw();
};

class ArcValue {
  public:
    inline ArcValue() { };
};

typedef Table<ArcKey, ArcValue>::Default ArcTable;

#endif //_ARCTAB
