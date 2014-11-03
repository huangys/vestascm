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

// File: ToolDirectoryServer.C

#include <Basics.H>
#include <Atom.H>
#include <SRPC.H>
#include <LimService.H>
#include <VestaConfig.H>
#include "ToolDirectoryServer.H"
#include "EvalBasics.H"
#include "Val.H"
#include "Expr.H"
#include "Evaluator_Dir_SRPC.H"

using std::ostream;
using std::istream;
using std::endl;
using std::flush;
using Basics::OBufStream;
using OS::cio;

static Basics::mutex TDSMu;

static Text tmpName("tmp");

// size of ToolDirectoryServer thread stacks (in bytes).
const long CallbackStackSize = 0x20000;

// Used to record runtool volatile directories and dependency:
RunToolCalls runToolCalls(maxThreads);

static pthread_once_t init_block = PTHREAD_ONCE_INIT;

// Tool directory server request counter.
static Bit32 toolDirServerReqs = 0UL;

// Tool call counter.
static Bit64 toolCalls = 0UL;

Word GetNewHandle() {
  TDSMu.lock();
  toolDirServerReqs++;
  toolCalls++;
  Word newHandle = ((toolCalls<<20) |
		    (toolDirServerReqs & 0xFFFFF));
  TDSMu.unlock();
  return newHandle;
}

Word GetNewHandle(Word handle) {
  TDSMu.lock();
  toolDirServerReqs++;
  Word newHandle = ((handle & CONST_INT_64(0xFFFFFFFFFFF00000)) |
		    (toolDirServerReqs & 0xFFFFF));
  TDSMu.unlock();
  return newHandle;
}

// Marshal/unmarshal directory handles.
void send_dir(SRPC *srpc, Word dirHandle) {
  DirInfos *dirInfos = 0;
  bool inTbl = runToolCalls.Get(WordKey(dirHandle>>20), dirInfos); assert(inTbl);
  srpc->send_bytes((char*)&dirHandle, DIR_HANDLE_BYTES);
}

DirInfo *recv_dir(SRPC *srpc, /*OUT*/ Word& dirHandle, /*OUT*/ DirInfos*& dirInfos) {
  int len = DIR_HANDLE_BYTES;
  srpc->recv_bytes_here((char*)&dirHandle, len);
  if (len != DIR_HANDLE_BYTES) {
    srpc->send_failure(SRPC::invalid_parameter, "Invalid directory handle");
  }
  if (!runToolCalls.Get(WordKey(dirHandle>>20), dirInfos)) {
    srpc->send_failure(SRPC::invalid_parameter, "No runtool for this directory handle");
  }
  DirInfo *dirInfo;
  dirInfos->mu.lock();
  bool inTbl = dirInfos->dirInfoTbl->Get(WordKey(dirHandle), dirInfo);
  dirInfos->mu.unlock();
  if (!inTbl) {
    srpc->send_failure(SRPC::invalid_parameter, "No such directory handle");
  }
  return dirInfo;
}

void DirInfos::AddDpnd(DirInfo *dir, const Text& name, Val v, const PathKind pk) {
  if (dir->coarseDep) return;

  dir->child_lookup = true;
  
  DepPath newDepPath(dir->path.content);
  if (pk == BLenPK) {
    if(dir->namesRecorded) return;
    newDepPath.content->pKind = BLenPK;
    v = ((BindingVC*)v)->Names();
  }
  else {
    newDepPath.Extend(name, pk);
  }
  if (fsDeps) {
    ostream& err_stream = cio().start_err();
    err_stream << this->thread_label << "FS dependency: ";
    newDepPath.Print(err_stream);
    err_stream << endl;
    cio().end_err();
  }
  DepPathTbl::KVPairPtr pr;
  this->mu.lock();
  this->dep->Put(newDepPath, v, pr);
  dir->namesRecorded = (pk == BLenPK);
  this->mu.unlock();
  // assert(v->FingerPrint() == pr->val->FingerPrint());
}

