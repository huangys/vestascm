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

// Created on Fri Feb 28 09:30:30 PST 1997 by heydon

// Last modified on Mon Aug  9 17:02:42 EDT 2004 by ken@xorian.net
//      modified on Mon Jan 26 21:24:25 PST 1998 by heydon

#include <stdio.h>
#include <Basics.H>
#include <SourceOrDerived.H>
#include <FS.H>
#include <Model.H>
#include "PkgBuild.H"

using std::istream;
using std::ostream;

void PkgBuild::Write(ostream &ofs) const throw (FS::Failure)
{
    this->pkgDirFP.Write(ofs);
    Model::Write(this->modelShortId, ofs);
}

Word PkgBuild::Hash() const throw ()
{
    Word res = this->pkgDirFP.Hash();
    res ^= (Word)(this->modelShortId);
    return res;
}

PkgBuild& PkgBuild::operator= (const PkgBuild& pkg) throw ()
{
    this->pkgDirFP = pkg.pkgDirFP;
    this->modelShortId = pkg.modelShortId;
    return *this;
}

bool operator== (const PkgBuild& pkg1, const PkgBuild& pkg2) throw ()
{
    return (pkg1.pkgDirFP == pkg2.pkgDirFP
         && pkg1.modelShortId == pkg2.modelShortId);
}

void PkgBuild::Read(istream &ifs) throw (FS::EndOfFile, FS::Failure)
{
    this->pkgDirFP.Read(ifs);
    Model::Read(/*OUT*/ this->modelShortId, ifs);
}

void PkgBuild::Print(ostream &os, int indent) const throw ()
{
    for (int i = 0; i < indent; i++) os << " ";
    os << "pkgDirFP = " << this->pkgDirFP << "; ";
    char buffer[20];
    int spres = sprintf(buffer, "%08x", this->modelShortId);
    assert(spres >= 0);
    os << "modelShortId = 0x" << buffer;
}
