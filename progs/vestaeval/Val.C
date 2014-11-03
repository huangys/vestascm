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

// File: Val.C

#include "Val.H"
#include "Expr.H"
#include "Parser.H"
#include "Location.H"
#include "ModelState.H"
#include "PrimRunTool.H"
#include "Files.H"
#include "Debug.H"
#include <CacheC.H>
#include <VestaSource.H>
#include <UniqueId.H>
#include <sys/types.h>
#include <sys/stat.h>
#include <BufStream.H>
#include "DepMergeOptimizer.H"

using std::ios;
using std::ostream;
using std::istream;
using std::fstream;
using std::endl;
using std::oct;
using std::hex;
using std::dec;
using Basics::OBufStream;
using Basics::IBufStream;
using OS::cio;

static Basics::mutex valMu;

// Useful constants:
Val valTrue       = NEW_CONSTR(BooleanVC, (true));
Val valFalse      = NEW_CONSTR(BooleanVC, (false));
Val valZero       = NEW_CONSTR(IntegerVC, (0));
Val valErr        = NEW_CONSTR(ErrorVC, (true));
Val valUnbnd      = NEW(UnbndVC);
Assoc nullAssoc   = NEW_CONSTR(AssocVC, (nameDot, valUnbnd));

// Text strings for types:
Val valTBinding = NEW_CONSTR(TextVC, ("t_binding"));
Val valTBool    = NEW_CONSTR(TextVC, ("t_bool"));
Val valTClosure = NEW_CONSTR(TextVC, ("t_closure"));
Val valTErr     = NEW_CONSTR(TextVC, ("t_err"));
Val valTInt     = NEW_CONSTR(TextVC, ("t_int"));
Val valTList    = NEW_CONSTR(TextVC, ("t_list"));
Val valTText    = NEW_CONSTR(TextVC, ("t_text"));

// Typecode fingerprints:
FP::Tag booleanTag("Bool"),
        integerTag("Int"),
	listTag("List"),
	bindingTag("Binding"),
	primitiveTag("Prim"),
	textTag("Text"),
	closureTag("Closure"),
	modelTag("Model"),
	errorTag("Error"),
	unbndTag("Unbnd");

Context conEmpty, conInitial;

Val CollectLet(const Text& name, Val v1, Val v2, DepMergeOptimizer &dmo);
void CollectLet(const Text& name, Val v1, DPaths *ps, Val res,
		/*OUT*/ DPaths*& nps, DepMergeOptimizer &dmo);
Val CollectFunc(Val bodyv, Val fv, const Context& c, DepMergeOptimizer &dmo);
void CollectFunc(DPaths *ps, Val fv, const Context& c, Val res,
		 /*OUT*/ DPaths*& nps, DepMergeOptimizer &dmo);
Val CollectModel(Val bodyv, Val fv, const Context& c, DepMergeOptimizer &dmo);
void CollectModel(DPaths *ps, const Context& c, Val res,
		  /*OUT*/ DPaths*& nps, DepMergeOptimizer &dmo);

static void Indent(ostream& os, int amount) {
  while (amount--) os << " ";
}

static int counter = 0;

static Text GetUniqueName() {
  int num = IncCounter(&counter);
  char buff[16];
  sprintf(buff, "%d", num);
  Text res(buff);
  return res;
}

// Fingerprinting a context.
FP::Tag FingerPrintContext(const Context& c) {
  Context work = c;

  RawFP fp = POLY_ONE;
  while (!work.Null())
    FP::Tag::ExtendRaw(/*INOUT*/ fp, work.Pop()->FingerPrint());
  FP::Tag tag;
  tag.Permute(fp);
  return tag;
}

FP::Tag FingerPrintContext(const Context& c, const Text& id) {
  Context work = c;

  RawFP fp = POLY_ONE;
  while (!work.Null()) {
    Assoc a = work.Pop();
    if (a->name != id)
      FP::Tag::ExtendRaw(/*INOUT*/ fp, a->FingerPrint());
  }
  FP::Tag tag;
  tag.Permute(fp);
  return tag;
}

// Print context.
void PrintContext(ostream& os, const Context& c, bool verbose, int indent) 
{
  Context cc = c;
  os << "[ ";
  if (!cc.Null())
    cc.Pop()->PrintD(os, verbose, indent + 2);
  while (!cc.Null()) {
    os << ",\n";
    Indent(os, indent + 2);
    cc.Pop()->PrintD(os, verbose, indent + 2);
  }
  os << " ]";
}

void PrintContext(ostream& os, const Context& c, const Text& id, bool verbose,
		  int indent) 
{
  Context cc = c;
  os << "[ ";
  while (!cc.Null()) {
    Assoc a = cc.Pop();
    if (a->name != id) {
      a->PrintD(os, verbose, indent + 2);
      break;
    }
  }
  while (!cc.Null()) {
    Assoc a = cc.Pop();
    if (a->name != id) {
      os << "," << endl;
      Indent(os, indent + 2);
      a->PrintD(os, verbose, indent + 2);
    }
  }
  os << " ]";
}

//// ValC:
Val ValC::MergeDPS(DPaths* ps) {
  if (ps != NULL) {
    if (this->dps == NULL)
      this->dps = NEW_CONSTR(DPaths, (ps->Size()));
    this->dps = this->dps->Union(ps);
  }
  return this;
}
Val ValC::MergeDPS(DPaths* ps, DepMergeOptimizer &dmo) {
  // Note that this will allocate a DPaths instance if this->dps is a
  // null pointer and ps is non-empty.
  this->dps = dmo.maybe_union(ps, this->dps);
  return this;
}

Val ValC::Merge(Val v) {
  this->MergeDPS(v->dps);
  if (v->path != NULL) {
    if (this->dps == NULL)
      this->dps = NEW(DPaths);
    // No need to deep copy v->path.
    DepPathTbl::KVPairPtr pr;
    (void)this->dps->Put(*(v->path), v, pr);
    // assert(v->FingerPrint() == pr->val->FingerPrint());
  }
  return this;
}

Val ValC::MergeAndTypeDPS(Val v) {
  this->MergeDPS(v->dps);
  if (v->path != NULL)
    this->AddToDPS(v->path, ValType(v), TypePK);
  return this;
}

Val ValC::MergeLenDPS(Val v) {
  if (v->vKind == BindingVK) {
    DPaths *ps = ((BindingVC*)v)->lenDps;
    if (ps != NULL) {
      if (this->dps == NULL)
	this->dps = NEW_CONSTR(DPaths, (ps->Size()));
      DepPathTbl::TIter iter(ps);
      DepPathTbl::KVPairPtr ptr;
      while (iter.Next(ptr)) {
	DepPath lenPath(ptr->key.content->path, BLenPK, ptr->key.content->pathFP);
	Val vNames = ((BindingVC*)ptr->val)->Names();
	(void)this->dps->Put(lenPath, vNames, ptr);
	// assert(vNames->FingerPrint() == ptr->val->FingerPrint());
      }
    }
  }
  else {
    assert(v->vKind == ListVK);
    DPaths *ps = ((ListVC*)v)->lenDps;
    if (ps != NULL) {
      if (this->dps == NULL)
	this->dps = NEW_CONSTR(DPaths, (ps->Size()));
      DepPathTbl::TIter iter(ps);
      DepPathTbl::KVPairPtr ptr;
      while (iter.Next(ptr)) {
	DepPath lenPath(ptr->key.content->path, LLenPK, ptr->key.content->pathFP);
	Val vLen = NEW_CONSTR(IntegerVC, (((ListVC*)ptr->val)->elems.Length()));
	(void)this->dps->Put(lenPath, vLen, ptr);
	// assert(vLen->FingerPrint() == ptr->val->FingerPrint());
      }
    }
  }
  return this;
}

Val ValC::MergeAndLenDPS(Val v) {
  this->MergeDPS(v->dps);
  if (v->vKind == BindingVK) {
    BindingVC *bv = (BindingVC*)v;
    if (bv->path != NULL)
      this->AddToDPS(bv->path, bv->Names(), BLenPK);
    else
      this->MergeLenDPS(bv);
  }
  else {
    assert(v->vKind == ListVK);
    ListVC *lstv = (ListVC*)v;
    if (lstv->path != NULL) {
      int len = lstv->elems.Length();
      IntegerVC *vLen = NEW_CONSTR(IntegerVC, (len));
      this->AddToDPS(lstv->path, vLen, LLenPK);
    }
    else
      this->MergeLenDPS(lstv);
  }
  return this;
}

Val ValC::AddToDPS(DepPath *dp, Val v, PathKind pk) {
  if (dp != NULL) {
    if (this->dps == NULL)
      this->dps = NEW(DPaths);
    DepPathTbl::KVPairPtr ptr;
    if (pk == DummyPK)
      ptr = NEW_CONSTR(DepPathTbl::KVPair, (*dp, v));
    else {
      DepPath newPath(dp->content->path, pk, dp->content->pathFP);
      ptr = NEW_CONSTR(DepPathTbl::KVPair, (newPath, v));
    }
    (void)this->dps->Put(ptr);
  }
  return this;
}

Val ValC::AddExtendToDPS(DepPath *dp, Val v, PathKind pk, const Text& id) {
  if (dp != NULL) {
    if (this->dps == NULL)
      this->dps = NEW(DPaths);
    DepPath newPath(dp->content);
    newPath.Extend(id);
    newPath.content->pKind = pk;
    DepPathTbl::KVPairPtr pr;
    (void)this->dps->Put(newPath, v, pr);
    // assert(v->FingerPrint() == pr->val->FingerPrint());
  }
  return this;
}

Val ValC::Extend(Val v, const Text& id, PathKind pk, bool add) {
  Val result;

  if (this->path != NULL) {
    result = v->Copy();
    result->path = this->path->DeepCopy();
    result->path->Extend(id, pk);
    if (add) result->dps = this->dps;
  }
  else if (add) {
    result = v->Copy();
    result->path = v->path;
    result->MergeDPS(v->dps)->MergeDPS(this->dps);
  }
  else
    // No need to copy.
    return v;
  return result;
}

//// Subclasses of ValC:
// BooleanVC
FP::Tag BooleanVC::FingerPrint() {
  if (!this->tagged) {
    FP::Tag theTag = booleanTag;
    bool8 fp_b = b ? 1 : 0;
    theTag.Extend(&fp_b, sizeof_assert(fp_b, 1));
    this->tag = theTag;
    this->tagged = true;
  }
  return this->tag;
}

// IntegerVC
FP::Tag IntegerVC::FingerPrint() {
  if (!this->tagged) {
    FP::Tag theTag = integerTag;
    Basics::int32 num_net = Basics::hton32(num);
    theTag.Extend((char*)(&num_net), sizeof_assert(num_net, 4));
    this->tag = theTag;
    this->tagged = true;
  }
  return this->tag;
}

// ListVC
void ListVC::PrintD(ostream& os, bool verbose, int indent, int depth, bool nl)
{
  Vals vv = this->elems;
  os << "< ";
  if (depth == 0) {
    os << "... >";
    return;
  }
  if (!vv.Null())
    vv.Pop()->PrintD(os, verbose, indent + 2, depth - 1, nl);
  while (!vv.Null()) {
    os << ",";
    if (nl) {
      os << endl;
      Indent(os, indent + 2);
    }
    else os << " ";
    vv.Pop()->PrintD(os, verbose, indent + 2, depth - 1, nl);
  }
  os << " >";
}

