// Copyright (C) 2003, Kenneth C. Schalk

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

#ifndef _Basics_RegExp
#define _Basics_RegExp

#include <pcreposix.h>

#include "Basics.H"

namespace Basics
{
  // Simplified interface to regular expressions.
  class RegExp
  {
  private:
    // Parsed regular expression
    regex_t parsed;
  public:
    // Error parsing regular expression
    struct ParseError
    {
      const int code;
      const char *msg;
      const char *re;
      ParseError(int code, const char *msg, const char *re)
	: code(code), msg(msg), re(re) { }
    };

    RegExp(const char *re,
	   // cflags controls the compilation of the regular
	   // expression.  It's passed directly on to regcomp.  (See
	   // the regcomp(3) man page.)
	   int cflags = (REG_EXTENDED | REG_ICASE | REG_NOSUB))
      throw(RegExp::ParseError)
    {
	    constructor_inner(re, cflags);
    }

    RegExp(const Text &re,
	   int cflags = (REG_EXTENDED | REG_ICASE | REG_NOSUB))
      throw(RegExp::ParseError)
    {
	    constructor_inner(re.cchars(), cflags);
    }
      
    ~RegExp()
    {
      regfree(&(this->parsed));
    }

    inline bool match(const char *string,
	       // nmatch and pmatch make it possible to get
	       // information about the location of any matches.  (See
	       // the regexec(3) man page.)
	       size_t nmatch = 0, regmatch_t *pmatch = 0,
	       // eflags controls some aspects of the matching
	       // behavior.  It's passed directly on to regexec.  (See
	       // the regexec(3) man page.)
	       int eflags = 0)
    {
      return regexec(&(this->parsed), string, nmatch, pmatch, eflags) == 0;
    }

    inline bool match(const Text &text,
		      size_t nmatch = 0, regmatch_t *pmatch = 0,
		      int eflags = 0)
    {
      return match(text.cchars(), nmatch, pmatch, eflags);
    }

    inline bool operator()(const char *string,
			   size_t nmatch = 0, regmatch_t *pmatch = 0,
			   int eflags = 0)
    {
      return match(string, nmatch, pmatch, eflags);
    }

    inline bool operator()(const Text &text,
			   size_t nmatch = 0, regmatch_t *pmatch = 0,
			   int eflags = 0)
    {
      return match(text.cchars(), nmatch, pmatch, eflags);
    }

    inline size_t nsubs(void)
    {
	    return parsed.re_nsub;
    }

  private:
    // Hide copy constructor and assignment operator as there's no way
    // to copy the parsed regular expression.
    RegExp(const RegExp &);
    void operator=(const RegExp &);

    // This does the actual work for the constructors since they're
    // both exactly the same other then the type of the first argument
    void constructor_inner(const char *re, int cflags)
	    throw(RegExp::ParseError);
  };
}

#endif
