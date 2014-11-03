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

// Last modified on Thu Aug  5 00:54:35 EDT 2004 by ken@xorian.net  

#include "StatLocation.H"

using std::ostream;
using std::endl;

static void Newline_Indent(ostream &os, int i) throw ()
{
  os << endl;
  for (; i > 0; i--) os << ' ';
}

void Stat::Location::print(ostream &out, int indent) const
{
  out << "path = " << path;
  if(has_pk)
    {
      Newline_Indent(out, indent);
      out << "pk = " << pk;
      Newline_Indent(out, indent);
      out << "sourceFunc = " << sourceFunc;
      if(has_cfp)
	{
	  Newline_Indent(out, indent);
	  out << "namesEpoch = " << namesEpoch;
	  Newline_Indent(out, indent);
	  out << "cfp = " << cfp;
	  if(has_ci)
	    {
	      Newline_Indent(out, indent);
	      out << "ci = " << ci;
	    }
	}
    }
}

ostream &operator<<(ostream &out, const Stat::Location &loc)
{
  loc.print(out);
  return out;
}
