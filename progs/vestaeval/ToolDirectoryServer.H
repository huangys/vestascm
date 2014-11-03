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

// ToolDirectoryServer.H

// The following defines the interface for creating an SRPC server 
// to implement "evaluator directories", which provide a file naming
// environment during execution of an encapsulated tool.

// The server is created at the first opportunity and remains in 
// existence for the duration of the evaluator with which it shares
// an address space.

#ifndef ToolDirectoryServer_H
#define ToolDirectoryServer_H

#include "EvalBasics.H"
#include "Val.H"
#include "WordKey.H"

// Initialize
void create_tool_directory_server();

// Data structures shared between RunTool and the server.
// It records the dependency while "spying" on a RunTool call.
class DirInfo {
public:
  Binding b;
  DepPath path;

  // This is the root of the temporary filesystem
  bool isRoot;

  // This directory is to be recorded as a coarse dependency: any
  // access of the directory records a dependency on the entire
  // directory.
  bool coarseDep;

  // Has any lookup been performed in this directory?  Used to handle
  // the case of a directory looked up with no lookups below it
  // (rare).
  bool child_lookup;

  // When a lookup within this directory fails, record a dependency on
  // all the names in this directory (similar to if the directory had
  // been listed) rather than recording a non-existence dependency on
  // the name looked up within it.
  bool coarseNames;

  // Have we already recorded a dependency on the names of this
  // directory?
  bool namesRecorded;

  // Bindings which select which subdirectories are to be treated as
  // coarse dependencies or 
  Binding coarseDep_sel, coarseNames_sel;
};

// DirInfoTbl
typedef Table<WordKey, DirInfo*>::Default DirInfoTbl;
typedef Table<WordKey, DirInfo*>::Iterator DirInfoIter;

class DirInfos {
public:
  Basics::mutex mu; // Used to protect dirInfoTbl and dep
  DirInfoTbl *dirInfoTbl;
  DPaths *dep;
  // The thread label for the corresponding _run_tool call, used in
  // messages printed when processing callbacks (in a different
  // thread).
  Atom thread_label;

  void AddDpnd(DirInfo *dir, const Text& name, Val v, const PathKind pk);
  Word LookupDir(DirInfo *dir, const Text& name, Val v, Word dirHandle);
};

// DirInfosTbl
typedef Table<WordKey, DirInfos*>::Default DirInfosTbl;

class RunToolCalls {
public:
  RunToolCalls(int numCalls) {
    dirInfosTbl = NEW_CONSTR(DirInfosTbl, (numCalls));
  }
  bool Get(const WordKey& k, /*OUT*/ DirInfos*& v) {
    this->mu.lock();
    bool res = this->dirInfosTbl->Get(k, v);
    this->mu.unlock();
    return res;
  }
  bool Put(const WordKey& k, DirInfos*& v, bool resize = true) {
    this->mu.lock();
    bool res = this->dirInfosTbl->Put(k, v, resize);
    this->mu.unlock();
    return res;
  }
  bool Delete(const WordKey& k, /*OUT*/ DirInfos*& v, bool resize = true) {
    this->mu.lock();
    bool res = this->dirInfosTbl->Delete(k, v, resize);
    this->mu.unlock();
    return res;
  }
private:
  DirInfosTbl *dirInfosTbl;
  Basics::mutex mu;
};

// Used to record volatile directories for all pending runtool calls.
extern RunToolCalls runToolCalls;

// Get a new handle for a new volatile directory.
Word GetNewHandle();

#endif // ToolDirectoryServer_H