FP::Tag ListVC::FingerPrint() {
  Vals vv = elems;

  if (!this->tagged) {
    FP::Tag theTag = listTag;
    RawFP fp;
    theTag.Unpermute(/*OUT*/ fp);
    while (!vv.Null())
      FP::Tag::ExtendRaw(/*INPUT*/ fp, vv.Pop()->FingerPrint());
    theTag.Permute(fp);
    this->tag = theTag;
    this->tagged = true;
  }
  return this->tag;
}

IntegerVC* ListVC::Length() {
  int len = this->elems.Length();
  IntegerVC *result = NEW_CONSTR(IntegerVC, (len));

  result->MergeDPS(this->dps);
  if (this->path != NULL) {
    FpVC *fpv = NEW_CONSTR(FpVC, (result->FingerPrint()));
    result->AddToDPS(this->path, fpv, LLenPK);
  }
  else
    result->MergeLenDPS(this);
  result->cacheit = this->cacheit;
  return result;
}

Val ListVC::GetElem(int index) {
  Vals vv = this->elems;
  Val result;

  if (index < 0 || index >= vv.Length()) {
    // Out of bounds:
    result = NEW(ErrorVC);
    return result->MergeAndLenDPS(this);
  }
  result = vv.Nth(index);
  if (path != NULL) {
    result = result->Copy();
    result->path = path->DeepCopy();
    result->path->Extend(IntArc(index));
    result->dps = this->dps;
  }
  else
    result->MergeDPS(this->dps);
  result->cacheit = result->cacheit && this->cacheit;
  return result;
}

Val ListVC::GetElemNoDpnd(int index) {
  Vals vv = elems;

  if (index < 0 || index >= vv.Length())
    return valErr;
  return vv.Nth(index);
}

Val ListVC::AddToLenDPS(const DepPath& dp, Val val) {
  if (this->lenDps == NULL) {
    this->lenDps = NEW_CONSTR(DPaths, (1));  // sizeHint = 1.
  }
  DepPathTbl::KVPairPtr pr;
  (void)this->lenDps->Put(dp, val, pr);
  // assert(val->FingerPrint() == pr->val->FingerPrint());
  return this;
}

Val ListVC::MergeToLenDPS(DPaths *ps) {
  /* Precondition: ps must be the lenDps of a binding or a list. */
  if (ps != NULL) {
    if (this->lenDps == NULL) {
      this->lenDps = NEW_CONSTR(DPaths, (ps->Size()));
    }
    this->lenDps = this->lenDps->Union(ps);
  }
  return this;
}

Val ListVC::Copy(bool more) {
  ListVC *result = NEW_CONSTR(ListVC, (*this));
  if (more) {
    if (this->lenDps != NULL && this->lenDps->Size() != 0) {
      result->lenDps = NEW_CONSTR(DPaths, (this->lenDps));
    }
  }
  return result;
}

// BindingVC
BindingVC::BindingVC(BindingVC *b1, BindingVC *b2, bool rec)
: ValC(BindingVK), lenDps(NULL) {
  /* We leave this->dps to be empty. The caller must set this field
     accordingly after creating a new binding value. */
  // Merge the two bindings:
  if (rec)
    this->elems = b1->RecursiveOverlay(b2);
  else
    this->elems = b1->SimpleOverlay(b2);

  // Handle length dependency:
  if (b1->path != NULL)
    this->AddToLenDPS(*b1->path, b1);
  else
    this->MergeToLenDPS(b1->lenDps);

  if (b2->path != NULL)
    this->AddToLenDPS(*b2->path, b2);
  else
    this->MergeToLenDPS(b2->lenDps);

  // Finally, set the cacheit flag: 
  this->cacheit = b1->cacheit && b2->cacheit;
}

void BindingVC::PrintD(ostream& os, bool verbose, int indent, int depth, bool nl) {
  Context work = this->elems;
  os << "[ ";
  if(depth == 0) {
    os << "... ]";
    return;
  }
  if (!work.Null()) 
    work.Pop()->PrintD(os, verbose, indent + 2, depth, nl);
  while (!work.Null()) {
    os << ",";
    if (nl) {
      os << endl;
      Indent(os, indent + 2);
    }
    else os << " ";
    work.Pop()->PrintD(os, verbose, indent + 2, depth, nl);
  }
  os << " ]";
}

FP::Tag BindingVC::FingerPrint() {
  if (!this->tagged) {
    FP::Tag theTag = bindingTag;
    Context work = this->elems;
    RawFP fp;
    theTag.Unpermute(/*OUT*/ fp);
    while (!work.Null()) {
      FP::Tag::ExtendRaw(/*INPUT*/ fp, work.Pop()->FingerPrint());
    }
    theTag.Permute(fp);
    this->tag = theTag;
    this->tagged = true;
  }
  return this->tag;
}

Val BindingVC::Names() {
  Context work = this->elems;

  Vals vs;
  while (!work.Null())
    vs.Append1D(NEW_CONSTR(TextVC, (work.Pop()->name)));
  return NEW_CONSTR(ListVC, (vs));
}

IntegerVC* BindingVC::Length() {
  int len = this->elems.Length();
  IntegerVC *result = NEW_CONSTR(IntegerVC, (len));

  result->MergeDPS(this->dps);
  if (this->path != NULL)
    result->AddToDPS(this->path, this->Names(), BLenPK);
  else
    result->MergeLenDPS(this);
  result->cacheit = this->cacheit;
  return result;
}

Val BindingVC::Defined(const Text& id) {
  Assoc elem = FindInContext(id, this->elems);
  bool b = (elem != nullAssoc);
  Val result = NEW_CONSTR(BooleanVC, (b));

  // Figure out the dependency:
  result->MergeDPS(this->dps);
  if (this->path != NULL) {
    Val val = (b ? valTrue : valFalse);
    result->AddExtendToDPS(this->path, val, BangPK, id);
  }
  else if (this->lenDps != NULL) {
    DepPathTbl::TIter iter(this->lenDps);
    DepPathTbl::KVPairPtr ptr;
    while (iter.Next(ptr)) {
      Context work = ((BindingVC*)ptr->val)->elems;
      bool b1 = false;
      while (!work.Null()) {
	if (work.Pop()->name == id) {
	  b1 = true;
	  break;
	}
      }
      Val val = (b1 ? valTrue : valFalse);
      result->AddExtendToDPS(&(ptr->key), val, BangPK, id);
    }
  }
  result->cacheit = this->cacheit;
  return result;
}

Val BindingVC::Lookup(const Text& id) {
  Val v = LookupInContext(id, this->elems);
  Val result = this->Extend(v, id);
  result->cacheit = result->cacheit && this->cacheit;
  return result;
}

Context BindingVC::SimpleOverlay(BindingVC *bv) {
  Context c1 = this->elems, c2 = bv->elems;
  Context result;
  Assoc a, a1, a2;
  Val v, v1, v2;
  Text name;

  while (!c1.Null()) {
    a1 = c1.Pop();
    name = a1->name;
    a2 = FindInContext(name, c2);
    if (a2 == nullAssoc) {
      v1 = a1->val;
      v = this->Extend(v1, name, NormPK, false);
      v->AddExtendToDPS(bv->path, valFalse, BangPK, name);
    }
    else {
      v2 = a2->val;
      v = bv->Extend(v2, name, NormPK, false);
    }
    a = NEW_CONSTR(AssocVC, (name, v));
    result.Append1D(a);
  }
  c1 = this->elems;
  while (!c2.Null()) {
    a2 = c2.Pop();
    name = a2->name;
    if (FindInContext(name, c1) == nullAssoc) {
      v2 = a2->val;
      v = bv->Extend(v2, name, NormPK, false);
      a = NEW_CONSTR(AssocVC, (name, v));
      result.Append1D(a);
    }
  }
  return result;
}

Context BindingVC::RecursiveOverlay(BindingVC *bv) {
  Context c1 = this->elems;
  Context c2 = bv->elems;
  Context result;
  Assoc a, a1, a2;
  Val v, v1, v2;
  Text name;

  while (!c1.Null()) {
    a1 = c1.Pop();
    name = a1->name;
    a2 = FindInContext(name, c2);
    if (a2 == nullAssoc) {
      v1 = a1->val;
      v = this->Extend(v1, name, NormPK, false);
      v->AddExtendToDPS(bv->path, valFalse, BangPK, name);
    }
    else {
      v2 = a2->val;
      if (v2->vKind == BindingVK) {
	v1 = a1->val;
	if (v1->vKind == BindingVK) {
	  v1 = this->Extend(v1, name, NormPK, false);
	  v2 = bv->Extend(v2, name, NormPK, false);
	  v = NEW_CONSTR(BindingVC, ((BindingVC*)v1, (BindingVC*)v2, true));
	  v->MergeDPS(v1->dps)->MergeDPS(v2->dps);
	}
	else {
	  v = bv->Extend(v2, name, NormPK, false);
	  v->AddExtendToDPS(this->path, ValType(v1), TypePK, name);
	}
      }
      else
	v = bv->Extend(v2, name, NormPK, false);
    }
    a = NEW_CONSTR(AssocVC, (name, v));
    result.Append1D(a);
  }
  c1 = this->elems;
  while (!c2.Null()) {
    a2 = c2.Pop();
    name = a2->name;
    if (FindInContext(name, c1) == nullAssoc) {
      v2 = a2->val;
      v = bv->Extend(v2, name, NormPK, false);
      if (v2->vKind == BindingVK)
	v->AddExtendToDPS(path, valFalse, BangPK, name);
      a = NEW_CONSTR(AssocVC, (name, v));
      result.Append1D(a);
    }
  }
  return result;
}

Val BindingVC::GetElem(const Text& name, int& index) {
  Context work = this->elems;
  Val result;
  
  index = 0;
  while (!work.Null()) {
    Assoc a = work.Pop();
    if (a->name == name) {
      result = a->val;
      return Extend(result, name);
    }
    index++;
  }
  result = NEW(ErrorVC);
  result->MergeDPS(dps);
  result->AddExtendToDPS(path, valFalse, BangPK, name);
  result->cacheit = result->cacheit && this->cacheit;
  return result;
}

Val BindingVC::GetElem(int index, Text& name) {
  Context work = this->elems;
  Val result;

  if (index < 0 || index >= work.Length()) {
    result = NEW(ErrorVC);
    return result->MergeAndLenDPS(this);
  }
  Assoc a = work.Nth(index);
  name = a->name;
  result = this->Extend(a->val, IntArc(index));
  result->cacheit = result->cacheit && this->cacheit;
  return result;
}

bool BindingVC::DefinedNoDpnd(const Text& id) {
  return (FindInContext(id, this->elems) != nullAssoc);
}

Val BindingVC::LookupNoDpnd(const Text& id) {
  return LookupInContext(id, this->elems);
}

Val BindingVC::GetElemNoDpnd(const Text& name, int& index) {
  Context work = this->elems;
  index = 0;
  
  while (!work.Null()) {
    Assoc a = work.Pop();
    if (a->name == name)
      return a->val;
    index++;
  }
  return valErr;
}

Val BindingVC::GetElemNoDpnd(int index, Text& name) {
  Context work = this->elems;

  if (index < 0 || index >= work.Length())
    return valErr;
  Assoc a = work.Nth(index);
  name = a->name;
  return a->val;
}

