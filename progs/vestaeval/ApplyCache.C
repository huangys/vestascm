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

// File: ApplyCache.C

#include <Basics.H>
#include <FP.H>
#include <Model.H>
#include <FV.H>
#include <CacheIndex.H>
#include <CacheC.H>
#include "ApplyCache.H"
#include "Expr.H"
#include "Val.H"
#include "Pickle.H"
#include "Err.H"
#include "PrimRunTool.H"
#include "Debug.H"
#include "ThreadData.H"
#include <algorithm>
#include "Timing.H"
#include <WaitDuplicateTable.H>
#include "EvalBasics.H"

using std::ostream;
using std::endl;
using Basics::OBufStream;
using OS::cio;

// KCS, 2001-12-6: Changed both evaluatorRunToolVersion and
// evaluatorFunctionVersion, as integer values are now fingerprinted
// as 32-bit values.

// KCS, 2002-8-20: Changed evaluatorFunctionVersion due to a fix to
// dependency analysis in several primitive functions for boundary
// cases with empty lists and bindings.

// KCS, 2002-11-6: Changed evaluatorFunctionVersion due to a bug fix
// to dependency collection code which iadvertently munged dependency
// paths.  This bug *seemed* to only add additional bogus secondary
// dependencies, but it's safer to update evaluatorFunctionVersion.

// KCS, 2003-2-11: Changed both evaluatorRunToolVersion and
// evaluatorFunctionVersion, as the pickle version has changed for
// the network byte order conversion.

// KCS, 2004-04-05: Changed evaluatorFunctionVersion due to
// dependency analysis bug fixes in CollectModel.

// STV&KCS, 2004-12-25: Changed evaluatorRunToolVersion after adding
// "dumped_core" to _run_tool result.

// KCS, 2005-02-11: Changed both evaluatorRunToolVersion and
// evaluatorFunctionVersion due to dependency analysis bug fix in
// _run_tool (not recording a dependency on directories looked up but
// never accessed).

// KCS, 2006-08-06: Changed both evaluatorRunToolVersion and
// evaluatorFunctionVersion due to dependency analysis bug fix in
// _run_tool (not including ./envVars in the recorded dependencies of
// the result).

// IF, 2006-08-16: Changed evaluatorRunToolVersion and 
// evaluatorFunctionVersion, as pickle/unpickleInt16 was replaced by 
// pickle/unpickleInt32 in most cases.

// KCS, 2006-10-26: Changed evaluatorFunctionVersion when fixing a
// dependency analysis bug in _map/_par_map which recorded
// insufficient dependencies when a call mapping over a binding
// returns an empty binding.

// KCS, 2007-04-22: Changed evaluatorFunctionVersion when making the
// _findre primitive function official.  (The parsed represenation of
// a fucntion refernceing a new primitive is unusable by older
// evaluators.)

// KCS, 2007-05-01: Reverted evaluatorFunctionVersion since we've
// backed out _findre for now.

// KCS, 2008-11-03: Changed evaluatorFunctionVersion when fixing the
// dependency bug caused by dpsVersion/currentDpsVersion.

// KCS, 2009-02-11: Changed evaluatorFunctionVersion since we've
// re-enabled _findre.

// Get the pickle version
extern int currentPickleVersion;
static const Basics::int32 currentPickleVersion_net = Basics::hton32(currentPickleVersion);

// Runtool call version:
const char *evaluatorRunToolVersion = "Wed Aug 16 13:07:11 EDT 2006";

// Function call version:
const char *evaluatorFunctionVersion = "Wed Feb 11 19:31:09 2009";

// This class hides, initializes, and optionally salts the base tags
class EvaluatorVersionTags
{
private:
  bool salted;
  FP::Tag function_tag;
  FP::Tag runTool_tag;
public:
  EvaluatorVersionTags()
    : salted(false),
      function_tag(evaluatorFunctionVersion),
      runTool_tag(evaluatorRunToolVersion)
  {
  }

  void salt() throw()
  {
    if(!salted)
      {
	salted = true;
	if(pkSalt != NULL)
	  {
	    function_tag.Extend(pkSalt);
	    runTool_tag.Extend(pkSalt);
	  }
      }
  }

  const FP::Tag &function() const throw()
  {
    return function_tag;
  }
  const FP::Tag &runTool() const throw()
  {
    return runTool_tag;
  }
};
static EvaluatorVersionTags evaluatorVersionTags;

// The number of addentry calls to the cache:
static int addEntryCount = 0;

// The mutex protects the variables lastRenewed and
// renewLease_failure.
static Basics::mutex leaseMu;
static time_t lastRenewed;
static bool renewLease_failure = false;

// Key type used for waiting for duplicate work after a cache miss
struct PKKey
{
  FP::Tag pk;
  const char *kind;

  PKKey(const FP::Tag &pk, const char *kind)
    : pk(pk), kind(kind)
  { }

  Word Hash() const throw()
  {
    return pk.Hash() ^ kind[0];
  }

  bool operator==(const PKKey &other) const throw()
  {
    return (pk == other.pk) && (strcmp(kind, other.kind) == 0);
  }
};

// A table to keep from running identical functions/tools in parallel.
// This checks for a PK and waits on a condition if that PK
// is already in progress.
class WaitPKTable : public Generics::WaitDuplicateTable<PKKey, Text>
{
protected:

  virtual void BeforeWaiting(const PKKey &key, const Text &info)
  {
    ostream &err = cio().start_err();
    err << ThreadLabel() << "Waiting on possibly identical "
        << key.kind;
    if(!info.Empty()) {
      err << " (" << info << ")";
    }
    err << endl;
    cio().end_err();
  }

  virtual void AfterWaiting(const PKKey &key, const Text &info)
  {
  }
};

static WaitPKTable runningPkTable;

//Checks the runningPkTable and sleeps if another thread is currently
//evaluating a function/tool with the same pk.
//Returns true if another thread might have populated the cache
//for us, false if we need to do the work ourself.
bool WaitForDuplicateEval(FP::Tag pk, const char *thing, Text &info) {
  PKKey k(pk, thing);
  return runningPkTable.WaitForDuplicate(k, info);
}
// after a cache entry is added, wake up any remaining threads that
// might be waiting for it.
void WakeWaitingEvals(FP::Tag pk, const char *thing) {
  PKKey k(pk, thing);
  runningPkTable.WakeWaiting(k);
}

// Function for doing an eval of a model or function and recording timing
Val time_eval(ExprC *b, const Context& c, Text loc, const Context& args = conEmpty) {
  Timers::IntervalRecorder *eval_timer = 0;
  Val ret;
  bool time_me = (TimingRecorder::level >= TimingRecorder::PrintFuncTimings)
	  && TimingRecorder::name_match(loc);
  if (time_me) {
	  eval_timer = NEW_PTRFREE(Timers::IntervalRecorder);
  }
  //Call the real Eval function
  ret = b->Eval(c);
  if (time_me) {
    assert(eval_timer != 0);
    double secs = eval_timer->get_delta();
    ostream& out = TimingRecorder::start_out();
    out << ThreadLabel() << "Function " << loc;
    if (args != conEmpty) {
      out << " with args";
      for(int i = 0; i < args.Length() ; i++) {
	out << " ";
	args.Nth(i)->PrintD(out, false, 0,
			TimingRecorder::report_argument_depth, false);
      }
    }
    out << " finished after " << secs << " seconds." << endl;
    TimingRecorder::end_out();
    delete eval_timer;
  }
  return ret;
}

void Tags(const CompactFV::List& names, const Context& c, 
	  /*OUT*/ FP::List& tags) {
  // Used for cache lookup:
  Basics::uint32 len = names.num;
  if (len > 0) {
    tags.len = len;
    tags.fp = NEW_PTRFREE_ARRAY(FP::Tag, len);
    Basics::uint32 size = names.tbl.NumArcs();
    Val *vals = NEW_ARRAY(Val, size);
    int i;
    for (i = 0; i < size; i++) { vals[i] = NULL; }
    for (i = 0; i < len; i++)	{
      Basics::uint32 idx = names.GetIndex(i);
      PathKind pkind = (PathKind)names.types[i];
      tags.fp[i] = LookupPath(idx, pkind, names.tbl, c, vals)->FingerPrint();
    }
  } else {
    tags.len = 0;
    tags.fp = NULL;
  }
  return;
}

