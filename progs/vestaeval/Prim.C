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

/* File: Prim.C                                                */

#include "Prim.H"
#include "Val.H"
#include "Expr.H"
#include "ApplyCache.H"
#include "Location.H"
#include "Err.H"
#include "Debug.H"
#include "VASTi.H"
#include "ThreadData.H"
#include <Table.H>
#include <VestaConfig.H>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <BufStream.H>
#include "RegExp.H"

using std::ostream;
using std::endl;
using std::hex;
using std::setw;
using std::setfill;
using Basics::OBufStream;
using OS::cio;

static const Text noName("Anonymous");
static int numThreads = 1;
static int threadAllowed;
static bool parMapFailing = false;
static Basics::mutex parMu;
static Basics::cond WorkCond;
static IntSeq ThreadIds;
static int ThreadIdMax = 1;

void PrimError(const Text& msg, const Vals& args, SrcLoc *loc) {
  ostream& err_stream = cio().start_err();
  Error(err_stream, msg + ":\n  args = ", loc);
  ErrorArgs(err_stream, args);
  ErrorDetail(err_stream, ".\n");
  cio().end_err();
}

void PrimError(const Text& msg, Val arg, SrcLoc *loc) {
  ostream& err_stream = cio().start_err();
  Error(err_stream, msg + ":\n  arg = `", loc);
  ErrorVal(err_stream, arg);
  ErrorDetail(err_stream, "'.\n");
  cio().end_err();
}
  
void PrimError(const Text& msg, Val arg1, Val arg2, SrcLoc *loc) {
  ostream& err_stream = cio().start_err();
  Error(err_stream, msg + ":\n  arg1 = `", loc);
  ErrorVal(err_stream, arg1);
  ErrorDetail(err_stream, "',\n  arg2 = `");
  ErrorVal(err_stream, arg2);
  ErrorDetail(err_stream, "'.\n");
  cio().end_err();
}

static Table<Text,PrimExec>::Default Primitives(64);

void AddPrimitive(const Text& id, PrimExec exec) {
  if (Primitives.Put(id, exec)) {
    Error(cio().start_err(), 
	  "(impl) Name conflict in Primitives! `" + id + "'.\n");
    cio().end_err();
  }
}

PrimExec LookupPrim(const Text& id) {
  PrimExec result;
  return Primitives.Get(id, result) ? result : NULL;
}

//// Primitive functions:
Val IsType(Val v, bool b) {
  // This function is used only in IsXxx. Since it creates new
  // sharing of v->dps, be careful with any other use.
  Val result = NEW_CONSTR(BooleanVC, (b));
  result->dps = v->dps;
  result->AddToDPS(v->path, ValType(v), TypePK);
  result->cacheit = v->cacheit;
  return result;
}

