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

// FS.H -- a simple exception-based interface to the file system

#ifndef _FS_H
#define _FS_H

#include <sys/stat.h> // mode constants
#include <errno.h>  // defines "errno" global
#include <Basics.H>

namespace FS
{
    // exceptions
    class Error {};
    class EndOfFile : Error
    {
    private:
      // The number of bytes we tried to read and the numebr we
      // actually read
      size_t requested, read;
      // The final stream position.
      off_t position;
    public:
      EndOfFile() : requested(0), read(0), position(0) { };
      EndOfFile(size_t req, size_t rd, off_t pos) 
	: requested(req), read(rd), position(pos)
      { };

      inline size_t bytes_requested() const throw() { return this->requested; }
      inline size_t bytes_read() const throw() { return this->read; }
      inline off_t final_position() const throw() { return this->position; }
    };
    class DoesNotExist : Error {};
    class AlreadyExists : Error {};
    class Failure : Error {
      public:
	Failure(const Text &opName, const Text &arg = "<none>") throw ()
	  : opName(opName), arg(arg), FSerrno(errno) { }
        Failure(const Text &opName, const Text &arg, int err_no) throw ()
	  : opName(opName), arg(arg), FSerrno(err_no) { }

        // Allow constructing an uninitialized Failure and assignment
        // for buffering a failure.
        Failure() throw()
	  : FSerrno(0) {}
        inline Failure & operator=(const Failure &other)
        {
	  opName = other.opName;
	  arg = other.arg;
	  FSerrno = other.FSerrno;
	  return *this;
	}

        // Accessors for the member variables
        inline int get_errno() const { return this->FSerrno; }
        inline const Text &get_op() const { return this->opName; }
        inline const Text &get_arg() const { return this->arg; }

      private:
	int FSerrno;     // Unix error number
	Text opName;     // name of operation causing failure
	Text arg;        // argument string
    };

    // methods
    void OpenReadOnly(const Text &fname, /*OUT*/ std::ifstream &ifs)
      throw (DoesNotExist, Failure);
    /* REQUIRES Unopened(ifs) */
    /* Open the (existing) file named "fname" for reading, and set "ifs" to an
       input file stream on that file. The stream initially points at the
       start of the file. Throw "DoesNotExist" if the file could not be found;
       throw "Failure" with the associated error code in the event of any
       other failure. */

    void OpenForWriting(const Text &fname, /*OUT*/ std::ofstream &ofs)
      throw (Failure);
    /* REQUIRES Unopened(ofs) */
    /* Open the file named "fname" for writing, and set "ofs" to an output
       file stream on that file. If the file does not exist, it is created.
       If it exists, the file is truncated. */

    void Seek(std::istream &ifs, std::streamoff offset,
      std::ios::seekdir orig = std::ios::beg) throw (Failure);
    void Seek(std::ostream &ofs, std::streamoff offset,
      std::ios::seekdir orig = std::ios::beg) throw (Failure);
    /* Seek to position "offset" relative to position "orig" in the input
       stream "ifs" or the output stream "ofs". If "orig" is "std::ios::beg", then
       offset is the number of bytes from the start of the file, if "orig" is
       "std::ios::cur", then offset is the number of bytes relative to the current
       position, and if "orig" is "std::ios::end", the offset is the number of
       bytes from the end of the file (with positive offsets counting
       backwards). Throws "Failure" in the event of a failure, in which case
       the position of "fd" within the file is unchanged. */

    std::streampos Posn(std::istream &ifs) throw (Failure);
    std::streampos Posn(std::ostream &ofs) throw (Failure);
    /* Return the current position in the input stream "ifs" or the output
	stream "ofs". Throws "FS::Failure" in the event of failure. */

    void Read(std::istream &ifs, char *buff, int n)
      throw (EndOfFile, Failure);
    /* Read "n" bytes from "ifs" into "buff". Throw "EndOfFile" if less than
       "n" bytes are available. Throw "Failure" in the event of any other
       failure. */

    void Write(std::ostream &ofs, char *buff, int n) throw (Failure);
    /* Write the "n" bytes pointed to by "buff" to "ofs". Throw "Failure" in
       the event of an I/O failure. */

    void Copy(std::istream &ifs, std::ostream &ofs, int len)
      throw (EndOfFile, Failure);
    /* Copy "len" bytes from "ifs" to "ofs". */

    bool AtEOF(std::istream &is) throw ();
    /* Return 'true' iff 'is' is at end-of-file. This is the case if the
       'eof' bit of 'is' is set, or if there are no more characters to be
       read from 'is'. Requires that no other error bits are set in 'is'. */

