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

// File: ValExpr.H 

#ifndef ValExpr_H
#define ValExpr_H

#include "ListT.H"
#include <Sequence.H>
#include <Table.H>

class ValC;
typedef ValC* Val;
typedef ListT<Val> Vals;

class AssocVC;
typedef AssocVC* Assoc;
typedef ListT<Assoc> Context;

class BindingVC;
typedef BindingVC* Binding;

class BooleanVC;
class IntegerVC;
class ClosureVC;
class ModelVC;

class ExprC;
typedef ExprC* Expr;

class ExprListEC;
typedef ExprListEC* ExprList;

class ArgListEC;
typedef ArgListEC* ArgList;

class StmtListEC;
typedef StmtListEC* StmtList;

class NameEC;
typedef NameEC* Name;

class ApplyEC;
class FuncEC;

typedef Sequence<Expr> Exprs;

typedef ListT<Text> Vars;

extern const char *nameDot;
extern Name exprDot;

#endif // ValExpr_H
