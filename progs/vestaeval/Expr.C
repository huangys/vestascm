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

// File: Expr.C

#include "Expr.H"
#include "ApplyCache.H"
#include "Files.H"
#include "Debug.H"
#include "ThreadData.H"
#include <FPStream.H>

using std::ostream;
using std::endl;
using Basics::OBufStream;
using OS::cio;
using FP::FPStream;

// Constants:
const char *nameDot = ".";

//// ExprC:
// Eval returns the value and dependency of evaluating an expression.
// Each subclass of ExprC should override Eval. 
void ExprC::PrintExprVars(ostream& os) 
{
  os << endl << "freeVars =";
  PrintVars(os, freeVars);
}

//// Expr subclasses:
// ConstantEC
Val ConstantEC::Eval(const Context& c) {
  // assert(!val->path && (val->dps->Size() == 0))
  // Must copy here!
  return this->val->Copy();
}

// IfEC
IfEC::IfEC(Expr tcond, Expr tthen, Expr tels, SrcLoc *tloc) 
: ExprC(IfEK, tloc), test(tcond), then(tthen), els(tels) {
  freeVars  = AddVars(test->freeVars, 
		      AddVars(els->freeVars, then->freeVars));
}

void IfEC::PrintD(ostream& os) 
{
  os << "(";
  os << "if "; test->PrintD(os);
  os << endl << " then "; then->PrintD(os);
  os << endl << " else "; els->PrintD(os);
  os << ")";
}

Val IfEC::Eval(const Context& c) {
  Val result, b = this->test->Eval(c);

  if (IsValTrue(b)) {
    result = this->then->Eval(c)->Merge(b);
    result->cacheit = result->cacheit && b->cacheit;
  }
  else if (IsValFalse(b)) {
    result = this->els->Eval(c)->Merge(b);
    result->cacheit = result->cacheit && b->cacheit;
  }
  else {
    this->EError(cio().start_err(), 
		 "The test of an if-expression must be boolean.\n");
    cio().end_err();
    result = RecordErrorOnStack(this);
    result->MergeDPS(b->dps);
    result->AddToDPS(b->path, ValType(b), TypePK);
  }
  return result;
}

// ComputedEC
void ComputedEC::PrintD(ostream& os) 
{
  os << '%';
  name->PrintD(os);
  os << '%';
}

Val ComputedEC::Eval(const Context& c) {
  Val v = this->name->Eval(c);

  if (v->vKind == TextVK) return v;
  this->EError(cio().start_err(), "Expected a text value. \n");
  cio().end_err();
  Val err = RecordErrorOnStack(this);
  err->MergeDPS(v->dps);
  return err->AddToDPS(v->path, ValType(v), TypePK);
}

// ExprListEC
void ExprListEC::PrintD(ostream& os) 
{
  int n = elems.size() - 1;
  for (int i = 0; i < n; i++) {
    elems.get(i)->PrintD(os);
    os << "; ";
  }
  if (n >= 0)
    elems.get(n)->PrintD(os);
}

void ExprListEC::AddExpr(Expr telem) {
  this->elems.addhi(telem);
  freeVars = AddVars(freeVars, telem->freeVars);
}

Val ExprListEC::Eval(const Context& c) {
  Vals vals;

  for (int i = 0; i < elems.size(); i++) {
    Val elem = elems.get(i)->Eval(c);
    vals.Append1D(elem);
  }
  Val result = NEW_CONSTR(ListVC, (vals));
  return result;
}

// ArgListEC
void ArgListEC::PrintD(ostream& os) 
{
  int n = elems.size() - 1;

  for (int i = 0; i < n; i++) {
    elems.get(i)->PrintD(os);
    os << ", ";
  }
  if (n >= 0)
    elems.get(n)->PrintD(os);
}

void ArgListEC::AddExpr(Expr telem, bool inPK) {
  if (inPK) {
    inPKs |= ((Bit64)1 << elems.size());
  }
  this->elems.addhi(telem);
  freeVars = AddVars(freeVars, telem->freeVars);
}

// StmtListEC
void StmtListEC::PrintD(ostream& os) 
{
  int n = elems.size() - 1;

  for (int i = 0; i < n; i++) {
    this->elems.get(i)->PrintD(os);
    os << ";" << endl;
  }
  if (n >= 0)
    this->elems.get(n)->PrintD(os);
}

