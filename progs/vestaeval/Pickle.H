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

// File: Pickle.H

#ifndef Pickle_H
#define Pickle_H

#include "ValExpr.H"
#include "Dep.H"
#include "Location.H"
#include <FP.H>
#include <Derived.H>
#include <CacheC.H>
#include <CacheIntf.H>
#include <PrefixTbl.H>

// the Pickle function returns true if pickling was successful.
// it returns false only in a case of value having "cacheit" set to false. 
// In the event of FS::failure it prints error message and aborts 
// and in the evant of PrefixTbl::Overflow it passes the failure to the caller, 
// where it is treated as fatal and evaluation aborts.
bool Pickle(Val value, /*OUT*/ VestaVal::T& vval);
bool Unpickle(const VestaVal::T& vval, const Context& c, /*OUT*/ Val& value);

#endif  // Pickle_H
