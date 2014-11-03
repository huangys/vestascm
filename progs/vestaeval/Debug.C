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

// File: Debug.C

#include "Debug.H"
#include "Expr.H"

#include <ThreadIO.H>

using std::ostream;
using std::cout;
using std::endl;
using std::flush;
using OS::cio;

bool CacheIt(Val v) {
  if (!v->cacheit) {
    cio().start_err() << "Cannot be cached." << endl;
    cio().end_err();
    return false;
  }
  switch (v->vKind) {
  case BindingVK:
    {
      Binding b = (BindingVC*)v;
      Context elems = b->elems;
      while (!elems.Null()) {
	if (!CacheIt(elems.Pop()->val))
	  return false;
      }
      break;
    }
  case ListVK:
    {
      Vals elems = ((ListVC*)v)->elems;
      while (!elems.Null()) {
	if (!CacheIt(elems.Pop()))
	  return false;
      }
      break;
    }
  default:
    break;
  }
  return true;
}

void PrintVars(ostream& os, const Vars& fv) 
{
  Vars vars = fv;
  os << "< ";
  if (!vars.Null())
    os << vars.Pop();
  while (!vars.Null())
    os << ", " << vars.Pop();
  os << " >";
  os.flush();
}

void PrintTags(ostream& os, const FP::List& tags) 
{
  os << "{" << endl;
  for (int i = 0; i < tags.len; i++) {
    os << "  " << i << ". " << tags.fp[i] << endl;
  }
  os << "}" << endl;
  return;
}

void PrintNames(ostream& os, const FV2::List& names) 
{
  os << "{" << endl;
  for (int i = 0; i < names.len; i++) {
    os << "  " << i << ". " << *names.name[i] << endl;
  }
  os << "}" << endl;
  return;
} 

Val ContextNames(const Context& c) {
  Context work = c;

  Vals names;
  while (!work.Null()) {
    names.Append1D(NEW_CONSTR(TextVC, (work.Pop()->name)));
  }
  return NEW_CONSTR(ListVC, (names));
}

void PrintDpnd(ostream& os, Val v) 
{
  int index = 0;
  os << "{ ";
  if (v->path) {
    os << index++ << ". <";
    v->path->Print(os);
    os << " : ";
    v->PrintD(os);
    os << ">";
    os << ";";
  }
  if (v->SizeOfDPS() != 0) {
    DepPathTbl::TIter iter(v->dps);
    DepPathTbl::KVPairPtr ptr;    
    if (iter.Next(ptr)) {
      os << endl << "  " << index++ << ". <";
      ptr->key.Print(os);
      os << " : ";
      ptr->val->PrintD(os);
      os << ">";
      while (iter.Next(ptr)) {
	os << "," << endl << "  " << index++ << ". <";
	ptr->key.Print(os);
	os << " : ";
	ptr->val->PrintD(os);
	os << ">";
      }
    }
  }
  os << " }" << endl;
}

