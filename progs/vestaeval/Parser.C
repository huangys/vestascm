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

/* File: Parser.C                                              */

#include "Parser.H"
#include "Files.H"
#include "Lex.H"
#include "ModelState.H"
#include "Err.H"
#include "Expr.H"
#include "Val.H"
#include "Prim.H"
#include <iostream>

using std::istream;
using std::ostream;
using OS::cio;

static char *pragmaNoCache = "nocache";
static char *pragmaInPK    = "pk";
static char *pragmaNoDup   = "waitdup";

// State for the parser:
static Token eatenToken, token, nextToken;
static bool hasNextToken = false;

void PError(const Text& msg) {
  Error(cio().start_err(), msg, token.loc);
  cio().end_err();
  throw "\nParsing terminated.\n";
}

inline void NextToken() {
  if (hasNextToken) {
    token.TokenAssign(nextToken);
    hasNextToken = false;
  }
  else
    token.Next();
  return;
}

inline void TokenLookAhead() {
  if (!hasNextToken) {
    nextToken.Next();
    hasNextToken = true;
  }
  return;
}

bool AtToken(TokenClass tc) {
  while (token.tclass == TkPragma &&
	 strcmp(token.Bytes(), pragmaInPK) &&
	 strcmp(token.Bytes(), pragmaNoCache) &&
	 strcmp(token.Bytes(), pragmaNoDup)) {
    NextToken();
  }
  return (token.tclass == tc);
}

bool AtDelimiter() {
  while (token.tclass == TkPragma &&
	 strcmp(token.Bytes(), pragmaInPK) &&
	 strcmp(token.Bytes(), pragmaNoCache) &&
	 strcmp(token.Bytes(), pragmaNoDup)) {
    NextToken();
  }
  return ((token.tclass == TkSlash) ||
          (token.tclass == TkBackSlash));
}

bool AtArc() {
  while (token.tclass == TkPragma &&
	 strcmp(token.Bytes(), pragmaInPK) &&
	 strcmp(token.Bytes(), pragmaNoCache) &&
	 strcmp(token.Bytes(), pragmaNoDup)) {
    NextToken();
  }
  return ((token.tclass == TkId) ||
	  (token.tclass == TkNumber) ||
          (token.tclass == TkString));
}

inline bool EatPragma() {
  if (token.tclass == TkPragma) {
    eatenToken.TokenAssign(token);
    NextToken();
    return true;
  }
  return false;
}

inline bool EatToken(TokenClass tc) {
  if (AtToken(tc)) {
    eatenToken.TokenAssign(token);
    NextToken();
    return true;
  }
  return false;
}

inline bool EatName(Name& name) {
  if (AtToken(TkId)) {
    name = (Name)(token.expr);
    eatenToken.TokenAssign(token);
    NextToken();
    return true;
  }
  return false;
}

inline bool EatArc(Name& arc) {
  if (AtArc()) {
    arc = NEW_CONSTR(NameEC, (token.AsText(), token.loc));
    eatenToken.TokenAssign(token);
    NextToken();
    return true;
  }
  return false;
}

inline bool EatDelimiter() {
  if (AtDelimiter()) {
    eatenToken.TokenAssign(token);
    NextToken();
    return true;
  }
  return false;
}

void Expect(TokenClass tk, char *msg) {
  if (!EatToken(tk))
    PError(Text("Expected ") + msg + "  Current token class: "
      + TokenNames[token.tclass] + ", `" + token.AsText() + "'.\n");
}

inline Name GetName() {
  Expect(TkId, "identifier.");

  Name name = (Name)(eatenToken.expr);
  return name;
}

Text ModelPath(const Text& from, const Text& path) {
  int fromLast = from.Length()-1;

  if (fromLast < 0) return path;
  if (path.Empty()) return from;
  if (IsDelimiter(from[fromLast])) {
    if (IsDelimiter(path[0]))
      return from + path.Sub(1);
    else
      return from + path;
  }
  else {
    if (IsDelimiter(path[0]))
      return from + path;
    else
      return from + PathnameSep + path;
  }
}

