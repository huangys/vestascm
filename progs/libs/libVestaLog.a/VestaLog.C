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
// VestaLog.C
//
// Log changes to the repository state
//

#include "VestaLog.H"
#include "VestaLogPrivate.H"

#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
// add declaration to fix broken <dirent.h> header file
extern "C" int _Preaddir_r(DIR *, struct dirent *, struct dirent **);

#include <FS.H>
#include <FdStream.H>

using std::ifstream;
using std::fstream;
using std::ios;
using std::endl;

static const char VersionFileName[] = "version";
static const char NewVersionFileName[] = "version.new";
static const char PrunedFileName[] = "pruned";
static const char NewPrunedFileName[] = "pruned.new";
static const char LockFileName[] = "lock";
static const char LogExtension[] = ".log";
static const char CheckpointExtension[] = ".ckp";
static const int MaxFileNameLen = 4096;

#define COPY_SIZE 8192
#define UNUSED_PHY (~((VLog::phy_t)0))

// Protection bits for log files hardwired here
static const int LOG_PROT = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

VestaLog::VestaLog() throw()
{
  vlp = NEW(VestaLogPrivate);
  vlp->state = VestaLogPrivate::initial;
}

void VestaLog::open(char* dir, int ver, bool readonly, bool lock,
		    char* dir2, bool bakckp) throw (VestaLog::Error)
{
    // state: initial -> recovering
    assert(vlp->state == VestaLogPrivate::initial);
    vlp->directory = NEW_PTRFREE_ARRAY(char, strlen(dir) + 1);
    strcpy(vlp->directory, dir);
    vlp->readonly = readonly;
    if (dir2) {
      vlp->directory2 = NEW_PTRFREE_ARRAY(char, strlen(dir2) + 1);
      strcpy(vlp->directory2, dir2);
    } else {
      vlp->directory2 = NULL;
      assert(!bakckp);
    }
    vlp->bakckp = bakckp;

    vlp->lockfd = -1;
    vlp->lockfd2 = -1;
    if (lock) {
	// Acquire advisory lock on this log
        Text lockfn = Text(vlp->directory) + PathnameSep + LockFileName;
	vlp->lockfd = ::open(lockfn.cchars(), O_RDWR | O_CREAT, 0666);
	if (vlp->lockfd == -1) {
	    vlp->state = VestaLogPrivate::bad;
	    throw Error(errno, Text("VestaLog::open got \"") +
			Basics::errno_Text(errno) + "\" opening " + lockfn);
	}
	struct flock fl;
	fl.l_type = readonly ? F_RDLCK : F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_pid = 0;
	int ok = ::fcntl(vlp->lockfd, F_SETLK, &fl);
	if (ok == -1) {
	    vlp->state = VestaLogPrivate::bad;
	    throw Error(errno, Text("VestaLog::open got \"") +
			Basics::errno_Text(errno) + "\" locking " + lockfn);
	}
	// Acquire advisory lock on backup log
	if (dir2) {
	  Text lockfn2 = Text(vlp->directory2) + PathnameSep + LockFileName;
	  vlp->lockfd2 = ::open(lockfn2.cchars(), O_RDWR | O_CREAT, 0666);
	  if (vlp->lockfd2 == -1) {
	    vlp->state = VestaLogPrivate::bad;
	    throw Error(errno, Text("VestaLog::open got \"") +
			Basics::errno_Text(errno) + "\" opening " + lockfn2);
	  }
	  ok = ::fcntl(vlp->lockfd, F_SETLK, &fl);
	  if (ok == -1) {
	    vlp->state = VestaLogPrivate::bad;
	    throw Error(errno, Text("VestaLog::open got \"") +
			Basics::errno_Text(errno) + "\" locking " + lockfn2);
	  }
	}
    }

    // Determine the highest committed checkpoint version number,
    //  and the log version to open
    Text vfn = Text(vlp->directory) + PathnameSep + VersionFileName;
    ifstream vf;
    do {
      // Retry loop to work around Tru64 NFS weirdness.  If the
      // version file has just been updated on another host, our NFS
      // client may have a mapping to the filehandle of the old version
      // file in its cache.  Arrgh!
      vf.open(vfn.cchars());
    } while (vf.fail() && errno == ESTALE);

    if (vf.fail()) {
	vlp->ccVersion = 0;
    } else {
	vf >> vlp->ccVersion;
    }
    vf.close();
    if (ver == -1) {
	vlp->version = vlp->ccVersion;
    } else {
	vlp->version = ver;
    }

    // Find highest committed checkpoint in backup directory
    if (vlp->directory2) {
      Text bvfn = Text(vlp->directory2) + PathnameSep + VersionFileName;
      ifstream bvf;
      do {
	bvf.open(bvfn.cchars());
      } while (bvf.fail() && errno == ESTALE);

      if (bvf.fail()) {
	vlp->ccVersion2 = 0;
      } else {
	bvf >> vlp->ccVersion2;
      }
      bvf.close();
    }

    // Check for invalid ver parameter
    if (vlp->version > vlp->ccVersion) {
	vlp->state = VestaLogPrivate::bad;
	throw Error(0,
	  "VestaLog::open parameter error: ver > last committed checkpoint");
    }

    // Open the logfile
    char vstr[16];
    sprintf(vstr, "%d", vlp->version);
    Text lfn = Text(vlp->directory) + PathnameSep + vstr + LogExtension;
    vlp->fd = ::open(lfn.cchars(),
		     (vlp->readonly ? O_RDONLY : O_RDWR) |
		     (vlp->version == 0 ? O_CREAT : 0), LOG_PROT);
    if (vlp->fd == -1) {
	vlp->state = VestaLogPrivate::bad;
	throw Error(errno, Text("VestaLog::open got \"") +
		    Basics::errno_Text(errno) + "\" opening " + lfn);
    }

    // Open the backup logfile if there is a backup
    vlp->fd2 = -1;
    if (vlp->directory2) {
      // Check if the primary was 0-length; if so, ok to create the backup
      struct stat statbuf;
      if (fstat(vlp->fd, &statbuf) != 0) {
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::open got \"")+
			      Basics::errno_Text(errno) + "\" on fstat");
      }
      Text lfn2 =
	Text(vlp->directory2) + PathnameSep + vstr + LogExtension;
      vlp->fd2 = ::open(lfn2.cchars(), O_RDWR |
			(statbuf.st_size == 0 ? O_CREAT | O_TRUNC : 0),
			LOG_PROT);
      if (vlp->fd2 == -1) {
	vlp->state = VestaLogPrivate::bad;
	throw Error(errno, Text("VestaLog::open got \"") +
		    Basics::errno_Text(errno) + "\" opening " + lfn2);
      }
    }

    // Initialize for reading
    vlp->curSeq = vlp->curLen = vlp->nextSeq = vlp->nextPhy = 0;
    vlp->hitEOF = vlp->usePocket = false;
    vlp->commSeq = vlp->commPhy = vlp->commPocketPhy = 0;
    vlp->commUsePocket = false;
    vlp->cur = vlp->last = vlp->pocket = vlp->free = NULL;
    vlp->nesting = 0;

    // Ready to go
    vlp->state = VestaLogPrivate::recovering;
    vlp->checkpointing = false;
}