void CollectAllDpnd(Val v, bool isModel, /*OUT*/ DPaths& ps) {
  /* Collect all the dependency of v into ps. */

  // collect v->dps:
  if (v->SizeOfDPS() == 0)
    v->dps = NULL;
  else {
    DepPathTbl::TIter iter(v->dps);
    DepPathTbl::KVPairPtr ptr;
    if (isModel) {
      while (iter.Next(ptr)) {
	if (ptr->key.content->path->getlo() == nameDot)
	  (void)ps.Put(ptr);
      }
    }
    else {
      while (iter.Next(ptr))
	(void)ps.Put(ptr);
    }
  }
  DepPath *dp = v->path;
  if (dp != NULL) {
    // collect v->path:
    if (!isModel || (dp->content->path->getlo() == nameDot)) {
      DepPathTbl::KVPairPtr pr;
      (void)ps.Put(*dp, v, pr);
      // assert(v->FingerPrint() == pr->val->FingerPrint());
    }
    else
      v->path = NULL;
  }
  else {
    // collect components of the value (only when no path):
    switch (v->vKind) {
    case BindingVK:
      {
	BindingVC *bv = (BindingVC*)v;
	if (bv->lenDps != NULL && bv->lenDps->Size() != 0) {
	  DepPathTbl::TIter iter(bv->lenDps);
	  DepPathTbl::KVPairPtr ptr;
	  if (isModel) {
	    while (iter.Next(ptr)) {
	      if (ptr->key.content->path->getlo() == nameDot) {
		DepPath lenPath(ptr->key.content->path, BLenPK, ptr->key.content->pathFP);
		Val vNames = ((BindingVC*)ptr->val)->Names();
		(void)ps.Put(lenPath, vNames, ptr);
		// assert(vNames->FingerPrint() == ptr->val->FingerPrint());
	      }
	    }
	  }
	  else {
	    while (iter.Next(ptr)) {
	      DepPath lenPath(ptr->key.content->path, BLenPK, ptr->key.content->pathFP);
	      Val vNames = ((BindingVC*)ptr->val)->Names();
	      (void)ps.Put(lenPath, vNames, ptr);
	      // assert(vNames->FingerPrint() == ptr->val->FingerPrint());
	    }
	  }
	}
	Context work = bv->elems;
	while (!work.Null())
	  CollectAllDpnd(work.Pop()->val, isModel, ps);
	break;
      }
    case ListVK:
      {
	ListVC *lstv = (ListVC*)v;
	if (lstv->lenDps != NULL && lstv->lenDps->Size() != 0) {
	  DepPathTbl::TIter iter(lstv->lenDps);
	  DepPathTbl::KVPairPtr ptr;
	  if (isModel) {
	    while (iter.Next(ptr)) {
	      if (ptr->key.content->path->getlo() == nameDot) {
		DepPath lenPath(ptr->key.content->path, LLenPK, 
				ptr->key.content->pathFP);
		Val vLen = NEW_CONSTR(IntegerVC, 
				      (((ListVC*)ptr->val)->elems.Length()));
		(void)ps.Put(lenPath, vLen, ptr);
		// assert(vLen->FingerPrint() == ptr->val->FingernPrint());
	      }
	    }
	  }
	  else {
	    while (iter.Next(ptr)) {
	      DepPath lenPath(ptr->key.content->path, LLenPK, 
			      ptr->key.content->pathFP);
	      Val vLen = NEW_CONSTR(IntegerVC, 
				    (((ListVC*)ptr->val)->elems.Length()));
	      (void)ps.Put(lenPath, vLen, ptr);
	      // assert(vLen->FingerPrint() == ptr->val->FingerPrint());
	    }
	  }
	}
	Vals vv = lstv->elems;
	while (!vv.Null())
	  CollectAllDpnd(vv.Pop(), isModel, ps);
	break;
      }
    case ClosureVK:
      {
	ClosureVC *cl = (ClosureVC*)v;
	Context work = cl->con;
	while (!work.Null()) {
	  Assoc a = work.Pop();
	  if (cl->func->name != a->name)
	    CollectAllDpnd(a->val, isModel, ps);
	}
	break;
      }
    default:
      break;
    }
  }
  return;
}

void PrintAllDpnd(ostream& os, Val v) 
{
  // Collect dependency:
  DPaths *ps = NEW_CONSTR(DPaths, (v->SizeOfDPS()));
  CollectAllDpnd(v, false, *ps);
  ps->Print(os);
}
  
void PrintDpndSize(ostream& os, Val v) 
{
  switch (v->vKind) {
  case BindingVK:
    {
      Binding b = (BindingVC*)v;
      Context elems = b->elems;
      os << "[";
      os << v->SizeOfDPS();
      os << ", ";
      if (v->path != NULL) os << "*";
      os << "[";
      if (!elems.Null())
	PrintDpndSize(os, elems.Pop()->val);
      while (!elems.Null()) {
	os << ", ";
	PrintDpndSize(os, elems.Pop()->val);
      }
      os << "]]";
      break;
    }
  case ListVK:
    {
      Vals elems = ((ListVC*)v)->elems;
      os << "[";
      os << v->SizeOfDPS();
      os << ", ";
      if (v->path != NULL) os << "*";
      os << "[";
      if (!elems.Null())
	PrintDpndSize(os, elems.Pop());
      while (!elems.Null()) {
	os << ", ";
	PrintDpndSize(os, elems.Pop());
      }
      os << "]]";
      break;
    }
  default:
    os << v->SizeOfDPS();
    break;
  }
  return;
}

#ifndef NO_DUMP_VAL
// Unless you #define NO_DUMP_VAL when compiling this file, an
// additional function will be available:

