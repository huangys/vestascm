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

//
// VLogHelp.C
// Last modified on Thu Jul 29 14:23:30 EDT 2004 by ken@xorian.net
//      modified on Wed Jul 18 19:30:49 PDT 2001 by mann
//
// Helpful functions for logging
//

#include <iomanip>
#include "VestaSource.H"
#include "Recovery.H"

using std::ostream;
using std::setw;
using std::setfill;
using std::hex;
using std::dec;

ostream&
operator<<(ostream& s, const LongId& longid)
{
    int i;
    int len = longid.length();
    for (i=0; i<len; i++) {
	s << setw(2) << setfill('0') << hex
	  << (int) (longid.value.byte[i] & 0xff);
    }
    s << setfill(' ') << dec;
    return s;
}


ostream&
PutQuotedString(ostream& s, const char* string)
{
    s << '\"';
    while (*string) {
	if (*string == '\"' || *string == '\\') {
	    s << '\\';
	}
	s << *string++;
    }
    s << '\"';
    return s;
}

ostream&
PutFPTag(ostream& s, const FP::Tag& fptag)
{
    const Bit8 *p = (Bit8*)((FP::Tag&)fptag).Words();
    int i;
    for (i=0; i<FP::ByteCnt; i++) {
	s << setw(2) << setfill('0') << hex << (int) (p[i] & 0xff);
    }
    s << setfill(' ') << dec;
    return s;
}

void
LogPutQuotedString(VestaLog& log, const char* string)
{
    log.put("\"");
    while (*string) {
      if (*string == '\"' || *string == '\\') {
	log.put('\\');
      }
      log.put(*string++);
    }
    log.put("\"");
}

void
GetFPTag(RecoveryReader* rr, char& c, FP::Tag& fptag)
{
    int i, j;
    char chr[3];
    chr[2] = '\000';
    Bit8* p = (Bit8*)fptag.Words();
    rr->skipWhite(c);
    for (i=0; i<FP::ByteCnt; i++) {
	for (j=0; j<2; j++) {
	    if (!isxdigit(c)) {
		throw VestaLog::Error(0, Text("RecoveryReader::getFPTag: ") +
				      "bad hex digit: " + c);
	    }
	    chr[j] = c;
	    rr->get(c);
	}
	p[i] = strtol(chr, NULL, 16);
    }
}

