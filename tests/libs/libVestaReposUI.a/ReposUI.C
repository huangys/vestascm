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

// 
// ReposUI.C
//

#include <Basics.H>
#include <Text.H>
#include <VestaSource.H>
#include <VDirSurrogate.H>
#include <sys/mount.h>
#include <dirent.h>
#include <fnmatch.h>
#include <algorithm>
#include <BufStream.H>
#include "ReposUI.H"
#include <VestaConfig.H>
#include <Units.H>
#include <signal.h>

using Basics::OBufStream;
using std::cin;
using std::cout;
using std::endl;
using std::flush;
using std::ofstream;
using std::ifstream;

static void
throw_errno(const Text& msg, int saved_errno)
{
  Text etxt = Basics::errno_Text(saved_errno);
  throw ReposUI::failure(msg + ": " + etxt, saved_errno);
}

static void
throw_errno(const Text& msg)
{
  throw_errno(msg, errno);
}

// Return true if name has "/", ".", or ".." as first component
bool
ReposUI::qualified(const Text& name) throw ()
{
    return (name[0] == '/') ||
      (name[0] == '.' && (name[1] == '\0' || name[1] == '/')) ||
      (name[0] == '.' && name[1] == '.' && (name[2]=='\0' || name[2]=='/'));
}

// If name is not qualified, prepend supplied root.
Text
ReposUI::qualify(const Text& name, const Text& root) throw ()
{
    if (qualified(name)) {
	return name;
    } else if (name == "") {
	return root;
    } else {
	return root + "/" + name;
    }
}

// Split name into parent name and final arc.
// name must not be empty or end with "/"
void
ReposUI::split(const Text& name, Text& par, Text& arc, const Text& errtext)
  throw (ReposUI::failure)
{
    int lastslash = name.FindCharR('/');
    if (lastslash == -1) {
	arc = name;
	par = "";
    } else {
	arc = name.Sub(lastslash + 1);
	par = name.Sub(0, lastslash);
	if (par == "") par = "/";
    }
    if (arc.Length() == 0) {
	throw ReposUI::failure(errtext +
			       " must not have empty final arc");
    }
    if (arc == "." || arc == "..") {
	throw ReposUI::failure(errtext +
			       " must not have . or .. as final arc");
    }
}

// If the given absolute name has prefix as its initial arc(s), return
// true and put the offset the offset to the remainder of the name in tail;
// else return false.
int
stripPrefix(Text name, Text prefix, /*OUT*/int& tail)
{
  int plen = prefix.Length();
  if (prefix == name.Sub(0, plen)) {
    if (name.Length() == plen) {
      tail = plen;
      return true;
    }
    if (name[plen] == '/') {
      tail = plen + 1;
      return true;
    }
  }
  return false;
}

static Text
precanonicalize(const Text& name, /*OUT*/ReposUI::RootLoc& rootloc)
     throw (VestaConfig::failure, ReposUI::failure)
{
  // Get the absolute filesystem path for name.  This resolves
  // any symbolic links or special names like "." or ".."
  Text cname;
  try {
    cname = FS::Realpath(name, false);
  } catch (FS::Failure f) {
    Text msg = Text::printf("%s failed: %s", f.get_op().cchars(), 
			    f.get_arg().cchars());
    throw_errno(msg, f.get_errno());
  } catch (FS::DoesNotExist) {
    throw ReposUI::failure(name + Text(" does not exist"));
  }

  // 3) If the first component of the name is (now) "/vesta" or
  // [UserInterface]AppendableRootName, skip this component and 
  // interpret the rest relative to the appendable root;
  // or similarly for "/vesta-work", [UserInterface]MutableRootName,
  // and the mutable root.  Otherwise, leave the name unchanged
  // and flag an error.
  cname = ReposUI::stripRoot(cname, /*OUT*/rootloc);
  return cname;
}

Text
ReposUI::canonicalize(const Text& name)
     throw (VestaConfig::failure, ReposUI::failure)
{
  ReposUI::RootLoc rootloc;
  Text cname = precanonicalize(name, /*OUT*/rootloc);
  Text root;
  if (rootloc == ReposUI::VESTA) {
    root = "/vesta";
  } else if (rootloc == ReposUI::VESTAWORK) {
    root = "/vesta-work";
  } else {
    throw ReposUI::failure(name + " is not in the Vesta repository");
  }
  return root + (cname.Empty() ? "" : "/") + cname;
}

Text
ReposUI::canonicalize(const Text& name, const Text& qual)
     throw (VestaConfig::failure, ReposUI::failure)
{
  ReposUI::RootLoc rootloc;
  Text cname = precanonicalize(qualify(name, qual), /*OUT*/rootloc);
  Text root;
  if (rootloc == ReposUI::VESTA) {
    root = "/vesta";
  } else if (rootloc == ReposUI::VESTAWORK) {
    root = "/vesta-work";
  } else {
    throw ReposUI::failure(name + " is not in the Vesta repository");
  }
  return root + (cname.Empty() ? "" : "/") + cname;
}

