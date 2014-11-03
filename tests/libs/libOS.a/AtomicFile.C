// AtomicFile.C

// Copyright (C) 2001, Compaq Computer Corporation

// This file is part of Vesta.

// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

//
// Atomically create or replace files.
//

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <errno.h>
#include <Basics.H>
#include "AtomicFile.H"
#include "FS.H"

class AFStaticStuff {
  public:
    char *suffix;
    int suffixlen; //strlen(suffix);
    AFStaticStuff();
};

static AFStaticStuff afss;

const char AtomicFile::reserved_char = ';';

AFStaticStuff::AFStaticStuff()
{
    time_t now = time(0);
    const int bufsize = 64;
    // I sure would like a real text package!
    suffix = NEW_PTRFREE_ARRAY(char, bufsize);
    sprintf(suffix, "%c%x", AtomicFile::reserved_char, now);
    suffixlen = strlen(suffix);
    assert(suffixlen < bufsize);
}

void AtomicFile::open(const char* name, std::ios::openmode mode, int prot) 
  throw ()
{
    if (!(mode & (std::ios::out|std::ios::app))) {
	clear(std::ios::badbit); // set bad bit
	errno = EINVAL;
	return;
    }
    realname = NEW_PTRFREE_ARRAY(char, strlen(name)+1);
    strcpy(realname, name);
    tempname = NEW_PTRFREE_ARRAY(char, strlen(name) + afss.suffixlen + 1);

    strcpy(tempname, name);
    strcat(tempname, afss.suffix);

    // ISO C++ removed std::ios::noreplace.  The gcc 3 FAQ makes the
    // following stupid suggestion:

    //   To be safe, you can open the file for reading, check if it
    //   has been opened, and then decide whether you want to
    //   create/replace or not.

    // This would create a race condition between two threads in the
    // same program trying to open the same file for an atomic write
    // (which is clearly the point of using std::ios::noreplace).  So
    // we do it the hard way: use open(2) with O_CREAT|O_EXCL, and
    // only open the stream if that succeeds.

    int fd;
    do
      fd = ::open(tempname, O_WRONLY|O_CREAT|O_EXCL, prot);
    while((fd == -1) && (errno == EINTR));

    if(fd != -1)
      {
	FS::OFdStream::open(tempname, mode);
	int close_res;
	do
	  close_res = ::close(fd);
	while((close_res != 0) && (errno == EINTR));
      }
    else
      {
	clear(std::ios::badbit); // set bad bit
      }
}

static void CheckStream(const std::ostream *ofs, const char *msg) {
    if (ofs->good() == 0) {
      std::cerr << "AtomicFile::close error: " << msg << std::endl;
      abort();
    }
}

void AtomicFile::close() throw ()
{
    if (tempname != NULL) {
        /* It is very important to check that this stream is good
           both before and after closing it because even if the
           stream is bad on entry, closing it can make it good again! */
        CheckStream(this, "stream bad on entry");
	// Also, make sure that any pending writes get completed now.
	// If there are pending writes when we call ofstream::close
	// and they fail (i.e. due to the disk filling up), the
	// stream's state may not indicate failure.
	flush();
        CheckStream(this, "stream bad after flush");
	// Make sure we commit all buffered writes to disk before
	// closing and renaming.  (If we don't do this, the system
	// could crash after the rename but before the OS flushes all
	// writes to disk.)
	{
	  int fsync_res;
	  do
	    fsync_res = fsync(this->fd());
	  while((fsync_res != 0) && (errno == EINTR));
	  if(fsync_res != 0)
	    {
	      int errno_save = errno;
	      std::cerr << "AtomicFile::close error on fsync(2): "
			<< Basics::errno_Text(errno_save) << std::endl;
	      abort();
	    }
	}
	FS::OFdStream::close(); // invoke parent method
        CheckStream(this, "close() failed");
        if (rename(tempname, realname) != 0) {
	  int errno_save = errno;
	  std::cerr << "AtomicFile::close error: rename(2) failed!"
		    << std::endl
		    << "  error : " << Basics::errno_Text(errno_save)
		    << " (errno = " << errno_save << ")" << std::endl
		    << "  old = " << tempname << std::endl
		    << "  new = " << realname << std::endl;
	  abort();
	}
	delete[] tempname;
	delete[] realname;
	tempname = realname = NULL;
    }
}

AtomicFile::~AtomicFile() throw ()
{
    if (tempname != NULL) {
      FS::OFdStream::close();
	delete[] tempname;
	delete[] realname;
	tempname = realname = NULL;
    }
}

#include <sys/types.h>
#include <dirent.h>
#if defined(__digital__)
// add declaration to fix broken <dirent.h> header file
extern "C" int _Preaddir_r(DIR *, struct dirent *, struct dirent **);
#endif

void AtomicFile::cleanup(const char* dirname) throw ()
{
    int namebuflen = strlen(dirname) + 258;
    char *namebuf = NEW_PTRFREE_ARRAY(char, namebuflen);
    DIR *dir = opendir(dirname);
    if (!dir) return;
    struct dirent de, *done;
    while (readdir_r(dir, /*OUT*/ &de, /*OUT*/ &done) == 0 && done != NULL) {
	char *suffixpos = strchr(de.d_name, AtomicFile::reserved_char);
	if (!suffixpos) continue;
	if (strcmp(suffixpos, afss.suffix) != 0) {
	  sprintf(namebuf, "%s%c%s", dirname, PathnameSep, de.d_name);
	    // std::cout << "unlinking " << namebuf << std::endl;
	    remove(namebuf);
	}
    }
}

void FS::Close(AtomicFile &vaf) throw (FS::Failure)
{
  // Since AtomicFile::close() is careful about checking the state of
  // the stream and does its own error handling, we don't need to.
  vaf.close();
}