// Return the version number of the logfile currently open.
int VestaLog::logVersion() throw (VestaLog::Error)
{
    assert(vlp->state != VestaLogPrivate::initial);
    assert(vlp->state != VestaLogPrivate::bad);
    return vlp->version;
}

// Open the checkpoint that this log starts from.  
// NULL for the first log.
fstream* VestaLog::openCheckpoint() throw (VestaLog::Error)
{
    assert(vlp->state == VestaLogPrivate::recovering);
    if (vlp->version == 0) return NULL;
    char vstr[16];
    sprintf(vstr, "%d", vlp->version);
    Text cfn = Text(vlp->directory) + PathnameSep + vstr + CheckpointExtension;
    fstream* ret = NEW_CONSTR(fstream, (cfn.cchars(), ios::in));
    if (ret->fail()) {
	vlp->state = VestaLogPrivate::bad;
	throw Error(errno, Text("VestaLog::openCheckpoint got \"") +
		    Basics::errno_Text(errno) + "\" opening " + cfn);
    } else {
	return ret;
    }
}

void VestaLog::get(char& c) throw (VestaLog::Eof, VestaLog::Error)
{
    assert(vlp->state == VestaLogPrivate::recovering);
    
    vlp->makeBytesAvail();
    c = vlp->cur->data->bytes[vlp->curLen++];
}

inline static VLog::seq_t HashSeq(VLog::seq_t seq)
{
    return (seq + 12345u) * 715827881u;
}

// Common routine used by makeBytesAvail and extendCur to read the
// next block.  Returns NULL if we hit EOF.  If log has a backup,
// reads both copies and checks that they match; if they don't, the
// log effectively ends here.
VLogBlock* VestaLogPrivate::readBlock() throw(VestaLog::Error)
{
  // Read primary
  VLogBlock* block = balloc();
  ssize_t res = ::read(fd, (char*) block->data, DiskBlockSize);
  if(res < 0) {
    // Error reading primary
    bfree(block);
    state = bad;
    throw
      VestaLog::Error(errno, Text("VestaLog::readBlock got \"") +
		      Basics::errno_Text(errno) + "\" on read" +
		      (fd2 == -1 ? "" : " from primary"));
  }
  else if(res < DiskBlockSize) {
    // Hit end of file on primary
    bfree(block);
    block = NULL;
  } 
  else {
    // Read primary OK
    assert(res == DiskBlockSize);
  }

  // Done if no backup
  if (fd2 == -1) {
    if (block == NULL) hitEOF = true;
    return block;
  }

  // Read backup
  VLogBlock* block2 = balloc();
  res = ::read(fd2, (char*) block2->data, DiskBlockSize);
  if(res < 0) {
    // Error reading backup
    bfree(block2);
    bfree(block);
    state = bad;
    throw
      VestaLog::Error(errno, Text("VestaLog::readBlock got \"") +
		      Basics::errno_Text(errno) + "\" on read from backup");
  }
  else if (res < DiskBlockSize) {
    bfree(block2);
    // Hit end of file on backup
    block2 = NULL;
  } 
  else {
    // Read backup OK
    assert(res == DiskBlockSize);
  }

  bool invalidate = false;
  if (block == NULL) {
    // Hit EOF on primary
    if (block2 == NULL) {
      // Hit EOF on both primary and backup
      hitEOF = true;
    } else {
      // Hit EOF on primary only; treat as invalid block
      block = block2;
      invalidate = true;
    }
  } else {
    // Read primary OK
    if (block2 == NULL) {
      // Hit EOF on backup only; treat as invalid block
      invalidate = true;
    } else {
      // Read both logs OK; check if they match
      if (block->data->getSeq() != block2->data->getSeq() ||
	  block->data->getLen() != block2->data->getLen() ||
	  block->data->getVer() != block2->data->getVer()) {
	// Blocks do not match; treat as invalid block
	invalidate = true;
      } else {
	// All is well
      }
      bfree(block2);
    }
  }

  if (invalidate) {
    block->data->setSeq(HashSeq((VLog::seq_t) 0xffffffff));
    block->data->setLen((VLog::len_t) 0xffff);
    block->data->setVer((VLog::len_t) 0xffff);
  }
  return block;
}

