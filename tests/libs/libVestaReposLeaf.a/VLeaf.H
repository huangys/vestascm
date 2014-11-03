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

//
// VLeaf.H
// Last modified on Mon Sep 19 09:27:52 EDT 2005 by ken@xorian.net
//      modified on Tue Aug  7 17:44:14 PDT 2001 by mann
//
// Derived class for in-memory representation of Vesta sources of
// type immutableFile, ghost, and stub. 
//
// Objects of this class are used only transiently, most often as
// results from VestaSource::lookup.  Most of the sources that exist
// at any moment will not have objects of this class corresponding to
// them; they will be stored in memory only in the representation of
// their parent directories.
//


#ifndef _VLEAF
#define _VLEAF 1

#include "VestaSource.H"

class VLeaf : public VestaSource {
  private:
    ShortId shortId_;
    unsigned int link_count;
  public:
    VestaSource::errorCode
      read(void* buffer, /*INOUT*/int* nbytes, Basics::uint64 offset,
	   AccessControl::Identity who) throw ();
    bool executable() throw ();
    Basics::uint64 size() throw ();
    time_t timestamp() throw ();
    VestaSource::errorCode
      write(const void* buffer, /*INOUT*/int* nbytes, Basics::uint64 offset,
	    AccessControl::Identity who) throw ();
    VestaSource::errorCode
      setExecutable(bool x, AccessControl::Identity who) throw ();
    VestaSource::errorCode
      setSize(Basics::uint64 s, AccessControl::Identity who) throw ();
    VestaSource::errorCode
      setTimestamp(time_t ts,AccessControl::Identity who) throw ();
    ShortId shortId() throw() { return shortId_; };
    VLeaf(VestaSource::typeTag type, ShortId shortId,
	  unsigned int links = 1) throw() {
	shortId_ = shortId;
	link_count = links;
	VestaSource::type = type;
	VestaSource::attribs = NULL;
    };
    unsigned int linkCount() { return link_count; }
};

#endif // _VLEAF
