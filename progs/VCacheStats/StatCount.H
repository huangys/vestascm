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


#ifndef _STAT_COUNT_H
#define _STAT_COUNT_H

/* A "StatCount" is a statistical container for a set of integers.
   There is a method for adding a number to the container, and for
   retrieving various statistical properties of the numbers, such
   as their minimum, maximum, and mean. There is also a method for
   retrieving a histogram of the numbers classified according to
   an overridable mapping function.

   The methods of a "StatCount" are unmonitored. */

#include <Basics.H>
#include <BasicsAllocator.H>
#include <Table.H>
#include <Generics.H>
#include <list>
#include <RegExp.H>
#include <SimpleKey.H>
#include "StatNames.H"
#include "StatLocation.H"

#include <Sequence.H>

// a sequence of pointers to RegExp objects
typedef Sequence<Basics::RegExp*> RegExpSeq;

// A table from 64-bit integers to 64-bit integers
typedef SimpleKey<Basics::int64> Int64Key;
typedef Table<Int64Key,Basics::int64>::Default Int64Int64Tbl;
typedef Table<Int64Key,Basics::int64>::Iterator Int64Int64Iter;

class StatCount {
public:
  StatCount() throw ();
  /* Initialize to count of zero items. */

  void SetHistoMap(const char *name, Basics::int64 (*map)(Basics::int64)) throw ();
  /* A StatCount maintains a histogram of the values placed in
     it via the "AddVal" method. By default, the histogram is
     of the values themselves. However, if the values are sparse,
     it may be more meaningful to keep a histogram of some mapping
     of the values. This function sets the mapping function to
     "map"; the name "name" will be printed as part of printing
     the histogram. */

  void SetReportCount(unsigned int reportCount)
  { this->reportCount = reportCount; this->reportCount_set = true; }
  /* If desired, while recording the total, min, max, and histogram
     of this statistic, a certain number of "troublesome" functions
     (sourceFuncs) can be saved and listed after statistics
     gathering.  This can be helpful in trying to track down
     functions that need attention. */

  enum troubleEnd_t { troubleHigh, troubleLow };
  void SetTroubleEnd(troubleEnd_t end)
  { this->troubleEnd = end; }
  /* The "trouble end" of a statistic determines whether we report
     about functions at the high or low end of the scale. */

  void AddReportMask(Basics::RegExp *mask)
  { this->troubleMasks.addhi(mask); }
  /* Keep any functions matching the regular expression passed from
     being included in the report of "troublesome" functions. */

  void AddVal(Basics::int64 val, const Stat::Location *loc) throw ();
  /* Add "val" to the set of numbers in this container. If this value
     is a new minimum and/or maximum, "loc" is recorded as the associated
     location that produced the value. */

  void Merge(const StatCount &sc) throw ();
  /* Merge the values in "sc" into this container. */

  bool IsEmpty() const throw () { return this->numVals == 0; }
  /* Return "true" iff the container does not contain any values. */

  Basics::int64 MinVal() const throw ();
  Basics::int64 MaxVal() const throw ();
  float MeanVal() const throw ();
  /* Return the minimum, maximum, and mean of the numbers added
     to the container so far. These methods are all checked
     run-time errors if "IsEmpty()". */

  Basics::int64 SumTotal() const throw () { return this->total; }
  /* Return the sum total of all the values added to this container
     so far. */

  void Print(std::ostream &os, int indent, bool printHisto, bool printMinLoc,
	     bool printMaxLoc, bool printMean = true) const throw ();
  /* Print the stats for this container to "os", indented by
     "indent" spaces on each line. If "printHisto" is true, the
     histogram is printed. If "printMinLoc"/"printMaxLoc" is true,
     the location associated with the minimum/maximum value is
     printed. If "printMean" is false and all the values added to
     this StatCount were 1, only the number of values added is
     printed; otherwise, the minimum, mean, and maximum values are
     printed, as well as any information requested by the
     "printHisto", "printMinLoc", and "printMaxLoc" arguments.
     Finally, if this statistic has a non-zero "reportCount",
     PrintTrouble is called. */

  void PrintHisto(std::ostream &os, int indent) const throw ();
  /* Print the container's histogram, with each line indented
     by "indent" spaces. */

  void PrintTrouble(std::ostream &os, int indent) const throw ();
  /* Print out information about "troublesome" functions. */

private:
  // data fields
  int numVals;
  Basics::int64 minVal;
  Basics::int64 maxVal;
  Basics::int64 total;
  Int64Int64Tbl tbl;
  const Stat::Location *minLoc, *maxLoc;

  // histogram map
  const char *histoMapName;
  Basics::int64 (*histoMap)(Basics::int64);

  // Parameters controlling the search for "troublesome" areas.
  troubleEnd_t troubleEnd;
  unsigned int reportCount;
  bool reportCount_set;

  // Data structures used to store information about "troublesome"
  // entries.
  struct toubleLoc_t
  {
    Basics::int64 val;
    const Stat::Location *loc;
    toubleLoc_t() : val(-1), loc(0) { }
    toubleLoc_t(Basics::int64 val, const Stat::Location *loc)
      : val(val), loc(loc) { }
    toubleLoc_t(const toubleLoc_t &other)
      : val(other.val), loc(other.loc) { }
    toubleLoc_t &operator=(const toubleLoc_t &other)
    {	val = other.val; loc = other.loc; return *this; }
    bool operator==(const toubleLoc_t &other)
    { return (val == other.val) && (loc == other.loc); }
    bool operator<(const toubleLoc_t &other)
    { return (val < other.val); }
  };
  typedef std::list<toubleLoc_t,
		    Basics::Allocator<toubleLoc_t> > troubleSet_t;
  troubleSet_t troubleSet;
  TextIntTbl troubleCounts;

  // Regular espressions used to exclude some functions from the
  // trouble reporting.
  RegExpSeq troubleMasks;

  // hide the copy constructor from clients
  StatCount(const StatCount &);

  bool MoreTrouble(Basics::int64 val1, Basics::int64 val2);

  // Insert a new entry into our troubleSet.
  void AddTroubleEntry(Basics::int64 val, const Stat::Location *loc);

  // Prune entries from our troubleSet.
  void PruneTroubleEntries();
};

#endif // _STAT_COUNT_H
