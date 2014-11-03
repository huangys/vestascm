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

// Created on Fri Nov  7 10:16:28 PST 1997 by heydon

#include <Basics.H>
#include <FS.H>
#include <VestaLog.H>
#include <Recovery.H>
#include "TextIO.H"
#include "BitVector.H"
#include "FV.H"
#include "CompactFV.H"

using std::ostream;
using std::istream;

inline void Indent(ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << " ";
}

// FV::List -------------------------------------------------------------------

void FV::List::Copy(const FV::List &l) throw ()
{
    this->len = l.len;
    this->name = NEW_ARRAY(FV::T, l.len);
    for (unsigned int i = 0; i < l.len; i++) {
	this->name[i] = l.name[i];
    }
}

int FV::List::Size() const throw ()
{
    register int res = this->CheapSize();
    for (unsigned int i = 0; i < this->len; i++) {
	res += name[i].Length() + 1;
    }
    return res;
}

void FV::List::Send(SRPC &srpc, bool old_protocol)
  const throw (SRPC::failure,
	       PrefixTbl::Overflow)
/* Note: The following must agree with "CompactFV::List::Recv"
   in "CompactFV.C". This method is only called to send back
   the PKFile's "allNames" for the "FreeVariables" method. */
{
    CompactFV::List names(*this);
    names.Send(srpc, old_protocol);
}

void FV::List::Recv(SRPC &srpc) throw (SRPC::failure)
/* Note: The following method must agree with "FV2::List::Send"
   in "FV2.C". */
{
    // The following code unmarshalls a list of FV::T's as
    // a sequence of Texts
    int new_len;
    srpc.recv_seq_start(/*OUT*/ &new_len);
    assert(new_len >= 0);
    this->len = new_len;
    this->name = NEW_ARRAY(FV::T, this->len);
    for (unsigned int i = 0; i < this->len; i++) {
	this->name[i].Recv(srpc);
    }
    srpc.recv_seq_end();
}

ostream& operator << (ostream &os, const FV::List &names) throw ()
{
    os << "{ ";
    for (unsigned int i = 0; i < names.len; i++) {
	os << "\"" << names.name[i] << "\"";
	if (i < names.len - 1) os << ", ";
    }
    os << " }";
    return os;
}

void FV::List::Print(ostream &os, int indent, const BitVector *bv)
  const throw ()
{
    if (this->len > 0) {
	indent--;
	for (unsigned int i = 0; i < this->len; i++) {
	    if (indent >= 0) {
		Indent(os, indent);
		os << ((bv != NULL && bv->Read(i)) ? '*' : ' ');
	    }
	    os << "nm[" << i << "] = \"" << this->name[i] << "\"\n";
	}
    } else {
	Indent(os, indent);
	os << "<< no names >>\n";
    }
}

void FV::List::Log(VestaLog &log) const
  throw (VestaLog::Error)
{
    log.write((char *)(&(this->len)), sizeof(this->len));
    for (int i = 0; i < this->len; i++) {
	TextIO::Log(log, this->name[i]);
    }
}

void FV::List::Recover(RecoveryReader &rd)
  throw (VestaLog::Error, VestaLog::Eof)
{
    rd.readAll((char *)(&(this->len)), sizeof(this->len));
    if (this->len > 0) {
	this->name = NEW_ARRAY(FV::T, this->len);
	for (int i = 0; i < this->len; i++) {
	    TextIO::Recover(rd, /*OUT*/ this->name[i]);
	}
    } else {
	this->name = (FV::T *)NULL;
    }
}

void FV::List::Write(ostream &ofs) const
  throw (FS::Failure)
{
    FS::Write(ofs, (char *)(&(this->len)), sizeof(this->len));
    for (int i = 0; i < this->len; i++) {
	TextIO::Write(ofs, this->name[i]);
    }
}

void FV::List::Read(istream &ifs)
  throw (FS::EndOfFile, FS::Failure)
{
    FS::Read(ifs, (char *)(&(this->len)), sizeof(this->len));
    if (this->len > 0) {
	this->name = NEW_ARRAY(FV::T, this->len);
	for (int i = 0; i < this->len; i++) {
	    TextIO::Read(ifs, /*OUT*/ this->name[i]);
	}
    } else {
	this->name = (FV::T *)NULL;
    }
}

bool FV::List::operator==(const FV::List &other) const throw()
{
  if(this->len != other.len)
    return false;
  assert((this->len == 0) || (this->name != 0));
  assert((other.len == 0) || (other.name != 0));
  for(Basics::uint32 i = 0; i < this->len; i++)
    {
      if(this->name[i] != other.name[i])
	return false;
    }
  return true;
}

// FV::ListApp ----------------------------------------------------------------

void FV::ListApp::Grow(int sizeHint) throw ()
{
    if (this->maxLen < sizeHint) {
	FV::T *new_names = NEW_ARRAY(FV::T, sizeHint);
	if (this->name != (FV::T *)NULL) {
	    for (unsigned int i = 0; i < this->len; i++) {
		new_names[i] = this->name[i];
	    }
	}
	this->name = new_names;
	this->maxLen = sizeHint;
    }
}

int FV::ListApp::Append(const FV::T& s) throw ()
{
    int res;
    if (this->len == this->maxLen) {
	this->maxLen = max(2, 2 * this->maxLen);
	assert(this->maxLen > this->len);
	FV::T *newNames = NEW_ARRAY(FV::T, this->maxLen);
	if (this->name != (FV::T *)NULL) {
	    for (unsigned int i = 0; i < this->len; i++) {
		newNames[i] = this->name[i];
	    }
	}
	this->name = newNames;
    }
    res = this->len;
    this->name[this->len++] = s;
    return res;
}

void FV::ListApp::Pack(const BitVector &packMask) throw ()
{
    BVIter it(&packMask);
    unsigned int i, ix;
    for (i = 0; it.Next(/*OUT*/ ix); i++) {
	assert(i <= ix);
	if (i < ix) this->name[i] = this->name[ix];
    }
    unsigned int oldLen = this->len;
    this->len = i;
    FV::T empty;
    for (/*SKIP*/; i < oldLen; i++) {
	this->name[i] = empty;
    }
}
