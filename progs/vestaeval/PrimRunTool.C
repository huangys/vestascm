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

/* File: PrimRunTool.C                                         */

#include <iomanip>
#include <Basics.H>
#include <OS.H>
#include <RegExp.H>
#include <VestaConfig.H>
#include <chars_seq.H>
#include <UniqueId.H>
#include <RunToolClient.H>
#include <SourceOrDerived.H>
#include <VDirSurrogate.H>
#include <VSStream.H>
#include <ReposUI.H>
#include "PrimRunTool.H"
#include "Expr.H"
#include "ToolDirectoryServer.H"
#include "Err.H"
#include "ListT.H"
#include "Signal.H"
#include "RunToolHost.H"
#include "ThreadData.H"
#include "Timing.H"
#include "EvalBasics.H"

using std::ios;
using std::ostream;
using std::istream;
using std::fstream;
using std::setw;
using std::setfill;
using std::hex;
using std::dec;
using std::endl;
using std::flush;
using Basics::OBufStream;
using OS::cio;


// Names for "fields" of RunTool result binding
static Text codeName("code"),
	    sigName("signal"),
	    stowName("stdout_written"),
	    stewName("stderr_written"),
	    stdoName("stdout"),
	    stdeName("stderr"),
	    rootName("root"),
	    coreName("dumped_core");

static DepPath rootPath(Text("."));

// Size hint for runtool dependency set:
static const int runToolDepSize = 80;

// Size hint for the number of volatile dirs in a runtool call.
static const int runToolDirNum = 20;

// Some utility functions
struct OurClosure {
  Binding newstuff;
  VestaSource *dir;
  ListT<NodeInfo> *nodeList;
  bool found_core;
  Text dir_path;
};

// A regular expression for matching core files
static Basics::RegExp *core_match_re = 0;

// The default regular expression to use, if
// [Evaluator]core_dump_regexp is not set.
static const char *core_match_re_default = "core(\\.[0-9][0-9]*)?";

// The default _run_tool dependency control binding
BindingVC *default_dep_control = 0;

extern "C"
{
  void core_match_init()
  {
    // If [Evaluator]core_dump_regexp is set, attempt to parse it as a
    // regular expression.
    if(VestaConfig::is_set("Evaluator", "core_dump_regexp"))
      {
	try
	  {
	    Text pattern =
	      VestaConfig::get_Text("Evaluator", "core_dump_regexp");
	    core_match_re = NEW_CONSTR(Basics::RegExp, (pattern));
	  }
	catch(Basics::RegExp::ParseError e)
	  {
	    cio().start_err() << "Error parsing [Evaluator]core_dump_regexp: "
			      << e.msg << endl;
	    cio().end_err();
	  }
	catch(VestaConfig::failure f)
	  {
	    // Shouldn't happen, but if it does we ignore it.
	  }
      }

    // If we still don't have a regular expression, either because
    // the config variable wasn't set, or we got a parsing error on
    // it, use a default
    if(core_match_re == 0)
      {
	try
	  {
	    core_match_re = NEW_CONSTR(Basics::RegExp, (core_match_re_default));
	  }
	catch(Basics::RegExp::ParseError e)
	  {
	    cio().start_err() << "Error parsing default core dump regular expression: "
			      << e.msg << endl;
	    cio().end_err();
	  }
      }
  }
}

// Test a filename to see if it looks like a core file
static bool core_match(Text arc)
{
  static pthread_once_t core_match_once = PTHREAD_ONCE_INIT;
  pthread_once(&core_match_once, core_match_init);

  if(core_match_re != 0) return core_match_re->match(arc);

  // Something must have gone wrong with initializing the core
  // matching regular expression.
  return false;
}

Text ToolCommandLineAsText(const RunToolArgs &args)
{
  // list is known to be a list of texts; see ApplyCache
  Vals vlist = args.command_line->elems;
  Text result;
  while (!vlist.Null()) {
    if(!result.Empty())
      {
	result += " ";
      }
    result += ((TextVC*)(vlist.Pop()))->NDS();
  }
  return result;
}