static void sub_dep_sel(Binding parent_sel, const Text& name,
			Binding &child_sel, bool &child_is_selected,
			// The remaining arguments are used for
			// printing a warning message if we ignore
			// something due to an invalid type
			const char *control_name, const ArcSeq *path,
			const Atom &thread_label)
{
  Val sub_sel = ((parent_sel != 0)
		 ? parent_sel->LookupNoDpnd(name)
		 : 0);
  // No selector present: use default values (set by caller)
  if((sub_sel == valUnbnd) || (sub_sel == 0))
    return;

  // If it's a non-zero integer...
  if(((sub_sel->vKind == IntegerVK) &&
      ((IntegerVC *) sub_sel)->num) ||
     // ...or a true boolean...
     ((sub_sel->vKind == BooleanVK) &&
      ((BooleanVC *) sub_sel)->b))
    {
      // Then the child is selected
      child_is_selected = true;
      child_sel = 0;
    }
  // If it's a binding
  else if(sub_sel->vKind == BindingVK)
    {
      // Propagate the sub-binding to the sub-directory, perhaps
      // selecting something inside this directory
      child_is_selected = false;
      child_sel = (BindingVC *) sub_sel;
    }
  else
    {
      // Print a warning tell the user that we're ignoring this due to
      // the invalid type.
      assert(path != 0);
      OBufStream warn_msg;
      warn_msg << "ignoring ";
      // Note: we skip the first two elements of path which are always
      // "./root".
      for(unsigned int i = 2; i < path->size(); i++)
	{
	  if(i > 2) warn_msg << "/";
	  warn_msg << path->get_ref(i);
	}
      warn_msg << " in '" << control_name
	       << "' sub-binding of ./tool_dep_control: invalid type";
      ostream &err = cio().start_err();
      err << thread_label;
      Warning(err, warn_msg.str());
      cio().end_err();
    }
}

Word DirInfos::LookupDir(DirInfo *dir, const Text& name, Val v, Word dirHandle) {
  /* Assumption: the repository does not look up the same directory
     more than once during a runtool call.  We do not keep a record
     of the directories being visited, and create a new dir handle
     for every directory lookup request.   */
  DepPath newPath(dir->path.content);

  newPath.Extend(name);
  DirInfo *newDirInfo = NEW(DirInfo);
  newDirInfo->b = (BindingVC*)v;
  newDirInfo->path = newPath;
  newDirInfo->isRoot = false;

  // By default, inherit the enclosing directory's coarseDep
  // (e.g. if we're below a coarseDep directory)
  newDirInfo->coarseDep = dir->coarseDep;
  newDirInfo->coarseDep_sel = 0;

  // Maybe set coarseDep/coarseDep_sel based on the selector from
  // the parent.
  sub_dep_sel(dir->coarseDep_sel, name,
	      newDirInfo->coarseDep_sel,
	      newDirInfo->coarseDep,
	      "coarse", newPath.content->path, this->thread_label);

  // If the child is non-coarse, then the parent must be non-coarse
  assert(newDirInfo->coarseDep || !dir->coarseDep);

  // By default, inherit the enclosing directory's coarseNames
  newDirInfo->coarseNames = dir->coarseNames;
  newDirInfo->coarseNames_sel = 0;

  // Maybe set coarseNames/coarseNames_sel based on the selector
  // from the parent.
  sub_dep_sel(dir->coarseNames_sel, name,
	      newDirInfo->coarseNames_sel,
	      newDirInfo->coarseNames,
	      "coarse_names", newPath.content->path, this->thread_label);

  // We haven't yet recorded a dependency on the set of names in this
  // directory
  newDirInfo->namesRecorded = false;

  // We haven't yet performed a lookup in this directory, but we have
  // performed a lookup in the parent.
  newDirInfo->child_lookup = false;
  dir->child_lookup = true;

  this->mu.lock();

  if (newDirInfo->coarseDep && !dir->coarseDep) {
    // Make it depend on the entire directory content.
    DepPath newDepPath(newDirInfo->path.content);
    if (fsDeps) {
      ostream& err_stream = cio().start_err();
      err_stream << this->thread_label << "FS dependency: ";
      newDepPath.Print(err_stream);
      err_stream << endl;
      cio().end_err();
    }
    DepPathTbl::KVPairPtr pr;
    this->dep->Put(newDepPath, v, pr);

    // Though there hasn't been a child lookup, we don't need to
    // record a type dependency on this directory if nothing looks
    // inside it.
    newDirInfo->child_lookup = true;
  }
  
  // Get a new handle of this new directory:
  Word newDirHandle = GetNewHandle(dirHandle);
  this->dirInfoTbl->Put(WordKey(newDirHandle), newDirInfo);

  this->mu.unlock();

  return newDirHandle;
}