Text ReposUI::stripRoot(const Text& name, /*OUT*/RootLoc& rootloc) 
  throw (VestaConfig::failure)
{
  int tail = 0;
  if (stripPrefix(name, "/vesta", /*OUT*/tail) ||
      stripPrefix(name, VestaConfig::get_Text("UserInterface",
					      "AppendableRootName"),
		  /*OUT*/tail)) {    
    rootloc = ReposUI::VESTA;
  } 
  else if (stripPrefix(name, "/vesta-work", /*OUT*/tail) ||
	   stripPrefix(name, VestaConfig::get_Text("UserInterface",
						   "MutableRootName"),
		       /*OUT*/tail)) {
    rootloc = ReposUI::VESTAWORK;
  } 
  else {
    rootloc = ReposUI::NOWHERE;
  }
  return name.Sub(tail);
}


Text ReposUI::stripSpecificRoot(const Text& name, RootLoc which_root, 
				bool require_canonical)
  throw (ReposUI::failure, VestaConfig::failure)
{
  int tail = 0;
  bool success = false;
  switch(which_root) {
  case ReposUI::VESTA:
    success = stripPrefix(name, "/vesta", /*OUT*/tail);
    if(!success && !require_canonical) {
      success = stripPrefix(name,
			    VestaConfig::get_Text("UserInterface",
						  "AppendableRootName"),
			    /*OUT*/tail);
    }
    if(!success)
      throw ReposUI::failure(name +
		     " is not in the appendable portion of the Vesta repository");
    break;
  case ReposUI::VESTAWORK:
    success = stripPrefix(name, "/vesta-work", /*OUT*/tail);
    if(!success && !require_canonical) {
      success = stripPrefix(name,
			    VestaConfig::get_Text("UserInterface",
						  "MutableRootName"),
			    /*OUT*/tail);
    }
    if(!success)
      throw ReposUI::failure(name +
		     " is not in the mutable portion of the Vesta repository");
    break;
  default: 
    throw ReposUI::failure("stripSpecificRoot: Invalid parameter");
  }
  return name.Sub(tail);
}

VestaSource* 
ReposUI::lookupCreatePath(Text root_name, 
			  Text pathname, Text hints)
  throw(ReposUI::failure, VestaConfig::failure, SRPC::failure)
{
  int len = pathname.Length();
  VestaSource* vs_next;
  Text current_path = root_name;

  VestaSource* vs_current = ReposUI::filenameToMasterVS(root_name, hints);

  while(!pathname.Empty()) {
    int separator = pathname.FindChar('/');
    // find out the directory name that should be looked up in current vs 
    // and be built in a case it is not there.  
    Text dir_name;
    if(separator > 0 && separator < len) {
      dir_name = pathname.Sub(0, separator);
      pathname = pathname.Sub(separator + 1);
    }
    else if(separator < 0) {
      dir_name = pathname;
      pathname = "";
    }
    else {
      assert(separator == 0);
      pathname = pathname.Sub(separator + 1);
      continue;
    }
    Arc arc = dir_name.cchars();
    // look up the directory name
    VestaSource::errorCode lookup_err = vs_current->lookup(arc, vs_next);
    if(lookup_err != VestaSource::ok) {
      if(lookup_err == VestaSource::notFound) {
	// insert the directory  
	VestaSource::errorCode create_err = 
	  vs_current->insertAppendableDirectory(arc, /*master*/true,
						/*who=*/NULL,
						/*chk=*/VestaSource::dontReplace,
						/*newvs=*/&vs_next);
	if(create_err != VestaSource::ok) {
	  throw ReposUI::failure(current_path + "/" + arc + ": " + 
				 ReposUI::errorCodeText(create_err), 
				 -1, create_err);
	}
      }
      else {
	throw ReposUI::failure(current_path + "/" + arc + ": " + 
			       ReposUI::errorCodeText(lookup_err), 
			       -1, lookup_err);
      }
    }
    else {
      // vs_next has to be a master for performing next step lookup,
      // creation a new directory (in a master object) if needed, 
      // or as a returning result. 
      if(!vs_next->master) {
	delete vs_next;
	try {
	  vs_next = ReposUI::filenameToMasterVS(current_path, hints);
	} catch(...) {
	  // Don't leak vs_current if we can't find the master copy of
	  // this directory.
	  delete vs_current;
	  throw;
	}
      }
    }
    // free memory allocated by current VS pointer and set it to point 
    // to the new VS
    delete vs_current;
    vs_current = vs_next;
    current_path = current_path + "/" + arc;  
  }
 
  return vs_current;
}