chars_seq *ConvertVals(ListVC *list) {
  // list is known to be a list of texts; see ApplyCache
  Vals vlist = list->elems;
  chars_seq *result = NEW_CONSTR(chars_seq, (5, 100));

  while (!vlist.Null()) {
    result->append(((TextVC*)(vlist.Pop()))->NDS().chars());
  }
  return result;
}

chars_seq *ConvertEnvVars(BindingVC *b) {
  // b is known to be a binding of texts; see BindRunToolArgs.
  Context c = b->elems;

  chars_seq *result = NEW_CONSTR(chars_seq, (10, 20));
  while (!c.Null()) {
    Assoc a = c.Pop();
    Text t(a->name + "=");
    t += ((TextVC*)a->val)->NDS();
    result->append(t);
  }    
  return result;
}

// The callback
bool ProcessDirectory(void* cl, VestaSource::typeTag type, Arc arc,
		      unsigned int index, Bit32 pseudoInode,
		      ShortId filesid, bool master) {
  NodeInfo ni = NEW(nodeinfo);

  ni->type  = type;
  ni->arc   = Text(arc);
  ni->index = index;
  ((OurClosure*)cl)->nodeList->Push(ni);
  return true;
}

// Helper function for a workaround for late deletion of ".nfsXXX"
// files that are are used by NFS clients for files deleted while a
// process still has them open.
static inline bool nfs_open_but_deleted_name(Arc arc)
{
  // If it doesn't start with ".nfs", it's not an open-but-deleted
  // name.
  if(strncmp(arc, ".nfs", 4) != 0)
    {
      return false;
    }
  // Skip over the ".nfs"
  arc += 4;
  // Skip characters until we find the end of the string or a
  // non-hex-digit.
  while(*arc && isxdigit(*arc))
    {
      arc++;
    }
  // If we've reached the end of the string (meaning all characters
  // after ".nfs" were hex digits), then this is an open-but-deleted
  // name.  Otherwise, it's not.
  return (*arc == 0);
}

bool AddToNewStuff(OurClosure *cl, bool look_for_cores, Text label,
		   unsigned int fp_threshold) {
  VestaSource *f, *d;
  Text name;
  TextVC *file;
  NodeInfo ni;
  OurClosure newcl;
  Arc arc;

  while(!cl->nodeList->Null()) {
    ni = cl->nodeList->Pop();
    arc = ni->arc.chars();
    name = Text(arc);
    try {
      switch (ni->type) {
      case VestaSource::mutableFile:
      case VestaSource::immutableFile:
	{
	  VestaSource::errorCode res =
	    cl->dir->lookupIndex(ni->index, f);
	  // Workaround for late delete-on-close of files deleted
	  // while open on client.  If one happens to get removed
	  // between the list call that got us its name and this
	  // lookup call, just ignore it and move on to the next entry
	  // in the directory.  (Note that this doesn't prevent such
	  // files from making it into the result of _run_tool.)
	  if((res == VestaSource::notFound) &&
	     nfs_open_but_deleted_name(arc))
	    {
	      break;
	    }
	  assert(res == VestaSource::ok);
	  while((f->type == VestaSource::mutableFile) &&
		// We can't just call makeFilesImmutable on the file
		// (f) alone.  In a volatileROEDirectory, the longid
		// of f will be a shortid-based-longid.
		// makeFilesImmutable needs to modify the enclosing
		// directory, and it's not possible to find the parent
		// directory from a shortid-based-longid.  So in that
		// case makeFilesImmutable would return
		// inapparopriateOp.

		// Calling it on the directory containing the file can
		// be more work for the server, but it may leave more
		// time before we resync this file.  That way, if
		// there are further incoming late writes, they might
		// make it mutable again by the time we call resync,
		// sending us around this loop again.  Having a
		// greater chance of gathering all the writes is an
		// advantage of calling makeFilesImmutable on the
		// whole directory.
		(cl->dir->makeFilesImmutable(fp_threshold) == VestaSource::ok))
	    {
	      f->resync();
	    }
	  assert(f->type == VestaSource::immutableFile);
	  file = NEW_CONSTR(TextVC, (name, f->shortId(), f->fptag));
	  cl->newstuff = cl->newstuff->AddBindingAssoc(name, file);
	  if(look_for_cores && core_match(name)) {
	    char *sid =
	      SourceOrDerived::shortIdToName(f->shortId(), false);
	    cio().start_err() << label << "Possible core file: "
			      << cl->dir_path << name << "\n"
			      << label << "at " << sid << endl;
	    cio().end_err();
	    delete[] sid;
	    cl->found_core = true;
	  }
	  break;
	}
      case VestaSource::volatileDirectory:
      case VestaSource::immutableDirectory:
      case VestaSource::volatileROEDirectory:
	{
	  VestaSource::errorCode res = cl->dir->lookup(arc, d);
	  assert(res == VestaSource::ok);
	  assert(d->type == VestaSource::volatileDirectory ||
		 d->type == VestaSource::immutableDirectory ||
                 d->type == VestaSource::volatileROEDirectory);
	  newcl.newstuff = NEW(BindingVC);
	  newcl.dir = d;
	  newcl.nodeList = NEW(ListT<NodeInfo>);
	  newcl.found_core = false;
	  newcl.dir_path = cl->dir_path + name + "/";
	  res = d->list(0, ProcessDirectory, &newcl, NULL, true);
	  assert(res == VestaSource::ok);
	  bool new_stuff_added = AddToNewStuff(&newcl, look_for_cores, label, fp_threshold);
	  assert(new_stuff_added);
	  cl->newstuff = cl->newstuff->AddBindingAssoc(name, newcl.newstuff);
	  cl->found_core = newcl.found_core || cl->found_core;
	  break;
	}
      case VestaSource::deleted:
	// Might have existed in original binding, or could have been
	// created and deleted by tool.
	cl->newstuff = cl->newstuff->AddBindingAssoc(name, valFalse);
	break;
      default:  // impossible
	Error(cio().start_err(), Text("AddToNewStuff failed, bad type: ") +
	      IntToText(ni->type) + ".\n");
	cio().end_err();
	return false;
      }
    } catch (SRPC::failure f) {
      Error(cio().start_err(), Text("AddToNewStuff failed (") + IntToText(f.r) +
	    "): " + f.msg + ".\n");
      cio().end_err();
      return false;
    }
  }
  return true;
}

