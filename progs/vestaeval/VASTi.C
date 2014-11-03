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

/* File: VASTi.C                                               */

#include <dirent.h>
#include <time.h>
extern "C" {
    // add declaration not included by broken header files
    struct tm *_Plocaltime_r(const time_t *timer, struct tm *result);
}
#include <Basics.H>
#include <VestaConfig.H>
#include <SourceOrDerived.H>
#include <ParCacheC.H>
#include <Timer.H>
#include <ReposUI.H>
#include "VASTi.H"
#include "Lex.H"
#include "Parser.H"
#include "Expr.H"
#include "Val.H"
#include "PrimRunTool.H"
#include "ToolDirectoryServer.H"
#include "Location.H"
#include "Err.H"
#include "Files.H"
#include "ApplyCache.H"
#include "ThreadData.H"

using std::ios;
using std::ostream;
using std::fstream;
using std::endl;
using std::flush;
using OS::cio;

// On some platforms (at least Linux) errno.h doesn't define ESUCCESS
#if !defined(ESUCCESS)
#define ESUCCESS 0
#endif

static Text modelPath;
static mode_t newUMask = 022, oldUMask;

// In parallel build, we only want to print out one error stack.
// The variable callStackPrinted controls that. It is protected
// by callStackMu.
static bool callStackPrinted = false;

// Should we ship symlinks pointing into an alternate shortid
// directory?  If so, where?
static bool use_alternate_sid_root = false;
static Text alternate_sid_root;

void PrintCacheStat(ostream& vout) 
{
  vout << endl << "Caching Stats:" << endl
       << "    Cache: [" << "Server=" << *ParCacheC::Locate() << ", "
       << "MetaData=" << VestaConfig::get_Text("CacheServer", "MetaDataRoot")
       << "]" << endl
       << "    Function: [" << "Calls=" << appCallCounter << ", "
       << "Hits=" << appHitCounter << "]" << endl
       << "    Model: ["
       << "Calls=[" << "Special=" << sModelCallCounter << ", "
       << "Normal=" << nModelCallCounter << "], "
       << "Hits=[" << "Special=" << sModelHitCounter << ", "
       << "Normal=" << nModelHitCounter << "]" << "]" << endl
       << "    RunTool: [" << "Calls=" << toolCallCounter << ", "
       << "Hits=" << toolHitCounter << "]" << endl;
}

extern void PrintMemStat(ostream& vout);

// Print function trace if asked:
static void PrintFuncTrace() 
{
  if (recordTrace) {
    cio().start_out() << endl << "Function call graph:" << endl
		      << ThreadDataGet()->traceRes->str();
    cio().end_out();
  }
}

void PrintErrorStack(ostream& vout) 
{
  if (!recordCallStack) return;
  callStackMu.lock();
  if (callStackPrinted) {
    callStackMu.unlock();
    return;
  }
  callStackPrinted = true;

  ThreadData *thdata = ThreadDataGet();
  vout << endl << "Error stack trace:" << endl;
  int counter = 0;
  int st = 0;
  for (;;) {
    int sz = thdata->callStack->size();
    for (int i = st; i < sz; i++) {
      Expr expr = thdata->callStack->get(i);
      vout << counter++ << ". " << expr->loc->file << ": line "
	   << expr->loc->line << ", char " << expr->loc->character
	   << endl;
    }
    if (thdata->parent == NULL) break;
    st = thdata->parent->callStack->size() - thdata->parentCallStackSize;
    thdata = thdata->parent;
  }
  callStackMu.unlock();  
}