void VestaLogPrivate::extendCur() throw(VestaLog::Eof, VestaLog::Error)
{
    // Extend the cur chain by one block, if possible
    // Precondition: on entry, there is a block in pocket unless we
    // have already hit EOF.

    VLogBlock* block = NULL;

    if (!hitEOF) {
      // Need to read a block
      block = readBlock();
      if (block) {
	block->phy = nextPhy++;
      } else {
	hitEOF = true;
      }
    }
    VLog::seq_t hNextSeq = HashSeq(nextSeq);

    if (block == NULL) {
	if (pocket == NULL) {
	    throw VestaLog::Eof();
	}
	// Only one block to consider
	if (pocket->data->getSeq() != hNextSeq) {
	    // This is not the block we need
	    throw VestaLog::Eof();
	}
	block = pocket;
	pocket = NULL;
	block->pocketPhy = nextPhy;
    } else if (pocket == NULL) {
	// Only one block to consider
	if (block->data->getSeq() != hNextSeq) {
	    bfree(block);  // forget we read this block
	    nextPhy--;
	    hitEOF = true;
	    throw VestaLog::Eof();
	}
	block->pocketPhy = nextPhy;
    } else {
	// Two blocks to consider
	if (block->data->getSeq() == hNextSeq) {
	    // block is a version of the block we need
	    if (pocket->data->getSeq() == hNextSeq) {
		// Both are versions of the block we need
		if (pocket->data->getVer() ==
		    ((block->data->getVer() + 1) % 4)) {
		    // Pocket block is current; swap
		    VLogBlock* temp = pocket;
		    pocket = block;
		    block = temp;
		}
	    }
	} else {
	    // block is not the block we need
	    if (pocket->data->getSeq() == hNextSeq) {
		// pocket is the block we need
		// Need to swap blocks
		VLogBlock* temp = pocket;
		pocket = block;
		block = temp;
	    } else {
		// Neither is the block we need
		bfree(block);  // forget we read this block
		nextPhy--;
		hitEOF = true;
		throw VestaLog::Eof();
	    }
	}
	block->pocketPhy = pocket->phy;
    }

    // Link block into the chain
    block->next = NULL;
    if (last == NULL) {
	cur = last = block;
    } else {
	last->next = block;
	last = block;
    }
    block->tailCommitted = false;
    nextSeq++;
}

void VestaLogPrivate::makeBytesAvail() throw (VestaLog::Eof, VestaLog::Error)
{
    // Get a block in pocket if possible
    if (pocket == NULL && !hitEOF) {
      pocket = readBlock();
      if (pocket) {
	pocket->phy = nextPhy++;
	pocket->pocketPhy = UNUSED_PHY; // not meaningful yet
      } else {
	hitEOF = true;
      }
    }

    // Discard fully read block if any
    if (cur != NULL && curLen >= sizeof(cur->data->bytes)) {
	VLogBlock* temp = cur;
	cur = cur->next;
	if (cur == NULL) last = NULL;
	if (temp->tailCommitted && cur != NULL && cur->data->getLen() == 0) {
	    cur->tailCommitted = true;
	}
	bfree(temp);
	curSeq++;
	curLen = 0;
    }
    
    // Get a block to read from if none
    if (cur == NULL) {
	extendCur();
    }
    
    // If necessary, look ahead to see whether bytes are committed
    if (!cur->tailCommitted &&
	curLen >= cur->data->getLen()) {
	VLogBlock* block = cur;
	do {
	    block = block->next;
	    if (block == NULL) {
		extendCur();
		block = last;
	    }
	} while (block->data->getLen() == 0);
	// if we get here, the bytes are committed
	cur->tailCommitted = true;
    }
}


void VestaLog::get(char* p, int n, char term)
  throw (VestaLog::Eof, VestaLog::Error)
{
    assert(vlp->state == VestaLogPrivate::recovering);
    
    do {
	vlp->makeBytesAvail();

	// Return as many bytes from this block as we can
	while (n > 1
	       && (vlp->cur->tailCommitted
		   || vlp->curLen < vlp->cur->data->getLen())
	       && (vlp->curLen < sizeof(vlp->cur->data->bytes))) {
	    *p = vlp->cur->data->bytes[vlp->curLen];
	    if (*p == term) break;
	    vlp->curLen++;
	    p++;
	    n--;
	}
	
    } while (n > 1 && *p != term);
    
    *p = '\0';
}

int VestaLog::read(char* p, int n) throw (VestaLog::Error)
{
    assert(vlp->state == VestaLogPrivate::recovering);
    
    int count = 0;
    do {
	try {
	    vlp->makeBytesAvail();
	} catch (Eof) {
	    return count;
	}

	// Return as many bytes from this block as we can
	while (count < n
	       && (vlp->cur->tailCommitted
		   || vlp->curLen < vlp->cur->data->getLen())
	       && (vlp->curLen < sizeof(vlp->cur->data->bytes))) {
	    *p = vlp->cur->data->bytes[vlp->curLen];
	    vlp->curLen++;
	    p++;
	    count++;
	}
	
    } while (count < n);

    return count;
}

void VestaLog::readAll(char* p, int n) throw (VestaLog::Eof, VestaLog::Error)
{
    assert(vlp->state == VestaLogPrivate::recovering);
    
    int count = 0;
    do {
	vlp->makeBytesAvail();

	// Return as many bytes from this block as we can
	while (count < n
	       && (vlp->cur->tailCommitted
		   || vlp->curLen < vlp->cur->data->getLen())
	       && (vlp->curLen < sizeof(vlp->cur->data->bytes))) {
	    *p = vlp->cur->data->bytes[vlp->curLen];
	    vlp->curLen++;
	    p++;
	    count++;
	}
	
    } while (count < n);
}

bool VestaLog::eof() throw (VestaLog::Error)
{
    assert(vlp->state == VestaLogPrivate::recovering);
    try {
	vlp->makeBytesAvail();
    } catch (Eof) {
	return true;
    }
    return false;
}