bool NamesTagsPickle(Val val, bool cutoff, const Context& inPKArgs,
		     /*OUT*/ char*& types, /*OUT*/ FV2::List& names,
		     /*OUT*/ FP::List& tags, /*OUT*/ VestaVal::T& vval,
		     DPaths *fv_exclude = 0) {
  /* Create all the stuff needed for an AddEntry call. */

  // We now create the secondary key of the entry. Since some dependencies
  // on the function arguments have already been encoded in the primary key,
  // they are redundant, and therefore removed.
  DPaths *sk = NEW_CONSTR(DPaths, (val->dps));
  ValueDpnd(val, sk);
  DepPathTbl::TIter iter(sk);
  DepPathTbl::KVPairPtr ptr;
  Text id;
  if(!inPKArgs.Null() || (fv_exclude != 0))
    {
      while (iter.Next(ptr)) {
	bool deleted = false;
	Context work = inPKArgs;
	while (!work.Null() && !deleted) {
	  id = work.Pop()->name;
	  if (ptr->key.content->path->getlo() == id)
	    {
	      sk->Delete(ptr->key, ptr, false);
	      deleted = true;
	    }
	}
	// If we have a set of paths to exclude from the secondary key
	if((fv_exclude != 0) && !deleted)
	  {
	    // Ugh this is O(n*m) because we have to compare each entry of
	    // sk with each entry of fv_exclude
	    DepPathTbl::TIter exclude_iter(fv_exclude);
	    DepPathTbl::KVPairPtr exclude_ptr;
	    while(exclude_iter.Next(exclude_ptr) && !deleted)
	      {
		// Determine whether the path from fv_exclude is a
		// prefix of the path in this entry of sk.

		// If the entry from sk has a shorter path, it's
		// obviously not a prefix.
		bool match = (ptr->key.content->path->size() >= 
			      exclude_ptr->key.content->path->size());

		// Compare path elements until we get a mismatch or run
		// out.
		for(unsigned int i = 0;
		    match && (i < exclude_ptr->key.content->path->size());
		    i++)
		  {
		    match = (ptr->key.content->path->get(i) ==
			     exclude_ptr->key.content->path->get(i));
		  }

		// Yes it's a prefix.  Exclude this secondary
		// dependency.
		if(match)
		  {
		    sk->Delete(ptr->key, ptr, false);
		    deleted = true;
		  }
	      }
	  }
      }
    }
  sk->Resize();
  // Set names and tags:
  int len = sk->Size();
  names.len = len;
  tags.len = len;
  if (len > 0) {
    types = NEW_PTRFREE_ARRAY(char, len);    
    names.name = NEW_ARRAY(FV2::TPtr, len);
    tags.fp = NEW_PTRFREE_ARRAY(FP::Tag, len);
    iter.Reset();
    int index = 0;
    while (iter.Next(ptr)) {
      types[index] = (char)ptr->key.content->pKind;
      names.name[index] = ptr->key.content->path;
      if (ptr->val->vKind != FpVK && ptr->key.content->pKind == ExprPK) {
	if (ptr->val->vKind == ClosureVK)
	  tags.fp[index++] = ((ClosureVC*)ptr->val)->FingerPrintExpr();
	else {
	  assert(ptr->val->vKind == ModelVK);
	  tags.fp[index++] = ((ModelVC*)ptr->val)->FingerPrintFile();
	}
      }
      else
	tags.fp[index++] = ptr->val->FingerPrint();
    }
  }
  // Pickle the value:
  if (cutoff) ModelCutOff(val);
  return Pickle(val, vval);
}

static void CheckLeases() {
  leaseMu.lock();    
  bool expired = (time(NULL) - lastRenewed) > leaseDuration;
  bool failed = renewLease_failure;
  leaseMu.unlock();
  if (expired) {
    throw(Evaluator::failure("Leases timed out. Start over.", true));
  } else if(failed) {
    throw(Evaluator::failure("Lease renewal failed.", true));
  }
}

void CollectKids(CacheEntry::IndicesApp* orphanCIs, int kidsIndex,
		 CacheEntry::Indices& kids) {
  CheckLeases();
  kids.len = orphanCIs->len - kidsIndex;
  kids.index = orphanCIs->index + kidsIndex;
}
    
void UpdateOrphans(CacheEntry::IndicesApp* orphanCIs, int kidsIndex,
		   CacheEntry::Index ci) {
  frontierMu.lock();
  assert(kidsIndex <= orphanCIs->len);
  orphanCIs->len = kidsIndex;
  orphanCIs->Append(ci);
  frontierMu.unlock();  
}

void Checkpoint() {
  CheckLeases();
  frontierMu.lock();	    
  addEntryCount++;
  if ((addEntryCount & 0x3f) == 0) {
    CacheEntry::IndicesApp orphanCIs;
    ThreadData::mu.lock();
    ThreadData* cur = ThreadData::head;
    while (cur) {
      assert(cur->orphanCIs != 0);
      for (int i = 0; i < cur->orphanCIs->len; i++) {
	orphanCIs.Append(cur->orphanCIs->index[i]);
      }
      cur = cur->next;
    }
    ThreadData::mu.unlock();
    theCache->Checkpoint(topModelRoot->fptag, topModelSid, orphanCIs, false);
  }
  frontierMu.unlock();
}

void FreeNamesTags(CompactFV::List& names, FP::List& tags) {
  // Free names and tags after cache lookup:
  names.tbl = PrefixTbl();
  tags.len = 0;
  tags.fp = NULL;
}

// General function application:
Context BindApplicationArgs(ClosureVC *clos, ArgList actuals, const Context& c) {
  Context result, closCon = clos->con;
  Exprs forms = clos->func->args->elems;
  Exprs acts = actuals->elems;
  int fSize = forms.size();
  int aSize = acts.size();
  Expr fe;
  AssignEC *formal;
  Text name;
  int i;

  if (aSize > fSize + 1) {
    actuals->EError(cio().start_err(), "Too many actual parameters:");
    cio().end_err();
    return conEmpty;
  }
  for (i = 0; i < aSize; i++) { // Actuals to formals
    if (i == fSize)
      name = nameDot;
    else {
      fe = forms.get(i);
      if (fe->kind == NameEK)
	name = ((Name)fe)->id;
      else if (fe->kind == AssignEK)
	name = ((AssignEC*)fe)->lhs->id;
      else {
	ostream& err_stream = cio().start_err();
	actuals->EError(err_stream, 
			"Bad parameter list. It is neither actual nor default: `");
	ErrorExpr(err_stream, fe);
	ErrorDetail(err_stream, "'.");
	cio().end_err();
	return conEmpty;
      }
    }
    PushToContext(name, acts.get(i), c, result);
  }
  for (i = aSize; i < fSize; i++) { // Default rest of formals
    fe = forms.get(i);
    if (fe->kind == AssignEK) {
      formal = (AssignEC*)fe;
      PushToContext(formal->lhs->id, formal->rhs, closCon, result);
    }
    else {
      ostream& err_stream = cio().start_err();
      actuals->EError(err_stream, "Bad parameter list. Must have default: `");
      ErrorExpr(err_stream, fe);
      ErrorDetail(err_stream, "'.");
      cio().end_err();
      return conEmpty;
    }
  }
  if (aSize <= fSize) {
    Name eDot = NEW_CONSTR(NameEC, (nameDot, actuals->loc));
    PushToContext(nameDot, eDot, c, result);
  }
  return result;
}

// Determine whether we should compare dependencies for this CI.  (We
// only do it once per CI in a single invocation.)  Only used when
// dependencyCheck is true.
static bool shouldDependencyCheck(CacheEntry::Index cIndex)
{
  static Basics::mutex mu;
  static BitVector *doneCIs = 0;

  mu.lock();
  if(!doneCIs)
    doneCIs = NEW(BitVector);
  bool not_yet = !doneCIs->Set(cIndex);
  mu.unlock();
  return not_yet;
}