void StmtListEC::AddExpr(Expr telem) {
  this->elems.addhi(telem);
  freeVars = AddVars(freeVars, Remove(telem->freeVars, boundVars));
  if (telem->kind == AssignEK)
    boundVars = Vars(((AssignEC*)telem)->lhs->id, boundVars);
  else if (telem->kind == IterateEK)
    boundVars = AddVars(boundVars, ((IterateEC*)telem)->body->boundVars);
  // assert(telem->kind == EvalEK)
}

// ListEC
void ListEC::PrintD(ostream& os) 
{
  os << "< ";

  int n = this->elems.size() - 1;
  for (int i = 0; i < n; i++) {
    this->elems.get(i)->PrintD(os);
    os << ", ";
  }
  if (n >= 0)
    this->elems.get(n)->PrintD(os);

  os << " >";
}

Val ListEC::Eval(const Context& c) {
  Vals vals;
  
  for (int i = 0; i < elems.size(); i++) {
    Val elem = elems.get(i)->Eval(c);
    vals.Append1D(elem);
  }
  Val result = NEW_CONSTR(ListVC, (vals));
  return result;
}

void ListEC::AddExpr(Expr telem) {
  this->elems.addhi(telem);
  freeVars = AddVars(freeVars, telem->freeVars);
}

// AssignEC
AssignEC::AssignEC(Name tlhs, Expr trhs, bool tfunc, SrcLoc *tloc)
: ExprC(AssignEK, tloc), lhs(tlhs), rhs(trhs), isFunc(tfunc) {
  freeVars = (tfunc) ? Remove(rhs->freeVars, lhs->id) : rhs->freeVars;
}

void AssignEC::PrintD(ostream& os) 
{
  lhs->PrintD(os);
  if (!this->isFunc) os << " = ";
  rhs->PrintD(os);
}

// BindEC
BindEC::BindEC(Expr tlhs, Expr trhs, SrcLoc *tloc)
: ExprC(BindEK, tloc), lhs(tlhs), rhs(trhs) {
  freeVars = AddVars(lhs->freeVars, rhs->freeVars);
}

void BindEC::PrintD(ostream& os) 
{
  lhs->PrintD(os);
  os << "=";
  rhs->PrintD(os);
}

// NameEC
Val NameEC::Eval(const Context& c) {
  Val v = LookupInContext(id, c);

  if (v->vKind == UnbndVK) {
    Text error_msg = Text("\nUnbound variable `") + id +
      "' encountered during evaluation in context:\n";
    ostream& err_stream = cio().start_err();
    this->EError(err_stream, error_msg);
    PrintContext(err_stream, c);
    ErrorDetail(err_stream, ".\n");
    cio().end_err();
    Val err = RecordErrorOnStack(this);
    return err->AddToDPS(NEW_CONSTR(DepPath, (id)), valFalse, BangPK);
  }
  Val result = v->Copy(true);
  result->path = NEW_CONSTR(DepPath, (id));
  return result;
}

// BindingEC
void BindingEC::PrintD(ostream& os) 
{
  os << "[ ";
  int n = this->assocs.size() - 1;
  for (int i = 0; i < n; i++) {
    this->assocs.get(i)->PrintD(os);
    os << ", ";
  }
  if (n >= 0)
    this->assocs.get(n)->PrintD(os);
  os << " ]";
}

