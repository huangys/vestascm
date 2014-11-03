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

// File: Expr.H 
// Last Modified On Fri Mar 13 16:14:04 PST 1998 by yuanyu     
//      Modified On Tue Feb 27 15:36:13 PST 1996 by levin      

#ifndef PickleStats_H
#define PickleStats_H

// The kinds of DepPath:
enum PathKind {
  NormPK = 'N', BangPK = '!', TypePK = 'T', LLenPK = 'L', BLenPK = 'B',
  ExprPK = 'E', DummyPK = 'D' };

// Cases for subclasses of ValC:
enum ValueKind {
  BooleanVK, IntegerVK, ListVK, BindingVK, PrimitiveVK,
  TextVK, ClosureVK, ModelVK, ErrorVK, FpVK, UnbndVK };

// Cases for subclasses of ExprC:
enum ExprKind {
  ConstantEK, IfEK, ComputedEK, ArgListEK, ExprListEK, StmtListEK,
  ApplyEK, ApplyOpEK, ApplyUnOpEK, AssignEK, BindEK, BindingEK,
  BlockEK, FileEK, FuncEK, IterateEK, ListEK, ModelEK, NameEK, 
  PairEK, PrimitiveEK, SelectEK, TypedEK, BaseTEK, ListTEK, 
  BindingTEK, FuncTEK, ErrorEK };

#endif // PickleStats_H