Expr ConvertToBindElem(Expr lhs, Expr rhs) {
  // Not used.
  Name name;

  switch (lhs->kind) {
  case NameEK:
    ((Name)lhs)->ClearFreeVars();
    return NEW_CONSTR(BindEC, (lhs, rhs, lhs->loc));
  case ConstantEK:
    {
      Val v = ((ConstantEC*)lhs)->val;
      if (v->vKind == IntegerVK)
	name = NEW_CONSTR(NameEC, (PrintForm(v), lhs->loc));
      else if (v->vKind == TextVK)
	name = NEW_CONSTR(NameEC, (((TextVC*)v)->NDS(), lhs->loc));
      else {
	ostream& err_stream = cio().start_err();
	Error(err_stream, 
	      "Expected left side of binding, found: `", lhs->loc);
	ErrorVal(err_stream, v);
	ErrorDetail(err_stream, "'.\n");
	cio().end_err();
	return NEW_CONSTR(ErrorEC, (token.loc));
      }
      name->ClearFreeVars();
      return NEW_CONSTR(BindEC, (name, rhs, lhs->loc));
    }
  case SelectEK:
    {
      BindingEC *be = NEW_CONSTR(BindingEC, (1, token.loc));
      SelectEC *sel = (SelectEC*)lhs;
      be->AddExpr(NEW_CONSTR(BindEC, (sel->field, rhs, sel->field->loc)));
      return ConvertToBindElem(sel->binding, be);
    }
  default:
    ostream& err_stream = cio().start_err();
    Error(err_stream, "Invalid left side of binding element: `", lhs->loc);
    ErrorExpr(err_stream, lhs);
    ErrorDetail(err_stream, "'.\n");
    cio().end_err();
    return NEW_CONSTR(ErrorEC, (token.loc));
  }
  // assert(false); // not reached
}

void InitState(istream *lexIn, const Text& modelName, ShortId sid,
	       VestaSource *modelRoot) {
  ModelStateInit(lexIn, modelRoot);
  token.Init(modelName, sid);
  NextToken();
}

/* The recursive descent parsing function signatures: */
Expr Model        ();

ExprList Files    ();
Expr FileItem     (bool couldBeList);
Expr FileList     ();

ExprList Imports  ();
Expr IncItem      (const Text& from, bool hasForm, bool couldBeList);
Expr IncList      (const Text& from, bool hasForm);
Text DelimPath    (Name& arc, bool arcSeen);

Expr Block        ();
Expr Stmt         ();
Expr Func         (const Text& name, bool noCache, bool noDup);
ArgList Formals   ();
Expr Assign       (Name name);
Expr Iterate      ();
Expr Control      ();

Expr Expression   ();
Expr Expr1        ();
Expr Expr2        ();
Expr Expr3        ();
Expr Expr4        ();
Expr Expr5        ();
Expr Expr6        ();
Expr Expr7        ();
Expr Expr8        ();
Expr Primary      ();

Expr BindingCons  (Expr e);
Expr BindElem     ();
Expr ListCons     (Expr e);
Expr GenArc       ();
ArgList Actuals   ();
Expr TypeDef      ();
Expr Type         ();

//// Now the functions:
Expr Model() {
  ExprList files   = Files();
  ExprList imports = Imports();
  bool noDup       = false;
  while(EatPragma()) {
	  if(!strcmp(eatenToken.Bytes(), pragmaNoDup)) {
		  noDup = true;
	  }
  }
  Expr block       = Block();
  return NEW_CONSTR(ModelEC, (files, imports, noDup, block, mRoot, files->loc));
}

ExprList Files() {
  ExprList dir = NEW_CONSTR(ExprListEC, (5, token.loc));
  bool looking;

  while (EatToken(TkFiles)) {
    looking = true;
    while (looking && (AtArc() || AtDelimiter())) {
      dir->AddExpr(FileItem(true));
      looking = EatToken(TkSemicolon);
    }
  }
  return dir;
}