Val BindingEC::Eval(const Context& c) {
  Text name;
  Val computed, result;
  Context bb;
  Vals vv;
  bool cacheit = true;

  for (int i = 0; i < assocs.size(); i++) {
    BindEC *a = (BindEC*)(assocs.get(i));
    // Get the field name:
    switch (a->lhs->kind) {
    case NameEK:
      name = ((Name)a->lhs)->id;
      computed = NULL;
      break;
    case ComputedEK:
      computed = a->lhs->Eval(c);
      if (computed->vKind != TextVK) {
	ostream& err_stream = cio().start_err();
	this->EError(err_stream, 
		     "Field name in binding must evaluate to text: `");
	ErrorExpr(err_stream, a->lhs);
	ErrorDetail(err_stream, "'.\n");
	cio().end_err();
	result = RecordErrorOnStack(a->lhs);
	result->MergeDPS(computed->dps);
	return result->AddToDPS(computed->path, ValType(computed), TypePK);
      }
      name = ((TextVC*)computed)->NDS();
      cacheit = cacheit && computed->cacheit;
      vv.Push(computed);
      break;
    default:
      ostream& err_stream = cio().start_err();
      this->EError(err_stream, "Field name in binding has wrong type: `");
      ErrorExpr(err_stream, a->lhs);
      ErrorDetail(err_stream, "'.\n");
      cio().end_err();
      result = RecordErrorOnStack(a->lhs);
      return result;
    }
    if (name.Empty()) {
      a->EError(cio().start_err(), "Empty field name in binding.");
      cio().end_err();
      result = RecordErrorOnStack(a);
      return (computed ? result->Merge(computed) : result);
    }
    if (FindInContext(name, bb) != nullAssoc) {
      this->EError(cio().start_err(), "Field name conflicts in binding.");
      cio().end_err();
      result = RecordErrorOnStack(this);
      while (!vv.Null()) result->Merge(vv.Pop());
      return result;
    }
    AppendDToContext(name, a->rhs, c, bb);
  }
  result = NEW_CONSTR(BindingVC, (bb));
  while (!vv.Null()) {
    result->Merge(vv.Pop());
  }
  result->cacheit = cacheit;
  return result;
}

void BindingEC::AddExpr(Expr telem) {
  this->assocs.addhi(telem);
  freeVars = AddVars(freeVars, telem->freeVars);
}

// ApplyOpEC
ApplyOpEC::ApplyOpEC(Expr te1, const Text& top, Expr te2, SrcLoc *tloc)
: ExprC(ApplyOpEK, tloc), e1(te1), op(top), e2(te2) {
  freeVars = AddVars(e1->freeVars, e2->freeVars);
}

Val ApplyOpEC::Eval(const Context& c) {
  if (recordCallStack) {
    callStackMu.lock();      
    ThreadDataGet()->callStack->addlo(this);
    callStackMu.unlock();          
  }
  Val result = LookupOp(this->op)(this->e1, this->e2, c);
  if (recordCallStack) {
    callStackMu.lock();          
    ThreadDataGet()->callStack->remlo();
    callStackMu.unlock();          
  }
  return result;
}

void ApplyOpEC::PrintD(ostream& os) 
{
  os << "(";
  e1->PrintD(os);
  os << ' ' << op << ' ';
  e2->PrintD(os);
  os << ")";
}

// ApplyUnOpEC
Val ApplyUnOpEC::Eval(const Context& c) {
  if (recordCallStack) {
    callStackMu.lock();          
    ThreadDataGet()->callStack->addlo(this);
    callStackMu.unlock();          
  }
  Val result = LookupUnOp(this->op)(this->e, c);
  if (recordCallStack) {
    callStackMu.lock();          
    ThreadDataGet()->callStack->remlo();
    callStackMu.unlock();          
  }
  return result;
}

void ApplyUnOpEC::PrintD(ostream& os) 
{
  os << ' ' << op;
  e->PrintD(os);
  os << ' ';
}

// PairEC
PairEC::PairEC(Expr tfirst, Expr tsecond, SrcLoc *tloc) 
: ExprC(PairEK, tloc), first(tfirst), second(tsecond) {
  freeVars = AddVars(first->freeVars, second->freeVars);
}

void PairEC::PrintD(ostream& os) 
{
  os << "[ ";
  first->PrintD(os);
  os << " = ";
  second->PrintD(os);
  os << " ]";
}

// SelectEC
SelectEC::SelectEC(Expr tbinding, Expr tfield, bool tbang,
		   SrcLoc *tloc)
: ExprC(SelectEK, tloc), binding(tbinding), field(tfield),
  bang(tbang) {
  if (field->kind == NameEK)
    freeVars = binding->freeVars;
  else
    freeVars = AddVars(binding->freeVars, field->freeVars);
}

void SelectEC::PrintD(ostream& os) 
{
  binding->PrintD(os);
  os << (bang ? '!' : '/');
  field->PrintD(os);
}

