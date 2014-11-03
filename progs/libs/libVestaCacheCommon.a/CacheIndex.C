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

// Created on Fri Nov  7 09:59:07 PST 1997 by heydon
// Last modified on Fri Apr 22 17:08:52 EDT 2005 by ken@xorian.net        
//      modified on Sun Jul 28 12:07:33 EDT 2002 by lken@remote.xorian.net
//      modified on Mon Apr 12 09:01:33 PDT 1999 by heydon

#include <Basics.H>
#include <FS.H>
#include <VestaLog.H>
#include <Recovery.H>
#include "ImmutableVal.H"
#include "CacheIndex.H"

using std::ostream;
using std::istream;
using std::ios;

inline void Indent(ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << ' ';
}

// CacheEntry::Indices --------------------------------------------------------

void CacheEntry::Indices::Log(VestaLog &log) const
  throw (VestaLog::Error)
{
    log.write((char *)(&(this->len)), sizeof_assert(this->len, 4));
    if (this->len > 0) {
	log.write((char *)(this->index),
          this->len * sizeof_assert(*(this->index), 4));
    }
}

void CacheEntry::Indices::Recover(RecoveryReader &rd)
  throw (VestaLog::Error, VestaLog::Eof)
{
    rd.readAll((char *)(&(this->len)), sizeof_assert(this->len, 4));
    if (this->len > 0) {
	this->index = NEW_PTRFREE_ARRAY(CacheEntry::Index, this->len);
	rd.readAll((char *)(this->index),
          this->len * sizeof_assert(*(this->index), 4));
    } else {
	this->index = (CacheEntry::Index *)NULL;
    }
}

void CacheEntry::Indices::Write(ostream &ofs) const
  throw (FS::Failure)
{
    FS::Write(ofs, (char *)(&(this->len)), sizeof_assert(this->len, 4));
    if (this->len > 0) {
	FS::Write(ofs, (char *)(this->index),
          this->len * sizeof_assert(*(this->index), 4));
    }
}

void CacheEntry::Indices::Read(istream &ifs)
  throw (FS::EndOfFile, FS::Failure)
{
    FS::Read(ifs, (char *)(&(this->len)), sizeof_assert(this->len, 4));
    if (this->len > 0) {
	this->index = NEW_PTRFREE_ARRAY(CacheEntry::Index, this->len);
	FS::Read(ifs, (char *)(this->index),
          this->len * sizeof_assert(*(this->index), 4));
    } else {
	this->index = (CacheEntry::Index *)NULL;
    }
}

ImmutableVal* CacheEntry::Indices::ReadImmutable(istream &ifs)
  throw (FS::EndOfFile, FS::Failure)
{
    int start = FS::Posn(ifs);
    Basics::int32 listLen;
    FS::Read(ifs, (char *)(&listLen), sizeof_assert(listLen, 4));
    int dataLen = listLen * sizeof_assert(CacheEntry::Index, 4);
    FS::Seek(ifs, dataLen, ios::cur);
    return NEW_CONSTR(ImmutableVal, (start, sizeof(listLen) + dataLen));
}

void CacheEntry::Indices::Send(SRPC &srpc) const throw (SRPC::failure)
{
    srpc.send_int(this->len);
    if (this->len > 0) {
      srpc.send_int32_array((const Basics::int32 *) this->index, this->len);
    }
}

void CacheEntry::Indices::Recv(SRPC &srpc) throw (SRPC::failure)
{
  this->len = srpc.recv_int();
  if (this->len > 0) {
    int len;
    this->index = (CacheEntry::Index *) srpc.recv_int32_array(len);
    assert(this->len == len);
  } else {
    this->index = (CacheEntry::Index *)NULL;
  }
}

void CacheEntry::Indices::Print(ostream &os, int indent) const throw ()
{
    const int NumCIsPerLine = 8;
    if (this->len < NumCIsPerLine) {
	// print in 1-line format
	os << *this;
    } else {
	// multi-line format
	os << '{';
	for (int i = 0; i < this->len; i++) {
	    if (i % NumCIsPerLine == 0) {
		os << '\n'; Indent(os, indent);
	    }
	    os << this->index[i];
	    if (i < this->len - 1) os << ", ";
	}
	os << " }";
    }
    os << '\n';
}

ostream& operator << (ostream &os, const CacheEntry::Indices& cis) throw ()
{
    os << "{ ";
    for (int i = 0; i < cis.len; i++) {
	os << cis.index[i];
	if (i < cis.len - 1) os << ", ";
    }
    os << " }";
    return os;
}
    
void CacheEntry::IndicesApp::Append(CacheEntry::Index ci) throw ()
{
    if (this->len == this->maxLen) {
	this->maxLen = max(2, 2 * this->maxLen);
	assert(this->maxLen > this->len);
	CacheEntry::Index *newIndex =
	  NEW_PTRFREE_ARRAY(CacheEntry::Index, this->maxLen);
	if (this->index != (CacheEntry::Index *)NULL) {
	    for (int i = 0; i < this->len; i++) {
		newIndex[i] = this->index[i];
	    }
	}
	this->index = newIndex;
    }
    this->index[this->len++] = ci;
}