// Format the volatile directory name into a Text
Text volatileDirName(const LongId &fsroot)
{
  unsigned int index;
  (void) fsroot.getParent(&index);
  char arc[(sizeof(index) * 2) + 1];
  sprintf(arc, "%08x", index);
  return VestaConfig::get_Text("Run_Tool", "VolatileRootName") + "/" + arc;
}


void clearCacheitBit(Val& value)
{
  switch(value->vKind)
    {
    case BindingVK:
      {
	BindingVC *bv = (BindingVC*)value;
	Context elems = bv->elems;
	while(!elems.Null()) {
	  Assoc a = elems.Pop();
	  clearCacheitBit(a->val);
	}
	break;
      }
    case ListVK:
      {
	ListVC *lstv = (ListVC*)value;	
	Vals elems = lstv->elems;
	while(!elems.Null()) {
	  Val val = elems.Pop();
	  clearCacheitBit(val);
	}
	break;
      }
    default:
      value->cacheit = false;
      break;
    }
}

// Run the tool!
Val RunTool(const RunToolArgs &args, VestaSource*& rootForTool) {
  // Returns a Binding; fields documented below.

  rootForTool = NULL;
  LongId fsroot;
  VSStream *value_out, *value_err;
  VestaSource* out_file;
  VestaSource* err_file;
  bool stdout_cache, stderr_cache, status_cache, signal_cache;
  Text stdin_name;

  OS::ThreadIOHelper* report_out = NULL;
  OS::ThreadIOHelper* report_err = NULL;

  create_tool_directory_server();

  // command_line:
  chars_seq *argv = ConvertVals(args.command_line);

  // stdin_data:
  if (args.stdin_data->Length() != 0) {
    ShortId id = args.stdin_data->Sid();
    SourceOrDerived sd;
    sd.open(id);
    stdin_name = VestaConfig::get_Text("Repository", "metadata_root") +
      sd.shortIdToName(id);
    sd.close();
  }

  // ./root: already type-checked by BindRunToolArgs
  Binding root = (BindingVC*)args.dot->LookupNoDpnd("root");

  // Initialize dirInfos of this runtool call, and put it into the 
  // table runToolCalls.
  DirInfo *rootDir = NEW(DirInfo);
  rootDir->b = root;
  rootDir->path = rootPath;
  rootDir->isRoot = true;
  rootDir->child_lookup = false;
  rootDir->coarseDep = false;
  rootDir->coarseNames = false;
  assert(args.dep_control != 0);
  rootDir->coarseDep_sel =
    (BindingVC*)args.dep_control->LookupNoDpnd("coarse");
  if(rootDir->coarseDep_sel == valUnbnd)
    rootDir->coarseDep_sel = 0;
  if((rootDir->coarseDep_sel != 0) && 
     (rootDir->coarseDep_sel->vKind != BindingVK))
    {
      Warning(cio().start_err(),
	      "ignoring non-binding for 'coarse' in ./tool_dep_control",
	      args.loc);
      cio().end_err();
      rootDir->coarseDep_sel = 0;
    }
  {
    Val coarseNames_val = args.dep_control->LookupNoDpnd("coarse_names");
    if(coarseNames_val == valUnbnd)
      coarseNames_val = 0;
    if(coarseNames_val != 0)
      {
	if(coarseNames_val->vKind == BindingVK)
	  {
	    rootDir->coarseNames_sel = (BindingVC*) coarseNames_val;
	  }
	else if(((coarseNames_val->vKind == IntegerVK) &&
	    ((IntegerVC *) coarseNames_val)->num) ||
	   ((coarseNames_val->vKind == BooleanVK) &&
	    ((BooleanVC *) coarseNames_val)->b))
	  {
	    // Completely disable fine-grained non-existence
	    // dependencies
	    rootDir->coarseNames_sel = 0;
	    rootDir->coarseNames = true;
	  }
	else
	  {
	    Warning(cio().start_err(),
		    ("ignoring invalid type for 'coarse_names' in "
		     "./tool_dep_control"),
		    args.loc);
	    cio().end_err();
	    rootDir->coarseNames_sel = 0;
	  }
      }
  }
  rootDir->namesRecorded = false;
  Word rootHandle = GetNewHandle();
  DirInfos *dirInfos = NEW(DirInfos);
  dirInfos->dirInfoTbl = NEW_CONSTR(DirInfoTbl, (runToolDirNum));
  dirInfos->dirInfoTbl->Put(WordKey(rootHandle), rootDir);
  dirInfos->dep = NEW_CONSTR(DPaths, (runToolDepSize));
  dirInfos->thread_label = ThreadLabel();
  runToolCalls.Put(WordKey(rootHandle>>20), dirInfos);

  // stdout_treatment:
  Text stdout_treatment((args.stdout_treatment)->NDS());
  stdout_cache = true;
  value_out = NULL;
  if (stdout_treatment == "value" ||
      stdout_treatment == "report_value") {
    try {
      out_file = CreateDerived();
      value_out = NEW_CONSTR(VSStream, (out_file));
    } catch (SRPC::failure f) {
      Error(cio().start_err(), 
	    Text("RunTool: Cannot create a stream for standard output; ") 
	    + f.msg + ".\n");
      cio().end_err();
      return NEW(ErrorVC);
    } 
  }
  if (stdout_treatment == "report_nocache" ||
      stdout_treatment == "report" ||
      stdout_treatment == "report_value") {
    stdout_cache = stdout_treatment != "report_nocache";
    report_out = NEW_CONSTR(OS::ThreadIOHelper, (cio(), false));
  }

  // stderr_treatment:
  Text stderr_treatment((args.stderr_treatment)->NDS());
  stderr_cache = true;
  report_err = NULL;
  value_err = NULL;
  if (stderr_treatment == "value" ||
      stderr_treatment == "report_value") {
    try {
      err_file = CreateDerived();
      value_err = NEW_CONSTR(VSStream, (err_file));
    } catch (SRPC::failure f) {
      Error(cio().start_err(), 
	    Text("RunTool: Cannot create a stream for standard err; ")
	    + f.msg + ".\n");
      cio().end_err();
      return NEW(ErrorVC);
    } 
  }
  if (stderr_treatment == "report_nocache" ||
      stderr_treatment == "report" ||
      stderr_treatment == "report_value") {
    stderr_cache = stderr_treatment != "report_nocache";
    report_err = NEW_CONSTR(OS::ThreadIOHelper, (cio(), true));
  }
  
  // status_treatment:
  Text status_treatment((args.status_treatment)->NDS());
  if (status_treatment == "report_nocache")
    status_cache = false;
  else {
    assert(status_treatment == "report");
    status_cache = true;
  }

  // signal_treatment:
  Text signal_treatment((args.signal_treatment)->NDS());
  if (signal_treatment == "report_nocache")
    signal_cache = false;
  else {
    assert(signal_treatment == "report");
    signal_cache = true;
  }

  // wd_name:
  const Text wd((args.wd_name)->NDS());

  // ./envVars:   type-checked by BindRunToolArgs
  BindingVC *envVars = (BindingVC*)args.dot->Lookup("envVars");
  chars_seq *ev = ConvertEnvVars(envVars);

  // create the volatile directory:
  try {
    VestaSource::errorCode err =
      VDirSurrogate::createVolatileDirectory(SRPC::this_host().chars(),
					     toolDirServerIntfName.chars(),
					     rootHandle, rootForTool, 0,
					     !args.existing_writable->b);
    if (err != VestaSource::ok) {
      Error(cio().start_err(), Text("RunTool: createVolatileDirectory failed (") +
	    VestaSource::errorCodeString(err) + ".\n");
      cio().end_err();
      return NEW(ErrorVC);
    }
  } catch (SRPC::failure f) {
    Error(cio().start_err(), Text("RunTool: createVolatileDirectory failed (")
	  + IntToText(f.r) + "): " + f.msg + ".\n");
    cio().end_err();
    return NEW(ErrorVC);
  }
  fsroot = rootForTool->longid;

  // host:
  void* handle;
  const Text host(RunToolHost(args.platform, args.loc, /*OUT*/handle));
  const Text label = ThreadLabel();

  // print the command-line:
  OBufStream msg;
  msg << label << host << ": ";
  for (int arg = 0; arg < argv->length(); arg++) {
    msg << (*argv)[arg] << " ";
  }
  ostream& out = cio().start_out();
  out << msg.str();
  out << endl;
  if(getenv("DEBUG_TOOL_DIR_SERVER"))
    {
      // Print out the root handle for the tool directory server, the
      // LongId of the root, and the name of the root within the
      // volatile root.
      char handle_buff[17];
      sprintf(handle_buff, "%016" FORMAT_LENGTH_INT_64 "x", rootHandle);
      out << label << "[tool dir server root handle = "
	  << handle_buff << "]" << endl
	  << label << "[root LongId = ";
      for(unsigned int i = 0, len = fsroot.length(); i < len; i++)
	{
	  out << setw(2) << setfill('0') << hex
	      << (int) (fsroot.value.byte[i] & 0xff);
	}
      out << setfill(' ') << dec << "]" << endl
	  << label << "[root directory name = ";
      out << volatileDirName(fsroot) << "]" << endl;
    }
  cio().end_out();
  if(pauseBeforeTool) {
    ostream* out = NULL;
    istream* in = NULL;
    cio().start_prompt(out, in);
    assert(out != 0);
    assert(in != 0);
    *out << label << "Running in " << volatileDirName(fsroot) << "/"
	 << wd << endl;
    *out << label << "Using the following environment variables:"
	 << endl;
    for(int i = 0; i < ev->length(); i++) {
      *out << label << "\t" << (*ev)[i] << endl;
    }
    char enter;
    *out << label << "Press Enter to continue..." << flush;
    in->get(enter);
    *out << label << "Continuing." << endl;
    cio().end_prompt();
  }

  // Call the RunTool!
  RunTool::Tool_result r;
  try {
    Timers::IntervalRecorder *tool_run_timer = 0;
    bool time_me = TimingRecorder::level >= TimingRecorder::PrintToolTimings
	    && TimingRecorder::name_match(msg.str());
    if(time_me) {
      tool_run_timer = NEW_PTRFREE(Timers::IntervalRecorder);
    }
    r = RunTool::do_it(
      host,       // const Text &host
      argv,       // chars_seq *argv
      fsroot,     // LongId &fsroot
      wd,         // const Text &wd = DEFAULT_TOOL_WD
      ev,         // chars_seq *ev = NULL
      report_out,
      value_out,
      report_err,
      value_err,
      label,
      stdin_name  // const Text &stdin_name = ""
    );
    if(time_me) {
      assert(tool_run_timer != 0);
      double secs = tool_run_timer->get_delta();
      TimingRecorder::start_out() << label << "Tool " << msg.str()
	      << " finished after " << secs << " seconds." << endl;
      TimingRecorder::end_out();
      delete tool_run_timer;
    }
    //  struct Tool_result {
    //    int status;
    //    int sigval;
    //    bool stdout_written;
    //    bool stderr_written;
    //  };
    if(report_out) delete report_out;
    if(report_err) delete report_err;
    if(value_out) { 
      if(value_out->error()) {
	Error(cio().start_err(), 
	      "Error capturing stdout: " + value_out->get_error() + "\n");
	cio().end_err();
	RunToolDone(handle);
	throw(Evaluator::failure(Text("_run_tool failure"), false));
      }
      delete value_out; 
      value_out = NULL;
    }
    if(value_err) { 
      if(value_err->error()) {
	Error(cio().start_err(),
	      "Error capturing stderr: " + value_err->get_error() + "\n");
	cio().end_err();
	RunToolDone(handle);
	throw(Evaluator::failure(Text("_run_tool failure"), false));
      }
      delete value_err; 
      value_err = NULL;
    } 
  } catch (SRPC::failure f) {
    Error(cio().start_err(), "invoking _run_tool: " + f.msg + "\n");
    cio().end_err();
    RunToolDone(handle);
    throw(Evaluator::failure(Text("_run_tool failure"), false));
  }
  if(pauseAfterTool || (pauseAfterToolSignal && r.sigval) ||
     (pauseAfterToolError && r.status)) {
    ostream* out = NULL; 
    istream* in = NULL;
    cio().start_prompt(out, in);
    assert(out != 0);
    assert(in != 0);
    *out << label << "Ran in " << volatileDirName(fsroot) << "/" << wd
	 << endl;
    *out << label << "Used the following environment variables:" << endl;
    for(int i = 0; i < ev->length(); i++) {
      *out << label << "\t" << (*ev)[i] << endl;
    }
    *out << label << "Exited with status " << r.status << ", signal "
	 << r.sigval << endl;
    char enter;
    *out << label << "Press Enter to continue..." << flush;
    in->get(enter);
    *out << label << "Continuing." << endl;
    cio().end_prompt();
  }
  RunToolDone(handle);

  OurClosure cl;
  cl.newstuff = NEW(BindingVC);
  cl.dir = rootForTool;
  cl.nodeList = NEW(ListT<NodeInfo>);
  cl.found_core = false;
  cl.dir_path = "/";

  try {
    VestaSource::errorCode ok;
    ok = rootForTool->list(0, ProcessDirectory, &cl, NULL, true);
    if (ok != VestaSource::ok)
      throw (SRPC::failure(ok,
			   Text("problem listing volatile directory: ") +
			   ReposUI::errorCodeText(ok)));
    ok = rootForTool->makeFilesImmutable(args.fp_content->num);
    if (ok != VestaSource::ok)
      throw (SRPC::failure(ok,
			   Text("problem with making files immutable for volatile directory: ") +
			   ReposUI::errorCodeText(ok)));
    bool new_stuff_added = AddToNewStuff(&cl, r.dumped_core, label, args.fp_content->num);
    assert(new_stuff_added);
  } catch (SRPC::failure f) {
    Error(cio().start_err(), 
	  Text("RunTool error (") + IntToText(f.r) + "): " + f.msg + ".\n");
    cio().end_err();
    return NEW(ErrorVC);
  }
  if (r.dumped_core && !cl.found_core) {
    cio().start_err() << label 
		      << "Warning: tool dumped core but couldn't find the file." << endl;
    cio().end_err();
  }

  if (r.sigval && !signal_cache) {
    if (stopOnError) {
      Error(cio().start_err(), Text("RunTool error: tool terminated with signal ") +
	    IntToText(r.sigval) + "(" + getSigMsg(r.sigval) + ").\n");
      cio().end_err();
      throw(Evaluator::failure(Text("exiting"), false));
    }
  }
  else if (r.status && !status_cache) {
    if (stopOnError) {
      Error(cio().start_err(),
	    Text("RunTool error: tool terminated with non-zero status (") +
	    IntToText(r.status) + ").\n");
      cio().end_err();
      throw(Evaluator::failure(Text("exiting"), false));
    }
  }

  // Collect the result:
  Context assocs;
  assocs.Push(NEW_CONSTR(AssocVC, (rootName, cl.newstuff)));
  if (stderr_treatment == "value" || stderr_treatment == "report_value") {
    VestaSource::errorCode mfi_err =
      err_file->makeFilesImmutable(args.fp_content->num);
    assert(mfi_err == VestaSource::ok);
    err_file->resync();
    assert(err_file->type == VestaSource::immutableFile);
    assert(err_file->shortId() != NullShortId);
    assocs.Push(NEW_CONSTR(AssocVC, (stdeName, 
      NEW_CONSTR(TextVC, (Text("stderr"), err_file->shortId(), args.fp_content->num)))));
    delete err_file;
    err_file = NULL;
  }
  if (stdout_treatment == "value" || stdout_treatment == "report_value") {
    VestaSource::errorCode mfi_err =
      out_file->makeFilesImmutable(args.fp_content->num);
    assert(mfi_err == VestaSource::ok);
    out_file->resync();
    assert(out_file->type == VestaSource::immutableFile);
    assert(out_file->shortId() != NullShortId);
    assocs.Push(NEW_CONSTR(AssocVC, (stdoName,
      NEW_CONSTR(TextVC, (Text("stdout"), out_file->shortId(), args.fp_content->num)))));
    delete out_file;
    out_file = NULL;
  }
  assocs.Push(NEW_CONSTR(AssocVC, 
			 (stewName, NEW_CONSTR(BooleanVC, (r.stderr_written)))));
  assocs.Push(NEW_CONSTR(AssocVC, 
			 (stowName, NEW_CONSTR(BooleanVC, (r.stdout_written)))));
  assocs.Push(NEW_CONSTR(AssocVC, 
			 (sigName, NEW_CONSTR(IntegerVC, (r.sigval)))));
  assocs.Push(NEW_CONSTR(AssocVC, 
			 (codeName, NEW_CONSTR(IntegerVC, (r.status)))));
  assocs.Push(NEW_CONSTR(AssocVC, 
			 (coreName, NEW_CONSTR(BooleanVC, (r.dumped_core)))));

  Val result = NEW_CONSTR(BindingVC, (assocs));

  // Set the cacheit bit:
  result->cacheit = ((!r.sigval || signal_cache) &&
		     (!r.status || status_cache) &&
		     (!r.stdout_written || stdout_cache) &&
		     (!r.stderr_written || stderr_cache) &&
		     (!(pauseAfterToolSignal && r.sigval)) &&
		     (!(pauseAfterToolError && r.status)) &&
		     (!pauseBeforeTool) &&
		     (!pauseAfterTool) &&
		     // It would be very strange for a process to dump
		     // core w/o getting a signal.  If that happens,
		     // we trat it like a signal for caching purposes.
		     (!r.dumped_core || signal_cache));


  // if cacheit bit is set to false 
  // set cacheit=false on all sub-values of the result
  if(!result->cacheit) clearCacheitBit(result);

  // remove information about this volatile directory.  (Note: there's
  // a minor race condition here as a ToolDirectoryServer thread could
  // already have a pointer to dirInfos and be using it past the point
  // we delete it from the table.)
  runToolCalls.Delete(rootHandle>>20, dirInfos);

  // For any directory which was looked up itself but for which none
  // of its children were looked up, add a type dependency on the
  // directory being a binding.
  DirInfoIter it(dirInfos->dirInfoTbl);
  WordKey unused;
  DirInfo *dirInfo;
  while(it.Next(unused, dirInfo))
    if(!dirInfo->child_lookup && !dirInfo->coarseDep)
      dirInfos->dep->Add(&(dirInfo->path), valTBinding, TypePK);

  // Set the dependency:
  result->dps = dirInfos->dep;

  // return result
  return result;
}

