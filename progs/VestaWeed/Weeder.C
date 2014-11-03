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

// Created on Mon Mar  3 16:48:41 PST 1997 by heydon

#include <time.h>
#include <stdio.h>
#include <Basics.H>
#include <SRPC.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <VestaLogSeq.H>
#include <SourceOrDerived.H>
#include <CacheIntf.H>
#include <CacheConfig.H>
#include <BitVector.H>
#include <GraphLog.H>
#include <WeederC.H> // cache server weeder-client interface
#include <Debug.H>
#include <BufStream.H>
#include <AtomicFile.H>
#include <ReadConfig.H>

#include "PkgBuild.H"
#include "CommonErrors.H"
#include "WeedArgs.H"
#include "WeederConfig.H"
#include "RootTbl.H"
#include "GLNode.H"
#include "GLNodeBuffer.H"
#include "GatherWeedRoots.H"
#include "ReposRoots.H"
#include "Weeder.H"

using std::ios;
using std::ifstream;
using std::ofstream;
using std::cout;
using std::cin;
using std::cerr;
using std::endl;
using std::hex;
using std::dec;
using Basics::OBufStream;

// Misc -----------------------------------------------------------------------

void Weeder::TimedMsg(char *msg, bool blankline, int level) throw ()
{
    if (this->debug >= level) {
	cout << Debug::Timestamp() << msg << endl;
	if (blankline) cout << endl;
    }
}

// Derived ShortIds -----------------------------------------------------------

/* To reduce the number of redundant ShortIds written to the file of derived
   files to keep, we keep a table of ShortIds written recently. The hope is
   that there will be some locality to the redundant ShortIds.

   The table size is bounded. If the table is full and we need to add a new
   entry, we first remove the oldest entry in the table. This is done by
   keeping a queue that records the order in which DIs were added to the
   table. */

typedef Table<IntKey,char>::Default ShortIdTbl;

typedef Sequence<ShortId,true> ShortIdSeq;

static ShortIdTbl *shortIdTbl;
static ShortIdSeq *shortIdSeq;

static void WriteShortId(FILE *fp, ShortId id) throw (FS::Failure)
/* Write the ShortId "id" to "fp" on its own line in a format understood
   by the repository's derived weeder. */
{
    // put the entry in the table; return if the entry was already there
    IntKey key(id);
    if (shortIdTbl->Put(key, (char)true)) {
	return;
    }

    // remove an entry from the table if it is full
    if (shortIdTbl->Size() >= Config_DIBuffSize) {
	assert(shortIdTbl->Size() == Config_DIBuffSize);
	char dummy;
	ShortId oldId = shortIdSeq->remlo();
	bool inTbl = shortIdTbl->Delete(IntKey(oldId), /*OUT*/ dummy);
	assert(inTbl);
    }
    shortIdSeq->addhi(id);

    // write the entry
    int res = fprintf(fp, "%08x\n", id);
    if (res < 0) throw (FS::Failure("fprintf(3)"));
} // WriteShortId

static FILE *OpenShortIdFile(/*OUT*/ ShortId &disShortId)
  throw (FS::Failure, ReposError)
/* Create a new derived file, write the ShortId into the file itself, and
   return a "FILE *" on the opened file. Set "disShortId" to the ShortId of
   the new file. */
{
    // open file
    FILE *res = (FILE *)NULL;
    int fd;
    try {
	fd = SourceOrDerived::fdcreate(
          /*OUT*/ disShortId, /*leafflag=*/ true);
    } catch (SourceOrDerived::Fatal &f) {
	throw ReposError(VestaSource::noPermission, "fdcreate",f.msg.cchars());
    }
    if (fd == -1) {
	throw FS::Failure("SourceOrDerived::fdcreate");
    }
    if ((res = fdopen(fd, "w")) == (FILE *)NULL) {
	throw FS::Failure("fdopen(3)");
    }

    // initialize global data structures
    shortIdTbl =
      NEW_CONSTR(ShortIdTbl, (/*sizeHint=*/ Config_DIBuffSize, /*useGC=*/ true));
    shortIdSeq = NEW_CONSTR(ShortIdSeq, (/*sizeHint=*/ Config_DIBuffSize));

    // write the ShortId of the file itself to the file
    WriteShortId(res, disShortId);
    return res;
} // OpenShortIdFile

static void CloseShortIdFile(FILE *fp) throw (FS::Failure)
{
    if (fflush(fp) != 0) throw FS::Failure("fflush(3)");
    int fd = fileno(fp); assert(fd != -1);
    if (fsync(fd) != 0) throw FS::Failure("fsync(2)");
    if (fclose(fp) != 0) throw FS::Failure("fclose(3)");
}

// Weeding --------------------------------------------------------------------

Weeder::Weeder(CacheIntf::DebugLevel debug) throw (SRPC::failure, FS::Failure)
  : debug(debug), weeded(NULL), initCIs(NULL), marked(NULL),
    wLeases(NULL), glCIs(NULL), graphLogSeq(Config_GraphLogPath.chars())
{
  // initialize the cache client
  this->cache = NEW_CONSTR(WeederC, (debug));

  // recover
  this->Recover();
}

void Weeder::Recover() throw (SRPC::failure, FS::Failure)
{
    // recover stable state
    RecoverWeeded();
    if (!(this->weeded->IsEmpty())) {
	RecoverMiscVars();
    }

    // tell cache server we have recovered
    if (this->cache->WeederRecovering(!(this->weeded->IsEmpty()))) {
	cerr << "Fatal error: another weed is already in progress!" << endl;
	exit(1);
    }
}

