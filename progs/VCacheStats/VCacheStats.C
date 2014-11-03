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

// Last modified on Fri Apr 29 00:15:35 EDT 2005 by ken@xorian.net  
//      modified on Sat Feb 12 13:10:03 PST 2000 by mann  
//      modified on Sun Aug 23 12:18:35 PDT 1998 by heydon

// VCacheStats -- gather and print statistics about the stable cache

#include <Basics.H>
#include <CacheArgs.H>
#include <CacheConfig.H>
#include "StatError.H"
#include "StatNames.H"
#include "StatCount.H"
#include "StatCollection.H"
#include "StatDirEntry.H"
#include "StatMPKFile.H"

using std::ostream;
using std::cerr;
using std::cout;
using std::endl;

void PrintSyntax(const char *msg, const char *arg = NULL) throw ()
{
    cerr << "Error: " << msg;
    if (arg != NULL) cerr << ": " << arg;
    cerr << endl;
    cerr << "Syntax: VCacheStats "
	<< "[ -histo ] [ -min ] [ -max ] [ -v | -V ] [ path ]" << endl;
    exit(1);
}

static void Banner(ostream &os) throw ()
{
    os << endl;
    for (int i = 0; i < 75; i++) os << '*';
    os << endl << endl;
}

void Process(const char *path, int verbose,
	     bool histo, bool minName, bool maxName,
	     /*INOUT*/Stat::Collection &stats) throw ()
{
    Text fullpath(path);
    if (path[0] != '/') {
	// make path absolute
	if (fullpath.Length() > 0) {
	    fullpath = Config_SCachePath + ('/' + fullpath);
	} else {
	    fullpath = Config_SCachePath;
	}
    }
    cout << "STATISTICS FOR:" << endl << "  " << fullpath << endl;

    try {
	// search "fullpath"
	DirEntry *entry = DirEntry::Open(fullpath);
	if (verbose > 0) cout << endl << "SEARCHING:" << endl;
	entry->Search(verbose, /*INOUT*/ stats);

	// print fanout stats
	cout << endl << "*** FANOUT STATISTICS ***" << endl;
	int i;
	for (i = stats.fanout.size() - 1; i >= 0; i--) {
	    cout << endl;
	    cout << Stat::LevelName(i) << " (level " << i << "):" << endl;
	    StatCount *sc = stats.fanout.get(i);
	    sc->Print(cout, /*indent=*/ 2, histo,
              minName, maxName, /*mean=*/ (i>0));
	}
	
	// print PKFile and cache entry stats
	cout << endl;
	cout << "*** MULTIPKFILE, PKFILE, AND CACHE ENTRY STATISTICS ***";
	cout << endl;
	for (i = 0; i < Stat::NumAttributes; i++) {
	  // If we didn't gather redundant statistics, don't print
	  // them.
	  if(!stats.redundant &&
	     ((i == Stat::NumRedundantNames) ||
	      (i == Stat::PcntRedundantNames)))
	    {
	      continue;
	    }

	    cout << endl << Stat::AttrName((Stat::Attribute)i) << ":" << endl;
	    stats.entryStats[i].Print(cout, /*indent=*/ 2,
				      histo, minName, maxName);
	}
    }
    catch (const StatError::UnevenLevels &dirName) {
	Banner(cerr);
	cerr << "Error: the following directory "
	     << "contains MPKFiles at different depths:" << endl;
	cerr << "  " << dirName << endl;
	Banner(cerr);
    }
    catch (const StatError::BadMPKFile &fname) {
	Banner(cerr);
	cerr << "Error: found illegal MultiPKFile:" << endl;
	cerr << "  " << fname << endl;
	Banner(cerr);
    }
    catch (const StatError::EndOfFile &fname) {
	Banner(cerr);
	cerr << "Fatal error: premature end-of-file" << endl;
        cerr << "  " << fname << endl;
	Banner(cerr);
    }
    catch (const FS::Failure &f) {
	Banner(cerr);
	cerr << "Error: " << f << endl;
	Banner(cerr);
    }
    catch (FS::DoesNotExist) {
	Banner(cerr);
	cerr << "Error: file '" << fullpath << "' does not exist" << endl;
	Banner(cerr);
    }
}


