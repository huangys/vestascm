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

// File: Location.H 

#ifndef Location_H
#define Location_H

#include "EvalBasics.H"
#include <VestaSource.H>

const int None = -1;

// Source locations are specified by a file name, a line number, and
// a character number within the line.
class SrcLoc {
public:
  SrcLoc()
    : line(None), character(None), file(emptyText), shortId(NullShortId)
      { /*SKIP*/ };
  SrcLoc(int linePos, int charPos, const Text& filename, ShortId sid)
    : line(linePos), character(charPos), file(filename), shortId(sid)
      { /*SKIP*/ };
  // 1. Create a location without a position.
  // 2. Create a location with a position.
  
  Text file;       // name of source modelfile
  ShortId shortId; // shortid of modelfile
  int line;        // line number for start of expression
  int character;   // character position in line of start of expression

  SrcLoc* Copy()
    { return NEW_CONSTR(SrcLoc, (line, character, file, shortId)); };
  // Copy tloc into this.

  void Init(const Text& filename, ShortId sid)
    { line = None; character = None; file = filename; shortId = sid; };

  bool IsNone() const
    { return (line == None && character == None); };
  // Return TRUE if this has no position.

};

// Useful constants:
extern SrcLoc *noLoc;

inline std::ostream &operator<<(std::ostream &out, const SrcLoc &loc)
{
  if (!loc.IsNone())
    {
      out << loc.file << ", line " << loc.line << ", char "
	  << loc.character;
    }
  return out;
}

#endif // Location_H
