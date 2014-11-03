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
// License along w ith Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

//
// vmeasure2.C
//

// Measure the size of things in the repository, including:

// - The source set of a build (i.e. all files and directories you
// would need to replicate to perform a build)

// - The internal representation of a directory (base chain, size
// taken up by directory entries, etc.)

#include <Basics.H>
#include <Text.H>
#include <Units.H>
#include <VestaConfig.H>
#include <VestaSource.H>
#include <VDirSurrogate.H>
#include <ReposUI.H>
#include <ParseImports.H>
#include <RemoteModelSpace.H>
#include <ShortIdKey.H>

#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif

using std::cout;
using std::cerr;
using std::endl;
using std::ostream;

typedef Table<ShortIdKey,bool>::Default VisitedShortidTable;
typedef Table<ShortIdKey,bool>::Iterator VisitedShortidIter;

// This data structure holds all the statistics we collect as we visit
// multiple directories and files.

class RecursiveStats
{
private:
  // The files and directories we have visited.
  VisitedShortidTable visitedFiles, visitedDirs;

  // Total files encountered.  (If we encounter the same file shortid
  // twice, it will be counted twice in this number.)
  Basics::uint64 totalFiles;

  // Unique files encountered.  (If we encounter the same file shortid
  // twice, it will be counted only once in this number.)
  Basics::uint64 uniqueFiles;

  // The total bytes in all unique files encountered.
  Basics::uint64 uniqueFileBytes;

  // Number of unique directories encountered.  (If we encounter the
  // same directory shortid twice, it will be counted only once in
  // this number.)
  Basics::uint64 uniqueDirs;

  // The total size (in the repository's memory) of all directories
  // encountered.  (Note that this includes shadowed entires along the
  // base chain.)
  Basics::uint64 uniqueDirBytes;

  // The total size (in the repository's memory) of the used directory
  // entries in all directories encountered.  (This excludes shadowed
  // entires along the base chain, and is closer to the amount of
  // space which would be consumed if replicating to an empty
  // repository.)
  Basics::uint64 uniqueDirUsedBytes;

public:
  RecursiveStats()
    : totalFiles(0), uniqueFiles(0), uniqueFileBytes(0),
      uniqueDirs(0), uniqueDirBytes(0), uniqueDirUsedBytes(0)
  {
  }

  void report(ostream &out, const char *indent)
  {
    out << indent << "total files       = "
	<< Basics::FormatUnitVal(totalFiles, true) << endl
	<< indent << "unique files      = "
	<< Basics::FormatUnitVal(uniqueFiles, true) << endl
	<< indent << "unique file size  = "
	<< Basics::FormatUnitVal(uniqueFileBytes) << endl
	<< indent << "total directories = "
	<< Basics::FormatUnitVal(uniqueDirs, true) << endl
	<< indent << "directory size    = "
	<< Basics::FormatUnitVal(uniqueDirUsedBytes) << endl;
  }

  void resetCounters()
  {
    totalFiles = 0;
    uniqueFiles = 0;
    uniqueFileBytes = 0;
    uniqueDirs = 0;
    uniqueDirBytes = 0;
    uniqueDirUsedBytes = 0;
  }

  void resetAll()
  {
    resetCounters();
    ShortIdKey k; bool v;
    VisitedShortidIter file_it(&visitedFiles);
    while(file_it.Next(k, v))
      {
	visitedFiles.Delete(k, v, false);
      }
    VisitedShortidIter dir_it(&visitedDirs);
    while(dir_it.Next(k, v))
      {
	visitedDirs.Delete(k, v, false);
      }
  }

  void visit(const Text &path, const Text &host, const Text &port);
  void visit(const Text &path, VestaSource *vs);

  void visitFile(VestaSource *parentVS, unsigned int index,
		 ShortId fileSid);

  void visitDir(VestaSource *dirVS);

  void visitImports(const Text &modelname, const Text &host, const Text &port);

  bool empty()
  {
    return ((visitedFiles.Size() == 0) &&
	    (visitedDirs.Size() == 0));
  }
};

// This enum is used for selecting what we're going to be actually
// printing

