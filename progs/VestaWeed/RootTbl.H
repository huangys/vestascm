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

// Created on Tue Mar  4 10:22:25 PST 1997 by heydon

// Last modified on Mon Aug  9 17:07:37 EDT 2004 by ken@xorian.net
//      modified on Fri Apr  9 16:10:10 PDT 1999 by heydon

// RootTbl.H -- defines a table type for storing a set of weeder roots

#ifndef _ROOT_TBL_H
#define _ROOT_TBL_H

#include <Basics.H>
#include <Table.H>
#include "PkgBuild.H"

// A "RootTbl" is a collection (set) of "PkgBuild" objects; the
// range of the table is used in a few cases.

typedef Table<PkgBuild,bool>::Default PkgBuildTbl;

class RootTbl: public PkgBuildTbl {
  public:
    RootTbl(int sizeHint = 0, bool useGC = false) throw ()
	: PkgBuildTbl(sizeHint, useGC) {}
    RootTbl(std::istream &ifs) throw (FS::EndOfFile, FS::Failure)
	{ this->Read(ifs); }

    void Write(std::ostream &ofs) const throw (FS::Failure);
    void Read(std::istream &ifs) throw (FS::EndOfFile, FS::Failure);

    void Print(std::ostream &os, int indent=0,
	       const Text &tstatus = "", const Text &fstatus = "") const
      throw ();
};

#endif // _ROOT_TBL_H