void DumpVal(Val v);

// The DumpVal function isn't called by any part of the evaluator, but
// can be very useful for developers working on the implementation of
// the evaluator.  It dumps out the complete structure of a value,
// including all dependency information to standard output.  Because
// it is not overloaded and you don't have to pass cout to it, it's
// easy to call it from a debugger.

static Text DumpValIndent(unsigned int nesting)
{
  Text result = "";
  for(unsigned int i = 0; i < nesting; i++)
    result += "    ";
  return result;
}

static const char *DumpValKind(ValueKind k)
{
  switch(k)
    {
    case BooleanVK:
      return "bool";
    case IntegerVK:
      return "int";
    case ListVK:
      return "list";
    case BindingVK:
      return "binding";
    case PrimitiveVK:
      return "prim";
    case  TextVK:
      return "text";
    case ClosureVK:
      return "closure";
    case ModelVK:
      return "model";
    case ErrorVK:
      return "error";
    case FpVK:
      return "fp";
    case UnbndVK:
      return "unbound";
    }
}

static void DumpValInner(Val v, unsigned int nesting = 0)
{
  Text indent = DumpValIndent(nesting);
  cout << indent << "Val @ " << ((void *) v) << endl
       << indent << " type : " << DumpValKind(v->vKind) << endl;
  if(v->path)
    {
      cout << indent << " path @ " << ((void *) v->path) << " : ";
      v->path->Print(cout);
      cout << endl;
    }

  if (v->SizeOfDPS() != 0)
    {
      cout << indent << " dps @ " << ((void *) v->dps) << " :" << endl;
      DepPathTbl::TIter iter(v->dps);
      DepPathTbl::KVPairPtr ptr;
      while(iter.Next(ptr))
	{
	  int index = 0;
	  cout << indent << "  " << index++ << ". <" << flush;
	  ptr->key.Print(cout);
	  cout << flush;
	  cout << " : " << flush;
	  ptr->val->PrintD(cout, false, (nesting*4)+3);
	  cout << flush;
	  cout << ">" << endl;
	}
    }

  switch (v->vKind)
    {
    case BindingVK:
      {
	BindingVC *bv = (BindingVC*)v;
	if(bv->lenDps && bv->lenDps->Size())
	  {
	    cout << indent << " lenDps @ " << ((void *) bv->lenDps) << " :"
		 << endl;
	    DepPathTbl::TIter iter(bv->lenDps);
	    DepPathTbl::KVPairPtr ptr;
	    while(iter.Next(ptr))
	      {
		int index = 0;
		cout << indent << "  " << index++ << ". <";
		ptr->key.Print(cout);
		cout << " : ";
		ptr->val->PrintD(cout, false, (nesting*4)+3);
		cout << ">";
	      }
	  }

	Context work = bv->elems;
	cout << indent << " value : " << endl
	     << indent << "  [" << endl;
	while (!work.Null())
	  {
	    Assoc a = work.Pop();
	    cout << indent << "   " << a->name << " = " << endl;
	    DumpValInner(a->val, nesting + 1);
	  }
	cout << indent << "  ]" << endl;
      }
      break;
    case ListVK:
      {
	ListVC *lv = (ListVC*)v;

	if(lv->lenDps && lv->lenDps->Size())
	  {
	    cout << indent << " lenDps @ " << ((void *) lv->lenDps) << " :"
		 << endl;
	    DepPathTbl::TIter iter(lv->lenDps);
	    DepPathTbl::KVPairPtr ptr;
	    while(iter.Next(ptr))
	      {
		int index = 0;
		cout << indent << "  " << index++ << ". <";
		ptr->key.Print(cout);
		cout << " : ";
		ptr->val->PrintD(cout, false, (nesting*4)+3);
		cout << ">";
	      }
	  }

	Vals elems = lv->elems;
	cout << indent << " value : " << endl
	     << indent << "  <" << endl;
	while (!elems.Null())
	  {
	    DumpValInner(elems.Pop(), nesting + 1);
	  }
      }
      break;
    case ClosureVK:
      {
	ClosureVC *cl = (ClosureVC*)v;

	cout << indent << " definition : "
	     << cl->func->loc->file
	     << ", line " << cl->func->loc->line
	     << ", char " << cl->func->loc->character << endl;

	Context work = cl->con;
	cout << indent << " context : " << endl
	     << indent << "  [" << endl;
	while (!work.Null())
	  {
	    Assoc a = work.Pop();
	    if (a->name != cl->func->name)
	      {
		cout << indent << "   " << a->name << " = " << endl;
		DumpValInner(a->val, nesting + 1);
	      }
	  }
	cout << indent << "  ]" << endl;
      }
      break;
    default:
      {
	cout << indent << " value : " << endl
	     << indent << "  ";
	v->PrintD(cout, false, (nesting*4)+2);
	cout << endl;
      }
      break;
    }
}