typedef enum {
  // Recurse into directories and follow model imports.  Each argument
  // is handled separately, so anything included in multiple arguments
  // will be represented multiple times.
  print_recursive_stats,

  // Recurse into directories and follow model imports.  Each argument
  // is counted as the additional space used over the previous
  // argumetns.  (In other words, if a repository already contained
  // the previous arguments, how much additional space would be needed
  // to replicate this argument.)
  print_recursive_delta_stats,

  // Recurse into directories and follow model imports.  The total
  // space used by all arguments is accumulated and printed.
  print_recursive_accum_stats,

  // Print all the raw statistics given by
  // VestaSource::measureDirectory
  print_raw_all,

  // Print just the base chain length
  print_raw_base,

  // Print just the directory used entry count.
  print_raw_used_count,

  // Print just the directory total entry count (including shadowed
  // entries along the base chain).
  print_raw_total_count,

  // Print just the in-memory size of the used directory entries.
  print_raw_used_size,

  // Print just the in-memory size of all directory entries (including
  // shadowed entries along the base chain).
  print_raw_total_size
} print_what;

Text program_name;

Text defpkgpar;

void usage()
{
  cerr << "Usage: " << program_name << endl
       << "        [-d] [-t] [-r] [-b] [-c] [-C] [-s] [-S]" << endl
       << "        [-R repos]" << endl
       << "        [path...]" << endl << endl;
  exit(1);
}

VestaSource *upToVersion(VestaSource *vs)
{
  assert(RootLongId.isAncestorOf(vs->longid));
  assert((vs->type == VestaSource::immutableDirectory) ||
	 (vs->type == VestaSource::immutableFile));

  while(1)
    {
      VestaSource *parent = vs->getParent();
      if(parent->type == VestaSource::appendableDirectory)
	{
	  delete parent;
	  return vs;
	}
      else if(parent->type == VestaSource::immutableDirectory)
	{
	  delete vs;
	  vs = parent;
	}
    }

  // Unreached
  return 0;
}

VestaSource *upToWorkingDir(VestaSource *vs)
{
  assert(MutableRootLongId.isAncestorOf(vs->longid));
  assert((vs->type == VestaSource::immutableDirectory) ||
	 (vs->type == VestaSource::mutableDirectory) ||
	 (vs->type == VestaSource::immutableFile) ||
	 (vs->type == VestaSource::mutableFile));

  // Until we find an object with a "checkout-by" attribute...
  while(vs && !vs->getAttrib("checkout-by"))
    {
      // ...move up to the parent
      VestaSource *parent = vs->getParent();
      delete vs;
      vs = parent;
    }

  return vs;
}

struct visitDirEntriesClosure
{
  VestaSource *dirVS;
  RecursiveStats *stats;
};

bool visitDirEntriesCallback(void* closure, VestaSource::typeTag type, Arc arc,
			     unsigned int index, Bit32 pseudoInode,
			     ShortId fileSid, bool master)
{
  visitDirEntriesClosure *cl = (visitDirEntriesClosure *) closure;

  switch(type)
    {
    case VestaSource::immutableFile:
    case VestaSource::mutableFile:
      cl->stats->visitFile(cl->dirVS, index, fileSid);
      break;
    case VestaSource::immutableDirectory:
    case VestaSource::mutableDirectory:
      {
	VestaSource *subdirVS;
	VestaSource::errorCode err =
	  cl->dirVS->lookupIndex(index, subdirVS);
	if(err == VestaSource::ok)
	  {
	    cl->stats->visitDir(subdirVS);
	    delete subdirVS;
	  }
	else
	  {
	    cerr << program_name << ": error looking up directory in "
		 << ReposUI::vsToFilename(cl->dirVS) << ": "
		 << ReposUI::errorCodeText(err) << endl;
	  }
      }
      break;
    }

  return true;
}

void RecursiveStats::visitFile(VestaSource *parentVS, unsigned int index,
			       ShortId fileSid)
{
  this->totalFiles++;
  bool visited = true;
  // If we've processed a file with this shortid before, we're done.
  if(this->visitedFiles.Get(fileSid, visited)) return;

  this->visitedFiles.Put(fileSid, visited);

  this->uniqueFiles++;

  VestaSource *fileVS;
  VestaSource::errorCode err =
    parentVS->lookupIndex(index, fileVS);
  if(err != VestaSource::ok)
    {
      cerr << program_name << ": error looking up file in "
	   << ReposUI::vsToFilename(parentVS) << ": "
	   << ReposUI::errorCodeText(err) << endl;
      return;
    }

  this->uniqueFileBytes += fileVS->size();

  delete fileVS;
}

