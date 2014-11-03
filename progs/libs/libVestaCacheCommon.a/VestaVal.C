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

// Created on Fri Nov  7 14:31:21 PST 1997 by heydon

#include <Basics.H>
#include <FS.H>
#include <SRPC.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <FP.H>
#include "ImmutableVal.H"
#include "Derived.H"
#include "VestaVal.H"

using std::ostream;
using std::istream;
using std::ios;
using std::endl;

inline void Indent(ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << " ";
}

VestaVal::T::T(char *bytebuff, int len) throw ()
: len(len)
{
    if (len < 0) this->len = strlen(bytebuff);
    this->bytes = NEW_PTRFREE_ARRAY(char, this->len);
    memcpy(this->bytes, bytebuff, this->len);
    this->fp = FP::Tag(this->bytes, this->len);
}

void VestaVal::T::Log(VestaLog &log) const
  throw (VestaLog::Error)
{
    this->fp.Log(log);
    this->dis.Log(log);
    this->prefixTbl.Log(log);
    log.write((char *)(&(this->len)), sizeof(this->len));
    if (this->len > 0) {
	log.write(this->bytes, this->len);
    }
}

void VestaVal::T::Recover(RecoveryReader &rd,
			  bool old_format)
  throw (VestaLog::Error, VestaLog::Eof)
{
    this->fp.Recover(rd);
    this->dis.Recover(rd);
    this->prefixTbl.Recover(rd, old_format);
    rd.readAll((char *)(&(this->len)), sizeof(this->len));
    if (this->len > 0) {
	this->bytes = NEW_PTRFREE_ARRAY(char, this->len);
	rd.readAll(this->bytes, this->len);
    } else {
	this->bytes = (char *)NULL;
    }
}

void VestaVal::T::Write(ostream &ofs) const
  throw (FS::Failure)
{
    this->fp.Write(ofs);
    this->dis.Write(ofs);
    this->prefixTbl.Write(ofs);
    FS::Write(ofs, (char *)(&(this->len)), sizeof(this->len));
    if (this->len > 0) {
	FS::Write(ofs, this->bytes, this->len);
    }
}

void VestaVal::T::Read(istream &ifs,
		       bool old_format)
  throw (FS::EndOfFile, FS::Failure)
{
    this->fp.Read(ifs);
    this->dis.Read(ifs);
    this->prefixTbl.Read(ifs, old_format);
    FS::Read(ifs, (char *)(&(this->len)), sizeof(this->len));
    if (this->len > 0) {
	this->bytes = NEW_PTRFREE_ARRAY(char, this->len);
	FS::Read(ifs, this->bytes, this->len);
    } else {
	this->bytes = (char *)NULL;
    }
}

ImmutableVal* VestaVal::T::ReadImmutable(istream &ifs,
					 bool old_format)
  throw (FS::EndOfFile, FS::Failure)
{
    int start = FS::Posn(ifs);
    int dataLen = 0;           // total length of value on disk

    // skip initial "fp" field
    FS::Seek(ifs, FP::ByteCnt, ios::cur);
    dataLen += FP::ByteCnt;

    // skip "dis"
    dataLen += Derived::Indices::Skip(ifs);

    // skip "prefixTbl"
    dataLen += PrefixTbl::Skip(ifs, old_format);

    // skip pickled value
    int pickleLen;
    FS::Read(ifs, (char *)(&pickleLen), sizeof(pickleLen));
    dataLen += sizeof(pickleLen);
    FS::Seek(ifs, pickleLen, ios::cur);
    dataLen += pickleLen;
    return NEW_CONSTR(ImmutableVal, (start, dataLen));
}

void VestaVal::T::Send(SRPC &srpc, bool old_protocol) const throw (SRPC::failure)
{
    this->fp.Send(srpc);
    this->dis.Send(srpc);
    this->prefixTbl.Send(srpc, old_protocol);
    srpc.send_bytes(this->bytes, this->len);
}

void VestaVal::T::Recv(SRPC &srpc, bool old_protocol) throw (SRPC::failure)
{
    this->fp.Recv(srpc);
    this->dis.Recv(srpc);
    this->prefixTbl.Recv(srpc, old_protocol);
    this->bytes = srpc.recv_bytes(/*OUT*/ this->len);
}

void VestaVal::T::Print(ostream &os, int indent) const throw ()
{
    Indent(os, indent); os << "fp    = " << this->fp << "\n";
    Indent(os, indent); os << "DIs   = "; this->dis.Print(os, indent+2);
    Indent(os, indent); os << "tbl   =";
    if (this->prefixTbl.NumArcs() > 0) {
	os << endl; this->prefixTbl.Print(os, indent+2);
    } else {
	os << " <empty>" << endl;
    }
    Indent(os, indent); os << "bytes = ";
    int printLen = min(this->len, (int)(((76 - (indent + 25)) * 4.0) / 9.0));
    for (int i = 0; i < printLen; i++) {
	char buff[3];
	int printLen = sprintf(buff, "%02x", (unsigned char)(this->bytes[i]));
	assert(printLen == 2);
	os << buff;
	if ((i+1) % 4 == 0) os << " ";
    }
    if (printLen < this->len) os << "...";
    os << " (" << this->len << " total)\n";
}
