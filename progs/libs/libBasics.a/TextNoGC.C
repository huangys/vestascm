// Copyright (C) 2001, Compaq Computer Corporation

// This file is part of Vesta.

// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

/* This is the Text implementation that is *not* linked with a garbage
   collector. Hence, its destructor actually deallocates memory. */

#include "TextCommon.C"

bool Text::GCImpl() throw () { return false; }

static const char *StrConcat(const char *str1, const char *str2) throw ()
/* Return a newly allocated null-terminated string containing the
   concatenation of the null-terminated strings "str1" and "str2". */
{
    // Note: null terminator is only included in second string
    int len1 = strlen(str1), len2 = strlen(str2) + 1;
    char *res = NEW_ARRAY(char, len1 + len2);
    if (len1 == 0) {
	memcpy(res, str2, len2);
    } else {
	memcpy(res, str1, len1);
	memcpy(res + len1, str2, len2);
    }
    return res;
}

// constructor from Text
Text::Text(const Text& t) throw ()
{
    this->s = StrCopy(t.s);
}

Text::Text(const char c) throw ()
{
    char *temp = NEW_ARRAY(char, 2);
    temp[0] = c; temp[1] = '\0';
    this->s = temp;
}

// destructor
Text::~Text() throw ()
{
    if (this->s != EmptyStr) {
	delete[] (char *)(this->s);
	this->s = (char *)NULL;
    }
}

// assignment
Text& Text::operator = (const char* str) throw ()
{
    if (this->s != EmptyStr) delete[] (char *)(this->s);
    this->s = StrCopy(str);
    return *this;
}

Text& Text::operator = (const Text& t) throw ()
{
    if (this->s != EmptyStr) delete[] (char *)(this->s);
    this->s = StrCopy(t.s);
    return *this;
}

// comparison
bool operator == (const Text& t1, const Text& t2) throw ()
{
    return ((t1.s == t2.s) || (strcmp(t1.s, t2.s) == 0));
}

bool operator != (const Text& t1, const Text& t2) throw ()
{
    return ((t1.s != t2.s) && (strcmp(t1.s, t2.s) != 0));
}

// concatenation
Text operator + (const Text& t1, const Text& t2) throw ()
{
    return Text(StrConcat(t1.s, t2.s), /*copy=*/ (void*)1);
}

Text operator + (const char* str, const Text& t) throw ()
{
    return Text(StrConcat(str, t.s), /*copy=*/ (void*)1);
}

Text operator + (const Text& t, const char* str) throw ()
{
    return Text(StrConcat(t.s, str), /*copy=*/ (void*)1);
}

// append
Text& Text::operator += (const char* str) throw ()
{
    const char *oldS = this->s;
    this->s = StrConcat(oldS, str);
    if (oldS != EmptyStr) delete[] (char *)oldS;
    return *this;
}

Text& Text::operator += (const Text& t) throw ()
{
    const char *oldS = this->s;
    this->s = StrConcat(oldS, t.s);
    if (oldS != EmptyStr) delete[] (char *)oldS;
    return *this;
}