void RecursiveStats::visitDir(VestaSource *dirVS)
{
  bool visited = true;
  // If we've processed a file with this directory shortid before,
  // we're done.
  if(this->visitedDirs.Get(dirVS->shortId(), visited)) return;

  this->visitedDirs.Put(dirVS->shortId(), visited);

  this->uniqueDirs++;

  VestaSource::directoryStats dirStats;
  VestaSource::errorCode err = dirVS->measureDirectory(dirStats);
  if(err != VestaSource::ok)
    {
      cerr << program_name << ": error measuring directory "
	   << ReposUI::vsToFilename(dirVS) << ": "
	   << ReposUI::errorCodeText(err) << endl;
      return;
    }

  this->uniqueDirBytes += dirStats.totalEntrySize;
  this->uniqueDirUsedBytes += dirStats.usedEntrySize;

  visitDirEntriesClosure cl;
  cl.dirVS = dirVS;
  cl.stats = this;

  err = dirVS->list(0, visitDirEntriesCallback, (void *) &cl);

  if(err != VestaSource::ok)
    {
      cerr << program_name << ": error listing directory "
	   << ReposUI::vsToFilename(dirVS) << ": "
	   << ReposUI::errorCodeText(err) << endl;
      return;
    }
}

void RecursiveStats::visitImports(const Text &modelname,
				  const Text &host, const Text &port)
{
  try
    {
      VestaSource *root = VDirSurrogate::LongIdLookup(RootLongId, host, port);
      {
	ParseImports::RemoteModelSpace ms(root, 0);
	ImportSeq seq;
	ParseImports::P(modelname, seq);
	while(seq.size())
	  {
	    Import *imp = seq.remlo();
	    this->visit(imp->path, host, port);
	    delete imp;
	  }
      }
      delete root;
    }
  catch(...)
    {
    }
}

void RecursiveStats::visit(const Text &path, VestaSource *vs)
{
  if(((vs->type == VestaSource::immutableFile) ||
      (vs->type == VestaSource::mutableFile)) &&
     (path.Sub(path.Length() - 4) == ".ves"))
    {
      this->visitImports(path, vs->host(), vs->port());
    }

  if(RootLongId.isAncestorOf(vs->longid))
    {
      if((vs->type == VestaSource::immutableDirectory) ||
	 (vs->type == VestaSource::immutableFile))
	{
	  vs = upToVersion(vs);
	}
    }
  else if(MutableRootLongId.isAncestorOf(vs->longid))
    {
      vs = upToWorkingDir(vs);
    }

  if((vs->type == VestaSource::immutableDirectory) ||
     (vs->type == VestaSource::mutableDirectory))
    {
      this->visitDir(vs);
    }
}

void RecursiveStats::visit(const Text &path,
			   const Text &host, const Text &port)
{
  VestaSource *vs = ReposUI::filenameToVS(path, host, port);
  this->visit(path, vs);
}

