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

// File: Val.H                                                

#ifndef Val_H
#define Val_H

#include "ValExpr.H"
#include "Location.H"
#include "Dep.H"
#include "Prim.H"
#include "Err.H"
#include <Generics.H>
#include <CacheC.H>
#include <PrefixTbl.H>
#include <VestaSource.H>
#include <iostream>
#include <fstream>

// Convenient constants:
extern Val valTrue, valFalse, valZero, valErr, valUnbnd;
extern Val valTBinding, valTBool, valTClosure, valTErr;
extern Val valTInt, valTList, valTText, valTModel;
extern Context conEmpty, conInitial;
extern Assoc nullAssoc;

class DepMergeOptimizer;

// Case selectors for subclasses of ValC:
enum ValueKind {
  BooleanVK, IntegerVK, ListVK, BindingVK, PrimitiveVK,
  TextVK, ClosureVK, ModelVK, ErrorVK, FpVK, UnbndVK };

//// ValC:
class ValC {
  // We record evaluation dependencies during an evaluation.
  // The superclass of all vesta SDL values:
public:
  ValueKind vKind;
  // Which subclass of ValC `this' belongs to.

  bool tagged;
  // Has fingerprint?

  bool cacheit;
  // The boolean is true iff no error occurs in creating this value.
  // We decided not to cache any value only when it is true.

  FP::Tag tag;
  // Fingerprint of the value.  We save the fp here for later uses when
  // the fp is computed in the first time.

  DPaths* dps;
  // Path dependency set.

  DepPath* path;
  // Path to structural dependency.

  virtual void PrintD(std::ostream& os, bool verbose = false, int indent = 0, int depth = -1, bool nl = true) = 0;
  // Pretty-print the value on `os'.

  virtual Val Copy(bool more = false) = 0;
  // Construct a copy of the value with no dependency.

  virtual FP::Tag FingerPrint() = 0;
  // Fingerprinting this value.

  void VError(std::ostream& os, const Text& message) { Error(os, message); };
  // Print `message' on 'os'.

  Val MergeDPS(DPaths* ps);
  Val MergeDPS(DPaths* ps, DepMergeOptimizer &dmo);
  // Merge in those paths of ps.

  Val Merge(Val val);
  // Merge val->dps into this->dps, and then add <val->path, val>.

  Val MergeAndTypeDPS(Val val);
  // Merge val->dps into this->dps, and then add <val->path, ValType(val)>.

  Val MergeAndLenDPS(Val val);
  // Merge val->dps into this->dps, and then add length dependency.

  Val MergeLenDPS(Val val);
  // Merge in those paths of val->lenDps.

  Val AddToDPS(DepPath* dp, Val v, PathKind pk = DummyPK);
  // Add a new path to dps.

  Val AddExtendToDPS(DepPath* dp, Val v, PathKind pk, const Text& id);
  // Add a new path to dps.

  Val Extend(Val v, const Text& id, PathKind pk = NormPK, bool add = true);
  // Create a copy of the value v, using dps and path of this.

  int SizeOfDPS() { return (dps == NULL) ? 0 : dps->Size(); };
  // Size of the dependency set.

protected:
  ValC(ValueKind vk)
    : vKind(vk), tagged(false), cacheit(true), path(NULL),
      dps(NULL) { /*SKIP*/ };
  // Construct a new value with empty dependency.

  ValC(const ValC& val)
    : vKind(val.vKind), tagged(val.tagged), tag(val.tag), cacheit(val.cacheit),
      path(NULL), dps(NULL) { /*SKIP*/ };
  // Copy, but do not copy dependency.
};

// Subclasses of ValC:
class BooleanVC: public ValC {
public:
  BooleanVC(bool bb)
    : ValC(BooleanVK), b(bb) { /*SKIP*/ };
  BooleanVC(const BooleanVC& val)
    : ValC(val), b(val.b) { /*SKIP*/ };
  void PrintD(std::ostream& os, bool verbose = false, int indent = 0, int depth = -1, bool nl = true)
    { os << ((b) ? "TRUE" : "FALSE"); };
  FP::Tag FingerPrint();
  bool b;
  Val Copy(bool more) { return NEW_CONSTR(BooleanVC, (*this)); };
};

