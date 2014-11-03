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

#include <Basics.H>
#include <FP.H>

#include "CacheEntry.H"
#include "StatNames.H"
#include "StatCFP.H"

static unsigned int count_redundant_names(const FV::List *allNames,
					  const BitVector *entryNames)
{
  BitVector redundantNames;

  BVIter nameIter1(*entryNames);
  unsigned int nameIndex1;
  while(nameIter1.Next(nameIndex1))
    {
      // If we've already marked this name as redundant, we don't need
      // to check for whether it makes anything else redundant.
      if(redundantNames.Read(nameIndex1))
	continue;

      const Text *name1 = &(allNames->name[nameIndex1]);
      if((*name1)[0] == 'N')
	{
	  Text path(name1->Sub(2));
	  BVIter nameIter2(*entryNames);
	  unsigned int nameIndex2;
	  while(nameIter2.Next(nameIndex2))
	    {
	      // Don't compare against ourselves
	      if(nameIndex2 == nameIndex1)
		continue;

	      // If we've already marked this name as redundant, we
	      // don't need to check for whether it's redundant.
	      if(redundantNames.Read(nameIndex2))
		continue;

	      const Text *name2 = &(allNames->name[nameIndex2]);

	      // Skip anything that doesn't match the path.
	      if(name2->Sub(2, path.Length()) != path)
		continue;

	      if(name2->Length() > (path.Length()+2))
		{
		  // If the next character after the path isn't a path
		  // separator, this isn't a sub-element of the first
		  // name.
		  if((*name2)[path.Length()+2] != '/')
		    continue;

		  // Otherwise, this is a sub-element of the first
		  // name, and therefore redundant.
		  redundantNames.Set(nameIndex2);
		}
	      else
		{
		  // This is a dependency for the same path.  If it's
		  // a type (T), list length (L), binding names (B),
		  // or expression (E) dependency, then it's
		  // redundant.
		  char type = (*name2)[0];
		  if((type == 'T') || (type == 'L') || (type == 'B') || (type == 'E'))
		    {
		      redundantNames.Set(nameIndex2);
		    }
		}
	    }
	}
    }

  // Return the total number of redundant names we found.
  return redundantNames.Cardinality();
}

int CFPObj::Search(int verbose,
		   /*INOUT*/ Stat::Collection &stats)
  const throw ()
{
    // set "sc" to be StatCount for level 0
    StatCount *sc;
    if (stats.fanout.size() == 0) {
      sc = NEW(StatCount);
      stats.fanout.addhi(sc);
    } else {
	sc = stats.fanout.get(0);
    }

    // iterate over children
    int cnt;
    {
      CE::T *ce = (CE::T *) 0;
      CFPIter it(this);
      for (cnt = 0; it.Next(/*OUT*/ ce); cnt++) {
	// Create a location for this entry
	Stat::Location *entry_loc = NEW_CONSTR(Stat::Location,
					       (this->loc->add_ci(ce->CI())));
	// gather stats on "ce"
	int totalNames = ce->FPs()->len;
	stats.entryStats[Stat::NumEntryNames].AddVal(totalNames, entry_loc);
	if (totalNames > 0) {
	  int numUncommonNames = ce->UncommonNames()->Cardinality();
	  stats.entryStats[Stat::NumUncommonNames].AddVal(numUncommonNames, entry_loc);
	  float totalF = (float)totalNames;
	  float uncommonF = (float)numUncommonNames;
	  int pcntUncommon = (int)(0.49 + (100.0 * uncommonF / totalF));
	  stats.entryStats[Stat::PcntUncommonNames].AddVal(pcntUncommon, entry_loc);

	  // Counting redundant dependencies is expensive, so we only
	  // do it if it's been specifically requested.
	  if(stats.redundant)
	    {
	      BitVector entryNames(this->pkFile->CommonNames());
	      entryNames |= *(ce->UncommonNames());

	      unsigned int redundantNames =
		count_redundant_names(this->pkFile->AllNames(), &entryNames);

	      stats.entryStats[Stat::NumRedundantNames].AddVal(redundantNames,
							       entry_loc);

	      int pcntRedundant = (int)(0.49 +
					(100.0 * ((float)redundantNames) /
					 totalF));
	  
	      stats.entryStats[Stat::PcntRedundantNames].AddVal(pcntRedundant,
								entry_loc);
	    }
	}
	const VestaVal::T *value = ce->Value();
	stats.entryStats[Stat::ValueSize].AddVal(value->len, entry_loc);
	stats.entryStats[Stat::NumDIs].AddVal(value->dis.len, entry_loc);
	stats.entryStats[Stat::NumKids].AddVal(ce->Kids()->len, entry_loc);
	const IntIntTblLR *imap = ce->IMap();
	int imapSz = (imap == NULL) ? 0 : imap->Size();
	stats.entryStats[Stat::NameMapSize].AddVal(imapSz, entry_loc);
	int imapNonEmpty = (imap == NULL) ? 0 : 1;
	stats.entryStats[Stat::NameMapNonEmpty].AddVal(imapNonEmpty, entry_loc);
	stats.entryStats[Stat::ValPfxTblSize].AddVal(value->prefixTbl.NumArcs(),
						     entry_loc);


	// update "sc"
	sc->AddVal(1, 0);
      }
      ce = (CE::T *) 0; // drop on floor for GC;
    }

    // update fan-out for this level
    int thisLevel = 1;
    if (stats.fanout.size() <= thisLevel) {
	assert(stats.fanout.size() == thisLevel);
	sc = NEW(StatCount);
	stats.fanout.addhi(sc);
    } else {
	sc = stats.fanout.get(thisLevel);
    }
    sc->AddVal(cnt, this->loc);
    return thisLevel;
}

bool CFPIter::Next(/*OUT*/ CE::T* &ce) throw ()
{
    bool res = (this->next != (CE::List *)NULL);
    if (res) {
	ce = this->next->Head();
	this->next = this->next->Tail();
    }
    return res;
}