Binding BindingVC::AddBindingAssoc(const Text& name, Val v) {
  return NEW_CONSTR(BindingVC, 
		    (Context(NEW_CONSTR(AssocVC, (name, v)), this->elems)));
}

Binding BindingVC::RemoveBindingAssoc(const Text& id) {
  Context work = this->elems;
  bool found;
  return NEW_CONSTR(BindingVC, (Snip(work, id, found)));
}

Val BindingVC::AddToLenDPS(const DepPath& dp, Val val) {
  if (this->lenDps == NULL)
    this->lenDps = NEW_CONSTR(DPaths, (1));   // sizeHint = 1.
  DepPathTbl::KVPairPtr pr;
  (void)this->lenDps->Put(dp, val, pr);
  // assert(val->FingerPrint() == pr->val->FingerPrint());
  return this;
}

Val BindingVC::MergeToLenDPS(DPaths *ps) {
  if (ps != NULL) {
    if (this->lenDps == NULL)
      this->lenDps = NEW_CONSTR(DPaths, (ps->Size()));
    this->lenDps = this->lenDps->Union(ps);
  }
  return this;
}

Val BindingVC::Copy(bool more) {
  BindingVC *result = NEW_CONSTR(BindingVC, (*this));
  if (more) {
    if (this->lenDps != NULL && this->lenDps->Size() != 0) {
      result->lenDps = NEW_CONSTR(DPaths, (this->lenDps));
    }
  }
  return result;
}

// PrimitiveVC
FP::Tag PrimitiveVC::FingerPrint() {
  if (!this->tagged) {
    FP::Tag theTag = primitiveTag;
    theTag.Extend(name);
    this->tag = theTag;
    this->tagged = true;
  }
  return this->tag;
}

// TextVC
TextVC::TextC::TextC(const Text& tname, const Text& ttext)
: hasTxt(false), hasName(true), name(tname) {
  try {
    VestaSource* vs = CreateDerived();
    int len = ttext.Length();
    VestaSource::errorCode err = vs->write(ttext.chars(), &len, 0);
    if(err != VestaSource::ok) {
      Error(cio().start_err(), 
	    Text("Failed to convert text to file (on write): ") + this->name 
	    + ": " + VestaSource::errorCodeString(err) + ".\n");
      cio().end_err();
      throw(Evaluator::failure(Text("exiting"), false));      
    }
    err = vs->makeFilesImmutable(fpContent->num);
    if(err != VestaSource::ok) {
      Error(cio().start_err(), 
	    Text("Failed to convert text to file (on makeFilesImmutable): ") + this->name 
	    + ": " + VestaSource::errorCodeString(err) + ".\n");
      cio().end_err();
      throw(Evaluator::failure(Text("exiting"), false));      
    }
    vs->resync();
    this->shortId = vs->shortId();
    delete vs; 
    vs = NULL;
    if(this->shortId == NullShortId)
      {
	Error(cio().start_err(), 
	      Text("Failed to convert text to file: ") + this->name 
	      + ": file has NullShortId.\n");
	cio().end_err();
	throw(Evaluator::failure(Text("exiting"), false));      
      }
    this->name = SourceOrDerived::shortIdToName(this->shortId);
    this->hasName = true;
  } catch (SRPC::failure f) {
    // Failed to create because an SRPC::failure is thrown:
    Error(cio().start_err(), 
	  Text("Failed to convert text to file:  SRPC::failure (")
	  + IntToText(f.r) + "), " + f.msg + ".\n");
    cio().end_err();
    throw(Evaluator::failure(Text("exiting"), false));
  }
}

TextVC::TextC::TextC(const Text& tname, fstream *iFile, VestaSource *vSource)
: name(tname), hasTxt(false), hasName(true), shortId(NullShortId) {
  this->shortId = vSource->shortId();
}

TextVC::TextVC(const Text& tname, const Text& ttext, char c, const FP::Tag& fp)
: ValC(TextVK) {
  content = NEW_CONSTR(TextC, (tname, ttext));
  this->tag = textTag;
  this->tag.Extend(c);
  this->tag.Extend(fp);
  this->tagged = true;
}

TextVC::TextVC(const Text& tname, fstream *iFile, VestaSource *vSource)
: ValC(TextVK) {
  this->content = NEW_CONSTR(TextC, (tname, iFile, vSource));
  this->tag = vSource->fptag;
  this->tagged = true;
}

TextVC::TextVC(const Text& tname, const ShortId sid, int fp_content)
: ValC(TextVK) {
  this->content = NEW_CONSTR(TextC, (tname, sid));
  this->tag = textTag;
  if (fp_content != 0) {
    SourceOrDerived f;
    f.open(sid);
    if (fp_content == -1) {
      this->tag.Extend('D');
      FP::FileContents(f, /*INOUT*/ this->tag);
      FS::Close(f);
    }
    else {
      f.seekg(0, ios::end);
      int length = f.tellg();
      f.seekg(0, ios::beg);
      if (length < fp_content) {
	this->tag.Extend('D');
	FP::FileContents(f, /*INOUT*/ this->tag);
	FS::Close(f);
      }
      else {
	this->tag.Extend('d');
	this->tag.Extend(UniqueId());
      }
    }
  }
  else {
    this->tag.Extend('d');
    this->tag.Extend(UniqueId());
  }
  this->tagged = true;
}

void TextVC::PrintD(ostream& os, bool verbose, int indent, int depth, bool nl) 
{
  if (verbose || this->HasTxt()) {
    bool close;
    istream *txt = this->Content(close);
    if (txt == NULL) return;
    os << '"' << oct;
    int ch;
    while ((ch = txt->get()) != EOF) {
      unsigned char c = ch;
      switch (c) {
      case '\n': os << "\\n";  break;
      case '\t': os << "\\t";  break;
      case '\r': os << "\\r";  break;
      case '\f': os << "\\f";  break;
      case '\b': os << "\\b";  break;
      case '\\': os << "\\\\"; break;
      case '"':  os << "\\\""; break;
      default:
        if (c < ' ' || c > '~') {
	  char oc = os.fill('0');
	  int ow = os.width(3);
	  os << '\\' << (int)c;
	  (void)os.fill(oc);
	}
	else os << c;
      }
    }
    os << '"' << dec;
    if (close) ((SourceOrDerived*)txt)->close();
  }
  else {
    if (this->HasSid()) {
      os << "<file";
      if(printSidNum) {
	char sidbuf[9];
	sprintf(sidbuf, "0x%08x", this->content->shortId);
	os << " " << sidbuf;
      }
      os << ": " 
	 << SourceOrDerived::shortIdToName(this->content->shortId, false)
	 << ">"; 
    }
    else {
      // No text and shortid means bad file.
      os << "<badfile " << this->content->name << ">";
    }
  }
}

FP::Tag TextVC::FingerPrint() {
  if (!this->tagged) {
    // It must have text.
    FP::Tag theTag = textTag;
    bool8 fp_hasTxt = content->hasTxt ? 1 : 0;
    theTag.Extend(&fp_hasTxt, sizeof_assert(fp_hasTxt, 1));
    theTag.Extend(content->txt);
    this->tag = theTag;
    this->tagged = true;
  }
  return this->tag;
}

static SourceOrDerived *open_shortid(ShortId sid,
				     const char *err_what,
				     const Text &err_name)
{
  SourceOrDerived *result = NEW(SourceOrDerived);
  errno = 0;
  result->open(sid);
  int l_errno_save = errno;
  if (result->fail())
    {
      OBufStream l_err_msg;
      char *l_sid_path = SourceOrDerived::shortIdToName(sid, false);
      l_err_msg << "Failed to open " << err_what
		<< ": " << err_name << endl
		<< "  shortid = 0x" << hex << sid << dec << " ("
		<< l_sid_path << ")" << endl;
      if(l_errno_save != 0)
	{
	  l_err_msg << "  " << Basics::errno_Text(l_errno_save)
		    << " (errno = " << l_errno_save << ")" << endl;
	}
      else if(!FS::Exists(l_sid_path))
	{
	  l_err_msg << "  shortid doesn't exist!" << endl;
	}
      delete [] l_sid_path;
      Error(cio().start_err(), l_err_msg.str());
      cio().end_err();
      return 0;
    }
  return result;
}

istream* TextVC::Content(bool& closeIt) {
  if (this->content->hasTxt) {
    closeIt = false;
    return NEW_CONSTR(IBufStream, (this->content->txt.chars()));
  }
  assert(this->HasSid());
  SourceOrDerived *iFile =
    open_shortid(this->content->shortId,
		 "file",
		 (this->content->hasName
		  ? this->content->name
		  : SourceOrDerived::shortIdToName(this->content->shortId)));
  if (!iFile) {
    if (!this->content->hasName) {
      this->content->name = iFile->shortIdToName(this->content->shortId);
      this->content->hasName = true;
    }
    throw(Evaluator::failure(Text("exiting"), false));      
  }
  closeIt = true;
  return iFile;
}

Text TextVC::NDS() {
  if (this->content->hasTxt) {
    return this->content->txt;
  }
  // assert(this->HasSid());
  SourceOrDerived *file =
    open_shortid(this->content->shortId,
		 "file",
		 (this->content->hasName
		  ? this->content->name
		  : SourceOrDerived::shortIdToName(this->content->shortId)));
  if (!file) {
    throw(Evaluator::failure(Text("exiting"), false));      
  }
  file->seekg(0, ios::end);
  unsigned long length = file->tellg();
  // Make sure this file is a reasonable size to read into a text
  // value.
  if(length > Text::MaxInt)
    {
      VError(cio().start_err(), Text("file ") + 
	     file->shortIdToName(content->shortId) +
	     " too big to manipulate as a text value");
      cio().end_err();
      throw(Evaluator::failure(Text("exiting"), false));
    }
  file->seekg(0, ios::beg);
  char *bytes = NEW_PTRFREE_ARRAY(char, length+1);
  if (!file->read(bytes, length)) {
    VError(cio().start_err(), 
	   Text("reading file ") + file->shortIdToName(content->shortId));
    cio().end_err();
    throw(Evaluator::failure(Text("exiting"), false));
  }
  file->close();
  // Add the terminating null byte.
  bytes[length] = 0;
  return Text(bytes, (void*)1);
}

ShortId TextVC::Sid() {
  valMu.lock();    
  if (this->HasSid()) {
    ShortId sid = this->content->shortId;
    valMu.unlock();
    return sid;
  }
  // assert(content->hasTxt);
  try {
    VestaSource* vs = CreateDerived();
    int len = this->content->txt.Length();
    VestaSource::errorCode err = 
      vs->write(this->content->txt.chars(), &len, 0);
    if(err != VestaSource::ok) {
      valMu.unlock();
      VError(cio().start_err(), Text("Failed to convert text to file (on write): ") + 
	     VestaSource::errorCodeString(err) + ".\n");
      cio().end_err();
      throw(Evaluator::failure(Text("exiting"), false));
    }
    err = vs->makeFilesImmutable(fpContent->num);
    if(err != VestaSource::ok) {
      valMu.unlock();
      VError(cio().start_err(), Text("Failed to convert text to file (on makeFilesImmutable): ") + 
	     VestaSource::errorCodeString(err) + ".\n");
      cio().end_err();
      throw(Evaluator::failure(Text("exiting"), false));
    }
    vs->resync();
    this->content->shortId = vs->shortId();
    delete vs;
    vs = NULL;
    if(this->content->shortId == NullShortId)
      {
	valMu.unlock();
	VError(cio().start_err(), Text("Failed to convert text to file: file has NullShortId.\n"));
	cio().end_err();
	throw(Evaluator::failure(Text("exiting"), false));
      }
    if (!this->content->hasName) {
      this->content->name = SourceOrDerived::shortIdToName(content->shortId);
      this->content->hasName = true;
    }
  } catch (SRPC::failure f) {
    // Failed to create because an SRPC::failure is thrown:
    valMu.unlock();      
    VError(cio().start_err(), 
	   Text("Failed to convert text to file:  SRPC::failure (")
	   + IntToText(f.r) + "), " + f.msg + ".\n");
    cio().end_err();
    throw(Evaluator::failure(Text("exiting"), false));
  } catch (SourceOrDerived::Fatal f) {
    // Failed to create because an SourceOrDerived::Fatal is thrown:
    valMu.unlock();      
    VError(cio().start_err(), 
	   Text("Failed to convert text to file: ") + f.msg + ".\n");
    cio().end_err();
    throw(Evaluator::failure(Text("exiting"), false));
  }
  ShortId sid = this->content->shortId;
  valMu.unlock();  
  return sid; 
}