void VestaLogPrivate::eraseUncommitted(int fd) throw (VestaLog::Error)
{
    // If there are any blocks in the file beyond the last one that
    // contains valid log data, overwrite them with invalid log
    // sequence numbers.  This is needed because some of the blocks
    // could contain valid sequence numbers left from a write that was
    // in progress at the time of the last crash or abort.  We need to
    // make sure these are not made to look like valid blocks by the
    // new writes we do.  If we had a block left in pocket, we
    // must overwrite it too.
    //
	
    VLogBlock* inval = balloc();
    inval->data->setSeq(HashSeq((VLog::seq_t) 0xffffffff));
    inval->data->setLen((VLog::len_t) 0xffff);
    inval->data->setVer((VLog::len_t) 0xffff);

    struct stat statbuf;
    if (fstat(fd, &statbuf) != 0) {
	state = bad;
	throw VestaLog::Error(errno, Text("VestaLog::eraseUncommitted got \"")+
			      Basics::errno_Text(errno) + "\" on fstat");
    }
    off_t clearstart = (cur->phy + (usePocket ? 1 : 0)) * DiskBlockSize;
    off_t file_offset = cur->pocketPhy * DiskBlockSize;
    if(usePocket && file_offset < clearstart) {
      (void) lseek(fd, file_offset, SEEK_SET);
      ssize_t res = ::write(fd, (const char*) inval->data, DiskBlockSize);
      if (res != DiskBlockSize) {
	state = bad;
	throw VestaLog::Error(errno,
			      Text("VestaLog::eraseUncommitted got \"") +
			      Basics::errno_Text(errno) + "\" on write");
      }
    }
    (void) lseek(fd, clearstart, SEEK_SET);
    while (clearstart < statbuf.st_size) {
	ssize_t res =
	  ::write(fd, (const char*) inval->data, DiskBlockSize);
	if (res != DiskBlockSize) {
	    state = bad;
	    throw VestaLog::Error(errno,
				  Text("VestaLog::eraseUncommitted got \"") +
				  Basics::errno_Text(errno) + "\" on write");
	}
	clearstart += DiskBlockSize;
    }
    bfree(inval);
    if (fsync(fd) != 0) {
	state = bad;
	throw VestaLog::Error(errno, Text("VestaLog::eraseUncommitted got \"")+
			      Basics::errno_Text(errno) + "\" on fsync");
    }
}

bool VestaLog::nextLog() throw (VestaLog::Error)
{
    // state: recovering -> (recovering | recovered)
    assert(vlp->state == VestaLogPrivate::recovering);
    assert(vlp->hitEOF);  // recovery has to have read everything
    assert(vlp->cur == NULL || !vlp->cur->tailCommitted);

    // open next log file if it exists
    char vstr[16];
    sprintf(vstr, "%d", vlp->version + 1);
    Text lfn = Text(vlp->directory) + PathnameSep + vstr + LogExtension;
    int fd =
      ::open(lfn.cchars(), (vlp->readonly ? O_RDONLY : O_RDWR), LOG_PROT);
    if (fd == -1) {
	if (errno == ENOENT) {
	    vlp->state = VestaLogPrivate::recovered;
	    return false;  // no more logs
	} else {
	    vlp->state = VestaLogPrivate::bad;
	    throw Error(errno, Text("VestaLog::nextLog got \"") +
			Basics::errno_Text(errno) + "\" opening " + lfn);
	}
    }

    // Open the next backup logfile if there is a backup
    int fd2 = -1;
    if (vlp->directory2) {
      // Check if the primary was 0-length; if so, ok to create the backup
      struct stat statbuf;
      if (fstat(fd, &statbuf) != 0) {
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::nextLog got \"")+
			      Basics::errno_Text(errno) + "\" on fstat");
      }
      Text lfn2 = Text(vlp->directory2) + PathnameSep + vstr + LogExtension;

      fd2 = ::open(lfn2.cchars(), (vlp->readonly ? O_RDONLY : O_RDWR),
		   (statbuf.st_size == 0 ? O_CREAT | O_TRUNC : 0), LOG_PROT);
      if (fd2 == -1) {
	vlp->state = VestaLogPrivate::bad;
	throw Error(errno, Text("VestaLog::nextLog got \"") +
		    Basics::errno_Text(errno) + "\" opening " + lfn2);
      }
    }

    // copied from close()
    while (vlp->cur != NULL) {
	VLogBlock* temp = vlp->cur->next;
	delete vlp->cur;
	vlp->cur = temp;
    }
    vlp->last = NULL;
    if (vlp->pocket != NULL) {
	delete vlp->pocket;
	vlp->pocket = NULL;
    }
    while (vlp->free != NULL) {
	VLogBlock* temp = vlp->free->next;
	delete vlp->free;
	vlp->free = temp;
    }

    (void) ::close(vlp->fd);
    vlp->fd = fd;
    vlp->version++;

    if (vlp->directory2) {
      (void) ::close(vlp->fd2);
      vlp->fd2 = fd2;
    }      

    // Initialize for reading
    vlp->curSeq = vlp->curLen = vlp->nextSeq = vlp->nextPhy = 0;
    vlp->hitEOF = vlp->usePocket = false;
    vlp->commSeq = vlp->commPhy = vlp->commPocketPhy = 0;
    vlp->commUsePocket = false;
    vlp->cur = vlp->last = vlp->pocket = vlp->free = NULL;
    vlp->nesting = 0;

    return true;
}

