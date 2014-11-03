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

#ifndef _GRAPH_LOG_H
#define _GRAPH_LOG_H

#include <time.h>         // for time_t in GraphLog::Root
#include <Basics.H>
#include <FS.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <FP.H>

#include "Model.H"
#include "CacheIndex.H"
#include "Derived.H"

/* The "graphLog" is a linked list of "GraphLog::Node" structures. There are
   two kinds of entries: "Root" and "Node". Each type is a subtype of
   the base class "GraphLog::Entry". 

   The base class, "GraphLog::Entry", is not meant to be instantiated.
   Instead, clients should invoke the constructors of the derived "Root" and
   "Node" classes.

   Each class has a "Log" method for pickling an instance of the class to a
   "VestaLog". However, only the "Log" methods of the derived "Root" and
   "Node" classes should be used by clients.*/

class GraphLog {
  public:

    // There are two kinds of graphLog entries
    enum Kind { RootKind, NodeKind };

    // Entry base class; do not instantiate directly!
    class Entry {
      public:
	// The kind of entry
	Kind kind;
	Entry(Kind kind) throw () : kind(kind) { /*SKIP*/ }

	// log/recover
	virtual void Log(VestaLog &log) const throw (VestaLog::Error)
           { log.write((char *)(&(this->kind)), sizeof(this->kind)); }
	static Entry *Recover(RecoveryReader &rd)
	  throw (VestaLog::Error, VestaLog::Eof);

	// read/write
	virtual void Write(std::ostream &ofs) const throw (FS::Failure)
	   { FS::Write(ofs, (char *)(&(this->kind)), sizeof(this->kind)); }
	static Entry *Read(std::istream &ifs) throw (FS::Failure, FS::EndOfFile);

	virtual void Debug(std::ostream &s) const throw ();
	/* Write a one-line representation of this entry to "s". The output is
	   indented and ends with a newline. */

        virtual void DebugFull(std::ostream &s) const throw ();
        /* Write a complete, multi-line, representation of this entry
	   to "s". */

      private:
	// hide copy constructor from clients
	Entry(const Entry &);
    };

    // Node derived class
    class Node: public Entry {
      public:
	Node *next;
	CacheEntry::Index ci;     // cache server index for this entry
	FP::Tag *loc;             // PK for this cache entry
	Model::T model;           // model in which entry's function is defined
	CacheEntry::Indices *kids;// child entries
	Derived::Indices *refs;   // deriveds reachable from this entry

	// Constructors
	Node(CacheEntry::Index ci, FP::Tag *loc, Model::T model,
	  CacheEntry::Indices *kids, Derived::Indices *refs) throw ()
	  : Entry(NodeKind) { Init(ci, loc, model, kids, refs); }
        Node(RecoveryReader &rd) throw (VestaLog::Error, VestaLog::Eof)
	  : Entry(NodeKind) { Recover(rd); }
        Node(std::istream &ifs) throw (FS::Failure, FS::EndOfFile)
	  : Entry(NodeKind) { Read(ifs); }

	void Init(CacheEntry::Index ci, FP::Tag *loc, Model::T model,
	  CacheEntry::Indices *kids, Derived::Indices *refs) throw ();
        /* Initialize the graph log entry to the vector of its arguments. The
	   responsibility for freeing the storage for "loc", "kids", and
	   "refs" remains with the caller. */

	~Node() throw () { this->Reset(); }
	void Reset() throw ()
           { this->loc = NULL; this->kids = NULL; this->refs = NULL; }
        /* Drop memory reachable from this "Node". */

        inline unsigned long Size()
        {
	  assert(this->kids != 0);
	  assert(this->refs != 0);
	  return (sizeof(this->ci) +
		  FP::ByteCnt + // == sizeof(loc)
		  sizeof(this->model) +
		  this->kids->Size() +
		  this->refs->Size());
	}

	// log/recover
	void Log(VestaLog &log) const throw (VestaLog::Error);
	void Recover(RecoveryReader &rd)
          throw (VestaLog::Error, VestaLog::Eof);
        /* NB: The "Recover" constructor assumes the "kind" of node has
	   already been read from the log, and is known to be "NodeKind". */

	// write/read
	void Write(std::ostream &ofs) const throw (FS::Failure);
	void Read(std::istream &ifs) throw (FS::Failure, FS::EndOfFile);
        /* NB: The "Read" constructor assumes the "kind" of node has already
	   been read from the log, and is known to be "NodeKind". */

	// Overridden "Debug" and "DebugFull" methods
	void Debug(std::ostream &os) const throw ();
	void DebugFull(std::ostream &os) const throw ();
      private:
	// hide copy constructor from clients
	Node(const Node &);
    };

    // Root derived class
    class Root: public Entry {
      public:
	FP::Tag pkgVersion;
	Model::T model;
	time_t ts; // timestamp of when this node was created
	CacheEntry::Indices *cis;
	bool done;

	// Constructors
	Root(const FP::Tag &pkgVersion, Model::T model,
          CacheEntry::Indices *cis, bool done) throw ()
	  : Entry(RootKind) { Init(pkgVersion, model, cis, done); }
	Root(RecoveryReader &rd) throw (VestaLog::Error, VestaLog::Eof)
	  : Entry(RootKind) { Recover(rd); }
	Root(std::istream &ifs) throw (FS::Failure, FS::EndOfFile)
	  : Entry(RootKind) { Read(ifs); }

	void Init(const FP::Tag &pkgVersion, Model::T model,
          CacheEntry::Indices *cis, bool done) throw ();
        /* Initialize the graph log entry to the vector of its arguments. The
	   responsibility for freeing the storage for "cis" is transferred
	   from caller to callee. */

	void DeleteContents() throw () { cis = NULL; }
        /* Drop memory reachable from this "Root". */

	// log/recover
	void Log(VestaLog &log) const throw (VestaLog::Error);
	void Recover(RecoveryReader &rd)
          throw (VestaLog::Error, VestaLog::Eof);
        /* NB: This constructor assumes the "kind" of node has already been
	   read from the log, and is known to be "RootKind". */

	// write/read
	void Write(std::ostream &ofs) const throw (FS::Failure);
	void Read(std::istream &ifs) throw (FS::Failure, FS::EndOfFile);
        /* NB: The "Read" constructor assumes the "kind" of node has already
	   been read from the log, and is known to be "NodeKind". */

	// Overridden "Debug" and "DebugFull" methods
	void Debug(std::ostream &os) const throw ();
	void DebugFull(std::ostream &os) const throw ();

      private:
	// hide copy constructor from clients
	Root(const Root &);
    };
};

#endif // _GRAPH_LOG_H