void DumpVal(Val v)
{
  DumpValInner(v);
  cout << endl;
}
#endif

class DepCompareInfo
{
private:
  ostream &out;
  const Text &source;
  const FP::Tag &pk;
  CacheEntry::Index cIndex;
  bool printed;
public:
  DepCompareInfo(ostream &o, const Text &s,
		 const FP::Tag &pk, CacheEntry::Index ci)
    : out(o), source(s), pk(pk), cIndex(ci), printed(false)
  {
  }

  void print()
  {
    if(!printed)
      {
	out << endl << "----- Dependency Differences In -----: " << endl 
	    << source << endl
	    << "Cache Hit PK: " << pk << endl
	    << "Cache Index: " << cIndex << endl << endl;
	printed = true;
      }
  }
};

static void printDPSDiffInner(ostream &out,
			      DepCompareInfo &where, Text subpath,
			      DPaths* old_dps, DPaths* new_dps)
{
  DPaths* deleted = 0;
  if(!new_dps)
    deleted = old_dps;
  else if(old_dps)
    deleted = old_dps->Difference(new_dps);
  if(deleted && deleted->Empty())
    deleted = 0;

  DPaths* added = 0;
  if(!old_dps)
    added = new_dps;
  else if(new_dps)
    added = new_dps->Difference(old_dps); 
  if(added && added->Empty())
    added = 0;

  if(deleted || added) {
    where.print();
    out << subpath << " :" << endl;
    if(deleted)
      deleted->Print(out, "-");
    if(added)
      added->Print(out, "+");
    out << endl;
  }
}

