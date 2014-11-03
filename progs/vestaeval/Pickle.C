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

// File: Pickle.C

#include "Pickle.H"
#include "Parser.H"
#include "Expr.H"
#include "Val.H"
#include "Err.H"
#include "Files.H"
#include "WordKey.H"
#include "ThreadData.H"
#include <VDirSurrogate.H>
#include <FP.H>
#include <Atom.H>
#include <BufStream.H>

using std::ios;
using std::ostream;
using std::endl;
using Basics::BufStream;
using Basics::OBufStream;
using OS::cio;

class PickleC {
public:
  // Consturctor for pickling
  PickleC()
    : size(0), shortId(NullShortId), linePos(0), read_size(0)
    { bytes = NEW(BufStream); }

  // Constructor for unpickling
  PickleC(char* str, int sz, const PrefixTbl& prefix, const Context& c)
    : size(sz), con(c), shortId(NullShortId), linePos(0), prefix(prefix),
      read_size(0)
      {
	bytes = NEW_CONSTR(BufStream, (str, sz, sz));
      }

  void PickleInt8(Basics::int8 n);
  void UnpickleInt8(Basics::int8& n);

  void PickleInt16(Basics::uint16 n);
  void UnpickleInt16(Basics::uint16& n);

  void PickleInt32(Basics::uint32 n);
  void UnpickleInt32(Basics::uint32& n);

  inline void PickleInt(int n) { PickleInt32(n); }
  inline void UnpickleInt(int& n)
  {
    Basics::uint32 un;
    UnpickleInt32(un);
    n = (int) un;
  }

  void PickleInt64(Basics::uint64 n);
  void UnpickleInt64(Basics::uint64& n);

  void PickleText(const Text& t);
  void UnpickleText(Text& t);
  // Same as UnpickleText, but fill an Atom instead.
  void UnpickleAtom(Atom& t);

  // Same as PickleText/UnpickleText, but uses a 32-bit integer for
  // the length.
  void PickleLText(const Text& t);
  void UnpickleLText(Text& t);

  // Pickle booleans in a dependable size, because sizeof(bool) can
  // change from compiler to compiler.  (The GNU compiler on Alpha
  // Linux has sizeof(bool) = 8, most others have sizeof(bool) = 1.)
  inline void PickleBool(bool v) { PickleInt8(v ? 1 : 0); }
  inline void UnpickleBool(bool& v)
  {
    Basics::int8 wv;
    UnpickleInt8(wv);
    v = (wv != 0);
  }

  void PickleLongId(const LongId &lid);
  void UnpicklLongId(LongId &lid);

  bool PickleVal(bool cut, Val value);
  bool UnpickleVal(bool cut, /*OUT*/ Val& value);
  Derived::IndicesApp dis;
  PrefixTbl prefix;

  // These are intended to be used after pickling a value.
  inline const char *getBytes()
  {
    return bytes->str();
  }
  inline int getSize()
  {
    unsigned int result = bytes->tellp();
    assert(size == result);
    return result;
  }

  // This is for use after unpickling.
  bool checkNumBytesRead();
private:
  BufStream *bytes;
  int size, read_size;
  Context con;
  Text fileName;
  ShortId shortId;
  int linePos;
  PrefixTbl::PutTbl nameTbl;
  bool PickleLocation(SrcLoc *loc);
  bool UnpickleLocation(SrcLoc*& loc);
  bool PickleContext(const Context& c);
  bool UnpickleContext(Context& c);
  void PickleDepPath(const DepPath& dp);
  DepPath UnpickleDepPath();
  void PickleDPS(DPaths *ps);
  bool UnpickleDPS(DPaths*& ps);
  void CollectDIs(Val value);
  void PickleExpr(Expr expr);
  bool UnpickleExpr(Expr& expr);
};

// Right now there are two levels of paranoia:
// 0 - No paranoid checking, just write the pickle
// 1 - Read back every integer stored in the pickle
static unsigned int pickling_paranoia = 0;

extern "C" void
PickleInit_inner()
{
  if(VestaConfig::is_set("Evaluator", "PickleParanoia"))
    {
      pickling_paranoia = VestaConfig::get_int("Evaluator", "PickleParanoia");
    }
}
void PickleInit()
{
  static pthread_once_t init_once = PTHREAD_ONCE_INIT;
  pthread_once(&init_once, PickleInit_inner);
}

// Pickle version history:
// 2 - All integers pickled in network byte order.  (KCS, 2003-02-05)
// 3 - Changed 16-bit integer counts to 32-bit integer counts. (IF, 2006-08-16)
// 4 - Added waitdup pragma to functions and models. (STV, 2006-11-13)
// 5 - Support for larger FV sets: store dependency path PrefixTbl
// indecies as 32-bit numbers (KCS, 2007-11-06)
int currentPickleVersion = 5;

void PickleC::PickleInt8(Basics::int8 n)
{
  FS::Write(*bytes, (char*)&n, sizeof_assert(n, 1));
  size += sizeof(n);

  if(pickling_paranoia > 0)
    {
      // Paranoia (see comment in PickleInt16)
      bytes->seekg(size - sizeof(n));
      Basics::int8 n_readback;
      FS::Read(*bytes, (char*)&n_readback, sizeof(n));
      assert(n_readback == n);
    }
}