AssocVC* Lookup(Val val) {
  /* Get the component of the value specified by shipFromPath. */
  Val result = val;
  const char *path = shipFromPath.cchars();
  int len = shipFromPath.Length();

  Text arc;
  int i = 0;
  while (i < len) {
    int j = i;
    while (j < len && !IsDelimiter(path[j])) j++;
    arc = shipFromPath.Sub(i, j);
    if (result->vKind == BindingVK) {
      AssocVC *as = FindInContext(arc, ((BindingVC*)result)->elems);
      if (as == nullAssoc) {
	cio().start_err() << "Warning: the path `" << shipFromPath
			  << "' does not exist in the result value." << endl;
	cio().end_err();
	return as;
      }
      result = as->val;
      i = j + 1;
    }
    else {
      Error(cio().start_err(), 
	    Text("Failed to find the value to ship in ") + shipFromPath + ".\n");
      cio().end_err();
      return NULL;
    }
  }
  return NEW_CONSTR(AssocVC, (arc, result));
}

int Copy(const char *fromPath, const char *toPath) {
  /* Copy the file specified by the fromPath to the file specified
     by the toPath.  It returns the errno if fails. */
  const int BuffSz = 8192;
  char buf[BuffSz];
  int fromFd = open(fromPath, O_RDONLY);
  if (fromFd == -1) return errno;

  // Determine the mode we should use for the shipped copy.  Start
  // with rw priviledges.  (Note that these will be modified by
  // umask.)
  mode_t to_mode = (S_IRUSR | S_IWUSR |
		    S_IRGRP | S_IWGRP |
		    S_IROTH | S_IWOTH);
  // If the file we're copying has execute permission, then give
  // execute permission on the shipped copy.
  struct stat from_stat;
  if(fstat(fromFd, &from_stat) == 0)
    {
      if(from_stat.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
	{
	  to_mode |= S_IXUSR | S_IXGRP | S_IXOTH;
	}
    }
  else
    {
      // This shouldn't happen.  We opened it, so we should be able to
      // stat it.
      int err = errno;
      cio().start_err() << "Warning: couldn't stat file to be shipped (`" 
			<< fromPath << "'): " << strerror(err) << endl;
      cio().end_err();

    }

  remove(toPath);
  int toFd = creat(toPath, to_mode);
  if (toFd == -1) {
    close(fromFd);
    return errno;
  }

  int size;
  int eno = ESUCCESS;
  while (size = read(fromFd, buf, BuffSz)) {
    if (write(toFd, buf, size) == -1) {
      eno = errno;
      break;
    }
  }
  close(fromFd);
  close(toFd);
  return eno;
}

int CleanDir(Text path) {
  int err = 0;
  int entries = 0;

  // Check if .log exists
  struct stat statbuf;
  bool havelog = (stat(".log", &statbuf) == 0);

  DIR *dirPt = opendir(".");
  if (dirPt == NULL) {
    Error(cio().start_err(), 
	  Text("Failed to open the directory ") + path + ".\n");
    cio().end_err();
    return -1;
  }

  for (dirent *dp = readdir(dirPt); dp != NULL; dp = readdir(dirPt)) {
    if (strcmp(dp->d_name, ".") && strcmp(dp->d_name, "..") && 
	strcmp(dp->d_name, ".log")) {
      entries++;
      if (!forceClean && !havelog) continue;
      err = lstat(dp->d_name, &statbuf);
      if (S_ISDIR(statbuf.st_mode)) {
	// must be a directory. Remove it recursively.
	err = chdir(dp->d_name);
        if (err) return err;
	err = CleanDir(path + dp->d_name + "/");
        if (err) return err;
	err = chdir(path.cchars());
        if (err) return err;

	if(!hushedShipping) {
	  cio().start_out() << "Cleaning " << path << dp->d_name << endl;
	  cio().end_out();
	}
	err = rmdir(dp->d_name);
        if (err) return err;

      } else {
	// must be a nondirectory. So, remove it.
	if(!hushedShipping) {
	  cio().start_out() << "Cleaning " << path << dp->d_name << endl;
	  cio().end_out();
	}
	err = remove(dp->d_name);
        if (err) return err;
      }
    }
  }
  if (havelog) {
    err = remove(".log");
    if (err) return err;
  }
  (void)closedir(dirPt);

  if (!forceClean && !havelog && entries > 0) {
    Error(cio().start_err(), 
	  Text("Tried to clean a nonempty directory with no .log file: ")
	  + shipToPath + ".\n");
    cio().end_err();
    return -1;
  }

  return 0;
}

enum ShipKind { CopyShip, SymLinkShip };
const char *KindName[] = { "copy", "link" };



void LogShip(ShortId sid, const char *path,
  fstream &logFile, struct tm *shipTM, bool is_dir = false) throw ()
{
    char dateStr[20];
    sprintf(dateStr, "%02d:%02d:%02d %02d-%02d-%04d",
      shipTM->tm_hour, shipTM->tm_min, shipTM->tm_sec,
      (shipTM->tm_mon + 1), shipTM->tm_mday, (1900 + shipTM->tm_year));

    logFile << modelPath << ": " << path;
    if(is_dir) {
      logFile << " -> directory";
    }
    else {
      if (sid) {
	char sidHex[20];
	int err = sprintf(sidHex, "0x%08x", sid); assert(err >= 0);
	logFile << " -> " << sidHex;
      } else {
	logFile << " -> literal";
      }
    }
    
    logFile << ' ' << dateStr << endl;
    logFile.flush();
}

int ShipShortId(ShortId sid, const Text &name, ShipKind kind,
		fstream &logFile, struct tm *shipTM)
throw () {
  int err = ESUCCESS;
  if (sid != NullShortId) {
    if (!hushedShipping) {
      cio().start_out() << "Shipping " << name << "..." << endl;
      cio().end_out();
    }
    switch (kind) {
    case CopyShip:
      {
	char *path = SourceOrDerived::shortIdToName(sid, /*tailonly=*/ false);
	err = Copy(path, name.cchars());
      }
      break;
    case SymLinkShip:
      remove(name.cchars());
      {
	Text path;
	if(use_alternate_sid_root)
	  {
	    // Combine the alternate shortid root with the relative
	    // shortid path
	    path = alternate_sid_root;
	    path += SourceOrDerived::shortIdToName(sid, /*tailonly=*/ true);
	  }
	else
	  {
	    // Use the fully-qualified shortid path
	    path = SourceOrDerived::shortIdToName(sid, /*tailonly=*/ false);
	  }
	if (symlink(path.cchars(), name.cchars()) == -1) {
	  err = errno;
	}
      }
      break;
    default:
      assert(false);
    }
    if (err != ESUCCESS) {
      Error(cio().start_err(), 
	    Text("Failed to ") + KindName[(int)kind] + " the file " +  
	    name + ": " + strerror(err) + ".\n");
      cio().end_err();
    }
    else {
      LogShip(sid, name.cchars(), logFile, shipTM);
    }
  }
  return err;
}

int ShipText(const Text &txt, const Text &name,
	     fstream &logFile, struct tm *shipTM)
  throw () 
{
  if (!hushedShipping) {
    cio().start_out() << "Shipping " << name << "..." << endl;
    cio().end_out();
  }
  remove(name.cchars());
  fstream outFile;
  outFile.open(name.cchars(), ios::out);
  if (outFile.fail()) {
    Error(cio().start_err(), Text("Failed to create the file ") + name + 
	  ": " + strerror(errno) + ".\n");
    cio().end_err();
    return errno;
  }
  outFile << txt << flush;
  if (outFile.fail()) {
    Error(cio().start_err(), Text("Failed to write to the file ") + name +
	  ": " + strerror(errno) + ".\n");
    cio().end_err();
    return errno;
    }
  outFile.close();
  LogShip(0, name.cchars(), logFile, shipTM);
  return 0;
}

int ShipValue(Val value, ShipKind kind, struct tm *shipTM, Text path) throw () {
  fstream logFile;
  logFile.open(dotLogFiles?".log":"/dev/null",
	       ios::out | ios::app);
  if (logFile.fail()) {
    Error(cio().start_err(), 
	  Text("Failed to open .log file: ") + strerror(errno) + ".\n");
    cio().end_err();
    return -1;
  }
   
  /* Assume that the work dir is the place to ship. */
  if (value->vKind == BindingVK) {
    BindingVC *bv = (BindingVC*)value;
    Context work = bv->elems;
    while (!work.Null()) {
      Assoc elem = work.Pop();
      Val val = elem->val;
      // Skip anything with the name ".", "..", or ".log" as those
      // names cause problems
      if((elem->name == ".") || (elem->name == "..") ||
	 (elem->name == ".log"))
	{
	  Error(cio().start_err(),
		Text::printf("Can't ship reserved name \"%s\", skipping\n",
			     elem->name.cchars()));
	  cio().end_err();
	  continue;
	}
      ShortId sid;
      int err = 0;      
      switch (val->vKind) {
	case TextVK:
	  if (((TextVC*)val)->HasSid()) {
	    sid = ((TextVC*)val)->Sid();
	    err = ShipShortId(sid, elem->name, kind, logFile, shipTM);
	  }
	  else { 
            err = ShipText(((TextVC*)val)->NDS(), elem->name, logFile, shipTM);
	  }
	  if (err) return err;
	  break;
	case ModelVK:
	  sid = ((ModelVC*)val)->Sid();
	  err = ShipShortId(sid, elem->name, kind, logFile, shipTM);
	  if (err) return err;
	  break;
	case BindingVK:
	  {
	    if(!FS::IsDirectory(elem->name.cchars())) {
	      // directory does not exist; create it
	      err = mkdir(elem->name.cchars(), 0777);
	      if (err) {
		Error(cio().start_err(), Text("Failed to create the directory ") +
		      elem->name + ": " + strerror(errno) + ".\n");
		cio().end_err();
	      }
	    }
	    LogShip(0, elem->name.cchars(), logFile, shipTM, true);	      
	  }
	  if (err) return err;
	  
	  err = chdir(elem->name.cchars());
	  if (err) return err;
	  err = ShipValue(val, kind, shipTM, path + elem->name + "/");
	  if (err) return err;	  
	  err = chdir(path.cchars());
	  if (err) return err;
	  break;
	default:
	  // ignore other cases
	  break;
      }
    }
  }
  logFile.close();
  return 0;
}

bool VestaShip(AssocVC *namedVal) {
  if(shipBySymLink)
    {
      try
	{
	  use_alternate_sid_root =
	    VestaConfig::get("Evaluator", "symlink_sid_root",
			     alternate_sid_root);
	  if(use_alternate_sid_root &&
	     (alternate_sid_root[alternate_sid_root.Length()-1] != '/'))
	    // Make sure alternate_sid_root ends in a slash
	    alternate_sid_root += "/";
	}
      catch(VestaConfig::failure)
	{
	  // (This should really be impossible here as it would
	  // indicate a parsing error and we've already gotten other
	  // values from the config file by now.)
	  use_alternate_sid_root = false;
	}
    }

  /* Change to the directory at shipToPath, empty the directory, if requested,
     and then ship the result.  */
  int err = 0;
  const char *path = shipToPath.cchars();
  Val shipValue;
  Text log_dir = shipToPath;

  // dirPath is used for both - cleaning and shipping
  Text dirPath = shipToPath;
  if(dirPath[dirPath.Length()-1] != '/')
    dirPath += "/";

  err = chdir(path);
  if (!err) {
    if (shipClean || forceClean) {
      // Empty the directory
      if(hushedShipping) {
	cio().start_out() << "Cleaning..." << endl;
	cio().end_out();
      }
      err = CleanDir(dirPath);
      if (err) {
	Error(cio().start_err(), 
	      Text("Failed to delete the contents of the directory ") +
	      shipToPath + ".\n");
	cio().end_err();
	return false;
      }
    }
    if (namedVal->val->vKind == BindingVK) {
      shipValue = namedVal->val;
    }
    else {
      shipValue = NEW_CONSTR(BindingVC, (Context(namedVal)));
    }
  }
  else if ((errno == ENOTDIR || errno == ENOENT) &&
	   (namedVal->val->vKind == TextVK ||
	    namedVal->val->vKind == ModelVK)) {
    // Shipping to a file
    Text filename;
    try {
      FS::SplitPath(shipToPath, filename, log_dir);
    }
    catch(FS::Failure f) {
      	Error(cio().start_err(), 
	      Text("FS failure: ") + f.get_op() + ": " + f.get_errno() + ".\n");
	cio().end_err();
	return false;
    }
    err = chdir(log_dir.cchars());
    if(!err) {
      shipValue = 
	NEW_CONSTR(BindingVC, 
		   (Context(NEW_CONSTR(AssocVC, (filename, namedVal->val)))));
    }
  }

  if(err) {
    Error(cio().start_err(), 
	  Text("Failed to cd to the directory ") + log_dir + ": "
	  + strerror(errno) + ".\n");
    cio().end_err();
    return false;
  }

  // ship the requested value if no errors have occurred
  if (hushedShipping) {
    cio().start_out() << "Shipping..." << endl;
    cio().end_out();
  }
  time_t nowT;
  if (time(&nowT) == (time_t)(-1)) {
    Error(cio().start_err(), 
	  Text("Getting time of day from time(): ") + strerror(errno) + ".\n");
    cio().end_err();
    return false;
  }
  else {
    struct tm buffTM, *nowTM;
    nowTM = localtime_r(&nowT, &buffTM);
    ShipKind kind = (shipBySymLink ? SymLinkShip : CopyShip);
      
    /* We still need to add a file lock, since there can be multiple
       writers to the same log file. */
    err = ShipValue(shipValue, kind, nowTM, dirPath);
  }

  if (hushedShipping) {
    cio().start_out() << "Done!" << endl;
    cio().end_out();
  }
  return (err == 0);
}

bool Interpret(const Text& model) {
  Text prefix;
  VestaSource *vSource;
  fstream *iFile;
  VestaSource::errorCode err;
  Expr expr;
  Val result;
  bool success = true;

  uid_t euid = geteuid();
  if (seteuid(getuid()) < 0) {
    throw(Evaluator::failure(Text("Failed to switch to the real user-id.\n"),
			     false));
  }

  try {
    modelPath = ReposUI::canonicalize(model);
  }
  catch(ReposUI::failure f) {
    Error(cio().start_err(), Text("ReposUI failure: ") + f.msg + ".\n");
    cio().end_err();
    return false;
  }

  if (seteuid(euid) < 0) {
    throw(Evaluator::failure(
	     Text("Failed to switch back to the effective user-id.\n"),
	     false));      
  }
  if (!OpenSource(NULL, modelPath, noLoc, /*OUT*/ iFile, /*OUT*/ topModelRoot,
		  /*OUT*/ topModelSid, /*OUT*/ vSource)) {
    // failed to open the top-level model.
    success = false;
  }
  else {
    // Parsing the top-level model:
    if (iFile == NULL) {
      iFile = NEW(SourceOrDerived);
      ((SourceOrDerived*)iFile)->open(topModelSid);
      if (iFile->bad()) {
	throw(Evaluator::failure(Text("Can't open the model ") + modelPath +
				 "; giving up!\n",
				 false));
      }
    }
    try {
      expr = Parse(iFile, modelPath, topModelSid, topModelRoot);
    } catch (const char* report) {
      // ErrorDetail(report);    // Handle parsing error exception.
      success = false;
    }
    iFile->close();
  }
  if (!success) {
    Error(cio().start_err(), "Failed to parse `" + model + "'.\n\n");
    cio().end_err();
    return false;
  }
  if (parseOnly) {
    cio().start_out() << "Parsing completed." << endl;
    cio().end_out();
    return true;
  }

  // Evaluate:
  ModelVC *fun = NULL;
  try {
    ModelEC *modelExpr = (ModelEC*)expr;
    Context cc = ProcessModelHead(modelExpr);
    fun = NEW_CONSTR(ModelVC, (modelPath, topModelSid, topModelRoot, 
			       modelExpr, cc, vSource));
    ArgList args = NEW_CONSTR(ArgListEC, (0, modelExpr->loc));
    ApplyEC *ae = NEW_CONSTR(ApplyEC, (NEW_CONSTR(NameEC, (model, noLoc)), 
				       args, modelExpr->loc));
    if (recordCallStack) {
      ThreadDataGet()->callStack->addlo(modelExpr);
    }
    result = ApplyModel(fun, ae, conInitial);
  } catch (SRPC::failure f) {
    Error(cio().start_err(),
	  Text("SRPC failure (") + IntToText(f.r) + "): " + f.msg + ".\n");
    cio().end_err();
    success = false;
  } catch (Evaluator::failure f) {
    Error(cio().start_err(),
	  Text("Vesta evaluation failure; ") + f.msg + ".\n");
    cio().end_err();
    success = false;
  } catch (const char* report) {
    // ErrorDetail(report);    // Handle parsing error exception.
    success = false;
  } catch (ReposUI::failure f) {
    Error(cio().start_err(), Text("ReposUI failure: ") + f.msg + ".\n");
    cio().end_err();
    success = false;
  } catch (VestaConfig::failure f) {
    Error(cio().start_err(), Text("VestaConfig failure: ") + f.msg + ".\n");
    cio().end_err();
    success = false;
  }

  // Print evaluation result:
  if (success && printResult) {
    // When there is no exception, always print the evaluation result,
    // which may still contains error.
    ostream& out_stream = cio().start_out();
    out_stream << endl << "Return value of `" << model << "':" << endl;
    result->PrintD(out_stream);
    out_stream << endl;
    cio().end_out();
  }

  // Make sure we know if there was an error
  if (ErrorCount() > 0) success = false;

  // Print error stack, if the evaluation failed and the stack
  // has not already been printed.
  if (!success) {
    PrintErrorStack(cio().start_err());
    cio().end_err();
  }
      
  // Print function call trace:
  PrintFuncTrace();

  // Print cache stats & Checkpoint cache:
  if (cacheOption != 0) {
    if (printCacheStats) {
      PrintCacheStat(cio().start_out());
      cio().end_out();
    }
    // orphanCIs contains all the cache entries that have no parent 
    // in the cache.
    CacheEntry::IndicesApp* orphanCIs = ThreadDataGet()->orphanCIs;    
    if (fun != NULL && orphanCIs->len > 0)
      try {
	theCache->Checkpoint(topModelRoot->fptag, topModelSid, *orphanCIs, true);
      } catch (SRPC::failure f) {
	Error(cio().start_err(), 
	      Text("SRPC failure when checkpointing cache (")
	      + IntToText(f.r) + "): " + f.msg + ".\n");	
	cio().end_err();
	success = false;
      }
  }

  if (printMemStats) {
    PrintMemStat(cio().start_out());
    cio().end_out();
  }

  // Ship the value if the evaluation succeeded.
  // First follow the shipFromPath to get the component to be shipped
  // from the result, and ship it to the place specified by the shipToPath.
  if (success && !shipToPath.Empty()) {
    AssocVC *shipValue = Lookup(result);
    if (shipValue != NULL) {
      if (seteuid(getuid()) < 0) {
	Error(cio().start_err(), "Failed to switch to the real user-id.\n");
	cio().end_err();
        success = false;
      }
      else {
	umask(oldUMask);
	success = VestaShip(shipValue);
      }
    }
  }

  return success;
}

bool StartEval(const Text& filename) {
  // Initialization:
  ThreadDataInit();
  LexInit();
  ValInit();
  PrimInit();
  PrimRunToolInit();
  StartRenewLeaseThread();
  EvalTagsInit();

  // Make sure umask used when directly writing deriveds into the
  // repository is not too restrictive.  Use the same value that
  // the repository uses internally.
  try {
    newUMask = 022; // documented default
    newUMask = VestaConfig::get_int("Repository", "umask");
  } catch (VestaConfig::failure) {
    // ignore errors, use default
  }
  oldUMask = umask(newUMask);

  // Evaluation:
  return Interpret(filename);
}