void VestaLog::loggingBegin() throw (VestaLog::Error)
{
    // state: recovered -> ready
    assert(vlp->state == VestaLogPrivate::recovered);
    assert(vlp->hitEOF);  // recovery has to have read everything
    assert(vlp->cur == NULL || !vlp->cur->tailCommitted);
    
    // Initialize for writing.
    assert(!vlp->readonly);

    // Note: vlp->curLen > 0 implies a partial version of the current 
    // logical block is already on disk, in which case vlp->cur is a
    // copy of it.   Otherwise the current logical block is empty and
    // not yet on disk, in which case cur may be NULL or may contain a
    // garbage block. 

    // Establish new invariants on cur->phy and cur->pocketPhy.  Now
    // cur->phy will be one possible physical address for the current
    // logical block, and it will be greater than or equal to the
    // highest physical address that contains a valid block.
    // cur->pocketPhy will be the other possible physical address for
    // the current logical block, and cur->pocketPhy < cur->phy.
    // Also, set vlp->usePocket = true if cur->phy contains a valid
    // copy of the block (i.e., if it must not be overwritten).
    //
    if (vlp->curLen > 0) {
	// The current logical block is already partially written on
	// disk, and vlp->cur is a copy of the latest version of it.
	assert(vlp->cur != NULL);
	assert(vlp->cur->pocketPhy != UNUSED_PHY);
	if (vlp->cur->pocketPhy >= vlp->cur->phy) {
	    int temp = vlp->cur->phy;
	    vlp->cur->phy = vlp->cur->pocketPhy;
	    vlp->cur->pocketPhy = temp;
	    vlp->usePocket = false; // pocketPhy is now where block came from
	} else {
	    vlp->usePocket = true;  // phy is still where block came from
	}
    } else {
	// The current logical block is empty.  Either vlp->cur is
	// NULL or it is a block of uncommitted data.
	if (vlp->cur == NULL) {
	    vlp->cur = vlp->balloc();
	    if (vlp->pocket == NULL) {
		vlp->cur->pocketPhy = vlp->nextPhy;
		vlp->cur->phy = vlp->nextPhy + 1;
	    } else {
		vlp->cur->pocketPhy = vlp->pocket->phy;
		vlp->cur->phy = vlp->nextPhy;
	    }
	    vlp->usePocket = true;  // always use lower block first
	} else {
	    assert(vlp->cur->pocketPhy != UNUSED_PHY);
	    if (vlp->cur->pocketPhy >= vlp->cur->phy) {
		int temp = vlp->cur->phy;
		vlp->cur->phy = vlp->cur->pocketPhy;
		vlp->cur->pocketPhy = temp;
	    }
	    vlp->usePocket = true;  // always use lower block first
	}
	vlp->cur->data->setSeq(HashSeq(vlp->curSeq));
	vlp->cur->data->setLen(0);
    }
    assert(vlp->cur->pocketPhy < vlp->cur->phy);

    // Erase leftover uncommitted data from before crash
    vlp->eraseUncommitted(vlp->fd);
    if (vlp->fd2 != -1) {
      vlp->eraseUncommitted(vlp->fd2);
    }

    // Free probably-unneeded block buffers
    while (vlp->cur->next != NULL) {
	VLogBlock* temp = vlp->cur->next->next;
	delete vlp->cur->next;
	vlp->cur->next = temp;
    }
    vlp->last = NULL;
    if (vlp->pocket != NULL) {
	delete vlp->pocket;
	vlp->pocket = NULL;
    }
    while (vlp->free != NULL) {
	VLogBlock* temp = vlp->free->next;
	delete vlp->free;
	vlp->free = temp;
    }

    // Save info to allow abort()
    vlp->commSeq = vlp->curSeq;
    vlp->commPhy = vlp->cur->phy;
    vlp->commPocketPhy = vlp->cur->pocketPhy;
    vlp->commUsePocket = vlp->usePocket;

    vlp->state = VestaLogPrivate::ready;
}

void VestaLog::start() throw (VestaLog::Error)
{
    // Start a record, or increment start nesting level
    // state: ready -> logging
    //        logging -> logging
    if (vlp->state == VestaLogPrivate::ready) {
	vlp->nesting = 1;
	vlp->state = VestaLogPrivate::logging;
    } else {
	assert(vlp->state == VestaLogPrivate::logging);
	vlp->nesting++;
    }
}

int VestaLog::nesting() throw ()
{
    // Return the nesting level
    // state: *
    return vlp->nesting;
}

void VestaLogPrivate::writeCur() throw (VestaLog::Error)
{
    cur->data->setVer(cur->data->getVer() + 1);
    off_t byteAddr;
    if (usePocket) {
      byteAddr = cur->pocketPhy * DiskBlockSize;
    } 
    else {
      byteAddr = cur->phy * DiskBlockSize;
    }
    (void) lseek(fd, byteAddr, SEEK_SET);
    ssize_t res = ::write(fd, (const char*) cur->data, DiskBlockSize);
    if (res != DiskBlockSize) {
	state = bad;
	throw VestaLog::Error(errno, Text("VestaLogPrivate::writeCur got \"") +
			      Basics::errno_Text(errno) + "\" on write" +
			      (fd2 == -1 ? "" : " + to primary"));
    }
    if (fd2 != -1) {
      (void) lseek(fd2, byteAddr, SEEK_SET);
      res = ::write(fd2, (const char*) cur->data, DiskBlockSize);
      if (res != DiskBlockSize) {
	state = bad;
	throw VestaLog::Error(errno, Text("VestaLogPrivate::writeCur got \"") +
			      Basics::errno_Text(errno) +
			      "\" on write to backup");
      }
    }
}

void VestaLogPrivate::makeSpaceAvail() throw (VestaLog::Error)
{
    // Make space to write in cur buffer
    assert(curLen >= sizeof(cur->data->bytes));
    
    // Write out the full block
    writeCur();

    // Prepare cur to receive next block
    curSeq++;
    curLen = 0;
    if (usePocket) {
	// writeCur() used cur->pocketPhy
	// cur->phy is still available
	cur->pocketPhy = cur->phy;
	cur->phy++;
    } else {
	// writeCur() used cur->phy 
	// cur->pocketPhy is still available
	cur->phy++;
    }
    // Do not overwrite the block holding the previous stable commit
    usePocket = (bool) (cur->pocketPhy !=
			   (commUsePocket ? commPhy : commPocketPhy));

    cur->data->setSeq(HashSeq(curSeq));
    cur->data->setLen(0);
    cur->data->setVer(0);
}

void VestaLog::put(char c) throw (VestaLog::Error)
{
    // state: logging
    assert(vlp->state == VestaLogPrivate::logging);
    if (vlp->curLen >= sizeof(vlp->cur->data->bytes)) {
	vlp->makeSpaceAvail();
    }
    vlp->cur->data->bytes[vlp->curLen++] = c;
}

void VestaLog::put(const char* p) throw (VestaLog::Error)
{
    // Put null-terminated string
    // state: logging
    assert(vlp->state == VestaLogPrivate::logging);
    while (*p != '\0') {
	if (vlp->curLen >= sizeof(vlp->cur->data->bytes)) {
	    vlp->makeSpaceAvail();
	}
	vlp->cur->data->bytes[vlp->curLen++] = *p++;
    }
}

void VestaLog::write(const char* p, int n) throw (VestaLog::Error)
{
    // state: logging
    assert(vlp->state == VestaLogPrivate::logging);
    int count = 0;
    while (count < n) {
	if (vlp->curLen >= sizeof(vlp->cur->data->bytes)) {
	    vlp->makeSpaceAvail();
	}
	vlp->cur->data->bytes[vlp->curLen++] = *p++;
	count++;
    }
}