void PickleC::PickleInt16(Basics::uint16 n)
{
  Basics::uint16 n_net = Basics::hton16(n);
  FS::Write(*bytes, (char*)&n_net, sizeof_assert(n_net, 2));
  size += sizeof(n_net);

  if(pickling_paranoia > 0)
    {
      // Paranoia: we've seen some incorrect 16-bit integers in
      // apparently corrupt pickled values.  This may prevent such a
      // value from making it into the cache.
      bytes->seekg(size - sizeof(n));
      Basics::int16 n_net_readback;
      FS::Read(*bytes, (char*)&n_net_readback, sizeof(n));
      assert(Basics::ntoh16(n_net_readback) == n);
    }
}

void PickleC::PickleInt32(Basics::uint32 n)
{
  Basics::uint32 n_net = Basics::hton32(n);
  FS::Write(*bytes, (char*)&n_net, sizeof_assert(n_net, 4));
  size += sizeof(n_net);

  if(pickling_paranoia > 0)
    {
      // Paranoia (see comment in PickleInt16)
      bytes->seekg(size - sizeof(n));
      Basics::int32 n_net_readback;
      FS::Read(*bytes, (char*)&n_net_readback, sizeof(n));
      assert(Basics::ntoh32(n_net_readback) == n);
    }
}

void PickleC::PickleInt64(Basics::uint64 n)
{
  Basics::uint64 n_net = Basics::hton64(n);
  FS::Write(*bytes, (char*)&n_net, sizeof_assert(n_net, 8));
  size += sizeof(n_net);

  if(pickling_paranoia > 0)
    {
      // Paranoia (see comment in PickleInt16)
      bytes->seekg(size - sizeof(n));
      Basics::int64 n_net_readback;
      FS::Read(*bytes, (char*)&n_net_readback, sizeof(n));
      assert(Basics::ntoh64(n_net_readback) == n);
    }
}

void PickleC::UnpickleInt8(Basics::int8& n)
{
  FS::Read(*bytes, (char*)&n, sizeof_assert(n, 1));
  read_size += sizeof(n);
}

void PickleC::UnpickleInt16(Basics::uint16& n)
{
  Basics::uint16 n_net;
  FS::Read(*bytes, (char*)&n_net, sizeof_assert(n_net, 2));
  n = Basics::ntoh16(n_net);
  read_size += sizeof(n_net);
}

void PickleC::UnpickleInt32(Basics::uint32& n)
{
  Basics::uint32 n_net;
  FS::Read(*bytes, (char*)&n_net, sizeof_assert(n_net, 4));
  n = Basics::ntoh32(n_net);
  read_size += sizeof(n_net);
}

void PickleC::UnpickleInt64(Basics::uint64& n)
{
  Basics::uint64 n_net;
  FS::Read(*bytes, (char*)&n_net, sizeof_assert(n_net, 8));
  n = Basics::ntoh64(n_net);
  read_size += sizeof(n_net);
}

void PickleC::PickleText(const Text& t)
{
  Basics::uint16 len = t.Length();
  assert(len == t.Length());
  PickleInt16(len);
  FS::Write(*bytes, t.chars(), len);
  size += len;
}

void PickleC::UnpickleText(Text& t)
{
  Basics::uint16 len;
  UnpickleInt16(len);
  if (len > size)
    throw FS::EndOfFile();
  char *s = NEW_PTRFREE_ARRAY(char, len+1);
  FS::Read(*bytes, s, len);
  read_size += len;
  s[len] = 0;
  t = Text(s, (void*)s);
}

void PickleC::UnpickleAtom(Atom& a)
{
  Basics::uint16 len;
  UnpickleInt16(len);
  if (len > size)
    throw FS::EndOfFile();
  char *s = NEW_PTRFREE_ARRAY(char, len+1);
  FS::Read(*bytes, s, len);
  read_size += len;
  s[len] = 0;
  a = Atom(s, (void*)s);
}

void PickleC::PickleLText(const Text& t)
{
  Basics::uint32 len = t.Length();
  assert(len == t.Length());
  PickleInt32(len);
  FS::Write(*bytes, t.chars(), len);
  size += len;
}

void PickleC::UnpickleLText(Text& t)
{
  Basics::uint32 len;
  UnpickleInt32(len);
  if (len > size)
    throw FS::EndOfFile();
  char *s = NEW_PTRFREE_ARRAY(char, len+1);
  FS::Read(*bytes, s, len);
  read_size += len;
  s[len] = 0;
  t = Text(s, (void*)s);
}

void PickleC::PickleLongId(const LongId &lid)
{
  FS::Write(*bytes, (char*)lid.value.byte, sizeof_assert(lid.value.byte, 32));
  size += sizeof(lid.value.byte);
}

void PickleC::UnpicklLongId(LongId &lid)
{
  FS::Read(*bytes, (char*)lid.value.byte, sizeof_assert(lid.value.byte, 32));
  read_size += sizeof(lid.value.byte);
}

