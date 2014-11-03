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

// Last modified on Fri Apr 22 18:18:58 EDT 2005 by ken@xorian.net         
//      modified on Thu Aug  8 13:14:06 EDT 2002 by kcschalk@shr.intel.com 
//      modified on Sat Feb 12 12:02:13 PST 2000 by mann  
//      modified on Mon Nov 10 12:41:26 PST 1997 by heydon

#include <Basics.H>
#include <SRPC.H>
#include <FP.H>

#include "PKPrefix.H"

using std::ostream;
using std::istream;

// Granularity ----------------------------------------------------------------

// Granularity (number of insignificant bits of the PK)
//
// The larger the granularity, the more PK's will be in each MultiPKFile.
// There may be several different versions of granularity outstanding, but
// there are never two different PKFiles stored in MultiPKFiles of different
// granularity. The different granularity versions are stored in the
// "Granularity" array from the newest to the oldest version. This interface
// currently does not export access to either of these two values; it only
// provides a method to access the newest granularity.
//
static const int NumGranularities = 2;
static const int Granularities[NumGranularities] = { 16, 24 };

int PKPrefix::Granularity() throw ()
{
    return Granularities[0];
}

static const int WdBits = 8 * sizeof(Word);

// PKPrefix::T ----------------------------------------------------------------

void PKPrefix::T::Init(int gran) throw ()
{
    assert(gran <= WdBits);

    if (gran > 0) {
	// keep the high-order "gran" bits of word "i"
	register Word mask = ~((Word) 0);
	mask <<= WdBits - gran;
	this->w &= mask;
    }
}

Text PKPrefix::T::Pathname(int sigBits, int arcBits) const throw ()
{
    assert(sigBits <= WdBits);
    Text res;
    Word mask1, mask2;
    const int fieldWidth = (arcBits + 3) / 4; // == ceiling(arcBits/4.0)
    char buff[65];                            // for formatting into
    int i = 0;
    while (i < sigBits) {
	int n = min(arcBits, sigBits - i);
	int loBit = i % WdBits;
	int hiBit = loBit + n;

        // set "mask" to bits "[loBit, hiBit)"
	Word mask = this->w;
	assert(hiBit <= WdBits);
	mask <<= loBit;        	              // shift off hi bits
	mask >>= loBit + (WdBits - hiBit);    // shift down to lsb 
	Text arc((i > 0) ? "/" : "");
	int bytesWritten = sprintf(buff, "%0*" FORMAT_LENGTH_INT_64 "x",
				   fieldWidth, mask);
	assert(bytesWritten <= fieldWidth);
	arc += buff;
	res += arc;
	i += n;
    }
    return res;
}

ostream& operator << (ostream &os, const PKPrefix::T &pfx) throw ()
{
    char buff[17];
    sprintf(buff, "%016" FORMAT_LENGTH_INT_64 "x", pfx.w);
    os << buff;
    return os;
}

// PKPrefix::List -------------------------------------------------------------

void PKPrefix::List::Write(ostream &ofs) const throw (FS::Failure)
{
    FS::Write(ofs, (char *)(&(this->len)), sizeof(this->len));
    for (int i = 0; i < len; i++) {
	pfx[i].Write(ofs);
    }
}

void PKPrefix::List::Read(istream &ifs) throw (FS::EndOfFile, FS::Failure)
{
    FS::Read(ifs, (char *)(&(this->len)), sizeof(this->len));
    if (len > 0) {
	pfx = NEW_PTRFREE_ARRAY(PKPrefix::T, len);
	for (int i = 0; i < len; i++) {
	    pfx[i].Read(ifs);
	}
    } else {
	pfx = (PKPrefix::T *)NULL;
    }
}

void PKPrefix::List::Send(SRPC &srpc) const throw (SRPC::failure)
{
    srpc.send_seq_start(this->len);
    for (int i = 0; i < this->len; i++) {
	this->pfx[i].Send(srpc);
    }
    srpc.send_seq_end();
}

void PKPrefix::List::Recv(SRPC &srpc) throw (SRPC::failure)
{
    srpc.recv_seq_start(&(this->len), NULL);
    pfx = NEW_PTRFREE_ARRAY(PKPrefix::T, this->len);
    for (int i = 0; i < this->len; i++) {
	this->pfx[i].Recv(srpc);
    }
    srpc.recv_seq_end();
}

inline void Indent(ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << " ";
}

void PKPrefix::List::Print(ostream &os, int indent) const throw ()
{
    if (this->len == 0) {
	os << " <<None>>\n";
    } else {
	os << '\n';
	for (int i = 0; i < this->len; i++) {
	    Indent(os, indent);
	    os << "pfx[" << i << "] = " << this->pfx[i] << "\n";
	}
    }
}
