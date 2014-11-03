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

// Last modified on Mon Nov 10 17:55:19 EST 2003 by ken@xorian.net

// StatLocation.H -- A class representing a collection of statistics
// about the cache.

#ifndef _STAT_COLLECTION_H
#define _STAT_COLLECTION_H

#include <Basics.H>
#include "StatNames.H"
#include "StatCount.H"

#include <Sequence.H>

// a sequence of pointers to StatCount objects
typedef Sequence<StatCount*> StatCountSeq;

namespace Stat
{
  struct Collection
  {
    StatCount entryStats[Stat::NumAttributes];
    StatCountSeq fanout;
    bool redundant;
  
    Collection(bool redundant = false);

    StatCount *getFanout(int level);
  };
}

#endif // _STAT_COLLECTION_H