static void Weeder_QueryDelStatus(/*INOUT*/ WeedArgs::DeletionStatus &stat)
  throw ()
/* If "stat" is "QueryDeletions", ask the user for confirmation, and set
   "stat" to either "WeedArgs::NoDeletions" or "WeedArgs::DoDeletions"
   depending on the response. */
{
    while (stat == WeedArgs::QueryDeletions) {
	cout << "Proceed to weeder deletion phase (yes/no)? ";
	cout.flush();
	const int BuffLen = 20; char buff[BuffLen];
	cin.getline(buff, BuffLen);
	if (strcmp(buff, "yes") == 0) {
	    stat = WeedArgs::DoDeletions;
	    cout << endl;
	} else if (strcmp(buff, "no") == 0) {
	    stat = WeedArgs::NoDeletions;
	    cout << endl;
	} else {
	    cout << "Please answer \"yes\" or \"no\"." << endl << endl;
	}
    }
} // Weeder_QueryDelStatus

bool Weeder::Weed(WeedArgs &args) throw (SRPC::failure,
  FS::Failure, FS::DoesNotExist, VestaLog::Error, VestaLog::Eof,
  InputError, SysError, ReposError)
{
    bool resumed = false;
    this->args = &args;

    // mark phase
    if (this->weeded->IsEmpty()) {
        if (args.noNew) {
	    cout << "No new weed instructions given; exiting" << endl;
            exit(0);
        }
	if (this->debug >= CacheIntf::StatusMsgs) {
	    TimedMsg("Starting weeder mark phase");
	}
	this->MarkPhase();
	if (this->debug >= CacheIntf::StatusMsgs) {
	    TimedMsg("Finished weeder mark phase", /*blankline=*/ false);
	    cout << "  total entries marked = "
                 << this->markedCntTotal << endl;
	    cout << "  total entries to be weeded = "
                 << this->weeded->Cardinality() << endl;
	    cout << endl;
	}

	// write misc vars to see disShortId
	WriteMiscVars(); // must be done *before* "WriteWeeded"

	// ask for confirmation if necessary
	Weeder_QueryDelStatus(/*INOUT*/ args.delStatus);

	if (args.delStatus == WeedArgs::DoDeletions) {
	    // stably log weeded set
	    WriteWeeded();   // commit point for end of mark phase
	}
    } else {
	cout <<"*** NOTE: Resuming previous failed weed."<<endl;
        resumed = true;

	// ask for confirmation if necessary
	Weeder_QueryDelStatus(/*INOUT*/ args.delStatus);
    }

    // deletion phase
    if (args.delStatus == WeedArgs::DoDeletions) {
	if (this->debug >= CacheIntf::StatusMsgs) {
	    TimedMsg("Starting weeder deletion phase");
	}
	this->DeletionPhase();
	if (this->debug >= CacheIntf::StatusMsgs) {
	    TimedMsg("Finished weeder deletion phase");
	}
    } else {
        // If there was an incomplete weed, it's still incomplete
        resumed = false;
    }
    return resumed;
} // Weed

// Marking phase --------------------------------------------------------------

void Weeder::MarkPhase() throw (SRPC::failure, FS::Failure,
  FS::DoesNotExist, VestaLog::Error, VestaLog::Eof, InputError,
  SysError, ReposError)
{
    // record & log start time for purposes of derived weeding
    (void) time(&(this->startTime)); // must be done *before* "StartMark"

    // Subtract a configurable amount of time from the start time.
    int startGracePeriod = 60;
    (void) ReadConfig::OptIntVal(Config_WeederSection, "GracePeriod",
				 startGracePeriod);
    if(startGracePeriod > 0)
      {
	this->startTime -= startGracePeriod;
      }

    // start the marking phase
    this->initCIs = cache->StartMark(/*OUT*/ this->newLogVer);

    // record the keep time
    (void) time(&(this->keepTime)); // must be done *after* "StartMark"
    this->keepTime -= this->args->keepSecs;

    // read the weeder instructions
    this->instrRoots = NEW(RootTbl);
    try {
	GatherWeedRoots::P(this->args->instrFile,
	  this->args->globInstrs, /*OUT*/ this->instrRoots);
    } catch (...) { this->cache->ResumeLeaseExp(); throw; }

    // do requested work on graphLog
    this->MarkWork();
} // MarkPhase

static void UnlinkFile(const Text &filename) throw ()
/* Invoke the unlink(2) command on "filename". Errors
   are (intentionally) ignored. */
{ (void) unlink(filename.chars()); }