Expr FileItem(bool couldBeList) {
  SrcLoc *eL = token.loc;
  Text path;
  Name arc, junque;
  Expr item;
  
  // assert(AtArc() || AtDelimiter())
  if (EatArc(arc)) {
    if (EatToken(TkEqual))
      if (EatToken(TkLBracket))
	if (couldBeList)
	  item = FileList();
	else
	  PError("Nested file lists not allowed.");
      else
	item = NEW_CONSTR(FileEC, 
			  (arc, DelimPath(junque, false), mRoot, false, eL));
    else if (AtDelimiter()) {
      path = DelimPath(arc, true);
      item = NEW_CONSTR(FileEC, (arc, path, mRoot, false, eL));
    }
    else
      item = NEW_CONSTR(FileEC, (arc, arc->id, mRoot, false, eL));
  }
  else if (AtDelimiter()) {
    path = DelimPath(arc, false);
    item = NEW_CONSTR(FileEC, (arc, path, mRoot, false, eL));
  }
  else
    PError("Must be an arc: identifier, integer, or string.");
  return NEW_CONSTR(BindEC, (arc, item, eL));
}

Expr FileList() {
  BindingEC *files = NEW_CONSTR(BindingEC, (5, token.loc));
  bool looking = true;
  
  while (looking && (AtArc() || AtDelimiter())) {
    files->AddExpr(FileItem(false));
    looking = EatToken(TkComma);
  }
  Expect(TkRBracket, "`]' to terminate file list.");
  return files;
}

ExprList Imports() {
  SrcLoc *eL = token.loc;;
  Text from;
  Name name, junque;
  ExprList dir = NEW_CONSTR(ExprListEC, (5, eL));
  bool hasFrom, looking;

  // Always import self!
  Name arc = NEW_CONSTR(NameEC, ("_self", eL));
  Text selfHead, selfTail;
  SplitPath(eL->file, selfHead, selfTail);
  Expr item = NEW_CONSTR(FileEC, (arc, selfTail, mRoot, true, eL));
  dir->AddExpr(NEW_CONSTR(BindEC, (arc, item, eL)));
  
  while (AtToken(TkFrom) || AtToken(TkImport)) {
    hasFrom = false;
    if (EatToken(TkFrom)) {
      from = DelimPath(junque, false);
      hasFrom = true;
    }
    Expect(TkImport, "`import' at head of model list.");
    looking = true;
    while (looking && AtArc()) {
      dir->AddExpr(IncItem(from, hasFrom, true));
      looking = EatToken(TkSemicolon);
    }
  }
  return dir;
}

Expr IncItem(const Text& from, bool hasFrom, bool couldBeList) {
  SrcLoc *eL = token.loc;
  Text path, rest;
  Name arc, junque;
  Expr item;
  bool isList = false;

  if (!EatArc(arc)) {
    PError("Must be an arc: identifier, integer, or string.");
  }

  if (EatToken(TkEqual))
    if (EatToken(TkLBracket))
      if (couldBeList) {
        item = IncList(from, hasFrom);
  	isList = true;
      }
      else
       PError("Nested import lists not allowed.");
    else if (hasFrom && AtDelimiter())
      PError("A delimiter is not permitted after this equality sign.");
    else {
      rest = DelimPath(junque, false);
      path = ModelPath(from, rest);
    }
  else if (!hasFrom) {
    PError("An explicit `id =' must precede this path.");
  }
  else if (AtDelimiter()) {
    Name more = arc;
    rest = DelimPath(more, true);
    path = ModelPath(from, rest);
  }
  else {
    path = ModelPath(from, arc->id);
  }

  if (!isList)
    item = NEW_CONSTR(FileEC, (arc, path, mRoot, true, eL));
  return NEW_CONSTR(BindEC, (arc, item, eL));
}

Expr IncList(const Text& from, bool hasFrom) {
  BindingEC *imports = NEW_CONSTR(BindingEC, (5, token.loc));
  bool looking = true;
  
  while (looking && AtArc()) {
    imports->AddExpr(IncItem(from, hasFrom, false));
    looking = EatToken(TkComma);
  }
  Expect(TkRBracket, "`]' to terminate import list.");
  return imports;
}

Text DelimPath(Name& arc, bool arcSeen) {
  Text path;
  
  if (arcSeen) {
    if (!AtDelimiter())
      return arc->id;
    path = arc->id;
  }
  /*
  else if (EatArc(arc)) {
    path = arc->id;
  }
  */  
  else {
    if (AtDelimiter()) {
      path += token.AsText();
      NextToken();
    }
    if (EatArc(arc)) {
      path += arc->id;
    }
    else {
      PError("Must be an arc: identifier, integer, or string.");
    }
  }

  while (AtDelimiter()) {
    path += token.AsText();
    NextToken();
    if (EatArc(arc))
      path += arc->id;
    else break;
  }
  return path;
}

