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

// Last modified on Mon Nov 10 16:47:33 EST 2003 by ken@xorian.net  
//      modified on Mon Feb 14 18:22:27 PST 2000 by mann  
//      modified on Sat Oct  4 08:02:16 PDT 1997 by heydon

#include <Basics.H>
#include "StatNames.H"

static char *AttrName0[] = {
    // MultiPKFile attributes
    "MultiPKFile size (in bytes)",

    // PKFile attributes
    "PKFile size (in bytes)",
    "Number of free variables per PKFile",
    "Free variable name length",
    "Number of cache entries per PKFile",
    "Number of common free variables per PKFile",
    "Percentage of common free variables per PKFile",

    // Cache entry attributes
    "Number of total free variables per cache entry",
    "Number of uncommon free variables per cache entry",
    "Percentage of uncommon free variables per cache entry",
    "Cache value size (in bytes)",
    "Number of derived indices (DI's) per cache value",
    "Number of child entries (kids) per cache entry",
    "Number of entries in name index map per cache entry",
    "Number of non-empty name index maps per cache entry (0 or 1)",
    "Size of the cached value's PrefixTbl",
    "Number of redundant free variables per cache entry",
    "Percentage redundant free variables per cache entry"
};

char *Stat::AttrName(Stat::Attribute attr) throw ()
{
    assert(0 <= attr && attr < Stat::NumAttributes);
    return AttrName0[(int)attr];
}

static const int DirLevel = 4;

static char *LevelName0[] = {
    "Cache entries",
    "Common finger print (CFP) groups",
    "PKFiles",
    "MultiPKFiles",
    "Directories"
};

char *Stat::LevelName(int level) throw ()
{
    assert(0 <= level);
    return LevelName0[min(level, DirLevel)];
}