bool PickleC::PickleLocation(SrcLoc *loc) {
  // file name and sid are recorded only once, at the start of pickling
  // the expression of a closure.
  // pickle loc->line and loc->character:
  char lineDelta = (char)(loc->line - linePos);
  char charPos = (char)loc->character;
  PickleInt8(lineDelta);
  PickleInt8(charPos);
  // update the location:
  linePos = loc->line;
  return true;
}

bool PickleC::UnpickleLocation(SrcLoc*& loc) {
  // unpickle loc->line:
  char lineDelta;
  UnpickleInt8(lineDelta);
  linePos += (int)lineDelta;
  // unpickle loc->character:
  char charPos;
  UnpickleInt8(charPos);

  loc = NEW_CONSTR(SrcLoc, (linePos, (int)charPos, fileName, shortId));
  return true;
}

bool PickleC::PickleContext(const Context& c) {
  bool cacheit = true;
  Context cc = c;
  Basics::uint32 len = cc.Length();
  // Check for overflow of length integer
  assert(len == cc.Length());
  PickleInt32(len);
  unsigned long pickled_entry_count = 0;
  while (cacheit && !cc.Null()) {
    Assoc elem = cc.Pop();
    PickleText(elem->name);
    cacheit = PickleVal(true, elem->val);
    pickled_entry_count++;
  }
  // Make sure we pickled the number we said we would
  assert(!cacheit || (pickled_entry_count == len));
  return cacheit;
}

bool PickleC::UnpickleContext(Context& c) {
  bool success = true;
  Basics::uint32 cLen;
  UnpickleInt32(cLen);
  Val val;
  while (success && cLen--) {
    Atom name;
    UnpickleAtom(name);
    success = UnpickleVal(true, val);
    c.Append1D(NEW_CONSTR(AssocVC, (name, val)));
  }
  return success;
}

void PickleC::PickleDepPath(const DepPath& dp) {
  // prefix encoding:
  Basics::uint32 index = prefix.Put(*dp.content->path, nameTbl);
  // path kind:
  char pk = (char)dp.content->pKind;
  PickleInt8(pk);
  // prefix index:
  PickleInt32(index);
}

DepPath PickleC::UnpickleDepPath() {
  // path kind:
  char pk;
  UnpickleInt8(pk);
  // arcs:
  Basics::uint32 index;
  UnpickleInt32(index);
  ArcSeq *path = prefix.Get(index);
  // fingerprint: pathFP is not in the pickle.
  return DepPath(path, (PathKind)pk);
}

void PickleC::PickleDPS(DPaths *ps) {
  Basics::uint32 len = 0;
  if (ps != NULL) {
    len = ps->Size();
    // Check for length overflow
    assert(len == ps->Size());
  }
  PickleInt32(len);

  unsigned long pickled_path_count = 0;
  if (ps != NULL) {
    DepPathTbl::TIter iter(ps);  
    DepPathTbl::KVPairPtr ptr;
    while (iter.Next(ptr))
      {
	PickleDepPath(ptr->key);
	pickled_path_count++;
      }
  }
  // Make sure we pickled the number we said we would
  assert(pickled_path_count == len);
}

bool PickleC::UnpickleDPS(DPaths*& ps) {
  Basics::uint32 len;
  UnpickleInt32(len);
  
  if (len > 0) {
    ps = NEW_CONSTR(DPaths, (len));
    while (len-- > 0) {
      DepPath newPath(UnpickleDepPath());
      DepPathTbl::KVPairPtr dummyPtr;
      Val val = LookupPath(&newPath, this->con);
      ps->Put(newPath, val, dummyPtr);
    }
  }
  return true;
}

void PickleC::CollectDIs(Val value) {
  /* Collect all the deriveds in the value. */
  switch (value->vKind) {
  case ListVK:
    {
      Vals elems = ((ListVC*)value)->elems;
      while (!elems.Null())
	CollectDIs(elems.Pop());
      break;
    }
  case BindingVK:
    {
      Context elems = ((BindingVC*)value)->elems;
      while (!elems.Null())
	CollectDIs(elems.Pop()->val);
      break;
    }
  case TextVK:
    {
      TextVC *tv = (TextVC*)value;
      if (!tv->HasTxt()) dis.Append(tv->Sid());
      break;
    }
  case ClosureVK:
    {
      ClosureVC *cl = (ClosureVC*)value;
      Text name(cl->func->name);
      Context work = cl->con;
      while (!work.Null()) {
	Assoc a = work.Pop();
	if (a->name != name) CollectDIs(a->val);
      }
      break;
    }
  case ModelVK:
    {
      dis.Append(((ModelVC*)value)->content->mRoot->shortId());
      break;
    }
  default:
    break;
  }
  return;
}

