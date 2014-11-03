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

/* File: Files.C                                               */

#include "Files.H"
#include "EvalBasics.H"
#include "ModelState.H"
#include "Err.H"
#include <ReposUI.H>
#include <Replicator.H>
#include <SplitText.H>

using std::fstream;
using std::ostream;
using std::endl;
using OS::cio;

extern "C" {void *opendir(const char *);}
extern "C" {int closedir(void *);}

const char UnixDelimiter = '/';

bool IsDelimiter(const char c) {
  return (c == '/' || c == '\\');
}

Text Canonical(const Text& path) {
  // Platform-dependent.  Take a Vesta path (with either kind of
  // delimiter) and convert to the appropriate delimiter for the
  // underlying file system.
  int len = path.Length();
  const char *p = path.cchars();
  char *s = NEW_PTRFREE_ARRAY(char, len+1);
  int i;

  for (i = 0; i < len; i++) {
    if (IsDelimiter(p[i])) s[i] = UnixDelimiter;
    else s[i] = p[i];
  }
  s[i] = 0;
  return Text(s);
}
  
Text setSuffix(const Text& s, const Text& suff) {
  bool sf = false;

  if (s == "/dev/null") return s;   // hack: Unix only
  for (int i = 0; i < s.Length(); i++) {
    if (s[i] == '.') sf = true;
    else if (IsDelimiter(s[i])) sf = false;
  }
  return (sf) ? s : s + suff;
}

bool IsAbsolutePath(const Text& path) {
  return !path.Empty() && IsDelimiter(path[0]);
}

void SplitPath(const Text& path, Text& prefix, Text& name) {
  int n = path.Length() - 1; 
  while (n >= 0 && !IsDelimiter(path[n])) n--;
  prefix = path.Sub(0, n+1);
  name = path.Sub(n+1);
  return;
}

void StripTrailingDelimiters(Text &path)
{
  int n = path.Length() - 1;
  if(IsDelimiter(path[n]))
    {
      while (n >= 0 && IsDelimiter(path[n])) n--;
      path = path.Sub(0, n+1);
    }
}

// Successively remove the trailing arc of partialPath and attempt to
// look the remainder below tempRoot.  Return the object found with
// the first successful such lookup.  If all arcs of partialPath are
// removed without any lookup succeeding, return 0.
VestaSource *LookupClosestEnclosing(VestaSource *tempRoot, Text partialPath)
{
  while(!partialPath.Empty())
    {
      Text head, tail;
      SplitPath(partialPath, head, tail);
      StripTrailingDelimiters(head);
      VestaSource *result;
      VestaSource::errorCode code = tempRoot->lookupPathname(head.cchars(), result);
      if(code == VestaSource::ok)
	{
	  return result;
	}
      partialPath = head;
    }
  return 0;
}

