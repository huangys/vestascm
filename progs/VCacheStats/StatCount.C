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
#include <Table.H>
#include <Generics.H>
#include "StatCount.H"

using std::ostream;
using std::endl;

static Basics::int64 IdentityMap(Basics::int64 x) throw ()
{ return x; }

static bool RESeqMatch(const char *string, RegExpSeq &res)
{
  for(unsigned int i = 0; i < res.size(); i++)
    {
      Basics::RegExp *re = res.get(i);
      assert(re != 0);
      if(re->match(string))
	return true;
    }
  return false;
}

StatCount::StatCount() throw ()
  : numVals(0), total(0), tbl(/*sizeHint=*/ 0), histoMapName(NULL),
    minLoc(0), maxLoc(0),
    troubleEnd(troubleHigh),
    reportCount(0), reportCount_set(false), troubleSet()
{
    this->histoMap = IdentityMap;
}

void StatCount::SetHistoMap(const char *name, Basics::int64 (*map)(Basics::int64)) throw ()
{
    this->histoMapName = name;
    this->histoMap = map;
}

void StatCount::AddTroubleEntry(Basics::int64 val, const Stat::Location *loc)
{
  // Find the place in the troubleSet to insert this entry
  // (possibly at the end)
  troubleSet_t::iterator placement = this->troubleSet.begin();
  while((placement != this->troubleSet.end()) &&
	this->MoreTrouble((*placement).val, val))
    placement++;

  // Insert this trouble location.
  this->troubleSet.insert(placement, toubleLoc_t(val, loc));

  // Increment the count for this sourceFunc.
  int souceFuncCount = 0;
  (void) this->troubleCounts.Get(loc->get_sourceFunc(),
				 /*OUT*/ souceFuncCount);
  this->troubleCounts.Put(loc->get_sourceFunc(),
			  ++souceFuncCount);
}

void StatCount::PruneTroubleEntries()
{
  // Remove some "trouble" entries if we need to.
  while((this->troubleSet.size() > 0) &&
	(this->troubleCounts.Size() > this->reportCount))
    {
      // We always remove the one at the back of the list.
      toubleLoc_t removed = this->troubleSet.back();
      assert(removed.loc != 0);
      this->troubleSet.pop_back();

      // Update the count of trouble entries for this
      // sourceFunc in the table.
      int souceFuncCount;
      bool inTbl =
	this->troubleCounts.Get(removed.loc->get_sourceFunc(),
				/*OUT*/ souceFuncCount);
      assert(inTbl);
      souceFuncCount--;
      if(souceFuncCount > 0)
	{
	  this->troubleCounts.Put(removed.loc->get_sourceFunc(),
				  souceFuncCount);
	}
      else
	{
	  this->troubleCounts.Delete(removed.loc->get_sourceFunc(),
				     /*OUT*/ souceFuncCount);
	}
    }
}

void StatCount::AddVal(Basics::int64 val, const Stat::Location *loc) throw ()
{
    if (this->numVals == 0) {
	this->minVal = this->maxVal = val;
	this->minLoc = this->maxLoc = loc;
    } else {
	if (val < this->minVal) {
	    this->minVal = val;
	    this->minLoc = loc;
	} else if (val > this->maxVal) {
	    this->maxVal = val;
	    this->maxLoc = loc;
	}
    }
    this->total += val;
    this->numVals++;
    Int64Key key((*(this->histoMap))(val));
    Basics::int64 cnt;
    if (this->tbl.Get(key, /*OUT*/ cnt)) {
	// already an entry in histogram table
	this->tbl.Put(key, cnt+1);
    } else {
	// create a new entry in histogram table
	this->tbl.Put(key, 1);
    }

    // If we're recording information about "troublesome" locations.
    if(this->reportCount_set)
      {
	// Skip any function matching one of our trouble masks.
	if(!RESeqMatch(loc->get_sourceFunc().cchars(), this->troubleMasks))
	{
	  // We'll record this new value in the trouble set if either:

	  // 1. We haven't met out minimum number of "trouble
	  // functions" to report on.

	  // 2. val is greater than the smallest value in the trouble
	  // set.
	  if((this->troubleCounts.Size() < this->reportCount) ||
	     ((this->troubleSet.size() > 0) &&
	      this->MoreTrouble(val, this->troubleSet.back().val)))
	    {
	      // Add an entry for this value.
	      AddTroubleEntry(val, loc);

	      // Prune if needed.
	      PruneTroubleEntries();
	    }
	}
      }
}