Val SelectEC::Eval(const Context& c) {
  Val lhs, rhs, result, err;
  Text id;

  // Compute the field name:
  switch (field->kind) {
  case NameEK:
    rhs = NULL;
    id = ((Name)field)->id;
    break;
  case ComputedEK:
    rhs = field->Eval(c);
    if (rhs->vKind != TextVK) {
      ostream& err_stream = cio().start_err();
      this->EError(err_stream, "Field selector must be evaluated to text: `");
      ErrorExpr(err_stream, field);
      ErrorDetail(err_stream, "'.\n");
      cio().end_err();
      err = RecordErrorOnStack(field);
      err->MergeDPS(rhs->dps);
      return err->AddToDPS(rhs->path, ValType(rhs), TypePK);
    }
    id = ((TextVC*)rhs)->NDS();
    break;
  default:
    ostream& err_stream = cio().start_err();
    this->EError(err_stream, "Field selector has wrong type: `");
    ErrorExpr(err_stream, field);
    ErrorDetail(err_stream, "'.\n");
    cio().end_err();
    return RecordErrorOnStack(field);
  }
  // Compute the binding:
  lhs = binding->Eval(c);
  if (lhs->vKind != BindingVK) {
    Text error_msg;
    if (bang) {
      error_msg = Text("Applying `!") + id + "' to a non-binding value: `";
    }    
    else {
      error_msg = Text("Selecting `") + id + "' from a non-binding value: `";
    }
    ostream& err_stream = cio().start_err();
    this->EError(err_stream, error_msg);
    ErrorVal(err_stream, lhs);
    ErrorDetail(err_stream, "'.\n");
    cio().end_err();
    err = RecordErrorOnStack(binding);
    err = err->MergeDPS(lhs->dps);
    return err->AddToDPS(lhs->path, ValType(lhs), TypePK);
  }
  // Select the field:
  if (rhs) lhs->Merge(rhs);
  if (bang) {
    result = ((BindingVC*)lhs)->Defined(id);
    if (rhs)
      result->cacheit = result->cacheit && rhs->cacheit;
    return result;
  }
  result = ((BindingVC*)lhs)->Lookup(id);
  if (result->vKind == UnbndVK) {
    ostream& err_stream = cio().start_err();
    this->EError(err_stream, 
		 Text("The field `") + id + "' is undefined in:\n");
    ErrorVal(err_stream, lhs);
    ErrorDetail(err_stream, ".\n");
    cio().end_err();
    err = RecordErrorOnStack(this);
    err->MergeDPS(result->dps);
    return err->AddToDPS(result->path, valFalse, BangPK);
  }
  if (rhs)
    result->cacheit = result->cacheit && rhs->cacheit;
  return result;
}

// FuncEC
FuncEC::FuncEC(bool tnoCache, bool tnoDup, const Text& tname, ArgList targs,
	       Expr tbody, SrcLoc *tloc)
: ExprC(FuncEK, tloc), noCache(tnoCache), noDup(tnoDup), name(tname),
  args(targs), body(tbody), tagged(false) {
  Exprs work = args->elems;
  Vars boundVars(nameDot);
  for (int i = 0; i < work.size(); i++) {
    Expr fe = work.get(i);
    if (fe->kind == NameEK)
      boundVars.Push(((NameEC*)fe)->id);
    else
      boundVars.Push(((AssignEC*)fe)->lhs->id);
  }
  freeVars = AddVars(args->freeVars,
		     Remove(body->freeVars, boundVars));
}

void FuncEC::PrintD(ostream& os) 
{
  os << "(";
  args->PrintD(os);
  os << ")";
  body->PrintD(os);
  os << endl;
}

Val FuncEC::Eval(const Context& c) {
  return NEW_CONSTR(ClosureVC, (this, RestrictContext(c, freeVars), true));
}

FP::Tag FuncEC::FingerPrint() {
  if (!this->tagged) {
    FPStream stream;
    this->PrintD(stream);
    this->tag = stream.tag();
    this->tagged = true;
  }
  return this->tag;
}

// BlockEC
BlockEC::BlockEC(StmtListEC *tassocs, Expr tbody, bool treturn,
		 SrcLoc *tloc)
: ExprC(BlockEK, tloc), assocs(tassocs), body(tbody), 
  isReturn(treturn) {
  freeVars = AddVars(assocs->freeVars,
		     Remove(body->freeVars, assocs->boundVars));
} 

void BlockEC::PrintD(ostream& os) 
{
  os << "{";
  assocs->PrintD(os);
  if (!assocs->Empty()) os << ";";
  os << endl;
  os << (isReturn ? "return " : "value ");
  body->PrintD(os);
  os << ";" << endl << "}" << endl;
}