VestaSource*
ReposUI::filenameToVS(Text name, Text host, Text port)
  throw (ReposUI::failure, VestaConfig::failure, SRPC::failure)
{
  ReposUI::RootLoc rootloc;
  Text cname = precanonicalize(name, /*OUT*/rootloc);
  VestaSource* root = NULL;
  bool needFree = false;

  try {
    if (rootloc == ReposUI::VESTA) {
      if (host.Empty()) {
	root = VestaSource::repositoryRoot();
      } else {
	root = VDirSurrogate::repositoryRoot(host, port);
	needFree = true;
      }
    } else if (rootloc == ReposUI::VESTAWORK) {
      if (host.Empty()) {
	root = VestaSource::mutableRoot();
      } else {
	root = VDirSurrogate::mutableRoot(host, port);
	needFree = true;
      }
    } else {
      throw ReposUI::failure(name + " is not in the Vesta repository");
    }

    VestaSource *vs;
    VestaSource::errorCode err =
      root->lookupPathname(cname.cchars(), vs);
    if (needFree) delete root;
    if (err != VestaSource::ok) {
      throw ReposUI::failure(name + ": " + ReposUI::errorCodeText(err),
			     -1, err);
    }
    return vs;
  } catch (SRPC::failure f) {
    if (needFree && root) delete root;
    throw ReposUI::failure(name + ": SRPC failure; " + f.msg);
  }
}

VestaSource*
ReposUI::filenameToVS(Text name)
  throw (ReposUI::failure, VestaConfig::failure, SRPC::failure)
{
  return filenameToVS(name, "", "");
}

VestaSource*
ReposUI::filenameToVS(Text name, Text hostport)
  throw (ReposUI::failure, VestaConfig::failure, SRPC::failure)
{
  Text host, port;
  if (hostport.Empty()) {
    host = port = "";
  } else {
    int colon = hostport.FindChar(':');
    if (colon == -1) {
      host = hostport;
      port = VestaConfig::get_Text("Repository", "VestaSourceSRPC_port");
    } else {
      host = hostport.Sub(0, colon);
      port = hostport.Sub(colon+1);
    }
  }
  return filenameToVS(name, host, port);
}



bool getMasterHintsCallback(void* closure, const char* value) 
{
  if(closure != 0) {
    Text* hints = (Text*) closure;
    *hints = *hints + " " + Text(value);
  }
  return true;
}


// Common routine for filenameToMasterVS and filenameToRealVS.
// Algorithm outline:
// 1) Canonicalize name
// 2) repos = local repository
// 3) Look up name in repos
// 4) If found and result matches criterion, return it
// 5) Traverse from this name upward until root and add all 
//    master-repository values to hints 
// 6) take the next hint from "hints" that has not already been used.
// 7) If none found, fail.
// 8) repos = hint
// 9) Go to 3.

