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

//
// RepositoryMain.C
//
// Main program for repository server
//

#include "VestaLog.H"
#include "Recovery.H"
#include "VestaSource.H"
#include "ShortIdImpl.H"
#include "ReadersWritersLock.H"
#include "VRConcurrency.H"
#include "VestaConfig.H"
#include "VestaSourceServer.H"
#include "VMemPool.H"
#include "VRWeed.H"
#include "DirShortId.H"
#include "FPShortId.H"
#include "Mastership.H"
#include "Replication.H"
#include "nfsd.H"
#include "logging.H"
#if defined(HAVE_GETOPT_H)
#include <getopt.h>
#endif
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <iomanip>
#include <stdio.h>
#include <signal.h>
#include <sys/resource.h>
#ifdef __osf__
#include <sys/sysinfo.h>
#include <machine/hal_sysinfo.h>
#endif

#include "MutableSidref.H"

#include "timing.H"
#include "lock_timing.H"

using std::fstream;
using std::ofstream;
using std::endl;
using std::cerr;
using std::cout;

#define progress(letter)
//#define progress(letter) putc(letter, stderr)

// Exported globals
char *program_name;
int nfs_bufreqs;  // # of NFS requests that should fit in socket buffer
int nfs_threads;  // # of NFS server threads (0=use main thread)

// Imported globals
extern int nfs_port;
extern void GlueInit();

#define DEFAULT_UMASK 022

static void usage(FILE *fp, int n)
{
  cerr << "Usage: " << program_name
       << " [-h] [-d level] [-f first-ckp]" << endl;
  exit(n);
}

// Main driver for recovery.
// Read first.ckp (if any), and first.log through the last log present.
// If first == -1, read most recent ckp (if any) and log.
void
Recover(Text log_dir, int first, Text log_dir2, bool bakckp)
{
    VRLog.open(log_dir.chars(), first, false, true,
	       log_dir2.Empty() ? NULL : log_dir2.chars(), bakckp);

    fstream* ckpt = VRLog.openCheckpoint();
    if (ckpt != NULL) {
	VMemPool::readCheckpoint(*ckpt, false);
	RecoveryReader crr(ckpt);
	RecoverFrom(&crr);
	ckpt->close();
	delete ckpt;
    }
    if(MutableSidrefRecoveryCheck())
      {
	MutableSidrefInit();
      }
    // Recover log records following checkpoint
    do {
	RecoveryReader lrr(&VRLog);
	RecoverFrom(&lrr);
	if(MutableSidrefRecoveryCheck())
	  {
	    MutableSidrefCheck(0, LongId::noLock);
	  }
    } while (VRLog.nextLog());
}

// Exit cleanly on SIGINT or SIGTERM
#if defined(__linux__)
// On Linux, we need to remember which is the main thread.
static pthread_t g_main_thread = pthread_self();
#endif
extern "C" void
SigHandler(int sig)
{
#if defined(__linux__)
  // The Linux pthreads implementation uses one process per thread.
  // The main thread will receive the signal from a ^C and then
  // proceed to call exit below.  This will in turn kill off the other
  // threads, delivering each of them a signal which will in turn call
  // this handler again.  If any of those threads then call exit, the
  // pthreads library seems to become confused and crashes.  For that
  // reason, we don't call exit unless we're the main thread.
  if(pthread_self() != g_main_thread)
    {
      return;
    }
#endif
  char* msg;
  if (sig == SIGINT) {
    msg = "repository: Got SIGINT (^C interrupt signal); exiting.\n";
  } else if (sig == SIGTERM) {
    msg = "repository: Got SIGTERM (kill signal); exiting.\n";
  } else {
    assert(false);
  }
  write(2, msg, strlen(msg));
  exit(3);
}

// This is just a hook to hang breakpoints, etc. on.
void
Siguser1Inner(int sig)
{
  return;
}

extern "C" void
Sigusr1Handler(int sig)
{
    Siguser1Inner(sig);
}

void
Siguser2Inner(int sig)
{
  return;
}

extern "C" void
Sigusr2Handler(int sig)
{
    Siguser2Inner(sig);
}

#ifdef __osf__
extern int __first_fit;
#endif