void PickleC::PickleExpr(Expr expr) {
  PickleLocation(expr->loc);

  char ek = (char)expr->kind;
  PickleInt8(ek);
  switch (ek) {
  case ConstantEK:
    {
      PickleVal(true, ((ConstantEC*)expr)->val);
      break;
    }
  case IfEK:
    {
      IfEC *ife = (IfEC*)expr;
      PickleExpr(ife->test);
      PickleExpr(ife->then);
      PickleExpr(ife->els);
      break;
    }
  case ComputedEK:
    {
      PickleExpr(((ComputedEC*)expr)->name);
      break;
    }
  case ExprListEK:
    {
      Exprs es = ((ExprListEC*)expr)->elems;
      Basics::uint32 len = es.size();
      // Check for overflow of length integer
      assert(len == es.size());
      PickleInt32(len);
      for (int i = 0; i < len; i++) {
	PickleExpr(es.get(i));
      }
      break;
    }
  case ArgListEK:
    {
      Exprs es = ((ArgListEC*)expr)->elems;
      PickleInt64(((ArgListEC*)expr)->inPKs);
      Basics::uint32 len = es.size();
      // Check for overflow of length integer
      assert(len == es.size());
      PickleInt32(len);
      for (int i = 0; i < len; i++) {
	PickleExpr(es.get(i));
      }
      break;
    }
  case StmtListEK:
    {
      Exprs es = ((StmtListEC*)expr)->elems;
      Basics::uint32 len = es.size();
      // Check for overflow of length integer
      assert(len == es.size());
      PickleInt32(len);
      for (int i = 0; i < len; i++) {
	PickleExpr(es.get(i));
      }
      break;
    }
  case ListEK:
    {
      Exprs es = ((ListEC*)expr)->elems;
      Basics::uint32 len = es.size();
      // Check for overflow of length integer
      assert(len == es.size());
      PickleInt32(len);
      for (int i = 0; i < len; i++) {
	PickleExpr(es.get(i));
      }
      break;
    }
  case AssignEK:
    {
      AssignEC *ae = (AssignEC*)expr;
      PickleExpr(ae->lhs);
      PickleExpr(ae->rhs);
      PickleBool(ae->isFunc);
      break;
    }
  case BindEK:
    {
      BindEC *be = (BindEC*)expr;
      PickleExpr(be->lhs);
      PickleExpr(be->rhs);
      break;
    }
  case NameEK:
    {
      NameEC *ne = (NameEC*)expr;
      PickleText(ne->id);
      break;
    }
  case BindingEK:
    {
      Exprs assocs = ((BindingEC*)expr)->assocs;
      Basics::uint32 len = assocs.size();
      // Check for overflow of length integer
      assert(len == assocs.size());
      PickleInt32(len);
      for (int i = 0; i < len; i++) {
	PickleExpr(assocs.get(i));
      }
      break;
    }
  case ApplyOpEK:
    {
      ApplyOpEC *ae = (ApplyOpEC*)expr;
      PickleText(ae->op);
      PickleExpr(ae->e1);
      PickleExpr(ae->e2);
      break;
    }
  case ApplyUnOpEK:
    {
      ApplyUnOpEC *ae = (ApplyUnOpEC*)expr;
      PickleText(ae->op);
      PickleExpr(ae->e);
      break;
    }
  case ModelEK:
    {
      ModelEC *me = (ModelEC*)expr;
      PickleExpr(me->files);
      PickleExpr(me->imports);
      PickleBool(me->noDup);
      PickleExpr(me->block);
      PickleLongId(me->modelRoot->longid);
      break;
    }
  case FileEK:
    {
      FileEC *fe = (FileEC*)expr;
      // the name:
      PickleExpr(fe->name);
      // the local path:
      PickleText(fe->localPath);
      // the model root:
      PickleLongId(fe->modelRoot->longid);
      // the import flag:
      PickleBool(fe->import);
      break;
    }
  case PrimitiveEK:
    {
      PrimitiveEC *pe = (PrimitiveEC*)expr;
      PickleText(pe->name);
      break;
    }
  case PairEK:
    {
      PairEC *pe = (PairEC*)expr;
      PickleExpr(pe->first);
      PickleExpr(pe->second);
      break;
    }
  case SelectEK:
    {
      SelectEC *se = (SelectEC*)expr;
      PickleExpr(se->binding);
      PickleExpr(se->field);
      PickleBool(se->bang);
      break;
    }
  case FuncEK:
    {
      FuncEC *fe = (FuncEC*)expr;
      PickleText(fe->name);
      PickleBool(fe->noCache);
      PickleBool(fe->noDup);
      PickleExpr(fe->args);
      PickleExpr(fe->body);
      break;
    }
  case BlockEK:
    {
      BlockEC *be = (BlockEC*)expr;
      PickleExpr(be->assocs);
      PickleExpr(be->body);
      PickleBool(be->isReturn);
      break;
    }
  case IterateEK:
    {
      IterateEC *ie = (IterateEC*)expr;
      PickleExpr(ie->control);
      PickleExpr(ie->e);
      PickleExpr(ie->body);
      break;
    }
  case ApplyEK:
    {
      ApplyEC *ae = (ApplyEC*)expr;
      PickleExpr(ae->func);
      PickleExpr(ae->args);
      break;
    }
  case ErrorEK:
    break;
  default:
    {
      ostream &err = cio().start_err();
      Error(err, "Invalid expression type in pickling: ");
      err << ((unsigned int) ek) << endl;
      InternalError(err, cio().helper(true), "PickleC::PickleExpr");
      // Unreached (InternalError will abort)
      cio().end_err();
    }
  }
}

