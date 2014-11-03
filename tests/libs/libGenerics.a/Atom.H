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


// Atom.H -- a package for unique text strings

/* An "Atom" is a unique text string. Two Atom's refs are equal iff their
   contents are equal. Hence, they can be compared for equality more
   efficiently than regular Text's.

   NOTE: Atoms should only be used when linking with the garbage collector!
   This is because Atoms are a subtype of Text, and so the Text destructor
   will always be called when an Atom is destroyed. This is disasterous when
   the garbage collector is not being used, since the Text destructor in that
   case deallocates the string. */

#ifndef _ATOM_H
#define _ATOM_H

#include <Basics.H>

class Atom: public Text {
  public:
    Atom() throw () : Text() { /*SKIP*/ }
    Atom(const Atom& atm) throw () { this->s = atm.s; }
    Atom(const Text& t) throw ()
	{ Init(t.cchars(), Text::GCImpl() ? (void *)1 : (void *)NULL); }
    Atom(const char c) throw ()	{ Init(&c, 1); }
    Atom(const char *str, const void *cpy = NULL) throw () { Init(str, cpy); }
    Atom(const char *bytes, int len) throw () { Init(bytes, len); }
    /* These initialize an Atom to the empty string, a copy of the atom "atm",
       a copy of the text "t", a string containing the single character "c", a
       copy of the null-terminated string "str", and the "len" chars pointed
       to by "bytes", respectively. In the latter case, it is an unchecked
       run-time error for any of the "len" bytes pointed to by "bytes" to be
       '\0'. In the case of initializing an Atom from a null-terminated string
       "str", if the bytes pointed to by "str" are immutable and long-lived,
       an extra copy can be avoided by passing a non-NULL value for "cpy". */

    // destructor
    ~Atom() throw () { /* SKIP -- Atoms are never de-allocated. */ }

    // assignment
    Atom& operator = (const char* str) throw (); 
    Atom& operator = (const Text& t) throw ();
    Atom& operator = (const Atom& a) throw ();

    // relational operators
    inline bool operator == (const Atom& a2) throw ()
	{ return (this->s == a2.s); }
    inline bool operator == (const Text& t2) throw ()
        { return (*((Text* ) this) == t2); }
    inline bool operator == (const char *c2) throw ()
        { return (*((Text* ) this) == c2); }
    inline bool operator != (const Atom& a2) throw ()
	{ return (this->s != a2.s); }
    inline bool operator != (const Text& t2) throw ()
        { return (*((Text* ) this) != t2); }
    inline bool operator != (const char *c2) throw ()
        { return (*((Text* ) this) != c2); }

    // destructive concatenation
    Atom& operator += (const char* str) throw ();
    Atom& operator += (const Text& t) throw ();
    Atom& operator += (const Atom& ) throw ();

  private:
    void Init(const char* str, const void* copy) throw ();
    void Init(const char* bytes, int len) throw ();
};

#endif // _ATOM_H