static VestaSource*
filenameToSpecialVS(Text name, Text hints, bool needMaster)
     throw (ReposUI::failure, VestaConfig::failure, SRPC::failure)
{
  Table<Text, Text>::Default tried;
  Text reposhost, reposport, defaultport;
  enum { NOTHING, NAME, COPY } found = NOTHING;
  
  // 1) Canonicalize name
  ReposUI::RootLoc rootloc;
  Text cname = precanonicalize(name, /*OUT*/rootloc);
  if (rootloc != ReposUI::VESTA) {
    throw ReposUI::failure(name + " is not under /vesta");
  }

  // 2) repos = local repository
  reposhost = VDirSurrogate::defaultHost();
  reposport = defaultport = VDirSurrogate::defaultPort();
 

  for (;;) {
    // 3) Look up name in repos
    tried.Put(reposhost + ":" + reposport, "");

    VestaSource* vs = NULL;
    VestaSource* root = NULL;
    VestaSource::errorCode err = VestaSource::rpcFailure;

    try {
      root = VDirSurrogate::repositoryRoot(reposhost, reposport);
      err = root->lookupPathname(cname.cchars(), vs);
    } catch (SRPC::failure f) {
      // fall through to try another hint
      tried.Put(reposhost + ":" + reposport, f.msg);
    }
    
    // 4) If found and result matches criterion, return it
    if (err == VestaSource::ok) {
      if (vs->master) {
	      delete root;
	      return vs;
      }
      tried.Put(reposhost + ":" + reposport, "Not master");
      if (vs->type != VestaSource::stub &&
	  vs->type != VestaSource::ghost) {
	if (!needMaster) {
		delete root;
		return vs;
	}
	found = COPY;
      } else {
	if (found < NAME) found = NAME;
      }
    }
    else {
      Text reason;
      bool inTable = tried.Get(reposhost + ":" + reposport, reason);
      if(!inTable || (inTable && reason == ""))
	tried.Put(reposhost + ":" + reposport, ReposUI::errorCodeText(err));
    }
    
    // 5) Traverse from this name upward until root and add all 
    //    master-repository values to hints
    Text head = cname;
    reposhost = reposport = "";
    if (root) {
      Text attrib_hints;
      for (;;) {
	if (vs) {
	  vs->getAttrib("master-repository", 
			getMasterHintsCallback, (void*) &attrib_hints);
	  delete vs;
	  vs = NULL;
	}
	int slash = head.FindCharR('/');
	if (slash == -1) break;
	head = head.Sub(0, slash);
	err = root->lookupPathname(head.cchars(), vs);
      }
      if (needMaster)
	{
	  // Put any hints from "master-repository" attributes ahead of
	  // the other hints.  Though the "master-repository" attribute
	  // isn't authoritative, in many cases it's correct.  The other
	  // hints should be used as a fall-back after we've tried any
	  // repositories listed in "master-repository" attributes.
	  hints = attrib_hints + " " + hints;
	}
      else
	{
	  // If we're not looking for a master copy specifically,
	  // there's no reason to prioritize the "master-repository"
	  // attribute hints.  In fact, other hints might be for
	  // repositories that are closer and have a copy of the
	  // object.
	  hints = hints + " " + attrib_hints;
	}
      delete root;
    }

    // 6) take the next hint from "hints" that has not already been used.
    for (;;) {
      int i = 0;
      while (hints[i] == ',' || hints[i] == ' ') i++;
      if (hints[i] == '\000') break;
      int j = i;
      while (hints[j] && hints[j] != ',' && hints[j] != ' ') j++;
      Text mht = hints.Sub(i, j - i);
      hints = hints.Sub(j);
      Text dummy;
      if (!tried.Get(mht, dummy)) {
	// Found a new hint
	int colon = mht.FindCharR(':');
	if(colon > 0) {
	  reposhost = mht.Sub(0, colon);
	  reposport = mht.Sub(colon+1);
	}
	else {
	  reposhost = mht;
	  reposport = defaultport;
	}
	break;
      }
    }

    // 7) If none found, fail.
    if (reposhost.Empty()) {
      Text msg;
      switch (found) {
      case NOTHING:
	msg = "can't find ";
	break;
      case NAME:
	msg = "can't find a replica of ";
	break;
      case COPY:
	msg = "can't find master replica of ";
	break;
      }
      msg = msg + name + " (tried:";
      Table<Text, Text>::Iterator iter(&tried);
      Text repname;
      Text repreason;
      while (iter.Next(repname, repreason)) {
	msg = msg + " " + repname;
	if(needMaster && repreason != "") {
	  msg = msg + " [" + repreason  + "]";
	}
      }
      throw ReposUI::failure(msg + ")");
    }
    // 8) repos = hint
    // 9) Go to 3.
  }
}

VestaSource*
ReposUI::filenameToMasterVS(Text name, Text hints)
     throw (ReposUI::failure, VestaConfig::failure, SRPC::failure)
{
  return filenameToSpecialVS(name, hints, true);
}

VestaSource*
ReposUI::filenameToRealVS(Text name, Text hints)
     throw (ReposUI::failure, VestaConfig::failure, SRPC::failure)
{
  return filenameToSpecialVS(name, hints, false);
}

Text
ReposUI::vsToFilename(VestaSource* vs)
  throw (ReposUI::failure, VestaConfig::failure, SRPC::failure)
{
    Text ret = "";
    LongId longid_par;
    unsigned int index;
    VestaSource* vs_cur = vs;
    VestaSource* vs_par;
    VestaSource* vs_junk;
    VestaSource::errorCode err;
    char arcbuf[MAX_ARC_LEN+1];
    for (;;) {
	if (vs_cur->longid == RootLongId) {
	    ret = "/vesta/" + ret;
	    break;
	} else if (vs_cur->longid == MutableRootLongId) {
	    ret = "/vesta-work/" + ret;
	    break;
	}
	longid_par = vs_cur->longid.getParent(&index);
	if (longid_par == NullLongId) {
	    throw ReposUI::failure("cannot find filename for given longid");
	}
	vs_par = VDirSurrogate::
	  LongIdLookup(longid_par, vs_cur->host(), vs_cur->port());
	assert(vs_par != NULL);
	err = vs_par->lookupIndex(index, vs_junk, arcbuf);
	assert(err == VestaSource::ok);
	delete vs_junk;
	ret = Text(arcbuf) + "/" + ret;
	if (vs_cur != vs) delete vs_cur;
	vs_cur = vs_par;
    }
    // Strip bogus final "/" from return value
    ret = ret.Sub(0, ret.Length() - 1);
    if (vs_cur != vs) delete vs_cur;
    return ret;		  
}

Text 
ReposUI::getMasterHintDir(VestaSource* vs, const Text& cname)
      throw (ReposUI::failure, SRPC::failure)
{
  if(!vs->master)
    throw ReposUI::failure("Not master", -1, VestaSource::notMaster);

  VestaSource* vs_cur = vs;
  VestaSource* vs_parent = NULL; 
  Text hint_dir = cname;
  while(vs_cur) {
    // get parent
    vs_parent = vs_cur->getParent();
    if(vs_parent) {
      // stop if parent is non master
      if(!vs_parent->master) 
	break;
     
      if(vs_cur != vs)
	delete vs_cur;
      vs_cur = vs_parent;

      int slash = hint_dir.FindCharR('/');
      assert(slash != -1);
      hint_dir = hint_dir.Sub(0, slash);
    }
    else // it is root
      break;
  }
  
  if(vs_parent)
    delete vs_parent;
  if(vs_cur && vs_cur != vs)
    delete vs_cur;
    
  return hint_dir;
}