void VestaLog::commit() throw (VestaLog::Error)
{
    // Commit the current record
    // state: logging -> ready or logging
    assert(vlp->state == VestaLogPrivate::logging);

    if (--vlp->nesting > 0) {
	return;
    }

    vlp->cur->data->setLen(vlp->curLen);
    vlp->writeCur();
    vlp->usePocket = (bool) !vlp->usePocket;
    if (fsync(vlp->fd) != 0) {
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::commit got \"") +
			      Basics::errno_Text(errno) + "\" on fsync" +
			      (vlp->fd2 == -1 ? "" : " of primary"));
    }
    if (vlp->fd2 != -1 && fsync(vlp->fd2) != 0) {
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::commit got \"") +
			      Basics::errno_Text(errno) + "\" on fsync of backup");
    }

    // Save info to allow abort() and to prevent this block from
    //  being overwritten until after the next commit
    vlp->commSeq = vlp->curSeq;
    vlp->commPhy = vlp->cur->phy;
    vlp->commPocketPhy = vlp->cur->pocketPhy;
    vlp->commUsePocket = vlp->usePocket;

    vlp->state = VestaLogPrivate::ready;
}

void VestaLog::abort() throw (VestaLog::Error)
{
    // Abort the current record
    // state: logging -> ready
    assert(vlp->state == VestaLogPrivate::logging);

    if (vlp->curSeq != vlp->commSeq) {
	// Buffer has a new block in it; need to get back old one
	off_t byteAddr;
	if (!vlp->commUsePocket) {
	    // pocketPhy was used last
	    byteAddr = vlp->commPocketPhy * DiskBlockSize;
	} else {
	    // phy was used last
	    byteAddr = vlp->commPhy * DiskBlockSize;
	}
	(void) lseek(vlp->fd, byteAddr, SEEK_SET);
 	ssize_t res = ::read(vlp->fd, (char*) vlp->cur->data,
			   DiskBlockSize);
	if(res < 0) {
	  vlp->state = VestaLogPrivate::bad;
	  throw VestaLog::Error(errno, Text("VestaLog::abort got \"") +
				Basics::errno_Text(errno) + "\" on read");
	}
	else {
	  assert(res == DiskBlockSize);
	}
	vlp->curSeq = vlp->commSeq;
	vlp->cur->phy = vlp->commPhy;
	vlp->cur->pocketPhy = vlp->commPocketPhy;
	vlp->usePocket = vlp->commUsePocket;
    }
    vlp->curLen = vlp->cur->data->getLen();

    // Erase uncommitted blocks
    vlp->eraseUncommitted(vlp->fd);
    if (vlp->fd2 != -1) {
      vlp->eraseUncommitted(vlp->fd2);
    }

    vlp->nesting = 0;
    vlp->state = VestaLogPrivate::ready;
}

fstream* VestaLog::checkpointBegin(ios::openmode mode)
     throw(VestaLog::Error)
{
    // state: ready, !checkpointing -> ready, checkpointing
    assert(vlp->state == VestaLogPrivate::ready);
    assert(!vlp->checkpointing);

    // Clean up any uncommitted checkpoints.
    int ver;
    for (ver = vlp->ccVersion + 1; ver <= vlp->version + 1; ver++) {
      char vstr[16];
      sprintf(vstr, "%d", ver);
      Text cfn =
	Text(vlp->directory) + PathnameSep + vstr + CheckpointExtension;
      (void) ::unlink(cfn.cchars());
    }

    if (vlp->bakckp) {
      // Clean up any uncommitted checkpoints in backup.
      for (ver = vlp->ccVersion2 + 1; ver <= vlp->version + 1; ver++) {
	char vstr[16];
	sprintf(vstr, "%d", ver);
	Text cfn2 =
	  Text(vlp->directory2) + PathnameSep + vstr + CheckpointExtension;
	(void) ::unlink(cfn2.cchars());
      }
    }

    // Open a file to receive a new checkpoint
    char vstr[16];
    sprintf(vstr, "%d", vlp->version + 1);
    Text cfn = Text(vlp->directory) + PathnameSep + vstr + CheckpointExtension;
    // If the checkpoint file doesn;t exist, create it with the right
    // permissions.
    if(!FS::Exists(cfn))
      {
	try
	  {
	    FS::Touch(cfn, LOG_PROT, false);
	  }
	catch(FS::Failure f)
	  {
	    throw VestaLog::Error(f.get_errno(),
				  Text("VestaLog::checkpointBegin got \"") +
				  Basics::errno_Text(f.get_errno()) +
				  "\" creating " + cfn);
	  }
      }
    fstream* ret = NEW_CONSTR(fstream, (cfn.cchars(), mode));
    if (!ret->good()) {
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::checkpointBegin got \"") +
			      Basics::errno_Text(errno) + "\" opening " + cfn);
    }

    // Start a new log, preserving the old one
    if (::close(vlp->fd) != 0) {
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::checkpointBegin got \"") +
			      Basics::errno_Text(errno) + "\" on close" +
			      (vlp->fd2 == -1 ? "" : " of primary"));
    }
    Text lfn = Text(vlp->directory) + PathnameSep + vstr + LogExtension;
    vlp->fd = ::open(lfn.cchars(), O_RDWR | O_CREAT | O_TRUNC, LOG_PROT);
    if (vlp->fd == -1) {
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::checkpointBegin got \"") +
			      Basics::errno_Text(errno) + "\" creating " + lfn);
    }
    vlp->version++;

    // Create backup log if there is a backup
    if (vlp->fd2 != -1) {
      if (::close(vlp->fd2) != 0) {
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::checkpointBegin got \"") +
			      Basics::errno_Text(errno) + "\" on close of backup");
      }
      Text lfn2 = Text(vlp->directory2) + PathnameSep + vstr + LogExtension;
      vlp->fd2 = ::open(lfn2.cchars(), O_RDWR | O_CREAT | O_TRUNC, LOG_PROT);
      if (vlp->fd2 == -1) {
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::checkpointBegin got \"") +
			      Basics::errno_Text(errno) + "\" creating " + lfn2);
      }
    }

    // Initialize for writing next log
    if (vlp->cur == NULL) {
	vlp->cur = vlp->balloc();
    }
    vlp->curSeq = 0;
    vlp->curLen = 0;
    vlp->cur->pocketPhy = 0;
    vlp->cur->phy = 1;
    vlp->usePocket = true;
    vlp->cur->data->setSeq(HashSeq(0));
    vlp->cur->data->setLen(0);

    // Save info to allow abort()
    vlp->commSeq = vlp->curSeq;
    vlp->commPhy = vlp->cur->phy;
    vlp->commPocketPhy = vlp->cur->pocketPhy;
    vlp->commUsePocket = vlp->usePocket;

    vlp->checkpointing = true;
    return ret;
}