bool PickleC::UnpickleExpr(Expr& expr) {
  SrcLoc *loc;
  UnpickleLocation(loc);

  char ek;
  UnpickleInt8(ek);
  switch (ek) {
  case ConstantEK:
    {
      Val val;
      UnpickleVal(true, val);
      expr = NEW_CONSTR(ConstantEC, (val, loc));
      break;
    }
  case IfEK:
    {
      Expr test, then, els;
      UnpickleExpr(test);
      UnpickleExpr(then);
      UnpickleExpr(els);
      expr = NEW_CONSTR(IfEC, (test, then, els, loc));
      break;
    }
  case ComputedEK:
    {
      Expr name;
      UnpickleExpr(name);
      expr = NEW_CONSTR(ComputedEC, (name));
      break;
    }
  case ExprListEK:
    {
      Basics::uint32 len;
      UnpickleInt32(len);
      ExprListEC *elst = NEW_CONSTR(ExprListEC, (len, loc));
      Expr elem;
      while (len--) {
	UnpickleExpr(elem);
	elst->AddExpr(elem);
      }
      expr = elst;
      break;
    }
  case ArgListEK:
    {
      Bit64 inPKs;
      UnpickleInt64(inPKs);
      Basics::uint32 len;
      UnpickleInt32(len);
      ArgListEC *alst = NEW_CONSTR(ArgListEC, (len, loc));
      Expr elem;
      while (len--) {
	UnpickleExpr(elem);
	alst->AddExpr(elem, false);
      }
      alst->inPKs = inPKs;
      expr = alst;
      break;
    }
  case StmtListEK:
    {
      Basics::uint32 len;
      UnpickleInt32(len);
      StmtListEC *es = NEW_CONSTR(StmtListEC, (loc));
      Expr elem;
      while (len--) {
	UnpickleExpr(elem);
	es->AddExpr(elem);
      }
      expr = es;
      break;
    }
  case ListEK:
    {
      Basics::uint32 len;
      UnpickleInt32(len);
      ListEC *elst = NEW_CONSTR(ListEC, (len, loc));
      Expr elem;
      while (len--) {
	UnpickleExpr(elem);
	elst->AddExpr(elem);
      }
      expr = elst;
      break;
    }
  case AssignEK:
    {
      Expr lhs, rhs;
      UnpickleExpr(lhs);
      UnpickleExpr(rhs);
      bool isFunc;
      UnpickleBool(isFunc);
      expr = NEW_CONSTR(AssignEC, ((NameEC*)lhs, rhs, isFunc, loc));
      break;
    }
  case BindEK:
    {
      Expr lhs, rhs;
      UnpickleExpr(lhs);
      UnpickleExpr(rhs);
      expr = NEW_CONSTR(BindEC, (lhs, rhs, loc));
      break;
    }
  case NameEK:
    {
      Text id;
      UnpickleText(id);
      expr = NEW_CONSTR(NameEC, (id, loc));
      break;
    }
  case BindingEK:
    {
      Basics::uint32 len;
      UnpickleInt32(len);
      BindingEC *be = NEW_CONSTR(BindingEC, (len, loc));
      Expr elem;
      while (len--) {
	UnpickleExpr(elem);
	be->AddExpr(elem);
      }
      expr = be;
      break;
    }
  case ApplyOpEK:
    {
      // the operator:
      Text op;
      UnpickleText(op);
      // the operands:
      Expr e1, e2;
      UnpickleExpr(e1);
      UnpickleExpr(e2);
      expr = NEW_CONSTR(ApplyOpEC, (e1, op, e2, loc));
      break;
    }
  case ApplyUnOpEK:
    {
      // the operator:
      Text op;
      UnpickleText(op);
      // the operand:
      Expr e;
      UnpickleExpr(e);
      expr = NEW_CONSTR(ApplyUnOpEC, (op, e, loc));
      break;
    }
  case ModelEK:
    {
      Expr files, imports, block;
      bool noDup;
      UnpickleExpr(files);
      UnpickleExpr(imports);
      UnpickleBool(noDup);
      UnpickleExpr(block);
      LongId lid;
      UnpicklLongId(lid);
      VestaSource *modelRoot = lid.lookup();
      expr = NEW_CONSTR(ModelEC, 
			((ExprListEC*)files, (ExprListEC*)imports, noDup,
			 block, modelRoot, loc));
      break;
    }
  case FileEK:
    {
      Expr name;
      UnpickleExpr(name);
      Text localPath;
      UnpickleText(localPath);
      LongId lid;
      UnpicklLongId(lid);
      VestaSource *modelRoot = lid.lookup();
      bool import;
      UnpickleBool(import);
      expr = NEW_CONSTR(FileEC, 
			((NameEC*)name, localPath, modelRoot, import, loc));
      break;
    }
  case PrimitiveEK:
    {
      Text name;
      UnpickleText(name);
      expr = NEW_CONSTR(PrimitiveEC, (name, LookupPrim(name), loc));
      break;
    }
  case PairEK:
    {
      Expr first, second;
      UnpickleExpr(first);
      UnpickleExpr(second);
      expr = NEW_CONSTR(PairEC, (first, second, loc));
      break;
    }
  case SelectEK:
    {
      Expr binding, field;
      UnpickleExpr(binding);
      UnpickleExpr(field);
      bool bang;
      UnpickleBool(bang);
      expr = NEW_CONSTR(SelectEC, (binding, field, bang, loc));
      break;
    }
  case FuncEK:
    {
      Text name;
      UnpickleText(name);
      bool noCache;
      UnpickleBool(noCache);
      bool noDup;
      UnpickleBool(noDup);
      Expr args, body;
      UnpickleExpr(args);
      UnpickleExpr(body);
      expr = NEW_CONSTR(FuncEC, (noCache, noDup, name, (ArgListEC*)args, body, loc));
      break;
    }
  case BlockEK:
    {
      Expr assocs, body;
      UnpickleExpr(assocs);
      UnpickleExpr(body);
      bool isReturn;
      UnpickleBool(isReturn);
      expr = NEW_CONSTR(BlockEC, ((StmtListEC*)assocs, body, isReturn, loc));
      break;
    }
  case IterateEK:
    {
      Expr control, e, body;
      UnpickleExpr(control);
      UnpickleExpr(e);
      UnpickleExpr(body);
      expr = NEW_CONSTR(IterateEC, (control, e, (StmtListEC*)body, loc));
      break;
    }
  case ApplyEK:
    {
      Expr func, args;
      UnpickleExpr(func);
      UnpickleExpr(args);
      expr = NEW_CONSTR(ApplyEC, (func, (ArgListEC*)args, loc));
      break;
    }
  case ErrorEK:    
    {
      expr = NEW_CONSTR(ErrorEC, (loc, false));
      break;
    }
  default:
    throw("Bad expression type in unpickling.\n");
  }
  return true;
}