// Server procedure:
void Evaluator_Dir_Server_Inner(SRPC *srpc, int intfVersion, int procId)
  throw(SRPC::failure, Evaluator::failure)
{
  int index;
  Val v;
  DirInfo *dir;
  Word dirHandle;
  DirInfos *dirs;

  if (intfVersion != EVALUATOR_DIR_SRPC_VERSION) {
    srpc->send_failure(SRPC::version_skew, "Unrecognized intfVersion");
  }

  // Dispatch procedure:
  switch(procId) {
  case ed_lookup:
    {
      dir = recv_dir(srpc, dirHandle, dirs);
      // recv_chars() allocates the bytes on the heap. So, we do not
      // need to copy the bytes for id.
      Atom id(srpc->recv_chars(), (void*)1);
      srpc->recv_end();
      if (evalCalls) {
	ostream& err_stream = cio().start_err();
	err_stream << "EvalDir lookup: ";
	dir->path.Print(err_stream);
	err_stream << '/' << id << endl;
	cio().end_err();
      }
      v = dir->b->GetElemNoDpnd(id, index);
      if (v->vKind != ErrorVK) {
	switch (v->vKind) {
	case BindingVK:
	  srpc->send_int(ed_directory);
	  srpc->send_int(index);
	  send_dir(srpc, dirs->LookupDir(dir, id, v, dirHandle));
	  break;
	case TextVK:
	  dirs->AddDpnd(dir, id, v, NormPK);
	  srpc->send_int(ed_file);
	  srpc->send_int(index);
	  srpc->send_int(((TextVC*)v)->Sid());
	  ((TextVC*)v)->FingerPrint().Send(*srpc);
	  break;
	case ModelVK:
	  dirs->AddDpnd(dir, id, v, NormPK);
	  srpc->send_int(ed_file);
	  srpc->send_int(index);
	  srpc->send_int(((ModelVC*)v)->Sid());
	  ((ModelVC*)v)->FingerPrintFile().Send(*srpc);
	  break;
	case IntegerVK:
	  dirs->AddDpnd(dir, id, v, NormPK);
	  srpc->send_int(ed_device);
	  srpc->send_int(index);
	  srpc->send_int(((IntegerVC*)v)->num);
	  break;
	default:
	  srpc->send_failure(SRPC::invalid_parameter,
			     id + " is not a file or directory");
	}
      }
      else {
	if(dir->coarseNames)
	  {
	    // Record names coarsely for this directory as requested
	    dirs->AddDpnd(dir, emptyText, dir->b, BLenPK);
	  }
	else if(!dir->namesRecorded)
	  {
	    // As long as we haven't already recorded a dependency on
	    // all the names in this directory (e.g. by listing the
	    // directory), record a fine-grained non-existence
	    // dependency for this name
	    dirs->AddDpnd(dir, id, valFalse, BangPK);
	  }
	srpc->send_int(ed_none);
      }
      srpc->send_end();
      break;
    }
  case ed_lookup_index:
    {
      Text id;
      dir = recv_dir(srpc, dirHandle, dirs);
      index = srpc->recv_int();
      srpc->recv_end();
      if (evalCalls) {
	ostream& err_stream = cio().start_err();
	err_stream << "EvalDir index:  ";
	dir->path.Print(err_stream);
	err_stream << '/' << index << endl;
	cio().end_err();
      }
      v = dir->b->GetElemNoDpnd(index, id);
      if (v->vKind != ErrorVK) {
	switch (v->vKind) {
	case BindingVK:
	  srpc->send_int(ed_directory);
	  srpc->send_chars(id.chars());
	  send_dir(srpc, dirs->LookupDir(dir, id, v, dirHandle));
	  break;
	case TextVK:
	  dirs->AddDpnd(dir, id, v, NormPK);
	  srpc->send_int(ed_file);
	  srpc->send_chars(id.chars());
	  srpc->send_int(((TextVC*)v)->Sid());
	  ((TextVC*)v)->FingerPrint().Send(*srpc);
	  break;
	case ModelVK:
	  dirs->AddDpnd(dir, id, v, NormPK);
	  srpc->send_int(ed_file);
	  srpc->send_chars(id.chars());
	  srpc->send_int(((ModelVC*)v)->Sid());
	  ((ModelVC*)v)->FingerPrintFile().Send(*srpc);
	  break;
	case IntegerVK:
	  dirs->AddDpnd(dir, id, v, NormPK);
	  srpc->send_int(ed_device);
	  srpc->send_chars(id.chars());
	  srpc->send_int(((IntegerVC*)v)->num);
	  break;
	default:
	  srpc->send_failure(SRPC::invalid_parameter,
			     id + " is not a file or directory");
	}
      }
      else {
	dirs->AddDpnd(dir, id, valFalse, BangPK);
	srpc->send_int(ed_none);
      }
      srpc->send_end();
      break;
    }
  case ed_oldlist:
  case ed_list:
    {
      Text id;
      dir = recv_dir(srpc, dirHandle, dirs);
      index = srpc->recv_int();
      int limit = srpc->recv_int();
      int overhead = srpc->recv_int();
      srpc->recv_end();
      if (evalCalls) {
	ostream& err_stream = cio().start_err();
	err_stream << "EvalDir list:   ";
	dir->path.Print(err_stream);
	err_stream << '/' << index << endl;
	cio().end_err();
      }
      dirs->AddDpnd(dir, emptyText, dir->b, BLenPK);
      srpc->send_seq_start();
      while (true) {
	v = dir->b->GetElemNoDpnd(index, id);
	if (v->vKind == ErrorVK) {
	  // Send terminating entry.
	  srpc->send_chars("");
	  srpc->send_int(ed_none);
	  if (procId != ed_oldlist) {
	    srpc->send_int(NullShortId);
	  }
	  break;
	}
	// Directory entry found; check limit
	limit -= id.Length()+overhead;
	if (limit < 0) break;
	// Send this entry
	srpc->send_chars(id.chars());
	ed_entry_type etype =
	  (v->vKind == TextVK) ? ed_file :
          (v->vKind == BindingVK) ? ed_directory :
          (v->vKind == IntegerVK) ? ed_device : ed_none;
	if (etype == ed_none) 
	  srpc->send_failure(SRPC::invalid_parameter,
			     id + " is not a file or directory");
	srpc->send_int(etype);
	if (procId != ed_oldlist) {
	  if (etype == ed_file) {
	    srpc->send_int(((TextVC*)v)->Sid());
	  } else {
	    srpc->send_int(NullShortId);
	  }
	}
	index++;
      } // while(true)
      srpc->send_seq_end();
      srpc->send_end();
      break;
    }
  default:
    srpc->send_failure(SRPC::invalid_parameter, "Unrecognized procId");
    break;
  }  // switch(procId)
}