void Weeder::MarkWork() throw (SRPC::failure, FS::Failure, VestaLog::Error,
  VestaLog::Eof, SourceOrDerived::Fatal, ReposError)
{
    BitVector *leasedCIs;
    try {
      this->marked = NEW_CONSTR(BitVector, (/*sizeHint=*/ this->initCIs->Size()));
      this->nodeBuff =
	NEW_CONSTR(GLNodeBuffer, (/*maxSize=*/ Config_GLNodeBuffSize));
      this->markedCntTotal = 0;

      // open file to which derived ShortId's to keep will be written
      this->disFP = OpenShortIdFile(/*OUT*/ this->disShortId);

      // scan the repository for all models if requested
      PkgTbl *pkgTbl = (PkgTbl *)NULL;
      if (this->args->printRoots) {
	pkgTbl = ReposRoots::Scan(this->debug >= CacheIntf::StatusMsgs);
      }

      // pre debugging
      if (this->debug >= CacheIntf::StatusMsgs) {
	TimedMsg("Starting to mark cache entries reachable from roots");
      }

      // copy graphLog to "pendingGL", marking "roots"
      this->markedRoots = NEW(RootTbl);
      this->CopyGLtoPending(pkgTbl);

      // we no longer need the "instrRoots", so drop them on the floor
      this->instrRoots = (RootTbl *)NULL;

      // mark everything reachable from the roots
      while (this->markedCnt > 0) {
	this->markedCntTotal += this->markedCnt;
	this->ScanLogOnce();
      }

      // post debugging
      if (this->debug >= CacheIntf::StatusMsgs) {
	TimedMsg("Finished marking cache entries reachable from roots",
		 /*blankline=*/ false);
	cout << "  entries marked = " << this->markedCntTotal << endl;
	cout << endl;
      }

      // set the cache's hitFilter
      BitVector *toDelete = NEW_CONSTR(BitVector, (this->initCIs));
      *(toDelete) -= *(this->marked);
      this->cache->SetHitFilter(*toDelete);
      toDelete = (BitVector *)NULL; // drop on floor for GC
    
      // get currently leased entries
      leasedCIs = this->cache->GetLeases();
    } catch (...) {
	// resume lease expiration in the event of a failure
	this->cache->ResumeLeaseExp();
	throw;
    }

    // pre debugging
    int preLeaseTotal = this->markedCntTotal;
    if (this->debug >= CacheIntf::StatusMsgs) {
	TimedMsg("Started marking entries reachable from leased entries");
    }

    // mark everything reachable from leased entries
    this->cache->ResumeLeaseExp();
    if (this->debug >= CacheIntf::StatusMsgs) {
	TimedMsg("Started marking leased cache entries");
    }
    this->markedCnt = 0;
    {   // new scope for locals
	BVIter it(*leasedCIs);
	CacheEntry::Index ci;
	while (it.Next(/*OUT*/ ci)) {
	    this->MarkNode(ci);
	}
    }
    if (this->debug >= CacheIntf::StatusMsgs) {
	TimedMsg("Finished marking leased cache entries", /*blankline=*/false);
	cout << "  entries marked = " << this->markedCnt << endl << endl;
    }
    while (this->markedCnt > 0) {
	this->markedCntTotal += this->markedCnt;
	this->ScanLogOnce();
    }

    // post debugging
    if (this->debug >= CacheIntf::StatusMsgs) {
	TimedMsg("Finished marking entries reachable from leased entries",
		 /*blankline=*/ false);
	int numMarked = this->markedCntTotal - preLeaseTotal;
	cout << "  entries marked = " << numMarked << endl << endl;
    }

    // Check for non-leased marked CIs which didn't have graph log
    // entries.  If there are any, it's big trouble because we can't
    // protect their child CIs, derived files, etc.  (It's OK for
    // leased entries to not have GL entries processed by the weeder,
    // as they may be new.  However, we wait until after processing
    // the leased entries becuase non-leased entries reachable from
    // leased entries *must* have GL entries.)
    {
      BitVector *marked_not_leased = *(this->marked) - *leasedCIs;
      leasedCIs = (BitVector *)NULL; // drop on floor for GC
      BitVector *marked_missing_gl = *marked_not_leased - *(this->glCIs);
      marked_not_leased = (BitVector *)NULL; // drop on floor for GC
      this->glCIs = (BitVector *)NULL;       // drop on floor for GC
      if(marked_missing_gl->Cardinality() > 0)
	{
	  cerr << Debug::Timestamp() << "Fatal error:" << endl
	       << "  Cache inconsistency detected: non-leased marked CI(s)"
	       << endl
	       << "   with no GL entry!" << endl
	       << endl
	       << "   cis missing GL entries = " << endl;
	  marked_missing_gl->PrintAll(cerr, 6);
	  cerr << "     (" << marked_missing_gl->Cardinality() << " total)"
	       << endl << endl;
	  exit(1);
	}
      marked_missing_gl = (BitVector *)NULL; // drop on floor for GC
    }

    // compute "this->weeded"
    this->weeded = this->initCIs;       // alias to "this->initCIs"
    this->initCIs = (BitVector *)NULL;  // drop on floor for GC
    *(this->weeded) -= *(this->marked); // subtract off marked entries
    this->marked = (BitVector *)NULL;   // drop on floor for GC

    // close derived ShortId file
    CloseShortIdFile(this->disFP);

    // drop structures on floor for GC
    shortIdTbl = (ShortIdTbl *)NULL;
    shortIdSeq = (ShortIdSeq *)NULL;
    this->disFP = (FILE *)NULL;

    // free file and memory resources
    this->nodeBuff = (GLNodeBuffer *)NULL;
    UnlinkFile(Config_PendingGLFile);
    UnlinkFile(Config_WorkingGLFile);
} // MarkWork