void VestaLog::checkpointEnd()
     throw(VestaLog::Error)
{
    // state: ready, checkpointing -> ready, !checkpointing
    assert(vlp->state == VestaLogPrivate::ready);
    assert(vlp->checkpointing);
    
    // Commit the current checkpoint
    Text nvfn = Text(vlp->directory) + PathnameSep + NewVersionFileName;
    FS::OFdStream vf(nvfn.cchars());
    vf << vlp->version << endl;
    if (fsync(vf.fd()) != 0) {
	vf.close();
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::checkpointEnd got \"") +
			      Basics::errno_Text(errno) + "\" on fsync");
    }
    if (!vf.good()) {
	vf.close();
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::checkpointEnd got \"") +
			      Basics::errno_Text(errno) + "\" writing(?)");
    }
    vf.close();
    Text vfn = Text(vlp->directory) + PathnameSep + VersionFileName;
    if (rename(nvfn.cchars(), vfn.cchars()) != 0) {
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::checkpointEnd got \"") +
			      Basics::errno_Text(errno) + "\" renaming \"" +
			      nvfn + "\" to \"" + vfn +"\"");
    }
    vlp->ccVersion = vlp->version;

    // Back up the checkpoint file if requested
    if (vlp->bakckp) {
      // Open the new primary checkpoint
      char vstr[16];
      sprintf(vstr, "%d", vlp->version);
      Text cfn =
	Text(vlp->directory) + PathnameSep + vstr + CheckpointExtension;
      fstream* cstream = NEW_CONSTR(fstream, (cfn.cchars(), ios::in));
      if (cstream->fail()) {
	vlp->state = VestaLogPrivate::bad;
	throw Error(errno, Text("VestaLog::checkpointEnd got \"") +
		    Basics::errno_Text(errno) + "\" opening " + cfn);
      }

      // Create the new backup checkpoint
      Text bfn =
	Text(vlp->directory2) + PathnameSep + vstr + CheckpointExtension;
      FS::FdStream* bstream = NEW_CONSTR(FS::FdStream, (bfn.cchars(), ios::out, LOG_PROT));
      if (bstream->fail()) {
	vlp->state = VestaLogPrivate::bad;
	throw Error(errno, Text("VestaLog::checkpointEnd got \"") +
		    Basics::errno_Text(errno) + "\" opening " + bfn);
      }

      // Copy the data
      do {
	char buf[COPY_SIZE];
	cstream->read(buf, COPY_SIZE);
	int count = cstream->gcount();
	if (cstream->bad()) {
	  vlp->state = VestaLogPrivate::bad;
	  throw Error(errno, Text("VestaLog::checkpointEnd got \"") +
		      Basics::errno_Text(errno) + "\" reading " + cfn);
	}
	bstream->write(buf, count);
	if (bstream->fail()) {
	  vlp->state = VestaLogPrivate::bad;
	  throw Error(errno, Text("VestaLog::checkpointEnd got \"") +
		      Basics::errno_Text(errno) + "\" opening " + bfn);
	}
      } while (!cstream->fail());

      cstream->close();
      bstream->flush();
      if (fsync(bstream->fd()) != 0) {
	bstream->close();
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::checkpointEnd got \"") +
			      Basics::errno_Text(errno) + "\" on fsync");
      }
      bstream->close();

      // Record the commit in the backup directory
      Text bnvfn = Text(vlp->directory2) + PathnameSep + NewVersionFileName;
      FS::OFdStream bnvf(bnvfn.cchars());
      bnvf << vlp->version << endl;
      if (fsync(bnvf.fd()) != 0) {
	bnvf.close();
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::checkpointEnd got \"") +
			      Basics::errno_Text(errno) + "\" on fsync");
      }
      if (!bnvf.good()) {
	bnvf.close();
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::checkpointEnd got \"") +
			      Basics::errno_Text(errno) + "\" writing(?)");
      }
      bnvf.close();
      Text bvfn = Text(vlp->directory2) + PathnameSep + VersionFileName;
      if (rename(bnvfn.cchars(), bvfn.cchars()) != 0) {
	vlp->state = VestaLogPrivate::bad;
	throw VestaLog::Error(errno, Text("VestaLog::checkpointEnd got \"") +
			      Basics::errno_Text(errno) + "\" renaming \"" +
			      bnvfn + "\" to \"" + bvfn +"\"");
      }
      vlp->ccVersion2 = vlp->version;
    }

    vlp->checkpointing = false;
}


void VestaLog::checkpointAbort()
     throw(VestaLog::Error)
{
    // state: ready, checkpointing -> ready, !checkpointing
    assert(vlp->state == VestaLogPrivate::ready);
    assert(vlp->checkpointing);
    
    // Remove the current uncommitted checkpoint.  Needed to prevent
    // checkpointResume from being able to find it.
    assert(vlp->version > vlp->ccVersion);
    char vstr[16];
    sprintf(vstr, "%d", vlp->version);
    Text cfn = Text(vlp->directory) + PathnameSep + vstr + CheckpointExtension;
    (void) ::unlink(cfn.cchars());

    vlp->checkpointing = false;
}

fstream *VestaLog::checkpointResume(ios::openmode mode)
     throw(VestaLog::Error)
{
    // state: recovered, !checkpointing -> recovered, checkpointing
    assert(vlp->state == VestaLogPrivate::recovered);
    assert(!vlp->checkpointing);
    assert(!vlp->readonly);

    // Return NULL if last checkpoint was committed
    if (vlp->version <= vlp->ccVersion) return NULL;

    // Try to open the possibly-existing uncommitted checkpoint
    char vstr[16];
    sprintf(vstr, "%d", vlp->version);
    Text cfn = Text(vlp->directory) + PathnameSep + vstr + CheckpointExtension;
    if(!FS::Exists(cfn))
      // Return NULL if checkpoint was not in progress
      return NULL;
    fstream* ret = NEW_CONSTR(fstream, (cfn.cchars(), mode));
    if (!ret->good()) {
      int saved_errno = errno;
      vlp->state = VestaLogPrivate::bad;
      throw VestaLog::Error(errno, Text("VestaLog::checkpointResume got \"") +
			    Basics::errno_Text(saved_errno) + "\" creating " + cfn);
    }
    
    vlp->checkpointing = true;
    return ret;
}

