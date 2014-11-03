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

// Last modified on Fri Nov 14 14:50:08 EST 2003 by ken@xorian.net
//      modified on Sat Oct  4 08:14:40 PDT 1997 by heydon

/* StatNames.H -- Names and meanings of levels and statistical values
   computed by the VCacheStats program. */

#ifndef _STAT_NAMES_H
#define _STAT_NAMES_H

namespace Stat
{
    enum Attribute {
	// MultiPKFile attributes
	MPKFileSize,       // size of MultiPKFile (in bytes)

	// PKFile attributes
	PKFileSize,	   // size of PKFile (in bytes)
	NumNames,          // number of free variables
	NameSize,          // length of free variable names
        NumEntries,	   // number of cache entries in this PKFile
        NumCommonNames,    // number of common free variable names
        PcntCommonNames,   // percentage of common free variable names

	// Cache entry attributes
        NumEntryNames,     // number of total free variable names
        NumUncommonNames,  // number of uncommon free variable names
        PcntUncommonNames, // percentage of uncommon free variable names
	ValueSize,         // size of the result value (in bytes)
	NumDIs,            // number of DI's in the result value
	NumKids,	   // number of child cache entries
	NameMapSize,	   // number of entries in name index map (imap)
        NameMapNonEmpty,   // number of name index maps that are non-empty
	ValPfxTblSize,     // size of an entry's value's PrefixTbl

	NumRedundantNames, // count of redundant dependencies
	PcntRedundantNames,// percentage of redundant dependencies

	// Total number of attributes
	NumAttributes
    };

    char *AttrName(Stat::Attribute attr) throw ();
    /* Return the printable name of attribute "attr". */

    char *LevelName(int level) throw ();
    /* Returns the name of the type of entities at level "level". */
}

#endif // _STAT_NAMES_H
