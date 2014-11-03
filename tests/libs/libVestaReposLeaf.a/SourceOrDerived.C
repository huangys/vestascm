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
// SourceOrDerived.C
//
// Library code to implement SourceOrDerived.H
// 

#include <Basics.H>
#include "SourceOrDerived.H"
#include "ShortIdBlock.H"
#include <VestaConfig.H>
#include <Thread.H>
#include <pthread.h>
#include <assert.h>
#include <iomanip>
#include <errno.h>
#if defined(__unix) || defined(__linux__)
#include <unistd.h>
#endif
#include <stdio.h>
#if defined(__digital__)
#include <sys/mode.h>
#endif
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <FS.H>

using std::cerr;
using std::endl;
using std::hex;
using std::fstream;
using std::ios;

// Directory modes for SID files hardwired here
const int SID_DIR_MODE = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

const int LeaseSafetyMargin = 60; // sec

//
// We keep a stockpile of each kind of short id.
//

class ShortIdStock {
    bool leafflag;
    ShortIdBlock *block[2];   // one block in use, one in reserve
    int usng;                 // block number we are allocating from, 0 or 1
    Basics::mutex mu;
    Basics::cond need;        // broadcast whenever a block is used up
    Basics::cond have;        // broadcast when no blocks were available,
                              //  and one is acquired
    Basics::thread stockboy_; // gets more blocks
    static void *stockboy(void *arg);
  public:
    ShortIdStock(bool leafflag);
    ShortId newShortId();
};

typedef ShortIdStock *ShortIdStockP;

static ShortIdStockP stock[2];
static pthread_once_t stockOnce = PTHREAD_ONCE_INIT;
static pthread_once_t mrlnOnce = PTHREAD_ONCE_INIT;
static Basics::mutex mu;
static char* metadataRootLocalName = NULL;

extern "C"
{
  static void 
  SourceOrDerived_initStock()
  {
    stock[0] = NEW_CONSTR(ShortIdStock, (false));
    stock[1] = NEW_CONSTR(ShortIdStock, (true));
  }

  void
  SourceOrDerived_initMRLN()
  {
    try {
	Text mrtext;
	mrtext = VestaConfig::get_Text("Repository", "metadata_root");
	metadataRootLocalName = strdup(mrtext.cchars());
    } catch (VestaConfig::failure f) {
	cerr << "SourceOrDerived_initMRLN: " << f.msg << endl;
	throw; // meant to be uncaught and fatal
    }
  }
}

void
SourceOrDerived::setMetadataRootLocalName(char* pathname) throw ()
{
    int err = pthread_once(&mrlnOnce, SourceOrDerived_initMRLN);
    assert(err == 0);
    mu.lock();
    free(metadataRootLocalName);
    metadataRootLocalName = strdup(pathname);
    mu.unlock();
}

char*
SourceOrDerived::getMetadataRootLocalName() throw ()
{
    int err = pthread_once(&mrlnOnce, SourceOrDerived_initMRLN);
    assert(err == 0);
    mu.lock();
    char* ret = strdup(metadataRootLocalName);
    mu.unlock();
    return ret;
}

// 
// Constructor for a ShortIdStock
//
ShortIdStock::ShortIdStock(bool leafflag)
{
    ShortIdStock::leafflag = leafflag;
    block[0] = block[1] = NULL;
    usng = 0;

    Basics::thread_attr stockboy_attr;
#if defined (_POSIX_THREAD_PRIORITY_SCHEDULING) && defined(PTHREAD_SCHED_RR_OK) && !defined(__linux__)
    // Linux only allows the superuser to use SCHED_RR
    stockboy_attr.set_schedpolicy(SCHED_RR);
    stockboy_attr.set_inheritsched(PTHREAD_EXPLICIT_SCHED);
    stockboy_attr.set_sched_priority(sched_get_priority_min(SCHED_RR));
#endif
  
    stockboy_.fork(stockboy, (void *) this, stockboy_attr);
}

