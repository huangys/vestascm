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

// Created on Mon Mar  3 16:36:52 PST 1997 by heydon
// Last modified on Mon Aug  9 17:08:16 EDT 2004 by ken@xorian.net  
//      modified on Tue Jun 27 15:42:14 PDT 2000 by mann  
//      modified on Wed Apr 14 11:50:39 PDT 1999 by heydon

// Weeder.H -- the Vesta-2 weeder class

#ifndef _WEEDER_H
#define _WEEDER_H

#include <Basics.H>
#include <FS.H>
#include <SRPC.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <VestaLogSeq.H>
#include <SourceOrDerived.H>
#include <CacheIntf.H>
#include <BitVector.H>
#include <WeederC.H>    // cache server weeder-client interface

#include "CommonErrors.H"
#include "WeedArgs.H"
#include "RootTbl.H"
#include "GLNode.H"
#include "GLNodeBuffer.H"
#include "ReposRoots.H"

class Weeder {
  public:
    // constructor
    Weeder(CacheIntf::DebugLevel debug =CacheIntf::None)
      throw (SRPC::failure, FS::Failure);
    /* Initialize the weeder and recover its stable state. */

    // do work
    bool Weed(WeedArgs &args) throw (SRPC::failure,
      FS::Failure, FS::DoesNotExist, VestaLog::Error, VestaLog::Eof,
      InputError, SysError, ReposError);
    /* Check for a previous incomplete weed, and if one exists,
       complete it and return true.  Otherwise, perform a new weed
       and return false, keeping all cache entries and deriveds
       reachable from roots found in the graphLog that to be kept
       according to the weeding instructions in the file named by
       "args.instrFile", or from the standard input if "args.instrFile
       == NULL".  If "args.doDeletions" is false, then no entries are
       actually deleted. If "args.globArgs" is true, the expansion of
       each file matched by a pattern in the weeder instructions is
       printed. If "args.printRoots" is true, for each Root in the
       graphLog, a line is printed indicating whether or not the
       weeder instructions indicate that the root should be kept. */

  private:
    // read-only after init
    CacheIntf::DebugLevel debug;
    const WeedArgs *args;
    WeederC *cache;

    // STABLE fields
    BitVector *weeded;        // set of CI's to delete
    time_t startTime;         // time recorded before graph log is flushed
    ShortId disShortId;       // ShortId of file containing DIs to keep
    RootTbl *instrRoots;      // roots to keep according to instructions file
    RootTbl *markedRoots;     // roots marked during mark phase
    /* Note: 'markedRoots' is a subset of the Root nodes encountered in the
       GraphLog during the mark phase. It includes all such encountered roots
       that are in 'instrRoots', plus all roots deemed ``fresh'' enough as
       per the "-keep" command-line switch. */

    // volatile fields
    BitVector *initCIs;       // initial CI's at start of weed
    BitVector *marked;        // CI's marked during the mark phase
    BitVector *wLeases;       // set of leased CI's
    BitVector *glCIs;         // set of CI's found in GL (used to
			      // check for inconsistencies)
    VestaLogSeq graphLogSeq;  // cache server's graph log sequence
    int newLogVer;            // version of "graphLogSeq" to read up to
    Text logChkptFName;	      // temporary filename to which we checkpoint
    			      // the pruned graph log
    std::ofstream pendingGL;  // output stream for overflow nodes
    int markedCnt;            // number of nodes marked on scan of (pending)GL
    int markedCntTotal;       // total entries marked
    GLNodeBuffer *nodeBuff;   // buffer of unprocessed graphLog nodes
    FILE *disFP;              // file for list of DIs to keep
    time_t keepTime;          // keep anything newer than this

    // mark phase methods -----------------------------------------------------

    void MarkPhase() throw (SRPC::failure, FS::Failure, FS::DoesNotExist,
      VestaLog::Error, VestaLog::Eof, InputError, SysError, ReposError);
    /* The weeder's mark phase. Call the cache server's "StartMark" method to
       checkpoint the graphLog, then process the weeder instructions. If "-n"
       was specified, then print the disposition of each of the roots in the
       graphLog. Otherwise, do the actual marking work by calling "MarkWork".
       If the call to "StartMark" succeeded but an exception is thrown
       thereafter, "ResumeLeaseExp" is called and the exception is re-thrown.

       During a successful run, this method performs the following steps:

       1. calls the cache's "StartMark" method, which has the side-effect
          of disabling lease expiration;
       2. calls GatherWeedRoots::P to read the weeder instructions; and
       3. calls MarkWork below to do the actual marking work.

       This method sets "this->roots" to the table of weeder roots to keep.
    */