// Internal routine for VestaLog::prune.
// Called twice, to prune the primary and (if any) the backup.
static void
doPrune(char* directory, int ckpcommitted, int ckpkeep, bool logkeep)
{
    // Get the highest pruned checkpoint version number, to avoid
    // searching back beyond that point for versions to keep.
    Text pfn = Text(directory) + PathnameSep + PrunedFileName;
    int prunedver = -1;
    ifstream pfi(pfn.cchars());
    if (!pfi.fail()) {
	pfi >> prunedver;
    }
    pfi.close();

    // Find checkpoints to keep
    int delver = ckpcommitted; // start at highest committed version
    int nkept = 0;
    // cerr << "highest committed = " << delver << endl;
    while (nkept < ckpkeep && delver > prunedver) {
	// Keep this version
	// cerr << "keeping version " << delver << endl;
	delver--;
	nkept++;
	while (nkept < ckpkeep && delver > prunedver && delver > 0) {
	    // Probe for the next lower committed version.
	    char cfn[MaxFileNameLen];	    
	    sprintf(cfn, "%s%c%d%s", directory, PathnameSep,
		    delver, CheckpointExtension);
	    struct stat junk;
	    if (stat(cfn, &junk) == 0) {
		// Found another committed version
		// cerr << "found version " << delver << endl;
		break;
	    } else {
		if (errno == ENOENT) {
		    // No committed version by this number
		    // cerr << "no version " << delver << endl;
		    delver--;
		} else {
		    throw
		      VestaLog::Error(errno, Text("VestaLog::prune got \"") +
				      Basics::errno_Text(errno) + "\" on stat of \"" +
				      cfn + "\"");
		}
	    }
	}
    }

    // Delete all checkpoint versions <= delver, and if !logkeep, all
    //  log versions <= delver.
    if (delver < 0) {
	// Nothing to do
	// cerr << "nothing to prune" << endl;
	return;
    }
    DIR* dir = opendir(directory);
    if (!dir) {
	throw VestaLog::Error(errno, Text("VestaLog::prune got \"") +
			      Basics::errno_Text(errno) + "\" opening directory \"" +
			      directory +"\"");
    }	
    struct dirent de, *done;
    while (readdir_r(dir, /*OUT*/ &de, /*OUT*/ &done) == 0 && done != NULL) {
	int num;
	char ext[4], junk;
	if (sscanf(de.d_name, "%d.%3c%c", &num, ext, &junk) == 2) {
	    ext[3] = '\0';
	    if (num <= delver
		&& (strcmp(ext, "ckp") == 0
		    || (!logkeep && strcmp(ext, "log") == 0))) {
		// Delete it!
		char delname[MaxFileNameLen];
		sprintf(delname, "%s%c%s", directory,
			PathnameSep, de.d_name);
		// cerr << "pruning " << delname << endl;
		if (::unlink(delname) < 0) {
		    throw
		      VestaLog::Error(errno, Text("VestaLog::prune got \"") +
				      Basics::errno_Text(errno) + "\" unlinking \"" +
				      delname +"\"");
		}
	    }
	}
    }   
    closedir(dir);

    // Done, record what we did.  This keeps us from counting down all
    //  the way to 0 in the probe loop if the user asks us to keep more
    //  versions than there currently are.  This code is a bit over-
    //  engineered; I adapted it from writing the version file.
    Text npfn = Text(directory) + PathnameSep + NewPrunedFileName;
    FS::OFdStream pfo(npfn.cchars());
    pfo << delver << endl;
    if (fsync(pfo.fd()) != 0 || !pfo.good()) {
	pfo.close();
	return;  // ignore it, who really cares?
    }
    pfo.close();
    (void) ::rename(npfn.cchars(), pfn.cchars());
}

void VestaLog::prune(int ckpkeep, bool logkeep, bool prunebak)
  throw (VestaLog::Error)
{
    // state: !initial & !bad
    assert(vlp->state != VestaLogPrivate::initial);
    assert(vlp->state != VestaLogPrivate::bad);
    assert(!vlp->readonly);

    try {
      // Prune the primary
      doPrune(vlp->directory, vlp->ccVersion, ckpkeep, logkeep);

      // Prune the backup
      if (prunebak && vlp->directory2) {
	doPrune(vlp->directory2, vlp->ccVersion2, ckpkeep, logkeep);
      }
    } catch (Error) {
      vlp->state = VestaLogPrivate::bad;
      throw;
    }
}

void VestaLog::close() throw ()
{
    // state: * -> initial
    switch (vlp->state) {
      case VestaLogPrivate::initial:
	break;

      case VestaLogPrivate::recovering:
      case VestaLogPrivate::ready:
      case VestaLogPrivate::logging:
      case VestaLogPrivate::recovered:
      case VestaLogPrivate::bad:
	if (vlp->fd != -1) {
	    (void) ::close(vlp->fd);
	}
	if (vlp->fd2 != -1) {
	    (void) ::close(vlp->fd2);
	}
	if (vlp->lockfd != -1) {
	    (void) ::close(vlp->lockfd);
	}
	if (vlp->lockfd2 != -1) {
	    (void) ::close(vlp->lockfd2);
	}
	break;
    }
    while (vlp->cur != NULL) {
	VLogBlock* temp = vlp->cur->next;
	delete vlp->cur;
	vlp->cur = temp;
    }
    vlp->last = NULL;
    if (vlp->pocket != NULL) {
	delete vlp->pocket;
	vlp->pocket = NULL;
    }
    while (vlp->free != NULL) {
	VLogBlock* temp = vlp->free->next;
	delete vlp->free;
	vlp->free = temp;
    }
    vlp->state = VestaLogPrivate::initial;
    if (vlp->directory != NULL) {
	delete [] vlp->directory;
	vlp->directory = NULL;
    }
}