bool PickleC::PickleVal(bool cut, Val value) {
  if (!value->cacheit) return false;
  
  bool cacheit = true;
  
  // Pickle value->path:
  bool hasPath = (value->path != NULL);
  PickleBool(hasPath);
  if (hasPath)
    PickleDepPath(*(value->path));
  
  // Pickle subvalues of the value:
  if (hasPath && cut)
    CollectDIs(value);
  else {
    char vk = (char)value->vKind;
    PickleInt8(vk);
    switch (vk) {
    case BooleanVK:
      {
	PickleBool(((BooleanVC*)value)->b);
	break;
      }
    case IntegerVK:
      {
	PickleInt(((IntegerVC*)value)->num);
	break;
      }
    case PrimitiveVK:
      {
	PickleText(((PrimitiveVC*)value)->name);
	break;
      }
    case ListVK:
      {
	ListVC *lstv = (ListVC*)value;	
	Vals elems = lstv->elems;
	Basics::uint32 len = elems.Length();
	// Check for overflow of length integer
	assert(len == elems.Length());
	PickleInt32(len);
	unsigned long pickled_elem_count = 0;
	while (cacheit && !elems.Null())
	  {
	    cacheit = PickleVal(cut, elems.Pop());
	    pickled_elem_count++;
	  }
	// Make sure we pickled the number we said we would
	assert(!cacheit || (pickled_elem_count == len));
	PickleDPS(lstv->lenDps);
	break;
      }
    case BindingVK:
      {
	BindingVC *bv = (BindingVC*)value;
	Context elems = bv->elems;
	Basics::uint32 len = elems.Length();
	// Check for overflow of length integer
	assert(len == elems.Length());
	PickleInt32(len);
	unsigned int pickled_elem_count = 0;
	while (cacheit && !elems.Null()) {
	  Assoc a = elems.Pop();
	  PickleText(a->name);
	  cacheit = PickleVal(cut, a->val);
	  pickled_elem_count++;
	}
	// Make sure we pickled the number we said we would
	assert(!cacheit || (pickled_elem_count == len));
	PickleDPS(bv->lenDps);
	break;
      }
    case TextVK:
      {
	TextVC *tv = (TextVC*)value;
	// Pickle the value as a text value if it doesn't have an
	// underlying shortid or if it's short enough that pickling it
	// as a raw string would be shorter than pickling the shortid
	// representation.
	bool asTxt = !tv->HasSid() ||
	  (tv->HasTxt() && (tv->Length() < (tv->TName().Length() +
					    sizeof(ShortId) + FP::ByteCnt)));
	PickleBool(asTxt);
	if (asTxt) {
	  assert(tv->HasTxt());
	  PickleLText(tv->NDS());
	}
	else {
	  assert(tv->HasSid());
	  PickleText(tv->TName());
	  ShortId sid = tv->Sid();
	  PickleInt32(sid);
	  // Write fingerprint in network byte order.
	  tv->FingerPrint().Write(*bytes);
	  size += FP::ByteCnt;
	  dis.Append(sid);
	}
	break;
      }
    case ClosureVK:
      {
	ClosureVC *cl = (ClosureVC*)value;

	// Pickle the function body:
	SrcLoc *loc = cl->func->loc;
	PickleText(loc->file);
	PickleInt32(loc->shortId);
	linePos = loc->line;
	PickleInt(linePos);
	PickleExpr(cl->func);

	// Pickle context:
	Text name(cl->func->name);
	bool recursive;
	Context cc = Snip(cl->con, name, recursive);
	cacheit = PickleContext(cc);

	// Pickle name:
	if (!cacheit) break;
	PickleBool(recursive);
	PickleLText(name);
	break;
      }
    case ModelVK:
      {
	ModelVC *model = (ModelVC*)value;
	// Name of the model:
	PickleText(model->content->name);

	// Sid of the model:
	ShortId sid = model->content->sid;
	assert(sid != 0);
	PickleInt32(sid);

	// Lid, sid, and fptag for the model root:
	// Lid is no longer used.
	sid = model->content->mRoot->shortId();
	PickleInt32(sid);
        model->content->mRoot->fptag.Write(*bytes);
	size += FP::ByteCnt;
 
	// Fingerprint:
	dis.Append(sid);   // we add the sid of the parent directory
	// Pickle fingerprints in network byte order
	model->FingerPrintFile().Write(*bytes);
	size += FP::ByteCnt;
	model->FingerPrint().Write(*bytes);
	size += FP::ByteCnt;
	break;
      }
    case ErrorVK:
      break;
    default:
      {
	ostream &err = cio().start_err();
	Error(err, "Invalid value type in pickling: ");
	err << ((unsigned int) vk) << endl;
	InternalError(err, cio().helper(true), "PickleC::PickleVal");
	// Unreached (InternalError will abort)
	cio().end_err();
      }
    }
  }
  // value->dps:
  if(cacheit) 
    PickleDPS(value->dps);
  return cacheit;
}