Text TextVC::TName() {
  valMu.lock();    
  if (this->content->hasName) {
    valMu.unlock();    
    return this->content->name;
  }
  // Force the name.
  valMu.unlock();
  this->Sid();
  return this->content->name;
}

int TextVC::Length() {
  if (this->content->hasTxt) {
    return this->content->txt.Length();
  }
  assert(this->HasSid());
  SourceOrDerived *file = 
    open_shortid(this->content->shortId,
		 "file",
		 (this->content->hasName
		  ? this->content->name
		  : SourceOrDerived::shortIdToName(this->content->shortId)));
  if (!file) {
    throw(Evaluator::failure(Text("exiting"), false));
  }
  file->seekg(0, ios::end);
  int length = file->tellg();
  file->close();
  return length;
}

// ClosureVC
ClosureVC::ClosureVC(FuncEC *tfunc, const Context& c, bool fresh)
: ValC(ClosureVK), func(tfunc), exprTagged(false) {
  if (fresh) {
    Context work = c;
    while (!work.Null()) {
      Assoc a = work.Pop();
      Val val = a->val->Copy(true);
      val->path = NEW_CONSTR(DepPath, (a->name));
      this->con.Append1D(NEW_CONSTR(AssocVC, (a->name, val)));
    }
  }
  else
    this->con = c;
}

void ClosureVC::PrintD(ostream& os, bool verbose, int indent, int depth, bool nl) 
{
  if (verbose) {
    os << "((Closure: formals = ";
    this->func->args->PrintD(os);
    os << endl;
    Indent(os, indent);
    os << "--Closure: definition = " << *(this->func->loc) << endl;
    Indent(os, indent);
    os << "--Closure: body = ";
    this->func->body->PrintD(os);
    os << endl;
    Indent(os, indent);
    os << "--Closure: context =" << endl;
    Indent(os, indent + 2);
    PrintContext(os, this->con, this->func->name, true, indent + 2);
    os << endl;
    Indent(os, indent);
    os << "--End Closure))";
  }
  else {
    os << "<Closure " << *(this->func->loc) << ">";
  }
}

FP::Tag ClosureVC::FingerPrint() {
  if (!this->tagged) {
    FP::Tag theTag = this->FingerPrintExpr();
    theTag.Extend(FingerPrintContext(con, this->func->name));
    this->tag = theTag;
    this->tagged = true;
  }
  return this->tag;
}

FP::Tag ClosureVC::FingerPrintExpr() {
  if (!this->exprTagged) {
    FP::Tag theTag = closureTag;
    theTag.Extend(this->func->FingerPrint());
    this->exprTag = theTag;
    this->exprTagged = true;
  }
  return this->exprTag;
}

// ModelVC
ModelVC::ModelVC(const Text& tname, ShortId tsid, VestaSource *root, 
		 Expr mod, const Context& cc, VestaSource *vSource)
: ValC(ModelVK) {
  this->content = NEW_CONSTR(ModelC, (tname, tsid, root, mod, cc));
  // tag is the fp of vSource:
  this->tag = modelTag;
  this->tag.Extend(vSource->fptag);
  this->tagged = true;
  // lidTag is fp combination of the model root fp and model name:
  this->lidTag = modelTag;
  this->lidTag.Extend(root->fptag);
  Text prefix, tail;
  SplitPath(tname, prefix, tail);
  this->lidTag.Extend(tail);
}

ModelVC::ModelVC(const Text& tlPath, VestaSource *root, SrcLoc *loc)
: ValC(ModelVK) {
  this->content = NEW_CONSTR(ModelC, (tlPath));
  if (!IsAbsolutePath(tlPath)) {
    Text prefix, tail;
    SplitPath(loc->file, prefix, tail);
    this->content->name = prefix + tlPath;
  }
  fstream *iFile;
  VestaSource *vSource;
  this->cacheit = OpenSource(root, tlPath, loc, /*OUT*/ iFile, 
			     /*OUT*/ content->mRoot, /*OUT*/ content->sid,
			     /*OUT*/ vSource);
  if (this->cacheit) {
    // tag is the fp of vSource:
    this->tag = modelTag;
    this->tag.Extend(vSource->fptag);
    this->tagged = true;
    // lidTag is fp combination of the model root fp and model name:
    this->lidTag = modelTag;
    this->lidTag.Extend(content->mRoot->fptag);
    Text prefix, tail;
    SplitPath(tlPath, prefix, tail);
    this->lidTag.Extend(tail);
  }
  else {
    throw(Evaluator::failure(Text("exiting"), false));
  }
}

void ModelVC::PrintD(ostream& os, bool verbose, int indent, int depth, bool nl) 
{
  //  os << "<Model " << form("0x%08x", this->content->sid) << ">";
  os << "<Model " << this->content->name << ">";
}

Val ModelVC::Force() {
  valMu.lock();
  if (!this->content->parsed) {
    // Need to parse the model:
    SourceOrDerived *iFile = open_shortid(content->sid,
					  "the model", content->name);
    if (!iFile) {
      valMu.unlock();
      return NEW(ErrorVC);
    }
    try {
      content->model = Parse(iFile, this->content->name, this->content->sid, 
			     this->content->mRoot);
    } catch (const char* report) {
      valMu.unlock();
      throw report;
    }
    iFile->close();
    this->content->c = ProcessModelHead((ModelEC*)content->model);
    this->content->parsed = true;
  }
  valMu.unlock();
  return this;
}

// ErrorVC
ErrorVC::ErrorVC()
: ValC(ErrorVK) {
  // Fatal error: always terminate.
  throw(Evaluator::failure(Text("exiting"), false));
}

FP::Tag ErrorVC::FingerPrint() { return errorTag; }

// UnbndVC
FP::Tag UnbndVC::FingerPrint() { return unbndTag; }

// AssocVC
void AssocVC::PrintD(ostream& os, bool verbose, int indent, int depth, bool nl) 
{
  bool quote = false;

  for (int i = 0; i < name.Length(); i++) {
    char c = name[i];
    if (((c < 'a') || (c > 'z')) &&
	((c < 'A') || (c > 'Z')) &&
	((c < '0') || (c > '9')) &&
	(c != '_') &&
	(c != '.')) {
      quote = true;
      break;
    }
  }
  if (quote)
    os << '"' << name << "\"=";
  else
    os << name << "=";
  switch (this->val->vKind) {
  case BindingVK:
  case ListVK:
    if (nl) {
      os << endl;
      Indent(os, indent);
    }
    break;
  case ClosureVK:
    if (verbose) {
      if (nl) {
	os << endl;
        Indent(os, indent);
      }
    }
    break;
  default:
    break;
  }
  this->val->PrintD(os, verbose, indent, depth - 1, nl);
}

FP::Tag AssocVC::FingerPrint() {
  FP::Tag tag(this->name);
  tag.Extend(this->val->FingerPrint());
  return tag;
}

//// Assorted functions:
// Type predicates.
bool IsValTrue(Val v) {
  return ((v->vKind == BooleanVK) && ((BooleanVC*)v)->b);
}

bool IsValFalse(Val v) {
  return ((v->vKind == BooleanVK) && !(((BooleanVC*)v)->b));
}
     
bool IsEmptyBinding(Val v) {
  return ((v->vKind == BindingVK) && ((BindingVC*)v)->Null());
}

bool IsEmptyList(Val v) {
  return ((v->vKind == ListVK) && ((ListVC*)v)->elems.Null());
}

// The type of a vesta value.
Val ValType(Val v) {
  switch(v->vKind) {
  case BooleanVK:
    return valTBool;
  case IntegerVK:
    return valTInt;
  case TextVK:
    return valTText;
  case BindingVK:
    return valTBinding;
  case ListVK:
    return valTList;
  case ClosureVK: case ModelVK: case PrimitiveVK:
    return valTClosure;
  case ErrorVK: case UnbndVK:
    return valTErr;
  default:
    assert(v != 0);
    ostream& err_str = cio().start_err();
    v->VError(err_str, "Unexpected type");
    err_str << "v = ";
    v->PrintD(err_str);
    err_str << endl;
    InternalError(err_str, cio().helper(true), "ValType");
    // Unreached: InternalError never returns
    abort();
  }
  return NULL;
}

// Functions related to dependency analysis.
void DeleteDuplicatePathsInner(Val v, DPaths *ps) {
  // Filter v->dps:
  if (v->SizeOfDPS() == 0)
    v->dps = NULL;
  else {
    DepPathTbl::TIter iter(v->dps);
    DepPathTbl::KVPairPtr ptr;
    DPaths *newDps = NEW_CONSTR(DPaths, (v->SizeOfDPS()));
    while (iter.Next(ptr)) {
      if (!ps->Member(ptr->key))
	(void)newDps->Put(ptr);
    }
    v->dps = (newDps->Size() == 0) ? NULL : newDps;
  }

  // Filter subvalues:
  if (v->path == NULL) {
    switch (v->vKind) {
    case BindingVK:
      {
	Context work = ((BindingVC*)v)->elems;
	while (!work.Null())
	  DeleteDuplicatePathsInner(work.Pop()->val, ps);
	break;
      }
    case ListVK:
      {
	Vals elems = ((ListVC*)v)->elems;
	while (!elems.Null())
	  DeleteDuplicatePathsInner(elems.Pop(), ps);
	break;
      }
    default:
      break;
    }
  }
  return;
}

void DeleteDuplicatePaths(Val v) {
  DPaths *ps = v->dps;

  // If v->path or v->dps is not empty, just return:
  if (v->path != NULL || ps == NULL || ps->Size() == 0)
    return;
  
  switch (v->vKind) {
  case BindingVK:
    {
      Context work = ((BindingVC*)v)->elems;
      while (!work.Null())
	DeleteDuplicatePathsInner(work.Pop()->val, ps);
      break;
    }
  case ListVK:
    {
      Vals elems = ((ListVC*)v)->elems;
      while (!elems.Null())
	DeleteDuplicatePathsInner(elems.Pop(), ps);
      break;
    }
  default:
    break;
  }
  return;
}