struct verclosure {
  long high;
  VestaSource* parent;
  Text hints;
  Text path;
};

static bool
vercallback(void* closure, VestaSource::typeTag type, Arc arc,
	    unsigned int index, Bit32 pseudoInode, ShortId filesid,bool master)
{
    verclosure* cl = (verclosure*) closure;
    char* endptr;
    long val = strtol(arc, &endptr, 10);
    if (*endptr == '\0' && val > cl->high) { 
      if(type != VestaSource::stub) {
	cl->high = val;
      } else if(!master) {
	try {
	  if(cl->path == "")
	     cl->path = ReposUI::vsToFilename(cl->parent);
	  Text name = cl->path + "/" + arc;
	  VestaSource* vs_real = ReposUI::filenameToRealVS(name, cl->hints);
	  if(vs_real->type != VestaSource::stub)
	    cl->high = val;
	  delete vs_real;
	} catch(ReposUI::failure) {
	  // Ignore
	}
      }
    }
    return true;
}

long
ReposUI::highver(VestaSource* vs, Text hints, Text path) 
  throw (ReposUI::failure, SRPC::failure)
{
    // Return the highest version number in directory vs.  That is,
    // considering the set of arcs in the directory that are composed
    // entirely of decimal digits, interpret each as a decimal number
    // and return the value of the largest that is not a stub.
    // Return -1 if the set is empty.
    verclosure cl;
    cl.high = -1;
    cl.parent = vs;
    cl.hints = hints;
    cl.path = path;
   
    VestaSource::errorCode err = vs->list(0, vercallback, &cl);
    if (err != VestaSource::ok) {
	throw ReposUI::failure("error listing directory of versions: " +
			       ReposUI::errorCodeText(err), -1, err);
    }
    return cl.high;
}

Text
ReposUI::uniquify(VestaSource* vs, const Text& prefix)
     throw (ReposUI::failure, SRPC::failure)
{
  char buf[64];
  Text result;
  unsigned int suffix = 1;
  while(1)
    {
      // Generate a candidate name
      sprintf(buf, "%d", suffix);
      result = prefix + "." + buf;
      // See if it already exists
      VestaSource *kid = 0;
      VestaSource::errorCode err = vs->lookup(result.cchars(), /*OUT*/ kid);
      // If this name doesn't exist, we're done.
      if(err == VestaSource::notFound)
	{
	  break;
	}
      // If this name does exist, free the object just created.
      else if(err == VestaSource::ok)
	{
	  assert(kid != 0);
	  delete kid;
	}
      // Any other error is fatal.
      else
	{
	  throw ReposUI::failure("error on lookup for generating unique name: " +
				 ReposUI::errorCodeText(err), -1, err);
	}
      // Increment the suffix for our next attempt.
      suffix++;
    }

  return result;
}

struct changed_closure {
    bool changed;
    VestaSource* vs;
    time_t since;
};

static bool
changed_callback(void* closure, VestaSource::typeTag type,
		 Arc arc, unsigned int index, Bit32 pseudoInode,
		 ShortId filesid, bool master)
{
    changed_closure* cl = (changed_closure*) closure;
    assert(!cl->changed);
    switch (type) {
      case VestaSource::mutableFile:
	cl->changed = true;
	break;
      case VestaSource::mutableDirectory:
	{
	    VestaSource* vs;
	    VestaSource::errorCode err = cl->vs->lookupIndex(index, vs);
	    if (err != VestaSource::ok) {
		throw ReposUI::failure("error on lookupIndex: " +
				       ReposUI::errorCodeText(err), -1 , err);
	    }
	    cl->changed = ReposUI::changed(vs, cl->since);
	}
	break;
      default:
	break;
    }
    return !cl->changed; // stop if a change found
}

bool
ReposUI::changed(VestaSource* vs, time_t since)
  throw (ReposUI::failure, SRPC::failure)
{
    // Return true if the tree rooted at the given mutable working
    // directory has changed since the last vcheckout or vadvance.  The
    // time of the vadvance must be provided as an argument, but the
    // checking is not based entirely on time.  The tree is deemed to have
    // changed if (1) the modified time of any directory in the tree is
    // greater than the given time, or (2) any directory in the tree
    // contains a mutable file (i.e., a file for which no copy-on-write
    // has been performed since the last vcheckout or vadvance).

    if (vs->timestamp() > since) return true;
    changed_closure cl;
    cl.changed = false;
    cl.vs = vs;
    cl.since = since;
    VestaSource::errorCode err = vs->list(0, changed_callback, &cl);
    if (err != VestaSource::ok) {
	throw ReposUI::failure("error listing directory: " +
			       ReposUI::errorCodeText(err), -1 , err);
    }
    return cl.changed;
}