void DeleteRootForTool(VestaSource* rootForTool) {
  VestaSource::errorCode err =
    VDirSurrogate::deleteVolatileDirectory(rootForTool);
  if (err != VestaSource::ok)
    throw (SRPC::failure(err, 
			 Text("problem deleting volatile directory for runtool: ") +
			 ReposUI::errorCodeText(err)));
}

void CreateRootForDeriveds() {
  create_tool_directory_server();
  // Note that we don't use read-only-existing.  It would provide no
  // advantage for this directory, but it would prevent
  // makeFilesImmutable working on individual files in the directory,
  // and we need to do that to make individual derived files
  // immutable.
  VestaSource::errorCode err =
    VDirSurrogate::createVolatileDirectory(SRPC::this_host().chars(),
					   toolDirServerIntfName.chars(),
					   GetNewHandle(), rootForDeriveds,
					   0, false);
  if (err != VestaSource::ok) {
    throw (SRPC::failure(err,
			 Text("problem creating volatile directory: ") +
			 ReposUI::errorCodeText(err)));
  }
}

void DeleteRootForDeriveds() {
  VestaSource::errorCode err = VDirSurrogate::deleteVolatileDirectory(rootForDeriveds);
  if (err != VestaSource::ok) {
    throw (SRPC::failure(err,
			 Text("problem deleting volatile directory: ") +
			 ReposUI::errorCodeText(err)));
  }
}

