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

// Created on Sun Jul 27 15:55:56 PDT 1997 by heydon
// Last modified on Tue Aug  3 16:31:50 EDT 2004 by ken@xorian.net
//      modified on Mon Jul 28 00:54:34 PDT 1997 by heydon

#include <Basics.H>
#include <FS.H>
#include "ImmutableVal.H"

using std::ostream;
using std::istream;

void ImmutableVal::Copy(istream &ifs, ostream &ofs)
  throw (FS::EndOfFile, FS::Failure)
{
    FS::Seek(ifs, this->start);
    FS::Copy(ifs, ofs, this->len);
}
