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

// File: Prim.H
// Last Modified On Thu Feb 26 12:03:44 PST 1998 by yuanyu    
//      Modified On Fri Jan 26 18:46:12 PST 1996 by horning   

#ifndef Prim_H
#define Prim_H

#include "ValExpr.H"

// Types for primitive operations:
typedef Val PrimExecF(ArgList exprs, const Context& c);
typedef PrimExecF* PrimExec;

typedef Val PrimOpF(Expr e1, Expr e2, const Context& c);
typedef PrimOpF* PrimOp;

typedef Val PrimUnOpF(Expr e, const Context& c);
typedef PrimUnOpF* PrimUnOp;

// Map from the textual name of a primitive to its C++ function:
PrimExec LookupPrim(const Text& primid);

// Map from the textual name of an operator to its C++ function:
PrimOp LookupOp(const Text& opid);

// Map from the textual name of a unary operator to its C++ function:
PrimUnOp LookupUnOp(const Text& opid);

// Initialize the mapping tables:
void PrimInit();

#endif // Prim_H