//
// Stockboy thread.  Gets more ShortId blocks to keep some in stock.
//
void *
ShortIdStock::stockboy(void *arg)
{
    ShortIdStock *mystock = (ShortIdStock *) arg;
    ShortIdBlock *newblock;
    
    signal(SIGPIPE, SIG_IGN);
    signal(SIGQUIT, SIG_DFL);
    //signal(SIGSEGV, SIG_DFL); // oops, process may be using incremental GC
    signal(SIGABRT, SIG_DFL);
    signal(SIGILL, SIG_DFL);
    signal(SIGBUS, SIG_DFL);

    mystock->mu.lock();
    for (;;) {
      outer_continue:
	struct timespec wakeup;
	wakeup.tv_sec = 0x7fffffff;
	wakeup.tv_nsec = 0;
	bool need_wakeup = false;
	try {
	    int which;
	    for (which = 0; which <= 1; which++) {
		if (mystock->block[which] != NULL &&
		    time(0) > mystock->block[which]->leaseExpires) {
		    // Lease already expired.  Should not happen.
		    cerr << "ShortIdStock::stockboy: "
		      << "internal error - expired lease on block 0x" << hex
		      << mystock->block[which]->start << endl;
		    delete mystock->block[which];
		    mystock->block[which] = NULL;
		}
		if (mystock->block[which] == NULL) {
		    mystock->mu.unlock();
		    newblock = ShortIdBlock::acquire(mystock->leafflag);
		    mystock->mu.lock();
		    assert(mystock->block[which] == NULL);
		    mystock->block[which] = newblock;
		    if (mystock->block[mystock->usng] == NULL) {
			mystock->usng = which;
		    }
		    mystock->have.broadcast();
		    goto outer_continue;
		}
		if (time(0) >= (mystock->block[which]->leaseExpires -
				2*LeaseSafetyMargin)) {
		    mystock->mu.unlock();
		    bool leaseValid =
		      ShortIdBlock::renew(mystock->block[which]);
		    assert(leaseValid);
		    mystock->mu.lock();
		    goto outer_continue;
		}
		if ((mystock->block[which]->leaseExpires !=
		     ShortIdBlock::leaseNonexpiring) &&
		    (wakeup.tv_sec >
		     mystock->block[which]->leaseExpires - LeaseSafetyMargin)){
		    wakeup.tv_sec =
		      mystock->block[which]->leaseExpires - LeaseSafetyMargin;
		    need_wakeup = true;
		}	    
	    }
	} catch (SRPC::failure f) {
	    cerr << "ShortIdStock::stockboy: "
	      << f.msg << " (" << f.r << ")"
	      << " on SRPC to repository" << endl;
	    throw;  // !!crash with uncaught exception
	}
	if (need_wakeup) {
	    mystock->need.timedwait(mystock->mu, &wakeup);
	} else {
	    mystock->need.wait(mystock->mu);
	}
    }
    //return (void *)NULL;    // Not reached
}    

void
SourceOrDerived::releaseResources() throw ()
{
    //!! Temporarily a no-op
    return;
}

ShortId
ShortIdStock::newShortId()
{
    ShortId sid;
    
    mu.lock();
    do {
	while (block[usng] == NULL) {
	    have.wait(mu);
	}
	sid = block[usng]->assignNextAvail();
	if (sid == 0) {
	    ShortIdBlock::release(block[usng]);
	    delete block[usng];
	    block[usng] = NULL;
	    usng = 1 - usng;
	    need.broadcast();
	}
    } while (sid == 0);
    mu.unlock();
    return sid;
}

char*
SourceOrDerived::shortIdToName(ShortId sid, bool tailOnly) throw ()
{
    int err = pthread_once(&mrlnOnce, SourceOrDerived_initMRLN);
    assert(err == 0);
    mu.lock();
    char *ret =
      ShortIdBlock::shortIdToName(sid, tailOnly ? "" : metadataRootLocalName);
    mu.unlock();
    return ret;
}


static int
MakeDirs(char* name)
{
    char *q, *r;
    q = name;
    if (*q == PathnameSep) q++;
    for (;;) {
	r = strchr(q, PathnameSep);
	if (r == NULL) {
	    break;
	} else {
	    *r = '\0';
	    int ret = mkdir(name, SID_DIR_MODE);
	    *r = PathnameSep;
	    if (ret < 0 && errno != EEXIST) return ret;
	    q = r + 1;
	}
    }
    return 0;
}

static Text parentDir(Text path)
{
  int slashPos = path.FindCharR(PathnameSep);
  assert(slashPos != -1);
  return path.Sub(0, slashPos);
}