Val BlockEC::Eval(const Context& c) {
  Context cc = AddStmtAssocs(assocs, c);
  Context work = cc, ac;

  while (!work.EqualQ(c))
    ac.Append1D(work.Pop());
  Val result = body->Eval(cc);
  if (cacheOption < 2) return result;
  return LetDpnd(result, ac);
}

// IterateEC
IterateEC::IterateEC(Expr tcontrol, Expr te, StmtListEC *tbody, SrcLoc *tloc)
: ExprC(IterateEK, tloc), control(tcontrol), e(te), body(tbody) {
  freeVars  = AddVars(e->freeVars, 
		      Remove(body->freeVars, control->freeVars));
}

void IterateEC::PrintD(ostream& os) 
{
  os << endl << "foreach ";
  if (control->kind == NameEK)
    control->PrintD(os);
  else if (control->kind == PairEK)
    control->PrintD(os);
  os << " in ";
  e->PrintD(os);
  os << " do" << endl << "{";
  body->PrintD(os);
  os << "}" << endl;
}  

// FileEC
void FileEC::PrintD(ostream& os) 
{
  os << ((this->import) ? "<Model `" : "<File `");
  os << name->id  << "' (`" << localPath << "')>";
}

// Internal callback for FileEC Eval.
struct FileECClosure {
  BindingEC *dir;
  VestaSource *newRoot;
  SrcLoc *loc;
};

static bool FileECCallback(void* closure, VestaSource::typeTag type, 
                           Arc arc, unsigned int index, Bit32 pseudoInode,
			   ShortId filesid, bool master) {
  FileECClosure *cl = (FileECClosure*)closure;

  Name name = NEW_CONSTR(NameEC, (arc, cl->loc));
  Expr e = NEW_CONSTR(FileEC, (name, arc, cl->newRoot, false, cl->loc));
  cl->dir->AddExpr(NEW_CONSTR(BindEC, (name, e, cl->loc)));
  return true;
}

Val FileEC::Eval(const Context& c) {
  Text path(this->localPath);
  VestaSource *newRoot;
  VestaSource::errorCode newRootErr;

  if (IsDirectory(modelRoot, path, newRoot, newRootErr)) {
    if (this->import) {
      char c = path[path.Length()-1];
      if (IsDelimiter(c))
        path = path + "build.ves";
      else
        path = path + PathnameSep + "build.ves";
      return NEW_CONSTR(ModelVC, (path, modelRoot, loc));
    }
    else if (newRoot->type == VestaSource::immutableDirectory) {
      FileECClosure cl;
      cl.dir = NEW_CONSTR(BindingEC, (5, loc));
      cl.newRoot = newRoot;
      cl.loc = loc;
      if (newRoot->list(0, FileECCallback, &cl) != VestaSource::ok) {
	this->EError(cio().start_err(), 
		     "Failed to list all objects in a directory.\n");
	cio().end_err();
	return RecordErrorOnStack(this);
      };
      return cl.dir->Eval(c);
    }
    else {
      this->EError(cio().start_err(), "Cannot import appendable directory.\n");
      cio().end_err();
      return RecordErrorOnStack(this);
    }
  }
  if (this->import) {
    Text modelPath(setSuffix(path, ".ves"));
    return NEW_CONSTR(ModelVC, (modelPath, modelRoot, loc));
  }
  if (newRootErr == VestaSource::ok) {
    return NEW_CONSTR(TextVC, (name->id, NULL, newRoot));
  }
  Error(cio().start_err(), 
	Text(VestaSourceErrorMsg(newRootErr)) + " opening `" + path + "'.\n");
  cio().end_err();
  return RecordErrorOnStack(this);
}
  
// ApplyEC
ApplyEC::ApplyEC(Expr tfunc, ArgList targs, SrcLoc *tloc) 
: ExprC(ApplyEK, tloc), func(tfunc), args(targs) {
  /* In Vesta, the . parameter may be passed implicitly. Since 
     we have not evaluated tfunc, we have no way to know if . is 
     actually needed.  So, we always add (conservatively) it into 
     freeVars.  */
  freeVars = AddVars(func->freeVars, args->freeVars);
  if (!freeVars.Member(nameDot))
    freeVars.Push(nameDot);
}