static void printDepsDiffInner(ostream &out,
			       DepCompareInfo &where, Text subpath,
			       Val old_v, Val new_v)
{
  // Compare paths
  if(old_v->path || new_v->path)
    {
      // Path missing in old value
      if(!old_v->path)
	{
	  where.print();
	  out << subpath << "->path :" << endl
	       << "  ! No old path" << endl
	       << "  + ";
	  new_v->path->Print(out);
	  out << endl << endl;
	}
      // Path missing in new value
      else if(!new_v->path)
	{
	  where.print();
	  out << subpath << "->path :" << endl
	       << "  - ";
	  old_v->path->Print(out);
	  out << endl
	       << "  ! No new path" << endl << endl;
	}
      // Different paths
      else if(!(*(old_v->path) == *(new_v->path)))
	{
	  where.print();
	  out << subpath << "->path :" << endl
	       << "  - ";
	  old_v->path->Print(out);
	  out << endl
	       << "  + ";
	  new_v->path->Print(out);
	  out << endl << endl;
	}
    }

  // Compare dps
  printDPSDiffInner(out, where, subpath+"->dps",
		    old_v->dps, new_v->dps);

  // Check that they have the same type
  if(old_v->vKind != new_v->vKind)
    {
      where.print();
      out << subpath << "->vKind :" << endl
	   << "  - " << DumpValKind(old_v->vKind) << endl
	   << "  + " << DumpValKind(new_v->vKind) << endl << endl;

      // Can't possibly recurse on subvalues
      return;
    }

  // Recurse as appropriate for composite types
  switch (old_v->vKind)
    {
    case BindingVK:
      {
	BindingVC *old_bv = (BindingVC*)old_v;
	BindingVC *new_bv = (BindingVC*)new_v;

	// Compare lenDps
	printDPSDiffInner(out, where, subpath+"->lenDps",
			  old_bv->lenDps, new_bv->lenDps);

	// Remember binding names added/removed in case the values
	// have different names
	TextSeq name_del, name_add;

	// Loop over the contents of the old value
	Context work = old_bv->elems;
	while (!work.Null())
	  {
	    Assoc a = work.Pop();
	    Val new_sv = new_bv->LookupNoDpnd(a->name);
	    if(new_sv != valUnbnd)
	      {
		printDepsDiffInner(out, where, subpath+"/"+a->name,
				   a->val, new_sv);
	      }
	    else
	      {
		// Name not bound in new value
		name_del.addhi(a->name);
	      }
	  }
	// Check for names bound in new but not in old
	work = new_bv->elems;
	while(!work.Null())
	  {
	    Assoc a = work.Pop();
	    if(old_bv->LookupNoDpnd(a->name) == valUnbnd)
	      {
		// Name not bound in old value
		name_add.addhi(a->name);
	      }
	  }
	// If there are names present in one binding but not the
	// other, print a message about that.
	if((name_add.size() > 0) || (name_del.size() > 0))
	  {
	    where.print();
	    out << subpath << "->{binding names} :" << endl;
	    while(name_del.size() > 0)
	      out << "  - " << name_del.remlo() << endl;
	    while(name_add.size() > 0)
	      out << "  + " << name_add.remlo() << endl;
	  }
      }
      break;
    case ListVK:
      {
	ListVC *old_lv = (ListVC*)old_v;
	ListVC *new_lv = (ListVC*)new_v;

	// Compare lenDps
	printDPSDiffInner(out, where, subpath+"->lenDps",
			  old_lv->lenDps, new_lv->lenDps);

	// Loop over the list elements
	Vals old_elems = old_lv->elems;
	Vals new_elems = new_lv->elems;
	unsigned int i = 0;
	while(!old_elems.Null() && !new_elems.Null())
	  {
	    Val old_sv = old_elems.Pop();
	    Val new_sv = new_elems.Pop();
	    char i_str[11];
	    sprintf(i_str, "%u", i);
	    printDepsDiffInner(out, where, subpath+"["+Text(i_str)+"]",
			       old_sv, new_sv);
	    i++;
	  }
	// Check for extra elements in one of the lists
	if(!old_elems.Null() || !new_elems.Null())
	  {
	    where.print();
	    out << subpath << "->{list length} :" << endl
		 << "  - " << old_lv->elems.Length() << endl
		 << "  + " << new_lv->elems.Length()  << endl;
	  }
      }
      break;
    case ClosureVK:
      {
	ClosureVC *old_cl = (ClosureVC*)old_v;
	ClosureVC *new_cl = (ClosureVC*)new_v;

	// Remember context names added/removed in case the values
	// have different names
	TextSeq name_del, name_add;

	// Loop over the context of the first function
	Context work = old_cl->con;
	while(!work.Null())
	  {
	    Assoc a = work.Pop();
	    if(a->name == old_cl->func->name)
	      // Avoid infinite recursion.  (A functions own name
	      // referes to itself in its definition context.)
	      continue;

	    Val new_sv = LookupInContext(a->name, new_cl->con);
	    if(new_sv != valUnbnd)
	      {
		printDepsDiffInner(out, where, subpath+"/"+a->name,
				   a->val, new_sv);
	      }
	    else
	      {
		// Name not bound in new context
		name_del.addhi(a->name);
	      }
	  }
	// Check for names bound in new context but not in old
	work = new_cl->con;
	while(!work.Null())
	  {
	    Assoc a = work.Pop();
	    if(LookupInContext(a->name, old_cl->con) == valUnbnd)
	      {
		// Name not bound in old value
		name_add.addhi(a->name);
	      }
	  }
	// If there are names present in one context but not the
	// other, print a message about that.
	if((name_add.size() > 0) || (name_del.size() > 0))
	  {
	    where.print();
	    out << subpath << "->{context names} :" << endl;
	    while(name_del.size() > 0)
	      out << "  - " << name_del.remlo() << endl;
	    while(name_add.size() > 0)
	      out << "  + " << name_add.remlo() << endl;
	  }
      }
      break;
    }
}

void PrintDepsDiffs(const Text &source, const FP::Tag &pk, CacheEntry::Index cIndex, 
		    Val old_v, Val new_v, const Text &name)
{
  ostream &out = cio().start_out();

  DepCompareInfo where(out, source, pk, cIndex);
  printDepsDiffInner(out, where, name, old_v, new_v);

  cio().end_out();
}