Val CanonicalDpnd(Val v) {
  /* Canonicalize dependency of the value. */
  DPaths *ps = v->dps;
  
  // Trivial cases:
  if (v->path != NULL || ps == NULL || ps->Size() == 0)
    return v;
  
  // We only need to canonicalize dependency for composite value.
  switch (v->vKind) {
  case BindingVK:
    {
      Context work = ((BindingVC*)v)->elems;
      int len = work.Length();
      
      if (len == 0) return v;
      DPaths **psList = NEW_ARRAY(DPaths*, len);
      DPaths *psi = CanonicalDpnd(work.Pop()->val)->dps;
      int minSize = (psi ? psi->Size() : 0);
      psList[0] = psi;
      for (int i = 1; i < len; i++) {
	psi = CanonicalDpnd(work.Pop()->val)->dps;
	psList[i] = psi;
	minSize = min(minSize, (psi ? psi->Size() : 0));
      }
      if (minSize > 0)
	// Add the shared paths in the sub-values to ps, and delete
	// them from the dependency sets in the subvalues.
	{
	  // Find the interesection of the sub-value dependency sets
	  DPaths *intersection = DPaths::Intersection(psList, len);

	  if(intersection && !intersection->Empty())
	    {
	      // Merge the interesection into ps
	      ps = ps->Union(intersection);

	      // And remove the intersection from the sub-value
	      // dependency sets
	      work = ((BindingVC*)v)->elems;
	      while(!work.Null())
		{
		  Val sv = work.Pop()->val;
		  sv->dps = sv->dps->Difference(intersection);
		}
	    }
	}
      break;
    }
  case ListVK:
    {
      Vals elems = ((ListVC*)v)->elems;
      int len = elems.Length();
      
      if (len == 0) return v;
      DPaths **psList = NEW_ARRAY(DPaths*, len);
      DPaths *psi = CanonicalDpnd(elems.Pop())->dps;
      int minSize = (psi ? psi->Size() : 0);
      psList[0] = psi;
      for (int i = 1; i < len; i++) {
	psi = CanonicalDpnd(elems.Pop())->dps;
	psList[i] = psi;
	minSize = min(minSize, (psi ? psi->Size() : 0));
      }
      if (minSize > 0)
	// Add the shared paths in the sub-values to ps, and delete
	// them from the dependency sets in the subvalues.
	{
	  // Find the interesection of the sub-value dependency sets
	  DPaths *intersection = DPaths::Intersection(psList, len);

	  if(intersection && !intersection->Empty())
	    {
	      // Merge the interesection into ps
	      ps = ps->Union(intersection);

	      // And remove the intersection from the sub-value
	      // dependency sets
	      elems = ((ListVC*)v)->elems;
	      while(!elems.Null())
		{
		  Val sv = elems.Pop();
		  sv->dps = sv->dps->Difference(intersection);
		}
	    }
	}
      break;
    }
  default:
    break;
  }
  return v;
}

void ModelCutOff(Val v) {
  /* Cut off local dependency of a model. This prevents local
     dependency from propagating up in the call graph. */

  // v->dps:
  if (v->SizeOfDPS() == 0)
    v->dps = NULL;
  else
    v->dps = v->dps->Restrict(nameDot);

  DepPath *dp = v->path;
  if (dp != NULL) {
    if (dp->content->path->getlo() != nameDot)
      v->path = NULL;
  }
  else {
    // collect components of the value (only when no path):
    switch (v->vKind) {
    case BindingVK:
      {
	BindingVC *bv = (BindingVC*)v;
	if (bv->lenDps != NULL && bv->lenDps->Size() != 0) {
	  bv->lenDps = bv->lenDps->Restrict(nameDot);
	}
	Context work = bv->elems;
	while (!work.Null()) ModelCutOff(work.Pop()->val);
	break;
      }
    case ListVK:
      {
	ListVC *lstv = (ListVC*)v;
	if (lstv->lenDps != NULL && lstv->lenDps->Size() != 0) {
	  lstv->lenDps = lstv->lenDps->Restrict(nameDot);
	}
	Vals vv = lstv->elems;
	while (!vv.Null()) ModelCutOff(vv.Pop());
	break;
      }
    case ClosureVK:
      {
	ClosureVC *cl = (ClosureVC*)v;
	Context work = cl->con;
	while (!work.Null()) {
	  Assoc a = work.Pop();
	  if (cl->func->name != a->name) ModelCutOff(a->val);
	}
	break;
      }
    default:
      break;
    }
  }
  return;
}

void CollectDefined(BindingVC *bv, const Text& id, DPaths *ps) {
  /* Collect dependency for the path !/id into ps.
     Precondition: ps # NULL.  */
  if (bv->path != NULL) {
    bool b = bv->DefinedNoDpnd(id);
    Val val = (b ? valTrue : valFalse);
    ps = ps->AddExtend(bv->path, val, BangPK, id);
  }
  else if (bv->lenDps != NULL) {
    DepPathTbl::TIter iter(bv->lenDps);
    DepPathTbl::KVPairPtr ptr;
    while (iter.Next(ptr)) {
      Context work = ((BindingVC*)ptr->val)->elems;
      bool b1 = false;
      while (!work.Null()) {
	if (work.Pop()->name == id) {
	  b1 = true;
	  break;
	}
      }
      Val val = (b1 ? valTrue : valFalse);
      ps = ps->AddExtend(&(ptr->key), val, BangPK, id);
    }
  }
  return;
}

Val CollectLookup(BindingVC *bv, const Text& id, DepPath*& path, 
		  DPaths *ps, DepMergeOptimizer &dmo) {
  /* Collect dependency for the id component of bv into ps.
     Precondition: ps # NULL.  */
  if (bv->path != NULL) {
    path = bv->path->DeepCopy();
    path->Extend(id);
    return bv->LookupNoDpnd(id);
  }
  path = NULL;
  Val result = LookupInContext(id, bv->elems);
  ps = dmo.maybe_union(result->dps, ps);
  return result;
}

void ValueDpnd(Val v, DPaths *ps) {
  /* Collect all the dependency of v into ps.
     Assume: v->dps has been collected by the caller. */

  DepPath *dp = v->path;
  if (dp != NULL) {
    // collect v->path:
    DepPathTbl::KVPairPtr pr;
    (void)ps->Put(*dp, v, pr);
    // assert(v->FingerPrint() == pr->val->FingerPrint());
  }
  else {
    // collect components of the value (only when there is no path):
    switch (v->vKind) {
    case BindingVK:
      {
	BindingVC *bv = (BindingVC*)v;
	if (bv->lenDps != NULL && bv->lenDps->Size() != 0) {
	  ps = ps->Union(bv->lenDps);
	}
	Context work = bv->elems;
	while (!work.Null()) {
	  Val v1 = work.Pop()->val;
	  ps = ps->Union(v1->dps);
	  ValueDpnd(v1, ps);
	}
	break;
      }
    case ListVK:
      {
	ListVC *lstv = (ListVC*)v;
	if (lstv->lenDps != NULL && lstv->lenDps->Size() != 0) {
	  ps = ps->Union(lstv->lenDps);
	}
	Vals vv = lstv->elems;
	while (!vv.Null()) {
	  Val v1 = vv.Pop();
	  ps = ps->Union(v1->dps);
	  ValueDpnd(v1, ps);
	}
	break;
      }
    case ClosureVK:
      {
	ClosureVC *cl = (ClosureVC*)v;
	Context work = cl->con;
	while (!work.Null()) {
	  Assoc a = work.Pop();
	  if (cl->func->name != a->name) {
	    ps = ps->Union(a->val->dps);
	    ValueDpnd(a->val, ps);
	  }
	}
	break;
      }
    default:
      break;
    }
  }
  return;
}

DepPath* CollectDpnd(Val v, DepPath *dp, DPaths *ps,
		     DepMergeOptimizer &dmo, bool all = true) {
  /* Collect dependency of value v on path p into ps.
     Precondition: ps # NULL.  
     NOTE: When called, the fingerprint of the argument dp may not be
     correct. For example, we remlo, but are not bothered to recompute
     the fingerprint.  */
  DepPath *newPath = NULL;

  if (v->path != NULL) {
    dp->ExtendLow(v->path);
    return dp;
  }
  if (dp->Size() == 0) {
    if (all) {
      switch (dp->content->pKind) {
      case NormPK:
	{
	  ValueDpnd(v, ps);
	  break;
	}
      case BLenPK:
	{
	  DPaths *lenDps = ((BindingVC*)v)->lenDps;
	  if (lenDps != NULL && lenDps->Size() != 0) {
	    DepPathTbl::TIter iter(lenDps);
	    DepPathTbl::KVPairPtr ptr;
	    while (iter.Next(ptr)) {
	      DepPath lenPath(ptr->key.content->path, BLenPK, ptr->key.content->pathFP);
	      Val vNames = ((BindingVC*)ptr->val)->Names();
	      (void)ps->Put(lenPath, vNames, ptr);
              // assert(vNames->FingerPrint() == ptr->val->FingerPrint());
	    }
	  }
	  break;
	}
      case LLenPK:
	{
	  DPaths *lenDps = ((ListVC*)v)->lenDps;
	  if (lenDps != NULL && lenDps->Size() != 0) {
	    ps = ps->Union(lenDps);
	  }
	  break;
	}
      case TypePK:
	{
	  DPaths *lenDps = NULL;
	  if (v->vKind == BindingVK) {
	    lenDps = ((BindingVC*)v)->lenDps;
	    if (lenDps != NULL && lenDps->Size() != 0) {
	      DepPathTbl::TIter iter(lenDps);
	      DepPathTbl::KVPairPtr ptr;
	      while (iter.Next(ptr)) {
		DepPath typePath(ptr->key.content->path, TypePK, ptr->key.content->pathFP);
		(void)ps->Put(typePath, valTBinding, ptr);
		// assert(valTBinding->FingerPrint() == ptr->val->FingerPrint());
	      }
	    }
	  }
	  else if (v->vKind == ListVK) {
	    lenDps = ((ListVC*)v)->lenDps;
	    if (lenDps != NULL && lenDps->Size() != 0) {
	      ps = ps->Union(lenDps);
	    }
	  }
	  break;
	}
      default:
	break;
      }
    }
    return NULL;
  }
  switch (v->vKind) {
  case BindingVK:
    {
      BindingVC *bv = (BindingVC*)v;
      if (dp->Size() == 1 && dp->content->pKind == BangPK)
	CollectDefined(bv, dp->content->path->getlo(), ps);
      else {
	int n = ArcInt(dp->content->path->getlo());
	if (n == -1) {
	  Val v1 = CollectLookup(bv, dp->content->path->getlo(), newPath, ps, dmo);
	  dp->content->path->remlo();
	  if (newPath != NULL) {
	    newPath->Extend(*dp->content->path, dp->content->pKind);
	    return newPath;
	  }
	  newPath = CollectDpnd(v1, dp, ps, dmo);
	}
	else {
	  Context elems = bv->elems;
	  dp->content->path->remlo();
	  newPath = CollectDpnd(elems.Nth(n)->val, dp, ps, dmo);
	}
      }
      break;
    }
  case ListVK:
    {
      int n = ArcInt(dp->content->path->getlo());
      dp->content->path->remlo();
      Val v1 = ((ListVC*)v)->elems.Nth(n);
      ps = dmo.maybe_union(v1->dps, ps);
      newPath = CollectDpnd(v1, dp, ps, dmo);
      break;
    }
  case ClosureVK:
    {
      ClosureVC *cl = (ClosureVC*)v;
      Context work = cl->con;
      Text name(dp->content->path->getlo());
      while (!work.Null()) {
	Assoc a = work.Pop();
	if (a->name == name) {
	  Val v1 = a->val;
	  ps = dmo.maybe_union(v1->dps, ps);
	  dp->content->path->remlo();
	  newPath = CollectDpnd(v1, dp, ps, dmo);
	  return newPath;
	}
      }
      break;
    }
  default:
    assert(v != 0);
    assert(dp != 0);
    ostream& err_str = cio().start_err();
    Error(err_str, "(Impl) CollectDpnd: wrong type of value.\n");
    // Before bailing out completely, print out some more information,
    // in hopes of helping to debug this when it happens.
    err_str << "v = ";
    v->PrintD(err_str);
    err_str << endl;
    err_str << "dp = ";
    dp->Print(err_str);
    err_str << endl;
    InternalError(err_str, cio().helper(true), "CollectDpnd");
    // Unreached: InternalError never returns
    abort();
    break;
  }
  return newPath;
}