void StatCount::Merge(const StatCount &sc) throw ()
{
    if (this->numVals == 0) {
	this->minVal = sc.minVal; this->minLoc = sc.minLoc;
	this->maxVal = sc.maxVal; this->maxLoc = sc.maxLoc;
    } else {
	if (sc.minVal < this->minVal) {
	    this->minVal = sc.minVal;
	    this->minLoc = sc.minLoc;
	}
	if (sc.maxVal > this->maxVal) {
	    this->maxVal = sc.maxVal;
	    this->maxLoc = sc.maxLoc;
	}
    }
    this->total += sc.total;
    this->numVals += sc.numVals;
    Int64Int64Iter it(&(sc.tbl));
    Int64Key key; Basics::int64 cnt, scCnt;
    while (it.Next(/*OUT*/ key, /*OUT*/ scCnt)) {
	assert(scCnt != 0);
	if (this->tbl.Get(key, /*OUT*/ cnt)) {
	    // already an entry in histogram table
	    this->tbl.Put(key, cnt + scCnt);
	} else {
	    // create a new entry in histogram table
	    this->tbl.Put(key, scCnt);
	}
    }

    // If we're keeping tracke of "troublesome" functions...
    if(this->reportCount_set)
      {
	// Loop over the touble set of the other StatCount
	for(troubleSet_t::const_iterator it = sc.troubleSet.begin();
	    it != sc.troubleSet.end();
	    it++)
	  {
	    // Skip any function matching one of our trouble masks.
	    if(!RESeqMatch((*it).loc->get_sourceFunc().cchars(),
			   this->troubleMasks))
	      continue;

	    // If we haven't met our keep limit yet or this entry is
	    // more trouble than our least troublesome, add it to our
	    // troubleSet.
	    if((this->troubleCounts.Size() < this->reportCount) ||
	       ((this->troubleSet.size() > 0) &&
		this->MoreTrouble((*it).val, this->troubleSet.back().val)))
	      {
		AddTroubleEntry((*it).val, (*it).loc);
	      }
	  }

	// Prune after adding all entries.
	PruneTroubleEntries();
      }
}

Basics::int64 StatCount::MinVal() const throw ()
{
    if (this->numVals == 0) assert(false);
    return this->minVal;
}

Basics::int64 StatCount::MaxVal() const throw ()
{
    if (this->numVals == 0) assert(false);
    return this->maxVal;
}

float StatCount::MeanVal() const throw ()
{
    if (this->numVals == 0) assert(false);
    return (float)(this->total) / (float)(this->numVals);
}

static void SC_Indent(ostream &os, int i) throw ()
{
    for (; i > 0; i--) os << ' ';
}

void StatCount::Print(ostream &os, int indent, bool printHisto,
  bool printMinLoc, bool printMaxLoc, bool printMean) const throw ()
{
    SC_Indent(os, indent);
    if (this->numVals == 0) {
	os << "number = 0" << endl;
	return;
    } else if (!printMean && this->minVal == this->maxVal
               && this->numVals == this->total) {
	// in this case, all the values were 1, so only print the number
	os << "number = " << this->numVals << endl;
	return;
    } else {
	os << "number = " << this->numVals << endl;
	SC_Indent(os, indent);
	os << "min = " << this->minVal << ", mean = " <<
	    this->MeanVal() << ", max = " << this->maxVal << endl;
	bool printBoth =
	    (printMinLoc && printMaxLoc && (this->minVal == this->maxVal));
	if (printMinLoc && (this->minLoc != 0)) {
	    SC_Indent(os, indent);
	    os << (printBoth ? "Min/max" : "Min");
	    os << " value in: " << endl;
	    SC_Indent(os, indent+2);
	    this->minLoc->print(os, indent+2);
	    os << endl;
	}
	if (!printBoth && printMaxLoc && (this->maxLoc != 0)) {
	    SC_Indent(os, indent);
	    os << "Max value in: " << endl;
	    SC_Indent(os, indent+2);
	    this->maxLoc->print(os, indent+2);
	    os << endl;
	}
    }
    if (printHisto) PrintHisto(os, indent);
    if (this->reportCount_set) PrintTrouble(os, indent);
}