ShortId
SourceOrDerived::create(bool leafflag, ios::openmode mode, mode_t prot)
  throw(SRPC::failure, SourceOrDerived::Fatal)
{
    int err = pthread_once(&stockOnce, SourceOrDerived_initStock);
    assert(err == 0);
    ShortId sid = stock[leafflag?1:0]->newShortId();
    char *name = SourceOrDerived::shortIdToName(sid, false);

    // Make sure that the directory containing this shortid exists.
    if(!FS::IsDirectory(parentDir(name)))
      {
	int ret = MakeDirs(name);
	if (ret < 0) {
	    if (errno == EACCES) {
		throw Fatal("no permission to create shortid file");
	    }
	    clear(ios::badbit);	// sets badbit
	    delete [] name;
	    return NullShortId;
	}
      }

    // Shortid file shouldn't exist yet.  If it does, it's an error.
    if(FS::Exists(name))
      {
	clear(ios::badbit);	// sets badbit
	delete [] name;
	return NullShortId;
      }

    // Create the file with the correct mode.
    try
      {
	FS::Touch(name, prot, false);
      }
    // Should never happen, since we already covered this case above,
    // but there is a race.
    catch(FS::AlreadyExists)
      {
	clear(ios::badbit);	// sets badbit
	delete [] name;
	return NullShortId;
      }
    catch(FS::Failure &err)
      {
	clear(ios::badbit);	// sets badbit
	delete [] name;
	if (err.get_errno() == EACCES) {
	    throw Fatal("no permission to create shortid file");
	}
	return NullShortId;
      }
    
    // Open the file.
    fstream::open(name, mode);
    delete [] name;
    if (fail()) {
	if (errno == EACCES) {
	    throw Fatal("no permission to open shortid file after creating it");
	}
	return NullShortId;
    }

    return sid;
}

void
SourceOrDerived::open(ShortId sid, ios::openmode mode) throw ()
{
    char *name = SourceOrDerived::shortIdToName(sid, false);
    if(FS::Exists(name))
      {
	fstream::open(name, mode);
      }
    else
      {
	clear(ios::badbit);	// sets badbit
      }
    delete [] name;
}

int
SourceOrDerived::fdcreate(ShortId& sid, bool leafflag, mode_t prot)
  throw(SRPC::failure, SourceOrDerived::Fatal)
{
    int err = pthread_once(&stockOnce, SourceOrDerived_initStock);
    assert(err == 0);
    sid = stock[leafflag?1:0]->newShortId();
    char *name = SourceOrDerived::shortIdToName(sid, false);
    int fd;
    
    // First, optimistically assume the directories are all there
    fd = ::open(name, O_RDWR | O_CREAT | O_EXCL, prot);
    if (fd < 0 && errno == ENOENT) {
	// Oops, a directory on the path is missing.
	// Make them all.
	int ret = MakeDirs(name);
	if (ret < 0) {
	    fd = -1; 
	} else {
	    fd = ::open(name, O_RDWR | O_CREAT | O_EXCL, prot);
	}
    }
    if (fd < 0) {
	if (errno == EACCES) {
	    throw Fatal("no permission to create shortid file");
	}
	sid = NullShortId;
    } else {
      // Try to ensure that the creation of the file makes it to disk
      // before we do anything like log a transaction referring to
      // this shortid.  (We ignore errors, because we don't expect any
      // and can't recover anyway.)
      (void) fsync(fd);
    }
    delete [] name;
    return fd;
}

int
SourceOrDerived::fdopen(ShortId sid, int oflag, mode_t prot) throw ()
{
    char *name = SourceOrDerived::shortIdToName(sid, false);
    oflag &= ~(O_CREAT);
    int ret = ::open(name, oflag, prot);
    delete [] name;
    return ret;
}

bool
SourceOrDerived::touch(ShortId sid) throw ()
{
    char *name = SourceOrDerived::shortIdToName(sid, false);
    struct timeval times[2];
    gettimeofday(&times[0], NULL);
    times[1] = times[0];
    int ok = utimes(name, times);
    delete [] name;
    return (ok == 0);
}

int
SourceOrDerived::keepDerived(ShortIdsFile ds, time_t dt, bool force)
  throw(SRPC::failure)
{
    return ShortIdBlock::keepDerived(ds, dt, force);
}

void
SourceOrDerived::checkpoint() throw(SRPC::failure)
{
    ShortIdBlock::checkpoint();
}

void
SourceOrDerived::getWeedingState(ShortIdsFile& ds, time_t& dt,
				 ShortIdsFile& ss, time_t& st,
				 bool& sourceWeedInProgress,
				 bool& deletionsInProgress,
				 bool& deletionsDone,
				 bool& checkpointInProgress)
  throw(SRPC::failure)
{
    ShortIdBlock::getWeedingState(ds, dt, ss, st, sourceWeedInProgress,
				  deletionsInProgress, deletionsDone,
				  checkpointInProgress);
}