Val CollectLetS(const Text& name, Val v1, Val v2, DepMergeOptimizer &dmo) {
  /* We only need to do something for composite values. */
  switch (v2->vKind) {
  case BindingVK:
    {
      // Collect dependency for each element of the binding:
      dmo.push_enclosing(v2);
      BindingVC *bv = (BindingVC*)v2;
      Context rc, work = bv->elems;
      while (!work.Null()) {
	Assoc a = work.Pop();
	a = NEW_CONSTR(AssocVC, (a->name, CollectLet(name, v1, a->val, dmo)));
	rc.Append1D(a);
      }
      bv->elems = rc;
      // Collect dependency for lenDps of the binding:
      DPaths *newLenDps;
      CollectLet(name, v1, bv->lenDps, bv, newLenDps, dmo);
      bv->lenDps = newLenDps;
      dmo.pop_enclosing(v2);
      return bv;
    }
  case ListVK:
    {
      // Collect dependency for each element of the list:
      dmo.push_enclosing(v2);
      ListVC *lstv = (ListVC*)v2;
      Vals res, elems = lstv->elems;
      while (!elems.Null())
	res.Append1D(CollectLet(name, v1, elems.Pop(), dmo));
      lstv->elems = res;
      // Collect dependency for lenDps of the list:
      DPaths *newLenDps;
      CollectLet(name, v1, lstv->lenDps, lstv, newLenDps, dmo);
      lstv->lenDps = newLenDps;
      dmo.pop_enclosing(v2);
      return lstv;
    }
  case ClosureVK:
    {
      ClosureVC *cl = (ClosureVC*)v2;
      dmo.push_enclosing(v2);
      Context rc, work = cl->con;
      while (!work.Null()) {
	Assoc a = work.Pop();
	if (a->name != cl->func->name)
	  a = NEW_CONSTR(AssocVC, (a->name, CollectLet(name, v1, a->val, dmo)));
	rc.Append1D(a);
      }
      cl->con = rc;
      dmo.pop_enclosing(v2);
      return v2;
    }
  default:
    break;
  }
  return v2;
}

Val CollectLet(const Text& name, Val v1, Val v2, DepMergeOptimizer &dmo) {
  // Special case (control expr of foreach):
  if (name == emptyText) {
    // See IterateAssoc for the meaning of v1.
    v2->Merge(v1);
    return v2;
  }

  // Normal case:
  bool found = false;
  DepPath *newPath = NULL;
  DPaths *rps;
  // Collect dependency of v2->dps:
  if (v2->SizeOfDPS() != 0) {
    found = v2->dps->ContainsPrefix(name);
    if (found) {
      rps = NEW(DPaths);
      DepPathTbl::TIter iter(v2->dps);
      DepPathTbl::KVPairPtr ptr;
      while (iter.Next(ptr)) {
	if (ptr->key.content->path->getlo() == name) {
	  rps = dmo.maybe_union(v1->dps, rps);
	  DepPath key1;
	  key1.DeepCopy(ptr->key);
	  key1.content->path->remlo();
	  newPath = CollectDpnd(v1, &key1, rps, dmo);
	  if (newPath != NULL) {
	    DepPathTbl::KVPairPtr pr;
	    (void)rps->Put(*newPath, ptr->val, pr);
	    // assert(ptr->val->FingerPrint() == pr->val->FingerPrint());
	  }
	}
	else
	  (void)rps->Put(ptr);
      }
    }
  }
  // Collect dependency of v2->path:
  DepPath *dp = v2->path;
  Val v3 = v2->Copy(true);
  v3->dps = (found) ? rps : v2->dps;
  rps = NULL;   // No longer needed.
  if (dp != NULL) {
    v3->path = dp;
    if (dp->content->path->getlo() == name) {
      v3->MergeDPS(v1->dps, dmo);
      newPath = dp->DeepCopy();
      newPath->content->path->remlo();
      if (v3->dps == NULL)
	v3->dps = NEW(DPaths);
      v3->path = CollectDpnd(v1, newPath, v3->dps, dmo, false); 
    }
    return v3;
  }
  /* We get here only when v2->path is NULL. So we recursively
     collect dependency for subvalues of v2.  */
  return CollectLetS(name, v1, v3, dmo);
}

Val LetDpnd(Val v, const Context& c) {
  Context work = c;
  Assoc a;

  while (!work.Null()) {
    a = work.Pop();
    // This data structure helps us save time and memory by not
    // redundantly copying the same set of dependencies into multiple
    // places
    DepMergeOptimizer dmo;
    v = CollectLet(a->name, a->val, v, dmo);
    /* After each round of collecting dependency, we try to make
       the dependency compact by:
        1. Delete any duplicate copies of any dependency path.
        2. Promote shared dependency paths.  */
    DeleteDuplicatePaths(v);
    CanonicalDpnd(v);
  }
  return v;
}

Val CollectFuncS(Val bodyv, Val fv, const Context& c, DepMergeOptimizer &dmo) {
  /* We only need to do something for composite values. */
  switch (bodyv->vKind) {
  case BindingVK:
    {
      dmo.push_enclosing(bodyv);
      BindingVC *bv = (BindingVC*)bodyv;
      Context rc, work = bv->elems;
      while (!work.Null()) {
	Assoc a = work.Pop();
	a = NEW_CONSTR(AssocVC, (a->name, CollectFunc(a->val, fv, c, dmo)));
	rc.Append1D(a);
      }
      bv->elems = rc;
      DPaths *newLenDps;
      CollectFunc(bv->lenDps, fv, c, bv, newLenDps, dmo);
      bv->lenDps = newLenDps;
      dmo.pop_enclosing(bodyv);
      return bv;
    }
  case ListVK:
    {
      dmo.push_enclosing(bodyv);
      ListVC *lstv = (ListVC*)bodyv;
      Vals res, elems = lstv->elems;
      while (!elems.Null())
	res.Append1D(CollectFunc(elems.Pop(), fv, c, dmo));
      lstv->elems = res;
      DPaths *newLenDps;
      CollectFunc(lstv->lenDps, fv, c, lstv, newLenDps, dmo);
      lstv->lenDps = newLenDps;
      dmo.pop_enclosing(bodyv);
      return lstv;
    }
  case ClosureVK:
    {
      dmo.push_enclosing(bodyv);
      ClosureVC *cl = (ClosureVC*)bodyv;
      Context rc, work = cl->con;
      while (!work.Null()) {
	Assoc a = work.Pop();
	if (a->name != cl->func->name)
	  a = NEW_CONSTR(AssocVC, (a->name, CollectFunc(a->val, fv, c, dmo)));
	rc.Append1D(a);
      }
      cl->con = rc;
      dmo.pop_enclosing(bodyv);
      return cl;
    }
  default:
    break;
  }
  return bodyv;
}

Val CollectFunc(Val bodyv, Val fv, const Context& c, DepMergeOptimizer &dmo) {
  Text name;
  DepPath *newPath = NULL;
  bool fromArgsCon;

  // argsCon1 are arguments which have an identical path in the
  // calling scope.  This allows us to avoid rewriting some dependency
  // paths.

  // argsCon2 is all other arguments.
  Context argsCon1, argsCon2;
  Context work = c;
  while (!work.Null()) {
    Assoc a = work.Pop();
    if (a->val->path != NULL &&
	a->val->path->Size() == 1 &&
	a->name == a->val->path->content->path->getlo())
      argsCon1.Push(a);
    else
      argsCon2.Push(a);
  }

  DPaths *rps = NEW(DPaths);
  // Path dependency for bodyv:
  if (bodyv->dps != NULL) {
    DepPathTbl::TIter iter(bodyv->dps);
    DepPathTbl::KVPairPtr ptr;
    DepPath key1;
    while (iter.Next(ptr)) {
      name = ptr->key.content->path->getlo();
      fromArgsCon = false;
      work = argsCon1;
      while (!fromArgsCon && !work.Null()) {
	Assoc a = work.Pop();
	if (a->name == name) {
	  rps = dmo.maybe_union(a->val->dps, rps);
	  // This argument's value has the same name in the calling
	  // scope, so we can just copy the dependency path as-is.
	  newPath = NULL;
	  (void)rps->Put(ptr);
	  fromArgsCon = true;
	}
      }
      work = argsCon2;
      while (!fromArgsCon && !work.Null()) {
	Assoc a = work.Pop();
	if (a->name == name) {
	  rps = dmo.maybe_union(a->val->dps, rps);
	  key1.DeepCopy(ptr->key);
	  key1.content->path->remlo();
	  newPath = CollectDpnd(a->val, &key1, rps, dmo);
	  fromArgsCon = true;
	}
      }
      if (!fromArgsCon) {
	key1.DeepCopy(ptr->key);
	if (((ClosureVC*)fv)->func->name == name) {
	  // The function fv is calling itself recursively!
	  key1.content->path->remlo();
	}
	newPath = CollectDpnd(fv, &key1, rps, dmo);
      }
      if (newPath != NULL) {
	DepPathTbl::KVPairPtr pr;
	(void)rps->Put(*newPath, ptr->val, pr);
	// assert(ptr->val->FingerPrint() == pr->val->FingerPrint());
      }
    }
  }
  
  // Dependency for function body:
  rps = dmo.maybe_union(fv->dps, rps);
  rps = rps->Add(fv->path, fv, ExprPK);
  
  // Dependency for path of bodyv:
  DepPath *dp = bodyv->path;
  Val bodyv1 = bodyv->Copy(true);
  bodyv1->dps = rps;
  bodyv1->path = dp;
  if (dp != NULL) {
    name = dp->content->path->getlo();
    fromArgsCon = false;
    work = argsCon1;
    while (!fromArgsCon && !work.Null()) {
      Assoc a = work.Pop();
      if (a->name == name) {
	bodyv1->MergeDPS(a->val->dps, dmo);
	fromArgsCon = true;
      }
    }
    work = argsCon2;
    while (!fromArgsCon && !work.Null()) {
      Assoc a = work.Pop();
      if (a->name == name) {
	bodyv1->MergeDPS(a->val->dps, dmo);
	newPath = dp->DeepCopy();
	newPath->content->path->remlo();
	bodyv1->path = CollectDpnd(a->val, newPath, bodyv1->dps, dmo, false);
	fromArgsCon = true;
      }
    }
    if (!fromArgsCon) {
      newPath = dp->DeepCopy();
      if (((ClosureVC*)fv)->func->name == name) {
	// This function is recursive!
	newPath->content->path->remlo();
      }
      bodyv1->path = CollectDpnd(fv, newPath, bodyv1->dps, dmo, false);
    }
    return bodyv1;
  }

  /* Structural dependency for bodyv.
     We get here only when bodyv->path is NULL.  So we recursively
     collect dependency for subvalues of v2.  */
  return CollectFuncS(bodyv1, fv, c, dmo);
}

