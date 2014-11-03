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

// Created on Sun Jul 27 15:53:03 PDT 1997 by heydon
// Last modified on Tue Aug  3 13:26:24 EDT 2004 by ken@xorian.net
//      modified on Mon Jul 28 00:54:33 PDT 1997 by heydon

// ImmutableVal.H -- an immutable value served by a backing file

#ifndef _IMMUTABLE_VAL_H
#define _IMMUTABLE_VAL_H

#include <Basics.H>
#include <FS.H>

class ImmutableVal {
public:
  // constructor
  ImmutableVal(std::streampos start, int len) throw ()
    : start(start), len(len) { /*SKIP*/ }

  // copy
  void Copy(std::istream &ifs, std::ostream &ofs) throw (FS::EndOfFile, FS::Failure);
  /* Copy the immutable value from the backing file served by "ifs" to
     the current location in "ofs". This first does a "FS::Seek" to the
     proper location in "ifs". */

private:
  std::streampos start; // start position of the value in the backing file
  int len;         // length of the value in the backing file
};

#endif // _IMMUTABLE_VAL_H