class Evaluator_Dir_Server_Handler : public LimService::Handler
{
public:
  Evaluator_Dir_Server_Handler() { }

  void call(SRPC *srpc, int intf_ver, int proc_id) throw(SRPC::failure);
  void call_failure(SRPC *srpc, const SRPC::failure &f) throw();
  void accept_failure(const SRPC::failure &f) throw();
  void other_failure(const char *msg) throw();
  void listener_terminated() throw();
};


void Evaluator_Dir_Server_Handler::call(SRPC *srpc, int intfVersion, int procId)
  throw(SRPC::failure)
  /* This is the server for the Evaluator_Dir_SRPC interface. It 
     expects to be invoked via the LimService interface, which
     establishes the RPC connection with the client. 'arg' is unused. */
{
  try {
    Evaluator_Dir_Server_Inner(srpc, intfVersion, procId);
  }
  catch (Evaluator::failure f) {
    cio().start_err() << "Evaluator failure occured in tool directory lookup." << endl;
    cio().end_err();
    exit(1);
  }
}

Text convert_failure(const SRPC::failure& f) {
  OBufStream s;
  s << "SRPC failure in ToolDirServer (";
  switch (f.r) {
  case SRPC::unknown_host:
    s << "unknown_host"; break;
  case SRPC::unknown_interface:
    s << "unknown_interface"; break;
  case SRPC::version_skew:
    s << "version_skew"; break;
  case SRPC::protocol_violation:
    s << "protocol_violation"; break;
  case SRPC::buffer_too_small:
    s << "buffer_too_small"; break;
  case SRPC::transport_failure:
    s << "transport_failure"; break;
  case SRPC::internal_trouble:
    s << "internal_trouble"; break;
  case SRPC::invalid_parameter:
    s << "invalid_parameter"; break;
  case SRPC::partner_went_away:
    s << "partner_went_away"; break;
  case SRPC::not_implemented:
    s << "not_implemented"; break;
  default:
    s << f.r; break;
  }
  s << "): " << f.msg.chars();
  Text t(s.str());
  return t;
}