    void MarkWork() throw (SRPC::failure, FS::Failure, VestaLog::Error,
        VestaLog::Eof, SourceOrDerived::Fatal, ReposError);
    /* Read "this->graphLogSeq", setting "this->weeded" to the set of cache
       entries to delete. Also, write the indices of any DIs to keep to a new
       derived file, and set "this->disShortId" to the ShortId of that file.
       If an exception is thrown before the call to "ResumeLeaseExp",
       "ResumeLeaseExp" is called and the exception is re-thrown.

       During a successful run, this method performs the following steps:

       1. creates/opens the derived ``keep file'';
       2. does marking work by scanning graph log until there are no more
          entries to mark;
       3. calls the cache's SetHitFilter method;
       4. calls the cache's GetLeases method;
       5. calls the cache's ResumeLeaseExp method;
       6. marks all leased cache entries, and scans graph log until all
          entries protected by leased entries are also marked;
       7. closes the file created in step 1;
    */

    void CopyGLtoPending(const PkgTbl *pkgTbl)
      throw (FS::Failure, VestaLog::Error);
    /* Copy the graph log "this->graphLogSeq" (reading up to but not including
       version "this->newLogVer" of the log) to the "pendingGL" file named by
       the Vesta configuration file. Any "Root" nodes encountered in the
       graphLog that are contained in "this->roots" are processed. If "pkgTbl"
       is non-NULL, print the disposition of each Root node in the graph log.
    */

    void ScanLogOnce() throw (FS::Failure);
    /* Repeatedly scan the "pendingGL" file, marking any reachable nodes,
       until there are no more nodes to mark. Write the indices of reachable
       deriveds from each marked node to the file "this->disFP". */

    void MarkNode(CacheEntry::Index ci) throw (FS::Failure);
    /* Mark the node with index "ci" as being reachable. If there is a
       node in the weeder's buffer with this "ci", process that node
       recursively. This sets the internal "markedOne" bit if "ci" was not
       already marked. */

    void ProcessNode(const GLNode &node) throw (FS::Failure);
    /* Process the node "node"; that is, mark each of its children. Also,
       write the set of DIs in "node" to "this->disFP". */

    void PrintRoot(const PkgBuild &root, const PkgTbl &pkgTbl,
      bool chosenRoot, bool freshRoot) throw ();
    /* Print the disposition of "root" according to "pkgTbl", "chosenRoot",
       and "freshRoot". "pkgTbl" is a table of all of the names of ".main.ves"
       files in the repository, "chosenRoot" is true iff "root" was designated
       as a root to keep, and "freshRoot" is true iff the root should be
       kept because it is new enough. */

    // deletion phase methods -------------------------------------------------

    void DeletionPhase()
      throw (SRPC::failure, FS::Failure, VestaLog::Error, VestaLog::Eof);
    /* The weeder's deletion phase. Uses "this->roots" to decide which root
       nodes of the cache's graph log to keep. */

    PKPrefixTbl *WeededPrefixes() throw (VestaLog::Error, FS::Failure);
    /* Return a new table whose domain is the set of all PKPrefix values for
       those nodes in the graphLog that are to be weeded according to
       "this->weeded". */

    void PruneGraphLog()
      throw (FS::Failure, VestaLog::Error, VestaLog::Eof);
    /* Prune the graphLog, writing the new version to a checkpoint file with
       version number "this->newLogVer". Only graph log root nodes in the
       domain of "this->roots" are kept. */

    // stable variable methods ------------------------------------------------

    void Recover() throw (SRPC::failure, FS::Failure);
    /* Recover the stable variables and call "CacheW.WeederRecovering"
       to indicate that the weeder has just recovered. */

    void RecoverWeeded() throw (FS::Failure);
    /* Initialize "weeded" from stable storage. */

    void WriteWeeded() throw (FS::Failure);
    /* Write the current value of "weeded" to stable storage. */

    void RecoverMiscVars() throw (FS::Failure);
    /* Initialize "startTime", "disShortId", and "roots" from
       stable storage. */

    void WriteMiscVars() throw (FS::Failure);
    /* Write the current values of "startTime", "disShortId", and
       "roots" to stable storage. */

    void TimedMsg(char *msg, bool blankline = true,
      int level = CacheIntf::StatusMsgs) throw ();
    /* Write the message "msg" to the standard output if the debugging level
       is at or above "level". If "newline" is true, append an extra blank
       line. */
};

#endif // _WEEDER_H