VestaSource* CreateDerived() {
  VestaSource* newvs = NULL;
  static Basics::mutex cdmu;
  static int cdcnt = 0;
  char arc[32];
  cdmu.lock();
  sprintf(arc, "%d", cdcnt++);
  cdmu.unlock();
  VestaSource::errorCode err =
    rootForDeriveds->insertMutableFile(arc, NullShortId, true,
				       NULL,VestaSource::replaceDiff, &newvs);
  if (err != VestaSource::ok) {
    throw (SRPC::failure(err, 
			 Text("problem creating derived: ") +
			 ReposUI::errorCodeText(err)));
  }
  ShortId sid = newvs->shortId();
  if (sid == NullShortId) {
    Error(cio().start_err(), Text("Failed to create derived"));
    cio().end_err();
    throw(Evaluator::failure(Text("exiting"), false));
  }
  return newvs;
}

extern "C" void PrimRunToolInit_inner() {
  rootPath.Extend(Text("root"));
  RunToolHostInit();

  // Should we make a configuration setting for the default
  // directories to record as coarse dependencies?

  // For now I've just used three fixed paths: /tmp, /var/tmp, and
  // /usr/tmp
  {
    // [ tmp = TRUE ]
    Context tmp_assocs;
    tmp_assocs.Push(NEW_CONSTR(AssocVC, ("tmp", valTrue)));
    BindingVC *tmp_binding = NEW_CONSTR(BindingVC, (tmp_assocs));

    // [ tmp = TRUE, var/tmp = TRUE, usr/tmp = TRUE ]
    Context coarse_assocs;
    coarse_assocs.Push(NEW_CONSTR(AssocVC, ("tmp", valTrue)));
    coarse_assocs.Push(NEW_CONSTR(AssocVC, ("var", tmp_binding)));
    coarse_assocs.Push(NEW_CONSTR(AssocVC, ("usr", tmp_binding)));

    // [ coarse = [ tmp = TRUE, var/tmp = TRUE, usr/tmp = TRUE ] ]
    Context assocs;
    assocs.Push(NEW_CONSTR(AssocVC, ("coarse", NEW_CONSTR(BindingVC, (coarse_assocs)))));

    default_dep_control = NEW_CONSTR(BindingVC, (assocs));
  }
}

void PrimRunToolInit()
{
  static pthread_once_t init_once = PTHREAD_ONCE_INIT;
  pthread_once(&init_once, PrimRunToolInit_inner);
}