void Weeder::ScanLogOnce() throw (FS::Failure)
{
    // pre debugging
    if (this->debug >= CacheIntf::WeederScans) {
	TimedMsg("Starting scan of pending graph log");
    }
    int nodesRead = 0;

    // open "workingGL" on current "pendingGL"
    ifstream workingGL;
    char *pendingGLFile = Config_PendingGLFile.chars();
    char *workingGLFile = Config_WorkingGLFile.chars();
    if (rename(pendingGLFile, workingGLFile) < 0) {
	throw FS::Failure("rename(2)", Config_PendingGLFile);
    }
    FS::OpenReadOnly(workingGLFile, /*OUT*/ workingGL);
    try {

    // open new "pendingGL" for writing
    FS::OpenForWriting(pendingGLFile, /*OUT*/ this->pendingGL);
    try {

    // scan
    this->markedCnt = 0;
    this->nodeBuff->flushedCnt = 0;
    while (!FS::AtEOF(workingGL)) {
	GLNode node(workingGL);
	nodesRead++;
	if (this->marked->Read(node.ci)) {
	    this->ProcessNode(node);
	} else {
	    this->nodeBuff->Put(node, this->pendingGL);
	}
    }

    // close "pendingGL"
    } catch (...) { FS::Close(pendingGL); throw; }
    FS::Close(pendingGL);

    // close "workingGL"
    } catch (...) { FS::Close(workingGL); throw; }
    FS::Close(workingGL);

    // post debugging
    if (this->debug >= CacheIntf::WeederScans) {
	TimedMsg("Finished scan of pending graph log", /*blankline=*/ false);
	cout << "  nodes read = " << nodesRead << endl;
	cout << "  entries marked = " << this->markedCnt << endl;
	cout << "  nodes written = " << this->nodeBuff->flushedCnt << endl;
	cout << endl;
    }
} // ScanLogOnce

void Weeder::CopyGLtoPending(const PkgTbl *pkgTbl)
  throw (FS::Failure, VestaLog::Error)
{
    int writtenCnt = 0;
    bool unused_cis = false;
    this->markedCnt = 0;

    // pre debugging
    if (this->debug >= CacheIntf::WeederScans) {
	TimedMsg("Starting scan of graph log");
    }
    int rootCnt = 0, nodeCnt = 0;
    if (this->args->printRoots) {
	cout << "Disposition of GraphLog roots:" << endl;
	assert(pkgTbl != (PkgTbl *)NULL);
    }

    // Allocate a new BitVector to collect the set of CIs in the graph
    // log.
    this->glCIs = NEW_CONSTR(BitVector, (/*sizeHint=*/ this->initCIs->Size()));

    // open "pendingGL" file (for writing)
    FS::OpenForWriting(Config_PendingGLFile, /*OUT*/ this->pendingGL);
    try {

    // open log
    this->graphLogSeq.Open(/*version=*/ -1, /*readonly=*/ true);
    try {

    /* roots whose dispositions have been printed; only used if
       "this->args->printRoots". Note that we have to keep two tables
       because a root may be printed with both a '-' early in the
       log and a '>' (freshness indicator) later in the log. */
    RootTbl unkeptPrintedRootsTbl, keptPrintedRootsTbl;

    // copy "logSeq" to "pendingGL"
    RecoveryReader *rd;
    int lastLogVer = -1;
    while ((rd = this->graphLogSeq.Next(this->newLogVer)) != NULL) {
        lastLogVer = this->graphLogSeq.CurLogVersion();
	while (!rd->eof()) {
	    GraphLog::Entry *entry = GraphLog::Entry::Recover(*rd);
	    switch (entry->kind) {
	      case GraphLog::RootKind:
		{ // see if this root is supposed to be kept
		  GraphLog::Root *root = (GraphLog::Root *)entry;
		  PkgBuild pkgBuild(root->pkgVersion, root->model);
		  bool dummy;
		  bool inTbl = this->instrRoots->Get(pkgBuild, /*OUT*/ dummy);
		  bool fresh = (root->ts >= this->keepTime);
		  bool keptRoot = (inTbl || fresh);
		  if (this->args->printRoots) {
		      // print it if it has not yet been printed
		      RootTbl *tbl = keptRoot
                        ? &keptPrintedRootsTbl
                        : &unkeptPrintedRootsTbl;
		      if (!tbl->Get(pkgBuild, /*OUT*/ dummy)) {
			  this->PrintRoot(pkgBuild, *pkgTbl, inTbl, fresh);
			  bool inTbl2 = tbl->Put(pkgBuild, false);
			  assert(!inTbl2);
		      }
		  }

		  // roots are kept either because they are in the
		  // instructions or because they are deemed fresh enough
		  if (keptRoot)
		    {
		      // if so, mark its corresponding CIs
		      CacheEntry::Indices *cis = root->cis;
		      for (int i = 0; i < cis->len; i++) {
			  this->MarkNode(cis->index[i]);
		      }

		      // add it to the table of marked roots, noting
		      // whether it was explicitly requested.
		      (void) this->markedRoots->Put(pkgBuild, inTbl);
		    }

		  // Check whether any of this root's CIs are
		  // unused.  If they are, this indicates an
		  // inconsistency (probably a cache or weeder
		  // bug).
		  for (int i = 0; i < root->cis->len; i++)
		    {
		      if(!this->initCIs->Read(root->cis->index[i]))
			{
			  // Print the "fatal error" banner only if we
			  // haven't already done so.
			  if(!unused_cis)
			    {
			      cerr << Debug::Timestamp() << "Fatal error:"
				   << endl;
			    }
			  // Print a text message about the problem(s)
			  // detected.
			  cerr << ("  Cache inconsistency detected: graph"
				   " log root with unused CI(s)!")
			       << endl << endl;
			  // Dump the complete entry.
			  entry->DebugFull(cerr);
			  cerr << endl;
			  // Remember that we've encountered an entry
			  // wih unused CIs.
			  unused_cis = true;
			  // Don't bother checking the rest of the CIs
			  // referenced by this root.
			  break;
			}
		    }
	        }
		rootCnt++;
		break;
	      case GraphLog::NodeKind:
		{
		  // write out the relevant fields as a "GLNode"
                  GraphLog::Node *node_entry = (GraphLog::Node *)entry;
		  GLNode node(*node_entry);
		  node.Write(this->pendingGL);
		  // Remember that there was a GL entry for this CI.
		  this->glCIs->Set(node.ci);
		  writtenCnt++;
		  nodeCnt++;

		  // Check whether this node's CI is unused, and
		  // whether any of this node's child CIs are unused.
		  // Unused CIs indicates an inconsistency (probably a
		  // cache or weeder bug).
		  bool self_unused = !this->initCIs->Read(node.ci),
		    kids_unused = false;
		  for (int i = 0; i < node.kids->len; i++)
		    {
		      if(!this->initCIs->Read(node.kids->index[i]))
			{
			  kids_unused = true;
			  break;
			}
		    }
		  if(self_unused || kids_unused)
		    {
		      // Print the "fatal error" banner only if we
		      // haven't already done so.
		      if(!unused_cis)
			{
			  cerr << Debug::Timestamp() << "Fatal error:"
			       << endl;
			}
		      // Print a text message about the problem(s)
		      // detected.
		      cerr << ("  Cache inconsistency detected: graph log"
			       " node with") << endl;
		      if(self_unused)
			{
			  cerr << "an unused CI";
			}
		      if(self_unused && kids_unused)
			{
			  cerr << " and ";
			}
		      if(kids_unused)
			{
			  cerr << "unused child CI(s)";
			}
		      cerr << "!" << endl << endl;
		      // Dump the complete entry.
		      entry->DebugFull(cerr);
		      cerr << endl;
		      // Remember that we've encountered an entry wih
		      // unused CIs.
		      unused_cis = true;
		    }
	        }
		break;
	      default:
		assert(false);
	    }
	}
    }

    // The last .log file we read from the graphLog should be just
    // before newLogVer, which the cache server told us and is the
    // .ckp file we will write later.  If that's not true, we may be
    // reading the graphLog from one cache and talking to a different
    // cache over the network, which will cause cache corruption.
    if(lastLogVer != (this->newLogVer - 1))
      {
	cerr << Debug::Timestamp() << "Fatal error:"
	     << endl
	     << "  Cache graph log doesn't seem to match cache server" << endl
	     << "  (Maybe the filesystem doesn't match the cache server?)" << endl
	     << endl
	     << "  last log read     = " << lastLogVer << endl
	     << "  expected last log = " << (this->newLogVer-1) << endl;
	exit(1);
      }

    // If we found entries with unused CIs, die.  We wait until now so
    // that if there's more than one we will print them all.
    if(unused_cis)
      {
	exit(1);
      }

    if (this->args->printRoots) {
	if (rootCnt == 0) cout << "  NONE FOUND" << endl;
	cout << endl;
    }

    // close log
    } catch (...) {
        if (this->args->printRoots) {
	    if (rootCnt == 0) cout << "  NONE FOUND SO FAR" << endl;
	    cout << endl;
        }
        this->graphLogSeq.Close();
        throw;
    }
    this->graphLogSeq.Close();

    // close "pendingGL" file
    } catch (...) { FS::Close(pendingGL); throw; }
    FS::Close(pendingGL);

    // post debugging
    if (this->debug >= CacheIntf::WeederScans) {
	TimedMsg("Finished scan of graph log", /*blankline=*/ false);
	cout << "  roots read = " << rootCnt << endl;
	cout << "  nodes read = " << nodeCnt << endl;
	cout << "  entries marked = " << this->markedCnt << endl;
	cout << "  nodes written = " << writtenCnt << endl;
	cout << endl;
    }
} // CopyGLtoPending

