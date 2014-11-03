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
#include "CacheEntry.H"
#include "SPKFileRep.H"
#include "SPKFile.H"
#include "StatNames.H"
#include "StatCFP.H"
#include "StatPKFile.H"

int PKFileObj::Search(int verbose,
		      /*INOUT*/ Stat::Collection &stats)
  const throw ()
{
    // save total number of entries before searching this PKFile
    int childLevel = 1;
    long int preNumEntries = (stats.fanout.size() > childLevel)
      ? (stats.fanout.get(childLevel))->SumTotal() : 0L;

    // iterate over children
    int cnt;
    {
      CFPObj *cfp = (CFPObj *) 0;
      PKFileIter it(this);
      for (cnt = 0; it.Next(/*OUT*/ cfp); cnt++) {
	(void) cfp->Search(verbose, /*INOUT*/stats);
      }
      cfp = (CFPObj *) 0; // drop on floor for GC
    }

    // update "stats.entryStats" fields for this PKFile
    stats.entryStats[Stat::PKFileSize].AddVal(this->len, this->loc);
    const FV::ListApp *allNames = this->pkFile->AllNames();
    stats.entryStats[Stat::NumNames].AddVal(allNames->len, this->loc);
    for (int i = 0; i < allNames->len; i++) {
	int nameLen = (allNames->name[i]).Length();
	stats.entryStats[Stat::NameSize].AddVal(nameLen, this->loc);
    }
    long int postNumEntries = (stats.fanout.get(childLevel))->SumTotal();
    int numEntries = (int)(postNumEntries - preNumEntries);
    stats.entryStats[Stat::NumEntries].AddVal(numEntries, this->loc);
    if (allNames->len > 0) {
        unsigned int numCommonNms = this->pkFile->CommonNames()->Cardinality();
	stats.entryStats[Stat::NumCommonNames].AddVal(numCommonNms, this->loc);
        float totalF = (float)(allNames->len);
        float commonF = (float)numCommonNms;
        int pcntCommon = (int)(0.49 + (100.0 * commonF / totalF));
	stats.entryStats[Stat::PcntCommonNames].AddVal(pcntCommon, this->loc);
    }

    // update fan-out for this level
    int thisLevel = childLevel + 1;
    StatCount *sc;
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

PKFileIter::PKFileIter(const PKFileObj *pkf) throw ()
  : pkf(pkf), pkHdr(pkf->pkHdr), cfpIndex(0)
{
    this->cfpEntryMap = pkf->pkFile->OldEntries();
}

bool PKFileIter::Next(/*OUT*/ CFPObj* &cfp) throw ()
{
    bool res = (this->cfpIndex < this->pkHdr->num);
    if (res) {
	SPKFileRep::HeaderEntry *he = &(this->pkHdr->entry[this->cfpIndex++]);
	CE::List *entries;
	bool inTbl = this->cfpEntryMap->Get(he->cfp, /*OUT*/ entries);
	assert(inTbl);
	Stat::Location *cfp_loc =
	  NEW_CONSTR(Stat::Location,
		     (pkf->loc->add_cfp(pkf->pkFile->NamesEpoch(),
					he->cfp)));
	cfp = NEW_CONSTR(CFPObj, (cfp_loc, pkf->pkFile, he->cfp, entries));
    }
    return res;
}