Val FuncDpnd(Val bodyv, Val fv, const Context& c) {
  DepMergeOptimizer dmo;
  return CollectFunc(bodyv, fv, c, dmo);
}

Val CollectModelS(Val bodyv, Val fv, const Context& c, DepMergeOptimizer &dmo) {
  /* Only for composite values. */
  switch (bodyv->vKind) {
  case BindingVK:
    {
      dmo.push_enclosing(bodyv);
      BindingVC *bv = (BindingVC*)bodyv;
      Context rc, work = bv->elems;
      while (!work.Null()) {
	Assoc a = work.Pop();
	a = NEW_CONSTR(AssocVC, (a->name, CollectModel(a->val, fv, c, dmo)));
	rc.Append1D(a);
      }
      bv->elems = rc;
      DPaths *newLenDps;
      CollectModel(bv->lenDps, c, bv, newLenDps, dmo);
      bv->lenDps = newLenDps;
      dmo.pop_enclosing(bodyv);
      return bv;
    }
  case ListVK:
    {
      dmo.push_enclosing(bodyv);
      ListVC *lstv = (ListVC*)bodyv;
      Vals res, elems = lstv->elems;
      while (!elems.Null())
	res.Append1D(CollectModel(elems.Pop(), fv, c, dmo));
      lstv->elems = res;
      DPaths *newLenDps;
      CollectModel(lstv->lenDps, c, lstv, newLenDps, dmo);
      lstv->lenDps = newLenDps;
      dmo.pop_enclosing(bodyv);
      return lstv;
    }
  case ClosureVK:
    {
      dmo.push_enclosing(bodyv);
      ClosureVC *cl = (ClosureVC*)bodyv;
      Context rc, work = cl->con;
      while (!work.Null()) {
	Assoc a = work.Pop();
	if (a->name != cl->func->name)
	  a = NEW_CONSTR(AssocVC, (a->name, CollectModel(a->val, fv, c, dmo)));
	rc.Append1D(a);
      }
      cl->con = rc;
      dmo.pop_enclosing(bodyv);
      return cl;
    }
  default:
    break;
  }
  return bodyv;
}

Val CollectModel(Val bodyv, Val fv, const Context& c, DepMergeOptimizer &dmo) {
  DepPath *newPath = NULL;
  bool merged = false;
  // assert((c.Length() == 1) && (c.Nth(0)->name == nameDot))
  Val dotVal = c.Nth(0)->val;
  DPaths *rps = NEW(DPaths);

  // Path dependency for v:
  if (bodyv->SizeOfDPS() != 0) {
    DepPathTbl::TIter iter(bodyv->dps);
    DepPathTbl::KVPairPtr ptr;
    DepPath key1;
    while (iter.Next(ptr)) {
      assert(ptr->key.content->path->getlo() == nameDot);
      rps = dmo.maybe_union(dotVal->dps, rps);
      key1.DeepCopy(ptr->key);
      key1.content->path->remlo();
      newPath = CollectDpnd(dotVal, &key1, rps, dmo);
      if (newPath != NULL) {
	DepPathTbl::KVPairPtr pr;
	(void)rps->Put(*newPath, ptr->val, pr);
	// assert(ptr->val->FingerPrint() == pr->val->FingerPrint());
      }
    }
  }
  // Dependency for function body:
  rps = rps->Union(fv->dps);
  rps = rps->Add(fv->path, fv, ExprPK);  // redundant, but added for caching
  rps = rps->Add(fv->path, fv, NormPK);

  // Dependency for path of bodyv:
  DepPath *dp = bodyv->path;
  Val bodyv1 = bodyv->Copy(true);
  bodyv1->dps = rps;
  if (dp != NULL) {
    newPath = NULL;
    assert(dp->content->path->getlo() == nameDot);
    bodyv1->MergeDPS(dotVal->dps, dmo);
    newPath = dp->DeepCopy();
    newPath->content->path->remlo();
    newPath = CollectDpnd(dotVal, newPath, bodyv1->dps, dmo, false);
    bodyv1->path = newPath;
    return bodyv1;
  }
  /* We get here only when bodyv->path is NULL.  So we now collect
     dependency for subvalues of v2.  */
  return CollectModelS(bodyv1, fv, c, dmo);
}

Val ModelDpnd(Val bodyv, Val fv, const Context& c) {
  DepMergeOptimizer dmo;
  return CollectModel(bodyv, fv, c, dmo);
}

void CollectLet(const Text& name, Val v1, DPaths *ps, Val res,
		/*OUT*/ DPaths*& nps, DepMergeOptimizer &dmo) {
  // Control expr of foreach:
  if (name == emptyText) {
    // See the function IterateAssoc for the meaning of v1.
    res->MergeDPS(v1->dps)->MergeDPS(ps);
    return;
  }
  // Trivial cases:
  if (ps == NULL || ps->Size() == 0) {
    nps = NULL;
    return;
  }
  // Normal case:
  bool found = false;
  DepPath *newPath = NULL;
  found = ps->ContainsPrefix(name);
  if (!found) {
    nps = ps;
  }
  else {
    nps = NEW(DPaths);
    if (res->dps == NULL)
      res->dps = NEW(DPaths);
    DPaths *rps = res->dps;
    DepPathTbl::TIter iter(ps);
    DepPathTbl::KVPairPtr ptr;
    while (iter.Next(ptr)) {
      if (ptr->key.content->path->getlo() != name)
	(void)nps->Put(ptr);
      else {
	rps = dmo.maybe_union(v1->dps, rps);
	DepPath key1;
	key1.DeepCopy(ptr->key);
	key1.content->path->remlo();
	newPath = CollectDpnd(v1, &key1, rps, dmo);
	if (newPath != NULL) {
	  DepPathTbl::KVPairPtr pr;
	  nps->Put(*newPath, ptr->val, pr);
	  // assert(ptr->val->FingerPrint() == pr->val->FingerPrint());
	}
      }
    }
  }
  if (nps->Size() == 0) nps = NULL;
  return;
}

void CollectFunc(DPaths *ps, Val fv, const Context& c, Val res,
		 /*OUT*/ DPaths*& nps, DepMergeOptimizer &dmo) {
  // Trivial cases:
  if (ps == NULL || ps->Size() == 0) {
    nps = NULL;
    return;
  }
  // Normal case:
  DepPath *newPath = NULL;
  Text name;
  bool fromArgsCon;
  nps = NEW(DPaths);
  if (res->dps == NULL)
    res->dps = NEW(DPaths);
  DPaths *rps = res->dps;
  DepPathTbl::TIter iter(ps);
  DepPathTbl::KVPairPtr ptr;
  DepPath key1;
  while (iter.Next(ptr)) {
    name = ptr->key.content->path->getlo();
    Context work = c;
    fromArgsCon = false;
    while (!fromArgsCon && !work.Null()) {
      Assoc a = work.Pop();
      if (a->name == name) {
	rps = dmo.maybe_union(a->val->dps, rps);
	key1.DeepCopy(ptr->key);
	key1.content->path->remlo();
	newPath = CollectDpnd(a->val, &key1, rps, dmo);
	fromArgsCon = true;
      }
    }
    if (!fromArgsCon) {
      key1.DeepCopy(ptr->key);
      if (((ClosureVC*)fv)->func->name == name) {
	key1.content->path->remlo();
      }
      rps = dmo.maybe_union(fv->dps, rps);
      newPath = CollectDpnd(fv, &key1, rps, dmo);
    }
    if (newPath != NULL) {
      DepPathTbl::KVPairPtr pr;
      nps->Put(*newPath, ptr->val, pr);
      // assert(ptr->val->FingerPrint() == pr->val->FingerPrint());
    }
  }
  if (nps->Size() == 0) nps = NULL;
  return;
}

void CollectModel(DPaths *ps, const Context& c, Val res,
		  /*OUT*/ DPaths*& nps, DepMergeOptimizer &dmo) {
  DepPath *newPath = NULL;
  // assert((c.Length() == 1) && (c.Nth(0)->name == nameDot))

  if (ps == NULL)
    nps = NULL;
  else {
    Val dotVal = c.Nth(0)->val;
    nps = NEW(DPaths);
    if (res->dps == NULL)
      res->dps = NEW(DPaths);
    DPaths *rps = res->dps;
    DepPathTbl::TIter iter(ps);
    DepPathTbl::KVPairPtr ptr;
    DepPath key1;
    while (iter.Next(ptr)) {
      assert(ptr->key.content->path->getlo() == nameDot);
      rps = dmo.maybe_union(dotVal->dps, rps);
      key1.DeepCopy(ptr->key);
      key1.content->path->remlo();
      newPath = CollectDpnd(dotVal, &key1, rps, dmo);
      if (newPath != NULL) {
	DepPathTbl::KVPairPtr pr;
	nps->Put(*newPath, ptr->val, pr);
	// assert(ptr->val->FingerPrint() == pr->val->FingerPrint());
      }
    }
  }
  return;
}

// Pretty-printing a value.
Text PrintForm(Val v) {
  OBufStream stream;
  v->PrintD(stream);
  return Text(stream.str());
}

// Lookup in context.
Assoc FindInContext(const Text& id, const Context& c) {
  Context work = c;

  while (!work.Null()) {
    Assoc a = work.Pop();
    if (a->name == id) return a;
  }
  return nullAssoc;
}

Val LookupInContext(const Text& id, const Context& c) {
  return FindInContext(id, c)->val;
}

// For performance, we use the LookupNoDpnd methods for LookupPath. 
// They return the same value as Lookup methods, but do not record
// dependency.
Val LookupArc(const Text& arc, Val v) {
  switch (v->vKind) {
  case BindingVK:
    {
      int n = ArcInt(arc);
      if (n == -1)
	v = ((BindingVC*)v)->LookupNoDpnd(arc);
      else {
	Text arc1;
	v = ((BindingVC*)v)->GetElemNoDpnd(n, arc1);
      }
      break;
    }
  case ListVK:
    {
      int n = ArcInt(arc);
      if (n == -1) return valUnbnd;
      v = ((ListVC*)v)->GetElemNoDpnd(n);      
      break;
    }
  case ClosureVK:
    v = LookupInContext(arc, ((ClosureVC*)v)->con);
    break;
  default:
    // cannot do a lookup with simple value.
    return valUnbnd;
  }
  return v;
}