struct cleanup_closure {
    VestaSource* vs;
    Sequence<Text>* pattern;
    size_t maxsize;
    Text prefix;
};

static bool
cleanup_callback(void* closure, VestaSource::typeTag type,
		 Arc arc, unsigned int index, Bit32 pseudoInode,
		 ShortId filesid, bool master)
{
  cleanup_closure* cl = (cleanup_closure*) closure;
  VestaSource* vs;
  VestaSource::errorCode err = cl->vs->lookupIndex(index, vs);
  if (err != VestaSource::ok) {
    throw ReposUI::failure("error on lookupIndex: " +
			   ReposUI::errorCodeText(err), -1 , err);
  }

  switch (type) {
  case VestaSource::mutableFile:
  case VestaSource::immutableFile: {
    int i;
    for (i=0; i<cl->pattern->size(); i++) {
      if (fnmatch(cl->pattern->get(i).cchars(), arc, 0) == 0) {
	err = cl->vs->reallyDelete(arc);
	if (err != VestaSource::ok) {
	  throw ReposUI::failure("error deleting " + cl->prefix + arc + ": " +
				 ReposUI::errorCodeText(err), -1 , err);
	}
	return true;
      }
    }
    if (type == VestaSource::mutableFile && vs->size() > cl->maxsize) {
      throw(ReposUI::failure(cl->prefix + arc + " is " +
			     Basics::FormatUnitVal(vs->size()) + 
			     " which is over the max file size safety limit (" +
			     Basics::FormatUnitVal(cl->maxsize) +  ")"));
    }
    break; }

  case VestaSource::mutableDirectory: {
    cleanup_closure cl2 = *cl;
    cl2.vs = vs;
    cl2.prefix = cl->prefix + arc + "/";
    err = vs->list(0, cleanup_callback, &cl2);
    if (err != VestaSource::ok) {
      throw ReposUI::failure("error listing directory " + cl->prefix +
			     ReposUI::errorCodeText(err), -1 , err);
    }
    break; }

  default:
    break;
  }
  return true;
}

void
ReposUI::cleanup(VestaSource* vs, const Text& pattern,
		 unsigned long maxsize, const Text& prefix)
  throw (ReposUI::failure, SRPC::failure)
{
  cleanup_closure cl;
  cl.vs = vs;
  int i = 0;
  cl.pattern = NEW(Sequence<Text>);
  for (;;) {
    while (pattern[i] && pattern[i] == ' ') i++;
    if (!pattern[i]) break;
    int j = i;
    while (pattern[j] && pattern[j] != ' ') j++;
    cl.pattern->addhi(pattern.Sub(i, j-i));
    i = j;
  }
  cl.maxsize = maxsize;
  cl.prefix = prefix;
  int len = prefix.Length();
  if (len == 0 || prefix[len - 1] != '/') {
    cl.prefix = cl.prefix + "/";
  }
  VestaSource::errorCode err = vs->list(0, cleanup_callback, &cl);
  if (err != VestaSource::ok) {
    throw ReposUI::failure("error listing directory " + prefix +
			   ReposUI::errorCodeText(err), -1 , err);
  }
}

const Text
ReposUI::errorCodeText(VestaSource::errorCode err) throw (ReposUI::failure)
{
    switch (err) {
      case VestaSource::ok:
	return "No error";
      case VestaSource::notFound:
	return "Not found";
      case VestaSource::noPermission:
	return "No permission";
      case VestaSource::nameInUse:
	return "Name in use";
      case VestaSource::inappropriateOp:
	return "Operation not available on given source type";
      case VestaSource::nameTooLong:
	return "Name too long";
      case VestaSource::rpcFailure:
	return "Remote call from repository to another server failed";
      case VestaSource::notADirectory:
	return "Not a directory";
      case VestaSource::isADirectory:
	return "Is a directory";
      case VestaSource::invalidArgs:
	return "Invalid argument";
      case VestaSource::outOfSpace:
	return "Out of disk space";
      case VestaSource::notMaster:
	return "Not master replica";
      case VestaSource::longIdOverflow:
	return "LongId overflow (directories nested too deeply)";
      default: {
	  const char *str = VestaSource::errorCodeString(err);
	  if (str) {
	      return Text("VestaSource::") + str;
	  } else {
	      throw ReposUI::failure("unknown VestaSource error code",
				     -1, err);
	  }
      }
    }
    //return "oops"; // not reached
}