// Server version.  This is defined in a file written in progs.ves.
extern const char *Version;

// Server start time, set in main.
time_t serverStartTime;

int
main(int argc, char *argv[])
{
    int c;
    sigset_t set;

    signal(SIGINT, SigHandler);
    signal(SIGTERM, SigHandler);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGILL, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    signal(SIGUSR1, Sigusr1Handler);
    signal(SIGUSR2, Sigusr2Handler);

    // Set blocking on USR1 and USR2 signals for inheritance by child threads.
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigaddset(&set, SIGUSR2);
    sigprocmask(SIG_BLOCK, &set, NULL);

#ifdef __osf__
    // Reduce lock contention during malloc/free
    __first_fit = 2;
#endif

    // Read config file
    bool ok, bakckp = 0;
    Text metadata_root, log_dir, log_dir2, scratch;
    try {
	Text t;
	metadata_root = VestaConfig::get_Text("Repository", "metadata_root");
	chdir(metadata_root.cchars());
	SourceOrDerived::setMetadataRootLocalName("");
	nfs_port = VestaConfig::get_int("Repository", "NFS_port");
	log_dir = VestaConfig::get_Text("Repository", "log_dir");
	if (!VestaConfig::get("Repository", "log_dir2", log_dir2)) {
	    log_dir2 = "";
        }
	if (VestaConfig::get("Repository", "backup_ckp", t)) {
	    bakckp = strtol(t.cchars(), NULL, 0) != 0;
	}
	nfs_bufreqs = VestaConfig::get_int("Repository", "bufreqs");
	nfs_threads = VestaConfig::get_int("Repository", "threads");
	if (VestaConfig::get("Repository", "debug_level", t)) {
	    Repos::setDebugLevel(strtol(t.cchars(), NULL, 0));
	}
	if (VestaConfig::get("Repository", "umask", t)) {
	    umask(strtol(t.cchars(), NULL, 0));
	} else {
	    umask(DEFAULT_UMASK);
	}
	// Optional setting of file descriptor limit.
	if(VestaConfig::get("Repository", "descriptor_limit", t))
	  {
	    int new_limit = strtol(t.cchars(), NULL, 0);
	    if(errno == ERANGE)
	      {
		cerr << "[Repository]descriptor_limit: " << strerror(ERANGE)
		     << endl;
		exit(2);
	      }

#ifdef __osf__
	    // If needed, enable support for more than 4k file
	    // descriptors.
	    if(new_limit > 4096)
	      {
		int err = setsysinfo(SSI_FD_NEWMAX, NULL, 0, NULL, 1);
		if(err != 0)
		  {
		    int l_errno = errno;
		    cerr << "Enabling more than 4096 descriptors: "
			 << strerror(l_errno) << endl;
		  }
	      }
#endif
	    // Set the soft and hard file descriptor limit to the
	    // requested value.
	    struct rlimit new_rlimit;
	    new_rlimit.rlim_cur = new_limit;
	    new_rlimit.rlim_max = new_limit;
	    int err = setrlimit(RLIMIT_NOFILE, &new_rlimit);
	    if(err != 0)
	      {
		int l_errno = errno;
		cerr << "setrlimit(RLIMIT_NOFILE, {"
		     << new_limit << ", " << new_limit << "}): "
		     << strerror(l_errno) << endl;
	      }
	  }
    } catch (VestaConfig::failure f) {
	cerr << f.msg << endl;
	exit(2);
    }

    /* Parse the command line options and arguments. */
    program_name = argv[0];
    int first = -1;
    opterr = 0;
    bool debug_dump = false;
    Text debug_dump_fname;
    while ((c = getopt(argc, argv, "hd:f:D:")) != EOF)
      switch (c) {
	case 'h':
	  usage(stdout, 0);
	  break;
	case 'd':
	  Repos::setDebugLevel(strtol(optarg, NULL, 0));
	  break;
	case 'f':
	  first = strtol(optarg, NULL, 0);
	  break;
	case 'D':
	  debug_dump = true;
	  debug_dump_fname = optarg;
	  break;
	case 0:
	  break;
	case '?':
	default:
	  usage(stderr, 1);
      }

    /* No more arguments allowed. */
    if (optind != argc)
      usage(stderr, 1);

    // Save previous core file if any
    if (Repos::isDebugLevel(DBG_SAVECORE)) {
	system("if [ -e core ] ; then mv core core.`date +%m-%d-%y.%T` ; fi");
    }

#if defined (_POSIX_THREAD_PRIORITY_SCHEDULING) && defined(PTHREAD_SCHED_RR_OK) && !defined(__linux__)
    // If we think it'll work, set our thread scheduling policy to
    // round-robin rather than the default.
    Basics::thread::self().set_sched_param(SCHED_RR,
					   sched_get_priority_min(SCHED_RR));
#endif

    // Initialize server
    try {
        progress('a');
	ShortIdServerInit();
	progress('b');
	AccessControl::serverInit();
	progress('c');
	VMemPool::init();
	VestaSource::init();
	progress('d');
	InitDirShortId();
	progress('e');
	InitFPShortId();
	progress('f');
	MastershipInit1();
	progress('g');
	Recover(log_dir, first, log_dir2, bakckp);
	progress('h');
	if(debug_dump)
	  {
	    Repos::dprintf(DBG_ALWAYS,
			   "Writing debug memory dump to %s\n",
			   debug_dump_fname.cchars());
	    {
	      ofstream dump_stream(debug_dump_fname.cchars());
	      VMemPool::debugDump(dump_stream);
	      dump_stream.flush();
	    }
	    Repos::dprintf(DBG_ALWAYS,
			   "Finished debug memory dump to %s\n",
			   debug_dump_fname.cchars());
	    exit(0);
	  }
	VRLog.loggingBegin();
	progress('i');
	if (!Repos::isDebugLevel(DBG_NOWEEDREC)) {
	    int err = SourceWeed();
	    if (err) {
	      Text emsg = Basics::errno_Text(err);
	      Repos::dprintf(DBG_ALWAYS,
			     "source weed failed during recovery, "
			     "%s (errno = %d)\n", emsg.cchars(), err);
	      abort();
	    }
	    err = DoDeletions();
	    if (err) {
	      Text emsg = Basics::errno_Text(err);
	      Repos::dprintf(DBG_ALWAYS,
			     "deletions failed during recovery, "
			     "%s (errno = %d)\n", emsg.cchars(), err);
	      abort();
	    }
	}
	progress('j');
	DebugShortIdBlocks("after recovery");
	ShortIdServerInit2();
	progress('k');
	ShortIdServerExport(NULL);
	progress('l');
	VestaSource::recoveryDone(); // turns on logging
	progress('m');
	MastershipInit2();
	progress('n');
	ReplicationCleanup();
	progress('o');
	// Record the start time before we start accepting SRPC calls
	serverStartTime = time((time_t *) 0);
	VestaSourceServerExport();
	progress('p');
	GlueInit();
	progress('q');
	nfsd_init(nfs_threads);
	progress('r');
	Repos::dprintf(DBG_ALWAYS, "Repository server started.  Version %s\n",
		       Version);

	// Use main thread for checkpointing (which needs deep recursion)
	// and to handle SIGUSR1 and SIGUSR2 (which need deep recursion if
	// tied to Third Degree leak checking).
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
	progress('s');
	CheckpointServer(); // does not return

    } catch (SRPC::failure f) {
	cerr << program_name << ": SRPC::failure in initialization: "
	  << f.msg << " (" << f.r << ")" << endl;
	abort();
    } catch (VestaLog::Error e) {
	cerr << program_name << ": VestaLog::Error in initialization: "
	  << e.msg << " (" << e.r << ")" << endl;
	if (e.msg == "VestaLog::open got \"Permission denied\" locking lock") {
	    cerr << "Check if there is already a repository running."<< endl;
	    exit(1);
	} else if (e.msg ==
		   "VestaLog::open got \"Permission denied\" opening lock") {
	    cerr << "Check that you have the right user id." << endl;
	    exit(1);
	}
	abort();
    } catch (VestaConfig::failure f) {
	cerr << f.msg << endl;
	exit(2);
    }

    // Not reached
    assert(false);
    return 0;
}