Val ApplicationFromCache(ClosureVC* clos, const Context& argsCon, SrcLoc *loc) {
  FP::Tag pk(evaluatorVersionTags.function());
  pk.Extend((char*)(&currentPickleVersion_net), sizeof_assert(currentPickleVersion_net, 4));
  CompactFV::List fvNames;
  FV2::List entryNames;
  FP::List tags;
  CacheEntry::Index cIndex;
  VestaVal::T cVal;
  CacheIntf::LookupRes cResult = CacheIntf::Miss;
  Model::T model = clos->func->body->loc->shortId;
  Val result = 0;

  Val old_result = 0;

  IncCounter(&appCallCounter);
  Context evalCon = argsCon.Append(clos->con);

  Text pksource = Text::printf("%s/%s(), line %d, col %d",
			       clos->func->loc->file.cchars(),
			       clos->func->name.cchars(),
			       clos->func->loc->line,
			       clos->func->loc->character);
  ostream *traceRes = ThreadDataGet()->traceRes;
  // Evaluate in the nocaching cases:
  if (cacheOption != 3 || clos->func->noCache) {
    // Normal function caching is disabled.
    if (traceRes) *traceRes << ": disabled\n";
    result = time_eval(clos->func->body, evalCon, pksource, argsCon);
    if (cacheOption < 2) return result;
    return FuncDpnd(result, clos, argsCon);
  }

  // Compute the primary key:
  // The pk consists of the fingerprints of the closure body, the argumnets
  // that either is specified by pk pragma or evaluates to simple values.
  pk.Extend(clos->FingerPrintExpr());
  Context work = argsCon;
  (void)work.Pop();   // Do not put the dot into pk.
  int last = clos->func->args->elems.size();
  Bit64 inPKs = clos->func->args->inPKs;
  Context inPKArgs;
  while (!work.Null()) {
    Assoc elem = work.Pop();
    if (inPKs & ((Bit64)1 << --last)) {
      // Add this argument into pk.
      pk.Extend(elem->FingerPrint());
      inPKArgs.Push(elem);
    }
    else {
      switch (elem->val->vKind) {
      case BooleanVK: case IntegerVK: case PrimitiveVK:
      case TextVK: case ErrorVK:
	pk.Extend(elem->FingerPrint());
	inPKArgs.Push(elem);
	break;
      case ClosureVK:
	{
	  FP::Tag tag(elem->name);
	  tag.Extend(((ClosureVC*)elem->val)->FingerPrintExpr());
	  pk.Extend(tag);
	  break;
	}
      case ModelVK:
	{
	  FP::Tag tag(elem->name);
	  tag.Extend(((ModelVC*)elem->val)->FingerPrintFile());
	  pk.Extend(tag);
	  break;
	}
      case FpVK:
	assert(false);
      default:
	break;
      }
    }
  }
  // Evaluate with caching:
  ThreadData *thdata = ThreadDataGet();
  CacheEntry::IndicesApp* orphanCIs = thdata->orphanCIs;  
  try {
    bool waited = false;
    Text dup_info;
    while (true) {
      bool noEntry;
      FV::Epoch epoch = theCache->FreeVariables(pk, /*OUT*/ fvNames, /*OUT*/ noEntry);
      if (noEntry)
	cResult = CacheIntf::Miss;
      else {
	Tags(fvNames, evalCon, /*OUT*/ tags);
	cResult = theCache->Lookup(pk, epoch, tags, /*OUT*/ cIndex, /*OUT*/ cVal);
      }
      FreeNamesTags(fvNames, tags);
      if (cResult == CacheIntf::Hit) {
	if (Unpickle(cVal, evalCon, result)) {
	  assert(result != 0);
	  // If we're supposed to compare dependencies, remember the
	  // cached value and pretend this was a miss.
	  if(dependencyCheck && shouldDependencyCheck(cIndex))
	    {
	      old_result = result;
	      cResult = CacheIntf::Miss;
	    }
	  else
	    {
	      if (traceRes) {
		*traceRes << ": hit (ci=" << cIndex << ")\n";
	      }
	      IncCounter(&appHitCounter);
	      UpdateOrphans(orphanCIs, orphanCIs->len, cIndex);
	      return FuncDpnd(result, clos, argsCon);
	    }
	}
	else {
	  OBufStream l_msg;
	  l_msg << "ApplicationFromCache: Cache hit but bad cache data."
		<< endl << endl
		<< "\tpk = " << pk << endl
		<< "\tci = " << cIndex << endl << endl
		<< "Evaluate as cache miss." << endl;
	  Error(cio().start_err(), Text(l_msg.str()), loc);
	  cio().end_err();
	  cResult = CacheIntf::Miss;
	}
      }
      if (cResult == CacheIntf::Miss) {
	if(waited) {
	    cio().start_err() << ThreadLabel() << "Function missed after waiting ("
			      << dup_info << ")." << endl;
	    cio().end_err();
	}
	if (traceRes) *traceRes << ": miss\n";
	int kidsIndex = orphanCIs->len;

	// Do the duplicate run supression
	dup_info = verboseDupMsgs ? pksource : (Text) clos->func->name;
	if(clos->func->noDup && WaitForDuplicateEval(pk, "function", dup_info)) {
	  // a duplicate was running.  try the cache lookup again
	  waited = true;
	  continue;
	}
	try {
	  result = time_eval(clos->func->body, evalCon, pksource, argsCon);
	  if (!noAddEntry) {
	    // Create a new cache entry:
	    char *types;
	    if (NamesTagsPickle(result, false, inPKArgs, /*OUT*/ types, 
			        /*OUT*/ entryNames, /*OUT*/ tags, /*OUT*/ cVal)) {
	      if(dependencyCheck) {
	        // If we got a cache hit earlier that we can compare
	        // against (we test it this way, because we change
	        // cResult in when dependencyCheck is set)...
	        if(old_result)
		  {
		    Val new_val = NULL;
		    if(Unpickle(cVal, evalCon, new_val)) {
		      assert(new_val != 0);
		      PrintDepsDiffs(pksource, pk, cIndex, old_result, new_val);
		      PrintDepsDiffs(pksource, pk, cIndex, result, new_val, "pickle");
		      if (traceRes) {
		        *traceRes << "  " << thdata->funcCallDepth << ". "
				  << clos->func->args->loc->file << ": "
				  << clos->func->name
				  << "(): dependencies comapred (with ci=" 
				  << cIndex << ")\n";
		      }
		    }
		    else
		      {
		        ostream &err = cio().start_err();
		        Error(err, "(Impl) Trying to compare dependencies, failed to unpickle new value");
		        InternalError(err, cio().helper(true), "ApplicationFromCache");
		        // Unreached
		        cio().end_err();
		      }
		  }
	      }
	      else {
	        CacheEntry::Indices kids;
	        CollectKids(orphanCIs, kidsIndex, kids);
	        // Add the entry, capturing the result of the call.
	        CacheIntf::AddEntryRes ae_res =
		  theCache->AddEntry(pk, types, entryNames, tags, cVal, model,
				     kids, pksource, /*OUT*/ cIndex);
	        // If the AddEntry fails, die with an error.
	        if(ae_res != CacheIntf::EntryAdded)
		  {
		    Error(cio().start_err(), Text("AddEntry failed: ")+
			  CacheIntf::AddEntryResName(ae_res)+"\n"
			  "This may be an evaluator bug.  "
			  "Please report it.\n",
			  loc);
		    cio().end_err();
		    if (traceRes) *traceRes << endl;
		    throw(Evaluator::failure(Text("AddEntry failed (may be an "
						  "evaluator bug)"),
					     false));
		  }
	        UpdateOrphans(orphanCIs, kidsIndex, cIndex);
	        Checkpoint();
	        if (traceRes) {
		  *traceRes << "  " << thdata->funcCallDepth << ". "
			    << clos->func->args->loc->file << ": "
			    << clos->func->name << "(): add (ci="
			    << cIndex << ")\n";
	        }
	      }
	    }
	  }
	  if(clos->func->noDup) {
	    WakeWaitingEvals(pk, "function");
	  }
	} catch (...) {
	  if(clos->func->noDup) {
	    WakeWaitingEvals(pk, "function");
	  }
	  throw;
	}
	return FuncDpnd(result, clos, argsCon);
      }
      if (cResult != CacheIntf::FVMismatch) {
	if (traceRes) *traceRes << ": mismatch\n";
	OBufStream err_msg;
	err_msg << "ApplicationFromCache got bad result: "
		<< CacheIntf::LookupResName(cResult) << endl
		<< "\tpk = " << pk << endl
		<< "\tFV epoch = " << epoch << endl
		<< "\ttags.len = " << tags.len << endl
		<< "This may be an evaluator bug.  Please report it." << endl;
        Error(cio().start_err(), Text(err_msg.str()), loc);
	cio().end_err();
	result = NEW(ErrorVC);
	return result;
      }
    }
  } catch (PrefixTbl::Overflow) {
    ostream& err_stream = cio().start_err(); 
    Error(err_stream, 
	  Text("Internal limits exceeded: PrefixTbl overflow\n"), loc);
    err_stream << "Caching function \"" << clos->func->name
	       << "\" defined at:" << endl << endl
	       << "  " << clos->func->loc->file
	       << ", line " << clos->func->loc->line
	       << ", char " << clos->func->loc->character
	       << endl << endl
	       << Text("This usually means that there are too many free variables "
		       "in the secondary key of the function being cached.  This "
		       "can often be alleviated by marking more arguments with "
		       "the /**pk**/ pragma.  (Fixing this will also reduce the CPU "
		       "load of your evaluator and cache server, and improve "
		       "build performance.)").WordWrap()
	       << endl << endl;
    cio().end_err(/*aborting*/true);
    // This is a fatal error.  Exit and dump core.
    abort();
  } 
  catch (SRPC::failure f) {
    Error(cio().start_err(), Text("ApplicationFromCache: SRPC failure (")
	  + IntToText(f.r) + "): " + f.msg + ".\n", loc);
    cio().end_err();
    if (traceRes) *traceRes << endl;
    throw(Evaluator::failure(Text("Cache server is possibly down"), false));
  }
  //return result; // not reached
}

Val ApplyFunction(ClosureVC* clos, ApplyEC *ae, const Context& c) {
  ArgList args = ae->args;
  Context argsCon;

  argsCon = BindApplicationArgs(clos, args, c);

  ThreadData *thdata = ThreadDataGet();
  ostream *traceRes = thdata->traceRes;
  thdata->funcCallDepth++;
  if (traceRes) {
    *traceRes << "  " << thdata->funcCallDepth << ". "
      << clos->func->args->loc->file << ": "
      << clos->func->name << "()";
  }
  if (argsCon == conEmpty) {
    // when argsCon is correct, it can not be empty since `dot' must be
    // defined in argsCon.
    ostream& err_stream = cio().start_err();
    ErrorDetail(err_stream, "\n  Formal list is ");
    ErrorExpr(err_stream, clos->func->args);
    ErrorDetail(err_stream, "\n  Actual list is ");
    ErrorExpr(err_stream, args);
    ErrorDetail(err_stream, "\n");
    cio().end_err();
    if (traceRes) *traceRes << ": badargs\n";
    return NEW(ErrorVC);
  }

  // Evaluate:
  Val result = ApplicationFromCache(clos, argsCon, ae->loc);
  thdata->funcCallDepth--;
  return result;
}
  
