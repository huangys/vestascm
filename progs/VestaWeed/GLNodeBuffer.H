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

// Created on Tue Mar 11 14:18:49 PST 1997 by heydon

// Last modified on Mon Aug  9 17:05:09 EDT 2004 by ken@xorian.net
//      modified on Tue Dec 16 00:40:03 PST 1997 by heydon

// GLNodeBuffer -- a buffer of GLNode objects, with an operation for
//   evicting the oldest one.

#ifndef _GL_NODE_BUFFER_H
#define _GL_NODE_BUFFER_H

#include <Basics.H>
#include <IntKey.H>
#include <FS.H>
#include <Table.H>
#include <Sequence.H>
#include <Generics.H>   // for "IntSeq"
#include <CacheIndex.H>
#include <Derived.H>
#include "GLNode.H"

/* The methods of a "GLNodeBuffer" are unmonitored. */

class GLNodeKids {
  public:
    GLNodeKids() throw () { /*SKIP*/ } // default constructor
    GLNodeKids(CacheEntry::Indices *kids, Derived::Indices *refs) throw ()
	: kids(kids), refs(refs) { /*SKIP*/ }
    GLNodeKids& operator= (const GLNodeKids& nk) throw ()
	{ this->kids = nk.kids; this->refs = nk.refs; return *this; }
    CacheEntry::Indices *kids; // child entries
    Derived::Indices *refs;    // deriveds reachable from this entry
};

typedef Table<IntKey,GLNodeKids>::Default GLNodeTbl;
typedef IntSeq CISeq;

class GLNodeBuffer {
  public:
    GLNodeBuffer(int maxSize) throw ();
    /* Initialize this buffer so it can contain at most "maxSize" nodes. */

    bool Delete(CacheEntry::Index ci, /*OUT*/ GLNode &node) throw ();
    /* Look for a node in the buffer with index "ci". If one exists, remove it
       from the buffer, set "node" to the node, and return true. Otherwise,
       return false. */

    void Put(const GLNode &node, std::ostream &ofs) throw (FS::Failure);
    /* Add the node "node" to the buffer; if that causes the buffer to contain
       more than "maxSize" nodes, remove the oldest node in the buffer and
       write its contents to "ofs". Throws "FS::Failure" if there is an
       error writing such ``overflow'' nodes. It is a checked runtime error
       for the buffer to already contain a node indexed by "node.ci". */

    int flushedCnt;
    /* This field is incremented for each GLNode written to "ofs" by the
       "Put" method above. */

  private:
    int maxSize;
    GLNodeTbl *tbl;  // table of nodes: CI -> GLNodeKids
    CISeq *seq;      // sequence of CIs in order they were added
};

#endif // _GL_NODE_BUFFER_H