VestaSource::errorCode LookupPath(const Text& path, VestaSource *mRoot,
                                  VestaSource*& newRoot, 
				  const Text& replSuffix = "") {
  Text relPath;
  VestaSource *tempRoot;
  VestaSource::errorCode code = VestaSource::ok;

  if (IsAbsolutePath(path)) {
    int i = 1;
    while (!IsDelimiter(path[i]) && i < path.Length()) i++;
    relPath = path.Sub(i+1);  // strip off first arc
    tempRoot = rRoot;
  }
  else {
    relPath = path;
    tempRoot = mRoot;
  }
  if (relPath.Empty())
    newRoot = tempRoot;
  else {
    code = tempRoot->lookupPathname(relPath.cchars(), newRoot);
    // Should we try to replicate the object from a peer repository?
    bool shouldRepl = false;
    if(code == VestaSource::ok) {
      if(newRoot->type == VestaSource::ghost || 
	 (newRoot->type == VestaSource::stub && !newRoot->master))
	// We found the exact object, but it was a ghost or non-master stub.
	shouldRepl = true;
    } else if((code == VestaSource::notADirectory)
	      // Skip this extra work unless auto-replicaiton is
	      // enabled.
	      && autoRepl) {
      // Check to see if the closest enclosing directory is a ghost or
      // non-master stub.
      newRoot = LookupClosestEnclosing(tempRoot, relPath);
      if(newRoot != 0)
	{
	  shouldRepl = ((newRoot->type == VestaSource::ghost) || 
			((newRoot->type == VestaSource::stub) && !newRoot->master));
	  delete newRoot;
	}
    } else if((code == VestaSource::notFound)
	      // Skip this extra work unless auto-replicaiton is
	      // enabled.
	      && autoRepl) {
      // Check to see if the closest enclosing directory is a
      // non-master appendable directory.
      newRoot = LookupClosestEnclosing(tempRoot, relPath);
      if(newRoot != 0)
	{
	  shouldRepl = ((newRoot->type == VestaSource::appendableDirectory) &&
			!newRoot->master);
	  delete newRoot;
	}
      else
	{
	  shouldRepl = ((tempRoot->type == VestaSource::appendableDirectory) &&
			tempRoot->master);
	}
    }
    if(autoRepl && shouldRepl) {
      newRoot = ReplicateMissing(tempRoot, relPath, replSuffix);
      code = (newRoot != 0) ? VestaSource::ok : VestaSource::notFound;
    }
  }
  return code;
}

// Try a sequence of default model names in order.  Return the first
// one that exists.
Text FindExistingModel(const TextSeq &p_defaults, const Text& p_dir = "")
{
  Text l_result;
  // Try the default model names in sequence.
  for(unsigned int l_i = 0; l_i < p_defaults.size(); l_i++)
    {
      l_result = p_dir + p_defaults.get(l_i);
      // If this one exists, accept it.
      if(FS::Exists(l_result))
	{
	  break;
	}
    }
  return l_result;
}

Text MainModel(const Text& filename, const Text& defaultmain) {
  Text modelname(Canonical(filename));
  TextSeq l_defaults = Generics::split_Text_ctype(defaultmain, isspace);
  if(modelname.Length() == 0)
    {
      // No model was specified.  Chose a default in the current
      // working directory.
      modelname = FindExistingModel(l_defaults);
    }
  else
    {
      char c = modelname[modelname.Length()-1];
      if (IsDelimiter(c))
	{
	  // Assume it's a directory and append the default model
	  // name.
	  modelname = FindExistingModel(l_defaults, modelname);
	}
      else
	{
	  // See if this is a directory.
	  void *d = opendir(modelname.cchars());
	  if (d == NULL)
	    {
	      // Not a directory, assume it's a file.  Make sure it
	      // has the right suffix.
	      modelname = setSuffix(modelname, ".ves");
	    }
	  else
	    {
	      (void)closedir(d);
	      // A directory was specified, append the default model
	      // name.
	      modelname = FindExistingModel(l_defaults,
					    modelname + PathnameSep);
	    }
	}
    }
  return modelname;
}

bool IsDirectory(VestaSource *mRoot, Text& path, VestaSource*& vSource,
		 VestaSource::errorCode& vSourceErr) {
  Text prefix, tail;
  SplitPath(path, prefix, tail);
  VestaSource* vsParent = 0;
  vSourceErr = LookupPath(prefix, mRoot, vsParent, tail);
  if(vSourceErr == VestaSource::ok) {
    if(vsParent->type == VestaSource::immutableDirectory) {
      // if parent is an immutable directory the object should 
      // be found locally
      vSourceErr = vsParent->lookup(tail.cchars(), vSource);
    }
    else if(vsParent->type == VestaSource::appendableDirectory) { 
      vSourceErr = LookupPath(tail, vsParent, vSource);
    }
    else { // should not happen
      assert(vsParent->type == VestaSource::appendableDirectory);
    }
  }
  return (vSourceErr == VestaSource::ok &&
	  vSource->type != VestaSource::immutableFile &&
	  vSource->type != VestaSource::mutableFile);
}

