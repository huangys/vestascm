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

// Created on Wed Mar 12 20:31:27 PST 1997 by heydon

// Last modified on Tue Nov 13 13:09:23 EST 2001 by ken@xorian.net
//      modified on Tue Jul  7 16:15:32 PDT 1998 by heydon

// ReposRoots.H -- find all package roots in the repository

#ifndef _REPOS_ROOTS_H
#define _REPOS_ROOTS_H

#include <Basics.H>
#include <Table.H>
#include <SRPC.H>
#include "PkgBuild.H"
#include "RootTbl.H"
#include "Pathname.H"

// maps PkgBuild -> Pathname::T
typedef Table<PkgBuild,Pathname::T>::Default PkgTbl;

class ReposRoots {
  public:
    static PkgTbl *Scan(bool debug) throw (SRPC::failure, ReposError);
    /* Scan the file system rooted at the repository's appendable root.
       Returns a new "PkgTbl" that maps the pair <directory-FP, NullShortId>
       to the full pathname of every immutable directory and that maps
       the pair <directory-FP, model-ShortId> to the full pathname of
       each immutable file with extension ".ves" contained in an immutable
       directory. */
};

#endif // _REPOS_ROOTS_H
