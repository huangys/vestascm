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

// Last modified on Fri Apr 22 17:43:17 EDT 2005 by ken@xorian.net

#include <string.h>
#include <Basics.H>
#include <Sequence.H>

#include "VestaSource.H"

// Result for VestaSourceSRPC::List call to the repository

struct ListResultItem
{
  char arc[MAX_ARC_LEN+1];
  VestaSource::typeTag type;
  unsigned int index;
  Bit32 pseudoInode;
  ShortId filesid;
  bool master;

  inline ListResultItem &operator=(const ListResultItem &other)
  {
    strcpy(arc, other.arc);
    type = other.type;
    index = other.index;
    pseudoInode = other.pseudoInode;
    filesid = other.filesid;
    master = other.master;

    return *this;
  }
};

typedef Sequence<ListResultItem, true> ListResultSeq;

// Result for getAttrib/listAttribs

typedef Sequence<char *> AttribResultSeq;

// Result for getAttribHistory

struct AttribHistoryResultItem
{
  VestaSource::attribOp op;
  char *name;
  char *value;
  time_t timestamp;
};

typedef Sequence<AttribHistoryResultItem> AttribHistoryResultSeq;
