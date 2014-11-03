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

// Created on Sat May 31 14:48:45 PDT 1997 by heydon
// Last modified on Tue Aug  3 13:55:44 EDT 2004 by ken@xorian.net
//      modified on Wed Jul 23 11:47:46 PDT 1997 by heydon

// CacheState.H -- a representation of various attributes of cache server state

#ifndef _CACHE_STATE_H
#define _CACHE_STATE_H

#include <time.h>
#include <Basics.H>
#include <SRPC.H>

class CacheId {
public:
  Text   host;       	 // host of machine on which cache is running
  Text   port;       	 // port on which cache is exporting its service
  Text   stableDir;  	 // directory of the cache server's stable cache
  Text   cacheVersion; // version number of the cache server
  int    intfVersion;  // interface version
  time_t startTime;  	 // time at which cache server became available

  // print
  void Print(std::ostream &os, int indent = 0) const throw ();

  // send/receive
  void Send(SRPC &srpc) const throw (SRPC::failure);
  void Recv(SRPC &srpc) throw (SRPC::failure);
};

class MethodCnts {
public:
  int    freeVarsCnt;  // number of times "FreeVars" method called (*)
  int    lookupCnt;    // number of times "Lookup" method called (*)
  int    addEntryCnt;  // number of times "AddEntry" method called (*)

  // (*) Each of these counts is the number of calls since the cache
  //     server came up.

  MethodCnts() throw ()
    : freeVarsCnt(0), lookupCnt(0), addEntryCnt(0) { /* SKIP */ }

  friend bool operator==(const MethodCnts &c1, const MethodCnts &c2)
    throw ();
  friend bool operator!=(const MethodCnts &c1, const MethodCnts &c2) throw ()
  { return !(c1 == c2); }
  /* Return "true" iff the counts "c1" and "c2" are (not) identical. */
};

class EntryState {
public:
  int    newEntryCnt;  // number of "new" cache entries in memory
  int    oldEntryCnt;  // number of "old" cache entries in memory
  int    newPklSize;   // total size of "new" entry pickled values
  int    oldPklSize;   // total size of "old" entry pickled values

  EntryState() throw ()
    : newEntryCnt(0), oldEntryCnt(0), newPklSize(0), oldPklSize(0)
  { /* SKIP */ }

  EntryState& operator+=(const EntryState &s) throw ();
  /* Increment this object's fields by those of "s". */
};

class CacheState {
public:
  unsigned int virtualSize;  // size of cache server process image (bytes)
  unsigned int physicalSize; // size of cache resident in physical
  // memory (bytes)
  MethodCnts   cnt;          // number of times each method has been called
  unsigned int vmpkCnt;      // number of volatile MPKFiles (in memory)
  unsigned int vpkCnt;       // number of volatile PKFiles (in memory)
  unsigned int entryCnt;     // total number of cache entries in cache
  EntryState   s;            // see EntryState above
  unsigned int hitFilterCnt; // number of indices in hit filter
  unsigned int delEntryCnt;  // number of cache entries pending deletion
  unsigned int mpkWeedCnt;   // number of MultiPKFiles remaining to be weeded

  // print
  void Print(std::ostream &os, int indent = 0) const throw ();

  // send/receive
  void Send(SRPC &srpc) const throw (SRPC::failure);
  void Recv(SRPC &srpc) throw (SRPC::failure);
};

#endif // _CACHE_STATE_H
