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

/* File: Driver.C                                              */

#include "VASTi.H"
#include "Err.H"
#include "Files.H"
#include "Val.H"
#include "PrimRunTool.H"
#include "Timing.H"
#include <CacheIndex.H>
#include <CacheConfig.H>
#include <CacheC.H>
#include <ParCacheC.H>
#include <VestaConfig.H>
#include <fstream>
#include <locale.h>

using std::cout;
using std::cerr;
using std::endl;
using std::fstream;
using std::ofstream;
using std::ios;

// Record the function call trace.
bool recordTrace;
bool recordCallStack;

CacheC *theCache = NULL;
time_t leaseDuration;

VestaSource *rRoot, *mRoot, *topModelRoot;
VestaSource *rootForDeriveds;
ShortId topModelSid;

bool printVersion;
bool printResult;
bool printCacheStats;
bool printMemStats = false;
bool printSidNum;

// The interface name of the tool directory server:
Text toolDirServerIntfName;

// Evaluator version.  Defiend but the build process
extern const char *Version;

// More globals:
bool parseOnly, noAddEntry, fsDeps, evalCalls, stopOnError,
     psStat, shipBySymLink, shipClean, forceClean, diagnose,
     hushedShipping, pauseBeforeTool, pauseAfterTool, verboseDupMsgs,
     pauseAfterToolSignal, pauseAfterToolError, autoRepl, dotLogFiles, dependencyCheck;

int toolCallCounter, toolHitCounter, appCallCounter, appHitCounter,
    sModelCallCounter, sModelHitCounter, nModelCallCounter, nModelHitCounter,
    cacheOption, maxThreads;

IntegerVC *fpContent;

Text shipToPath, shipFromPath;
char* pkSalt = 0;

