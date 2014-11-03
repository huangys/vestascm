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

#include "StatCollection.H"

static char *Log2Name = "ceiling(log_2(x))";
static Basics::int64 Log2(Basics::int64 x) throw ()
{
    assert(x >= 0);
    Basics::int64 res = 0;
    for (Basics::uint64 n = 1U; n < x; n <<= 1) res++;
    return res;
}

static char *Div2Name = "ceiling(x/2)";
static char *Div5Name = "ceiling(x/5)";
static char *Div10Name = "ceiling(x/10)";
static Basics::int64 Div2(Basics::int64 x) throw ()  { return (x+1) >> 1; }
static Basics::int64 Div5(Basics::int64 x) throw ()  { return (x+4) / 5; }
static Basics::int64 Div10(Basics::int64 x) throw () { return (x+9) / 10; }

Stat::Collection::Collection(bool redundant)
  : fanout(/*sizehint=*/ 10), redundant(redundant)
{
  // override histogram maps as appropriate
  entryStats[Stat::MPKFileSize].SetHistoMap(Log2Name, Log2);
  entryStats[Stat::PKFileSize].SetHistoMap(Log2Name, Log2);
  entryStats[Stat::NumNames].SetHistoMap(Div10Name, Div10);
  entryStats[Stat::NameSize].SetHistoMap(Div2Name, Div2);
  entryStats[Stat::NumCommonNames].SetHistoMap(Div10Name, Div10);
  entryStats[Stat::PcntCommonNames].SetHistoMap(Div5Name, Div5);
  entryStats[Stat::NumEntryNames].SetHistoMap(Div10Name, Div10);
  entryStats[Stat::NumUncommonNames].SetHistoMap(Div10Name, Div10);
  entryStats[Stat::PcntUncommonNames].SetHistoMap(Div5Name, Div5);
  entryStats[Stat::ValueSize].SetHistoMap(Log2Name, Log2);
  entryStats[Stat::NumDIs].SetHistoMap(Div5Name, Div5);
  entryStats[Stat::NameMapSize].SetHistoMap(Div10Name, Div10);
  entryStats[Stat::ValPfxTblSize].SetHistoMap(Div10Name, Div10);
  entryStats[Stat::NumRedundantNames].SetHistoMap(Div10Name, Div10);
  entryStats[Stat::PcntRedundantNames].SetHistoMap(Div5Name, Div5);

  // Set the "trouble" end of some statistics to "low".
  entryStats[Stat::PcntCommonNames].SetTroubleEnd(StatCount::troubleLow);
}

StatCount *Stat::Collection::getFanout(int level)
{
  // Make sure that the StatCount object for this level exists
  while(this->fanout.size() <= level)
    this->fanout.addhi(NEW(StatCount));

  // Return the StatCount object for this level
  return this->fanout.get(level);
}