class IntegerVC: public ValC {
public:
  IntegerVC(Basics::int32 i): ValC(IntegerVK), num(i) { /*SKIP*/ };
  IntegerVC(const IntegerVC& val): ValC(val), num(val.num) { /*SKIP*/ };
  void PrintD(std::ostream& os, bool verbose = false, int indent = 0, int depth = -1, bool nl = true)
    { os << num; };
  FP::Tag FingerPrint();
  Basics::int32 num;
  Val Copy(bool more) { return NEW_CONSTR(IntegerVC, (*this)); };
};

class ListVC: public ValC {
public:
  ListVC(const Vals& telems)
    : ValC(ListVK), elems(telems), lenDps(NULL) { /*SKIP*/ };
  ListVC(const ListVC& val)
    : ValC(val), elems(val.elems), lenDps(NULL) { /*SKIP*/ };
  void PrintD(std::ostream& os, bool verbose = false, int indent = 0, int depth = -1, bool nl = true);
  FP::Tag FingerPrint();
  Vals elems;
  DPaths* lenDps;
  IntegerVC* Length();
  Val GetElem(int index);
  Val GetElemNoDpnd(int index);
  Val AddToLenDPS(const DepPath& dp, Val val);
  Val MergeToLenDPS(DPaths *ps);
  Val Copy(bool more);
};

class BindingVC: public ValC {
public:
  BindingVC(): ValC(BindingVK), lenDps(NULL) { /*SKIP*/ };
  BindingVC(const Context& telems)
    : ValC(BindingVK), lenDps(NULL) { elems = telems; };
  BindingVC(Binding b1, Binding b2, bool rec);
  BindingVC(const BindingVC& val)
    : ValC(val), elems(val.elems), lenDps(NULL) { /*SKIP*/ };
  Context elems;
  DPaths* lenDps;
  void PrintD(std::ostream& os, bool verbose = false, int indent = 0, int depth = -1, bool nl = true);
  FP::Tag FingerPrint();
  IntegerVC* Length();
  Val Names();
  bool Null() { return this->elems.Null(); };
  Val Defined(const Text& id);
  Val Lookup(const Text& id);  // may return UnbndVC
  Val GetElem(const Text& name, int& index);
  Val GetElem(int index, Text& name);
  bool DefinedNoDpnd(const Text& id);
  Val LookupNoDpnd(const Text& id);
  Val GetElemNoDpnd(const Text& name, int& index);
  Val GetElemNoDpnd(int index, Text& name);
  Binding AddBindingAssoc(const Text& name, Val v);
  Binding RemoveBindingAssoc(const Text& id);
  Val AddToLenDPS(const DepPath& dp, Val val);
  Val MergeToLenDPS(DPaths *ps);
  Val Copy(bool more);
private:
  Context SimpleOverlay(Binding b);
  Context RecursiveOverlay(Binding b);
};

class PrimitiveVC: public ValC {
public:
  PrimitiveVC(const Text& tname, PrimExec texec)
    : ValC(PrimitiveVK), name(tname), exec(texec) { /*SKIP*/ };
  PrimitiveVC(const PrimitiveVC& val)
    : ValC(val), name(val.name), exec(val.exec) { /*SKIP*/ };
  void PrintD(std::ostream& os, bool verbose = false, int indent = 0, int depth = -1, bool nl = true)
    { os << name; };
  FP::Tag FingerPrint();
  Text name;
  PrimExec exec;
  Val Copy(bool more) { return NEW_CONSTR(PrimitiveVC, (*this)); };
};