bool PickleC::UnpickleVal(bool cut, /*OUT*/ Val& value) {
  bool success = true;
  
  // Unpickle value->path:
  bool hasPath;
  UnpickleBool(hasPath);
  DepPath *path = NULL;
  if (hasPath)
    path = NEW_CONSTR(DepPath, (UnpickleDepPath()));

  // Unpickle subvalues of the value:
  if (hasPath && cut) {
    Val val = LookupPath(path, con);
    value = val->Copy();
  }
  else {
    char vk;
    UnpickleInt8(vk);
    switch (vk) {
    case BooleanVK:
      {
	bool b;
	UnpickleBool(b);
	value = NEW_CONSTR(BooleanVC, (b));
	break;
      }
    case IntegerVK:
      {
	Basics::int32 n;
	UnpickleInt(n);
	value = NEW_CONSTR(IntegerVC, (n));
	break;
      }
    case PrimitiveVK:
      {
	Atom name;
	UnpickleAtom(name);
	value = NEW_CONSTR(PrimitiveVC, (name, LookupPrim(name)));
	break;
      }
    case ListVK:
      {
	Basics::uint32 len;
	UnpickleInt32(len);
	Vals elems;
	Val v;
	while ((len-- > 0) && UnpickleVal(cut, v))
	  elems.Append1D(v);
	value = NEW_CONSTR(ListVC, (elems));
	success = success && UnpickleDPS(((ListVC*)value)->lenDps);
	break;
      }
    case BindingVK:
      {
	Basics::uint32 len;
	UnpickleInt32(len);
	Context elems;
	Val v;
	while (success && len--) {
	  Atom name;
	  UnpickleAtom(name);
	  success = UnpickleVal(cut, v);
	  elems.Append1D(NEW_CONSTR(AssocVC, (name, v)));
	}
	value = NEW_CONSTR(BindingVC, (elems));
	success = success && UnpickleDPS(((BindingVC*)value)->lenDps);
	break;
      }
    case TextVK:
      {
	bool hasTxt;
	UnpickleBool(hasTxt);
	if (hasTxt) {
	  Text t;
	  UnpickleLText(t);
	  value = NEW_CONSTR(TextVC, (t));
	}
	else {
	  // Must have sid.
	  Text name;
	  UnpickleText(name);
	  ShortId sid;
	  UnpickleInt32(sid);
	  FP::Tag tag;
	  // Read fingerprint in network byte order.
	  tag.Read(*bytes);
	  read_size += FP::ByteCnt;
	  value = NEW_CONSTR(TextVC, (name, sid, tag));
	}
	break;
      }
    case ClosureVK:
      {
	// Unpickle the function body:
	UnpickleText(fileName);
	UnpickleInt32(shortId);
	UnpickleInt(linePos);
	Expr func;
	success = UnpickleExpr(func);

	// Unpickle context:
	Context c;
	if (success)
	  success = UnpickleContext(c);

	// unpickle name:
	Text name;
	bool recursive;
	if (success) {
	  UnpickleBool(recursive);
	  UnpickleLText(name);
	}

	// Form the closure:
	if (success) {
	  value = NEW_CONSTR(ClosureVC, ((FuncEC*)func, c, false));
	  if (recursive)
	    ((ClosureVC*)value)->con =
	      Context(NEW_CONSTR(AssocVC, (name, value)), c);
	}
	break;
      }
    case ModelVK:
      {
	// Name of the model:
	Text name;
	UnpickleText(name);

	// Sid of the model:
	ShortId sid;
	UnpickleInt32(sid);

	// VestaSource of the model root:
	// We leave master, pseudoInode, ac, rep, and attribs uninitialized,
	// since we know we do not need them.  We believe we do not need the
	// fptag, but we decided to pickle/unpickle them just in case.
	ShortId rootSid;
	UnpickleInt32(rootSid);
	FP::Tag rootTag;
	rootTag.Read(*bytes);
	read_size += FP::ByteCnt;
	VestaSource* root = NEW_CONSTR(VDirSurrogate, (0, rootSid));
        root->type = VestaSource::immutableDirectory;
	root->longid = LongId::fromShortId(rootSid);
	root->fptag = rootTag;
       
	// Fingerprint:
	FP::Tag tag;
	// Unpickle in network byte order
	tag.Read(*bytes);
	read_size += FP::ByteCnt;
	FP::Tag lidTag;
	lidTag.Read(*bytes);
	read_size += FP::ByteCnt;

	value = NEW_CONSTR(ModelVC, (name, sid, root, tag, lidTag));
	break;
      }
    case ErrorVK:
      {
	value = NEW_CONSTR(ErrorVC, (true));
	break;
      }
    default:
      {
	Error(cio().start_err(),
	      "Bad value type in unpickling. Cache data may be corrupted!\n");
	cio().end_err();
	success = false;
      }
    }
  }
  if (success) {
    value->path = path;
    success = UnpickleDPS(value->dps);
  }
  return success;
}