Expr Block() {
  SrcLoc *eL = token.loc;
  StmtListEC *assocs = NEW_CONSTR(StmtListEC, (token.loc));
  bool looking = true;

  Expect(TkLBrace, "`{' at start of block.");
  while (looking && !EatToken(TkReturn) && !EatToken(TkValue)) {
    assocs->AddExpr(Stmt());
    looking = EatToken(TkSemicolon);
  }
  if (looking) {
    bool ret = (eatenToken.tclass == TkReturn);
    Expr body = Expression();
    looking = EatToken(TkSemicolon);
    Expect(TkRBrace, "`}' at end of block.");
    return NEW_CONSTR(BlockEC, (assocs, body, ret, eL));
  }
  else
    PError("Expecting semicolon.\n");
  return NULL; // not reached
}

Expr Stmt() {
  Name name;
  bool noCache = false;
  bool noDup   = false;

  while (EatPragma()) {
    if(!strcmp(eatenToken.Bytes(), pragmaNoCache)) {
	    noCache = true;
    } else if(!strcmp(eatenToken.Bytes(), pragmaNoDup)) {
	     noDup = true;
    }
  }
  if (EatName(name)) {
    if (AtToken(TkLParen)) {
      return NEW_CONSTR(AssignEC, 
		(name, Func(name->id, noCache, noDup), true, name->loc));
    }
    else {
      return NEW_CONSTR(AssignEC, 
			(name, Assign(name), false, name->loc));
    }
  }
  if (EatToken(TkType)) {
    return TypeDef();
  }
  if (AtToken(TkForeach)) {
    return Iterate();
  }
  PError("Expected assignment, function def, type def, or iteration.\n");
  return NULL; // not reached
}

Expr Func(const Text& name, bool noCache, bool noDup) {
  SrcLoc *eL = token.loc;
  Expr type = NULL;

  if (EatToken(TkLParen))
    {
      ArgList l_formals = Formals();
      Expr l_body = Func(name + "()", noCache, noDup);
      return NEW_CONSTR(FuncEC, (noCache, noDup, name, l_formals, l_body, eL));
    }
  if (EatToken(TkColon))
    type = Type();
  return Block();
}

ArgList Formals() {
  ArgList formals = NEW_CONSTR(ArgListEC, (5, token.loc));
  Name name;
  bool looking = true;
  bool eqSeen = false;
  Expr type = NULL;

  if (EatToken(TkRParen)) {
    return formals;
  }
  while (looking) {
    SrcLoc *eL = token.loc;
    bool inPK = false;
    if (EatPragma()) {
      inPK = !strcmp(eatenToken.Bytes(), pragmaInPK);
    }
    if (EatName(name)) {
      if (name->id == nameDot) {
	PError("`.' not allowed as a formal parameter.\n");
      }
      for (int i = 0; i < formals->elems.size(); i++) {
	if (((Name)formals->elems.get(i))->id == name->id)
	  PError(Text("`") + name->id 
		 + Text("' occurs more than once in parameter list.\n"));
      }
      if (EatToken(TkColon)) type = Type();
      eqSeen = (eqSeen || AtToken(TkEqual));
      if (eqSeen) {
	Expect(TkEqual, "`=' for parameter default.");
	formals->AddExpr(NEW_CONSTR(AssignEC, (name, Expression(), false, eL)),
			 inPK);
      }
      else {
	name->ClearFreeVars();
	formals->AddExpr(name, inPK);
      }
      looking = EatToken(TkComma);
    }
    else {
      // Error: a formal must be an Id.
      Expect(TkId, "identifier for formals.");
    }
  }
  assert(formals->Length() < 65);
  Expect(TkRParen, "`)' at end of formal list.");
  return formals;
}

Expr Assign(Name name) {
  Text op;
  Expr type = NULL;

  if (EatToken(TkColon))
    type = Type();
  if (AtToken(TkPlus) || AtToken(TkPlusPlus) || AtToken(TkMinus) ||
      AtToken(TkStar)) {
    op = token.AsText();
    NextToken();
  }
  Expect(TkEqual, "`=' in assignment.");
  Expr e = Expression();
  if (op.Empty())
    return e;
  return NEW_CONSTR(ApplyOpEC, (name, op, e, name->loc));
}