static StatCount *parseStatName(Stat::Collection &stats, const char *stat)
{
#define ATTRIB_CASE(name) \
if(strcmp(#name, stat) == 0) return &(stats.entryStats[Stat::name])
  
  ATTRIB_CASE(MPKFileSize);

  ATTRIB_CASE(PKFileSize);
  ATTRIB_CASE(NumNames);
  ATTRIB_CASE(NameSize);
  ATTRIB_CASE(NumEntries);
  ATTRIB_CASE(NumCommonNames);
  ATTRIB_CASE(PcntCommonNames);

  ATTRIB_CASE(NumEntryNames);
  ATTRIB_CASE(NumUncommonNames);
  ATTRIB_CASE(PcntUncommonNames);
  ATTRIB_CASE(ValueSize);
  ATTRIB_CASE(NumDIs);
  ATTRIB_CASE(NumKids);
  ATTRIB_CASE(NameMapSize);

  // This one is really uninteresting for reporting or masking:
  // ATTRIB_CASE(NameMapNonEmpty);

  ATTRIB_CASE(ValPfxTblSize);
  ATTRIB_CASE(NumRedundantNames);
  ATTRIB_CASE(PcntRedundantNames);

#define FANOUT_CASE(name, level) \
  if(strcmp(name, stat)) return stats.getFanout(level);

  FANOUT_CASE("PKFileFanout", 2);
  FANOUT_CASE("CFPFanout", 1);
  
  // If we make it down here, this is not a valid attribute name.
  PrintSyntax("unrecognized statistic name", stat);

  // (Unreachable, but needed for compilation.)
  return 0;
}

int main(int argc, char *argv[])
{
    // process command-line
    int verbose = 0;
    bool printHisto = false, printMinName = false, printMaxName = false;
 
    // allocate StatCount structures
    Stat::Collection stats;

   int arg;
    for (arg = 1; arg < argc && *argv[arg] == '-'; arg++) {
	if (CacheArgs::StartsWith(argv[arg], "-histo")) {
	    printHisto = true;
	} else if (CacheArgs::StartsWith(argv[arg], "-min", 3)) {
	    printMinName = true;
	} else if (CacheArgs::StartsWith(argv[arg], "-max", 3)) {
	    printMaxName = true;
	} else if (CacheArgs::StartsWith(argv[arg], "-verbose")) {
	    verbose = max(verbose, 1);
	} else if (CacheArgs::StartsWith(argv[arg], "-Verbose")) {
	  verbose = max(verbose, 2);
	} else if (CacheArgs::StartsWith(argv[arg], "-report")) {
	  // Next arg: which stat to report about
	  if(++arg >= argc)
	    PrintSyntax("-report expects stat name");
	  StatCount *stat = parseStatName(stats, argv[arg]);
	  // Next arg: number of functions to report about
	  if(++arg >= argc)
	    PrintSyntax("-report expects count");
	  unsigned int report_count = atoi(argv[arg]);
	  // Set the report count for this stat
	  stat->SetReportCount(report_count);
	} else if (CacheArgs::StartsWith(argv[arg], "-mask")) {
	  // Next arg: which stat to report about
	  if(++arg >= argc)
	    PrintSyntax("-mask expects stat name");
	  StatCount *stat = parseStatName(stats, argv[arg]);
	  // Next arg: regular expression to mask out of the report
	  // for this statistic.
	  if(++arg >= argc)
	    PrintSyntax("-mask expects regular expression");
	  Basics::RegExp *mask = NEW_CONSTR(Basics::RegExp, (argv[arg]));
	  // Add this to the report mask for this stat
	  stat->AddReportMask(mask);
	} else if (CacheArgs::StartsWith(argv[arg], "-redundant")) {
	  // Turn on the expensive redundant dependencies check.
	  stats.redundant = true;
	} else {
	    PrintSyntax("unrecognized command-line option", argv[arg]);
	}
    }

    // process argument
    const char *path = (arg == argc) ? "" : argv[arg];
    Process(path, verbose, printHisto, printMinName, printMaxName, stats);
}