void Evaluator_Dir_Server_Handler::call_failure(SRPC *srpc,
						const SRPC::failure& f)
  throw()
{
  // called when a failure occurs during server execution
  if (f.r == SRPC::partner_went_away) return; // normal occurrence
  cio().start_err() << convert_failure(f).chars() << " (continuing...)" << endl;
  cio().end_err();
}

void Evaluator_Dir_Server_Handler::accept_failure(const SRPC::failure& f)
  throw()
{
  // called when a failure occurs during server execution
  cio().start_err() << convert_failure(f).chars() << " (continuing...)" << endl;
  cio().end_err();
}

void Evaluator_Dir_Server_Handler::other_failure(const char *msg)
  throw()
{
  // called when a failure occurs during server execution
  cio().start_err() << msg << " (continuing...)" << endl;
  cio().end_err();
}

void Evaluator_Dir_Server_Handler::listener_terminated() throw()
{
  // called when a failure occurs during server execution
  cio().start_err()
    << ("Fatal ToolDirectoryServer error: unable to accept new "
	"connections; exiting") << endl;
  cio().end_err();
  abort();
}

extern "C"
{
  void init() {
    toolCalls = ((Bit64) time(NULL))<<20;
    try {
      // Get the maximum number of simultaneously serviced callbacks
      // from the repository
      int max_running = VestaConfig::get_int("Evaluator", "server_max_threads");

      Evaluator_Dir_Server_Handler *handler =
	NEW(Evaluator_Dir_Server_Handler);
      LimService *ls =
	NEW_CONSTR(LimService, (max_running,
				*handler, CallbackStackSize));
      toolDirServerIntfName = ls->IntfName();
      if (getenv("DEBUG_TOOL_DIR_SERVER")) {
	ostream* out_stream = NULL;
	istream* in_stream = NULL;
	cio().start_prompt(out_stream, in_stream);
	assert(out_stream != 0);
	assert(in_stream != 0);
	*out_stream << "Tool directory server listening on port "
		    << toolDirServerIntfName << endl
		    << "Hit enter to continue: " << flush;
	char enter;
	in_stream->get(enter);
	cio().end_prompt();
      }
      (void) ls->Forked_Run();
    } catch (VestaConfig::failure f) {
      cio().start_err() << endl
			<< "Configuration error starting tool directory server:"
			<< endl << f.msg << endl;
      cio().end_err();
      exit(1);
    } catch (SRPC::failure f) {
      cio().start_err() << endl
			<< "Error starting tool directory server:" << endl
			<< convert_failure(f) << endl;
      cio().end_err();
      exit(1);
    } catch(...) {
      cio().start_err() << endl
	   << "Unexpected exception starting tool directory server!" << endl;
      cio().end_err();
      exit(1);
    }
  }
}

void create_tool_directory_server() {
  pthread_once(&init_block, &init);
}