void ApplyEC::PrintD(ostream& os) 
{
  func->PrintD(os);
  os << "(";
  args->PrintD(os);
  os << ")";
}

Val ApplyEC::Eval(const Context& c) {
  Val result;

  if (recordCallStack) {
    callStackMu.lock();          
    ThreadDataGet()->callStack->addlo(this);
    callStackMu.unlock();          
  }

  Val fun = func->Eval(c);
  switch (fun->vKind) {
  case ClosureVK:
    result = ApplyFunction((ClosureVC*)fun, this, c);
    break;
  case ModelVK:
    result = ApplyModel((ModelVC*)fun, this, c);
    break;
  case PrimitiveVK:
    {
      PrimitiveVC *prim = (PrimitiveVC*)fun;
      if (prim->exec != NULL) {
	result = prim->exec(args, c);
	result->Merge(fun);
      }
      else {
	ostream& err_stream = cio().start_err();
	this->EError(err_stream, "Trying to apply non-existing primitive: `");
	ErrorVal(err_stream, prim);
	ErrorDetail(err_stream, "'.\n");
	cio().end_err();
	result = NEW(ErrorVC);
	result->Merge(fun);
      }
      break;
    }
  default:
    ostream& err_stream = cio().start_err();
    this->EError(err_stream, 
		 "Trying to apply non-primitive, non-function, non-model: `");
    ErrorVal(err_stream, fun);
    ErrorDetail(err_stream, "'.\n");
    cio().end_err();
    result = NEW(ErrorVC);
    result->MergeDPS(fun->dps);
    result->AddToDPS(fun->path, ValType(fun), TypePK);
  }

  if (recordCallStack) {
    callStackMu.lock();          
    ThreadDataGet()->callStack->remlo();
    callStackMu.unlock();          
  }
  return result;
}

// ModelEC
void ModelEC::PrintD(ostream& os)
{
  os << "files" << endl;
  files->PrintD(os);
  os << endl << "import" << endl;
  imports->PrintD(os);
  os << endl;
  block->PrintD(os);
}

bool ModelEC::ImportLocalModel() {
  Exprs elems = this->imports->elems;
  bool found = false;

  for (int i = 0; i < elems.size(); i++) {
    // assert(elems.get(i)->kind == BindEK);
    Expr item = ((BindEC*)elems.get(i))->rhs;
    if (item->kind == FileEK)
      found = !IsAbsolutePath(((FileEC*)item)->localPath);
    else {
      // assert(item->kind == BindingEK);
      Exprs elems1 = ((BindingEC*)item)->assocs;
      if (elems1.size() != 0) {
	// We only need to check one of them:
	// assert(elems1.getlo()->kind == BindEK);
	Expr item1 = ((BindEC*)elems1.getlo())->rhs;
	// assert(item1->kind == FileEK);
	found = !IsAbsolutePath(((FileEC*)item1)->localPath);
      }
    }
    if (found) break;
  }
  return found;
}

Val ModelEC::Eval(const Context& c) {
  Context cc = ProcessModelHead(this);
  Name eDot = NEW_CONSTR(NameEC, (nameDot, loc));

  PushToContext(nameDot, eDot, c, cc);
  return block->Eval(cc);
}

// TypedEC
void TypedEC::PrintD(ostream& os) 
{
  val->PrintD(os);
  os << ": ";
  typ->PrintD(os);
}

// ListTEC
void ListTEC::PrintD(ostream& os)
{
  os << "list";
  if (type) {
    os << "[ ";
    type->PrintD(os);
    os << " ]";
  }
}

// BindingTEC
void BindingTEC::PrintD(ostream& os) 
{
  os << "binding";
  if (fields) fields->PrintD(os);
}

// FuncTEC
void FuncTEC::PrintD(ostream& os) 
{
  os << "function";
  if (fields) fields->PrintD(os);
}

// ErrorEC
ErrorEC::ErrorEC(SrcLoc *tloc, bool runTime)
: ExprC(ErrorEK, tloc) {
  if (runTime)
    throw(Evaluator::failure(Text("exiting"), false));
}

//// Miscellaneous functions:
FP::Tag FingerPrint(Expr expr) {
  FPStream stream;
  expr->PrintD(stream);
  return stream.tag();
}