void Weeder::MarkNode(CacheEntry::Index ci) throw (FS::Failure)
{
    if (!(this->marked->Set(ci))) {
	// this node was previously unmarked
	this->markedCnt++;

	// if the node for "ci" is in the buffer, remove & process it
	GLNode node;
	if (this->nodeBuff->Delete(ci, /*OUT*/ node)) {
	    this->ProcessNode(node);
	}
    }
}

void Weeder::ProcessNode(const GLNode &node) throw (FS::Failure)
{
    register int i;

    // mark each of the child entries (if any)
    CacheEntry::Indices *cis = node.kids;
    for (i = 0; i < cis->len; i++) {
	this->MarkNode(cis->index[i]);
    }

    // write the deriveds reachable from this node (if any)
    Derived::Indices *dis = node.refs;
    for (i = 0; i < dis->len; i++) {
	WriteShortId(this->disFP, dis->index[i]);
    }
}

void Weeder::PrintRoot(const PkgBuild &root, const PkgTbl &pkgTbl,
  bool chosenRoot, bool freshRoot) throw ()
{
    // print prefix
    cout << "  " << (chosenRoot ? '+' : (freshRoot ? '>' : '-')) << ' ';

    // print result
    Pathname::T path;
    if (pkgTbl.Get(root, /*OUT*/ path)) {
	Pathname::Print(cout, path);
    } else {
	// no match for an exact model; see if we can find a directory
	PkgBuild dir(root.pkgDirFP, NullShortId);
	if (pkgTbl.Get(dir, /*OUT*/ path)) {
	    Pathname::Print(cout, path);
	    char buffer[20];
	    int spres = sprintf(buffer, "%08x", root.modelShortId);
	    assert(spres >= 0);
	    cout << "; modelShortId = 0x" << buffer;
	} else {
	    // we don't have a pathname for this model, so print a "raw" form
	    root.Print(cout);
	}
    }
    cout << endl;
}