Expr Iterate() {
  SrcLoc *eL = token.loc;
  
  Expect(TkForeach, "`foreach' or other statement starter.");
  Expr control = Control();
  Expect(TkIn, "`in' following iteration variable.");
  Expr e = Expression();
  Expect(TkDo, "`do' following iteration expression.");
  StmtListEC *stmts = NEW_CONSTR(StmtListEC, (token.loc));

  bool looking = true;
  if (EatToken(TkLBrace)) {
    while (looking && !AtToken(TkRBrace)) {
      stmts->AddExpr(Stmt());
      looking = EatToken(TkSemicolon);
    }
    Expect(TkRBrace, "`}' following statements of iteration body.");
  }
  else
    stmts->AddExpr(Stmt());;
  return NEW_CONSTR(IterateEC, (control, e, stmts, eL));
}

Expr Control() {
  SrcLoc *eL = token.loc;
  Name name1, name2;
  Expr type = NULL;
  
  if (EatToken(TkLBracket)) {
    name1 = GetName();
    if (EatToken(TkColon)) type = Type();
    Expect(TkEqual, "`=' in iteration control pair.");
    name2 = GetName();
    if (EatToken(TkColon)) type = Type();
    Expect(TkRBracket, "`]' following iteration control pair.");
    return NEW_CONSTR(PairEC, (name1, name2, eL));
  }
  name1 = GetName();
  if (EatToken(TkColon)) type = Type();
  return name1;
}

Expr Expression() {
  SrcLoc *eL = token.loc;

  if (EatToken(TkIf)) {
    Expr e1 = Expression();
    Expect(TkThen, "`then' following `if' expression.");
    Expr e2 = Expression();
    Expect(TkElse, "`else' following `then' expression.");
    return NEW_CONSTR(IfEC, (e1, e2, Expression(), eL));
  }
  return Expr1();
}

Expr Expr1() {
  SrcLoc *eL = token.loc;

  Expr e = Expr2();
  while (EatToken(TkImplies))
    e = NEW_CONSTR(ApplyOpEC, (e, "=>", Expr2(), eL));
  return e;
}

Expr Expr2() {
  SrcLoc *eL = token.loc;

  Expr e = Expr3();
  while (EatToken(TkOr))
    e = NEW_CONSTR(ApplyOpEC, (e, "||", Expr3(), eL));
  return e;
}

Expr Expr3() {
  SrcLoc *eL = token.loc;

  Expr e = Expr4();
  while (EatToken(TkAnd))
    e = NEW_CONSTR(ApplyOpEC, (e, "&&", Expr4(), eL));
  return e;
}

Expr Expr4() {
  SrcLoc *eL = token.loc;

  Expr e = Expr5();
  if (AtToken(TkGreater)) {
    TokenLookAhead();
    if ((nextToken.tclass == TkNumber)    ||
	(nextToken.tclass == TkString)    ||
	(nextToken.tclass == TkId)        ||
	(nextToken.tclass == TkTrue)      ||
	(nextToken.tclass == TkFalse)     ||
	(nextToken.tclass == TkErr)       ||
	(nextToken.tclass == TkMinus)     ||
	(nextToken.tclass == TkBang)      ||
	(nextToken.tclass == TkLParen)    ||
	(nextToken.tclass == TkLess)      ||
	(nextToken.tclass == TkLBracket)  ||
	(nextToken.tclass == TkLBrace)) {
      // > is treated as arithmetic operator.
      EatToken(TkGreater);
      Text l_op = eatenToken.AsText();
      return NEW_CONSTR(ApplyOpEC, (e, l_op, Expr5(), eL));
    }
  }
  else if (EatToken(TkEqEq)    ||
	   EatToken(TkNotEq)   ||
	   EatToken(TkLess)    ||
	   EatToken(TkLessEq)  ||
	   EatToken(TkGreaterEq))
    {
      Text l_op = eatenToken.AsText();
      return NEW_CONSTR(ApplyOpEC, (e, l_op, Expr5(), eL));
    }
  return e;
}

