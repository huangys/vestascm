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

// File: EvalBasics.H

// This header file contains definitions that are closely connected
// to the language definition.

#ifndef EvalBasics_H
#define EvalBasics_H

#include "ValExpr.H"
#include <Basics.H>
#include <CacheIndex.H>
#include <CacheC.H>
#include <VestaSource.H>

class Evaluator {
public:
  class failure {
  public:
    Text msg;
    bool r;
    inline failure(const Text &msg, bool r)
      { this->msg = msg; this->r = r; };
    inline failure(const failure &f) 
      { this->msg = f.msg; this->r = f.r; };
    inline failure& operator=(const failure &f) {
      this->msg = f.msg; this->r = f.r; return *this; };
  };
};

// Globals for command line. Set once at the start of an evaluation.
// No need to protect by mutex.
extern bool parseOnly, noAddEntry, fsDeps, evalCalls, stopOnError,
            psStat, shipBySymLink, shipClean, forceClean, diagnose,
	    hushedShipping, pauseBeforeTool, pauseAfterTool, verboseDupMsgs,
	    pauseAfterToolSignal, pauseAfterToolError, autoRepl, dotLogFiles, dependencyCheck;
extern Text shipToPath, shipFromPath;
extern char* pkSalt;

// Globals for the cache. Set once at the start of an evaluation.
// No need to protect by mutex.
extern CacheC *theCache;
extern time_t leaseDuration;
extern int cacheOption, maxThreads;

// Globals counters. Protected by counterMu.
extern int toolCallCounter, toolHitCounter, appCallCounter, appHitCounter,
           sModelCallCounter, sModelHitCounter, nModelCallCounter, 
	   nModelHitCounter;

// The threshold to determine whether to fingerprint file content.
// Set once at the start of an evaluation. No need to protect by mutex.
extern IntegerVC *fpContent;

// Globals for the interface name of tool directory server:
extern Text toolDirServerIntfName;

// Globals for the top-level model. Set once at the start of an
// evaluation. No need to protect by mutex.
extern VestaSource *topModelRoot;
extern ShortId topModelSid;

// Globals for diagnostic I/O:
extern bool recordTrace;
extern bool recordCallStack;
extern bool printVersion;
extern bool printResult;
extern bool printCacheStats;
extern bool printMemStats;
extern bool printSidNum;

// Mutex to protect callStackPrinted and workers' call stacks.
extern Basics::mutex callStackMu;

// Global for creating deriveds.
extern VestaSource *rootForDeriveds;

// Character string manipulation (char * and Text)
extern const Text emptyText;

// Conversion between integer and arc.
Text IntToText(int n);
Text IntArc(int n);
int ArcInt(const Text& t);
int IncCounter(int* counter);

#endif  // EvalBasics_H
