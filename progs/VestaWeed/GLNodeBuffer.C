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

// Created on Tue Mar 11 14:32:26 PST 1997 by heydon

// Last modified on Sat Apr 30 16:37:07 EDT 2005 by ken@xorian.net
//      modified on Tue Dec 16 00:41:13 PST 1997 by heydon

#include <Basics.H>
#include <IntKey.H>
#include <FS.H>
#include <Table.H>
#include <Sequence.H>
#include <CacheIndex.H>
#include "GLNode.H"
#include "GLNodeBuffer.H"

using std::ostream;

GLNodeBuffer::GLNodeBuffer(int maxSize) throw ()
  : maxSize(maxSize)
{
  this->tbl = NEW_CONSTR(GLNodeTbl, (/*sizeHint=*/ maxSize));
  this->seq = NEW_CONSTR(CISeq, (/*sizeHint=*/ maxSize));
}

bool GLNodeBuffer::Delete(CacheEntry::Index ci, /*OUT*/ GLNode &node) throw ()
{
    GLNodeKids nodeKids;
    bool res;
    if ((res = this->tbl->Delete(IntKey(ci), /*OUT*/ nodeKids))) {
	node.ci = ci; node.kids = nodeKids.kids; node.refs = nodeKids.refs;
    }
    return res;
}

void GLNodeBuffer::Put(const GLNode &node, ostream &ofs) throw (FS::Failure)
{
    // evict a node if necessary
    if (this->tbl->Size() >= this->maxSize) {
	// find the oldest node in the table & remove it
	CacheEntry::Index ci;
	GLNodeKids nodeKids;
	/* We have to loop here until we find a "ci" that is actually in
           "tbl", since many entries in the "this->seq" queue may have
           been deleted from "tbl" by calls to the "Delete" method above. */
	do {
	    ci = this->seq->remlo();
	} while (!(this->tbl->Delete(IntKey(ci), /*OUT*/ nodeKids)));

	// write the node to "ofs"
	GLNode del(ci, nodeKids.kids, nodeKids.refs);
	del.Write(ofs);
	this->flushedCnt++;
    }

    // add the new node to the table
    GLNodeKids nodeKids(node.kids, node.refs);
    bool inTbl = this->tbl->Put(IntKey(node.ci), nodeKids);
    assert(!inTbl);

    // add this node to the sequence
    this->seq->addhi(node.ci);
}