Expr Expr5() {
  SrcLoc *eL = token.loc;
  Text op;

  Expr e = Expr6();
  while (AtToken(TkPlus) || AtToken(TkPlusPlus) ||
	 AtToken(TkMinus)) {
    op = token.AsText();
    NextToken();
    e = NEW_CONSTR(ApplyOpEC, (e, op, Expr6(), eL));
  }
  return e;
}

Expr Expr6() {
  SrcLoc *eL = token.loc;

  Expr e = Expr7();
  while (AtToken(TkStar)) {
    NextToken();
    e = NEW_CONSTR(ApplyOpEC, (e, "*", Expr7(), eL));
  }
  return e;
}

Expr Expr7() {
  SrcLoc *eL = token.loc;
  
  if (EatToken(TkMinus))
    return NEW_CONSTR(ApplyUnOpEC, ("-", Primary(), eL));
  if (EatToken(TkBang))
    return NEW_CONSTR(ApplyUnOpEC, ("!", Primary(), eL));
  return Expr8();
}

Expr Expr8() {
  SrcLoc *eL = token.loc;
  Expr e = Primary();
  Expr type = NULL;

  if (EatToken(TkColon))
    type = Type();
  return e;
}

Expr Primary() {
  SrcLoc *eL = token.loc;
  Expr primary;
  ExprList elems;

  switch (token.tclass) {
  case TkId:
    {
      Name name = (Name)token.expr;
      PrimExec prim = LookupPrim(name->id);
      if (prim == NULL)
	primary = name;
      else
	primary = NEW_CONSTR(PrimitiveEC, (name->id, prim, eL));
      NextToken();
      break;
    }
  case TkString: case TkNumber:
    primary = token.expr;
    NextToken();
    return primary;
  case TkErr:
    NextToken();
    return NEW_CONSTR(ErrorEC, (token.loc, false));
  case TkTrue:
    NextToken();
    return NEW_CONSTR(ConstantEC, (valTrue, eL));
  case TkFalse:
    NextToken();
    return NEW_CONSTR(ConstantEC, (valFalse, eL));
  case TkLParen:
    NextToken();
    primary = Expression();
    Expect(TkRParen, "`)' to match `(' in primary.");
    break;
  case TkLBracket:              // constructing a binding
    NextToken();
    if (EatToken(TkRBracket))
      primary = NEW_CONSTR(BindingEC, (0, eL));
    else
      primary = BindingCons(BindElem());
    break;
  case TkLess:                  // constructing a List
    NextToken();
    if (EatToken(TkGreater))
      primary = NEW_CONSTR(ListEC, (0, eL));
    else
      primary = ListCons(Expression());
    break;
  case TkLBrace:
    primary = Block();
    break;
  default:
    PError(Text("Expected primary, saw `") + token.AsText() + "'.\n");
    break;
  }
  while (true) {
    if (EatToken(TkSlash) || EatToken(TkBackSlash))
      primary = NEW_CONSTR(SelectEC, (primary, GenArc(), false, primary->loc));
    else if (EatToken(TkBang))
      primary = NEW_CONSTR(SelectEC, (primary, GenArc(), true, primary->loc));
    else if (EatToken(TkLParen))
      primary = NEW_CONSTR(ApplyEC, (primary, Actuals(), primary->loc));
    else return primary;
  }
}

Expr GenArc() {
  Name name;
  Expr computed;
  
  if (EatArc(name)) {
    name->ClearFreeVars();
    return name;
  }
  if (EatToken(TkDollar)) {
    if (EatToken(TkLParen)) {
      computed = Expression();
      Expect(TkRParen, "`)' at end of computed field.");
    }
    else
      computed = GetName();
  }
  else if (EatToken(TkPercent)) {
    computed = Expression();
    Expect(TkPercent, "`%' at end of computed field.");
  }
  else
    Expect(TkErr, "name in binding field.");
  return NEW_CONSTR(ComputedEC, (computed));
}

Expr BindingCons(Expr e) {
  BindingEC *be = NEW_CONSTR(BindingEC, (5, e->loc));
  
  be->AddExpr(e);
  while (EatToken(TkComma) && !AtToken(TkRBracket))
    be->AddExpr(BindElem());
  Expect(TkRBracket, "`]' to end binding.");
  return be;
}