static Text promptMessage(bool interactive,
			  const Text &description) throw (ReposUI::failure)
{
  Text message;
  // Read message from standard input
  char buf[1024];
  int i = 0;
  int c;
  bool start_of_line = true;
  bool dot_starts_line = false;
  bool broke_with_dot = false;
  if (interactive) {
    cout << "Enter " << description
	 << ", terminated with ^D or . on a line by itself\n"
	 << ": " << flush;
  }
  while ((c = cin.get()) != EOF) {
    if (c == '\000') {
      throw ReposUI::failure(description + " contains a NUL character", 
			     -1, VestaSource::invalidArgs);
    }
    if (c == '.' && start_of_line == true) {
      dot_starts_line = true;
    }
    else if(c != '\n') {
      dot_starts_line = false;
    }
    start_of_line = false;
    buf[i++] = c;
    if (interactive && c == '\n') {
      if ( dot_starts_line == true ) {
	broke_with_dot = true;
	break;
      }
      cout << ": " << flush;
      start_of_line = true;
    }
    if (i == sizeof(buf) - 1) {
      buf[i] = '\000';
      message = message + buf;
      i = 0;
    }
  }

  buf[i] = '\000';
  message = message + buf;
  // now remove the "\n.\n" if that's how the message was ended.
  if(broke_with_dot) message = message.Sub(0, message.Length()-3);
  if (interactive) cout << endl;

  return message;
}

Text ReposUI::getMessage(const Text &description,
			 const Text &in_description,
			 const char* in_message) 
  throw (ReposUI::failure, FS::Failure)
{
  Text message;
  char* editor = NULL;
  bool interactive = isatty(0);
  editor = getenv("EDITOR");
  if(!editor)
    {
      if(interactive && in_message)
	cout << in_description << ":" << endl
	     << in_message << endl;
      message = promptMessage(interactive, description);
    }
  else
    {
      char temp_name[15] = "/tmp/msgXXXXXX";
      int temp_fd = mkstemp(temp_name);
      if(temp_fd == -1)
	throw ReposUI::failure("error creating a temp file for editing "+
			       description);
      // Close the file descriptor mkstemp opened, looping on EINTR
      {
	int close_res;
	do
	  close_res = ::close(temp_fd);
	while((close_res != 0) && (errno == EINTR));
      }
      Text fname = (Text)temp_name;
      // copy the input message to the file
      Text default_msg = ("<enter " + description + " here>");
      ofstream ofs;
      FS::OpenForWriting(fname, ofs);
      if(in_message)
	FS::Write(ofs, (char *) in_message, strlen(in_message));
      else
	FS::Write(ofs, default_msg.chars(), default_msg.Length());
      FS::Close(ofs);
      // run editor
      cout << "Running " << editor << " to edit " << description << endl;
      Text cmd = (Text)editor + " " + fname;
      if(system(cmd.chars()) != 0) {
	throw ReposUI::failure("error running editor " + (Text)editor);
      }
      // read the message from the file
      ifstream ifs;
      FS::OpenReadOnly(fname, ifs);
      char buff[1024];
      try {
	while(!FS::AtEOF(ifs)) {
	  memset(buff,0, 1024);
	  FS::Read(ifs, buff, 1023);
	  message = message + (Text)buff;
	}
      }
      catch(FS::EndOfFile) {
	message = message + (Text)buff;
      }
      FS::Close(ifs);
      FS::Delete(fname);

      // Convert the default message to an empty message.
      if(message == default_msg)
	message = "";
      else
	{
	  // Convert an all whitespace message to an empty message.
	  int i = 0;
	  while((i < message.Length()) && isspace(message[i]))
	    i++;
	  if(i == message.Length())
	    message = "";
	}

      // If the user entered an empty message or left the input message
      // unedited, ask them for confirmation.  (This is the user's only
      // chance to abort when EDITOR is set.)
      if(message.Empty() ||
	 (in_message && message == in_message))
	{
	  if(message.Empty())
	    cout << "Empty " << description;
	  else
	    cout << in_description << " unedited for " << description;
	  cout << ".  Continue (y/n)? " << flush;

	  char buff[20];
	  cin.getline(buff, sizeof(buff));
	  if((buff[0] != 'y') && (buff[0] != 'Y'))
	    {
	      throw ReposUI::failure("user aborted after editing " +
				     description);
	    }
	}
    }
  return message;
}