// Model application:
Val NormalModelFromCache(ModelVC *fun, const Context& evalCon, SrcLoc *loc) {
  Model::T model = fun->content->sid;
  FP::Tag pk(evaluatorVersionTags.function());
  pk.Extend((char*)(&currentPickleVersion_net), sizeof_assert(currentPickleVersion_net, 4));
  CompactFV::List fvNames;
  FV2::List entryNames;
  FP::List tags;
  CacheEntry::Index cIndex;
  VestaVal::T cVal;
  CacheIntf::LookupRes cResult = CacheIntf::Miss;
  Val result = 0;

  Val old_result = 0;

  ThreadData *thdata = ThreadDataGet();
  ostream *traceRes = thdata->traceRes;
  
  Text pksource(fun->content->name);
  pksource += "() (normal)";
  // Evaluate as nocache if the model imports a local model:
  if (((ModelEC*)fun->content->model)->ImportLocalModel()) {
    result = time_eval(((ModelEC*)fun->content->model)->block, evalCon, pksource);
    ModelCutOff(result);
    return result;
  }
  
  IncCounter(&nModelCallCounter);
  // Record this call if traced:
  if (traceRes) {
    *traceRes << "  " << thdata->funcCallDepth << ". " << fun->content->name;
  }

  // Compute the primary key:
  pk.Extend(fun->FingerPrintFile());

  // Evaluate:
  CacheEntry::IndicesApp* orphanCIs = ThreadDataGet()->orphanCIs;  
  try {
    while (true) {
      bool noEntry;
      FV::Epoch epoch = theCache->FreeVariables(pk, /*OUT*/ fvNames, /*OUT*/ noEntry);
      if (noEntry)
	cResult = CacheIntf::Miss;
      else {
	Tags(fvNames, evalCon, /*OUT*/ tags);
	cResult = theCache->Lookup(pk, epoch, tags, /*OUT*/ cIndex, /*OUT*/ cVal);
      }
      FreeNamesTags(fvNames, tags);
      if (cResult == CacheIntf::Hit) {
	if (Unpickle(cVal, evalCon, result)) {
	  assert(result != 0);
	  // If we're supposed to compare dependencies, remember the
	  // cached value and pretend this was a miss.
	  if(dependencyCheck && shouldDependencyCheck(cIndex))
	    {
	      old_result = result;
	      cResult = CacheIntf::Miss;
	    }
	  else
	    {
	      if (traceRes) {
		*traceRes << ": hit (ci=" << cIndex << ")\n";
	      }
	      IncCounter(&nModelHitCounter);
	      UpdateOrphans(orphanCIs, orphanCIs->len, cIndex);
	      return result;
	    }
	}
	else {
	  OBufStream l_msg;
	  l_msg << "NormalModelFromCache: Cache hit but bad cache data."
		<< endl << endl
		<< "\tpk = " << pk << endl
		<< "\tci = " << cIndex << endl << endl
		<< "Evaluate as cache miss." << endl; 
	  Error(cio().start_err(), Text(l_msg.str()), loc); 	
	  cio().end_err();
	  throw(Evaluator::failure(Text("Bad cache data"), false));
	}
      }
      if (cResult == CacheIntf::Miss) {
	if (traceRes) *traceRes << ": miss\n";
	int kidsIndex = orphanCIs->len;
	result = time_eval(((ModelEC*)fun->content->model)->block,
			evalCon, pksource);
	if (!noAddEntry) {
	  // Create the normal-case model entry:
	  char *types;
	  if (NamesTagsPickle(result, true, conEmpty, /*OUT*/ types, 
			      /*OUT*/ entryNames, /*OUT*/ tags, /*OUT*/ cVal)) {
	    if(dependencyCheck) {
	      // If we got a cache hit earlier that we can compare
	      // against (we test it this way, because we change
	      // cResult in when dependencyCheck is set)...
	      if(old_result)
		{
		  Val new_val = NULL;
		  if(Unpickle(cVal, evalCon, new_val)) {
		    assert(new_val != 0);
		    PrintDepsDiffs(pksource, pk, cIndex, old_result, new_val);
		    PrintDepsDiffs(pksource, pk, cIndex, result, new_val, "pickle");
		    if (traceRes) {
		      *traceRes << "  " << thdata->funcCallDepth << ". " 
				<< fun->content->name
				<< ": dependencies comapred (with ci=" 
				<< cIndex << ")\n";
		    }
		  }
		  else
		    {
		      ostream &err = cio().start_err();
		      Error(err, "(Impl) Trying to compare dependencies, failed to unpickle new value");
		      InternalError(err, cio().helper(true), "NormalModelFromCache");
		      // Unreached
		      cio().end_err();
		    }
		}
	    }
	    else {
	      CacheEntry::Indices kids;
	      CollectKids(orphanCIs, kidsIndex, kids);
	      // Add the entry, capturing the result of the call.
	      CacheIntf::AddEntryRes ae_res =
		theCache->AddEntry(pk, types, entryNames, tags, cVal, model,
				   kids, pksource, /*OUT*/ cIndex);
	      // If the AddEntry fails, die with an error.
	      if(ae_res != CacheIntf::EntryAdded)
		{
		  Error(cio().start_err(), Text("AddEntry failed: ")+
			CacheIntf::AddEntryResName(ae_res)+"\n"
			"This may be an evaluator bug.  "
			"Please report it.\n", loc);
		  cio().end_err();
		  if (traceRes) *traceRes << endl;
		  throw(Evaluator::failure(Text("AddEntry failed (may be an "
						"evaluator bug)"),
					   false));
		}
	      UpdateOrphans(orphanCIs, kidsIndex, cIndex);
	      Checkpoint();
	      if (traceRes) {
		*traceRes << "  " << thdata->funcCallDepth << ". "
			  << fun->content->name
			  << ": add (ci=" << cIndex << ")\n";
	      }
	    }
	  }
	}
	return result;
      }
      if (cResult != CacheIntf::FVMismatch) {
	if (traceRes) *traceRes << ": mismatch\n";
	OBufStream err_msg;
	err_msg << "NormalModelFromCache got bad result: "
		<< CacheIntf::LookupResName(cResult) << endl
		<< "\tpk = " << pk << endl
		<< "\tFV epoch = " << epoch << endl
		<< "\ttags.len = " << tags.len << endl
		<< "This may be an evaluator bug.  Please report it." << endl;
        Error(cio().start_err(), Text(err_msg.str()), loc);
	cio().end_err();
	result = NEW(ErrorVC);
	return result;
      }
    }
  } catch (PrefixTbl::Overflow) {
    ostream& err_stream = cio().start_err();
    Error(err_stream, 
	  Text("Internal limits exceeded: PrefixTbl overflow\n"), loc);
    err_stream << "Caching model (normal):" << endl << endl
	       << "  " << fun->content->name
	       << endl << endl
	       << Text("This usually means that there are too many free variables "
		       "in the secondary key of the function being cached.  For "
		       "models, this may mean too much work is being done in a "
		       "single model file.  Try splitting up the work of this "
		       "model file into several separate model files each doing "
		       "a smaller amount of work.  (Fixing this will also reduce "
		       "the CPU load of your evaluator and cache server, and "
		       "improve build performance.)").WordWrap()
	       << endl << endl;
    cio().end_err(/*aborting*/true);
    // This is a fatal error.  Exit and dump core.
    abort();
  }
  catch (SRPC::failure f) {
    fun->VError(cio().start_err(), Text("NormalModelFromCache: SRPC failure (")
		+ IntToText(f.r) + "): " + f.msg + ".\n");
    cio().end_err();
    if (traceRes) *traceRes << endl;
    throw(Evaluator::failure(Text("Cache server is possibly down"), false));
  }
}

