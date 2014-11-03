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

/* This is the Text implementation that *is* linked with a garbage
   collector. Hence, none of its methods deallocates any memory. */

#include "TextCommon.C"

bool Text::GCImpl() throw () { return true; }

/* This is an implementation of the Text interface that relies for its
   correctness on being linked with a garbage collector. When one text is
   assigned from another the underlying string pointer is copied, rather than
   the bytes of the string itself. Conversly, when a (char *) is incorporated
   into a Text, it's bytes must be copied, since those bytes aren't guaranteed
   to reside on the heap (and hence, to be under the control of the
   collector). */

static const char *StrConcat(const char *str1, const char *str2,
  bool str1mutable, bool str2mutable) throw ()
/* Return a newly allocated null-terminated string containing the
   concatenation of the null-terminated strings "str1" and "str2". In the
   event that either "str1" or "str2" is empty, simply return the other
   string. In that case, the corresponding "str?mutable" argument is
   consulted to see if the string needs to be copied first. */
{
    // Note: null terminator is only included in second string
    int len1 = strlen(str1), len2 = strlen(str2);
    char *res;
    if (len1 == 0) {
	return (str2mutable ? StrCopy(str2, len2) : str2);
    } else if (len2 == 0) {
	return (str1mutable ? StrCopy(str1, len1) : str1);
    } else {
	char *res = NEW_PTRFREE_ARRAY(char, len1 + len2 + 1);
	memcpy(res, str1, len1);
	memcpy(res + len1, str2, len2 + 1);
	return res;
    }
}

// constructor from Text
Text::Text(const Text& t) throw ()
{
    this->s = t.s;
}

// keep a global cache of one-char texts
char *CharTexts[256] = { (char *)NULL, };

Text::Text(const char c) throw ()
{
  // If we say c < 256, the GNU compiler complains that the comparison
  // is always 1.
  unsigned char uc = c;
  assert((uc * sizeof(CharTexts[0])) < sizeof(CharTexts));

    if (CharTexts[c] == NULL) {
	char *temp = NEW_PTRFREE_ARRAY(char, 2);
	temp[0] = c; temp[1] = '\0';
	CharTexts[c] = temp;
    }
    this->s = CharTexts[c];
}

// destructor
Text::~Text() throw () { /* SKIP */ }

// assignment
Text& Text::operator = (const char* str) throw ()
{
    this->s = StrCopy(str);
    return *this;
}

Text& Text::operator = (const Text& t) throw ()
{
    this->s = t.s;
    return *this;
}

// comparison
bool operator == (const Text& t1, const Text& t2) throw ()
{
    bool res;
    if (t1.s == t2.s) return true;
    if (res = (strcmp(t1.s, t2.s) == 0)) {
	// This is a benevolent side-effect on the argument "t2",
        // so we cast away the fact that it is "const".
	((Text&) t2).s = t1.s;
    }
    return res;
}

bool operator != (const Text& t1, const Text& t2) throw ()
{
    bool res;
    if (t1.s == t2.s) return false;
    if (!(res = (strcmp(t1.s, t2.s) != 0))) {
	// This is a benevolent side-effect on the argument "t2",
        // so we cast away the fact that it is "const".
	((Text&) t2).s = t1.s;
    }
    return res;
}

// concatenation
Text operator + (const Text& t1, const Text& t2) throw ()
{
    return Text(StrConcat(t1.s, t2.s, false, false), /*copy=*/ (void*)1);
}

Text operator + (const char* str, const Text& t) throw ()
{
    return Text(StrConcat(str, t.s, true, false), /*copy=*/ (void*)1);
}

Text operator + (const Text& t, const char* str) throw ()
{
    return Text(StrConcat(t.s, str, false, true), /*copy=*/ (void*)1);
}

// append
Text& Text::operator += (const char* str) throw ()
{
    this->s = StrConcat(this->s, str, false, true);
    return *this;
}

Text& Text::operator += (const Text& t) throw ()
{
    this->s = StrConcat(this->s, t.s, false, false);
    return *this;
}