void ReposUI::runTriggers(const Text &name, bool verbose, bool execute)
  throw(ReposUI::failure, VestaConfig::failure)
{
  // Get the list of trigger names and sort them.
  TextSeq triggerNames = VestaConfig::section_vars(name);
  std::sort(triggerNames.begin(), triggerNames.end(),
	    Basics::TextLessThanWithInts);

  // Run the trigger commands
  while(triggerNames.size())
    {
      // Get the next command
      Text triggerName = triggerNames.remlo();
      Text command = VestaConfig::get_Text(name, triggerName);

      // Skip any that are empty.  (This allows a user to remove a
      // trigger by setting it to the empty string.)
      if(command.Empty())
	continue;

      // Print the trigger if we're supposed to be verbose.
      if(verbose)
	cout << "Running trigger [" << name << "]" << triggerName
	     << " = " << command << endl;

      // If we're not supposed to execute the triggers, skip the the
      // next iteration.
      if(!execute)
	continue;

      errno = -1; // Protect against the possibility of system(3)
		  // leaving errno unmodified.
      // Run the trigger command.
      int sres = system(command.cchars());

      // Handle various failure possibilities
      if(sres == -1)
	{
	  // system(3) failed to launch the command
	  int errno_save = errno;
	  OBufStream msg;
	  msg << "Couldn't start trigger [" << name << "]" << triggerName
	      << " (" << command << ")";
	  throw failure(msg.str(), errno_save);
	}
      else if(WEXITSTATUS(sres))
	{
	  // Trigger indicates failure
	  OBufStream msg;
	  msg << "Trigger [" << name << "]" << triggerName
	      << " (" << command << ")"
	      << " exited with error (" << WEXITSTATUS(sres) << ")";
	  throw failure(msg.str());
	}
      else if(WIFSIGNALED(sres))
	{
	  if(WTERMSIG(sres) == SIGINT || WTERMSIG(sres) == SIGQUIT)
	    {
	      // User probably killed the trigger (e.g. ^C when promted
	      // for a response).
	      OBufStream msg;
	      msg << "Trigger [" << name << "]" << triggerName
		  << " (" << command << ")"
		  << " was interrupted by the user";
	      throw failure(msg.str());
	    }
	  else
	    {
	      // Trigger crashed?
	      OBufStream msg;
	      msg << "Trigger [" << name << "]" << triggerName
		  << " (" << command << ")"
		  << " was killed by a signal ("
		  << WTERMSIG(sres);
	      if(WTERMSIG(sres) == SIGSEGV)
		msg << ", invalid memory reference";
	      if(WTERMSIG(sres) == SIGABRT)
		msg << ", abort";
	      msg << ")";
	      throw failure(msg.str());
	    }
	}
    }
}

void ReposUI::setTriggerVars(const TextTextTbl &vars, bool verbose)
  throw()
{
  TextTextIter it(&vars);
  Text var, value;
  if(verbose)
    {
      // Extract the names from the table
      TextSeq varNames;
      while(it.Next(var, value))
	{
	  varNames.addhi(var);
	}
      it.Reset();

      // Sort the names
      std::sort(varNames.begin(), varNames.end());

      // Print out the names and values sorted by name
      cout << "Trigger variables:" << endl;
      while(varNames.size() > 0)
	{
	  var = varNames.remlo();
	  bool inTbl = vars.Get(var, value); assert(inTbl);
	  cout << "\t" << var << "=" << value << endl;
	}
    }

  // Set the variables
  while(it.Next(var, value))
    {
      OS::setenv(var, value);
    }
}

Text ReposUI::prevVersion(VestaSource* vs, Text hints)
  throw (failure, SRPC::failure)
{
	VestaSource* parent = NULL;
	VestaSource* version = vs;
	VestaSource *cvs;

	//Hints for finding content in other repositories
	if(hints == "" && VestaConfig::is_set("UserInterface", "DefaultHints")) {
		hints = VestaConfig::get_Text("UserInterface", "DefaultHints");
	}

	//Follow the content attribute back as far as it goes.
	while(1) {
		char *content = version->getAttrib("content");
		if(!content) break;

		//Lookup the path given in the attribute
		try {
			cvs = filenameToRealVS(content, hints);
		} catch(...) {
			cvs = NULL;
		}

		// follow the content attribute
		if(cvs) {
			assert(version != cvs);
			if(version != vs) delete version;
			version = cvs;
			delete [] content;
			continue;
		}

		// We couldn't find a replica of the content, so we'll look for
		// one of its parent
		Text parent_t, arc;
		split(content, parent_t, arc, "malformed content attribute");
		try {
			parent = filenameToRealVS(parent_t, hints);
		} catch(...) {
			//Ignore
		}
		delete [] content;
		break;
	}
	
	//Presumably, we're now looking at a version in a session directory.
	//The real old-version is on the session directory itself.
	if(parent == NULL || !parent->inAttribs("type", "session")) {
		parent = version->getParent();
	}
	char *ov = NULL;
	Text r("");
	if(parent->inAttribs("type", "session")) {
		ov = parent->getAttrib("old-version");
	}
	if(ov) {
		r = ov;
	} else if(ov = version->getAttrib("old-version")) {
		//If version has an old-version attribute, use it.
		r = ov;
	} else if(parent->inAttribs("type", "package") || parent->inAttribs("type", "branch")) {
		//If parent is a package or branch (maybe we're at version 0
		//of a branch which wouldn't have an old-verions attribute),
		//use the parent's old-version
		ov = parent->getAttrib("old-version");
		if(ov) r = ov;
	} else {
		// Come up with something creative to put in here.
	}

	//And now we've done everything we can, clean up and return whatever we found.
	delete parent;
	if(ov) delete [] ov;
	if(version != vs) delete version;
	return r;
}
