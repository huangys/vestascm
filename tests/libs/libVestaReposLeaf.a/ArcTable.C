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
// ArcTable.C
// Last modified on Tue Jul 20 15:15:02 EDT 2004 by ken@xorian.net
//      modified on Thu Jul 19 15:29:00 PDT 2001 by mann
//
// Temporary hash table of arcs.  Representation is a pointer to the
// non-null-terminated characters (not a copy!), plus a copy of the length.
//

#include "ArcTable.H"

Word
ArcKey::Hash() const throw ()
{
    Word v = 0, i;
    for (i = 0; i < slen; i++)
      v += (s[i] & 0xff) << ((i * 8) % (sizeof(Word) * 8 - 1));
    return v;
}

bool
operator==(const ArcKey& k1, const ArcKey& k2) throw ()
{
    return k1.slen == k2.slen && memcmp(k1.s, k2.s, k1.slen) == 0;
}

