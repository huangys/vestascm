// Copyright (C) 2006, Kenneth C. Schalk
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

#include <Table.H>
#include "ShortIdKey.H"
#include "VMemPool.H"
#include "DebugDumpHelp.H"

// The next index to be given out for each VMemPool::typeCode

static unsigned int next_block_num[16] = {
  1, 1, 1, 1,
  1, 1, 1, 1,
  1, 1, 1, 1,
  1, 1, 1, 1
};

// Shortid, short pointer... same thing, right?  Close enough for our
// purposes anyway.

static Table<ShortIdKey, unsigned int>::Default block_nums;

void PrintBlockID(std::ostream &dump, Bit32 spointer)
{
  assert(spointer != 0);
  VMemPool::typeCode t = VMemPool::type(VMemPool::lengthenPointer(spointer));

  ShortIdKey k(spointer);
  unsigned int block_num;
  if(!block_nums.Get(k, block_num))
    {
      block_num = next_block_num[t]++;
      block_nums.Put(k, block_num);
    }

  switch(t)
    {
    case VMemPool::vDirChangeable:
      dump << "vDirChangeable";
      break;
    case VMemPool::vForward:
      dump << "vForward";
      break;
    case VMemPool::vDirImmutable:
      dump << "vDirImmutable";
      break;
    case VMemPool::vAttrib:
      dump << "vAttrib";
      break;
    case VMemPool::vDirAppendable:
      dump << "vDirAppendable";
      break;
    default:
      dump << "<unexpected VMemPool type!>";
      break;
    }
  dump << " #" << block_num;
}
