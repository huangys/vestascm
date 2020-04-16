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


#include <climits>
#include <Basics.H>
#include <BufStream.H>
#include <Generics.H>
#include <SRPC.H>
#include "FV2.H"
#include <strings.h>

using std::ostream;
using Basics::OBufStream;

// delimeter between names of a FV2::T when marshalled/unmarshalled
/* For now, we are using a printing character to make debugging easier,
   but for safety, it would probably be better to use a non-printing character
   like '\001' that cannot appear in an Arc. */
const char Delim = '/';

/* FV2::T ------------------------------------------------------------------ */

// max expected FV::T length
const int MaxLen = 128;

void FV2::T::FromStr(const char *str, const char delim) throw ()
{
    const char *start = str;
    while (true) {
	const char *match = index(start, (int)delim);
	if (match != NULL) {
	    int subLen = (match - start); // length of the substring
	    this->addhi(Text(start, subLen));
	    start = match + 1;
	} else {
	    this->addhi(Text(start));
	    break;
	}
    }
}

char* FV2::T::ToStr(char *buff, const int sz, char delim, char type)
  const throw ()
/* We don't know ahead of time how large the string will be, so there are
   two cases.

   In the fast case, we attempt to write the Text's into a fixed-length buffer
   allocated on the stack. Before writing each string, we test that there is
   sufficient space for that string and the 'Delim' character following it.

   In the slow case, we allocate a buffer on the heap and write the string
   into it. This requires us to count the lengths of the remaining strings
   to see how large the buffer needs to be. */
{
    char *res = buff;

    // fast case: attempt to fill in "buff"
    int i;
    int totalLen;
    if (type != '\0') {
	assert(sz >= 2);
	res[0] = type;
	res[1] = delim;
	totalLen = 2;
    } else {
	totalLen = 0;
    }
    for (i = 0; i < this->size(); i++) {
	Text txt = this->get(i);
	int len = txt.Length();
	if (totalLen + len >= sz) break;
	memcpy(res+totalLen, txt.cchars(), len);
	totalLen += len;
	res[totalLen++] = delim;
    }

    // test if fast case succeeded
    if (i < this->size()) {
	// slow case

	// count total length of string
	int curr = totalLen; // current length
	for (int j = i; j < this->size(); j++) {
	    totalLen += this->get(j).Length() + 1; // include 1 for each Delim
	}

	// allocate the new buffer and copy strings so far
	char *newBuff = NEW_PTRFREE_ARRAY(char, totalLen);
	memcpy(newBuff, res, curr);
	res = newBuff;

	// copy the remaining strings
	for (/*SKIP*/; i < this->size(); i++) {
	    Text txt = this->get(i);
	    int len = txt.Length();
	    assert(curr + len < totalLen);
	    memcpy(res+curr, txt.cchars(), len);
	    curr += len;
	    res[curr++] = delim;
	}
	assert(curr == totalLen);
    }
    totalLen--;
    res[totalLen] = '\0';
    return res;
}

Text FV2::T::ToText(const char delim) const throw ()
{
    char fixedBuff[MaxLen];
    char *buff = ToStr(fixedBuff, MaxLen, delim);
    return Text(buff);
}

void FV2::T::Send(SRPC &srpc, char type) const throw (SRPC::failure)
{
    char stackBuff[MaxLen];
    char *buff = this->ToStr(stackBuff, MaxLen, Delim, type);

    // If any of the arcs contain the delimiter, we're in trouble.
    // Should be fixed in a future version by marshallaing in a way
    // that allows any character in an arc.
    bool l_bad_arc = false;
    unsigned int l_bad_arc_index = 0;
    for (unsigned int i = 0; !l_bad_arc && (i < this->size()); i++)
      {
	if(this->get(i).FindChar(Delim) != -1)
	  {
	    l_bad_arc = true;
	    l_bad_arc_index = i;
	  }
      }
    // If we found a bad arc, generate a detailed error message and
    // throw SRPC::failure with the invalid_parameter error code.
    if(l_bad_arc)
      {
	OBufStream l_msg;
	l_msg << "Free variable arc containing delimiter character ('" << Delim << "'):\n"
	      << "    arc = " << this->get(l_bad_arc_index) << "\n"
	      << "    path = " << buff;

	throw SRPC::failure(SRPC::invalid_parameter, Text(l_msg.str()));
      }

    // send the string
    srpc.send_chars(buff);
}

ostream& operator << (ostream &os, const FV2::T &t) throw ()
{
  t.Print(os);
  return os;
}

void FV2::T::Print(ostream &os) const throw()
{
    char fixedBuff[MaxLen];
    char *buff = this->ToStr(fixedBuff, MaxLen, '/');
    os << buff;
}

/* FV2::List --------------------------------------------------------------- */

void FV2::List::Send(SRPC &srpc, const char *types) const throw (SRPC::failure)
/* Note: The following must agree with the "FV::List::Recv" method
   in "FV.C". This method is only called to send the free variables
   for the "AddEntry" method. */
{
    srpc.send_seq_start(this->len);
    for (int i = 0; i < this->len; i++) {
	this->name[i]->Send(srpc, types[i]);
    }
    srpc.send_seq_end();
}

int FV2::ListApp::Append(FV2::T *s) throw ()
{
    int res;
    if (len == maxLen) {
	maxLen = max(2, 2 * maxLen);
	assert(maxLen > len);
	FV2::TPtr *newNames = NEW_ARRAY(FV2::TPtr, maxLen);
	if (name != (FV2::TPtr *)NULL) {
	    for (int i = 0; i < len; i++) newNames[i] = name[i];
	}
	name = newNames;
    }
    res = len;
    name[len++] = s;
    return res;
}

ostream& operator << (ostream &os, const FV2::List &names) throw ()
{
    os << "{ ";
    for (int i = 0; i < names.len; i++) {
	os << "\"" << names.name[i] << "\"";
	if (i < names.len - 1) os << ", ";
    }
    os << " }";
    return os;
}

inline void Indent(ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << " ";
}

void FV2::List::Print(ostream &os, int indent, const char *types) const
  throw ()
{
    if (this->len > 0) {
	for (int i = 0; i < this->len; i++) {
	    Indent(os, indent);
#if 0
	    // Verbose printing code to make each arc distinct, even
	    // if the arcs contain '/'.
	    os << "nm[" << i << "] = ";
	    os << types[i] << " <\"";
	    for (int j = 0; j < this->name[i]->size(); j++)
	      {
		if(j > 0)
		  {
		    os << "\", \"";
		  }
		os << this->name[i]->get(j);
	      }
	    os << "\">" << endl;
#else
	    os << "nm[" << i << "] = \"";
	    os << types[i] << '/' << *(this->name[i]) << "\"\n";
#endif
	}
    } else {
	Indent(os, indent);
	os << "<< no names >>\n";
    }
}