Val LookupPath(const FV2::T& path, const Context& c) {
  /* The function is used to compute the values of the free variables
     in the current context.  The 0-th element of path is the kind of
     the path. See cache lookup.  */
  int len = path.size();
  Val result;

  // figure out the path kind:
  PathKind pkind = (PathKind)path.get(0).chars()[0];

  // lookup for the first arc:
  result = LookupInContext(path.get(1), c);
  if (result->vKind == UnbndVK) return result;

  // lookup for the remaining arcs:
  if (pkind == BangPK) {
    for (int i = 2; i < (len - 1); i++) {
      result = LookupArc(path.get(i), result);
      if (result->vKind == UnbndVK) return result;
    }
    if (result->vKind != BindingVK)
      return valUnbnd;
    return ((BindingVC*)result)->DefinedNoDpnd(path.get(len-1)) ? valTrue : valFalse;
  }
  for (int i = 2; i < len; i++) {
    result = LookupArc(path.get(i), result);
    if (result->vKind == UnbndVK) return result;
  }

  // Special treatment for the following kinds of path:
  switch (pkind) {
  case BLenPK:
    if (result->vKind != BindingVK) return valUnbnd;
    return ((BindingVC*)result)->Names();
  case LLenPK:
    if (result->vKind != ListVK) return valUnbnd;
    return ((ListVC*)result)->Length();
  case TypePK:
    return ValType(result);
  case ExprPK:
    if (result->vKind == ClosureVK)
      result = NEW_CONSTR(FpVC, (((ClosureVC*)result)->FingerPrintExpr()));
    else if (result->vKind == ModelVK)
      result = NEW_CONSTR(FpVC, (((ModelVC*)result)->FingerPrintFile()));
    else
      result = valUnbnd;
    return result;
  default:
    return result;
  }
  // return result; // not reached
}

Val LookupPath(DepPath *dp, const Context& c) {
  /* The function is used in Unpickle to restore the fingerprint for
     DPS's and the value for path and LenDPS's. See Unpickle. */
  PathKind pkind = dp->content->pKind;
  ArcSeq *path = dp->content->path;
  int len = path->size();

  // Lookup for the first arc. It must be bound in the context.
  Val result = LookupInContext(path->get(0), c);

  // lookup for the remaining arcs:
  if (pkind == BangPK) {
    for (int i = 1; i < len-1; i++) {
      assert(result->vKind != UnbndVK);
      result = LookupArc(path->get(i), result);
    }
    assert(result->vKind == BindingVK);
    return ((BindingVC*)result)->DefinedNoDpnd(path->get(len-1)) ? valTrue : valFalse;
  }
  for (int i = 1; i < len; i++) {
    assert(result->vKind != UnbndVK);
    result = LookupArc(path->get(i), result);
  }
  
  // Special treatment for the following kinds of path:
  switch (pkind) {
  case BLenPK:
    assert(result->vKind == BindingVK);
    return ((BindingVC*)result)->Names();
  case LLenPK:
    assert(result->vKind == ListVK);
    return ((ListVC*)result)->Length();
  case TypePK:
    return ValType(result);
  case ExprPK:
    if (result->vKind == ClosureVK)
      result = NEW_CONSTR(FpVC, (((ClosureVC*)result)->FingerPrintExpr()));
    else {
      assert(result->vKind == ModelVK);
      result = NEW_CONSTR(FpVC, (((ModelVC*)result)->FingerPrintFile()));
    }
    return result;
  default:
    return result;
  }
  // return result; // not reached
}

Val Lookup(Basics::uint32 idx, const PrefixTbl& tbl,
	   const Context& c, Val* vals) {
  Val res;
  if (vals[idx] == NULL) {
    if (tbl.PrefixIndex(idx) == PrefixTbl::endMarker) {
      res = LookupInContext(tbl.Arc(idx), c);
      vals[idx] = res;
    }
    else {
      res = Lookup(tbl.PrefixIndex(idx), tbl, c, vals);
      if (res->vKind == UnbndVK) {
	vals[idx] = valUnbnd;
      }
      else {
	res = LookupArc(tbl.Arc(idx), res);
	vals[idx] = res;
      }
    }
  }
  else {
    res = vals[idx];
  }
  return res;
}

Val LookupPath(Basics::uint32 idx, PathKind pkind, const PrefixTbl& tbl,
	       const Context& c, Val *vals) {
  Val result;
  if (pkind == BangPK) {
    Basics::uint32 idx1 = tbl.PrefixIndex(idx);
    result = Lookup(idx1, tbl, c, vals);
    if (result->vKind == BindingVK)
      result = ((BindingVC*)result)->DefinedNoDpnd(tbl.Arc(idx)) ? valTrue : valFalse;
    else
      result = valUnbnd;
  }
  else {
    result = Lookup(idx, tbl, c, vals);
    switch (pkind) {
    case BLenPK:
      if (result->vKind == BindingVK)
	result = ((BindingVC*)result)->Names();
      else
	result = valUnbnd;
      break;
    case LLenPK:
      if (result->vKind == ListVK)
	result = ((ListVC*)result)->Length();
      else
	result = valUnbnd;
      break;
    case TypePK:
      result = ValType(result);
      break;
    case ExprPK:
      if (result->vKind == ClosureVK)
	result = NEW_CONSTR(FpVC, (((ClosureVC*)result)->FingerPrintExpr()));
      else if (result->vKind == ModelVK)
	result = NEW_CONSTR(FpVC, (((ModelVC*)result)->FingerPrintFile()));
      else
	result = valUnbnd;
      break;
    default:
      break;
    }
  }
  return result;
}

void AppendDToContext(const Text& name, Expr elem, const Context& cElem, 
		     Context& cc) {
  // Val v = elem->Eval(cElem);
  Val v = elem->Eval(RestrictContext(cElem, elem->freeVars));
  cc.Append1D(NEW_CONSTR(AssocVC, (name, v)));
}

void PushToContext(const Text& name, Expr elem, const Context& cElem,
		   Context& in) {
  // Val v = elem->Eval(cElem);
  Val v = elem->Eval(RestrictContext(cElem, elem->freeVars));
  in.Push(NEW_CONSTR(AssocVC, (name, v)));
}

Context RestrictContext(const Context& con, Vars fv) {
  Context result;

  while (!fv.Null()) {
    Text id(fv.Pop());
    Assoc a = FindInContext(id, con);
    if (a != nullAssoc) result.Append1D(a);
  }
  return result;
}

Context Snip(const Context& orig, const Text& name, bool& found) {
  Context result;
  Context work = orig;
  Assoc a;

  found = false;
  while (!work.Null()) {
    a = work.Pop();
    if (a->name == name) {
      found = true;
      break;
    }
    result.Append1D(a);
  }
  if (!found) return orig;
  while (!work.Null()) {
    a = work.Pop();
    if (a->name != name)
      result.Append1D(a);
  }
  return result;
}

Context Prune(const Context& orig, const Context& remove) {
  if (remove.Null()) return orig;

  Context work = orig;
  Context result;
  while (!work.Null()) {
    Assoc a = work.Pop();
    if (FindInContext(a->name, remove) == nullAssoc)
      result.Append1D(a);
  }
  return result;
}

void AssignAssoc(AssignEC *ae, Context& con) {
  Text name(ae->lhs->id);

  if (ae->isFunc) {
    FuncEC *e = (FuncEC*)ae->rhs;
    ClosureVC *cl = NEW_CONSTR(ClosureVC, (e, con, true));
    AssocVC* av = NEW_CONSTR(AssocVC, (name, cl));
    con.Push(av);
    cl->con = RestrictContext(Context(av, cl->con), e->freeVars);
  }
  else {
    Expr e = ae->rhs;
    ExprKind ek = e->kind;
    if (ek != BaseTEK && ek != ListTEK &&
	ek != BindingTEK && ek != FuncTEK) {
      Val v = e->Eval(RestrictContext(con, e->freeVars));
      con.Push(NEW_CONSTR(AssocVC, (name, v)));
    }
  }
}

void IterateAssoc(IterateEC *it, Context& c) {
  Expr control = it->control, e = it->e;
  StmtListEC *body = it->body;
  Val v = e->Eval(c), v1;
  Text newName = GetUniqueName();
  c.Push(NEW_CONSTR(AssocVC, (newName, v)));

  if (control->kind == NameEK) {
    Text id(((Name)control)->id);
    if (v->vKind == ListVK) {
      int len = ((ListVC*)v)->elems.Length();
      v1 = NEW_CONSTR(IntegerVC, (len));
      FpVC *fpv = NEW_CONSTR(FpVC, (v1->FingerPrint()));
      v1->AddToDPS(NEW_CONSTR(DepPath, (newName)), fpv, LLenPK);
      c.Push(NEW_CONSTR(AssocVC, (emptyText, v1)));
      Vals vlist = ((ListVC*)v)->elems;
      int i = 0;
      while (!vlist.Null()) {
	v1 = vlist.Pop()->Copy();
	v1->path = NEW_CONSTR(DepPath, (newName));
	v1->path->Extend(IntArc(i++), NormPK);
	c.Push(NEW_CONSTR(AssocVC, (id, v1)));
	c = AddStmtAssocs(body, c);
      }
    }
    else {
      c.Push(NEW_CONSTR(AssocVC, (emptyText, v)));
      ostream& err_stream = cio().start_err();
      Error(err_stream, "Control expression doesn't evaluate to list.\n", e->loc);
      ErrorVal(err_stream, v);
      cio().end_err();
      throw(Evaluator::failure(Text("exiting"), false));
    }
  }
  else {
    // control is PairEK:
    PairEC *pair = (PairEC*)control;
    Text id1(((Name)(pair->first))->id);
    Text id2(((Name)(pair->second))->id);
    if (v->vKind == BindingVK) {
      BindingVC *bv = (BindingVC*)v;
      int len = bv->elems.Length();
      v1 = NEW_CONSTR(IntegerVC, (len));
      v1->AddToDPS(NEW_CONSTR(DepPath, (newName)), bv->Names(), BLenPK);
      c.Push(NEW_CONSTR(AssocVC, (emptyText, v1)));
      Context es = ((BindingVC*)v)->elems;
      while (!es.Null()) {
	Assoc a = es.Pop();
	c.Push(NEW_CONSTR(AssocVC, (id1, NEW_CONSTR(TextVC, (a->name)))));
	v1 = a->val->Copy();
	v1->path = NEW_CONSTR(DepPath, (newName));
	v1->path->Extend(a->name, NormPK);
	c.Push(NEW_CONSTR(AssocVC, (id2, v1)));
	c = AddStmtAssocs(body, c);
      }
    }
    else {
      c.Push(NEW_CONSTR(AssocVC, (emptyText, v)));
      ostream& err_stream = cio().start_err();
      Error(err_stream, "Control expression doesn't evaluate to binding.\n", e->loc);
      ErrorVal(err_stream, v);
      cio().end_err();
      throw(Evaluator::failure(Text("exiting"), false));
    }
  }
  return;
}

Context AddStmtAssocs(StmtListEC *assocs, const Context& c) {
  Context cc = c;
  Exprs els = assocs->elems;
  
  for (int i = 0; i < els.size(); i++) {
    Expr e = els.get(i);
    switch (e->kind) {
    case TypedEK:
      // Type-checking: to be added.
      break;
    case AssignEK:
      AssignAssoc((AssignEC*)e, cc);
      break;
    case IterateEK:
      IterateAssoc((IterateEC*)e, cc);
      break;
    case TryEK:
      e->Eval(cc);
      break;
    default:
      ostream& err_stream = cio().start_err();
      Error(err_stream, "Invalid statement:\n`", e->loc);
      ErrorExpr(err_stream, e);
      ErrorDetail(err_stream, "'.");
      cio().end_err();
    }
  }
  return cc;
}

extern "C" void ValInit_inner() {
  conInitial.Push(NEW_CONSTR(AssocVC, (nameDot, NEW(BindingVC))));
}

void ValInit()
{
  static pthread_once_t init_once = PTHREAD_ONCE_INIT;
  pthread_once(&init_once, ValInit_inner);
}