Val ModelFromCache(ModelVC* fun, const Context& argsCon, SrcLoc *loc) {
  Context evalCon;
  Model::T model = fun->content->sid;
  FP::Tag pk(evaluatorVersionTags.function());
  pk.Extend((char*)(&currentPickleVersion_net), sizeof_assert(currentPickleVersion_net, 4));
  CompactFV::List fvNames;
  FV2::List entryNames;
  FP::List tags;
  CacheEntry::Index cIndex;
  VestaVal::T cVal;
  CacheIntf::LookupRes cResult = CacheIntf::Miss;
  Val result = 0;

  Val old_result = 0;

  IncCounter(&sModelCallCounter);
  ThreadData *thdata = ThreadDataGet();
  ostream *traceRes = thdata->traceRes;
  
  Text pksource(fun->content->name);
  pksource += "() (special)";
  // Evaluate in the nocache cases:
  if (cacheOption < 2) {
    if (traceRes) *traceRes << ": disabled\n";
    Val fun1 = ((ModelVC*)fun)->Force();
    if (fun1->vKind == ModelVK) {
      ModelEC *modelExpr = (ModelEC*)((ModelVC*)fun1)->content->model;
      evalCon = argsCon.Append(((ModelVC*)fun1)->content->c);
      return time_eval(modelExpr->block, evalCon, pksource);
    }
    ostream& err_stream = cio().start_err();
    Error(err_stream, "Trying to apply a model that failed to parse: `", loc);
    ErrorVal(err_stream, fun1);
    ErrorDetail(err_stream, "'.\n");
    cio().end_err();
    result = NEW(ErrorVC);
    return result;
  }
  
  // Compute the primary key:
  pk.Extend(fun->FingerPrint());

  // Evaluate with caching:
  CacheEntry::IndicesApp* orphanCIs = ThreadDataGet()->orphanCIs;  
  try {
    bool waited = false;
    while (true) {
      bool noEntry;
      FV::Epoch epoch = theCache->FreeVariables(pk, /*OUT*/ fvNames, /*OUT*/ noEntry);
      if (noEntry)
	cResult = CacheIntf::Miss;
      else {
	Tags(fvNames, argsCon, /*OUT*/ tags);
	cResult = theCache->Lookup(pk, epoch, tags, /*OUT*/ cIndex, /*OUT*/ cVal);
      }
      FreeNamesTags(fvNames, tags);
      if (cResult == CacheIntf::Hit) {
	if (Unpickle(cVal, argsCon, result)) {
	  assert(result != 0);
	  // If we're supposed to compare dependencies, remember the
	  // cached value and pretend this was a miss.
	  if(dependencyCheck && shouldDependencyCheck(cIndex))
	    {
	      old_result = result;
	      cResult = CacheIntf::Miss;
	    }
	  else
	    {
	      if (traceRes) {
		*traceRes << ": hit (ci=" << cIndex << ")\n";
	      }
	      IncCounter(&sModelHitCounter);
	      UpdateOrphans(orphanCIs, orphanCIs->len, cIndex);
	      return ModelDpnd(result, fun, argsCon);
	    }
	}
	else {
	  OBufStream l_msg;
	  l_msg << "ModelFromCache: Cache hit but bad cache data." << endl << endl
		<< "\tpk = " << pk << endl
		<< "\tci = " << cIndex << endl << endl
		<< "Evaluate as cache miss." << endl;
	  Error(cio().start_err(), Text(l_msg.str()), loc);
	  cio().end_err();
	  throw(Evaluator::failure(Text("Bad cache data"), false));
	}
      }
      if (cResult == CacheIntf::Miss) {
	if(waited) {
	  if(verboseDupMsgs) {
	    cio().start_err() << ThreadLabel()
		  << "Model missed after waiting (" 
		  << fun->content->name << ")." << endl;
	    cio().end_err();
	  } else {
	    cio().start_err() << ThreadLabel()
		  << "Model missed after waiting." << endl;
	    cio().end_err();
	  }
	}
	if (traceRes) *traceRes << ": miss\n";
	int kidsIndex = orphanCIs->len;
	Val fun1 = ((ModelVC*)fun)->Force();
	if (fun1->vKind == ModelVK) {
	  evalCon = argsCon.Append(((ModelVC*)fun1)->content->c);
	  Text dup_info = verboseDupMsgs ? fun->content->name : "";
	  if(((ModelEC*)fun->content->model)->noDup && WaitForDuplicateEval(pk, "model", dup_info)) {
	    waited = true;
            continue;
	  }
	  try {
	    result = NormalModelFromCache(fun, evalCon, loc);
	    if(((ModelEC*)fun->content->model)->noDup) {
		    WakeWaitingEvals(pk, "model");
	    }
	  } catch (...) {
	    if(((ModelEC*)fun->content->model)->noDup) {
	      WakeWaitingEvals(pk, "model");
	    }
	    throw;
	  }
	  if (noAddEntry) {
	    ModelCutOff(result);
	  }
	  else {
	    // Create the special-case model entry:
	    char *types;
	    if (NamesTagsPickle(result, false, conEmpty, /*OUT*/ types, 
				/*OUT*/ entryNames, /*OUT*/ tags, /*OUT*/ cVal)) {
	      if(dependencyCheck) {
		// If we got a cache hit earlier that we can compare
		// against (we test it this way, because we change
		// cResult in when dependencyCheck is set)...
		if(old_result)
		  {
		    Val new_val = NULL;
		    if(Unpickle(cVal, evalCon, new_val)) {
		      assert(new_val != 0);
		      PrintDepsDiffs(pksource, pk, cIndex, old_result, new_val);
		      PrintDepsDiffs(pksource, pk, cIndex, result, new_val, "pickle");
		      if (traceRes) {
			*traceRes << "  " << thdata->funcCallDepth << ". " 
				  << fun->content->name
				  << ": dependencies comapred (with ci="
				  << cIndex << ")\n";
		      }
		    }
		    else
		      {
			ostream &err = cio().start_err();
			Error(err, "(Impl) Trying to compare dependencies, failed to unpickle new value");
			InternalError(err, cio().helper(true), "ModelFromCache");
			// Unreached
			cio().end_err();
		      }
		  }
	      }
	      else {
		CacheEntry::Indices kids;
		CollectKids(orphanCIs, kidsIndex, kids);
		// Add the entry, capturing the result of the call.
		CacheIntf::AddEntryRes ae_res =
		  theCache->AddEntry(pk, types, entryNames, tags, cVal, model,
				     kids, pksource, /*OUT*/ cIndex);
		// If the AddEntry fails, die with an error.
		if(ae_res != CacheIntf::EntryAdded)
		  {
		    Error(cio().start_err(), Text("AddEntry failed: ")+
			  CacheIntf::AddEntryResName(ae_res)+"\n"
			  "This may be an evaluator bug.  "
			  "Please report it.\n", loc);
		    cio().end_err();
		    if (traceRes) *traceRes << endl;
		    throw(Evaluator::failure(Text("AddEntry failed (may be an "
						  "evaluator bug)"),
					     false));
		  }
		UpdateOrphans(orphanCIs, kidsIndex, cIndex);
		Checkpoint();
		if (traceRes) {
		  *traceRes << "  " << thdata->funcCallDepth << ". "
			    << fun->content->name
			    << ": add (ci=" << cIndex << ")\n";
		}
	      }
	    }
	  }
	  return ModelDpnd(result, fun, argsCon);
	}
	ostream& err_stream = cio().start_err();
	Error(err_stream, "Trying to apply a model that failed to parse: `", loc);
	ErrorVal(err_stream, fun1);
	ErrorDetail(err_stream, "'.\n");
	cio().end_err();	
	result = NEW(ErrorVC);
	return result->Merge(fun);
      }
      if (cResult != CacheIntf::FVMismatch) {
	if (traceRes) *traceRes << ": mismatch\n";
	OBufStream err_msg;
	err_msg << "ModelFromCache got bad result: "
		<< CacheIntf::LookupResName(cResult) << endl
		<< "\tpk = " << pk << endl
		<< "\tFV epoch = " << epoch << endl
		<< "\ttags.len = " << tags.len << endl
		<< "This may be an evaluator bug.  Please report it." << endl;
    	
        Error(cio().start_err(), Text(err_msg.str()), loc);
	cio().end_err();
	result = NEW(ErrorVC);
	return result;
      }
    }
  } catch (PrefixTbl::Overflow) {
    ostream& err_stream = cio().start_err();
    Error(err_stream, Text("Internal limits exceeded: PrefixTbl overflow\n"), loc);
    err_stream << "Caching model (special):" << endl << endl
	       << "  " << fun->content->name
	       << endl << endl
	       << Text("This usually means that there are too many free variables "
		       "in the secondary key of the function being cached.  For "
		       "models, this may mean too much work is being done in a "
		       "single model file.  Try splitting up the work of this "
		       "model file into several separate model files each doing "
		       "a smaller amount of work.  (Fixing this will also reduce "
		       "the CPU load of your evaluator and cache server, and "
		       "improve build performance.)").WordWrap()
	       << endl << endl;
    cio().end_err(/*aborting*/true);
    // This is a fatal error.  Exit and dump core.
    abort();
  } catch (SRPC::failure f) {
    Error(cio().start_err(), 
	  Text("ModelFromCache: SRPC failure (") + IntToText(f.r) + "): "
	  + f.msg + ".\n", loc);
    cio().end_err();
    if (traceRes) *traceRes << endl;
    throw(Evaluator::failure(Text("Cache server is possibly down"), false));
  }
  //return result; // not reached
}

Val ApplyModel(ModelVC* fun, ApplyEC *ae, const Context& c) {
  /* There are two cache entries created.  The first one created in
     NormalModelFromCache is the normal-case model entry, the second one
     is the special-case model entry.  In cache lookup, we always first
     look up the special-case model entry, and then the normal-case
     model entry.   */
  ArgList args = ae->args;
  Exprs acts = args->elems;
  Context argsCon;

  ThreadData *thdata = ThreadDataGet();
  ostream *traceRes = thdata->traceRes;

  // Bind model argument:
  int aSize = acts.size();
  if (aSize == 0) {
    Name eDot = NEW_CONSTR(NameEC, (nameDot, args->loc));
    PushToContext(nameDot, eDot, c, argsCon);
  }
  else if (aSize == 1)
    PushToContext(nameDot, acts.getlo(), c, argsCon);
  else {
    args->EError(cio().start_err(), "Too many actual parameters:");
    cio().end_err();
    thdata->funcCallDepth++;
    if (traceRes) {
      *traceRes << "  " << thdata->funcCallDepth << ". " << fun->content->name;
      *traceRes << ": badargs\n";
    }
    thdata->funcCallDepth--;
    return NEW(ErrorVC);
  }

  thdata->funcCallDepth++;
  // Record this call if traced:
  if (traceRes) {
    *traceRes << "  " << thdata->funcCallDepth << ". " << fun->content->name;
  }

  // Evaluate:
  Val result = ModelFromCache(fun, argsCon, ae->loc);
  thdata->funcCallDepth--;
  return result;
}  

// RunTool application:
bool CheckTreatment(TextVC *tr) {
  Text txt(tr->NDS());
  return (txt == "ignore" || 
	  txt == "report" || 
	  txt == "report_nocache" ||
          txt == "value" ||
          txt == "report_value");	  
}

bool AllTexts(BindingVC *b) {
  Context work = b->elems;
  while (!work.Null()) {
    Assoc a = work.Pop();
    if (a->val->vKind != TextVK) return false;
  }
  return true; 
}