// Deleting phase -------------------------------------------------------------

void Weeder::DeletionPhase()
  throw (SRPC::failure, FS::Failure, VestaLog::Error, VestaLog::Eof)
{
    /* Interaction with the cache is necessary only if there are
       some cache entries to weed. */

    if (!(this->weeded->IsEmpty())) {
	// form table of PKPrefixes containing "weeded" entries
	PKPrefixTbl *pfxTbl = WeededPrefixes();

	// call cache server's "EndMark" method
	this->newLogVer = this->cache->EndMark(*(this->weeded), *pfxTbl);
	pfxTbl = (PKPrefixTbl *)NULL; // drop on floor for GC
    }

    // weed derived files -------------------------------------------------

    // start repository source/derived weed
    TimedMsg("Started deleting dead repository files");
    int derRes =
      SourceOrDerived::keepDerived(this->disShortId, this->startTime);
    TimedMsg("Finished deleting dead repository files");

    if (!(this->weeded->IsEmpty())) {
	// write graphLog checkpoint & commit it
	this->PruneGraphLog();
	try
	  {
	    bool l_chkpt_accepted =
	      this->cache->CommitChkpt(this->logChkptFName);

	    // If the cache server rejects our graph log checkpoint,
	    // something is very wrong.
	    if(!l_chkpt_accepted)
	      {
		cerr << Debug::Timestamp() << "Fatal error:" << endl;
		cerr << "  graph log checkpoint rejected by cache server"
		     << endl;
		cerr << endl;
		exit(1);
	      }
	  }
	// If anything goes wrong with committing the checkpoint we
	// wrote, delete it.
	catch(...)
	  {
	    try
	      { FS::Delete(this->logChkptFName); }
	    catch(FS::Failure)
	      { /* We don't really care if that operation failed. */ }

	    // Re-throw whatever went wrong with committing the
	    // checkpoint
	    throw;
	  }

	// reset "weeded"
	this->weeded->ResetAll(/*freeMem=*/ true);
	this->WriteWeeded();
    }

    // if there were no errors, checkpoint the repository
    if (derRes != 0) {
	cerr << Debug::Timestamp() << "Fatal repository error" << endl;
	cerr << "  Derived weed error = " << derRes << endl;
	cerr << endl;
    } else {
	TimedMsg("Started checkpointing the repository");
	SourceOrDerived::checkpoint();
	TimedMsg("Finished checkpointing the repository");
    }
}

PKPrefixTbl *Weeder::WeededPrefixes() throw (VestaLog::Error, FS::Failure)
{
  // allocate result
  PKPrefixTbl *res = NEW_CONSTR(PKPrefixTbl, (/*sizeHint=*/ 100));

  // open graphLog
  this->graphLogSeq.Open(/*ver=*/ -1, /*readOnly=*/ true);
  try {

    // copy "logSeq" to "pendingGL"
    RecoveryReader *rd;
    while ((rd = this->graphLogSeq.Next(this->newLogVer)) != NULL) {
      while (!rd->eof()) {
	GraphLog::Entry *entry = GraphLog::Entry::Recover(*rd);
	if (entry->kind == GraphLog::NodeKind) {
	  GraphLog::Node *node = (GraphLog::Node *)entry;
	  if (this->weeded->Read(node->ci)) {
	    PKPrefix::T pfx(*(node->loc));
	    (void) res->Put(pfx, (char)true);
	  }
	}
      }
    }

    // close graphLog
  } catch (...) { this->graphLogSeq.Close(); throw; }
  this->graphLogSeq.Close();
  return res;
}