bool StatCount::MoreTrouble(Basics::int64 val1, Basics::int64 val2)
{
  if(this->troubleEnd == troubleHigh)
    {
      return (val1 > val2);
    }
  else
    {
      return (val1 < val2);
    }
}

extern "C"
{
  static int IntCompare(const void *v1, const void *v2) throw ()
  {
    int *i1 = (int *)v1, *i2 = (int *)v2;
    return *i1 - *i2;
  }
}

void StatCount::PrintHisto(ostream &os, int indent) const throw ()
{
    // allocate array of keys
    int tblSz = this->tbl.Size();
    Basics::int64 *keys = NEW_PTRFREE_ARRAY(Basics::int64, tblSz);
    
    // iterate over table, filling keys array
    Int64Int64Iter it(&(this->tbl));
    Int64Key key; Basics::int64 val, i = 0;
    while (it.Next(/*OUT*/ key, /*OUT*/ val)) {
	keys[i++] = key.Val();
    }
    assert(i == tblSz);
    
    // sort the keys
    qsort(keys, tblSz, sizeof(*keys), IntCompare);
    
    // print the histogram in increasing key order
    int lineCnt = 0;
    if (this->histoMapName != NULL) {
	SC_Indent(os, indent);
	os << "Histogram mapping function: " << this->histoMapName << endl;
    }
    SC_Indent(os, indent); os << "{ ";
    for (i = 0; i < tblSz; i++) {
	Int64Key key(keys[i]);
	bool inTbl = this->tbl.Get(key, /*OUT*/ val); assert(inTbl);
	if (i > 0) os << ", ";
	if (lineCnt >= 6) {
	    os << endl; SC_Indent(os, indent); os << "  ";
	    lineCnt = 0;
	}
	os << keys[i] << " -> " << val;
	lineCnt++;
    }
    os << " }" << endl;
}

void StatCount::PrintTrouble(ostream &os, int indent) const throw ()
{
  // Print out just the sourceFuncs of the most troublesome functions,
  // in order of decreasing max value.
  SC_Indent(os, indent);
  os << ((this->troubleEnd == troubleHigh) ? "High " : "Low ")
     << this->reportCount << " functions:" << endl;

  TextIntTbl not_yet_printed(&this->troubleCounts, true);
  for(troubleSet_t::const_iterator it = this->troubleSet.begin();
      it != this->troubleSet.end();
      it++)
    {
      const Text &sourceFunc = (*it).loc->get_sourceFunc();
      int unused;
      if(not_yet_printed.Delete(sourceFunc, /*OUT*/ unused))
	{
	  SC_Indent(os, indent+2);
	  os << sourceFunc << endl;
	}
    }
  
  // Loop over the touble set, printing each entry.
  SC_Indent(os, indent);
  os << "Detailed "
     << ((this->troubleEnd == troubleHigh) ? "high" : "low")
     << " cases:" << endl;

  for(troubleSet_t::const_iterator it = this->troubleSet.begin();
      it != this->troubleSet.end();
      it++)
    {
      SC_Indent(os, indent+2);
      os << "value = " << (*it).val << endl;
      SC_Indent(os, indent+2);
      (*it).loc->print(os, indent+2);
      os << endl << endl;
    }
}

