// Copyright (C) 2007, Vesta Free Software Project
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

#if !defined (_GENERICS_SPLITTEXT_H)
#define _GENERICS_SPLITTEXT_H

#include <Basics.H>
#include <Generics.H>

// Template function for splitting a Text into several on bondaries of
// one or more characters for which "separator_p" returns true.  For
// example, you can use isspace(3) from the standard library:

// TextSeq s = Generics::split_Text_ctype(t, isspace);

namespace Generics
{
  template<class F>
  TextSeq split_Text_ctype(const Text &t, F separator_p)
  {
    const char *c = t.cchars();
    TextSeq result;
    unsigned int start = 0, i;
    for(i = 0; c[i] != 0; i++)
      {
	if(i > 0)
	  {
	    if(separator_p(c[i]) && !separator_p(c[i-1]))
	      {
		assert(start < i);
		result.addhi(t.Sub(start, i-start));
	      }
	    else if(!separator_p(c[i]) && separator_p(c[i-1]))
	      {
		start = i;
	      }
	  }
      }

    if((start < i) && !separator_p(c[i-1]))
      {
	result.addhi(t.Sub(start, i-start));
      }

    return result;
  }
}

#endif