class TextVC: public ValC {
public:
  class TextC {
  public:
    TextC(const Text& ttext)
      : txt(ttext), shortId(NullShortId), hasTxt(true), hasName(false)
	{ /*SKIP*/ };
    TextC(const Text& tname, const Text& ttext);
    TextC(const Text& tname, std::fstream *iFile, VestaSource *vSource);
    TextC(const Text& tname, const ShortId sid)
      : name(tname), shortId(sid), hasTxt(false), hasName(true)
	{ /*SKIP*/ };
    Text txt;            // the text
    Text name;
    ShortId shortId;
    bool hasTxt, hasName;
  };
  TextVC(const Text& ttext): ValC(TextVK) { content = NEW_CONSTR(TextC, (ttext)); };
    /* The constructor for a normal text string. */
  TextVC(const Text& tname, const Text& ttext, char c, const FP::Tag& fp);
    /* The constructor for the derived file generated for the text. */
  TextVC(const Text& tname, std::fstream *iFile, VestaSource *vSource);
    /* The constructor for the file specified by vSource. */
  TextVC(const Text& tname, const ShortId sid, int fp_content);
    /* The constructor for the derived file with known sid. */
  TextVC(const Text& tname, const ShortId sid, const FP::Tag& fp)
    : ValC(TextVK)
      { content = NEW_CONSTR(TextC, (tname, sid)); tag = fp; tagged = true; };
    /* The constructor for the file with known sid and fp. */
  TextVC(const TextVC& val)
    : ValC(val), content(val.content) { /*SKIP*/ };
  TextC* content;
  void PrintD(std::ostream& os, bool verbose = false, int indent = 0, int depth = -1, bool nl = true);
  FP::Tag FingerPrint();
  Text NDS();
  Text TName();
  ShortId Sid();
  bool HasTxt() { return content->hasTxt; };
  bool HasSid() { return (content->shortId != NullShortId); };
  int Length();
  Val Copy(bool more) { return NEW_CONSTR(TextVC, (*this)); };
private:
  std::istream* Content(bool& closeIt);
};

class ModelVC: public ValC {
public:
  class ModelC {
  public:
    ModelC(const Text& tlPath)
      : name(tlPath), sid(NullShortId), mRoot(NULL), parsed(false)
	{ /*SKIP*/ };
    ModelC(const Text& tname, ShortId tsid, VestaSource *root, 
	   Expr mod, const Context& cc)
      : name(tname), sid(tsid), mRoot(root), model(mod), c(cc),
        parsed(true) { /*SKIP*/ };
    ModelC(const Text& tname, ShortId tsid, VestaSource *root)
      : name(tname), sid(tsid), mRoot(root), parsed(false)
        { /*SKIP*/ };
    Text name;
    VestaSource *mRoot;
    Expr model;
    Context c;
    ShortId sid;
    bool parsed;
  };
  ModelVC(const Text& tname, ShortId tsid, VestaSource *root, 
	  Expr mod, const Context& cc, VestaSource *vSource);
  ModelVC(const Text& tlPath, VestaSource *root, SrcLoc *loc);
  ModelVC(const Text& tname, ShortId tsid, VestaSource *root,
	  const FP::Tag& ttag, const FP::Tag& tlidTag)
    : ValC(ModelVK)
      { content = NEW_CONSTR(ModelC, (tname, tsid, root));
	tag = ttag; tagged = true; lidTag = tlidTag; };
  ModelVC(const ModelVC& val)
    : ValC(val), content(val.content), lidTag(val.lidTag)
      { /*SKIP*/ };
  ModelC* content;
  void PrintD(std::ostream& os, bool verbose = false, int indent = 0, int depth = -1, bool nl = true);
  FP::Tag lidTag;
  FP::Tag FingerPrint() { return this->lidTag; };
  FP::Tag FingerPrintFile() { return this->tag; };
  ShortId Sid() { return this->content->sid; };
  Val Force();
  Val Copy(bool more) { return NEW_CONSTR(ModelVC, (*this)); };
};

class ClosureVC: public ValC {
public:
  ClosureVC(FuncEC *tfunc, const Context& c, bool fresh);
  ClosureVC(const ClosureVC& val)
    : ValC(val), func(val.func), con(val.con),
      exprTag(val.exprTag), exprTagged(val.exprTagged) { /*SKIP*/ };
  FuncEC *func;
  Context con;
  bool exprTagged;
  FP::Tag exprTag;
  void PrintD(std::ostream& os, bool verbose = false, int indent = 0, int depth = -1, bool nl = true);
  FP::Tag FingerPrint();
  FP::Tag FingerPrintExpr();
  Val Copy(bool more) { return NEW_CONSTR(ClosureVC, (*this)); };
};

