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

#include <Basics.H>
#include "Generics.H"
#include "Atom.H"

// The global atom table and the mutex that protects it
static Basics::mutex *mu = 0;
static TextStringTbl *tbl = 0;

// Module initialization ------------------------------------------------------

extern "C" void AtomInit_inner()
{
  // The empty text to which the Atom() constructor initializes new
  // Atom's
  static const Text EmptyTxt;

  mu = NEW_PTRFREE(Basics::mutex);

  mu->lock();
  // allocate new table
  tbl = NEW_CONSTR(TextStringTbl, (/*sizeHint=*/ 100));

  // add empty string
  bool inTbl = tbl->Put(EmptyTxt, EmptyTxt.cchars()); assert(!inTbl);
  mu->unlock();
}

void AtomInit()
{
  static pthread_once_t init_once = PTHREAD_ONCE_INIT;
  pthread_once(&init_once, AtomInit_inner);
}

// StrCopy --------------------------------------------------------------------

static const char *StrCopy(const char *str) throw ()
// Returns a newly allocated copy of the null-terminated string "str".
{
    int len = strlen(str) + 1;  // include +1 for null terminator
    char* res = NEW_PTRFREE_ARRAY(char, len);
    memcpy(res, str, len);
    return res;
}

static const char *StrCopy(const char *s, int len) throw ()
/* Returns a newly-allocated, null-terminated copy of the "len" chars pointed
   to by "s"; it is an unchecked run-time error for any of those bytes to
   be the null character. */
{
    char* res = NEW_PTRFREE_ARRAY(char, len+1);
    memcpy(res, s, len);
    res[len] = '\0';
    return res;
}

// assignment -----------------------------------------------------------------

Atom& Atom::operator = (const char* str) throw ()
{
    this->Init(str, /*cpy=*/ (void *)NULL);
    return *this;
}

Atom& Atom::operator = (const Text& t) throw ()
{
    this->Init(t.cchars(), Text::GCImpl() ? (void *)1 : (void *)NULL);
    return *this;
}

Atom& Atom::operator = (const Atom& a) throw ()
{
    // always simply copy the string pointer from another atom
    this->s = a.s;
    return *this;
}

// destructive concatenation --------------------------------------------------
static const char *StrConcat(const char *str1, const char *str2,
  bool str2mutable) throw ()
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
	return str1;
    } else {
        char *res = NEW_PTRFREE_ARRAY(char, len1 + len2 + 1);
	memcpy(res, str1, len1);
	memcpy(res + len1, str2, len2 + 1);
	return res;
    }
}

Atom& Atom::operator += (const char* str) throw ()
{
    const char *catres = StrConcat(this->s, str, true);
    this->Init(catres, /*cpy=*/ (void *)NULL);
    return *this;
}

Atom& Atom::operator += (const Text& t) throw ()
{
    const char *catres = StrConcat(this->s, t.cchars(), true);
    this->Init(catres, /*cpy=*/ (void *)NULL);
    return *this;
}

Atom& Atom::operator += (const Atom& a) throw ()
{
    const char *catres = StrConcat(this->s, a.s, false);
    this->Init(catres, /*cpy=*/ (void *)NULL);
    return *this;
}

// Atom::Init -----------------------------------------------------------------

void Atom::Init(const char* str, const void* cpy) throw ()
{
    AtomInit();
    mu->lock();
    Text inTxt(str);
    if (!(tbl->Get(inTxt, /*OUT*/ this->s))) {
	this->s = (cpy == (void *)NULL) ? StrCopy(str) : str;
	bool inTbl = tbl->Put(inTxt, this->s); assert(!inTbl);
    }
    mu->unlock();
}

void Atom::Init(const char* bytes, int len) throw ()
{
    const char *str = StrCopy(bytes, len);
    Text inTxt(str);
    AtomInit();
    mu->lock();
    if (!(tbl->Get(inTxt, /*OUT*/ this->s))) {
	this->s = str;
	bool inTbl = tbl->Put(inTxt, this->s); assert(!inTbl);
    }
    mu->unlock();
}