Expr BindElem() {
  Expr name = GenArc();

  if (EatDelimiter()) {
    if (EatToken(TkEqual))
      return NEW_CONSTR(BindEC, (name, Expression(), name->loc));
    BindingEC *be = NEW_CONSTR(BindingEC, (1, token.loc));
    be->AddExpr(BindElem());
    return NEW_CONSTR(BindEC, (name, be, name->loc));
  }
  if (EatToken(TkEqual))
    return NEW_CONSTR(BindEC, (name, Expression(), name->loc));
  if (eatenToken.tclass == TkId &&
      name->kind == NameEK) {
    Expr name1 = NEW_CONSTR(NameEC, (((NameEC*)name)->id, name->loc));
    return NEW_CONSTR(BindEC, (name, name1, name->loc));
  }
  PError(Text("Expected identifier.\n"));
  return NULL; // not reached
}

Expr ListCons(Expr e) {
  ListEC *elst = NEW_CONSTR(ListEC, (5, e->loc));

  elst->AddExpr(e);
  while (EatToken(TkComma) && !AtToken(TkGreater))
    elst->AddExpr(Expression());
  Expect(TkGreater, "`>' to end list.");
  return elst;
}

ArgList Actuals() {
  SrcLoc *eL = token.loc;
  ArgList actuals = NEW_CONSTR(ArgListEC, (5, token.loc));
  bool looking = true;

  while (looking && !AtToken(TkRParen)) {
    actuals->AddExpr(Expression(), false);
    looking = EatToken(TkComma);
  }
  Expect(TkRParen, "`)' at end of actual parameter list.");
  return actuals;
}

Expr TypeDef() {
  Name name;

  if (EatName(name)) {
    Expect(TkEqual, "`=' following identifier in type definition.");
    return NEW_CONSTR(AssignEC, (name, Type(), false, name->loc));
  }
  else
    PError("Expected type identifier.\n");
  return NULL; // not reached
}

Expr Type() {
  bool looking = true;
  Name name;
  SrcLoc *eL;

  if (EatToken(TkBool) || EatToken(TkInt) || EatToken(TkText))
    eL = token.loc;
  else if (EatToken(TkList)) {
    eL = token.loc;
    if (EatToken(TkLBracket)) {
      // This is here only for backward compatible.
      Type();
      Expect(TkRBracket, "`]' to close list type.");
    }
    else if (EatToken(TkLParen)) {
      Type();
      Expect(TkRParen, "`)' to close list type.");
    }
  }
  else if (EatToken(TkBinding)) {
    eL = token.loc;
    if (EatToken(TkLBracket)) {
      // This is here only for backward compatible.
      if (EatToken(TkColon))
	Type();
      else {
	while (looking && EatName(name)) {
	  if (EatToken(TkColon))
	    Type();
	  looking = EatToken(TkComma);
	}
      }
      Expect(TkRBracket, "`]' to close binding type.");
    }
    else if (EatToken(TkLParen)) {
      if (EatToken(TkColon))
	Type();
      else {
	while (looking && EatName(name)) {
	  if (EatToken(TkColon))
	    Type();
	  looking = EatToken(TkComma);
	}
      }
      Expect(TkRParen, "`)' to close binding type.");
    }
  }
  else if (EatToken(TkFunction)) {
    eL = token.loc;
    if (EatToken(TkLParen)) {
      while (looking && !AtToken(TkRParen)) {
	if (EatToken(TkId)) {
	  if (EatToken(TkColon)) Type();
	} 
	else
	  Type();
	looking = EatToken(TkComma);
      }
      Expect(TkRParen, "`)' to close function type.");
    }
    if (EatToken(TkColon))
      Type();
  }
  else {
    eL = token.loc;
    Expect(TkId, "type identifier or type expression.");
  }
  return NEW_CONSTR(BaseTEC, ("<Type>", eL));
}

// Parse a Vesta model:
Expr Parse(istream *lexIn, const Text& name, ShortId sid, 
	   VestaSource *modelRoot) {
  InitState(lexIn, name, sid, modelRoot);
  Expr result = Model();
  token.LexFlush();
  return result;
}