bool BindRunToolArgs(ArgList args, const Context& c,
                     RunToolArgs& runToolArgs) {
  // Handle type-checking and defaulting of arguments to _run_tool
  int nArgs = args->Length();
  Exprs elems = args->elems;
  Val val;

  if (nArgs < 2) {
    args->EError(cio().start_err(), "Too few actual parameters:");
    cio().end_err();
    return false;
  }
  if (nArgs > 10) {
    args->EError(cio().start_err(), "Too many actual parameters:");
    cio().end_err();
    return false;
  }
  
  runToolArgs.loc = args->loc;

  // platform:
  val = elems.get(0)->Eval(c);
  if (val->vKind != TextVK) {
    args->EError(cio().start_err(), 
		 "The `platform' argument to _run_tool must be a text:");
    cio().end_err();
    return false;
  }
  runToolArgs.platform = (TextVC*)val;

  // command:
  val = elems.get(1)->Eval(c);
  bool ok = false;
  if (val->vKind == ListVK) {
    Vals vs = ((ListVC*)val)->elems;
    ok = true;
    while (!vs.Null())
      if (vs.Pop()->vKind != TextVK) { ok = false; break; }
  }
  if (!ok) {
    args->EError(cio().start_err(),
		 "The `command' argument to _run_tool must be a list of texts:");
    cio().end_err();
    return false;
  }
  runToolArgs.command_line = (ListVC*)val;

  // stdin:
  if (nArgs <= 2) {
    Text stdin_default(emptyText);
    val = NEW_CONSTR(TextVC, (stdin_default));
  }
  else {
    val = elems.get(2)->Eval(c);
    if (val->vKind != TextVK) {
      args->EError(cio().start_err(), 
		   "The `stdin' argument to _run_tool must be a text:");
      cio().end_err();
      return false;
    }
  }
  runToolArgs.stdin_data = (TextVC*)val;

  // stdout_treatment:
  if (nArgs <= 3) {
    Text stdout_treatment_default("report");
    val = NEW_CONSTR(TextVC, (stdout_treatment_default));
  }
  else {
    val = elems.get(3)->Eval(c);
    if (val->vKind != TextVK || !CheckTreatment((TextVC*)val)) {
      args->EError(cio().start_err(),
		   "The `stdout_treatment' argument to _run_tool is not a suitable text:");
      cio().end_err();
      return false;
    }
  }
  runToolArgs.stdout_treatment = (TextVC*)val;

  // stderr_treatment:
  if (nArgs <= 4) {
    Text stderr_treatment_default("report");
    val = NEW_CONSTR(TextVC, (stderr_treatment_default));
  }
  else {
    val = elems.get(4)->Eval(c);
    if (val->vKind != TextVK || !CheckTreatment((TextVC *)val)) {
      args->EError(cio().start_err(),
		   "The `stderr_treatment' argument to _run_tool is not a suitable text:");
      cio().end_err();
      return false;
    }
  }
  runToolArgs.stderr_treatment = (TextVC*)val;

  // status_treatment:
  if (nArgs <= 5) {
    Text status_treatment_default("report_nocache");
    val = NEW_CONSTR(TextVC, (status_treatment_default));
  }
  else {
    val = elems.get(5)->Eval(c);
    if (val->vKind != TextVK || !CheckTreatment((TextVC *)val)) {
      args->EError(cio().start_err(), 
		   "The `status_treatment' argument to _run_tool is not a suitable text:");
      cio().end_err();
      return false;
    }
  }
  runToolArgs.status_treatment = (TextVC*)val;

  // signal_treatment:
  if (nArgs <= 6) {
    Text signal_treatment_default("report_nocache");
    val = NEW_CONSTR(TextVC, (signal_treatment_default));
  }
  else {
    val = elems.get(6)->Eval(c);
    if (val->vKind != TextVK || !CheckTreatment((TextVC *)val)) {
      args->EError(cio().start_err(),
		   "The `signal_treatment' argument to _run_tool is not a suitable text:");
      cio().end_err();
      return false;
    }
  }
  runToolArgs.signal_treatment = (TextVC*)val;

  // fp_content:
  if (nArgs <= 7) {
    val = fpContent;
  }
  else {
    val = elems.get(7)->Eval(c);
    if (val->vKind == BooleanVK) {
      val = NEW_CONSTR(IntegerVC, (((BooleanVC*)val)->b ? -1 : 0));
    }
    else if (val->vKind != IntegerVK) {
      args->EError(cio().start_err(),
		   "The `fp_content' argument to _run_tool must be an integer or boolean:");
      cio().end_err();
      return false;
    }
    else if (((IntegerVC*)val)->num == -2) {
      val = fpContent;
    }
  }
  runToolArgs.fp_content = (IntegerVC*)val;

  // wd:
  if (nArgs <= 8) {
    Text wd_name(".WD");
    val = NEW_CONSTR(TextVC, (wd_name));
  }
  else {
    val = elems.get(8)->Eval(c);
    if (val->vKind != TextVK) {
      args->EError(cio().start_err(),
		   "The `wd' argument to _run_tool must be a text:");
      cio().end_err();
      return false;
    }
  }
  runToolArgs.wd_name = (TextVC*)val;

  // existing_writable:
  if (nArgs <= 9) {
    val = NEW_CONSTR(BooleanVC, (false));
  }
  else {
    val = elems.get(9)->Eval(c);
    if (val->vKind != BooleanVK) {
      args->EError(cio().start_err(), 
		   "The `existing_writable' argument to _run_tool must be a boolean:");
      cio().end_err();
      return false;
    }      
  }
  runToolArgs.existing_writable = (BooleanVC*)val;

  // .:
  val = LookupInContext(nameDot, c);
  if (val->vKind != BindingVK) {
    args->EError(cio().start_err(),
		 "The `.' argument to _run_tool must be a binding:");
    cio().end_err();
    return false;
  }
  runToolArgs.dot = (BindingVC *)val->Copy();
  runToolArgs.dot->path = NEW_CONSTR(DepPath, (nameDot));
  Val envVars = runToolArgs.dot->Lookup("envVars");
  if (envVars->vKind != BindingVK || !AllTexts((BindingVC*)envVars)) {
    args->EError(cio().start_err(),
		 "./envVars isn't a binding of texts:");
    cio().end_err();
    return false;
  }
  Val root = runToolArgs.dot->Lookup("root"); 
  if (root->vKind != BindingVK) {
    args->EError(cio().start_err(), "./root isn't a binding:");
    cio().end_err();
    return false;
  }

  // ./dep_control:
  val = runToolArgs.dot->LookupNoDpnd("tool_dep_control"); 
  if ((val == valUnbnd) || (val == 0)) {
    val = default_dep_control;
  }
  else {
    if (val->vKind != BindingVK) {
      args->EError(cio().start_err(), 
		   "./tool_dep_control must be a binding");
      cio().end_err();
      return false;
    }
    // Check for an unrecognized names in dep_control, and warn the
    // user if there are any.
    {
      TextSeq unrecognized_names;
      Context work = ((BindingVC *) val)->elems;
      while (!work.Null()) {
	Assoc a = work.Pop();
	// Note: if any new names are added to dep_control, this will
	// need to be updated.
	if((a->name != "pk") &&
	   (a->name != "coarse") &&
	   (a->name != "coarse_names"))
	  unrecognized_names.addhi(a->name);
      }
      // We found some unrecognized names.  Print a warning for the
      // user as we'll ignore them.
      if(unrecognized_names.size() > 0)
	{
	  ostream& err_stream = cio().start_err();
	  Warning(err_stream,
		  "ignoring unrecognized names in ./tool_dep_control:",
		  runToolArgs.loc);
	  err_stream << endl;
	  for(unsigned int i = 0; i < unrecognized_names.size(); i++)
	    {
	      err_stream << "\t" << unrecognized_names.get_ref(i) << endl;
	    }
	  err_stream << endl;
	  cio().end_err();
	}
    }
  }
  assert(val->vKind == BindingVK);
  runToolArgs.dep_control = (BindingVC *) val;

  // RunTool arguments are ok.
  return true;
}

// Make it possibke to use std::sort on a container of Assocs (see
// below).
static bool AssocNameLessThan(const Assoc &a, const Assoc &b)
{
  assert(a != 0);
  assert(b != 0);
  return a->name < b->name;
}

static void print_ignored_path(ostream &out, const char *reason,
			       const ArcSeq &path)
{
  out << "\t";
  // The first two elements of the path are always "./root", so we
  // skip them when printing a message about ignoring something.
  for(unsigned int i = 2; i < path.size(); i++)
    {
      if(i > 2) out << "/";
      out << path.get_ref(i);
    }
  out << " : " << reason << endl;
}

void RunToolPKFromRoot(BindingVC *control, BindingVC *dir, FP::Tag &pk,
		       const ArcSeq &partial_path, DPaths &fv_exclude,
		       ostream &ignore_msgs)
{
  // We start by sorting the control binding by name.  We do this to
  // make sure that the PK does not depend on the order of elements in
  // the control binding.
  Context work_con = control->elems;
  Sequence<Assoc> work;
  while(!work_con.Null())
    {
      work.addhi(work_con.Pop());
    }
  std::sort(work.begin(), work.end(), AssocNameLessThan);

  // Now go through the sorted control binding
  while(work.size() > 0)
    {
      Assoc a = work.remlo();
      ArcSeq path_seq(partial_path);
      path_seq.addhi(a->name);
      Val v = dir->LookupNoDpnd(a->name);

      if((v != 0) && (v != valUnbnd))
	{
	  // If the control value is a non-zero integer or a true
	  // boolean, then we are to include this value in the PK
	  if(((a->val->vKind == IntegerVK) &&
	      ((IntegerVC *) a->val)->num) ||
	     ((a->val->vKind == BooleanVK) &&
	      ((BooleanVC *)a->val)->b))
	    {
	      // We construct a DepPath because it's a convenient way
	      // to get a fingerprint for the path that we can include
	      // in the PK along with the fingerprint of the value.
	      // (We must include the path to avoid the case of the
	      // same file appearing at two different paths and being
	      // included in the PK at those different paths.)
	      DepPath path(NEW_CONSTR(ArcSeq, (path_seq)), DummyPK);

	      pk.Extend(path.content->pathFP);
	      pk.Extend(v->FingerPrint());

	      // Remember that we need to exclude this path from the
	      // secondary dependencies of this _run_tool when adding
	      // a cache entry.
	      fv_exclude.Add(&path, a->val);
	    }
	  // If both are bindings, recurse
	  else if((a->val->vKind == BindingVK) &&
		  (v != 0) &&
		  (v->vKind == BindingVK))
	    {
	      RunToolPKFromRoot((BindingVC *) a->val, (BindingVC *) v,
				pk, path_seq, fv_exclude, ignore_msgs);
	    }
	  // Note if the control is a binding but the value from the
	  // directory is some other type, we will ignore it.
	  else
	    {
	      print_ignored_path(ignore_msgs,
				 ((a->val->vKind == BindingVK)
				  ? "control binding for non-directory"
				  : "invalid control type"),
				 path_seq);
	    }
	}
      // Note that if the the value is missing in the directory, we
      // ignore the control value.
      else
	{
	  print_ignored_path(ignore_msgs, "not in ./root", path_seq);
	}
    }
}

