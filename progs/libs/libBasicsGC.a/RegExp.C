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

// Last modified on Sun Apr 17 22:47:32 EDT 2005 by ken@xorian.net        

#include "RegExp.H"

void Basics::RegExp::constructor_inner(const char *re, int cflags)
  throw(Basics::RegExp::ParseError)
{
  int comp_result = regcomp(&(this->parsed), re, cflags);
  if(comp_result != 0)
    {
      unsigned int esize = 1024;
      char *ebuf = NEW_PTRFREE_ARRAY(char, esize);
      while(regerror(comp_result, &(this->parsed), ebuf, esize) > esize)
	{
	  delete [] ebuf;
	  esize *= 2;
	  ebuf = NEW_PTRFREE_ARRAY(char, esize);
	}
      throw Basics::RegExp::ParseError(comp_result, ebuf, re);
    }
}

