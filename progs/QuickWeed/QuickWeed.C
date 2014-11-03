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


/* This program does a quick weed of derived files. It does so
   by treating all of the cache entries in the cache server as
   roots of the weed. Hence, it does not cause the cache server
   to delete any cache entries, and it keeps all derived files
   stored in all cache entries in the cache.

   The program works by recording the current time, invoking
   the cache server's checkpoint method to flush the graphLog,
   and then reading the graphlog to select out all the ShortIDs
   of all derived files mentioned in values in the graphLog. It
   then calls the appropriate repository methods to weed all
   deriveds except for the ones it found. */

/* Note: The quick weeder does not call several of the cache server's
   weeding methods, such as "SetHitFilter", "GetLeases", "EndMark", and
   "CommitChkpt". Mainly this is because the quick weeder does not
   actually delete any cache entries (or deriveds reachable from them)
   that appear in the graph log. As a result, the quick weeder does not
   have to re-write the graph log. Since it does not call the "CommitChkpt"
   method, the checkpoint started by "StartMark" will never be committed,
   which is desired, since the previous checkpoint plus all subsequent logs
   should still be taken to constitute the entire graph log after QuickWeed
   has run. The next time the cache server's "StartMark" method is called,
   any outstanding graph log checkpoint is aborted. */

#include <Basics.H>
#include <Generics.H>
#include <VestaConfig.H>
#include <SRPC.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <VestaLogSeq.H>
#include <FP.H>
#include <SourceOrDerived.H>
#include <ReadConfig.H>
#include <BitVector.H>
#include <PKPrefix.H>
#include <WeederC.H>
#include <Derived.H>
#include <ParCacheC.H>
#include <Debug.H>
#include <GraphLog.H>

using std::ostream;
using std::ifstream;
using std::cerr;
using std::endl;
using OS::cio;

class FSFailure {
  public:
    FSFailure(int sys_errno) throw ()
      : sys_errno(sys_errno) { /*SKIP*/ }
    friend ostream& operator<<(ostream &os, const FSFailure &f) throw ();
  private:
    int sys_errno;
};

ostream& operator<<(ostream &os, const FSFailure &f) throw ()
{
    os << "errno = " << f.sys_errno;
    return os;
}

static void WriteShortId(FILE *fp, ShortId id) throw (FSFailure)
/* Write the ShortId "id" to "fp" on its own line in a format understood
   by the repository's derived weeder. */
{
    int res = fprintf(fp, "%08x\n", id);
    if (res < 0) throw (FSFailure(errno));
}

static void TimedMessage(char *msg) throw ()
{
  cio().start_err() << Debug::Timestamp() << msg << endl;
  cio().end_err();
}

static void ScanGraphLogReader(RecoveryReader &rd, FILE *fp,
  /*INOUT*/ int &numEntries, /*INOUT*/ int &numDIs)
  throw (VestaLog::Error, VestaLog::Eof, FSFailure)
{
    while (!rd.eof()) {
	GraphLog::Entry *entry = GraphLog::Entry::Recover(rd);
	if (entry->kind == GraphLog::NodeKind) {
	    GraphLog::Node *node = (GraphLog::Node *)entry;
	    numEntries++;
	    Derived::Indices *refs = node->refs;
	    for (int i = 0; i < refs->len; i++) {
		WriteShortId(fp, refs->index[i]);
	    }
	    numDIs += refs->len;
	}
	delete entry;
    }
}

const char *CacheSection = "CacheServer";

static Text ReadGraphLogPath() throw ()
{
    const Text MDRoot(     ReadConfig::TextVal(CacheSection, "MetaDataRoot"));
    const Text MDDir(      ReadConfig::TextVal(CacheSection, "MetaDataDir"));
    const Text GraphLogDir(ReadConfig::TextVal(CacheSection, "GraphLogDir"));
    return MDRoot +'/'+ MDDir +'/'+ GraphLogDir;
}

static void ScanGraphLog(FILE *fp, const Text &host, const Text &port)
  throw (SRPC::failure, VestaLog::Error, VestaLog::Eof, FSFailure)