void Weeder::PruneGraphLog()
  throw (FS::Failure, VestaLog::Error, VestaLog::Eof)
{
    // pre debugging
    if (this->debug >= CacheIntf::LogFlush) {
	TimedMsg("Started pruning graph log");
    }
    int nodeCnt = 0, rootCnt = 0;

    // Figure out the name we will write the checkpoint to.  We append
    // a suffix to the "real" checkpoint filename in order to cover a
    // potentially problematic situation: two weeders simultaneously
    // trying to checkpoint the same cache server's graph log.
    Text chkptFileName, rel_chkptFileName;
    time_t now = time(0);
    bool l_exists;
    do
      {
	OBufStream l_temp_fname;
	l_temp_fname << dec << this->newLogVer << ".ckp"
	             << AtomicFile::reserved_char << hex << now;

	rel_chkptFileName = l_temp_fname.str();
	chkptFileName = Config_GraphLogPath + '/' + rel_chkptFileName;
	
	// We may need to create a different temporary filename, so
	// bump the number we're using.

	now++;
      }
    // Test to make sure that there isn't already a file with this
    // name.  This is a little on the paranoid side, but since
    // we're trying to prevent a problem with two weeders doing
    // this simultaneously, it seems worthwhile.
    while(FS::Exists(chkptFileName));

    // table of roots already written to the graph log, with the value
    // set to true when we've written the 'done' root.
    RootTbl writtenRootsTbl;

    // open the checkpoint
    ofstream chkpt;
    FS::OpenForWriting(chkptFileName, /*OUT*/ chkpt);
    // Remember the checkpoint filename, as we'll need it to pass to
    // the cache server when we call CommitChkpt.
    this->logChkptFName = rel_chkptFileName;
    try {

    // open the graphLog
    this->graphLogSeq.Open(/*ver=*/ -1, /*readOnly=*/ true);
    try {

    // read the graphLog, copying relevant nodes to "chkpt"
    RecoveryReader *rd;
    while ((rd = this->graphLogSeq.Next(this->newLogVer)) != NULL) {
	while (!rd->eof()) {
	    GraphLog::Entry *entry = GraphLog::Entry::Recover(*rd);
	    switch (entry->kind) {
	      case GraphLog::RootKind:
		{ // start new scope
		  GraphLog::Root *root = (GraphLog::Root *)entry;
		  PkgBuild pkgBuild(root->pkgVersion, root->model);
		  // The value associated with each PkgBuild in
		  // this->markedRoots tells us whether it was
		  // explicitly kept or by a keep duration.
		  bool explicitlyKept = false;
		  bool inRootTbl = this->markedRoots->Get(pkgBuild,
							  explicitlyKept);
		  // The value in writtenRootsTbl represents whether
		  // we've written the final root for this pkgBuild
		  // (the one with done = true).  (It's possible that
		  // a pkgBuild might have no such root, if the
		  // evaluator never completed.)
		  bool doneWritingRoot = false;
		  bool inWrittenTbl = writtenRootsTbl.Get(pkgBuild,
							  doneWritingRoot);
		  // We will keep this root if it was explicitly kept
		  // or it was kept by freshness and is within the
		  // keep duration, as long as we haven't already
		  // finished writing the full set of roots for this
		  // build.
		  if ((inRootTbl && (explicitlyKept ||
				     (root->ts >= this->keepTime))) &&
		      (!inWrittenTbl || !doneWritingRoot)) {
		      // make sure none of its kids was deleted
		      CacheEntry::Indices *cis = root->cis;
		      for (int i = 0; i < cis->len; i++) {
			  assert(!this->weeded->Read(cis->index[i]));
		      }

		      // write it out
		      root->Write(chkpt);
		      rootCnt++;

		      // add it to the writtenRootsTbl if neccessary,
		      // noting whether it is done
		      if (root->done)
			{
		          writtenRootsTbl.Put(pkgBuild, true);
			}
		      else if(!inWrittenTbl)
			{
		          inWrittenTbl = writtenRootsTbl.Put(pkgBuild, false);
			  assert(!inWrittenTbl);
			}
		  }
		}
		break;
	      case GraphLog::NodeKind:
		{ // local scope
                  GraphLog::Node *node = (GraphLog::Node *)entry;
		  if (!(this->weeded->Read(node->ci))) {
		      // this entry was not weeded, so write it
		      /* Note: once cutoffs are supported, we have to
                       	 form a new node that does not include the children
                       	 that were weeded. For now, though, so long as the
                       	 node itself was not weeded, none of its children
                       	 was weeded either. */
		      node->Write(chkpt);
		      nodeCnt++;
		  }
		}
		break;
	      default:
		assert(false);
	    }
	}
    }

    // close graphLog
    } catch (...) { this->graphLogSeq.Close(); throw; }
    this->graphLogSeq.Close();
   
    // close the checkpoint
    } catch (...) { FS::Close(chkpt); throw; }
    FS::Close(chkpt);

    // Now that we've finished, we need to do a little sanity
    // checking.  It's possible that someone erased the cache but
    // *not* the weeder's metadata.  If that's the case the marked
    // roots stored stably won't have been in the graph log we're
    // pruning, and in fact we may have just erroneously pruned almost
    // everything from the graph log.  Before comitting the
    // checkpoint, we check for this case.
    if (this->debug >= CacheIntf::LogFlush) {
	TimedMsg("Checking marked vs. written roots", /*blankline=*/ true);
    }

    Table<PkgBuild,bool>::Iterator marked_iterator(this->markedRoots);
    PkgBuild pkgBuild;
    bool dummy;
    // Loop over all the marked roots
    while(marked_iterator.Next(pkgBuild, dummy))
    {
      // Check whether this one was written to the graph log
      // checkpoint.
      bool inWrittenTbl = writtenRootsTbl.Get(pkgBuild, dummy);
      // If this one was never written, and thus never seen in the
      // graph log...
      if(!inWrittenTbl)
	{
	  // Print out the non-matching sets of roots
	  TimedMsg("Mismatch ebtween marked and written roots",
		   /*blankline=*/ true);
	  cout << "  marked roots = " << endl;
	  this->markedRoots->Print(cout, /*indent=*/ 4,
				   "[explicit]", "[fresh]");
	  cout << "  written roots = " << endl;
	  writtenRootsTbl.Print(cout, /*indent=*/ 4,
				"[done]", "[not done]");
	  cout << endl;

	  // Print out an explanation and then exit with error status.
	  cerr << Debug::Timestamp() << "Fatal error:" << endl
	       << "  some marked roots never seen while" << endl
	       << "  checkpointing graph log!" << endl
	       << endl
	       << "  *** This may mean that the cache metadata was"   << endl
	       << "      erased, but the weeder metadata was not."    << endl
	       << "      In other words, this may be the resumption"  << endl
	       << "      of a previously failed weed against a"       << endl
	       << "      different cache than the original weed"      << endl
	       << "      was run against."                            << endl
	       << endl
	       << "  *** If this was not the resumption of a "        << endl
	       << "      previously failed weed, then there's a bug." << endl
	       << endl
	       << "  *** If you don't understand that, you should"    << endl
	       << "      erase your weeder metadata with:"            << endl
	       << endl
	       << "      rm " << Config_WeederMDPath << "/*" << endl
	       << endl
	       << "      and re-run the weeder." << endl;
	  exit(1);
	  
	}
    }

    // post debugging
    if (this->debug >= CacheIntf::LogFlush) {
	TimedMsg("Finished pruning graph log", /*blankline=*/ false);
	cout << "  roots written = " << rootCnt << endl;
	cout << "  nodes written = " << nodeCnt << endl;
	cout << endl;
    }
}