Vals EvalArgs(ArgList args, const Context& c) {
  Exprs elems = args->elems;
  Vals result;

  for (int i = 0; i < elems.size(); i++) {
    result.Append1D(elems.get(i)->Eval(c));
  }
  return result;
}

Vars AddVars(const Vars& fv1, const Vars& fv2) {
  Vars work = fv1;
  Vars result = fv2;

  while (!work.Null()) {
    Text fv(work.Pop());
    if (!fv2.Member(fv))
      result.Push(fv);
  }
  return result;
}

Vars Remove(const Vars& body, const Text& id) {
  Vars work = body, result;
  Text fv;
  while (!work.Null()) {
    fv = work.Pop();
    if (fv != id)
      result.Push(fv);
  }
  return result;
}

Vars Remove(const Vars& body, const Vars& bound) {
  if (bound.Null())
    return body;

  Vars work = body, result;
  Text fv;
  while (!work.Null()) {
    fv = work.Pop();
    if (!bound.Member(fv))
      result.Push(fv);
  }
  return result;
}

bool AllFreeVarsBoundInContext(const Expr e, const Context& c) {
  // Unused anywhere.
  Vars fv = e->freeVars;

  while(!fv.Null()) {
    Text id(fv.Pop());
    if (FindInContext(id, c) == nullAssoc) {
      ostream& err_stream = cio().start_err();
      e->EError(err_stream, Text("\n  Variable `") + id
		+ "' is free in next expression, but not bound in context.");
      ErrorDetail(err_stream, "\nCurrent context:\n");
      PrintContext(err_stream, c);
      cio().end_err();
      return false;
    }
  }
  return true;
}

// Process the file and import clauses.
bool IsId(const Text& id) {
  /* True iff id is an identifier. */
  int idx = 0, base = 10;
  if (id[0] == '0') {
    if (id[1] == 'x' || id[1] == 'X') {
      idx = 2;
      base = 16;
    } else {
      idx = 1;
      base = 8;
    }
  }
  bool isNumber = true, isIdOrNumber = true;
  for (int i = idx; i < id.Length(); i++) {
    char ch = id[i];
    switch (ch) {
    case '0': case '1': case '2': case '3': case '4': case '5':
    case '6': case '7':
      break;
    case '8': case '9':
      isNumber = isNumber && (base != 8);
      break;
    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
      isNumber = isNumber && (base == 16);
      break;
    default:
      isNumber = false;
      isIdOrNumber = (isIdOrNumber &&
		      ((ch >= 'a') && (ch <= 'z') ||
		       (ch >= 'A') && (ch <= 'Z') ||
		       (ch == '_') ||
		       (ch == '.')));
      break;
    }
  }
  return isIdOrNumber && !isNumber && (id[0] != '\0');
}

Context ProcessModelHead(ModelEC *model) {
  Exprs felems = model->files->elems;
  Exprs ielems = model->imports->elems;
  Context cc;
  Expr elem;
  BindEC *bind;
  int i;

  for (i = 0; i < felems.size(); i++) {
    elem = felems.get(i);
    if (elem->kind == BindEK) {
      bind = (BindEC*)elem;
      Name name = (Name)bind->lhs;
      if (IsId(name->id))
	PushToContext(name->id, bind->rhs, conEmpty, cc);
      else {
	name->EError(cio().start_err(), Text("The name `") + name->id
		     + "' for this file clause must be an identifier.");
	cio().end_err();
	throw(Evaluator::failure(Text("exiting"), false));
      }
    }
    else {
      InternalError(cio().start_err(), cio().helper(true), 
		    "ProcessModelHead (processing files).");
      // Unreached: InternalError never returns
      abort();
    }
  }

  for (i = 0; i < ielems.size(); i++) {
    elem = ielems.get(i);
    if (elem->kind == BindEK) {
      bind = (BindEC*)elem;
      Name name = (Name)bind->lhs;
      if (IsId(name->id))
	PushToContext(name->id, bind->rhs, conEmpty, cc);
      else {
	name->EError(cio().start_err(), Text("The name `") + name->id
		     + "' for this import clause must be an identifier.");
	cio().end_err();
	throw(Evaluator::failure(Text("exiting"), false));
      }
    }
    else {
      InternalError(cio().start_err(), cio().helper(true),
		    "ProcessModelHead (processing imports).");
      // Unreached: InternalError never returns
      abort();
    }
  }
  return cc;
}
