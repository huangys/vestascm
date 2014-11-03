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

// Created on Wed Mar 12 16:07:53 PST 1997 by heydon

#include <Basics.H>
#include <Atom.H>
#include <IntKey.H>
#include <Sequence.H>
#include <BufStream.H>
#include "Pathname.H"

using std::ostream;
using Basics::OBufStream;

class PathPair {
  public:
    PathPair() throw () { /*SKIP*/ }
    PathPair(Pathname::T prefix, Atom base) throw ()
      : prefix(prefix), base(base) { /*SKIP*/ }
    Pathname::T prefix;
    Atom base;
};

// sequence type
typedef Sequence<PathPair> PathSeq;

// global sequence
PathSeq paths(/*sizeHint=*/ 500);

Pathname::T Pathname::New(Pathname::T prefix, const char *arc) throw ()
{
    Atom base(arc);
    PathPair pair(prefix, base);
    Pathname::T res = paths.size();
    paths.addhi(pair);
    return res;
}

void Pathname::Print(ostream &os, Pathname::T path) throw ()
{
    PathPair pair = paths.get(path);
    if (pair.prefix >= 0) {
	// print prefix recursively
	Pathname::Print(os, pair.prefix);
	os << '/';
    }
    os << pair.base;
}

Text Pathname::Get(T path) throw ()
{
  OBufStream buf;
  Pathname::Print(buf, path);
  return buf.str();
}
