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

// Created on Tue Mar 11 13:38:31 PST 1997 by heydon

// Last modified on Mon Aug  9 17:04:47 EDT 2004 by ken@xorian.net
//      modified on Tue Dec 16 00:39:16 PST 1997 by heydon

// GLNode.H -- defines a class like a "GraphLog::Node", but with fewer fields

#ifndef _GL_NODE_H
#define _GL_NODE_H

#include <Basics.H>
#include <FS.H>
#include <CacheIndex.H>
#include <Derived.H>
#include <GraphLog.H>

class GLNode {
  public:
    GLNode() throw () { /* SKIP */ }
    /* Default constructor */

    GLNode(const GraphLog::Node &node) throw ()
      : ci(node.ci), kids(node.kids), refs(node.refs) { /* SKIP */ }
    /* Initialize this node to contain the relevant values of "node". */

    GLNode(std::istream &ifs) throw (FS::Failure, FS::EndOfFile) { Read(ifs); }
    /* Initialize this node from the input stream "ifs". */

    GLNode(CacheEntry::Index ci, CacheEntry::Indices *kids,
      Derived::Indices *refs) throw ()
      : ci(ci), kids(kids), refs(refs) { /*SKIP*/ }

    // write/read
    void Write(std::ostream &ofs) const throw (FS::Failure);
    void Read(std::istream &ifs) throw (FS::Failure, FS::EndOfFile);

    // fields
    CacheEntry::Index ci;      // this node's index
    CacheEntry::Indices *kids; // child entries
    Derived::Indices *refs;    // deriveds reachable from this entry
};

#endif // _GL_NODE_H