/* Scan the graphLog, writing any deriveds appearing in it to "fp". */
{
    Text graphLogPath(ReadGraphLogPath());

    // pre debugging
    TimedMessage("Started graph log scan");
    cio().start_err() << "  Configuration file: " 
		      << VestaConfig::get_location() << endl
		      << "  Hostname:port: " << host << ':' << port << endl
		      << "  Graph log: " << graphLogPath << endl;
    cio().end_err();

    // initialize and make sure another weed is not running
    WeederC weeder;  // binds to the cache server
    try {
      ifstream ifs;
      Text weededFile = 
	ReadConfig::TextVal("CacheServer", "MetaDataRoot") + "/" +
	ReadConfig::TextVal("Weeder", "MetaDataDir") + "/" +
	ReadConfig::TextVal("Weeder", "Weeded");
      FS::OpenReadOnly(weededFile, /*OUT*/ ifs);
      BitVector weeded(ifs);
      if (!weeded.IsEmpty()) {
        cerr <<
	  "Fatal error: a VestaWeed is already running or in need of recovery!"
	     << endl;
	cerr << "  If VestaWeed is running, wait for it to finish." <<endl;
	cerr << "  If VestaWeed is not running, either run it, allowing"<<endl;
	cerr << "  the prior weed to finish, or use EraseCache to" <<endl;
	cerr << "  completely delete the cache and the weeding state." << endl;
	exit(1);
      }
      FS::Close(ifs);
    }
    catch (FS::DoesNotExist) {
      // good, no weed to recover
    }
    
    bool conflict = weeder.WeederRecovering(/*doneMarking=*/ false);
    if (conflict) {
      cerr <<
	"Fatal error: a VestaWeed or QuickWeed is already running!" << endl;
      exit(1);
    }

    // checkpoint the cache server
    int newLogVer;
    (void) weeder.StartMark(/*OUT*/ newLogVer);
    weeder.ResumeLeaseExp();

    // open & read "graphLog"
    VestaLogSeq graphLogSeq(graphLogPath.chars());
    graphLogSeq.Open(/*ver=*/ -1, /*readonly=*/ true);
    RecoveryReader *rd;
    int numEntries = 0, numDIs = 0;
    int lastLogVer = -1;
    while ((rd = graphLogSeq.Next(newLogVer)) != (RecoveryReader *)NULL) {
        lastLogVer = graphLogSeq.CurLogVersion();
	ScanGraphLogReader(*rd, fp, /*INOUT*/ numEntries, /*INOUT*/ numDIs);
    }
    // The last .log file we read from the graphLog should be just
    // before newLogVer, which the cache server told us.  If that's
    // not true, we may be reading the graphLog from one cache and
    // talking to a different cache over the network, which could
    // cause cache deleting deriveds referenced by the cache server
    // we're talking to.
    if(lastLogVer != (newLogVer - 1))
      {
	cerr << Debug::Timestamp() << "Fatal error:"
	     << endl
	     << "  Cache graph log doesn't seem to match cache server" << endl
	     << "  (Maybe the filesystem doesn't match the cache server?)" << endl
	     << endl
	     << "  last log read     = " << lastLogVer << endl
	     << "  expected last log = " << (newLogVer-1) << endl;
	exit(1);
      }

    // post debugging
    TimedMessage("Finished graph log scan");

    // print stats
    cio().start_err() << "  Graph log entries processed = " 
		      << numEntries << endl
		      << "  Derived files found = " << numDIs << endl;
    cio().end_err();
}

static void ScanFromConfig(FILE *fp)
  throw (SRPC::failure, VestaLog::Error, VestaLog::Eof, FSFailure)
{
    // specify the machine on which the cache server is running
    Text host(ReadConfig::TextVal(CacheSection, "Host"));
    Text port(ReadConfig::TextVal(CacheSection, "Port"));
    ParCacheC::SetServerHost(host.chars());

    // scan the graphLog
    ScanGraphLog(fp, host, port);
}

static void SyntaxError(char *msg, char *arg = NULL) throw ()
{
    cerr << "Error: " << msg;
    if (arg != NULL) cerr << ": \"" << arg << "\"";
    cerr << "; exiting..." << endl;
    cerr << "Syntax: QuickWeed [ -n ] [ -i ] [ -cf config-file ] ..." << endl;
    exit(1);
}

