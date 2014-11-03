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

#ifndef Expr_H
#define Expr_H

#include "ValExpr.H"
#include "Location.H"
#include "Val.H"
#include "Prim.H"
#include "Err.H"
#include <FP.H>
#include <iostream>
#include <fstream>

// 'ExpC' includes all of the different classes of exprs that
// a client may encounter in an expression tree, or may build.
enum ExprKind {
  ConstantEK, IfEK, ComputedEK, ArgListEK, ExprListEK, StmtListEK,
  ApplyEK, ApplyOpEK, ApplyUnOpEK, AssignEK, BindEK, BindingEK,
  BlockEK, FileEK, FuncEK, IterateEK, ListEK, ModelEK, NameEK, 
  PairEK, PrimitiveEK, SelectEK, TypedEK, BaseTEK, ListTEK, 
  BindingTEK, FuncTEK, ErrorEK, TryEK };

class ExprC {
  // The superclass of all expressions:
public:
  // Where the source text for this expression starts:
  SrcLoc *loc;

  // Which subclass of ExprC `this' belongs to:
  ExprKind kind;

  // The identifiers of the (syntactically) free variables of `this':
  Vars freeVars;

  // Print `this' on `d':
  virtual void PrintD(std::ostream& os) = 0;

  // Both value and dependency of this in the context `c':
  virtual Val Eval(const Context& c) = 0;

  // Print the free variables of `this':
  void PrintExprVars(std::ostream& os);

  // Print `message' on 'os':
  void EError(std::ostream& os, const Text& message) { Error(os, message, loc); };

protected:
  // Construct a <tcl>C with location `tloc':
  ExprC(ExprKind tcl, SrcLoc *tloc)
    : loc(tloc), kind(tcl) { /*SKIP*/ };
};

// Subclasses of different expression types:
class ConstantEC: public ExprC {  // Literal or without free variables
public:
  ConstantEC(Val lit, SrcLoc *tloc)
    : ExprC(ConstantEK, tloc), val(lit) { /*SKIP*/ };
  Val val;
  void PrintD(std::ostream& os) { val->PrintD(os); };
  Val Eval(const Context& c);
};

class IfEC: public ExprC {        // Conditional expression
public:
  IfEC(Expr tcond, Expr tthen, Expr tels, SrcLoc *tloc);
  Expr test, then, els;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c);
};

class ComputedEC: public ExprC {  // Expression evaluating to a name
public:
  ComputedEC(Expr tname)
    : ExprC(ComputedEK, tname->loc), name(tname)
      { freeVars = tname->freeVars; };
  Expr name;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c);
};

class ExprListEC: public ExprC {  // Used in parser for syntactic lists
public:
  ExprListEC(int size, SrcLoc *tloc)
    : ExprC(ExprListEK, tloc), elems(size) { /*SKIP*/ };
  Exprs elems;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c);
  void AddExpr(Expr telem);
  bool Empty() { return (elems.size() == 0); };
  int Length() { return elems.size(); };
};

class ArgListEC: public ExprC {  // Used for formals and actuals
public:
  ArgListEC(int size, SrcLoc *tloc)
    : ExprC(ArgListEK, tloc), elems(size), inPKs(0) { /*SKIP*/ };
  Exprs elems;
  Bit64 inPKs;  // Well, there can only be 64 arguments for a function
  void PrintD(std::ostream& os);
  Val Eval(const Context& c) { return NEW(ErrorVC); };
  void AddExpr(Expr telem, bool inPK);
  bool Empty() { return (elems.size() == 0); };
  int Length() { return elems.size(); };
};

class StmtListEC: public ExprC {  // Used in parser for statement lists
public:
  StmtListEC(SrcLoc *tloc)
    : ExprC(StmtListEK, tloc) { /*SKIP*/ };
  Exprs elems;
  Vars boundVars;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c) { return NEW(ErrorVC); };
  void AddExpr(Expr telem);
  bool Empty() { return (elems.size() == 0); };
  int Length() { return elems.size(); };
};

class ListEC: public ExprC {      // Explicit list
public:
  ListEC(int size, SrcLoc *tloc)
    : ExprC(ListEK, tloc), elems(size) { /*SKIP*/ };
  Exprs elems;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c);
  void AddExpr(Expr telem);
};

class AssignEC: public ExprC {    // lhs = rhs (as a statement)
public:
  AssignEC(Name tlhs, Expr trhs, bool tisFunc, SrcLoc *tloc);
  Name lhs;
  Expr rhs;
  bool isFunc;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c) { return NEW(ErrorVC); };
};

class BindEC: public ExprC {      // lhs = rhs (as a binding component)
public:
  BindEC(Expr tlhs, Expr trhs, SrcLoc *tloc);
  Expr lhs, rhs;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c) { return NEW(ErrorVC); };
};

class NameEC: public ExprC {      // Identifier
public:
  NameEC(const Text& tid, SrcLoc *tloc)
    : ExprC(NameEK, tloc), id(tid) { freeVars.Push(id); };
  Atom id;
  void PrintD(std::ostream& os) { os << id; };
  Val Eval(const Context& c);
  void ClearFreeVars() { freeVars.SetEmpty(); };
};

class BindingEC: public ExprC {   // Complete binding
public:
  BindingEC(int size, SrcLoc *tloc)
    : ExprC(BindingEK, tloc), assocs(size) { /*SKIP*/ };
  Exprs assocs;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c);
  void AddExpr(Expr elem);
};

class ApplyOpEC: public ExprC {   // Application of binary operator
public:
  ApplyOpEC(Expr te1, const Text& top, Expr te2, SrcLoc *loc);
  Atom op;
  Expr e1, e2;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c);
};

