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

// Created on Mon Jul 14 10:38:04 PDT 1997 by heydon

// ParseImports.H -- function for parsing a model's imports

#ifndef _PARSE_IMPORTS_H
#define _PARSE_IMPORTS_H

#include <Basics.H>
#include <FS.H>
#include <Sequence.H>
#include <Generics.H>
#include <SRPC.H>

typedef TextSeq ImportID;

class Import {
  public:
    Text path;
    Text orig;
    int origOffset;
    ImportID id;
    int start, end;
    bool local;
    bool fromForm;
    bool noUpdate;
    Import(const Text &path, const Text &orig, int origOffset, const ImportID &id,
	   int start, int end,
	   bool local, bool fromForm=false, bool noUpdate=false) throw ()
      : path(path), orig(orig), origOffset(origOffset), id(id),
	start(start), end(end), local(local),
        fromForm(fromForm), noUpdate(noUpdate) { /*SKIP*/ }
    Import(const Import &other)
      : path(other.path), orig(other.orig), origOffset(other.origOffset), id(other.id),
	start(other.start), end(other.end),
	local(other.local), fromForm(other.fromForm), noUpdate(other.noUpdate)
    { /*SKIP*/ }
};

/* An "Import" represents the importation of a model "m'" by a model "m".
   If "i: Import", then "i.path" is the full, absolute pathname of the
   imported model "m'", "i.orig" is the portion of "m" denoting the
   importation of "m'", [ "i.start", "i.end" ) is the half-open interval
   of zero-based character positions in the model "m" bounding the text
   "i.orig" of the imported model, "local" is true iff the model is named by
   a relative path, "fromForm" is true if and only if the imported model
   is named in a "from ... import" clause, and "noUpdate" is true if and
   only if the model specification "i.orig" was immediately preceeded by
   a "noupdate" or "NOUPDATE" pragma in the input.

   If "!(i.fromForm)", then "i.orig" and "i.path" will be nearly identical,
   with two exceptions: 1) if "i.orig" is relative, then the model's directory
   has been prepended to "i.path", and 2) the "ResolvePath" function (see
   below) is applied to the resulting path.

   If "i.fromForm", then the complete path is actually expressed in two
   parts: the one appearing after "from" but before "import", and the one
   appearing after "import". "i.orig" is set to the latter part of the path,
   while "i.path" is the result of applying the "ResolvePath" function below
   to the concatenation of the two parts (as above, the model's directory is
   prepended to the first part if it is relative). In this case, "i.start"
   and "i.end" bound the positions of the latter part of the path (the
   portion of the model "m" to which "i.orig" is set). */

// a sequence of pointers to "Import" objects
typedef Sequence<Import*> ImportSeq;

namespace ParseImports {
    // parsing error
    class Error {
      public:
	Error(const Text &msg) throw () : msg(msg) { /*SKIP*/ }
	Text const msg; // error message
    };

    class ModelSpace {
      // Space of files that can be opened for reading, with a limited
      // set of operations.  Clients can subclass this interface and 
      // pass an object of the appropriate type in to the P method.
      public: 
        // Operations on files
        virtual char getC() throw (Error, FS::EndOfFile, SRPC::failure) = 0;
	virtual void ungetC(char c) throw () = 0;  // limited to 1 character
	virtual long tell() throw () = 0;

        // Create an object with no file associated; use one of these
        // as the argument to P below.  The getc, ungetc, and tell
        // methods will not be called on it.
        ModelSpace() { };

	// Open a model file in the space.  All the methods may be
        // used on it, including open() itself.  The open method does
        // not use the object, but it cannot be static because it is
        // virtual; that is, we need an object to know which subclass
        // to take the method from.
	virtual ModelSpace* open(const Text &modelname) const
            throw(Error, FS::Failure, SRPC::failure, FS::DoesNotExist) = 0;

	// Is name a file, directory, or neither?
	enum type { file, directory, none };
        virtual type getType(const Text &name) const
	  throw (Error, FS::Failure, SRPC::failure) = 0;

        // Close the associated file, if any
	virtual ~ModelSpace() { };
    };

  //
  // Default ModelSpace implementation using local file system
  //
    class LocalModelSpace : public ModelSpace
    {
    public:
      char getC() throw (ParseImports::Error, FS::EndOfFile);
      inline void ungetC(char c) throw () { ifs.putback(c); };
      inline long tell() throw () { return ifs.tellg(); };
      LocalModelSpace() throw() { }
      ParseImports::ModelSpace* open(const Text &modelname) const
	throw(ParseImports::Error, FS::Failure, FS::DoesNotExist);
      type getType(const Text &name) const throw ();
      LocalModelSpace(const Text &modelname) throw(FS::Failure, FS::DoesNotExist);
      ~LocalModelSpace() { FS::Close(ifs); };
    private:
      std::ifstream ifs;
    };

    void P(const Text &modelname, /*INOUT*/ ImportSeq &seq,
	   const ModelSpace* ms =NULL)
      throw(FS::DoesNotExist, FS::Failure, Error, SRPC::failure);
    /* Read the imports in the model named by the absolute pathname
       "modelname", and append new "Import" objects corresponding to any
       such models to "seq".  The ModelSpace is used to look up the
       model name; if none is provided, the local file system is used.  */

    Text ResolvePath(const Text &path, const Text &wd,
		     const ModelSpace* ms =NULL, int *origOffset = 0)
      throw(Error, FS::Failure, SRPC::failure);
    /* Remove any double-quote characters from "path". If "path" is relative,
       prepend the working directory "wd", which is expected to end in '/'.
       Finally, if the resulting path is a directory, append "build.ves";
       otherwise, if the resulting path does not end in ".ves", append that
       extension. Return the resulting path. */ 
}

std::ostream& operator<<(std::ostream &os, const ParseImports::Error &err)
  throw ();

#endif // _PARSE_IMPORTS_H
