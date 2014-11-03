// AtomicFile.H

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
#ifndef _ATOMIC_FILE_H
#define _ATOMIC_FILE_H

#include "FdStream.H"
#include "FS.H"

// Open a new file under a temporary name.  Rename it to its real name
// when it is closed. The destructor closes the temporary file, but does
// not rename the temporary file to the real name. To change the name,
// you must invoke the "close" method explicitly.
//
// Temporary names are chosen in such a way that the temporaries can be found
// and deleted by "AtomicFile::cleanup" if the program crashes with files
// still open.
//
// The last component of the filename may not contain reserved_char,
// nor may anything else in its directory.

class AtomicFile : public FS::OFdStream {
    char *tempname, *realname;
  public:
    void open(const char* name, std::ios::openmode mode, int prot = 0644) throw();
    void close() throw();
    ~AtomicFile() throw();

// The following are in the class purely for name scoping purposes

// Remove old temporary files in the specified directory.  Any name
// containing reserved_char is assumed to be a temporary.  Subdirectories
// are not traversed.
//
    static void cleanup(const char* dirname =".") throw();
    static const char reserved_char;  // ';'
};

namespace FS
{
  // Same as the other variants in FS.[HC]
  void Close(AtomicFile &vaf) throw (FS::Failure);
}

#endif // _ATOMIC_FILE_H
