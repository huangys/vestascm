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

// Created on Fri Feb 28 08:55:37 PST 1997 by heydon

// Last modified on Mon Aug  9 17:02:09 EDT 2004 by ken@xorian.net
//      modified on Sun Sep 13 16:55:53 PDT 1998 by heydon

// PkgBuild.H -- defines a structure that uniquely represents the top-level
// build of a particular version of a package.

#ifndef _PKG_BUILD_H
#define _PKG_BUILD_H

#include <Basics.H>
#include <FS.H>
#include <FP.H>
#include <Model.H>

class PkgBuild {
  public:
    // public fields
    FP::Tag pkgDirFP;      // the FP of a versioned package directory
    Model::T modelShortId; // the ShortId of a model within that package

    // constructors
    PkgBuild() throw () { /* SKIP */ }
    PkgBuild(const FP::Tag& pkgDirFP, Model::T modelShortId) throw ()
	: pkgDirFP(pkgDirFP), modelShortId(modelShortId) { /* SKIP */ }
    PkgBuild(std::istream &ifs) throw (FS::EndOfFile, FS::Failure)
	{ this->Read(ifs); }

    // Table operations
    Word Hash() const throw ();
    PkgBuild& operator= (const PkgBuild& pkg) throw ();
    friend bool operator== (const PkgBuild& pkg1, const PkgBuild& pkg2)
      throw ();

    // write/read
    void Write(std::ostream &ofs) const throw (FS::Failure);
    void Read(std::istream &ifs) throw (FS::EndOfFile, FS::Failure);

    // print
    void Print(std::ostream &os, int indent = 0) const throw ();
};

#endif /* _PKG_BUILD_H */