Val IsBinding(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 1) {
    PrimError("_is_binding takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v = args.First();
  bool b = (v->vKind == BindingVK);
  return IsType(v, b);
}

Val IsBool(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 1) {
    PrimError("_is_bool takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v = args.First();
  bool b = (v->vKind == BooleanVK);
  return IsType(v, b);
}

Val IsClosure(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 1) {
    PrimError("_is_closure takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v = args.First();
  bool b = (v->vKind == ClosureVK ||
	    v->vKind == ModelVK ||
	    v->vKind == PrimitiveVK);
  return IsType(v, b);
}

Val IsErr(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 1) {
    PrimError("_is_err takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v = args.First();
  bool b = (v->vKind == ErrorVK);
  return IsType(v, b);
}

Val IsInt(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 1) {
    PrimError("_is_int takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v = args.First();
  bool b = (v->vKind == IntegerVK);
  return IsType(v, b);
}

Val IsList(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 1) {
    PrimError("_is_list takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v = args.First();
  bool b = (v->vKind == ListVK);
  return IsType(v, b);
}

Val IsText(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 1) {
    PrimError("_is_text takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v = args.First();
  bool b = (v->vKind == TextVK);
  return IsType(v, b);
}

Val TypeOfVal(Val v) {
  // This function is used only in TypeOf and SameType. Since it creates
  // new sharing of v->dps, be careful with any other use.
  Val tv = ValType(v);
  Val result = tv->Copy();
  result->dps = v->dps;
  result->AddToDPS(v->path, tv, TypePK);
  return result;
}
  
Val TypeOf(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 1) {
    PrimError("_type_of takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }
  return TypeOfVal(args.First());
}

Val SameTypeInner(Val v1, Val v2) {
  Val result;
  bool b = (v1->vKind == v2->vKind);
  result = NEW_CONSTR(BooleanVC, (b));
  result->dps = TypeOfVal(v1)->dps;
  return result->MergeAndTypeDPS(v2);
}

Val SameType(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 2) {
    PrimError("_same_type takes two arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v1 = args.First(), v2 = args.Second();
  Val result = SameTypeInner(v1, v2);
  result->cacheit = v1->cacheit && v2->cacheit;
  return result;
}

Val BindingAppend(BindingVC* b1, BindingVC* b2) {
  Context c1 = b1->elems, c2 = b2->elems;
  Context rc;
  Val v, test;
  Assoc a1, a2;
  Text name;

  while (!c1.Null()) {
    a1 = c1.Pop();
    name = a1->name;
    test = b2->Defined(name);
    if (IsValTrue(test)) {
      Val result = NEW_CONSTR(ErrorVC, (false));
      return result->Merge(test)->Merge(b1->Defined(name));
    }
    v = b1->Extend(a1->val, name, NormPK, false);
    rc.Append1D(NEW_CONSTR(AssocVC, (name, v)));
  }
  while (!c2.Null()) {
    a2 = c2.Pop();
    name = a2->name;
    v = b2->Extend(a2->val, name, NormPK, false);
    rc.Append1D(NEW_CONSTR(AssocVC, (name, v)));
  }
  BindingVC *result = NEW_CONSTR(BindingVC, (rc));
  result->MergeAndLenDPS(b1)->MergeAndLenDPS(b2);
  result->cacheit = b1->cacheit && b2->cacheit;
  return result;
}

Val ListAppend(ListVC* l1, ListVC* l2) {
  Vals elems1 = l1->elems, elems2 = l2->elems;
  Val v1, v2, v;
  Vals rc;

  int cnt = 0;
  while (!elems1.Null()) {
    v1 = elems1.Pop();
    v = l1->Extend(v1, IntArc(cnt++), NormPK, false);
    rc.Append1D(v);
  }
  cnt = 0;
  while (!elems2.Null()) {
    v2 = elems2.Pop();
    v = l2->Extend(v2, IntArc(cnt++), NormPK, false);
    rc.Append1D(v);
  }
  ListVC* result = NEW_CONSTR(ListVC, (rc));
  result->MergeDPS(l1->dps)->MergeDPS(l2->dps);

  // In addition to maintaining result->lenDps, we add length dep of l1 
  // into result->dps. Adding length dep of l1 into result->dps can be a 
  // bit too coarse.  A possible fix is to add a copy length dep of l2 
  // into each of elements of l2 in the second while statement.
  if (l1->path != NULL) {
    result->AddToDPS(l1->path, NEW_CONSTR(IntegerVC, (l1->elems.Length())), 
		     LLenPK);
    result->AddToLenDPS(*l1->path, l1);
  }
  else {
    result->MergeLenDPS(l1);
    result->MergeToLenDPS(l1->lenDps);
  }

  if (l2->path != NULL)
    result->AddToLenDPS(*l2->path, l2);
  else
    result->MergeToLenDPS(l2->lenDps);
  result->cacheit = l1->cacheit && l2->cacheit;
  return result;
}

Val Append(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);

  if (args.Length() != 2) {
    PrimError("_append must take two arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v1 = args.First(), v2 = args.Second(), result;
  if (v1->vKind != v2->vKind) {
    PrimError("`_append' not implemented for these args", v1, v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1)->MergeAndTypeDPS(v2);
  }
  switch (v1->vKind) {
  case ListVK:
    // The same as +.
    if (IsEmptyList(v2)) {
      v1->cacheit = v1->cacheit && v2->cacheit;
      return v1->MergeAndLenDPS(v2);
    }
    if (IsEmptyList(v1)) {
      v2->cacheit = v1->cacheit && v2->cacheit;
      return v2->MergeAndLenDPS(v1);
    }
    return ListAppend((ListVC*)v1, (ListVC*)v2);
  case BindingVK:
    if (IsEmptyBinding(v2)) {
      v1->cacheit = v1->cacheit && v2->cacheit;
      return v1->MergeAndLenDPS(v2);
    }
    if (IsEmptyBinding(v1)) {
      v2->cacheit = v1->cacheit && v2->cacheit;
      return v2->MergeAndLenDPS(v1);
    }
    result = BindingAppend((BindingVC*)v1, (BindingVC*)v2);
    if (result->vKind == ErrorVK) {
      PrimError("Bindings passed to _append must be disjoint", v1, v2, exprs->loc);
      return NEW(ErrorVC);
    }
    return result;
  default:
    PrimError("Arguments of _append must be lists or bindings", v1, v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1)->MergeAndTypeDPS(v2);
  }
}

Val BindingMinus(BindingVC* b1, BindingVC* b2) {
  Context rc;
  
  Context work = b1->elems;
  while (!work.Null()) {
    Assoc a = work.Pop();
    if (!b2->DefinedNoDpnd(a->name)) {
      Val v = b1->Extend(a->val, a->name, NormPK, false);
      rc.Append1D(NEW_CONSTR(AssocVC, (a->name, v)));
    }
  }
  BindingVC *result = NEW_CONSTR(BindingVC, (rc));
  result->MergeAndTypeDPS(b1)->MergeAndLenDPS(b2);
  // Get the result->lenDps correct:
  if (b1->path != NULL)
    result->AddToLenDPS(*b1->path, b1);
  else
    result->MergeToLenDPS(b1->lenDps);
  if (b2->path != NULL)
    result->AddToLenDPS(*b2->path, b2);
  else
    result->MergeToLenDPS(b2->lenDps);
  result->cacheit = b1->cacheit && b2->cacheit;
  return result;
}

Val Minus(Expr e1, Expr e2, const Context& c) {
  Val v1 = e1->Eval(c), v2 = e2->Eval(c);
  Val result;

  if (v1->vKind != v2->vKind) {
    PrimError("`-' not implemented for these args", v1, v2, e1->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1)->MergeAndTypeDPS(v2);
  }
  switch (v1->vKind) {
  case IntegerVK:
    {
      Basics::int32 n1 = ((IntegerVC*)v1)->num;
      Basics::int32 n2 = ((IntegerVC*)v2)->num;
      Basics::int32 res = n1 - n2;
      if ((n1 < 0) != (n2 < 0) && (n1 < 0) != (res < 0)) {
	PrimError("Overflow on `-'", v1, v2, e1->loc);
	result = NEW(ErrorVC);
	return result->Merge(v1)->Merge(v2);
      }
      result = NEW_CONSTR(IntegerVC, (res));
      result->cacheit = v1->cacheit && v2->cacheit;
      return result->Merge(v1)->Merge(v2);
    }
  case BindingVK:
    if (IsEmptyBinding(v2)) {
      v1->cacheit = v1->cacheit && v2->cacheit;
      return v1->MergeAndLenDPS(v2);
    }
    if (IsEmptyBinding(v1)) {
      v1->cacheit = v1->cacheit && v2->cacheit;
      return v1->MergeAndTypeDPS(v2);
    }
    return BindingMinus((BindingVC*)v1, (BindingVC*)v2);
  default:
    PrimError("`-' not implemented for these args", v1, v2, e1->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1)->MergeAndTypeDPS(v2);
  }
}
		  
Val Bind1(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);

  if (args.Length() != 2) {
    PrimError("_bind1 takes two arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v1 = args.First(), v2 = args.Second(), result;
  if (v1->vKind != TextVK) {
    PrimError("First argument of _bind1 must be a text", v1, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  Text name(((TextVC*)v1)->NDS());
  if (name.Empty()) {
    PrimError("First argument of _bind1 must be nonempty", v1, exprs->loc);
    result = NEW(ErrorVC);
    return result->Merge(v1);
  }
  result = NEW_CONSTR(BindingVC, 
		      (Context(NEW_CONSTR(AssocVC, (name, v2)))));
  result->cacheit = v1->cacheit;
  return result->Merge(v1);
}

Val List1(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 1) {
    PrimError("_list1 takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v1 = args.First();
  // cacheit is set to true:
  return NEW_CONSTR(ListVC, (Vals(v1)));
}

Val Defined(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 2) {
    PrimError("_defined takes two arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v1 = args.First(), v2 = args.Second(), result;
  if (v2->vKind != TextVK) {
    PrimError("Second argument of _defined must be a text", v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  Text name(((TextVC*)v2)->NDS());
  if (name.Empty()) {
    PrimError("Second argument of _defined must be nonempty", v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->Merge(v2);
  }
  if (IsEmptyBinding(v1)) {
    result = NEW_CONSTR(BooleanVC, (false));
    result->cacheit = v1->cacheit && v2->cacheit;
    return result->MergeAndLenDPS(v1)->Merge(v2);
  }
  if (v1->vKind != BindingVK) {
    PrimError("First argument of _defined must be a binding", v1, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  result = ((BindingVC*)v1)->Defined(name);
  result->cacheit = result->cacheit && v2->cacheit;
  return result->Merge(v2);
}

Val Div(ArgList exprs, const Context& ctxt) {
  Vals args = EvalArgs(exprs, ctxt);
  if (args.Length() != 2) {
    PrimError("_div takes two arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v1 = args.First(), v2 = args.Second(), result;
  if (v1->vKind != IntegerVK) {
    PrimError("First arguments of _div must be integer", v1, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  if (v2->vKind != IntegerVK) {
    PrimError("Second argument of _div must be integer", v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  Basics::int32 a = ((IntegerVC*)v1)->num;
  Basics::int32 b = ((IntegerVC*)v2)->num;
  if (b == 0) {
    PrimError("Attempt to divide by 0 with _div", args, exprs->loc);
    result = NEW(ErrorVC);
    return result->Merge(v2);
  }
  // This code does Modula-3 style DIV
  Basics::int32 c;
  if ((a == 0) && (b != 0)) {  c = 0;
  } else if (a > 0)  {  c = (b >= 0) ? (a) / (b) : -1 - (a-1) / (-b);
  } else /* a < 0 */ {  c = (b >= 0) ? -1 - (-1-a) / (b) : (-a) / (-b);
  }
  if (b == -1 && a == c) {
    PrimError("Overflow in _div", args, exprs->loc);
    result = NEW(ErrorVC);
    return result->Merge(v1)->Merge(v2);
  }
  result = NEW_CONSTR(IntegerVC, (c));
  result->cacheit = v1->cacheit && v2->cacheit;
  return result->Merge(v1)->Merge(v2);
}

Val Elem(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 2) {
    PrimError("_elem takes two arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v1 = args.First(), v2 = args.Second(), result;
  if (v2->vKind != IntegerVK) {
    PrimError("Second argument of _elem must be an integer", v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  Basics::int32 n = ((IntegerVC*)v2)->num;
  switch (v1->vKind) {
  case TextVK:
    if (n < 0) {
      result = NEW_CONSTR(TextVC, (emptyText));
      result->cacheit = v1->cacheit && v2->cacheit;
      return result->MergeAndTypeDPS(v1)->Merge(v2);
    }
    if (n > Text::MaxInt) {
      PrimError("(impl) _elem integer argument too big", v2, exprs->loc);
      result = NEW(ErrorVC);
      return result->MergeAndTypeDPS(v1)->Merge(v2);
    }
    result = NEW_CONSTR(TextVC, (((TextVC*)v1)->NDS().Sub(n, 1)));
    result->cacheit = v1->cacheit && v2->cacheit;
    return result->Merge(v1)->Merge(v2);
  case ListVK:
    {
      ListVC *lstv = (ListVC*)v1;
      int len = lstv->elems.Length();
      if (n < 0 || n >= len) {
	PrimError("_elem number out of range", v2, exprs->loc);
	result = NEW(ErrorVC);
	return result->MergeAndLenDPS(lstv)->Merge(v2);
      }
      result = lstv->GetElem(n);
      result->cacheit = result->cacheit && v2->cacheit;
      return result->Merge(v2);      
    }
  case BindingVK:
    {
      BindingVC *bv = (BindingVC*)v1;
      int len = bv->elems.Length();
      if (n < 0 || n >= len) {
	PrimError("_elem number out of range", v2, exprs->loc);
	result = NEW(ErrorVC);
	return result->MergeAndLenDPS(bv)->Merge(v2);
      }
      Assoc a = bv->elems.Nth(n);
      Val v = v1->Extend(a->val, IntArc(n), NormPK, false);
      result = NEW_CONSTR(BindingVC, 
			  (Context(NEW_CONSTR(AssocVC, (a->name, v)))));
      result->cacheit = result->cacheit && v2->cacheit;
      return result->MergeDPS(v1->dps)->Merge(v2);
    }
  default:
    PrimError("First argument of _elem must be a text, list, or binding", v1, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
}

Val Assert(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 2) {
    PrimError("_assert takes two arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v1 = args.First();
  if (v1->vKind != BooleanVK) {
    PrimError("First argument of _assert must be a boolean", v1, exprs->loc);
    return NEW(ErrorVC);
  }
  if (((BooleanVC*)v1)->b) { return v1; }
  Val v2 = args.Second();
  ostream& err_stream = cio().start_err();  
  Error(err_stream, "");
  ErrorVal(err_stream, v2);
  ErrorDetail(err_stream, ".\n");
  if (diagnose) { PrintContext(err_stream, c); }
  cio().end_err();
  return NEW(ErrorVC);
}

Val FindInner(ArgList exprs, const Context& c, int inc, Text prim) {
  Vals args = EvalArgs(exprs, c);
  Basics::int32 start = 0;
  Val v1, v2, v3 = valZero, result;
  switch (args.Length()) {
  case 3:
    v3 = args.Third();
    if (v3->vKind != IntegerVK) {
      PrimError("Third argument of " + prim + " must be an integer", v3, exprs->loc);
      result = NEW(ErrorVC);
      return result->MergeAndTypeDPS(v3);
    }
    start = ((IntegerVC*)v3)->num;
    break;
  case 2:
    break;
  default:
    PrimError(prim + " takes two or three arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  v1 = args.First();
  if (v1->vKind != TextVK) {
    PrimError("First argument of " + prim + " must be a text", v1, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  Text haystack(((TextVC*)v1)->NDS());
  v2 = args.Second();
  if (v2->vKind != TextVK) {
    PrimError("Second argument of " + prim + " must be a text", v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  Text needle(((TextVC*)v2)->NDS());
  if (start > Text::MaxInt) {
    PrimError("(impl) " + prim + " integer argument too big", v3, exprs->loc);
    result = NEW(ErrorVC);
    return result->Merge(v3);
  }
  int nlen = needle.Length();
  int j = haystack.Length() - nlen;
  int i = max(start, 0);
  int offset = inc > 0 ? i : j;
  int end = inc < 0 ? i : j;
  while ((inc > 0) ? (offset <= end) : (end <= offset)) {
    int k = 0;
    while (k < nlen && haystack[offset+k] == needle[k]) k++;
    if (k == nlen) {
      result = NEW_CONSTR(IntegerVC, (offset));
      result->cacheit = v1->cacheit && v2->cacheit && v3->cacheit;
      return result->Merge(v1)->Merge(v2)->Merge(v3);
    }
    offset += inc;
  }
  result = NEW_CONSTR(IntegerVC, (-1));
  result->cacheit = v1->cacheit && v2->cacheit && v3->cacheit;
  return result->Merge(v1)->Merge(v2)->Merge(v3);
}

inline Val Find(ArgList exprs, const Context& c) {
  return FindInner(exprs, c, 1, "_find");
}

inline Val FindR(ArgList exprs, const Context& c) {
  return FindInner(exprs, c, -1, "_findr");
}

Val FindRegex(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  Basics::int32 start = 0;
  int cflags = REG_EXTENDED;
  Val v1, v2, v3 = valZero, result;
  switch (args.Length()) {
  case 3:
    v3 = args.Third();
    if (v3->vKind != IntegerVK) {
      PrimError("Third argument of _findre must be an integer", v3, exprs->loc);
      result = NEW(ErrorVC);
      return result->MergeAndTypeDPS(v3);
    }
    start = ((IntegerVC*)v3)->num;
    break;
  case 2:
    break;
  default:
    PrimError("_findre takes two or three arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  v1 = args.First();
  if (v1->vKind != TextVK) {
    PrimError("First argument of _findre must be a text", v1, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  Text haystack(((TextVC*)v1)->NDS());
  if(start < 0) //negative offset means start back from the end
          start = max(0, haystack.Length() + start);
  v2 = args.Second();
  if (v2->vKind != TextVK) {
    PrimError("Second argument of _findre must be a text", v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  Text needle(((TextVC*)v2)->NDS());
  if (start > Text::MaxInt) {
    PrimError("(impl) _findre integer argument too big", v3, exprs->loc);
    result = NEW(ErrorVC);
    return result->Merge(v3);
  }
  int nmatch;
  regmatch_t *pmatch;
  bool match;
  try {
    Basics::RegExp re(needle, cflags);
    nmatch = re.nsubs() + 1;
    pmatch = NEW_ARRAY(regmatch_t, nmatch);
    match = re.match(haystack.cchars() + start, nmatch, pmatch);
  } catch(const Basics::RegExp::ParseError &err) {
    Text err_msg("Error parsing regular expression: ");
    err_msg += err.msg;
    PrimError(err_msg, v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->Merge(v2);
  }
  Vals vs;
  if (match) {
          for(int i=0; i<nmatch; i++) {
                  Vals startstop;
                  Val match_start = NEW_CONSTR(IntegerVC, (pmatch[i].rm_so + start));
                  startstop = startstop.Append1(match_start);
                  Val match_stop = NEW_CONSTR(IntegerVC, (pmatch[i].rm_eo + start));
                  startstop = startstop.Append1(match_stop);
		  Val ss_list = NEW_CONSTR(ListVC, (startstop));
                  vs = vs.Append1(ss_list);
          }
	  result = NEW_CONSTR(ListVC, (vs));
  } else {
          result = NEW_CONSTR(BooleanVC, (false));
  }
  result->cacheit = v1->cacheit && v2->cacheit
                 && v3->cacheit;
  return result->Merge(v1)->Merge(v2)->Merge(v3);
}

Val Head(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);

  if (args.Length() != 1) {
    PrimError("_head takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }    
  Val v = args.First(), result;
  switch (v->vKind) {
  case ListVK:
    {
      Vals vals = ((ListVC*)v)->elems;
      if (vals.Null()) {
	PrimError("Argument of _head must not be nil", v, exprs->loc);
	result = NEW(ErrorVC);
	return result->Merge(v);
      }
      return ((ListVC*)v)->GetElem(0);
    }
  case BindingVK:
    {
      Context ctxt = ((BindingVC*)v)->elems;
      if (ctxt.Null()) {
	PrimError("Argument of _head must not be nil", v, exprs->loc);
	result = NEW(ErrorVC);
	return result->Merge(v);
      }
      Assoc a = ctxt.First();
      Val v0 = v->Extend(a->val, IntArc(0), NormPK, false);
      result = NEW_CONSTR(BindingVC, 
			  (Context(NEW_CONSTR(AssocVC, (a->name, v0)))));
      result->cacheit = v->cacheit;
      return result->MergeDPS(v->dps);
    }
  default:
    PrimError("Argument of _head must be a list or binding", v, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v);
  }
}

Val Tail(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 1) {
    PrimError("_tail takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }    
  Val v = args.First();
  switch (v->vKind) {
  case ListVK:
    {
      ListVC *lstv = (ListVC*)v;
      Vals vals = lstv->elems;
      if (vals.Null()) {
	PrimError("Argument of _tail must not be nil", lstv, exprs->loc);
	Val result = NEW(ErrorVC);
	return result->Merge(lstv);
      }
      Val v1 = vals.Pop();
      Vals vs;
      ListVC *result;
      if (lstv->path == NULL) {
	result = NEW_CONSTR(ListVC, (vals));
	result->lenDps = lstv->lenDps;
      }
      else {
	int cnt = 1;
	while (!vals.Null()) {
	  v1 = lstv->Extend(vals.Pop(), IntArc(cnt++), NormPK, false);
	  vs.Append1D(v1);
	}
	result = NEW_CONSTR(ListVC, (vs));
	// Add the length dependency:
	result->AddToLenDPS(*lstv->path, lstv);
      }
      result->dps = lstv->dps;
      result->cacheit = lstv->cacheit;
      return result;
    }
  case BindingVK:
    {
      BindingVC *bv = (BindingVC*)v;
      Context ctxt = bv->elems;
      if (ctxt.Null()) {
	PrimError("Argument of _tail must not be nil", bv, exprs->loc);
	Val result = NEW(ErrorVC);
	return result->Merge(bv);
      }
      Assoc a = ctxt.Pop();
      Context cc;
      Val v1;
      BindingVC *result;
      if (bv->path == NULL) {
	result = NEW_CONSTR(BindingVC, (ctxt));
	result->lenDps = bv->lenDps;
      }
      else {
	while (!ctxt.Null()) {
	  a = ctxt.Pop();
	  v1 = bv->Extend(a->val, a->name, NormPK, false);
	  cc.Append1D(NEW_CONSTR(AssocVC, (a->name, v1)));
	}
	result = NEW_CONSTR(BindingVC, (cc));
	// Add the length dependency:
	result->AddToLenDPS(*bv->path, bv);
      }
      result->dps = bv->dps;
      result->cacheit = bv->cacheit;
      return result;
    }
  default:
    {
      PrimError("Argument of _tail must be a list or binding", v, exprs->loc);
      Val result = NEW(ErrorVC);
      return result->MergeAndTypeDPS(v);
    }
  }
}

Val Length(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 1) {
    PrimError("_length takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v1 = args.First(), result;
  switch (v1->vKind) {
  case TextVK:
    result = NEW_CONSTR(IntegerVC, (((TextVC*)v1)->NDS().Length()));
    result->cacheit = v1->cacheit;
    return result->Merge(v1);
  case ListVK:
    return ((ListVC*)v1)->Length();
  case BindingVK:
    return ((BindingVC*)v1)->Length();
  default:
    PrimError("Argument of _length must be a text, list, or binding", v1, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
}

Val Lookup(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 2) {
    PrimError("_lookup takes two arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v1 = args.First(), v2 = args.Second(), result;
  if (v2->vKind != TextVK) {
    PrimError("Second argument of _lookup must be a text", v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  Text name(((TextVC*)v2)->NDS());
  if (name.Empty()) {
    PrimError("Second argument of _lookup must be nonempty", v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->Merge(v2);
  }
  if (IsEmptyBinding(v1)) {
    PrimError("_lookup of unbound name", args, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndLenDPS(v1)->Merge(v2);
  }
  if (v1->vKind != BindingVK) {
    PrimError("First argument of _lookup must be a binding", v1, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  result = ((BindingVC*)v1)->Lookup(name);
  if (result->vKind == UnbndVK) {
    PrimError("_lookup of unbound name", args, exprs->loc);
    Val err = NEW(ErrorVC);
    err->MergeDPS(result->dps);
    return err->AddToDPS(result->path, valFalse, BangPK);
  }
  result->cacheit = result->cacheit && v2->cacheit;
  return result->Merge(v2);
}

Val Map(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 2) {
    PrimError("_map takes two arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  ThreadData *thData = ThreadDataGet();
  ostream *traceRes = thData->traceRes;	  
  Val v1 = args.First(), v2 = args.Second(), result;
  switch (v2->vKind) {
  case BindingVK:
    {
      Context work = ((BindingVC*)v2)->elems, elems;
      if (v1->vKind != ClosureVK) {
	PrimError("The first argument of _map must be a function",
		  v1, exprs->loc);
	result = NEW(ErrorVC);
	return result->MergeAndTypeDPS(v1);
      }
      ClosureVC *fun = (ClosureVC*)v1;
      Exprs forms = fun->func->args->elems;
      if (forms.size() != 2) {
	PrimError("The first argument of _map must be a function of two arguments when the second argument is a binding",
		  v1, exprs->loc);
	result = NEW(ErrorVC);
	return result->MergeAndTypeDPS(v1);
      }
      Text name1, name2;
      Expr fe = forms.get(0);
      if (fe->kind == NameEK)
	name1 = ((Name)fe)->id;
      else if (fe->kind == AssignEK)
	name1 = ((AssignEC*)fe)->lhs->id;
      else {
	fe->EError(cio().start_err(), "The function has bad parameter list.");
	cio().end_err();
	return NEW(ErrorVC);
      }
      fe = forms.get(1);
      if (fe->kind == NameEK)
	name2 = ((Name)fe)->id;
      else if (fe->kind == AssignEK)
	name2 = ((AssignEC*)fe)->lhs->id;
      else {
	fe->EError(cio().start_err(), "The function has bad parameter list.");
	cio().end_err();
	return NEW(ErrorVC);
      }
      bool cacheit = true;
      Name eDot = NEW_CONSTR(NameEC, (nameDot, exprs->loc));
      Val vDot = eDot->Eval(RestrictContext(c, eDot->freeVars));
      DPaths *ps = NEW(DPaths);
      AssocVC dotAssoc(nameDot, vDot),
	n_Assoc(name1, valUnbnd),
	v_Assoc(name2, valUnbnd);
      Context argsCon(&n_Assoc, &v_Assoc, &dotAssoc);
      while (!work.Null()) {
	Assoc a = work.Pop();
	Val elem = v2->Extend(a->val, a->name, NormPK, false);
	n_Assoc.val = NEW_CONSTR(TextVC, (a->name));
	v_Assoc.val = elem;
	// Apply the function:
	thData->funcCallDepth++;
	if (traceRes) {
	  *traceRes << "  " << thData->funcCallDepth << ". "
		    << fun->func->args->loc->file << ": "
		    << fun->func->name << "()";
	}
	elem = ApplicationFromCache(fun, argsCon, exprs->loc);
	thData->funcCallDepth--;
	if (elem->vKind != BindingVK) {
	  Error(cio().start_err(), 
		"The function must return a binding.\n", exprs->loc);
	  cio().end_err();
	  result = NEW(ErrorVC);
	  return result->MergeDPS(v2->dps)->Merge(elem);
	}
	// Add the new result elem into elems that is used to collect
	// the results.
	Context work = ((BindingVC*)elem)->elems;
	ps = ps->Union(elem->dps);
	while (!work.Null()) {
	  Assoc as = work.Pop();
	  if (FindInContext(as->name, elems) != nullAssoc) {
	    Error(cio().start_err(), 
		  "Field name conflicts in binding.\n", exprs->loc);
	    cio().end_err();
	    result = NEW(ErrorVC);
	    return result->MergeDPS(ps)->Merge(elem);
	  }
	  Val asVal = elem->Extend(as->val, as->name, NormPK, false);
	  elems.Append1D(NEW_CONSTR(AssocVC, (as->name, asVal)));
	}
	cacheit = cacheit && elem->cacheit;
      }
      result = NEW_CONSTR(BindingVC, (elems));
      result->dps = ps;
      result->MergeAndLenDPS(v2);
      result->cacheit = cacheit;
      return result;
    }
  case ListVK:
    {
      Vals work = ((ListVC*)v2)->elems, vals;
      switch (v1->vKind) {
      case ClosureVK:
	{
	  ClosureVC *fun = (ClosureVC*)v1;
	  Exprs forms = fun->func->args->elems;
	  if (forms.size() != 1) {
	    PrimError("The first argument of _map must be a function of one argument when the second argument is a list",
		      v1, exprs->loc);
	    result = NEW(ErrorVC);
	    return result->MergeAndTypeDPS(v1);
	  }
	  Expr fe = forms.getlo();
	  Text name;
	  if (fe->kind == NameEK)
	    name = ((Name)fe)->id;
	  else if (fe->kind == AssignEK)
	    name = ((AssignEC*)fe)->lhs->id;
	  else {
	    fe->EError(cio().start_err(), 
		       "The function has bad parameter list.");
	    cio().end_err();
	    return NEW(ErrorVC);
	  }
	  bool cacheit = true;
	  Name eDot = NEW_CONSTR(NameEC, (nameDot, exprs->loc));
	  Val vDot = eDot->Eval(RestrictContext(c, eDot->freeVars));
	  int index = 0;
	  AssocVC dotAssoc(nameDot, vDot),
	    v_Assoc(name, valUnbnd);
	  Context argsCon(&v_Assoc, &dotAssoc);
	  while (!work.Null()) {
	    Val elem = v2->Extend(work.Pop(), IntArc(index++), NormPK, false);
	    v_Assoc.val = elem;
	    // Apply the function:
	    thData->funcCallDepth++;
	    if (traceRes) {
	      *traceRes << "  " << thData->funcCallDepth << ". "
			<< fun->func->args->loc->file << ": "
			<< fun->func->name << "()";
	    }
	    elem = ApplicationFromCache(fun, argsCon, exprs->loc);
	    thData->funcCallDepth--;
	    cacheit = cacheit && elem->cacheit;
	    vals.Append1D(elem);
	  }
	  result = NEW_CONSTR(ListVC, (vals));
	  result->MergeAndLenDPS(v2);
	  result->cacheit = cacheit;
	  break;
	}
      case ModelVK:
	{
	  ModelVC *fun = (ModelVC*)v1;
	  bool cacheit = true;
	  int index = 0;
	  AssocVC dotAssoc(nameDot, valUnbnd);
	  Context argsCon(&dotAssoc);
	  while (!work.Null()) {
	    Val elem = v2->Extend(work.Pop(), IntArc(index++), NormPK, false);
	    dotAssoc.val = elem;
	    elem = ModelFromCache(fun, argsCon, exprs->loc);
	    cacheit = cacheit && elem->cacheit;
	    vals.Append1D(elem);
	  }
	  result = NEW_CONSTR(ListVC, (vals));
	  result->MergeAndLenDPS(v2);
	  result->cacheit = cacheit;
	  break;
	}
      default:
	PrimError("The first argument of _map must be either a function or a model",
		  v1, exprs->loc);
	result = NEW(ErrorVC);
	return result->MergeAndTypeDPS(v1);
      }
      return result;
    }
  default:
    PrimError("The second argument of _map must be either a binding or a list",
	      v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
}

class EvalWorker {
public:
  EvalWorker(Val fun, const Context& args, SrcLoc *loc,
	     int depth, int stackSize, ThreadData* parent)
    : id(-1), fun(fun), argsCon(args), loc(loc), result(NULL),
      funcCallDepth(depth), parentCallStackSize(stackSize),
      parent(parent), done(false), isWaiting(false) {
    if (numThreads < threadAllowed) {
      numThreads++;
      if (ThreadIds.size() == 0) {
	this->id = ThreadIdMax++;
      }
      else {
	this->id = ThreadIds.remhi();
      }
    }
  }
  int id;
  Val fun;
  Context argsCon;
  SrcLoc *loc;
  Val result;
  CacheEntry::IndicesApp orphanCIs;
  OBufStream traceRes;
  int funcCallDepth;
  int parentCallStackSize;
  ThreadData *parent;
  bool done;
  Basics::cond doneCond;
  bool isWaiting;
};

typedef Sequence<EvalWorker*> Workers;

static void* DoWork(void *arg) throw () {
  EvalWorker *worker = (EvalWorker*)arg;
  ThreadData* thdata;
  if (worker->id < 0) {
    thdata = ThreadDataGet();
  }
  else {
    thdata = ThreadDataCreate(worker->id, &worker->orphanCIs);
    thdata->traceRes = &worker->traceRes;
    thdata->funcCallDepth = worker->funcCallDepth;
    thdata->parentCallStackSize = worker->parentCallStackSize;
    thdata->parent = worker->parent;
  }
  ostream *traceRes = thdata->traceRes;
  
  // Do the real work.
  parMu.lock();
  bool failing = parMapFailing;
  parMu.unlock();
  if (!failing) {
    try {
      if (worker->fun->vKind == ClosureVK) {
	ClosureVC *fun = (ClosureVC*)worker->fun;
	thdata->funcCallDepth++;
	if (traceRes) {
	  *traceRes << "  " << thdata->funcCallDepth << ". "
		    << fun->func->args->loc->file << ": "
		    << fun->func->name << "()";
	}
	worker->result =
	  ApplicationFromCache(fun, worker->argsCon, worker->loc);
	thdata->funcCallDepth--;
      }
      else {
	assert(worker->fun->vKind == ModelVK);
	worker->result = 
	  ModelFromCache((ModelVC*)worker->fun, worker->argsCon, worker->loc);
      }
    } catch (SRPC::failure f) {
      parMu.lock();
      if (!parMapFailing) {
	ostream& err_stream = cio().start_err();
	Error(err_stream, Text("_par_map: SRPC failure (") + 
	      IntToText(f.r) + "): " + f.msg +
	      "The evaluation will exit when _par_map finishes.\n",
	      worker->loc);
	PrintErrorStack(err_stream);
	cio().end_err();
      }
      parMu.unlock();
    } catch (Evaluator::failure f) {
      parMu.lock();
      if (!parMapFailing) {
	ostream& err_stream = cio().start_err();
	Error(err_stream, Text("_par_map: Vesta evaluation failure; ") +
	      "The evaluation will exit when _par_map finishes.\n",
	      worker->loc);
	PrintErrorStack(err_stream);    
	cio().end_err();
      }
      parMu.unlock();
    } catch (const char* report) {
      // Handle parsing error exception.
      parMu.lock();
      if (!parMapFailing) {
	ostream& err_stream = cio().start_err();	
	ErrorDetail(err_stream, Text(report) +
		    " The evaluation will exit when _par_map finishes.\n");
	PrintErrorStack(err_stream);    
	cio().end_err();
      }
      parMu.unlock();
    }
  }

  // Finish up
  parMu.lock();
  worker->done = true;
  if (worker->result == NULL) parMapFailing = true;
  frontierMu.lock();
  if(worker->orphanCIs.len > 0)
    {
      // We must have a parent thread and it must not be this thread
      assert(worker->parent != 0);
      assert(worker->parent != thdata);
      // This thread is teminating.  Make sure that we renew the
      // leases on these CIs until the _par_map that invoked us
      // reclaims these CIs.
      for (int i = 0; i < worker->orphanCIs.len; i++) {
	worker->parent->unclaimed_child_orphanCIs
	  .insert(worker->orphanCIs.index[i]);
      }
    }
  frontierMu.unlock();
  if (worker->isWaiting) {
    numThreads--;
    ThreadIds.addhi(worker->id);
    worker->doneCond.signal();
  }
  else if (worker->id >= 0) {
    numThreads--;
    ThreadIds.addhi(worker->id);    
    WorkCond.signal();    
  }
  parMu.unlock();  
  return (void *)NULL;  // make compiler happy
}

static long WorkerStackSize = (1<<20);

static EvalWorker* StartWorker(Val val, const Context& argsCon, SrcLoc *loc,
			       ThreadData *thdata, Workers *workers) {
  EvalWorker *worker =
    NEW_CONSTR(EvalWorker, (val, argsCon, loc, thdata->funcCallDepth,
		   (recordCallStack ? thdata->callStack->size() : 0), thdata));
  workers->addhi(worker);
  if (worker->id == -1) return worker;
  Basics::thread *th = NEW(Basics::thread);
  th->fork_and_detach(DoWork, (void*)worker, WorkerStackSize);
  return (EvalWorker *)NULL;   // make compiler happy
}

static Val FinishWorker(EvalWorker *worker, ThreadData *thdata) {
  if (worker->id != -1) {
    parMu.lock();
    while (!worker->done) {
      threadAllowed++;
      WorkCond.signal();
      worker->isWaiting = true;
      worker->doneCond.wait(parMu);
      threadAllowed--;
    }
    parMu.unlock();
    frontierMu.lock();
    // Propagate the orphaned CIs from the worker to our set
    for (int i = 0; i < worker->orphanCIs.len; i++) {
      thdata->orphanCIs->Append(worker->orphanCIs.index[i]);
      // Remove this orphan CI from the unclaimed set
      thdata->unclaimed_child_orphanCIs.erase(worker->orphanCIs.index[i]);
    }
    frontierMu.unlock();
    if (recordTrace) {	
      *thdata->traceRes << worker->traceRes.str();
    }
  }
  Val elem = worker->result;
  return elem;
}

class ParWork {
public:
  ParWork(Val func, Val arg, const Text& name1, const Text& name2,
	  Val vDot, Context bwork, Vals lwork, ThreadData *thdata, SrcLoc *loc)
    : func(func), arg(arg), formal1(name1), formal2(name2), vDot(vDot),
      bwork(bwork), lwork(lwork), index(0), workers(10),
      thdata(thdata), loc(loc), prev(NULL), next(NULL)
  { /*SKIP*/ }
  
  Val func;
  Val arg;
  const Text formal1;
  const Text formal2;
  Val vDot;
  Context bwork;
  Vals lwork;
  int index;
  Workers workers;
  ThreadData* thdata;
  SrcLoc* loc;
  ParWork *prev;
  ParWork *next;
};

static ParWork *AvailWorks;

static void AddParWork(ParWork *parWork) {
  parWork->prev == NULL;
  if (AvailWorks) AvailWorks->prev = parWork;
  parWork->next = AvailWorks;
  AvailWorks = parWork;
}

static void DeleteParWork(ParWork *parWork) {
  if (parWork->prev) {
    parWork->prev->next = parWork->next;
  }
  else {
    AvailWorks = parWork->next;
  }
  if (parWork->next) {
    parWork->next->prev = parWork->prev;
  }
}

static void* DoAvailWork(void *arg) throw () {
  parMu.lock();
  while (true) {
    waiting:
    WorkCond.wait(parMu);
    while (numThreads >= threadAllowed) {
      WorkCond.wait(parMu);
    }
    ParWork *parWork = AvailWorks;
    while (parWork) {
      if (!parWork->bwork.Null()) {
	while (!parWork->bwork.Null()) {
	  Assoc a = parWork->bwork.Pop();
	  Context argsCon(NEW_CONSTR(AssocVC, (parWork->formal1, 
					       NEW_CONSTR(TextVC, (a->name)))));
	  Val elem = parWork->arg->Extend(a->val, a->name, NormPK, false);
	  argsCon.Push((NEW_CONSTR(AssocVC, (parWork->formal2, elem))));
	  if (parWork->vDot != NULL) {
	    argsCon.Push(NEW_CONSTR(AssocVC, (nameDot, parWork->vDot)));
	  }
	  
	  // Start a worker to do the work.
	  StartWorker(parWork->func, argsCon, parWork->loc, 
		      parWork->thdata, &parWork->workers);
	  if (numThreads >= threadAllowed) goto waiting;	  
	}
      }
      else {
	while (!parWork->lwork.Null()) {
	  Val elem = parWork->arg->Extend(parWork->lwork.Pop(), 
					  IntArc((parWork->index)++),
					  NormPK, false);
	  Context argsCon(NEW_CONSTR(AssocVC, (parWork->formal1, elem)));
	  if (parWork->vDot != NULL) {
	    argsCon.Push(NEW_CONSTR(AssocVC, (nameDot, parWork->vDot)));
	  }
	  // Start a worker to do the work.
	  StartWorker(parWork->func, argsCon, parWork->loc, 
		      parWork->thdata, &parWork->workers);
	  if (numThreads >= threadAllowed) goto waiting;
	}
      }
      parWork = parWork->next;
    }
  }
  // parMu.unlock();
}

Val ParMap(ArgList exprs, const Context& c) {
  // Parallel version of Map.
  if (maxThreads < 2) {
    // use Map if there is one thread allowed.
    return Map(exprs, c);
  }
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 2) {
    PrimError("_par_map takes two arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  ThreadData *thdata = ThreadDataGet();

  Val v1 = args.First(), v2 = args.Second(), result;
  ParWork *parWork = NULL;
  bool parWorkAdded = false;
  switch (v2->vKind) {
  case BindingVK:
    {
      Context elems;
      if (v1->vKind != ClosureVK) {
	PrimError("The first argument of _par_map must be a function",
		  v1, exprs->loc);
	result = NEW(ErrorVC);
	return result->MergeAndTypeDPS(v1);
      }
      ClosureVC *fun = (ClosureVC*)v1;
      Exprs forms = fun->func->args->elems;
      if (forms.size() != 2) {
	PrimError("The first argument of _par_map must be a function of two arguments",
		  v1, exprs->loc);
	result = NEW(ErrorVC);
	return result->MergeAndTypeDPS(v1);
      }
      Text name1, name2;
      Expr fe = forms.get(0);
      if (fe->kind == NameEK)
	name1 = ((Name)fe)->id;
      else if (fe->kind == AssignEK)
	name1 = ((AssignEC*)fe)->lhs->id;
      else {
	fe->EError(cio().start_err(), "The function has bad parameter list.");
	cio().end_err();
	return NEW(ErrorVC);
      }
      fe = forms.get(1);
      if (fe->kind == NameEK)
	name2 = ((Name)fe)->id;
      else if (fe->kind == AssignEK)
	name2 = ((AssignEC*)fe)->lhs->id;
      else {
	fe->EError(cio().start_err(), "The function has bad parameter list.");
	cio().end_err();
	return NEW(ErrorVC);
      }
      Name eDot = NEW_CONSTR(NameEC, (nameDot, exprs->loc));
      Val vDot = eDot->Eval(RestrictContext(c, eDot->freeVars));      
      Vals none;
      parWork = NEW_CONSTR(ParWork, (v1, v2, name1, name2, vDot, 
				     ((BindingVC*)v2)->elems, none,
				     thdata, exprs->loc));
      // Do the work.
      while (true) {
	parMu.lock();
	if(parWork->bwork.Null()) {
	  parMu.unlock();
	  break;
	}
	Assoc a = parWork->bwork.Pop();
	Context argsCon(NEW_CONSTR(AssocVC, 
				   (name1, NEW_CONSTR(TextVC, (a->name)))));
	Val elem = v2->Extend(a->val, a->name, NormPK, false);
	argsCon.Push(NEW_CONSTR(AssocVC, (name2, elem)));
	argsCon.Push(NEW_CONSTR(AssocVC, (nameDot, vDot)));	

	EvalWorker *worker = StartWorker(v1, argsCon, exprs->loc, 
					 thdata, &parWork->workers);
	if (worker != NULL && !parWorkAdded) {
	  AddParWork(parWork);
	  parWorkAdded = true;
	}
	parMu.unlock();
	if (worker != NULL) DoWork((void*)worker);
      }
      // Postprocess when the work finishes.
      bool cacheit = true;
      DPaths *ps = NEW(DPaths);
      for (int i = 0; i < parWork->workers.size(); i++) {
	Val elem = FinishWorker(parWork->workers.get(i), thdata);
	if(elem == NULL) {
	  if(!parMapFailing) 
	    parMapFailing = true;
	  continue;
	}
	if(elem->vKind != BindingVK) {
	  Error(cio().start_err(), 
		"The function must return a binding.\n", exprs->loc);
	  cio().end_err();
	  if(!parMapFailing) 
	    parMapFailing = true;
	  continue;
	}
	// Add the new elem into elems that is used to collect
	// the results.
	Context work = ((BindingVC*)elem)->elems;
	ps = ps->Union(elem->dps);
	while (!work.Null()) {
	  Assoc as = work.Pop();
	  if (FindInContext(as->name, elems) != nullAssoc) {
	    Error(cio().start_err(), 
		  "Field name conflicts in binding.\n", exprs->loc);
	    cio().end_err();
	    if(!parMapFailing) 
	      parMapFailing = true;
	    break;
	  }
	  Val asVal = elem->Extend(as->val, as->name, NormPK, false);
	  elems.Append1D(NEW_CONSTR(AssocVC, (as->name, asVal)));
	}
	cacheit = cacheit && elem->cacheit;
      }
      result = NEW_CONSTR(BindingVC, (elems));
      result->dps = ps;
      result->MergeAndLenDPS(v2);
      result->cacheit = cacheit;
      // Delete parWork if it is added to AvailWorks:
      if(parWorkAdded) {
	parMu.lock();
	DeleteParWork(parWork);
	parMu.unlock();
      }
      break;
    }
  case ListVK:
    {
      Vals vals;
      switch (v1->vKind) {
      case ClosureVK:
	{
	  ClosureVC *fun = (ClosureVC*)v1;
	  Exprs forms = fun->func->args->elems;
	  if (forms.size() != 1) {
	    PrimError("The first argument of _par_map must be a function of one argument",
		      v1, exprs->loc);
	    result = NEW(ErrorVC);
	    return result->MergeAndTypeDPS(v1);
	  }
	  Expr fe = forms.getlo();
	  Text name;
	  if (fe->kind == NameEK)
	    name = ((Name)fe)->id;
	  else if (fe->kind == AssignEK)
	    name = ((AssignEC*)fe)->lhs->id;
	  else {
	    fe->EError(cio().start_err(), 
		       "The function has bad parameter list.");
	    cio().end_err();
	    return NEW(ErrorVC);
	  }
	  Name eDot = NEW_CONSTR(NameEC, (nameDot, exprs->loc));
	  Val vDot = eDot->Eval(RestrictContext(c, eDot->freeVars));	  
	  Context none;
	  parWork = NEW_CONSTR(ParWork, (v1, v2, name, Text(""), vDot, none, 
					 ((ListVC*)v2)->elems, thdata, exprs->loc));
	  // Do the work.
	  while (true) {
	    parMu.lock();
	    if (parWork->lwork.Null()) {
	      parMu.unlock();
	      break;
	    }
	    Val elem = v2->Extend(parWork->lwork.Pop(), IntArc(parWork->index++), 
				  NormPK, false);
	    Context argsCon(NEW_CONSTR(AssocVC, (name, elem)));
	    argsCon.Push(NEW_CONSTR(AssocVC, (nameDot, vDot)));
	    
	    EvalWorker *worker = StartWorker(v1, argsCon, exprs->loc, 
					     thdata, &parWork->workers);
	    if (worker != NULL && !parWorkAdded) {
	      AddParWork(parWork);
	      parWorkAdded = true;
	    }
	    parMu.unlock();
	    if (worker != NULL) DoWork((void*)worker);
	  }
	  bool cacheit = true;
	  for (int i = 0; i < parWork->workers.size(); i++) {
	    Val elem = FinishWorker(parWork->workers.get(i), thdata);
	    if(elem == NULL) {
	      if(!parMapFailing) 
		parMapFailing = true;
	      continue;
	    }
	    cacheit = cacheit && elem->cacheit;
	    vals.Append1D(elem);
	  }
	  result = NEW_CONSTR(ListVC, (vals));
	  result->MergeAndLenDPS(v2);
	  result->cacheit = cacheit;
	  break;
	}
      case ModelVK:
	{
	  ModelVC *fun = (ModelVC*)v1;
	  // Do the work.
	  Context none;
	  parWork = NEW_CONSTR(ParWork, (v1, v2, nameDot, Text(""), NULL, none, 
					 ((ListVC*)v2)->elems, thdata, exprs->loc));
	  while (true) {
	    parMu.lock();
	    if (parWork->lwork.Null()) {
	      parMu.unlock();
	      break;
	    }
	    Val elem = v2->Extend(parWork->lwork.Pop(), IntArc(parWork->index++), 
				  NormPK, false);
	    Context argsCon(NEW_CONSTR(AssocVC, (nameDot, elem)));
	    EvalWorker *worker = StartWorker(v1, argsCon, exprs->loc, 
					     thdata, &parWork->workers);
	    if (worker != NULL && !parWorkAdded) {
	      AddParWork(parWork);
	      parWorkAdded = true;
	    }
	    parMu.unlock();
	    if (worker != NULL) DoWork((void*)worker);
	  }
	  bool cacheit = true;
	  for (int i = 0; i < parWork->workers.size(); i++) {
	    Val elem = FinishWorker(parWork->workers.get(i), thdata);
	    if(elem == NULL) {
	      if(!parMapFailing) 
		parMapFailing = true;
	      continue;
	    }
	    cacheit = cacheit && elem->cacheit;
	    vals.Append1D(elem);
	  }
	  result = NEW_CONSTR(ListVC, (vals));
	  result->MergeAndLenDPS(v2);
	  result->cacheit = cacheit;
	  break;
	}
      default:
	PrimError("The first argument of _par_map must be either a function or a model",
		  v1, exprs->loc);
	result = NEW(ErrorVC);
	return result->MergeAndTypeDPS(v1);
      }
      // Delete parWork if it is added to AvailWorks:
      if (parWorkAdded) {
	parMu.lock();
	DeleteParWork(parWork);
	parMu.unlock();
      }
      break;
    }
  default:
    PrimError("The second argument of _par_map must be either a binding or a list",
	      v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  
  if(parMapFailing) {
    
    return NEW(ErrorVC);
  }
  return result;
}

Val Max(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 2) {
    PrimError("_max takes two arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v1 = args.First(), v2 = args.Second(), result;
  if (v1->vKind != IntegerVK) {
    PrimError("First argument of _max must be integer", v1, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  if (v2->vKind != IntegerVK) {
    PrimError("Second argument of _max must be integer", v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  if (((IntegerVC*)v1)->num > ((IntegerVC*)v2)->num)
    result = v1->Merge(v2);
  else 
    result = v2->Merge(v1);
  result->cacheit = v1->cacheit && v2->cacheit;
  return result;
}

Val Min(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 2) {
    PrimError("_min takes two arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v1 = args.First(), v2 = args.Second(), result;
  if (v1->vKind != IntegerVK) {
    PrimError("First argument of _min must be integer", v1, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  if (v2->vKind != IntegerVK) {
    PrimError("Second argument of _min must be integer", v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  if (((IntegerVC*)v1)->num > ((IntegerVC*)v2)->num) 
    result = v2->Merge(v1);
  else
    result = v1->Merge(v2);
  result->cacheit = v1->cacheit && v2->cacheit;
  return result;
}

Val Mod(ArgList exprs, const Context& ctxt) {
  Vals args = EvalArgs(exprs, ctxt);
  if (args.Length() != 2) {
    PrimError("_mod takes two arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v1 = args.First(), v2 = args.Second(), result;
  if (v1->vKind != IntegerVK) {
    PrimError("First argument of _mod must be integer", v1, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  if (v2->vKind != IntegerVK) {
    PrimError("Second argument of _mod must be integer", v2, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  Basics::int32 a = ((IntegerVC*)v1)->num;
  Basics::int32 b = ((IntegerVC*)v2)->num;
  if (b == 0) {
    PrimError("Attempt to divide by 0 with _mod", args, exprs->loc);
    result = NEW(ErrorVC);
    return result->Merge(v2);
  }
  // This code does Modula-3 style MOD
  Basics::int32 c;
  if ((a == 0) && (b != 0)) {  c = 0;
  } else if (a > 0) {  c = (b >= 0) ? a % b : b + 1 + (a-1) % (-b);
  } else /*a < 0*/ {  c = (b >= 0) ? b - 1 - (-1-a) % (b) : - ((-a) % (-b));
  }
  result = NEW_CONSTR(IntegerVC, (c));
  result->cacheit = v1->cacheit && v2->cacheit;
  return result->Merge(v1)->Merge(v2);
}

Val GetName(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 1) {
    PrimError("_n takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v = args.First(), result;
  if (v->vKind != BindingVK) {
    PrimError("_n's argument must be a binding", v, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v);
  }
  int len = ((BindingVC*)v)->elems.Length();
  if (len != 1) {
    PrimError("_n's argument must be a singleton binding", v, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndLenDPS(v);
  }
  Text name(((BindingVC*)v)->elems.First()->name);
  result = NEW_CONSTR(TextVC, (name));
  result->cacheit = v->cacheit;
  return result->MergeAndLenDPS(v);
}

Val GetValue(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 1) {
    PrimError("_v takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val v = args.First(), result;
  if (v->vKind != BindingVK) {
    PrimError("_v's argument must be a binding", v, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v);
  }
  if (((BindingVC*)v)->elems.Length() != 1) {
    PrimError("_v's argument must be a binding", v, exprs->loc);
    result = NEW(ErrorVC);
    return result->MergeAndLenDPS(v);
  }
  Text name;
  result = ((BindingVC*)v)->GetElem(0, name);
  return result;
}

Val Sub(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  Basics::int32 start = 0;
  Basics::int32 len = Text::MaxInt;
  Val v1, v2 = valZero, v3 = valZero, result;
  int argsLen = args.Length();
  switch (argsLen) {
  case 3:
    v3 = args.Third();
    if (v3->vKind != IntegerVK) {
      PrimError("Third argument of _sub must be an integer", v3, exprs->loc);
      result = NEW(ErrorVC);
      return result->MergeAndTypeDPS(v3);
    }
    len = ((IntegerVC*)v3)->num;
    if (len < 0) len = 0;
    if (len > Text::MaxInt) {
      PrimError("(impl) _sub integer argument too big", v3, exprs->loc);
      result = NEW(ErrorVC);
      return result->Merge(v3);
    }
    // fall through
  case 2:
    v2 = args.Second();
    if (v2->vKind != IntegerVK) {
      PrimError("Second argument of _sub must be an integer", v2, exprs->loc);
      result = NEW(ErrorVC);
      return result->MergeAndTypeDPS(v2);
    }
    start = ((IntegerVC*)v2)->num;
    if (start < 0) start = 0;
    if (start > Text::MaxInt) {
      PrimError("(impl) _sub integer argument too big", v2, exprs->loc);
      result = NEW(ErrorVC);
      return result->Merge(v2);
    }
    // fall through
  case 1:
    v1 = args.First();
    if (v1->vKind == TextVK) {
      Text txt(((TextVC*)v1)->NDS());
      bool toFile = (!((TextVC*)v1)->HasTxt()) && (len > 128);
      if (toFile) {
	FP::Tag fp(v1->FingerPrint());
	if (argsLen > 1)
	  fp.Extend(v2->FingerPrint());
	if (argsLen > 2)
	  fp.Extend(v3->FingerPrint());
	result = NEW_CONSTR(TextVC, (noName, txt.Sub(start, len), '-', fp));
      }
      else {
	result = NEW_CONSTR(TextVC, (txt.Sub(start, len)));
      }
      result->Merge(v1)->Merge(v2)->Merge(v3);
    }
    else if (v1->vKind == BindingVK) {
      Context work = ((BindingVC*)v1)->elems;
      while (!work.Null() && (start-- > 0)) work.Pop();
      Context rElems;
      while (!work.Null() && (len-- > 0)) {
	Assoc a = work.Pop();
	Val v11 = v1->Extend(a->val, a->name, NormPK, false);
	rElems.Append1D(NEW_CONSTR(AssocVC, (a->name, v11)));
      }
      result = NEW_CONSTR(BindingVC, (rElems));
      // Add the length dependency
      if (v1->path == NULL)
	((BindingVC*)result)->lenDps = ((BindingVC*)v1)->lenDps;
      else
	((BindingVC*)result)->AddToLenDPS(*v1->path, v1);
      result->MergeAndLenDPS(v1);
      result->Merge(v2)->Merge(v3);
    }
    else if (v1->vKind == ListVK) {
      Vals work = ((ListVC*)v1)->elems;
      Basics::int32 cnt = start;
      while (!work.Null() && (start-- > 0)) work.Pop();
      Vals rElems;
      while (!work.Null() && (len-- > 0)) {
	Val v11 = v1->Extend(work.Pop(), IntArc(cnt++), NormPK, false);
	rElems.Append1D(v11);
      }
      result = NEW_CONSTR(ListVC, (rElems));
      // Add the length dependency
      if (v1->path == NULL)
	((ListVC*)result)->lenDps = ((ListVC*)v1)->lenDps;
      else
	((ListVC*)result)->AddToLenDPS(*v1->path, v1);
      result->MergeAndLenDPS(v1);
      result->Merge(v2)->Merge(v3);
    }
    else {
      PrimError("First argument of _sub must be a text, binding, or list", v1, exprs->loc);
      result = NEW(ErrorVC);
      return result->MergeAndTypeDPS(v1);
    }
    break;
  default:
    PrimError("_sub takes one to three arguments", args, exprs->loc);
    return NEW(ErrorVC);
  }
  result->cacheit = v1->cacheit && v2->cacheit && v3->cacheit;
  return result;
}

// Kludge for debugging help.
Val Print(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  Val v1, v2, v3;
  bool verbose = false;

  switch (args.Length()) {
  case 3:
    v3 = args.Third();
    if (v3->vKind != BooleanVK) {
      PrimError("Third argument of _print must be a boolean.", v3, exprs->loc);
      return args.First();
    }
    verbose = ((BooleanVC*)v3)->b;
    // fall through ...
  case 2: {
    v1 = args.First();
    v2 = args.Second();
    if (v2->vKind != IntegerVK) {
      PrimError("Second argument of _print must be an integer.", v2, exprs->loc);
      return v1;
    }
    ostream& out_stream = cio().start_out();
    switch (((IntegerVC*)v2)->num) {
    case 0:
      v1->PrintD(out_stream, verbose);
      out_stream << endl;
      break;
    case 1:
      out_stream << "The value is: ";
      v1->PrintD(out_stream, verbose);
      out_stream << endl << "And the dependency is:" << endl;
      PrintDpnd(out_stream, v1);
      out_stream << endl;
      break;
    default:
      out_stream << "The value is: ";
      v1->PrintD(out_stream, verbose);
      out_stream << endl << "And the dependency is:" << endl;
      PrintAllDpnd(out_stream, v1);
      out_stream << endl;
      break;
    }
    cio().end_out();
    break;
  }
  case 1: {
    ostream& out_stream = cio().start_out();
    v1 = args.First();
    v1->PrintD(out_stream);
    out_stream << endl;
    cio().end_out();
    break;
  }
  default:
    PrimError("_print takes one or two or three arguments.", args, exprs->loc);
    return NEW(ErrorVC);
  }
  return v1;
}

Val ModelName(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 1) {
    PrimError("_model_name takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val result;
  Val v = args.First();
  if (v->vKind != ModelVK) {
    PrimError("_model_name's argument must be a model", v, exprs->loc);
    return NEW(ErrorVC);
  }
  result = NEW_CONSTR(TextVC, (((ModelVC*)v)->content->name));
  result->cacheit = v->cacheit;
  return result->Merge(v);
}

Val GetFP(ArgList exprs, const Context& c) {
  Vals args = EvalArgs(exprs, c);
  if (args.Length() != 1) {
    PrimError("_fingerprint takes one argument", args, exprs->loc);
    return NEW(ErrorVC);
  }
  Val result;
  Val v = args.First();

  int i;
  unsigned char fpbytes[FP::ByteCnt];
  v->FingerPrint().ToBytes(fpbytes);
  char buf[FP::ByteCnt*2+1];
  OBufStream fp(buf, sizeof(buf));
  fp << hex;
  for (i=0; i<FP::ByteCnt; i++) {
    fp << setw(2) << setfill('0') << (int) fpbytes[i];
  }
  result = NEW_CONSTR(TextVC, (Text(fp.str())));
  result->cacheit = v->cacheit;
  return result->Merge(v);
}

//// Primitive operators:
static Table<Text,PrimOp>::Default PrimitiveOps(64);

void AddPrimitiveOp(const Text& name, PrimOp op) {
  if (PrimitiveOps.Put(name, op)) {
    Error(cio().start_err(), 
	  "(impl) Name conflict in PrimitiveOps! `" + name + "'.\n");
    cio().end_err();
  }
}

PrimOp LookupOp(const Text& opid) {
  PrimOp result;
  return PrimitiveOps.Get(opid, result) ? result : NULL;
}

static Table<Text,PrimUnOp>::Default PrimitiveUnOps(8);

void AddPrimitiveUnOp(const Text& name, PrimUnOp op) {
  if (PrimitiveUnOps.Put(name, op)) {
    Error(cio().start_err(), 
	  "(impl) Name conflict in PrimitiveUnOps! `" + name + "'.\n");
    cio().end_err();
  }
}

PrimUnOp LookupUnOp(const Text& opid) {
  PrimUnOp result;
  return PrimitiveUnOps.Get(opid, result) ? result : NULL;
}

Val Plus(Expr e1, Expr e2, const Context& c) {
  Val v1 = e1->Eval(c), v2 = e2->Eval(c);
  Val result;

  if (v1->vKind != v2->vKind) {
    PrimError("`+' not implemented for these args", v1, v2, e1->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1)->MergeAndTypeDPS(v2);
  }
  switch (v1->vKind) {
  case IntegerVK:
    {
      Basics::int32 n1 = ((IntegerVC*)v1)->num;
      Basics::int32 n2 = ((IntegerVC*)v2)->num;
      Basics::int32 res = n1 + n2;
      if ((n1 < 0) == (n2 < 0) && (n2 < 0) != (res < 0)) {
	PrimError("Overflow on `+'", v1, v2, e1->loc);
	result = NEW(ErrorVC);
	return result->Merge(v1)->Merge(v2);
      }
      result = NEW_CONSTR(IntegerVC, (res));
      result->cacheit = v1->cacheit && v2->cacheit;
      return result->Merge(v1)->Merge(v2);
    }
  case ListVK:
    if (IsEmptyList(v2)) {
      v1->cacheit = v1->cacheit && v2->cacheit;
      return v1->MergeAndLenDPS(v2);
    }
    if (IsEmptyList(v1)) {
      v2->cacheit = v2->cacheit && v1->cacheit;
      return v2->MergeAndLenDPS(v1);
    }
    return ListAppend((ListVC*)v1, (ListVC*)v2);
  case BindingVK:
    if (IsEmptyBinding(v2)) {
      v1->cacheit = v1->cacheit && v2->cacheit;
      return v1->MergeAndLenDPS(v2);
    }
    if (IsEmptyBinding(v1)) {
      v2->cacheit = v1->cacheit && v2->cacheit;
      return v2->MergeAndLenDPS(v1);
    }
    result = NEW_CONSTR(BindingVC, ((BindingVC*)v1, (BindingVC*)v2, false));
    return result->MergeAndTypeDPS(v1)->MergeAndTypeDPS(v2);
  case TextVK:
    {
      bool toFile = ((TextVC*)v1)->HasSid() || ((TextVC*)v2)->HasSid();
      if (toFile) {
	FP::Tag fp(v1->FingerPrint());
	fp.Extend(v2->FingerPrint());
	result = NEW_CONSTR(TextVC, (noName, ((TextVC*)v1)->NDS() + 
				     ((TextVC*)v2)->NDS(), '+', fp));
      }
      else {
	result = NEW_CONSTR(TextVC, (((TextVC*)v1)->NDS() + ((TextVC*)v2)->NDS()));
      }
      result->cacheit = v1->cacheit && v2->cacheit;
      return result->Merge(v1)->Merge(v2);
    }
  default:
    PrimError("`+' not implemented for these args", v1, v2, e1->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1)->MergeAndTypeDPS(v2);
  }
}

Val PlusPlus(Expr e1, Expr e2, const Context& c) {
  Val v1 = e1->Eval(c), v2 = e2->Eval(c);
  Val result;

  if (v1->vKind != BindingVK) {
    PrimError("`++' not implemented for non-binding", v1, e1->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  if (v2->vKind != BindingVK) {
    PrimError("`++' not implemented for non-binding", v2, e2->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  if (IsEmptyBinding(v2)) {
    v1->cacheit = v1->cacheit && v2->cacheit;
    return v1->MergeAndLenDPS(v2);
  }
  if (IsEmptyBinding(v1)) {
    v2->cacheit = v2->cacheit && v1->cacheit;
    return v2->MergeAndLenDPS(v1);
  }
  result = NEW_CONSTR(BindingVC, ((BindingVC*)v1, (BindingVC*)v2, true));
  return result->MergeAndTypeDPS(v1)->MergeAndTypeDPS(v2);
}

Val EqualVal(Val v1, Val v2, SrcLoc *loc) {
  Val test, result;
  bool b;

  if (v1->vKind == v2->vKind) {
    switch (v1->vKind) {
    case BooleanVK:
      b = (((BooleanVC*)v1)->b == ((BooleanVC*)v2)->b);
      result = NEW_CONSTR(BooleanVC, (b));
      result->cacheit = v1->cacheit && v2->cacheit;
      return result->Merge(v1)->Merge(v2);
    case IntegerVK:
      b = (((IntegerVC*)v1)->num == ((IntegerVC*)v2)->num);
      result = NEW_CONSTR(BooleanVC, (b));
      result->cacheit = v1->cacheit && v2->cacheit;
      return result->Merge(v1)->Merge(v2);
    case TextVK:
      b = (((TextVC*)v1)->NDS() == ((TextVC*)v2)->NDS());
      result = NEW_CONSTR(BooleanVC, (b));
      result->cacheit = v1->cacheit && v2->cacheit;
      return result->Merge(v1)->Merge(v2);
    case ListVK:
      {
	if (IsEmptyList(v1) && IsEmptyList(v2)) {
	  result = NEW_CONSTR(BooleanVC, (true));
	  result->cacheit = v1->cacheit && v2->cacheit;
	  return result->MergeAndLenDPS(v1)->MergeAndLenDPS(v2);
	}
	Vals l1 = ((ListVC*)v1)->elems,	l2 = ((ListVC*)v2)->elems;
	int len1 = l1.Length(), len2 = l2.Length();
	if (len1 != len2) {
	  result = NEW_CONSTR(BooleanVC, (false));
	  result->cacheit = v1->cacheit && v2->cacheit;
	  return result->MergeAndLenDPS(v1)->MergeAndLenDPS(v2);
	}
	Val vv1, vv2;
	result = NEW_CONSTR(BooleanVC, (true));
	unsigned int l_elem = 0;
	while (!l1.Null()) {
	  vv1 = v1->Extend(l1.Pop(), IntArc(l_elem), NormPK, false);
	  vv2 = v2->Extend(l2.Pop(), IntArc(l_elem), NormPK, false);
	  l_elem++;
	  test = EqualVal(vv1, vv2, loc);
	  if (IsValFalse(test)) {
	    test->cacheit = test->cacheit && v1->cacheit && v2->cacheit;
	    return test->MergeAndLenDPS(v1)->MergeAndLenDPS(v2);
	  }
	  result->Merge(test);
	  result->cacheit = result->cacheit && test->cacheit;
	}
	result->MergeAndLenDPS(v1)->MergeAndLenDPS(v2);
	result->cacheit = result->cacheit && v1->cacheit && v2->cacheit;
	return result;
      }
    case BindingVK:
      {
	if (IsEmptyBinding(v1) & IsEmptyBinding(v2)) {
	  result = NEW_CONSTR(BooleanVC, (true));
	  result->cacheit = v1->cacheit && v2->cacheit;
	  return result->MergeAndLenDPS(v1)->MergeAndLenDPS(v2);
	}
	Binding b1 = (BindingVC*)v1, b2 = (BindingVC*)v2;
	Context c1 = b1->elems, c2 = b2->elems;
	int len1 = c1.Length(), len2 = c2.Length();
	if (len1 != len2) {
	  result = NEW_CONSTR(BooleanVC, (false));
	  result->cacheit = b1->cacheit && b2->cacheit;
	  return result->MergeAndLenDPS(b1)->MergeAndLenDPS(b2);
	}
	Val vv1, vv2;
	Assoc a1, a2;
	result = NEW_CONSTR(BooleanVC, (true));
	unsigned int l_elem = 0;
	while (!c1.Null()) {
	  a1 = c1.Pop();
	  a2 = c2.Pop();
	  if (a1->name != a2->name) {
	    result = NEW_CONSTR(BooleanVC, (false));
	    result->cacheit = b1->cacheit && b2->cacheit;
	    return result->MergeAndLenDPS(b1)->MergeAndLenDPS(b2);
	  }
	  vv1 = v1->Extend(a1->val, IntArc(l_elem), NormPK, false);
	  vv2 = v2->Extend(a2->val, IntArc(l_elem), NormPK, false);
	  l_elem++;
	  test = EqualVal(vv1, vv2, loc);
	  if (IsValFalse(test)) {
	    test->cacheit = test->cacheit && b1->cacheit && b2->cacheit;
	    return test->MergeAndLenDPS(b1)->MergeAndLenDPS(b2);
	  }
	  result->Merge(test);
	  result->cacheit = result->cacheit && test->cacheit;
	}
	result->MergeAndLenDPS(v1)->MergeAndLenDPS(v2);
	result->cacheit = result->cacheit && b1->cacheit && b2->cacheit;
	return result;
      }
    case ErrorVK:
      result = NEW_CONSTR(BooleanVC, (true));
      result->cacheit = v1->cacheit && v2->cacheit;
      return result->Merge(v1)->Merge(v2);
    default:
      PrimError("(impl) `==' not implemented for these args", v1, v2, loc);
      result = NEW(ErrorVC);
      return result->Merge(v1)->Merge(v2);
    }
  }
  result = NEW_CONSTR(BooleanVC, (false));
  result->cacheit = v1->cacheit && v2->cacheit;
  return result->MergeAndTypeDPS(v1)->MergeAndTypeDPS(v2);
}
		  
Val Equal(Expr e1, Expr e2, const Context& c) {
  // cacheit flag of the result is set in EqualVal.
  return EqualVal(e1->Eval(c), e2->Eval(c), e1->loc);
}

Val Not(Expr e, const Context& c) {
  Val v1 = e->Eval(c);
  Val result;

  if (v1->vKind != BooleanVK) {
    PrimError("Argument of unary `!' must be a boolean.", v1, e->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  result = NEW_CONSTR(BooleanVC, (!(((BooleanVC*)v1)->b)));
  result->cacheit = v1->cacheit;
  return result->Merge(v1);
}
		  
Val Neg(Expr e, const Context& c) {
  Val v1 = e->Eval(c);
  Val result;

  if (v1->vKind != IntegerVK) {
    PrimError("Argument of unary `-' must be an integer.", v1, e->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  Basics::int32 n = ((IntegerVC*)v1)->num;
  if (n != 0 && n == -n) {
    PrimError("Overflow on unary `-'.", v1, e->loc);
    result = NEW(ErrorVC);
    return result->Merge(v1);
  }
  result = NEW_CONSTR(IntegerVC, (-n));
  result->cacheit = v1->cacheit;
  return result->Merge(v1);
}
		  
Val And(Expr e1, Expr e2, const Context& c) {
  Val v1 = e1->Eval(c);
  if (v1->vKind != BooleanVK) {
    PrimError("First argument of `&&' must be boolean.", v1, e1->loc);
    Val result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  if (IsValFalse(v1)) return v1;
  Val v2 = e2->Eval(c);
  if (v2->vKind != BooleanVK) {
    PrimError("Second Argument of `&&' must be boolean.", v2, e2->loc);
    Val result = NEW(ErrorVC);
    return result->Merge(v1)->MergeAndTypeDPS(v2);
  }
  v2->cacheit = v2->cacheit && v1->cacheit;
  return v2->Merge(v1);
}
		  
Val Or(Expr e1, Expr e2, const Context& c) {
  Val v1 = e1->Eval(c);
  if (v1->vKind != BooleanVK) {
    Error(cio().start_err(), "First argument of `||' must be boolean. \n");
    cio().end_err();
    Val result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  if (IsValTrue(v1)) return v1;
  Val v2 = e2->Eval(c);
  if (v2->vKind != BooleanVK) {
    Error(cio().start_err(), "Second argument of `||' must be boolean. \n");
    cio().end_err();
    Val result = NEW(ErrorVC);
    return result->Merge(v1)->MergeAndTypeDPS(v2);
  }
  v2->cacheit = v2->cacheit && v1->cacheit;
  return v2->Merge(v1);
}

Val Implies(Expr e1, Expr e2, const Context& c) {
  Val v1 = e1->Eval(c);
  if (v1->vKind != BooleanVK) {
    PrimError("First argument of `=>' must be boolean.", v1, e1->loc);
    Val result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  if (IsValFalse(v1)) {
    Val result = NEW_CONSTR(BooleanVC, (true));
    result->cacheit = v1->cacheit;
    return result->Merge(v1);
  }
  Val v2 = e2->Eval(c);
  if (v2->vKind != BooleanVK) {
    PrimError("Second argument of `=>' must be boolean.", v2, e2->loc);
    Val result = NEW(ErrorVC);
    return result->Merge(v1)->MergeAndTypeDPS(v2);
  }
  v2->cacheit = v2->cacheit && v1->cacheit;
  return v2->Merge(v1);
}

Val NotEq(Expr e1, Expr e2, const Context& c) {
  // cacheit flag of the result is set in EqualVal.
  Val test = EqualVal(e1->Eval(c), e2->Eval(c), e1->loc);

  if (test->vKind == ErrorVK) return test;
  BooleanVC *result = (BooleanVC*)test;
  result->b = IsValFalse(test);
  return result;
}

Val GreaterEq(Expr e1, Expr e2, const Context& c) {
  Val v1 = e1->Eval(c), v2 = e2->Eval(c);
  if (v1->vKind != IntegerVK) {
    PrimError("First argument of `>=' must be integer", v1, e1->loc);
    Val result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  if (v2->vKind != IntegerVK) {
    PrimError("Second argument of `>=' must be integer", v2, e2->loc);
    Val result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  bool b = (((IntegerVC*)v1)->num >= ((IntegerVC*)v2)->num);
  Val result = NEW_CONSTR(BooleanVC, (b));
  result->cacheit = v1->cacheit && v2->cacheit;
  return result->Merge(v1)->Merge(v2);
}

Val LessEq(Expr e1, Expr e2, const Context& c) {
  Val v1 = e1->Eval(c), v2 = e2->Eval(c);
  if (v1->vKind != IntegerVK) {
    PrimError("First argument of `<=' must be integer", v1, e1->loc);
    Val result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  if (v2->vKind != IntegerVK) {
    PrimError("Second argument of `<=' must be integer", v2, e2->loc);
    Val result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  bool b = (((IntegerVC*)v1)->num <= ((IntegerVC*)v2)->num);
  Val result = NEW_CONSTR(BooleanVC, (b));
  result->cacheit = v1->cacheit && v2->cacheit;
  return result->Merge(v1)->Merge(v2);
}

Val Greater(Expr e1, Expr e2, const Context& c) {
  Val v1 = e1->Eval(c), v2 = e2->Eval(c);
  if (v1->vKind != IntegerVK) {
    PrimError("First argument of `>' must be integer", v1, e1->loc);
    Val result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  if (v2->vKind != IntegerVK) {
    PrimError("Second argument of `>' must be integer", v2, e2->loc);
    Val result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  bool b = (((IntegerVC*)v1)->num > ((IntegerVC*)v2)->num);
  Val result = NEW_CONSTR(BooleanVC, (b));
  result->cacheit = v1->cacheit && v2->cacheit;
  return result->Merge(v1)->Merge(v2);
}

Val Less(Expr e1, Expr e2, const Context& c) {
  Val v1 = e1->Eval(c), v2 = e2->Eval(c);
  Val result;

  if (v1->vKind != IntegerVK) {
    PrimError("First argument of `<' must be integer", v1, e1->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  if (v2->vKind != IntegerVK) {
    PrimError("Second argument of `<' must be integer", v2, e2->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  bool b = (((IntegerVC*)v1)->num < ((IntegerVC*)v2)->num);
  result = NEW_CONSTR(BooleanVC, (b));
  result->cacheit = v1->cacheit && v2->cacheit;
  return result->Merge(v1)->Merge(v2);
}

Val Star(Expr e1, Expr e2, const Context& c) {
  Val v1 = e1->Eval(c), v2 = e2->Eval(c);
  Val result;

  if (v1->vKind != IntegerVK) {
    PrimError("First argument of `*' must be integer", v1, e1->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v1);
  }
  if (v2->vKind != IntegerVK) {
    PrimError("Second argument of `*' must be integer", v2, e2->loc);
    result = NEW(ErrorVC);
    return result->MergeAndTypeDPS(v2);
  }
  Basics::int32 n1 = ((IntegerVC*)v1)->num;
  Basics::int32 n2 = ((IntegerVC*)v2)->num;
  Basics::int32 res = n1 * n2;

  // Overflow check.  This is cheating, but it's probably more
  // efficient that anything else we could write in C++.
  Basics::int64 l_overflow_check = n1;
  l_overflow_check *= n2;
  if(l_overflow_check != res)
    {
      PrimError("Overflow on `*'", v1, v2, e1->loc);
      result = NEW(ErrorVC);
      return result->Merge(v1)->Merge(v2);
    }

  result = NEW_CONSTR(IntegerVC, (res));
  result->cacheit = v1->cacheit && v2->cacheit;
  return result->Merge(v1)->Merge(v2);
}

extern "C" void PrimInit_inner() {
  AddPrimitive("_append",        Append);
  AddPrimitive("_assert",        Assert);
  AddPrimitive("_bind1",         Bind1);
  AddPrimitive("_defined",       Defined);
  AddPrimitive("_div",           Div);
  AddPrimitive("_elem",          Elem);
  AddPrimitive("_find",          Find);
  AddPrimitive("_findr",         FindR);
  AddPrimitive("_findre",        FindRegex);
  AddPrimitive("_head",          Head);
  AddPrimitive("_is_binding",    IsBinding);
  AddPrimitive("_is_bool",       IsBool);
  AddPrimitive("_is_closure",    IsClosure);
  AddPrimitive("_is_err",        IsErr);
  AddPrimitive("_is_int",        IsInt);
  AddPrimitive("_is_list",       IsList);
  AddPrimitive("_is_text",       IsText);
  AddPrimitive("_length",        Length);
  AddPrimitive("_list1",         List1);
  AddPrimitive("_lookup",        Lookup);
  AddPrimitive("_map",           Map);
  AddPrimitive("_par_map",       ParMap);
  AddPrimitive("_max",           Max);
  AddPrimitive("_min",           Min);
  AddPrimitive("_mod",           Mod);
  AddPrimitive("_n",             GetName);
  AddPrimitive("_same_type",     SameType);
  AddPrimitive("_sub",           Sub);
  AddPrimitive("_tail",          Tail);
  AddPrimitive("_type_of",       TypeOf);
  AddPrimitive("_v",             GetValue);
  AddPrimitive("_run_tool",      ApplyRunTool);
  AddPrimitive("_print",         Print);
  AddPrimitive("_model_name",    ModelName);
  AddPrimitive("_fingerprint",   GetFP);
  
  AddPrimitiveUnOp("!",  Not);
  AddPrimitiveUnOp("-",  Neg);

  AddPrimitiveOp("==",  Equal);
  AddPrimitiveOp("&&",  And);
  AddPrimitiveOp("||",  Or);
  AddPrimitiveOp("=>",  Implies);
  AddPrimitiveOp("!=",  NotEq);
  AddPrimitiveOp(">=",  GreaterEq);
  AddPrimitiveOp("<=",  LessEq);
  AddPrimitiveOp("++",  PlusPlus);
  AddPrimitiveOp(">",   Greater);
  AddPrimitiveOp("<",   Less);
  AddPrimitiveOp("-",   Minus);
  AddPrimitiveOp("+",   Plus);
  AddPrimitiveOp("*",   Star);

  if(VestaConfig::is_set("Evaluator", "WorkerStackSize"))
    {
      WorkerStackSize = VestaConfig::get_int("Evaluator", "WorkerStackSize");
    }

  threadAllowed = maxThreads;
  AvailWorks = NULL;
  Basics::thread* th = NEW(Basics::thread);
  th->fork(DoAvailWork, (void*)NULL);
}

void PrimInit()
{
  static pthread_once_t init_once = PTHREAD_ONCE_INIT;
  pthread_once(&init_once, PrimInit_inner);
}