int main(int argc, char *argv[]) 
{
    TextSeq configFiles;   // configuration files to scan
    bool doCentral = true; // scan graphLog of default config file?
    bool doWeeds = true;   // do the source and derived weeds?
    bool ckptRepos = true; // checkpoint repository after weeding?
    bool noCache = false;  // ignore the cache altogether

    // process command-line
    for (int arg = 1; arg < argc; arg++) {
	if (*argv[arg] == '-') {
	    if (strcmp(argv[arg], "-n") == 0) {
		doWeeds = false;
	    } else if (strcmp(argv[arg], "-i") == 0) {
		doCentral = false;
	    } else if (strcmp(argv[arg], "-p") == 0) {
		ckptRepos = false;
	    } else if (strcmp(argv[arg], "-no-cache") == 0) {
		noCache = true;
	    } else if (strcmp(argv[arg], "-cf") == 0) {
		if (++arg < argc) {
		    Text path(argv[arg]);
		    if (path[path.Length()-1] == '/') {
			path += "vesta.cfg";
		    }
		    configFiles.addhi(path);
		} else {
		    SyntaxError("expecting config-file after \"-cf\"");
		}
	    } else {
		SyntaxError("unrecognized switch", argv[arg]);
	    }
	} else {
	    SyntaxError("unrecognized argument", argv[arg]);
	}
    }

    if (!doCentral && configFiles.size() == 0)
      {
	if(noCache)
	  {
	    cerr << "NOTE: ignoring all caches at your request." << endl
		 << "      (If there are any caches using this repository," << endl
		 << "      this will invalidate them!)" << endl;
	  }
	else
	  {
	    cerr << "Error: -i specified without -cf; exiting..." << endl;
	    exit(1);
	  }
      }
    else if(noCache)
      {
	cerr << "Error: -no-cache specified without -i or with -cf; exiting..."
	     << endl;
	exit(1);
      }

    try {
	FILE *fp = (FILE *)NULL;
	ShortId disShortId;

	if (doWeeds) {
	    // create file for writing ShortIds to
	    int fd = SourceOrDerived::fdcreate(
              /*OUT*/ disShortId, /*leafflag=*/ true);
	    if (fd == -1) throw FSFailure(errno);
            fp = fdopen(fd, "w");
	    char buf[8];
	    sprintf(buf, "%08x", disShortId);
	    cio().start_err() << "Writing ShortIds of deriveds to keep to file "
			      << buf << endl;
	    cio().end_err();
	} else {
	    fp = stdout;
	}

	if (doWeeds) {
	    TimedMessage("Started marking derived files to keep");
	    // write the ShortId of the file itself
	    WriteShortId(fp, disShortId);
	}

	// record start time (for later call to "keepDerived")
	time_t startT;
	(void) time(&startT);
	// Subtract a configurable amount of time from the start time.
	int startGracePeriod = 60;
	(void) ReadConfig::OptIntVal("Weeder", "GracePeriod",
				     startGracePeriod);
	if(startGracePeriod > 0)
	  {
	    startT -= startGracePeriod;
	  }

	// scan all relevant graphLog files
	if (doCentral) ScanFromConfig(fp);
	while (configFiles.size() > 0) {
	    VestaConfig::set_location(configFiles.remlo());
	    ScanFromConfig(fp);
	}

	if (doWeeds) {
	    // close the output file
	    if (fflush(fp) == EOF || fclose(fp) == EOF) {
		throw (FSFailure(errno));
	    }
	    TimedMessage("Finished marking derived files to keep");

	    // delete the derived files
	    TimedMessage(
                "Started marking sources and deleting unreachable files");
	    int derRes =
                SourceOrDerived::keepDerived(disShortId, startT);
	    TimedMessage(
                "Finished marking sources and deleting unreachable files");
	    if (derRes != 0) {
		cerr << "Derived weed error = " << derRes << endl;
	    } else {
                if (ckptRepos) {
                    TimedMessage("Started checkpointing the repository");
                    SourceOrDerived::checkpoint();
                    TimedMessage("Finished checkpointing the repository");
                }
            }
	}
    }
    catch (SRPC::failure &f) {
	cerr << "SRPC failure: " << f.msg << "; exiting..." << endl;
	exit(1);
    }
    catch (VestaLog::Error &err) {
	cerr << "VestaLog fatal error -- failed reading graph log:" << endl;
        cerr << "  " << err.msg << endl;
        cerr << "Exiting..." << endl;
	exit(1);
    }
    catch (VestaLog::Eof) {
	cerr << "VestaLog fatal error: ";
        cerr << "unexpected EOF while reading graph log; exiting..." << endl;
	exit(1);
    }
    catch (FSFailure &f) {
	cerr << "Error creating/writing derived keep file, "
          << f << "; exiting..." << endl;
	exit(1);
    }
}
