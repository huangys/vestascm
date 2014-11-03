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


// CacheIntf -- defines constants for Cache Server interface, version,
//   procedures, and result codes

#ifndef _CACHE_INTF_H
#define _CACHE_INTF_H

#include <Basics.H>

class CacheIntf {
  public:
    // Interface version (used by both clients and servers)
    //
    // History:
    //   Version  1 - initial version
    //   Version  2 - added Checkpoint() method; removed EraseCache()
    //   Version  3 - added timestamps to Lookup() and AddEntry()
    //   Version  4 - added RenewLeases() method and minimum derived
    //                lease timestamp to the AddEntry() method
    //   Version  5 - added support for weeding
    //   Version  6 - changed format of free variable names on wire;
    //                added "fvs" result to "Lookup" method
    //   Version  7 - added GetLookupStats() method
    //   Version  8 - removed timestamps
    //   Version  9 - no longer uses int_seq to send CIs and DIs
    //   Version 10 - removed "fvs" result from "Lookup" method
    //   Version 11 - removed GetLookupStats() method
    //   Version 12 - added "isEmpty" result to "FreeVariables" method
    //   Version 13 - added "pkgVersion" argument to "Checkpoint" method
    //   Version 14 - added "GetCacheId" and "GetCacheState" methods
    //   Version 15 - changed size of PKPrefix::T
    //   Version 16 - changed "FreeVars" signature to use CompactFV::List
    //   Version 17 - removed derived timestamp arg to "AddEntry" method
    //   Version 18 - added boolean return result to "WeederRecovering"
    //   Version 19 - added Text "sourceFunc" argument to "AddEntry"
    //   Version 20 - added GetCacheInstance() method; changed several
    //                methods to include the instance fingerprint to avoid
    //                evaluations and weeds continuing across cache server
    //                restarts.
    //   Version 21 - Changed CommitChkpt() to accept its checkpoint
    //                relative to the graph log root.  A couple
    //                potentially problematic things are cleaned up
    //                with that method.
    //   Version 22 - Eliminated inappropriate uses of
    //                send_bytes/recv_bytes for integers and arrays of
    //                integers.  This makes it possible for cache
    //                servers and clients of different byte orders to
    //                inter-operate.
    //   Version 23 - Major re-work of free variables to allow for
    //                much larger sets, eliminating ceilings imposed
    //                by the use of 16-bit integers

    enum {
      LargeFVsVersion = 23,
      Version = 23
    };

    // Debugging levels
    enum DebugLevel {
	None = 0,		// no debugging at all
        StatusMsgs,		// cache/weeder status messages
	LeaseExp,		// lease expiration messages
	LogRecover,		// show recovered data at start-up
	LogFlush,		// show when logs are flushed
        MPKFileFlush,           // show when MPKFiles are flushed
	LogFlushEntries,	// also show individual entries written to logs
	WeederOps,		// show operations related to the weeder
	AddEntryOp,		// show SRPC calls for "AddEntry"
	OtherOps,		// show all other SRPC calls
        WeederScans,            // show scans of graphLog by weeder
	All                     // all levels
    };
    static char *DebugName(int i) throw ();
    /* Return a string name for debugging level "i". */

    // Procedure identifiers
    enum ProcIds {
      // evaluator client procs
      AddEntryProc, FreeVarsProc, LookupProc,
      CheckpointProc, RenewLeasesProc,

      // weeder client procs
      WeedRecoverProc, StartMarkProc, SetHitFilterProc,
      GetLeasesProc, ResumeLeasesProc, EndMarkProc, CommitChkptProc,

      // procs used by both the evaluator and weeder
      GetCacheInstanceProc,

      // debugging client procs
      FlushAllProc, GetCacheIdProc, GetCacheStateProc
    };

    // "Lookup" result codes and names
    enum LookupRes { Hit, Miss, FVMismatch, BadLookupArgs };
    static const char *const LookupResName(LookupRes res) throw ();

    // the kinds of lookup outcomes
    enum LookupOutcome {
      NewHits, WarmHits, DiskHits, AllMisses, NumKinds, NoOutcome};

    // "AddEntry" result codes and names
    enum AddEntryRes { EntryAdded, NoLease, BadAddEntryArgs };
    static const char *const AddEntryResName(AddEntryRes res) throw ();
};

#endif // _CACHE_INTF_H
