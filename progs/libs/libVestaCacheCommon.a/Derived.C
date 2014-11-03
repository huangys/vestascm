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

// Created on Fri Nov  7 13:27:50 PST 1997 by heydon
// Last modified on Fri Apr 22 17:21:41 EDT 2005 by ken@xorian.net        
//      modified on Sun Jul 28 11:57:12 EDT 2002 by lken@remote.xorian.net
//      modified on Mon Nov 10 12:52:54 PST 1997 by heydon

#include <Basics.H>
#include <FS.H>
#include <SRPC.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <SourceOrDerived.H>
#include "Derived.H"

using std::ostream;
using std::istream;
using std::ios;
using std::hex;

inline void Indent(ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << " ";
}

// Derived::Indices -----------------------------------------------------------

void Derived::Indices::Log(VestaLog &log) const
  throw (VestaLog::Error)
{
    log.write((char *)(&(this->len)), sizeof_assert(this->len, 4));
    if (this->len > 0) {
	log.write((char *)(this->index),
	  this->len * sizeof_assert(*(this->index), 4));
    }
}

void Derived::Indices::Recover(RecoveryReader &rd)
  throw (VestaLog::Error, VestaLog::Eof)
{
    rd.readAll((char *)(&(this->len)), sizeof_assert(this->len, 4));
    if (this->len > 0) {
	this->index = NEW_PTRFREE_ARRAY(Derived::Index, this->len);
	rd.readAll((char *)(this->index),
          this->len * sizeof_assert(*(this->index), 4));
    } else {
	this->index = (Derived::Index *)NULL;
    }
}

void Derived::Indices::Write(ostream &ofs) const
  throw (FS::Failure)
{
    FS::Write(ofs, (char *)(&(this->len)), sizeof_assert(this->len, 4));
    if (this->len > 0) {
	FS::Write(ofs, (char *)(this->index),
          this->len * sizeof_assert(*(this->index), 4));
    }
}

void Derived::Indices::Read(istream &ifs)
  throw (FS::EndOfFile, FS::Failure)
{
    FS::Read(ifs, (char *)(&(this->len)), sizeof_assert(this->len, 4));
    if (this->len > 0) {
	this->index = NEW_PTRFREE_ARRAY(Derived::Index, this->len);
	FS::Read(ifs, (char *)(this->index),
          this->len * sizeof_assert(*(this->index), 4));
    } else {
	this->index = (Derived::Index *)NULL;
    }
}

int Derived::Indices::Skip(istream &ifs)
  throw (FS::EndOfFile, FS::Failure)
{
    int len;
    FS::Read(ifs, (char *)(&len), sizeof(len));
    int dataLen = len * sizeof(Derived::Index);
    FS::Seek(ifs, dataLen, ios::cur);
    return (sizeof(len) + dataLen);
}

void Derived::Indices::Send(SRPC &srpc) const throw (SRPC::failure)
{
    srpc.send_int(this->len);
    if (this->len > 0) {
      srpc.send_int32_array((const Basics::int32 *) this->index, this->len);
    }
}

void Derived::Indices::Recv(SRPC &srpc) throw (SRPC::failure)
{
  this->len = srpc.recv_int();
  if (this->len > 0) {
    int len;
    this->index = (Derived::Index *) srpc.recv_int32_array(len);
    assert(this->len == len);
  } else {
    this->index = (Derived::Index *)NULL;
  }
}

void Derived::Indices::Print(ostream &os, int indent) const throw ()
{
  const int NumDIsPerLine = 5;
  if (this->len < NumDIsPerLine) {
    // print in 1-line format
    os << *this;
  } else {
    // multi-line format
    ios::fmtflags baseVal = os.flags();
    os << '{' << hex;
    for (int i = 0; i < this->len; i++) {
      if (i % NumDIsPerLine == 0) {
	os << '\n'; Indent(os, indent);
      }
      os << "0x" << this->index[i];
      if (i < this->len - 1) os << ", ";
    }
    os << " }";
    (void) os.setf(baseVal, ios::basefield);
  }
  os << '\n';
}

ostream& operator << (ostream &os, const Derived::Indices& dis) throw ()
{
  ios::fmtflags baseVal = os.flags();
  os << "{ " << hex;
  for (int i = 0; i < dis.len; i++) {
    os << "0x" << dis.index[i];
    if (i < dis.len - 1) os << ", ";
  }
  os << " }";
  (void) os.setf(baseVal, ios::basefield);
  return os;
}

// Derived::IndicesApp --------------------------------------------------------

void Derived::IndicesApp::Append(Derived::Index di) throw ()
{
    if (this->len == this->maxLen) {
	this->maxLen = max(2, 2 * this->maxLen);
	assert(this->maxLen > this->len);
	Derived::Index *newIndex =
	  NEW_PTRFREE_ARRAY(Derived::Index, this->maxLen);
	if (this->index != (Derived::Index *)NULL) {
	    for (int i = 0; i < this->len; i++) {
		newIndex[i] = this->index[i];
	    }
	}
	this->index = newIndex;
    }
    this->index[this->len++] = di;
}
