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

// Text.H -- a package for text strings
//
// A "Text" is an immutable sequence of characters.

#ifndef _TEXT_H
#define _TEXT_H

#include "Basics.H"

class Text {
  public:
    /* In this interface, arguments of type "char *" named "str" are
       assumed to be null-terminated strings. */

    // constructors
    Text() throw ();
    Text(const Text& t) throw ();
    Text(const char c) throw ();
    Text(const char *str, void *copy = NULL) throw ();
    Text(const char *bytes, int len) throw ();
    /* These initialize the text to the empty string, a copy of the text "t",
       a string containing the single character "c", a copy of the null-
       terminated string "str", and the "len" chars pointed to by "bytes",
       respectively. In the latter case, it is an unchecked run-time error
       for any of the "len" bytes pointed to by "bytes" to be '\0'. In the
       case of initializing a Text from a null-terminated string "str", if
       the bytes pointed to by "str" are immutable and long-lived, an extra
       copy can be avoided by passing a non-NULL value for "copy". */

    // init from string
    void Init(const char *str) { this->s = str; }
    /* Initialize this text to "str" without any allocations. The
       client *must not* modify or de-allocate "str" after this call.
       This method is provided for cases where it is inconvenient to
       use the Text(const char*, void*) constructor above, but the
       client has just allocated "str" and can avoid a copy. */

    // destructor
    ~Text() throw ();

    // assignment
    Text& operator = (const char* str) throw (); 
    Text& operator = (const Text& t) throw ();

    // relational operators
    friend bool operator == (const Text& t1, const Text& t2) throw ();
    friend bool operator != (const Text& t1, const Text& t2) throw ();
    friend bool operator < (const Text& t1, const Text& t2) throw ()
	{ return strcmp(t1.s, t2.s) < 0; }
    friend bool operator <= (const Text& t1, const Text& t2) throw ()
	{ return strcmp(t1.s, t2.s) <= 0; }
    friend bool operator > (const Text& t1, const Text& t2) throw ()
	{ return strcmp(t1.s, t2.s) > 0; }
    friend bool operator >= (const Text& t1, const Text& t2) throw ()
	{ return strcmp(t1.s, t2.s) >= 0; }

    // concatenation
    friend Text operator + (const Text& t1, const Text& t2) throw ();
    friend Text operator + (const char* str, const Text& t) throw ();
    friend Text operator + (const Text& t, const char* str) throw ();

    // destructive concatenation
    Text& operator += (const char* str) throw ();
    Text& operator += (const Text& t) throw ();

    // conversion to (const char *) and (char *)
    // Note that these operations are unsafe, since they return
    // a pointer into the argument Text, which may be destroyed
    // before the pointer is discarded.
    const char *cchars() const throw () { return s; }
    char *chars() const throw () { return (char *)s; }
      // This operation is also unsafe because it permits subsequent
      // modification of the Text, which is supposed to be immutable.

    // output
    friend std::ostream& operator << (std::ostream& os, const Text& t) throw ()
    /* Write the text "t" to the stream "os". */
	{ return os << t.s; }

    // "MaxInt" == the largest possible integer
    enum { MaxInt = (int)(~(1u << ((sizeof(int) * 8) - 1))) };

    // Other operations

    char operator [] (int i) const throw () { return s[i]; }
    /* Return character "i" of the text. It is an unchecked run-time
       error if "i < 0" or "i >= Length()". */

    int Length() const throw () { return (int)strlen(this->s); }
    /* Return the number of characters in the text. */

    bool Empty() const throw () { return *(this->s) == '\0'; }
    /* Return "true" iff the text is empty. */

    Text Sub(int start, int len = MaxInt) const throw ();
    /* Return a sub-sequence of the text: empty if "len = 0" or if
       "start >= this->Length()"; otherwise, the subsequence ranging from
       "start" to the minimum of "start+len-1" and "this->Length()-1". It
       is an unchecked run-time error for "start < 0" or "len < 0". */

    int FindChar(char c, int start = 0) const throw ();
    /* Return the smallest index "i >= start" such that "c" is the "i"th
       character of this text (counting from 0) if one exists; otherwise,
       return -1. */

    int FindCharR(char c, int start = MaxInt) const throw ();
    /* Return the largest index "i <= start" such that "c" is the "i"th
       character of this text (counting from 0) if one exists; otherwise,
       return -1. */

    int FindText(const Text &substr, int start = 0) const throw ();
    /* Return the smallest index "i >= start" such that the text
       "substr" occurs in this text starting at position "i" (counting
       from 0); otherwise, return -1. */

    Word Hash() const throw ();
    /* Return a hash function of the text's contents. */

    static bool GCImpl() throw ();
    /* Return true iff the client has linked with the Text implementation that
       depends on being linked with a garbage-collector. This guarantees that
       the underlying storage for the text is not freed until there are no
       references to it. That fact can be used by implementations of
       subclasses such as "Atom" to avoid unnecessary copying. */

    Text WordWrap(const Text &prefix = "", unsigned int columns = 70)
      const throw ();
    /* Wrap the content of this text string at the whitesapce between
       words.  Useful for formatting long messages. */

    Text PadLeft(unsigned int toLen, const Text &padding = " ")
      const throw();
    Text PadRight(unsigned int toLen, const Text &padding = " ")
      const throw();
    /* Pad the string on either the left or the right to at least a
       certain target length using a given padding string (which
       defaults to a space character).  (This is intended to give a
       capability line "%5s" in printf.)  The result string will
       always have exactly the requested length.  If the padding
       string is longer than one character, the result may have a
       partial copy at the end of the passing.  If the padding is the
       empty string, an assertion will fail.  */

    static Text printf(char *fmt, ...) throw();
    /* Format a text string as printf would but place it into a Text
       value. */

  protected:
    // The text is represented by a constant null-terminated string
    const char *s;
};

/* Memory Management:

   All methods in this interface that produce new "Text" values allocate 
   new buffers for those texts. In particular, the methods that allocate 
   new buffers for their results are: 

     * the single-argument constructors
     * the "operator =" (assignment) methods
     * the "operator +" (concatenation) methods
     * the "operator +=" (destructive concatenation) methods
     * the "Sub" (substring) method

   The destructor for a "Text" deletes the buffer associated with 
   it (if any). Moreover, the "operator =" (assignment) and "operator +="
   (destructive concatenation) methods delete the original buffer associated
   with their first argument.

   The (const char *) and (char *) casting operators return a pointer to the
   buffer associated with the text. Hence, if the Text is destroyed (via the
   destructor or one of the destructive operators), the pointer becomes
   invalid. */

#endif // _TEXT_H
