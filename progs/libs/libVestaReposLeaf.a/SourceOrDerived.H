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
// SourceOrDerived.H
// Last modified on Tue Sep 21 11:22:00 EDT 2004 by ken@xorian.net
//      modified on Thu Jul 20 14:24:31 PDT 2000 by mann
//
// This interface provides access to source and derived files named by
// ShortId.  The interface is implemented mostly in library code local
// to each client.  An underlying RPC interface is used to get blocks
// of new ShortIds from the repository server and to inform it of
// values passed to the KeepOnly procedures.

// A process running this code accesses source and derived files
// directly in the underlying filesystem, so it must run with the
// proper permissions.  Usually that means it must run as user
// "vadmin."  (Exception: if only the weeder interface procedures
// are used, no special permissions are needed.)

#ifndef _VSOD
#define _VSOD

#include <sys/stat.h>

#include "Basics.H"
#include "SRPC.H"
#include <fstream>

typedef Bit32 ShortId;
#define NullShortId ((ShortId) 0)
typedef ShortId ShortIdsFile;  // ShortId of a file containing
			       // ShortIds, formatted in hexadecimal,
			       // zero-filled to 8 digits, one per line.

// SourceOrDerived provides access to source and derived files
// named by ShortId.  It is a subclass of the underlying fstream file
// class.  Opening a SourceOrDerived file is somewhat different
// from opening an ordinary file: an existing file is named by ShortId
// in the open call, but for new files, the create call chooses and
// returns a new ShortId, with the caller supplying only the LeafFlag.
//
class SourceOrDerived : public std::fstream {
    void open(const char *name, std::ios::openmode mode)
    { }; // not permitted
public:
    static const mode_t default_prot = (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    // Catchable exception for fatal errors, allowing the caller to
    // print a message and exit instead of dumping core.
    class Fatal {
      public:
	Fatal(const Text msg) throw() : msg(msg) { };
	Text msg;
    };

    // Get or set the filesystem pathname that this package is to use
    // for accessing the metadata root.  If the set method is not
    // called, the value of [Repository]metadata_root in vesta.cfg is
    // used.  This package will open files faster if a shorter
    // pathname can be used.  In particular, the main program may
    // chdir() to the metadata root and pass an empty string to this
    // method.  If the supplied pathname is not empty, it must end
    // with a pathname separator ("/" on Unix).  The caller is
    // responsible for freeing the storage returned by the get method.
    static void setMetadataRootLocalName(char* pathname) throw();
    static char* getMetadataRootLocalName() throw();

    // Create or open a SourceOrDerived as an fstream
    ShortId create(bool leafflag =true, std::ios::openmode mode =std::ios::out,
		   mode_t prot =default_prot)
      throw(SRPC::failure, Fatal);
    void open(ShortId sid, std::ios::openmode mode =std::ios::in) throw();

    // Create or open a SourceOrDerived and return a file descriptor.
    // The oflag and prot arguments are passed directly to the creat()
    // or open() system call.  If the returned value is -1, errno is set.
    static int fdcreate(ShortId& sid, bool leafflag =true,
			mode_t prot =default_prot)
      throw(SRPC::failure, Fatal);
    static int fdopen(ShortId sid, int oflag, mode_t prot =default_prot)
      throw();

    // One bit of a derived's ShortId is a flag to indicate whether
    // the derived may contain pointers (ShortIds) referring to other
    // deriveds, which must be traced by the weeder.  A "leaf"
    // contains none.  The repository does this tracing for directory
    // ShortId's, but not for file ShortId's.  The evaluator and cache
    // are free to create non-leaf files, but the derived weeder is
    // responsible for tracing them.
    //
    static inline bool leafShortId(ShortId sid) throw()
      { return (bool) ((sid & 0x80000000) != 0); };

    // The repository provides ShortIds for immutable directories as
    // well as files.  One bit of a non-leaf ShortId is a flag to
    // indicate whether the ShortId is a directory.
    static inline bool dirShortId(ShortId sid) throw()
      { return (bool) ((sid & 0xc0000000) == 0x40000000); };

    // Convert ShortId to filename.  If tailOnly is true, the name is
    // relative to [Repository]metadata_root as defined in the
    // vesta.cfg file.  If tailOnly is false, the metadata root local
    // name (see above) is prepended, so the returned name can be used
    // directly in the current process, but not necessarily in any
    // other process.  The caller is responsible for freeing the
    // storage allocated.  Applying this method to a dirShortId is
    // invalid.
    static char* shortIdToName(ShortId sid, bool tailOnly =true) throw();

    // Release server resources held by this process.  It is polite
    // (but not essential) to call this routine before exiting.
    // Details: The routine releases a "stock" of ShortIds that are
    // reserved by this process.  If it is not called, the stock will
    // eventually be freed anyway when its lease expires.
    static void releaseResources() throw();

    // Update the last-modified time of a SourceOrDerived to the
    // current time.  This "renews the lease" on the SourceOrDerived;
    // i.e., protects it from deletion by the weeding machinery.
    // Returns false if the sid did not exist (or could not be touched
    // for some other reason); this is a fatal error in the expected
    // use of this function.
    static bool touch(ShortId sid) throw();

    //
    // Weeder interface.  
    //

    // The repository is free to delete any SourceOrDerived that is
    // not a source (that is, does not have a pathname in the
    // repository source name space), that is not a descendent in the
    // namespace of an immutable directory whose ShortId is in ds,
    // that does not have its own ShortId listed in ds, and whose
    // last-modified time is less than dt.
    //
    // Initially ds = NullShortId (representing the empty set) and dt
    // = 0 (a time far in the past, which causes everything to be
    // kept).  Calls to keepDerived update these values, and they are
    // retained stably across repository restarts.  The file ds must
    // contain its own ShortId.
    //
    // In the implementation, keepDerived also asks the repository
    // to recompute its notion of which SourceOrDeriveds are sources
    // ("source weed") and to delete SourceOrDeriveds that are not
    // required to be kept.  The call does not return until the
    // weeding and deletions are finished.  The return value is 0 for
    // success, otherwise an errno value.  Some disk space is required
    // for these operations, so ENOSPC is a possible error.
    //
    // If keepDerived is called a second time with the same
    // parameters, the second call will wait for the first to
    // complete.  Then if force is false and the first call was
    // successful, the second call will return 0 without taking any
    // further action.  If force is true, the second call will start a
    // new weed even if the first one succeeded.
    //
    static int keepDerived(ShortIdsFile ds, time_t dt, bool force =false)
      throw(SRPC::failure);

    // checkpoint() tells the repository to checkpoint its internal
    // state, thereby trucating its recovery log and speeding up
    // recovery after future crashes.  The checkpoint will be smaller
    // if keepDerived was called recently.  Warning: The repository
    // will crash and restart if there is not enough disk space to
    // write the checkpoint.
    static void checkpoint() throw(SRPC::failure);

    // Get the current state of the various low-level weeding
    // parameters.  <ds, dt> are the values described above.  <ss, st>
    // are corresponding values for sources, as determined internally
    // by the last completed source weed.  
    static void getWeedingState(ShortIdsFile& ds, time_t& dt,
				ShortIdsFile& ss, time_t& st,
				bool& sourceWeedInProgress,
				bool& deletionsInProgress,
				bool& deletionsDone,
				bool& checkpointInProgress)
      throw (SRPC::failure);
};

#endif // _VSOD