// Stable variables -----------------------------------------------------------

void Weeder::RecoverWeeded() throw (FS::Failure)
{
    // debugging start
    if (this->debug >= CacheIntf::LogRecover) {
	TimedMsg("Recovering `weeded'", /*blankline=*/ false);
    }

    // actual work
    ifstream ifs;
    try {
	FS::OpenReadOnly(Config_WeededFile, /*OUT*/ ifs);
	this->weeded = NEW_CONSTR(BitVector, (ifs));
	FS::Close(ifs);
    }
    catch (FS::DoesNotExist) {
      this->weeded = NEW(BitVector);
    }
    catch (FS::EndOfFile) {
	// programming error
	assert(false);
    }

    // debugging end
    if (this->debug >= CacheIntf::LogRecover) {
	cout << "  weeded = " << *(this->weeded) << "" << endl;
	cout << endl;
    }
}

void Weeder::WriteWeeded() throw (FS::Failure)
{
    // debugging start
    if (this->debug >= CacheIntf::LogFlush) {
	TimedMsg("Logging `weeded'", /*blankline=*/ false);
	cout << "  weeded = " << endl;
	this->weeded->PrintAll(cout, 6);
	cout << "    (" << this->weeded->Cardinality() << " total)" << endl
	     << endl;
    }

    // actual work
    AtomicFile ofs;
    ofs.open(Config_WeededFile.chars(), ios::out);
    if (ofs.fail()) {
	throw (FS::Failure(Text("Weeder::WriteWeeded"), Config_WeededFile));
    }
    this->weeded->Write(ofs);
    FS::Close(ofs); // swing file pointer atomically
}

void Weeder::RecoverMiscVars() throw (FS::Failure)
{
    // debugging start
    if (this->debug >= CacheIntf::LogRecover) {
	TimedMsg("Recovering miscellaneous variables", /*blankline=*/ false);
    }

    // actual work
    ifstream ifs;
    try {
	FS::OpenReadOnly(Config_MiscVarsFile, /*OUT*/ ifs);

	// Read the startTime/keepTime timestamps as 32-bit integers
	// to maintain compatibility between 32-bit and 64-bit
	// platforms.
	Basics::int32 l_ts;
	FS::Read(ifs, (char *)(&l_ts), sizeof_assert(l_ts, 4));
	this->startTime = l_ts;
	FS::Read(ifs, (char *)(&l_ts), sizeof_assert(l_ts, 4));
	this->keepTime = l_ts;

	FS::Read(ifs, (char *)(&(this->disShortId)), sizeof(this->disShortId));
	this->markedRoots = NEW_CONSTR(RootTbl, (ifs));
	FS::Close(ifs);
    }
    catch (FS::DoesNotExist) {
	this->startTime = -1;
    }
    catch (FS::EndOfFile) {
	// programming error
	assert(false);
    }

    // debugging end
    if (this->debug >= CacheIntf::LogRecover) {
	cout << "  startTime = " << this->startTime << endl;
	cout << "  disShortId = 0x" << hex << this->disShortId << dec << endl;
	cout << "  markedRoots =" << endl;
	cout << "    size = " << this->markedRoots->Size() << endl;
        if (this->markedRoots->Size() > 0) {
	    cout << "    content =" << endl;
	    this->markedRoots->Print(cout, /*indent=*/ 6,
				     "[explicit]", "[fresh]");
	}
	cout << endl;
    }
} // Weeder::RecoverMiscVars

void Weeder::WriteMiscVars() throw (FS::Failure)
{
    // debugging start
    if (this->debug >= CacheIntf::LogFlush) {
	TimedMsg("Logging miscellaneous variables", /*blankline=*/ false);
	cout << "  startTime = " << this->startTime << endl;
	cout << "  disShortId = 0x" << hex << this->disShortId << dec << endl;
	cout << "  markedRoots =" << endl;
	cout << "    size = " << this->markedRoots->Size() << endl;
        if (this->markedRoots->Size() > 0) {
	    cout << "    content =" << endl;
	    this->markedRoots->Print(cout, /*indent=*/ 6,
				     "[explicit]", "[fresh]");
	}
	cout << endl;
    }

    // actual work
    AtomicFile ofs;
    ofs.open(Config_MiscVarsFile.chars(), ios::out);
    if (ofs.fail()) {
	throw FS::Failure(Text("Weeder::WriteMiscVars"), Config_MiscVarsFile);
    }

    // Write the startTime/keepTime timestamps as 32-bit integers to
    // maintain compatibility between 32-bit and 64-bit platforms.
    Basics::int32 l_ts = this->startTime;
    FS::Write(ofs, (char *)(&l_ts), sizeof_assert(l_ts, 4));
    l_ts = this->keepTime;
    FS::Write(ofs, (char *)(&l_ts), sizeof_assert(l_ts, 4));

    FS::Write(ofs, (char *)(&(this->disShortId)), sizeof(this->disShortId));
    this->markedRoots->Write(ofs);

    // swing file pointer atomically
    FS::Close(ofs);
} // Weeder::WriteMiscVars