class ErrorVC: public ValC {
public:
  ErrorVC();
  ErrorVC(bool cacheable)
    : ValC(ErrorVK) { this->cacheit = cacheable; };
  ErrorVC(const ErrorVC& val): ValC(val) { /*SKIP*/ };
  void PrintD(std::ostream& os, bool verbose = false, int indent = 0, int depth = -1, bool nl = true)
    { os << "ERR"; };
  FP::Tag FingerPrint();
  Val Copy(bool more) { return NEW_CONSTR(ErrorVC, (*this)); };
};

class FpVC: public ValC {
  // For value we only know its fingerprint -- obtained from cache.
  // Never returned by Eval.
public:
  FpVC(const FP::Tag& ttag)
    : ValC(FpVK) { tag = ttag; tagged = true; };
  void PrintD(std::ostream& os, bool verbose = false, int indent = 0, int depth = -1, bool nl = true)
    { os << "<Value tag>"; };
  FP::Tag FingerPrint() { return tag; }
  Val Copy(bool more)
  { VError(OS::cio().start_err(), "Copy a FpVC?");
    OS::cio().end_err();
    return NEW_CONSTR(FpVC, (tag)); };
};

class UnbndVC: public ValC {
  // Never returned by Eval.
public:
  UnbndVC() : ValC(UnbndVK) { /*SKIP*/ };
  void PrintD(std::ostream& os, bool verbose = false, int indent = 0, int depth = -1, bool nl = true)
    { os << "<unbound name>"; };
  FP::Tag FingerPrint();
  Val Copy(bool more) { return NEW(UnbndVC); };
};

class AssocVC {
  // Note that it is not a subclass of ValC.
public:
  AssocVC(const Text& tname, Val tval)
    : name(tname), val(tval) { /*SKIP*/ };
  void PrintD(std::ostream& os, bool verbose = false, int indent = 0, int depth = -1, bool nl = true);
  FP::Tag FingerPrint();
  Atom name;
  Val val;
};

// Type predicates:
bool IsValTrue(Val v);
bool IsValFalse(Val v);
bool IsEmptyBinding(Val v);
bool IsEmptyList(Val v);

// Return the type of a vesta value.
Val ValType(Val v);

// Collect dependency for value v in context c.
Val LetDpnd(Val v, const Context& c);
Val FuncDpnd(Val bodyv, Val fv, const Context& c);
Val ModelDpnd(Val bodyv, Val fv, const Context& c);
DPaths* ClosureDpnd(ClosureVC *cl);
void ModelCutOff(Val v);
void ValueDpnd(Val v, DPaths *ps);

// Print a value:
Text PrintForm(Val v);

// Print a context:
void PrintContext(std::ostream& os, const Context& c, bool verbose = false,
		  int indent = 0);

// Return the (top) association binding id in context c:
Assoc FindInContext(const Text& id, const Context& c);

// Return the value of the (top) association binding id in context c:
Val LookupInContext(const Text& id, const Context& c);

// Lookup a path to get the corresponding value:
Val LookupPath(const FV2::T& path, const Context& c);
Val LookupPath(DepPath *dp, const Context& c);
Val LookupPath(Basics::uint32 idx, PathKind pkind, const PrefixTbl& tbl,
	       const Context& c, Val *vals);

// Bind name to the value of elem in cElem and append to cc:
void AppendDToContext(const Text& name, Expr elem, const Context& cElem,
		      Context& cc);

// Return a context like con, limited to the free variables of fv:
Context RestrictContext(const Context& con, Vars fv);

// Return a context like orig, with a single identifier binding removed
// from the original context (if present):
Context Snip(const Context& orig, const Text& remove, bool& found);

// Remove multiple bindings from a context:
Context Prune(const Context& orig, const Context& remove);

// Bind name to the value of elem in cElem and add to in:
void PushToContext(const Text& name, Expr elem, const Context& cElem,
		   Context& in);

// Return c augmented with the bindings created by the statements in assocs:
Context AddStmtAssocs(StmtListEC *assocs, const Context& c);

// Initialize module:
void ValInit();

#endif // Val_H