bool PickleC::checkNumBytesRead()
{
  assert((unsigned int) bytes->tellg() == read_size);

  // Did we read less than all the bytes of the pickle?
  if(size > read_size)
    {
      cio().start_err() << ThreadLabel() 
			<< "Unpickling didn't read complete pickle (read "
			<< read_size << " out of " << size << " bytes)" << endl
			<< "(This is probably a bug; please report it.)" << endl;
      cio().end_err();
      return false;
    }
  // Did we read past the end of the pickle?  (Should be impossible
  // with a corrct stream implementation, but can't hurt to check.)
  else if(size < read_size)
    {
      OBufStream l_msg;
      l_msg << "Read past end of pickle (read "
	    << read_size << " bytes, pickle was " << size << " bytes)";
      throw Evaluator::failure(l_msg.str(), false);
    }
  return true;
}

bool Pickle(Val value, /*OUT*/ VestaVal::T& vval) {
  PickleInit();
  PickleC pickle;
  bool cacheit;
  
  try {
    // Record in pickle the current pickle version:
    pickle.PickleInt(currentPickleVersion);
    // Do the work:
    cacheit = pickle.PickleVal(true, value);
  } catch (FS::Failure f) {
    ostream &err = cio().start_err();
    Error(err, "Pickling error: ");
    err << f << endl;
    InternalError(err, cio().helper(true), "Pickle");
    // Unreached (InternalError will abort)
    cio().end_err();
  } catch (PrefixTbl::Overflow) {
    // Just pass this on to the caller.
    throw;
  }
  
  if (cacheit) {
    vval.fp = value->FingerPrint();
    vval.bytes = (char *) pickle.getBytes();
    vval.len = pickle.getSize(); // Note: includes sanity check on size.
    vval.dis = pickle.dis;
    vval.prefixTbl = pickle.prefix;
  }
  return cacheit;
}

bool Unpickle(const VestaVal::T& vval, const Context& c, /*OUT*/ Val& value) {
  try {
    PickleC pickle(vval.bytes, vval.len, vval.prefixTbl, c);
    // Make sure pickle versions are matched:
    int pickleVersion = -1;
    pickle.UnpickleInt(pickleVersion);
    // Do the work:
    if (pickleVersion != currentPickleVersion) {
      OBufStream l_msg;
      l_msg << "Pickle version mismatch.  (Got " << pickleVersion
	    << ", expected " << currentPickleVersion << ")";
      throw Evaluator::failure(l_msg.str(), false);
    }
    bool success = pickle.UnpickleVal(true, value);
    // Sanity check that we read exactly the number of bytes in the
    // pickled value, no more no less.
    success = success && pickle.checkNumBytesRead();
    return success;
  } catch (FS::Failure f) {
    cio().start_err() << ThreadLabel() << "Unpickling error: " << f << endl
		      << "(This is probably a bug; please report it.)" << endl;
    cio().end_err();
  } catch (FS::EndOfFile f) {
    cio().start_err() << ThreadLabel() << "EOF while unpickling." << endl
		      << "(This is probably a bug; please report it.)" << endl;
    cio().end_err();
  } catch (const char* report) {
    cio().start_err() << ThreadLabel() << report << endl;
    cio().end_err();
  } catch (Evaluator::failure f) {
    cio().start_err() << ThreadLabel() << f.msg << endl; 
    cio().end_err();
    throw;   
  } catch (...) {
    cio().start_err() << ThreadLabel() << "Unknown exception in unpickling.\n";
    cio().end_err();
  }
  return false;
}
