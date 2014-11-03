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

// Last modified on Thu May 27 00:58:09 EDT 2004 by ken@xorian.net        

#include "RegExp.H"

using std::cout;
using std::cerr;
using std::endl;

int main(void)
{
  try
    {
      Basics::RegExp aStarb("a*b");

      if(!aStarb.match("b"))
	{
	  cerr << "Regular expression \"a*b\" didn't match \"b\"" << endl;
	  return 1;
	}

      if(!aStarb.match("ab"))
	{
	  cerr << "Regular expression \"a*b\" didn't match \"ab\"" << endl;
	  return 1;
	}

      if(!aStarb.match("aaaaab"))
	{
	  cerr << "Regular expression \"a*b\" didn't match \"aaaaab\"" << endl;
	  return 1;
	}

      if(aStarb.match("aa"))
	{
	  cerr << "Regular expression \"a*b\" did match \"aa\"" << endl;
	  return 1;
	}

      // We want substring matches, so we pass non-default cflags
      Basics::RegExp bStarc("b*c", REG_EXTENDED | REG_ICASE);

      // Match and get information about the match
      Text orig("----aabbbbc----");
      regmatch_t match;
      if(bStarc.match(orig, 1, &match))
	{
	  // Extract the matched text
	  Text match_text = orig.Sub(match.rm_so, (match.rm_eo - match.rm_so));

	  if(match_text != "bbbbc")
	    {
	      cerr << "Regular expression \"b*c\" matched chars "
		   << match.rm_so << "-" << match.rm_eo
		   << " in \"" << orig
		   << "\" (\"" << match_text
		   << "\"), which isn't what we expected (\"bbbbc\")"
		   << endl;
	      return 1;

	    }

	  // Match the matched text, which better work.
	  if(!bStarc.match(match_text))
	    {
	      cerr << "Regular expression \"b*c\" matched chars "
		   << match.rm_so << "-" << match.rm_eo
		   << " in \"" << orig
		   << "\", but then didn't match the matched text (\""
		   << match_text << "\")" << endl;
	      return 1;
	    }
	}
      else
	{
	  cerr << "Regular expression \"b*c\" didn't match \"" << orig << "\""
	       << endl;
	  return 1;
	}
    }
  catch(const Basics::RegExp::ParseError &err)
    {
      cerr << "Error parsing redular expression \"" << err.re << "\": "
	   << err.msg << " (error code = " << err.code << ")" << endl;
      return 1;
    }

  cout << "All tests passed!" << endl;
  return 0;
}
