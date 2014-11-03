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

// Created on Tue Mar 11 13:44:17 PST 1997 by heydon

// Last modified on Sat Apr 30 16:35:25 EDT 2005 by ken@xorian.net
//      modified on Tue Dec 16 00:38:53 PST 1997 by heydon

#include <Basics.H>
#include <FS.H>
#include <CacheIndex.H>
#include <Derived.H>
#include <GraphLog.H>
#include "GLNode.H"

using std::istream;
using std::ostream;

void GLNode::Write(ostream &ofs) const throw (FS::Failure)
{
    FS::Write(ofs, (char *)(&(this->ci)), sizeof(CacheEntry::Index));
    this->kids->Write(ofs);
    this->refs->Write(ofs);
}

void GLNode::Read(istream &ifs) throw (FS::Failure, FS::EndOfFile)
{
    FS::Read(ifs, (char *)(&(this->ci)), sizeof(CacheEntry::Index));
    this->kids = NEW_CONSTR(CacheEntry::Indices, (ifs));
    this->refs = NEW_CONSTR(Derived::Indices, (ifs));
}