DPaths *RunToolPK(const RunToolArgs& runToolArgs, FP::Tag &pk) {
  // Primary key for a runtool call:
  // We combine fingerprints of relevant arguments to _run_tool, 
  // except for ./root, whose dependencies are fine-grained. 
  pk.Extend("_run_tool");
  pk.Extend(runToolArgs.platform->FingerPrint());
  pk.Extend(runToolArgs.command_line->FingerPrint());
  pk.Extend(runToolArgs.stdin_data->FingerPrint());
  pk.Extend(runToolArgs.stdout_treatment->FingerPrint());
  pk.Extend(runToolArgs.stderr_treatment->FingerPrint());
  pk.Extend(runToolArgs.status_treatment->FingerPrint());
  pk.Extend(runToolArgs.signal_treatment->FingerPrint());
  pk.Extend(runToolArgs.fp_content->FingerPrint());
  pk.Extend(runToolArgs.wd_name->FingerPrint());
  if (runToolArgs.existing_writable->b)
    pk.Extend(runToolArgs.existing_writable->FingerPrint());
  Val envVars = runToolArgs.dot->Lookup("envVars");
  pk.Extend(envVars->FingerPrint());

  DPaths *fv_exclude = 0;
  if(runToolArgs.dep_control != 0)
    {
      Val pk_control = runToolArgs.dep_control->LookupNoDpnd("pk");

      if((pk_control != 0) &&
	 (pk_control != valUnbnd))
	{
	  if(pk_control->vKind == BindingVK)
	    {
	      ArcSeq path_seq;
	      path_seq.addhi(".");
	      path_seq.addhi("root");

	      fv_exclude = NEW(DPaths);
	      Val root = runToolArgs.dot->LookupNoDpnd("root"); 
	      assert(root->vKind == BindingVK);
	      OBufStream ignore_msgs;
	      RunToolPKFromRoot((BindingVC *) pk_control, (BindingVC *) root,
				pk, path_seq, *fv_exclude, ignore_msgs);
	      // If we ignored something from pk_control, print a
	      // warning for the user
	      if(!ignore_msgs.empty())
		{
		  ostream& err_stream = cio().start_err();
		  Warning(err_stream,
			  ("ignoring some values in 'pk' sub-binding"
			   " of ./tool_dep_control:"),
			  runToolArgs.loc);
		  err_stream << endl
			     << ignore_msgs.str()
			     << endl;
		  cio().end_err();
		}
	    }
	  else
	    {
	      // We're ignoring the whole thing because it's not a
	      // binding: print a warning for the user
	      Warning(cio().start_err(),
		      "ignoring non-binding for 'pk' in ./tool_dep_control",
		      runToolArgs.loc);
	      cio().end_err();
	    }
	}
      
    }
  return fv_exclude;
}

Val MergeArgsDpnd(Val result, const RunToolArgs& args) {
  // Add the dependency of the arguments of this runtool call.
  result->Merge(args.platform);

  ListVC *command = args.command_line;
  result->Merge(command);
  if (!command->path) {
    if (command->lenDps != NULL &&
	command->lenDps->Size() != 0) {
      DPaths psTemp;
      ValueDpnd(command, &psTemp);
      result->MergeDPS(&psTemp);
    }
    else {
      Vals vs = command->elems;
      while (!vs.Null())
	result->Merge(vs.Pop());
    }
  }
  result->Merge(args.stdin_data);
  result->Merge(args.stdout_treatment);
  result->Merge(args.stderr_treatment);
  result->Merge(args.wd_name);
  result->Merge(args.existing_writable);
  
  Val envVars = args.dot->Lookup("envVars");
  assert(envVars->vKind == BindingVK);
  result->Merge((BindingVC*)envVars);
  assert(envVars->path);
  return result;
}

static Basics::thread renewLeaseThread;

static void* RenewLeases(void *arg) throw () {
  time_t tsStart;
  // time_t sleepDuration = (time_t)(leaseDuration * 0.8);
  time_t sleepDuration = 10;

  // Remember how many time we've hit the "server busy" case.
  unsigned int server_busy_count = 0;
  
  while (true) {
    int left = sleepDuration;
    do {
      left = sleep(left);
    } while (left > 0);

    // If lease already expired, quit.
    leaseMu.lock();
    if (time(&tsStart) - lastRenewed > leaseDuration) {
      leaseMu.unlock();
      // If we've had trouble getting the cache server to accept our
      // calls and the leases expired, print a message about that.
      if(server_busy_count > 0)
	{
	  Error(cio().start_err(), 
		Text("RenewLeases thread got \"connection refused: server busy\" ") +
		IntToText(server_busy_count) + " times, and now leases have expired!\n");
	  cio().end_err();
	}
      return NULL;
    }
    leaseMu.unlock();
    
    // Renew leases:
    CacheEntry::IndicesApp orphanCIs;    
    frontierMu.lock();
    ThreadData::mu.lock();
    ThreadData* cur = ThreadData::head;
    while (cur) {
      assert(cur->orphanCIs != 0);
      for (int i = 0; i < cur->orphanCIs->len; i++) {
	orphanCIs.Append(cur->orphanCIs->index[i]);
      }
      for(ThreadData::orphanCI_set::iterator it =
	    cur->unclaimed_child_orphanCIs.begin();
	  it != cur->unclaimed_child_orphanCIs.end();
	  it++)
	{
	  orphanCIs.Append(*it);
	}
      cur = cur->next;
    }
    ThreadData::mu.unlock();
    frontierMu.unlock();

    bool renewed = false;
    try
      {
	// Renew the leases, remembering whether we succeeded.
	renewed = theCache->RenewLeases(orphanCIs);

	// No "server busy" SRPC failure.
	server_busy_count = 0;

	// If we didn't renew successfully, print an error message,
	// remember that we've failed, and exit the thread.
	if(!renewed)
	  {
	    Error(cio().start_err(), 
		  Text("RenewLeases returned failure!\n") +
		  "This may be a bug.  Please reposrt it.\n");
	    cio().end_err();

	    leaseMu.lock();
	    renewLease_failure = true;
	    leaseMu.unlock();

	    return NULL;
	  }
      }
    // If we get an SRPC failure...
    catch (SRPC::failure f)
      {
	// If this looks like hitting the cache server's connection
	// limits, ignore it (but remember it).  (Presumably we'll get
	// in on a subsequent call.)
	if((f.r == 1) && (f.msg.FindText("connection refused: server busy") != -1))
	  {
	    server_busy_count++;
	  }
	// If it doesn't look like the LimService message...
	else
	  {
	    // ...assume the cache server went down.  Print an error
	    // message with the SRPC failure, note that we've failed,
	    // and exit the thread.
	    Error(cio().start_err(), Text("RenewLeases thread: SRPC failure (")
		  + IntToText(f.r) + "): " + f.msg + ".\n");
	    cio().end_err();

	    leaseMu.lock();
	    renewLease_failure = true;
	    leaseMu.unlock();

	    return NULL;
	  }
      }
    
    // If lease expires during renewing, quit.
    leaseMu.lock();
    if (time(NULL) - lastRenewed > leaseDuration) {
      leaseMu.unlock();
      // If we've had trouble getting the cache server to accept our
      // calls and the leases expired, print a message about that.
      if(server_busy_count > 0)
	{
	  Error(cio().start_err(), 
		Text("RenewLeases thread got \"connection refused: server busy\" ") +
		IntToText(server_busy_count) + " times, and now leases have expired!\n");
	  cio().end_err();
	}
      return NULL;
    }
    
    if(renewed)
      {
	// Leases are renewed successfully:
	lastRenewed = tsStart;
      }
    leaseMu.unlock();    
  }
}

extern "C" void StartRenewLeaseThread_inner() {
  if (cacheOption != 0) {
    time(&lastRenewed);  
    renewLeaseThread.fork_and_detach(RenewLeases, (void*)NULL);
  }
}

void StartRenewLeaseThread()
{
  static pthread_once_t init_once = PTHREAD_ONCE_INIT;
  pthread_once(&init_once, StartRenewLeaseThread_inner);
}

void EvalTagsInit()
{
  evaluatorVersionTags.salt();
}