int main(int argc, char* argv[]) {

  // First thing we'll make sure that the
  // locale settings can't mess with how
  // regular expressions are matched.
  setlocale(LC_COLLATE, "C");
  setlocale(LC_CTYPE, "C");

  bool success = false;
  CacheIntf::DebugLevel cdebug = CacheIntf::None;

  leaseDuration = Config_LeaseTimeoutSecs;

  parseOnly = noAddEntry = fsDeps = evalCalls = psStat = false;
  shipBySymLink = shipClean = forceClean = hushedShipping = false;
  pauseBeforeTool = pauseAfterTool = verboseDupMsgs =
    pauseAfterToolSignal = pauseAfterToolError = false;
  cacheOption = 3;     // The default is to cache all.
  stopOnError = true;  // The default is to stop on the first error.
  diagnose = false;
  recordTrace = false;
  recordCallStack = false;
  printVersion = false;
  printResult = false;
  printSidNum = false;
  printCacheStats = false;
  printMemStats = false;
  autoRepl = true;
  dotLogFiles = true;
  dependencyCheck = false;

  maxThreads = -1;
  
  toolCallCounter = toolHitCounter = appCallCounter = appHitCounter = 0;
  sModelCallCounter = sModelHitCounter = nModelCallCounter = nModelHitCounter = 0;

  int evalArgc = 0;
  char *evalArgs[32];

  // Get default main model name
  Text defaultmain(".main.ves");
  (void) VestaConfig::get("Evaluator", "DefaultMain", defaultmain);
  Text filename;

  // Get optional setting which enables numeric display of shortids.
  if(VestaConfig::is_set("Evaluator", "print_numeric_sid")) {
    printSidNum = VestaConfig::get_bool("Evaluator", "print_numeric_sid");
  }

  // Parse evaluator switches from vesta config file, and save
  // the switches in evalArgs.
  try {
    Text switches;
    if (VestaConfig::get("Evaluator", "Switches", switches)) {
      int i = 0, len = switches.Length();
      while (i < len) {
	// Skip white space:
	char c = switches[i];
	while (i < len && (c == ' ' || c == '\t' || c == '\f' || c == '\n'))
	  c = switches[++i];
	// Get the switch:
	int j = i;
	while (j < len && c != ' ' && c != '\t' && c != '\f' && c != '\n')
	  c = switches[++j];
	// Parse the switch:
	evalArgs[evalArgc++] = switches.Sub(i, j - i).chars();
	// Increment i:
	i = j;
      }
    }
  } catch (VestaConfig::failure f) {
    Error(cerr, Text("Vesta configuration failure: ") + f.msg + ".\n");
    return true;
  }
  // Save switches from command line in evalArgs.
  int i;
  for (i = 1; i < argc; i++) {
    evalArgs[evalArgc++] = argv[i];
  }
  // Interpret the combined switches:
  i = 0;
  while (i < evalArgc) {
    if (evalArgs[i][0] == '-') {
      if (strcmp(evalArgs[i], "-cache") == 0) {
	if (evalArgc <= ++i) {
	  cerr << "Error: expecting a caching level: none, runtool, model, or pfga.\n";
	  return true;
	}
	if (strcmp(evalArgs[i], "none") == 0) {
	  cacheOption = 0;
	  // cout << "Ignoring cache.\n";
	}
	else if (strcmp(evalArgs[i], "runtool") == 0) {
	  cacheOption = 1;
	  // cout << "Caching run_tool primitives only.\n";
	}
	else if (strcmp(evalArgs[i], "model") == 0) {
	  cacheOption = 2;
	  // cout << "Caching run_tool primitives and models only.\n";
	}
	else if (strcmp(evalArgs[i], "all") == 0) {
	  // cout << "Caching all function calls.\n";
	}
	else {
	  cerr << "Error: unrecognized caching-level '" << evalArgs[i] << "'\n";
	  return true;
	}
	i++;
      }
      else if (strcmp(evalArgs[i], "-trace") == 0) {
	recordTrace = true;
	// cout << "Tracing function calls.\n";
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-trace") == 0) {
	recordTrace = false;
	// cout << "Tracing function calls.\n";
	i++;
      }
      else if (strcmp(evalArgs[i], "-stack") == 0) {
	recordCallStack = true;
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-stack") == 0) {
	recordCallStack = false;
	i++;
      }
      else if (strcmp(evalArgs[i], "-s") == 0) {
	shipBySymLink = true;
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-s") == 0) {
	shipBySymLink = false;
	i++;
      }
      else if (strcmp(evalArgs[i], "-result") == 0) {
	printResult = true;
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-result") == 0) {
	printResult = false;
	i++;
      }
      else if (strcmp(evalArgs[i], "-cstats") == 0) {
	printCacheStats = true;
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-cstats") == 0) {
	printCacheStats = false;
	i++;
      }
      else if (strcmp(evalArgs[i], "-mstats") == 0) {
	printMemStats = true;
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-mstats") == 0) {
	printMemStats = false;
	i++;
      }
      else if (strcmp(evalArgs[i], "-version") == 0) {
	printVersion = true;
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-version") == 0) {
	printVersion = false;
	i++;
      }
      else if (strcmp(evalArgs[i], "-clean") == 0) {
	shipClean = true;
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-clean") == 0) {
	shipClean = false;
	i++;
      }
      else if (strcmp(evalArgs[i], "-CLEAN") == 0) {
	forceClean = true;
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-CLEAN") == 0) {
	forceClean = false;
	i++;
      }
      else if (strcmp(evalArgs[i], "-shipto") == 0) {
	if (evalArgc <= ++i) {
	  cerr << "Error: expecting a pathname to which to ship the result.\n";
	  return true;
	}
	shipToPath = evalArgs[i++];
      }
      else if (strcmp(evalArgs[i], "-shipfrom") == 0) {
	if (evalArgc <= ++i) {
	  cerr << "Error: expecting a pathname from which to get the result.\n";
	  return true;
	}
	shipFromPath = evalArgs[i++];
      }
      else if (strcmp(evalArgs[i], "-pk-salt") == 0) {
	if (evalArgc <= ++i) {
	  cerr << "Error: expecting a pk salt string by which base PK tags should be extended.\n";
	  return true;
	}
	// (No need to copy the string argument thanks to the
	// assumption of garbage collection)
	pkSalt = evalArgs[i++];
      }
      else if (strcmp(evalArgs[i], "-cdebug") == 0) {
	if (evalArgc <= ++i) {
	  cerr << "Error: expecting cache server debugging level.\n";
	  return true;
	}
	if (sscanf(evalArgs[i], "%d", &cdebug) != 1) {
          int j;
	  for (j = 0; j <= CacheIntf::All; j++) {
	    if (strcmp(evalArgs[i], CacheIntf::DebugName(j)) == 0) break;
	  }
	  cdebug = (CacheIntf::DebugLevel)j;
	}
	if (cdebug < CacheIntf::None || cdebug > CacheIntf::All) {
	  cerr << "Error: unrecognized debug-level '" << evalArgs[i] << "'\n";
	  return true;
	}
	i++;
      }
      else if (strcmp(evalArgs[i], "-k") == 0) {
	stopOnError = false;
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-k") == 0) {
	stopOnError = true;
	i++;
      }
      else if (strcmp(evalArgs[i], "-diagnose") == 0) {
	diagnose = true;
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-diagnose") == 0) {
	diagnose = false;
	i++;
      }
      else if (strcmp(evalArgs[i], "-noaddentry") == 0) {
	  noAddEntry = true;
	  // cout << "Preventing any new entry from being added to the cache.\n";
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-noaddentry") == 0) {
	  noAddEntry = false;
	  // cout << "Allowing any new entries to be added to the cache.\n";
	i++;
      }
      else if (strcmp(evalArgs[i], "-addentry") == 0) {
	  noAddEntry = false;
	  // cout << "Allowing any new entries to be added to the cache.\n";
	i++;
      }
      else if (strcmp(evalArgs[i], "-stop-before-tool") == 0 ) {
	  pauseBeforeTool = true;
	  i++;
      }
      else if (strcmp(evalArgs[i], "-stop-after-tool") == 0 ) {
	  pauseAfterTool = true;
	  i++;
      }
      else if (strcmp(evalArgs[i], "-stop-after-tool-signal") == 0 ) {
	  pauseAfterToolSignal = true;
	  i++;
      }
      else if (strcmp(evalArgs[i], "-stop-after-tool-error") == 0 ) {
	  pauseAfterToolError = true;
	  i++;
      }
      else if (strcmp(evalArgs[i], "-fsdeps") == 0) {
	  fsDeps = true;
	  // cout << "Printing filesystem dependencies.\n";
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-fsdeps") == 0) {
	  fsDeps = false;
	  // cout << "Not printing filesystem dependencies.\n";
	i++;
      }
      else if (strcmp(evalArgs[i], "-evalcalls") == 0) {
	  evalCalls = true;
	  // cout << "Not printing all EvaluatorDir calls.\n";
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-evalcalls") == 0) {
	  evalCalls = false;
	  // cout << "Not printing all EvaluatorDir calls.\n";
	i++;
      }
      else if (strcmp(evalArgs[i], "-ps") == 0) {
	psStat = true;
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-ps") == 0) {
	psStat = false;
	i++;
      }
      else if (strcmp(evalArgs[i], "-maxthreads") == 0) {
	if (evalArgc <= ++i) {
	  cerr << "Error: expecting an integer for maxthreads.\n";
	  return true;
	}
	maxThreads = atoi(evalArgs[i++]);
      }
      else if (strcmp(evalArgs[i], "-parse") == 0) {
	  parseOnly = true;
	  // cout << "Just parse.\n";
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-parse") == 0) {
	  parseOnly = false;
	  // cout << "Parse and execute.\n";
	i++;
      }
      else if (strcmp(evalArgs[i], "-hushship") == 0) {
          hushedShipping = true;
        i++;
      }
      else if (strcmp(evalArgs[i], "-no-hushship") == 0) {
          hushedShipping = false;
        i++;
      }
      else if(strcmp(evalArgs[i], "-dependency-check") == 0) {
	dependencyCheck = true;
	i++;
      }
      else if(strcmp(evalArgs[i], "-no-dependency-check") == 0) {
	dependencyCheck = false;
	i++;
      }
      else if (strcmp(evalArgs[i], "-autorepl") == 0) {
	autoRepl = true;
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-autorepl") == 0) {
	autoRepl = false;
	i++;
      }
      else if (strcmp(evalArgs[i], "-no-log") == 0) {
	  dotLogFiles = false;
	i++;
      }
      else if (strcmp(evalArgs[i], "-log") == 0) {
	  dotLogFiles = true;
	i++;
      }
      else if (strcmp(evalArgs[i], "-printtimings") == 0) {
	  if (evalArgc <= i++) {
	    cerr << "Error: expecting a timing level to print: "
	      << TimingRecorder::printLevels() << endl;
	    return true;
	  }
	  try {
	    TimingRecorder::level = TimingRecorder::parseTimingLevel(evalArgs[i]);
	  } catch (TimingRecorder::failure f) {
	    cerr << "Error: " << f.msg << endl;
	    return true;
	  }
	  i++;
      }
      else if (strcmp(evalArgs[i], "-timingfile") == 0) {
	  if (evalArgc <= i++) {
	    cerr << "Error: expecting a filename for timing data." << endl;
	    return true;
	  }
	  try {
	    TimingRecorder::set_out(evalArgs[i]);
	  } catch (TimingRecorder::failure f) {
	    cerr << "Error: " << f.msg << endl;
	    return true;
	  }
	  i++;
      }
      else if (strcmp(evalArgs[i], "-timing-regexp") == 0) {
	  if (evalArgc <= i++) {
	    cerr << "Error: expecting a regular expression." << endl;
	    return true;
	  }
	  try {
	    TimingRecorder::init_regexp(evalArgs[i]);
	  } catch (TimingRecorder::failure f) {
	    cerr << "Error: " << f.msg << endl;
	    return true;
	  }
	  i++;
      }
      else if (strcmp(evalArgs[i], "-timing-regexp-file") == 0) {
	  if (evalArgc <= i++) {
	    cerr << "Error: expecting a file of regular expressions." << endl;
	    return true;
	  }
	  try {
	    TimingRecorder::parse_re_file(evalArgs[i]);
	  } catch (TimingRecorder::failure f) {
	    cerr << "Error: " << f.msg << endl;
	    return true;
	  }
	  i++;
      }
      else if (strcmp(evalArgs[i], "-timing-args-depth") == 0) {
	  if (evalArgc <= i++) {
	    cerr << "Error: expecting a depth." << endl;
	    return true;
	  }
          TimingRecorder::report_argument_depth = atoi(evalArgs[i++]);
      }
      else if (strcmp(evalArgs[i], "-verbose-dup-msgs") == 0) {
	      verboseDupMsgs = true;
	      i++;
      }
      else if (strcmp(evalArgs[i], "-no-verbose-dup-msgs") == 0) {
	      verboseDupMsgs = false;
	      i++;
      }
      else {
	cerr << "\n++++ unrecognized flag: " << evalArgs[i] << ".\n\n";
	return true;
      }
    }
    else {
      if (i != evalArgc-1) {
	cerr << "\n++++ unrecognized flag: " << evalArgs[i] << ".\n\n";
	return true;
      }
      filename = evalArgs[i++];
    }
  } // end of while.

  if (printVersion) {
    cout << "Vesta evaluator, version " << Version << "\n" << endl;
  }

  try {
    if (maxThreads == -1) {
      maxThreads = VestaConfig::get_int("Evaluator", "MaxThreads");
    }
    fpContent = NEW_CONSTR(IntegerVC, 
			   (VestaConfig::get_int("Evaluator", "FpContent")));
    // Set up the cache:
    if (cacheOption != 0) {
      Text host;
      if (VestaConfig::get("CacheServer", "Host", host) && host.Length() > 0)
	ParCacheC::SetServerHost(host.chars());
      theCache = NEW_CONSTR(CacheC, (cdebug));
    }
    // Set up the repository:
    rRoot = VestaSource::repositoryRoot();
    CreateRootForDeriveds();
    // Obtain the model to be evaluated:
    filename = MainModel(filename, defaultmain);
    // Evaluate:
    success = StartEval(filename);
    // Print this process's status:
    if (psStat) {
      cout << "\nEvaluator Process Stats: " << endl;
      Text command("ps -o pid,user,vsz,rss,time,comm -p ");
      command += IntToText(getpid());
      system(command.chars());
    }
    DeleteRootForDeriveds();
  } catch (VestaConfig::failure f) {
    Error(cerr, Text("Vesta configuration failure: ") + f.msg + ".\n");
    success = false;
  } catch (SRPC::failure f) {
    Error(cerr, Text("SRPC failure: ") + f.msg + ". ");
    if (theCache == NULL) {
      cerr << "The cache server is possibly down.";
    }
    else {
      cerr << "The repository server is possibly down.";
    }
    success = false;
   } catch (Evaluator::failure f) {
     Error(cerr, Text("Vesta evaluation failure: ") + f.msg + ".\n");
     success = false;
   }

  // Print error summary, if the evaluation failed.
  ErrorSummary(cerr);
  if (!success) {
    cerr << "Vesta evaluation failed.\n";
  }

  return !success;
}