bool OpenSource(VestaSource *mRoot, const Text& fname, SrcLoc *loc,
		fstream*& iFile, VestaSource*& newRoot, ShortId& shortId,
		VestaSource*& vSource) {
  Text prefix, tail;
  VestaSource::errorCode err;

  shortId = NullShortId;
  SplitPath(fname, prefix, tail);
  err = LookupPath(prefix, mRoot, newRoot, tail);
  if(err != VestaSource::ok) {
    Error(cio().start_err(), 
	  VestaSourceErrorMsg(err) + " opening directory `" + prefix + "'.\n",
	  loc);
    cio().end_err();
    return false;
  }
  err = newRoot->lookup(tail.cchars(), vSource);
  if (err != VestaSource::ok) {
    Error(cio().start_err(), 
	  VestaSourceErrorMsg(err) + " opening `" + fname + "'.\n", loc);
    cio().end_err();
    return false;
  }
  iFile = NULL;
  shortId = vSource->shortId();
  return true;
}

VestaSource *ReplicateMissing(VestaSource* root, const Text& name, 
			      const Text& replSuffix)
{
  VestaSource *vSource = 0;

  assert(autoRepl);
  assert(RootLongId.isAncestorOf(root->longid));
  
  try {
    Text path = ReposUI::vsToFilename(root) + "/" + name;
    Text replPath = path;
    if(!replSuffix.Empty()) {
      replPath += replSuffix;
    }
    const int vlen = 7; // ==strlen("/vesta/")

    cio().start_out() << "Trying to find a copy of " << replPath << endl; 
    cio().end_out();

    Text defhints;
    (void) VestaConfig::get("UserInterface", "DefaultHints", defhints);
    VestaSource* real_source;
    try
      {
	real_source = ReposUI::filenameToRealVS(replPath, defhints);
      }
    catch(ReposUI::failure f)
      {
	return 0;
      }

    cio().start_out() << "Replicating " << replPath << " from " 
		      << real_source->host() << ":" << real_source->port() 
		      << endl;
    cio().end_out();

    Replicator repl(real_source->host(), real_source->port(), 
		    rRoot->host(), rRoot->port());
    Replicator::Directive d('+', replPath.Sub(vlen));
    Replicator::DirectiveSeq direcs;
    direcs.addhi(d);
    Replicator::Flags flags = (Replicator::Flags) 
      (Replicator::attrNew | Replicator::attrOld | Replicator::attrAccess |
       Replicator::revive | Replicator::inclStubs | Replicator::latest);
    repl.replicate(&direcs, flags);
    vSource = ReposUI::filenameToVS(path, rRoot->host(), rRoot->port());
    delete real_source;
  }
  catch(ReposUI::failure f){
    cio().start_err() << "ReposUI failure during replication of " << name 
		      << ": " << f.msg << endl;
    cio().end_err();
    throw Evaluator::failure("ReposUI failure during replication", false);
  }
  catch(VestaConfig::failure f){
    cio().start_err() << "VestaConfig failure during replication of " << name 
		      << ": " << f.msg << endl;
    cio().end_err();
    throw Evaluator::failure("VestaConfig failure during replication", false);
  }
  catch(SRPC::failure f){
    cio().start_err() << "SRPC failure during replication of " << name << ": "
		      << f.msg << " (" << f.r << ")" << endl;
    cio().end_err();
    throw Evaluator::failure("SRPC failure during replication", false);
  }
  catch(Replicator::Failure f){
    if(f.code == (VestaSource::errorCode) -1) {
      cio().start_err() << "Failure during replication of " << name << ": " 
			<< f.msg << endl;
      cio().end_err();
    } 
    else {
      cio().start_err() << "Failure during replication of " << name << ": " 
			<< ReposUI::errorCodeText(f.code) << ", " << f.msg << endl;
      cio().end_err();
    }
    throw Evaluator::failure("Replication failure", false);
  }
  assert(vSource != 0);
  return vSource;
}