void process(const Text &path, const Text &host, const Text &port,
	     RecursiveStats &stats,
	     print_what output_selection, bool print_path)
{
  // Canonicalize path
  Text cpath;
  cpath = ReposUI::canonicalize(path, defpkgpar);

  VestaSource *vs = ReposUI::filenameToVS(cpath, host, port);

  // Did the user ask for raw statistics?
  if((output_selection == print_raw_all) ||
     (output_selection == print_raw_base) ||
     (output_selection == print_raw_used_count) ||
     (output_selection == print_raw_total_count) ||
     (output_selection == print_raw_used_size) ||
     (output_selection == print_raw_total_size))
    {
      VestaSource::directoryStats dirStats;
      VestaSource::errorCode err = vs->measureDirectory(dirStats);
      if(err != VestaSource::ok)
	{
	  cerr << program_name << ": " << cpath << ": "
	       << ReposUI::errorCodeText(err) << endl;
	  exit(2);
	}

      if(print_path)
	cout << cpath << ":";

      // Print the statistic(s) the user asked for.
      if(print_path)
	cout << " ";
      switch(output_selection)
	{
	case print_raw_base:
	  cout << dirStats.baseChainLength << endl;
	  break;
	case print_raw_used_count:
	  cout << dirStats.usedEntryCount << endl;
	  break;
	case print_raw_total_count:
	  cout << dirStats.totalEntryCount << endl;
	  break;
	case print_raw_used_size:
	  cout << dirStats.usedEntrySize << endl;
	  break;
	case print_raw_total_size:
	  cout << dirStats.totalEntrySize << endl;
	  break;
	default:
	  if(print_path)
	    cout << endl << "\t";
	  cout << "base chain length = " << dirStats.baseChainLength << endl;
	  if(print_path)
	    cout << "\t";
	  cout << "used entry count  = " << dirStats.usedEntryCount << endl;
	  if(print_path)
	    cout << "\t";
	  cout << "total entry count = " << dirStats.totalEntryCount << endl;
	  if(print_path)
	    cout << "\t";
	  cout << "used entry size   = "
	       << Basics::FormatUnitVal(dirStats.usedEntrySize, true) << endl;
	  if(print_path)
	    cout << "\t";
	  cout << "total entry size  = "
	       << Basics::FormatUnitVal(dirStats.totalEntrySize, true) << endl;
	  break;
	}
    }
  else
    {
      bool first = stats.empty();

      stats.visit(cpath, vs);

      if(output_selection == print_recursive_stats)
	{
	  if(print_path)
	    cout << cpath << endl;
	  stats.report(cout, print_path ? "\t" : "");
	  
	  stats.resetAll();
	}
      else if(output_selection == print_recursive_delta_stats)
	{
	  if(print_path)
	    {
	      cout << cpath;
	      if(!first)
		cout << " (delta)";
	      cout << endl;
	    }
	  stats.report(cout, print_path ? "\t" : "");

	  stats.resetCounters();
	}
    }
}

int
main(int argc, char* argv[])
{
  program_name = argv[0];

  try {
    //
    // Read config file
    //
    defpkgpar = VestaConfig::get_Text("UserInterface",
				      "DefaultPackageParent");

    // Default to treating each argument independently and gathering
    // full recursive statistics.
    print_what output_selection = print_recursive_stats;

    // Use the repository from the config file by default
    Text repos;
    bool default_repos = true;
    Text host(VDirSurrogate::defaultHost());
    Text port(VDirSurrogate::defaultPort());

    opterr = 0;
    for (;;) {
      int c = getopt(argc, argv, "?bcCsSrtdR:");
      if (c == EOF) break;
      switch (c) {
      case 'b':
	output_selection = print_raw_base;
	break;
      case 'c':
	output_selection = print_raw_used_count;
	break;
      case 'C':
	output_selection = print_raw_total_count;
	break;
      case 's':
	output_selection = print_raw_used_size;
	break;
      case 'S':
	output_selection = print_raw_total_size;
	break;
      case 'r':
	output_selection = print_raw_all;
	break;
      case 'd':
	output_selection = print_recursive_delta_stats;
	break;
      case 't':
	output_selection = print_recursive_accum_stats;
	break;
      case 'R':
	repos = optarg;
	default_repos = false;
	break;
      case '?':
      default:
	usage();
      }
    }

    // Use a nondefault repository if specified
    if (!default_repos)
      {
	int colon = repos.FindCharR(':');
	if (colon == -1)
	  {
	    host = repos;
	    repos = repos + ":" + port;
	  }
	else
	  {
	    host = repos.Sub(0, colon);
	    port = repos.Sub(colon+1);
	  }
      }

    RecursiveStats stats;

    // If there are multiple arguments, print the path of each before
    // the measurement.
    bool print_path = (argc > (optind + 1));

    if((argc - optind) == 0)
      {
	process(".", host, port, stats,
		output_selection, print_path);
      }
    else
      {
	for(unsigned int i = optind; i < argc; i++)
	  {
	    process(argv[i], host, port,
		    stats, output_selection, print_path);
	  }
      }
    if(output_selection == print_recursive_accum_stats)
      {
	cout << "Total:" << endl;
	stats.report(cout, "\t");
      }

  } catch (VestaConfig::failure f) {
    cerr << program_name << ": " << f.msg << endl;
    exit(2);
  } catch (SRPC::failure f) {
    cerr << program_name
	 << ": SRPC failure; " << f.msg << " (" << f.r << ")" << endl;
    exit(2);
  } catch (ReposUI::failure f) {
    cerr << program_name << ": " << f.msg << endl;
    exit(2);
  }

  return 0;
}