class ApplyUnOpEC: public ExprC { // Application of unary operator
public:
  ApplyUnOpEC(const Text& top, Expr te, SrcLoc *tloc)
    : ExprC(ApplyUnOpEK, tloc), op(top), e(te) { freeVars = e->freeVars; };
  Atom op;
  Expr e;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c);
};

class ModelEC: public ExprC {     // Parsed model
public:
  ModelEC(ExprList tfiles, ExprList timports, bool tnoDup, Expr tblock,
	  VestaSource *mRoot, SrcLoc *tloc)
    : ExprC(ModelEK, tloc), files(tfiles), imports(timports), noDup(tnoDup),
      block(tblock), modelRoot(mRoot) { /*SKIP*/ };
  ExprList files, imports;
  bool noDup;
  Expr block;
  VestaSource *modelRoot;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c);
  bool ImportLocalModel();
};

class FileEC: public ExprC {      // Name from `files' or `import'
public:
  FileEC(Name tname, const Text& tpath, VestaSource *mRoot,
	 bool timport, SrcLoc *tloc)
    : ExprC(FileEK, tloc), name(tname), localPath(tpath),
      modelRoot(mRoot), import(timport) { /*SKIP*/ };
  Name name;
  Atom localPath;
  VestaSource *modelRoot;
  bool import;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c);
};

class PrimitiveEC: public ExprC { // Language primitive function
public:
  PrimitiveEC(const Text& tname, PrimExec texec, SrcLoc *tloc)
    : ExprC(PrimitiveEK, tloc), name(tname), exec(texec) { /*SKIP*/ };
  Atom name;
  PrimExec exec;
  void PrintD(std::ostream& os) { os << name; };
  Val Eval(const Context& c)
    { return NEW_CONSTR(PrimitiveVC, (this->name, this->exec)); };
};

class PairEC: public ExprC {      // Two expression (used internally)
public:
  PairEC(Expr tfirst, Expr tsecond, SrcLoc *tloc);
  Expr first, second;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c) { return NEW(ErrorVC); };
};

class SelectEC: public ExprC {    // Field selection from binding
public:
  SelectEC(Expr tbinding, Expr tfield, bool tbang, SrcLoc *tloc);
  Expr binding, field;
  bool bang;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c);
};

class FuncEC: public ExprC {      // A function declaration
public:
  FuncEC(bool tnoCache, bool tNoDup, const Text& tname, ArgList targs,
	  Expr tbody, SrcLoc *tloc);
  Atom name;
  ArgList args;
  Expr body;
  bool noCache;
  bool noDup;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c);
  FP::Tag FingerPrint();
private:
  bool tagged;
  FP::Tag tag;
};

class BlockEC: public ExprC {     // Block, with `value' or `return'
public:
  BlockEC(StmtListEC *tassocs, Expr tbody, bool treturn, SrcLoc *tloc);
  StmtListEC *assocs;
  Expr body;
  bool isReturn;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c);
};

class IterateEC: public ExprC {   // Iteration statement
public:
  IterateEC(Expr tcontrol, Expr te, StmtListEC *tbody, SrcLoc *tloc);
  Expr control, e;
  StmtListEC *body;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c) { return NEW(ErrorVC); };
};

class ApplyEC: public ExprC {     // Application of function, primitive, model
public:
  ApplyEC(Expr tfunc, ArgList targs, SrcLoc *loc);
  Expr func;
  ArgList args;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c);
};

class TypedEC: public ExprC {     // Typed value
public:
  TypedEC(Expr tval, Expr ttyp, SrcLoc *tloc)
    : ExprC(TypedEK, tloc), val(tval), typ(ttyp)
      { freeVars = tval->freeVars; };
  Expr val, typ;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c) { return val->Eval(c); };
};

class BaseTEC: public ExprC {     // Base type
public:
  BaseTEC(const Text& tbase, SrcLoc *tloc)
    : ExprC(BaseTEK, tloc), base(tbase) { /*SKIP*/ };
  Atom base;
  void PrintD(std::ostream& os) { os << base; };
  Val Eval(const Context& c) { return NEW(ErrorVC); };
};

class ListTEC: public ExprC {     // List type
public:
  ListTEC(Expr ttype, SrcLoc *tloc)
    : ExprC(ListTEK, tloc), type(ttype) { /*SKIP*/ };
  Expr type;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c) { return NEW(ErrorVC); };
};

class BindingTEC: public ExprC {  // Binding type
public:
  BindingTEC(Expr tfields, SrcLoc *tloc)
    : ExprC(BindingTEK, tloc), fields(tfields) { /*SKIP*/ };
  Expr fields;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c) { return NEW(ErrorVC); };
};

class FuncTEC: public ExprC {     // Function type
public:
  FuncTEC(Expr tfields, SrcLoc *tloc)
    : ExprC(FuncTEK, tloc), fields(tfields) { /*SKIP*/ };
  Expr fields;
  void PrintD(std::ostream& os);
  Val Eval(const Context& c) { return NEW(ErrorVC); };
};

class ErrorEC: public ExprC {     // Error expression
public:
  ErrorEC(SrcLoc *tloc, bool runTime = true);
  void PrintD(std::ostream& os) { os << "ERR"; };
  Val Eval(const Context& c) { return NEW_CONSTR(ErrorVC, (true)); };
};

// End of subclasses of ExprC.

// Fingerprint an expression:
FP::Tag FingerPrint(Expr expr);

// Text list operations:
Vars AddVars(const Vars& fv1, const Vars& fv2);
Vars Remove(const Vars& body, const Text& id);
Vars Remove(const Vars& body, const Vars& bound);

Vals EvalArgs(ArgList args, const Context& c);
Context ProcessModelHead(ModelEC*);

#endif // Expr_H
