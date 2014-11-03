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

// Created on Tue Mar  4 10:24:41 PST 1997 by heydon

// Last modified on Mon Aug  9 17:26:32 EDT 2004 by ken@xorian.net  
//      modified on Sat Feb 12 18:10:12 PST 2000 by mann  
//      modified on Fri Apr  9 16:18:20 PDT 1999 by heydon

#include <Basics.H>
#include <Table.H>
#include "PkgBuild.H"
#include "RootTbl.H"

using std::istream;
using std::ostream;
using std::endl;

typedef Table<PkgBuild,bool>::Iterator RootTblIter;

void RootTbl::Write(ostream &ofs) const throw (FS::Failure)
{
    int tblSize = this->Size();
    FS::Write(ofs, (char *)(&tblSize), sizeof(tblSize));
    RootTblIter iter(this);
    PkgBuild pkg; bool status;
    while (iter.Next(/*OUT*/ pkg, /*OUT*/ status)) {
	pkg.Write(ofs);
	bool8 wstatus = status ? 1 : 0;
	FS::Write(ofs, (char *) &wstatus, sizeof_assert(wstatus, 1));
    }
}

void RootTbl::Read(istream &ifs) throw (FS::EndOfFile, FS::Failure)
{
    int tblSize;
    FS::Read(ifs, (char *)(&tblSize), sizeof(tblSize));
    for (int i = 0; i < tblSize; i++) {
	PkgBuild pkg(ifs);
	bool8 wstatus;
	FS::Read(ifs, (char *) &wstatus, sizeof_assert(wstatus, 1));
	bool inTbl = this->Put(pkg, wstatus); assert(!inTbl);
    }
}

void RootTbl::Print(ostream &os, int indent,
	       const Text &tstatus, const Text &fstatus) const throw ()
{
    RootTblIter iter(this);
    PkgBuild pkg; bool status;
    while (iter.Next(/*OUT*/ pkg, /*OUT*/ status)) {
	pkg.Print(os, indent);
	os << "\t" << (status ? tstatus : fstatus) << endl;
    }
}