    void Flush(std::ostream &ofs) throw (Failure);
    /* Flush buffered write data for "ofs". Throw "Failure" in
       the event of an I/O failure. */

    void Close(std::ifstream &ifs) throw (Failure);
    void Close(std::ofstream &ofs) throw (Failure);
    void Close(std::fstream &fs) throw (Failure);
    /* Close the input stream "ifs", output stream "ofs", or I/O stream "fs".
       This invokes the stream's "close" method, and then throws "Failure" if
       the method failed. */

    void Delete(const Text &fname) throw (Failure);
    /* Delete the file named "fname". Throws "Failure" in the event of some
       filesystem error. */

    bool DeleteDir(const Text &dirname) throw (Failure);
    /* Atomically test if the directory "dirname" is empty, and if so, delete
       it and return true. If it is non-empty, return false. If "dirname" does
       not name a directory or if any other filesystem error occurs, throw
       "Failure". */

    bool Exists(const Text &fname) throw ();
    /* Test whether the given path exists.  (Note: follows
       symlinks.) */

    bool IsFile(const Text &fname) throw ();
    /* Test whether the given path exists and is a regular file.
       (Note: follows symlinks.) */

    bool IsDirectory(const Text &fname) throw ();
    /* Test whether the given path exists and is a directory.  (Note:
       follows symlinks.) */

    bool IsSymlink(const Text &fname) throw ();
    /* Test whether the given path exists and is a symbolic link. */

    Text Realpath(const Text &fname, bool must_exist = true)
      throw (DoesNotExist, Failure);
    /* Resolve all symbolic links and return the full path.
       Note: this happens by temporarily changing directory. */

    void Touch(const Text &fname,
	       mode_t mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH),
	       bool allowOverwrite = true)
      throw (AlreadyExists, Failure);
    /* Create a file with permissions as specified by mode.  If
       allowOverwrite is false and the file already exists, throw
       "AlreadyExists".  If another error occurs, throw "Failure". */

    Text TempFname(Text prefix, Text (*suffix_func)(),
		   mode_t mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH))
      throw (Failure);
    /* Given a path prefix and a function for generating a suffix,
       create a new file with a unique filename.  This function will
       make as many attempts as necessary to find an unused
       filename. */

    Text MakeTempDir(Text prefix, Text (*suffix_func)(),
		   mode_t mode = (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH))
      throw (Failure);
    /* Similar to TempFname, but create a directory rather than a file. */

    Text GetAbsolutePath(const Text &fname) throw(Failure);
    /* Returns an absolute path equivalent to fname.  (If fname is a
       relative path, it prepends the current working directory to
       it.)  Doesn't check existence of fname. */

    Text RemoveSpecialArcs(const Text &fname) throw();
    /* Remove ".", ".." and empty arcs from fname.  Doesn't check
       existence of fname. */

    void SplitPath(const Text &fname, 
		   /*OUT*/ Text &name, /*OUT*/ Text &parent) throw(Failure);
    /* Split fname into name and parent.  First, fname is converted to
       an equivalent absolute path.  Then "name" is set to its last
       arc and "parent" is set to its parent directory.  Doesn't check
       existence of fname. */

    Text ParentDir(const Text &fname) throw (Failure);
    /* Return a path to the parent directory of the given pathname.
       (Really just a wrapper around SplitPath.) */

    Text CommonParent(const Text &fname1, const Text &fname2, 
		      /*OUT*/Text* rest1 = NULL, /*OUT*/Text* rest2 = NULL);
    /* Returns a path of common parent of two given pathnames, and 
       sets the rest1 and rest2 if they do not point to NULL 
       with values of remaining parts of pathnames. 
       Does not check existence of the pathnames. 
       The input pathnames should be in absolute path format */
  
    Text GetFirstArc(const Text& pathname, /*OUT*/Text* rest = NULL);
    /* Returns first arc from the pathname. If "rest" is not NULL 
       it would be set to the remaining part of the pathname starting 
       from the first non-slash char. */

    Text getcwd() throw (Failure);
    /* Same as getcwd(3) with allocation of needed space and
       conversion of errors to an FS::Failure exception. */

    Text readlink(const Text &fname)
      throw (Failure, DoesNotExist);
    /* Same as readlink(2) with allocation of needed space and
       conversion of errors to an FS::Failure or FS::DoesNotExist
       exception. */
}

std::ostream& operator<<(std::ostream &os, const FS::Failure &f) throw ();
std::ostream& operator<<(std::ostream &os, const FS::EndOfFile &f) throw ();

#endif // _FS_H