Val RunToolFromCache(const RunToolArgs& runToolArgs, const Context& c, SrcLoc *loc) {
  FP::Tag pk(evaluatorVersionTags.runTool());
  pk.Extend((char*)(&currentPickleVersion_net), sizeof_assert(currentPickleVersion_net, 4));
  CompactFV::List fvNames;
  FV2::List entryNames;
  FP::List tags;
  CacheEntry::Index cIndex;
  VestaVal::T cVal;
  CacheIntf::LookupRes cResult = CacheIntf::Miss;
  Model::T model = NullShortId;
  Val result = 0;
  VestaSource* rootForTool;

  Val old_result = 0;
  
  ThreadData *thdata = ThreadDataGet();
  ostream *traceRes = thdata->traceRes;

  // Evaluate in the nocaching cases:
  if (cacheOption == 0) {
    if (traceRes) *traceRes << ": disabled\n";
    result = RunTool(runToolArgs, rootForTool);
    DeleteRootForTool(rootForTool);
    return result;
  }

  // Compute the primary key and gather paths to be excluded from the
  // fv set when adding a cache entry
  DPaths *fv_exclude = RunToolPK(runToolArgs, pk);

  // Evaluate with caching:
  CacheEntry::IndicesApp* orphanCIs = ThreadDataGet()->orphanCIs;    
  try {
    bool waited = false;
    Text dup_info;
    while (true) {
      bool noEntry;
      FV::Epoch epoch = theCache->FreeVariables(pk, /*OUT*/ fvNames, /*OUT*/ noEntry);
      if (noEntry)
	cResult = CacheIntf::Miss;
      else {
	Tags(fvNames, c, /*OUT*/ tags);
	cResult = theCache->Lookup(pk, epoch, tags, /*OUT*/ cIndex, /*OUT*/ cVal);
      }
      FreeNamesTags(fvNames, tags);
      if (cResult == CacheIntf::Hit) {
	if (Unpickle(cVal, c, result)) {
	  assert(result != 0);
	  // If we're supposed to compare dependencies, remember the
	  // cached value and pretend this was a miss.
	  if(dependencyCheck && shouldDependencyCheck(cIndex))
	    {
	      old_result = result;
	      cResult = CacheIntf::Miss;
	    }
	  else
	    {
	      if (traceRes) *traceRes << ": hit(ci=" << cIndex << ")\n";
	      IncCounter(&toolHitCounter);
	      if (cacheOption == 1) {
		result->dps = NULL;
		return result;
	      }
	      UpdateOrphans(orphanCIs, orphanCIs->len, cIndex);
	      return MergeArgsDpnd(result, runToolArgs);
	    }
	}
	else {
	  OBufStream l_msg;
	  l_msg << "RunToolFromCache: Cache hit but bad cache data." << endl << endl
		<< "\tpk = " << pk << endl
		<< "\tci = " << cIndex << endl << endl
		<< "Evaluate as cache miss." << endl;
	  Error(cio().start_err(), Text(l_msg.str()), loc);
	  cio().end_err();
	  cResult = CacheIntf::Miss;
	}
      }
      if (cResult == CacheIntf::Miss) {
	if (traceRes) *traceRes << ": miss\n";
	int kidsIndex = orphanCIs->len;

	// Check to make sure there isn't an outstanding tool run with the same pk
	if(waited) {
	  cio().start_err() << ThreadLabel() << "Tool missed after waiting ("
			    << dup_info << ")." << endl;
	  cio().end_err();
	}
	dup_info = ThreadLabel();
	dup_info += "... ";
	if(verboseDupMsgs) {
	  dup_info += ToolCommandLineAsText(runToolArgs);
	} else {
	  dup_info += ((TextVC*)(runToolArgs.command_line->elems.First()))->NDS();
	}
	if(WaitForDuplicateEval(pk, "tool", dup_info)) {
		// another thread just did it.  repeat the lookup
		waited = true;
		continue;
	}

	// run the tool
	try {
	  result = RunTool(runToolArgs, rootForTool);
	  char *types;
	  if (!noAddEntry &&
	    NamesTagsPickle(result, false, conEmpty, /*OUT*/ types, /*OUT*/ entryNames,
			    /*OUT*/ tags, /*OUT*/ cVal, fv_exclude)) {
	    Text pksource("_run_tool, command line: ");
	    pksource += ToolCommandLineAsText(runToolArgs);

	    if(dependencyCheck) {
	      // If we got a cache hit earlier that we can compare
	      // against (we test it this way, because we change
	      // cResult in when dependencyCheck is set)...
	      if(old_result) {
		Val new_val = NULL;
		if(Unpickle(cVal, c, new_val)) {
		  assert(new_val != 0);
		  PrintDepsDiffs(pksource, pk, cIndex, old_result, new_val);
		  PrintDepsDiffs(pksource, pk, cIndex, result, new_val, "pickle");

		  if (traceRes) {
		    *traceRes << "  " << thdata->funcCallDepth << ". " << loc->file 
			      << ": " << "_run_tool(): dependencies comapred (with ci="
			      << cIndex << ")\n";
		  }

		} else {
		  ostream &err = cio().start_err();
		  Error(err, "(Impl) Trying to compare dependencies, failed to unpickle new value");
		  InternalError(err, cio().helper(true), "RunToolFromCache");
		  // Unreached
		  cio().end_err();
		}
	      }
	    } else {
	      CacheEntry::Indices kids;
	      CollectKids(orphanCIs, kidsIndex, kids);
	      // kids should be empty when evaluating eagerly.

	      // Add the entry, capturing the result of the call.
	      CacheIntf::AddEntryRes ae_res =
		    theCache->AddEntry(pk, types, entryNames, tags, cVal, model,
				 kids, pksource, /*OUT*/ cIndex);
	      // If the AddEntry fails, die with an error.
	      if(ae_res != CacheIntf::EntryAdded) {
		Error(cio().start_err(),
		      Text("AddEntry failed: ")+
		      CacheIntf::AddEntryResName(ae_res)+"\n"
		      "This may be an evaluator bug.  "
		      "Please report it.\n",
		      loc);
		cio().end_err();
		if (traceRes) *traceRes << endl;
		throw(Evaluator::failure(Text("AddEntry failed (may be an "
					      "evaluator bug)"),
					false));
	      }
	      // cIndex is added to the orphans.
	      UpdateOrphans(orphanCIs, kidsIndex, cIndex);
	      Checkpoint();
	      if (traceRes) {
		*traceRes << "  " << thdata->funcCallDepth << ". " << loc->file << ": "
			<< "_run_tool(): add (ci=" << cIndex << ")\n";
	      }
	    }
	  }
	  // now wake up the other threads waiting for the same PK
	  WakeWaitingEvals(pk, "tool");
	} catch(...) {
	  WakeWaitingEvals(pk, "tool");
	  throw;
	}

	// delete the volatile dir for root after adding cache entry.
	// The new cache entry is now protecting the deriveds from weeding.
	DeleteRootForTool(rootForTool);	  
	if (cacheOption == 1) {
	  result->dps = NULL;
	  return result;
	}
	return MergeArgsDpnd(result, runToolArgs);
      }
      if (cResult != CacheIntf::FVMismatch) {
	if (traceRes) *traceRes << ": mismatch\n";
	OBufStream err_msg;
	err_msg << "RunToolFromCache got bad result: "
		<< CacheIntf::LookupResName(cResult) << endl
		<< "\tpk = " << pk << endl
		<< "\tFV epoch = " << epoch << endl
		<< "\ttags.len = " << tags.len << endl
		<< "This may be an evaluator bug.  Please report it." << endl;
        Error(cio().start_err(), Text(err_msg.str()), loc);
	cio().end_err();
	result = NEW(ErrorVC);
	return result;
      }
    }
  } catch (PrefixTbl::Overflow) {
    Text cmd_line = ToolCommandLineAsText(runToolArgs);
    ostream& err_stream = cio().start_err();
    Error(err_stream, Text("Internal limits exceeded: PrefixTbl overflow\n"), loc);
    err_stream << "Caching _run_tool with command line:" << endl << endl
	       << "  " << cmd_line << endl << endl
	       << Text("This usually means that there are too many free variables "
		       "in the secondary key of the function being cached.  For "
		       "_run_tool calls, this may mean you're trying to do too "
		       "much work in a single tool invocation.  Try splitting the "
		       "work of this tool invocation into several separate tool "
		       "invocations each doing a smaller amount of work and using "
		       "a smaller number of source files.  (Fixing this will also "
		       "reduce the CPU load of your evaluator and cache server, and "
		       "improve build performance.)").WordWrap()
	       << endl << endl;
    cio().end_err(/*aborting*/true);
    // This is a fatal error.  Exit and dump core.
    abort();
  } catch (SRPC::failure f) {
    Error(cio().start_err(), 
	  Text("RunToolFromCache: SRPC failure (")
	  + IntToText(f.r) + "): " + f.msg + ".\n",
	  loc);
    cio().end_err();
    if (traceRes) *traceRes << endl;
    throw(Evaluator::failure(Text("Cache server is possibly down"), false));
  }
  // return result;  // not reached
}

Val ApplyRunTool(ArgList args, const Context& c) {
  RunToolArgs runToolArgs;

  ThreadData *thdata = ThreadDataGet();
  ostream *traceRes = thdata->traceRes;
  
  IncCounter(&toolCallCounter);
  if (!BindRunToolArgs(args, c, runToolArgs)) {
    thdata->funcCallDepth++;
    if (traceRes) {
      *traceRes << "  " << thdata->funcCallDepth << ". " << args->loc->file << ": ";
      *traceRes << "_run_tool()";
      *traceRes << ": badargs\n";
    }
    thdata->funcCallDepth--;
    return NEW(ErrorVC);
  }
  thdata->funcCallDepth++;
  if (traceRes) {
    *traceRes << "  " << thdata->funcCallDepth << ". " << args->loc->file << ": ";
    *traceRes << "_run_tool()";
  }

  // Evaluate:
  Val result = RunToolFromCache(runToolArgs, c, args->loc);
  thdata->funcCallDepth--;
  return result;
}
