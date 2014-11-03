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

// vupdate(1) -- update the imports of a specified model


#include <unistd.h>    // for getcwd(3), getpid(2)
#include <stdio.h>     // for rename(2), sprintf(3)
#include <ctype.h>     // for isdigit(3)
#include <errno.h>

#include <Basics.H>
#include <sys/types.h> // for opendir(3), etc.
#include <dirent.h>    // for opendir(3), etc.

#include <FS.H>
#include <Generics.H>
#include <VestaConfig.H>
#include <VestaSource.H>
#include <UniqueId.H>
#include <ParseImports.H>
#include <ReposUI.H>

#include <strings.h>   // for bzero

using std::ostream;
using std::ofstream;
using std::ifstream;
using std::cout;
using std::cin;
using std::cerr;
using std::endl;

// Possible values for the 'verbose' switch variable
static const int Silent = 0;       // don't print anything
static const int ChangesOnly = 1;  // only print models with updates (default)
static const int Verbose = 2;      // print all models considered

// Variables for the command-line switches
static bool doWork = true;            // true iff -no-action switch NOT present
static bool query = false;            // is -query switch present?
static int verbose = ChangesOnly;     // is -silent or -verbose switch present?
static bool update_local = false;     // is -update-local switch present?
static bool update_co = false;       // is -update-checkout switch present?
static bool advance_co = false;       // is -advance-checkout switch present?
static bool onlyMine = false;         // is -only-mine switch present?
static bool toCheckout = false;       // is -to-checkout switch present?
static bool parse_errors_stop = false;
static TextSeq limiters;              // arg(s) to -limit switch(es)
static char *attrName = NULL;         // attribute name to match
static TextSeq *attrVals = NULL;      // list of attribute values
static bool attrMatchSense;           // true for ':', false for '^'
static bool followContinuations = false; // is -follow-continuations switch present? 

// How many times has a vadvance(1) command we invoked failed?  (Only
// applies when "advance_co" is true.)
static unsigned int vadvance_error_count = 0;

static void Option(char *flag, char *meaning) throw ()
{
    cerr << "  " << flag;
    int pad = max(0, 23 - strlen(flag));
    for (int i = 0; i < pad; i++) cerr << ' ';
    cerr << ' ' << meaning << endl;
}

static void Syntax(char *msg, char *arg = (char *)NULL) throw ()
{
    cerr << "Error: " << msg;
    if (arg != (char *)NULL) cerr << ": '" << arg << "'";
    cerr << endl;
    cerr << "Syntax: vupdate [ options ] [ model ]" << endl;
    cerr << "Possible options are:" << endl;
    Option("-n or -no-action", "do not change any models");
    Option("-q or -query", "ask user about each update");
    Option("-s or -silent", "do not report updates");
    Option("-r or -update-local",
	   "recurse on all models imported locally");
    Option("-c or -update-checkout",
	   "recurse on all models imported from checkout sessions");
    Option("-u or -update-all", "set both -update-local and -update-checkout");
    Option("-a or -advance-checkout", "advance imported checkout sessions");
    Option("-v or -verbose", "report on all processed models");
    Option("-l or -limit substring",
	   "only update imports containing 'substring'");
    Option("-L or -limit-checkout",
	   "only update imports from checkout sessions");
    Option("-t or -to-checkout", "update to a more recent checkout session");
    Option("-m or -only-mine", "update a checkout import only if owned by me");
    Option("-A or -attr attr-spec",
	   "only update to versions with given attributes");
    Option("-e or -parse-errors-stop",
	   "stop if on import paths we can't understand (non-numeric arcs)");
    exit(1);
}

static void Indent(ostream &os, int n) throw ()
{
    for (/*SKIP*/; n > 0; n--) os << ' ';
}

static void CopyFromTo(ifstream &ifs, ofstream &ofs, int loc = -1)
  throw (FS::Failure)
/* Copy characters from "ifs" to "ofs" starting at the current character in
   "ifs" and up to (but not including) the character in "ifs" at position
   "loc". If "loc < 0", copy up to the end of "ifs". */
{
    const int BuffSz = 1024;
    char buff[BuffSz];
    int toCopy = (loc >= 0) ? (loc - (int)FS::Posn(ifs)) : (-1);
    assert(loc < 0 || toCopy >= 0);
    try {
	while (loc < 0 || toCopy > 0) {
	    int cnt = (loc < 0) ? BuffSz : min(BuffSz, toCopy);
	    FS::Read(ifs, buff, cnt);
	    FS::Write(ofs, buff, cnt);
	    if (loc >= 0) toCopy -= cnt;
	}
    }
    catch (FS::EndOfFile) {
	// We should only encounter this exception if no definite stopping
	// point was provided.
        if(loc >= 0)
	  {
	    throw FS::Failure("copying from old file to new file",
			      "unexpected end-of-file (maybe there are two "
			      "vupdates working on the same .ves file?)");
	  }

	// write the characters that were read
	FS::Write(ofs, buff, ifs.gcount());
    }
}

// name and VestaSource of repository's appendable root (/vesta)
static const Text AppendableRootName = "/vesta/";
static VestaSource *RepositoryRoot;

// dummy class for initializing RepositoryRoot
class VUpdateInit {
  public:
    VUpdateInit() throw () {
        RepositoryRoot = VestaSource::repositoryRoot();
    }
};
static VUpdateInit vupdateInit;

static bool ImmutableDir(const Text &path) throw (SRPC::failure)
/* Return "true" iff "path" is the absolute path of an immutable directory
   in the appendable root. */
{
    if (path.FindText(AppendableRootName) != 0) return false;
    Text relPath(path.Sub(AppendableRootName.Length()));
    VestaSource *res = 0;
    VestaSource::errorCode err =
	RepositoryRoot->lookupPathname(relPath.cchars(), /*OUT*/ res);
    bool result = ((err == VestaSource::ok) &&
		   (res->type == VestaSource::immutableDirectory));
    if(res != 0)
      delete res;
    return result;
}

static void VersionDir(const Text &path, /*OUT*/ int &prefixLen,
  /*OUT*/ int &suffixStart, /*OUT*/ int &verNum,
  /*INOUT*/ bool &checkedIn, bool doStat)
  throw (ParseImports::Error, SRPC::failure)
/* "path" is a (possibly relative) path of some model. Set "prefixLen" to
   the length of the prefix of "path" that is the name of the directory
   (ending with a '/') in which to search for the highest version of the
   package or checkout session of the model; "suffixStart" to the index
   of the first character of the suffix of "path" to be appended after
   the correct version (which, if non-empty, will start with '/'); and
   set "verNum" to the current version number. If "suffixStart" is set
   to "path.Length()", then the suffix is empty.

   Examples:

|  path                          prefix              	    suffix
|  ----------------------------- ------------------------- --------------
|  thread/3                      thread/             	   <empty>
|  thread/3/build.ves            thread/             	   /build.ves
|  thread/3/src/progs.ves        thread/             	   /src/progs.ves
|  thread/3.test/5               thread/3.test/      	   <empty>
|  thread/3.test/5/build.ves     thread/3.test/      	   /build.ves
|  thread/3.test/5/src/progs.ves thread/3.test/      	   /src/progs.ves
|  thread/checkout/2/5/build.ves thread/checkout/2/        /build.ves
|  thread/3.test/checkout/2/5    thread/3.test/checkout/2/ <empty>

   In the event that "path" names a checkout session, and if "checkedIn" is
   initially true or "doStat" is true and that checkout session has been
   checked in, then "prefixLen" will be set so the highest numbered version
   will be searched for on the main branch, and "checkedIn" will be set to
   "true". Otherwise, "checkedIn" will be unchanged. For example, if version
   2 of both the "thread" and "thread/3.test" packages have been checked in:

|  path                          prefix              	    suffix
|  ----------------------------- ------------------------- --------------
|  thread/checkout/2/5/build.ves thread/                   /build.ves
|  thread/3.test/checkout/2/5    thread/3.test/            <empty>
*/
{
    Text verTxt; // package sub-version number

    // first, see if "path" is a checkout version
    int len = path.Length();
    Text pattern("/checkout/");
    int ckout = path.FindText(pattern);
    if (ckout >= 0) {
	// skip first arc after "/checkout/"
	int ckoutEnd = ckout + pattern.Length();
	prefixLen = path.FindChar('/', ckoutEnd);
	if (prefixLen < 0) {
	    Text msg("malformed checkout session name: '");
	    msg += path; msg += "'";
	    throw ParseImports::Error(msg);
	}
	Text ckoutVer(path.Sub(ckoutEnd, prefixLen - ckoutEnd));
	prefixLen++; // skip '/'

	// skip next arc for start of suffix
	// (and set 'verTxt' to name of next arc (package subversion))
	suffixStart = path.FindChar('/', prefixLen);
	if (suffixStart < 0) suffixStart = len;
	verTxt = path.Sub(prefixLen, suffixStart - prefixLen);

        // adjust "prefixLen" and "verTxt" if session has been checked in
	if (doStat) {
	    Text dirName(path.Sub(0, ckout + 1) + ckoutVer);
	    checkedIn = ImmutableDir(dirName);
	}
        if (checkedIn) {
	    prefixLen = ckout + 1;
	    verTxt = ckoutVer;
	}
    } else {
	// stop at first arc of "path" that consists entirely of digits
	prefixLen = 0;
	do {
	    if (path[prefixLen] == '/') prefixLen++;
	    int curr = prefixLen;
	    while (curr < len && isdigit((int)(path[curr]))) curr++;
	    if (curr >= len || path[curr] == '/') {
		suffixStart = curr;
		break;
	    }
	    prefixLen = path.FindChar('/', prefixLen);
	} while (prefixLen >= 0);
	if (prefixLen < 0 || prefixLen >= len) {
	    Text msg("no numeric arc: '"); msg += path; msg += "'";
	    throw ParseImports::Error(msg);
	}
	verTxt = path.Sub(prefixLen, suffixStart - prefixLen);
    }
    // convert "verTxt" to "verNum"
    if (sscanf(verTxt.chars(), "%d", &verNum) != 1) {
	// directory is not a number; give up...
	Text msg("version not numeric: '"); msg += verTxt; msg += "'";
	throw ParseImports::Error(msg);
    }
} // end VersionDir

static bool AllDigits(const char *str) throw ()
/* Return "true" iff "str" is a string consisting of all digits. */
{
    for (/*SKIP*/; *str != '\0'; str++) {
	if (!isdigit((int)(*str))) return false;
    }
    return true;
}

static bool AttribMatch(VestaSource *child) throw (SRPC::failure)
/* Return true iff the attributes of 'child' match the '-attr' specification
   embodied by the global variables 'attrName', 'attrVals', and
   'attrMatchSense'. */
{
  // does 'child' define attribute?
  const char *attrVal = child->getAttrib(attrName);
  if (attrVal != (char *)NULL) {
    delete [] attrVal;
    // were any attribute values specified?
    if (attrVals != (TextSeq *)NULL) {
      for (int i = 0; i < attrVals->size(); i++) {
	Text val = attrVals->get(i);
	if (child->inAttribs(attrName, val.cchars())) {
	  return attrMatchSense;
	}
      }
      // there was no match on any value
      return !attrMatchSense;
    } else {
      // this is the '-attr attrName' case
      return true;
    }
  } else {
    // otherwise, there is a match iff this is the 'attr^vals' case
    return (attrVals != NULL && !attrMatchSense);
  }
}

// Structure type for 'ListCallback' closure argument
typedef struct {
    int max;
    VestaSource *parent;
} CallbackClosure;

static bool ListCallback(void *closure, VestaSource::typeTag type,
  Arc arc, unsigned int index, Bit32 pseudoInode, ShortId filesid, bool master)
  throw (SRPC::failure)
/* The 'closure' argument is actually a 'CallbackClosure *' that points
   to the largest version number seen so far. If 'type' is immutable
   directory and if 'arc' names a numeric directory whose value is larger
   than the current maximum, update the maximum value in 'closure'. */
{
    CallbackClosure *cc = (CallbackClosure *)closure;
    if (type == VestaSource::immutableDirectory && AllDigits(arc)) {
        if (attrName != (char *)NULL) {
            // Fetch the 'VestaSource' object for 'index'
            VestaSource *child = 0;
	    VestaSource::errorCode err =
              cc->parent->lookupIndex(index, /*OUT*/ child);
	    bool no_good = (err != VestaSource::ok || !AttribMatch(child));
	    if (child != NULL) delete child;
	    if(no_good)
	      return true;
        }
        int val;
	int scanRes = sscanf(arc, "%d", &val);
	assert(scanRes == 1);
	if (val > cc->max) cc->max = val;
    }
    return true;
}

static int HighestVersion(const Text &path)
  throw (ParseImports::Error, SRPC::failure)
/* Return the numerical value of the name in the directory "dir" that
   consists of all digits, has the largest numerical value, and names
   a sub-directory (and hence, is not a ghost or stub). */
{
    // verify that 'path' names an appendable directory, and get it
    if (path.FindText(AppendableRootName) != 0) {
        Text errMsg("imported path not in appendable root: ");
	errMsg += path;
        throw ParseImports::Error(errMsg);
    }
    Text relPath(path.Sub(AppendableRootName.Length()));
    VestaSource *dir = 0;
    VestaSource::errorCode err =
      RepositoryRoot->lookupPathname(relPath.cchars(), /*OUT*/ dir);
    if (err != VestaSource::ok) {
        Text errMsg("unable to open directory in import: ");
	errMsg += path;
	throw ParseImports::Error(errMsg);
    } else if (dir->type != VestaSource::appendableDirectory) {
        delete dir;
        Text errMsg("package import is not an appendable directory: ");
        errMsg += path;
	throw ParseImports::Error(errMsg);
    }

    assert(dir != 0);

    // iterate over the directory
    CallbackClosure cc;
    cc.max = -1;
    cc.parent = dir;
    err = dir->list(/*firstIndex=*/ 0, ListCallback, (void *)(&cc));

    // clean up
    delete dir;
    return cc.max;
} // end HighestVersion

static bool OthersCheckout(const Text &p_path)
/* Return true iff the imported path 'p_path' is a checkout session
   belonging to someone else. */
{
    // pattern to search for
    Text pattern("/checkout/");

    int ckout = p_path.FindText(pattern);
    int ckoutEnd = ckout + pattern.Length();
    // Assume that this is either not a checkout session, or that it
    // must belong to us.
    bool result = false;
  
    // If we found "/checkout/" and there's something past it...
    if((ckout >= 0) && (p_path[ckoutEnd] != 0))
    {
        // skip first arc after "/checkout/"
        int l_session_len = p_path.FindChar('/', ckoutEnd);
	Text l_session_path = ((l_session_len >= 0)
			       ? p_path.Sub(0, l_session_len)
			       : p_path);
	// Look up the session
	VestaSource *l_session = NULL;
	try {
	    l_session = ReposUI::filenameToVS(l_session_path);
	} catch (...) {
	    // ReposUI::filenameToVS may throw an exception.  We need to
	    // catch it if it does, but we really only care if we got
	    // something.
	}
	// As long as we got it and it is a session...
	if ((l_session != NULL) && l_session->inAttribs("type", "session")) {
	    // Get the global username
	    Text l_uspec(AccessControl::self()->user());

	    // Test if the session is checked out by someone who isn't
	    // us.  (If it has no "checkout-by" attribute, assume it's
	    // *not* ours.)
            char *l_checkout_user = l_session->getAttrib("checkout-by");
	    if((l_checkout_user == 0) ||
	       (strcmp(l_uspec.cchars(), l_checkout_user) != 0))
	      result = true;
	    if(l_checkout_user != 0)
	      delete [] l_checkout_user;
	}
	if(l_session != 0)
	  delete l_session;
    }

     return result;
}

static void Update(const Text &model, /*INOUT*/ TextSeq &localModels)
  throw (FS::DoesNotExist, FS::Failure, ParseImports::Error, 
	 VestaConfig::failure, SRPC::failure);  // forward

static void RecurseOnCheckout(const Text &p_path, const Text& suffix)
  throw (FS::DoesNotExist, FS::Failure, ParseImports::Error, 
	 VestaConfig::failure, SRPC::failure)
/* If p_path comes from a checkout session belonging to this user:
   If the -c flag was given, invoke vupdate recursively on the session.
   If the -a flag or -c flag was given, advance the session. */
{
  if (!advance_co && !update_co) return;

  // pattern to search for
  Text pattern("/checkout/");

  int ckout = p_path.FindText(pattern);
  int ckoutEnd = ckout + pattern.Length();
  int l_session_len;
 
  // skip first arc after "/checkout/"
  if (ckout < 0 || (l_session_len = p_path.FindChar('/', ckoutEnd)) < 0) { 
    // Not a checkout import
    return;
  }

  Text l_session_path = p_path.Sub(0, l_session_len);
  // Look up the session
  VestaSource *l_session = NULL;
  try {
    l_session = ReposUI::filenameToVS(l_session_path);
  } catch (...) {
    // ReposUI::filenameToVS may throw an exception.  We need to
    // catch it if it does, but we really only care if we got
    // something.
  }
  if (l_session == NULL || !l_session->inAttribs("type", "session")) {
    // Not a session
    return;
  }

  // Get the global username
  Text l_uspec(AccessControl::self()->user());
  
  // Test if the session is checked out by someone who isn't us.  (If
  // it has no "checkout-by" attribute, assume it's *not* ours.)
  char *l_checkout_user = l_session->getAttrib("checkout-by");
  bool not_my_checkout = ((l_checkout_user == 0) ||
			  (strcmp(l_uspec.cchars(), l_checkout_user) != 0));
  if(l_checkout_user != 0)
    delete [] l_checkout_user;
  if(not_my_checkout)
    {
      delete l_session;
      return;
    }

  // Find working directory.
  char *l_workdir = l_session->getAttrib("work-dir");
  delete l_session;
  if (l_workdir == NULL) return;  // no workdir attribute
  // Look up the working directory that the work-dir attribute points
  // to.
  VestaSource *l_workdir_vs = 0;
  try {
    l_workdir_vs = ReposUI::filenameToVS(l_workdir);
  } catch (...) {
    // Failure handled below
  }
  // If the working directory doesn't exist or isn't a mutable
  // directory, print a message and return.  (This can happen with a
  // non-exclusive session that has since had its working directory
  // deleted.)
  if((l_workdir_vs == 0) ||
     (l_workdir_vs->type != VestaSource::mutableDirectory))
    {
      if (verbose != Silent)
        cerr << "vupdate:  the working directory of checkout session "
	     << l_session_path << " (" << l_workdir
	     << (") no longer exists!  "
		 "Skipping recursive updates for this import.")
	     << endl;
      delete [] l_workdir;
      if(l_workdir_vs != 0) delete l_workdir_vs;
      return;
    }
  // Check to make sure that the working directory we have found is
  // for this session directory.  Print a message and return if it's
  // not.  (The original working directory could have been renamed or
  // deleted, and this could be for some other session.)
  else if(!l_workdir_vs->inAttribs("session-dir", l_session_path.cchars()))
    {
      if (verbose != Silent)
        cerr << "vupdate:  the working directory of checkout session "
	     << l_session_path << " (" << l_workdir
	     << (") exists but belongs to a different session.  "
		 "Skipping recursive updates for this import.")
	     << endl;
      delete [] l_workdir;
      delete l_workdir_vs;
      return;
    }
  // We don't need the VestaSource for anything else.
  delete l_workdir_vs;
  l_workdir_vs = 0;
  
  // Construct model name in working directory
  Text w_path = Text(l_workdir) + (suffix.Empty() ? "/build.ves" : suffix);

  // OK to update it?
  if (update_co) {
    if (query) {
      cout << "  Update \"" << w_path << "\" (y/n)? ";
      cout.flush();
      const int BuffLen = 20; char buff[BuffLen];
      cin.getline(buff, BuffLen);
      if (buff[0] != 'y')
	{
	  delete [] l_workdir;
	  return;
	}
    }

    // Update the imported model
    TextSeq localModels(/*sizeHint=*/ 10);
    Update(w_path, /*INOUT*/ localModels);
    while (localModels.size() > 0) {
      // Update the imported model's local imports
      Text model = localModels.remlo();
      Update(model, /*INOUT*/ localModels);
    }
  }

  // Advance (by invoking a separate program).
  // If we get here, advance_co must be true.
  if (doWork) {
    Text cmd = Text("vadvance ") + l_workdir;
    int vadvance_err = system(cmd.cchars());
    if(
       // fork(2) failed
       (vadvance_err == -1) ||
       // Process didn't exit normally (e.g. was killed by a signal)
       !WIFEXITED(vadvance_err) ||
       // Process exited with error status
       (WEXITSTATUS(vadvance_err) != 0))
      {
	vadvance_error_count++;
      }
  }
  delete [] l_workdir;
}

// Figure out how much of an import's path must remain the same in
// order for it to be possible to update to it.  This is the length of
// any "from" path plus the first arc in the text we're going to be
// replacing (imp->orig).  (We could actually leave out the first arc
// of imp->orig in cases where it's not used as the identfier in SDL,
// but the import parser doesn't currently tell us that.)  This is
// used with FollowContinuation to ensure that a continuation doesn't
// move outside the directory the import is constrained to.
int FindRequiredPrefixLen(const Import *imp)
{
  int result = imp->origOffset;
  const char *cur = imp->orig.cchars();
  while(*cur)
    {
      // If it's not a quote, increment the result.  (Quotes can
      // appear in imp->orig but won't appear in imp->path, so we
      // don't include them in our result.)
      if(*cur != '"') result++;

      // If it's a slash and this isn't an absolute path that starts
      // with a slash, we're done.  (If this is an absolute path we'll
      // stop on the second slash, so the result will probably be 6
      // for "/vesta".)
      if((*cur == '/') && (result > 1))
	{
	  // Don't include the final slash
	  result--;
	  break;
	}

      cur++;
    }
  return result;
}

// If we're in verbose mode, print a message about following or
// rejecting a continuation.  If "first" is true, no continuation has
// yet been followed and we should print the current path before the
// continuation path.  If "reject_reason" is non-zero, that indicates
// the the continuation was rejected.
void PrintVerboseContinuation(const Text &cur_path,
			      const Text &continuation,
			      bool first,
			      const char *reject_reason = 0)
{
  if (verbose == Verbose)
    {
      if(first)
	{
	  cout << endl
	       << "  " << cur_path << endl;
	}
      if(reject_reason == 0)
	{
	  cout << "  ...continued in " << continuation << endl;
	}
      else
	{
	  cout << "  ...NOT continued in " << continuation << endl
	       << "    ..." << reject_reason << endl;
	}
    }
}

// Follow "continued-in" attributes starting at "dir_path".  To be
// followed, the continuation must be within the directory whose path
// is the leading "required_prefix_len" characters of "dir_path".  The
// result indicates whether there were any continuations followed.  If
// the result is true, "new_dir_path" is set to a directory to replace
// "dir_path" and "new_ver_num" is set to a version number within that
// directory that further updates should start from.
static bool FollowContinuation(const Text &dir_path,
			       // Number of leading characters which
			       // must be the same in any directory we
			       // follow to (normally the "from"
			       // directory).
			       int required_prefix_len,
			       /*OUT*/ Text &new_dir_path,
			       /*OUT*/ int &new_ver_num)
{
  if(required_prefix_len < 0) return false;
  new_dir_path = dir_path;
  bool result = false;
  VestaSource *cur_dir_vs = 0;
  try {
    cur_dir_vs = ReposUI::filenameToVS(new_dir_path);
  } catch (...) {
    // Original directory doesn't exist!?!
    return false;
  }
  Table<Text,bool>::Default continuations_seen;
  while(1)
    {
      assert(cur_dir_vs != 0);
      char *continuation_str = cur_dir_vs->getAttrib("continued-in");
      if(continuation_str == 0) break;
      Text continuation(continuation_str);
      delete [] continuation_str;
      // If we've already followed this continuation...
      if(continuations_seen.Put(continuation, true))
	{
	  // Reject this continuation, avoiding a continuation loop
	  PrintVerboseContinuation(new_dir_path, continuation, !result,
				   "continuation loop detected");
	  break;
	}
      // If it doesn't match the required prefix ...
      else if((strncmp(continuation.cchars(), dir_path.cchars(), required_prefix_len) != 0) ||
	 // ... or the character after the required prefix isn't a
	 // terminating null byte or a slash
	 ((continuation[required_prefix_len] != 0) && (continuation[required_prefix_len] != '/')))
	{
	  // Reject this continuation
	  Text msg;
	  if(verbose == Verbose)
	    {
	      // If we actually need it, construct a detailed message 
	      Text required_prefix = dir_path.Sub(0, required_prefix_len);
	      msg = Text::printf("continuation path does not begin with \"%s\"",
				 required_prefix.cchars());
	    }
	  PrintVerboseContinuation(new_dir_path, continuation, !result,
				   msg.cchars());
	  break;
	}
      // If the continuation is a checkout session belonging to someone
      // else and we're supposed to stick to our own...
      else if(onlyMine && OthersCheckout(continuation))
	{
	  // Reject this continuation
	  PrintVerboseContinuation(new_dir_path, continuation, !result,
				   "checkout belongs to someone else and -only-mine was used");
	  break;
	}
      VestaSource *new_dir_vs = 0;
      try {
	new_dir_vs = ReposUI::filenameToVS(continuation);
      } catch (...) {
	// Ignore the exception.

	// Reject this continuation.
	PrintVerboseContinuation(new_dir_path, continuation, !result,
				 "doesn't exist");
      }
      if(new_dir_vs != 0)
	{
	  if(new_dir_vs->type == VestaSource::appendableDirectory)
	    {
	      // Follow this continuation
	      PrintVerboseContinuation(new_dir_path, continuation, !result);
	      new_dir_path = continuation;
	      new_ver_num = 0;
	      result = true;
	      delete cur_dir_vs;
	      cur_dir_vs = new_dir_vs;
	    }
	  else if(new_dir_vs->type == VestaSource::immutableDirectory)
	    {
	      // We allow a continuation to point to a specific
	      // version in a package/branch.  Make sure this meets
	      // necessary criteria.
	      VestaSource *new_dir_parent_vs = new_dir_vs->getParent();
	      if(new_dir_parent_vs == 0)
		{
		  // Couldn't get the parent?  Probably impossible.

		  // Reject this continuation.
		  PrintVerboseContinuation(new_dir_path, continuation, !result,
					   "couldn't get parent (deleted during vupdate?)");
		  delete new_dir_vs;
		  break;
		}
	      else if(new_dir_parent_vs->type != VestaSource::appendableDirectory)
		{
		  // Parent isn't an appendable directory.

		  // Reject this continuation.
		  PrintVerboseContinuation(new_dir_path, continuation, !result,
					   "parent of immutable dir isn't an appendable dir");
		  delete new_dir_parent_vs;
		  delete new_dir_vs;
		  break;
		}
	      int last_slash_pos = continuation.FindCharR('/');
	      if(last_slash_pos < 0)
		{
		  // No slashes?  Probably not possible, but if it is
		  // how can we check that the final arc is all
		  // digits?

		  // Reject this continuation.
		  PrintVerboseContinuation(new_dir_path, continuation, !result,
					   "couldn't find slash in path (should be impossible)");
		  delete new_dir_parent_vs;
		  delete new_dir_vs;
		  break;
		}
	      const char *final_arc = continuation.cchars()+last_slash_pos+1;
	      int final_arc_num;
	      if(!AllDigits(final_arc) ||
		 (sscanf(final_arc, "%d", &final_arc_num) != 1))
		{
		  // Final arc isn't a number.

		  // Reject this continuation.
		  PrintVerboseContinuation(new_dir_path, continuation, !result,
					   "final arc of continuation isn't a number");
		  delete new_dir_parent_vs;
		  delete new_dir_vs;
		  break;
		}

	      // This is definitely a numeric verison in an appendable
	      // directory, so follow this continuation.
	      PrintVerboseContinuation(new_dir_path, continuation, !result);
	      new_dir_path = continuation.Sub(0, last_slash_pos);
	      new_ver_num = final_arc_num;
	      result = true;
	      delete cur_dir_vs;
	      delete new_dir_vs;
	      cur_dir_vs = new_dir_parent_vs;
	    }
	  else
	    {
	      // Reject this continuation.  (If it's a ghost or
	      // non-master stub, we could search remote repositories,
	      // but currently vupdate only uses the local
	      // repository.)

	      // Reject this continuation.
	      PrintVerboseContinuation(new_dir_path, continuation, !result,
				       "not an immutable dir or appendable dir");
	      delete new_dir_vs;
	      break;
	    }
	}
      if(new_dir_vs == 0)
	{
	  break;
	}
    }
  delete cur_dir_vs;
  if ((verbose == Verbose) && result)
    {
      cout << endl;
    }
  return result;
}

// Quote any elements of a path that are reserved words in SDL or
// aren't legal identifiers or integers in SDL.
Text QuotePath(const Text &path)
{
  // This list of SDL reserved words was lifted from Lex.C in the
  // evaluator sources.
  static const char * reserved_words[] = 
    { "binding", "bool", "do", "else", "ERR", "FALSE", "files",
      "foreach", "from", "function", "if", "in", "import", "int",
      "list", "return", "text", "then", "TRUE", "type", "value" };
  static const unsigned int n_reserved_words =
    sizeof(reserved_words) / sizeof(reserved_words[0]);

  Text result;
  int pos = 0;
  while(pos < path.Length())
    {
      int end = path.FindChar('/', pos);
      if(end < 0)
	{
	  end = path.Length();
	}
      if(end > pos)
	{
	  // Extract the portion up to the next slash or the end of
	  // the string.
	  Text arc = path.Sub(pos, end-pos);
	  bool needs_quotes = false;
	  // Check against the reserved words
	  for(unsigned int i = 0; i < n_reserved_words; i++)
	    {
	      if(arc == reserved_words[i])
		{
		  needs_quotes = true;
		  break;
		}
	    }
	  if(!needs_quotes)
	    {
	      // Check for characters not legal in identifiers
	      const char *cur = arc.cchars();
	      while(*cur)
		{
		  if(!isalpha(*cur) && !isdigit(*cur) && (*cur != '.') && (*cur != '_'))
		    {
		      needs_quotes = true;
		      break;
		    }
		  cur++;
		}
	    }
	  if(needs_quotes)
	    {
	      result += Text::printf("\"%s\"", arc.cchars());
	    }
	  else
	    {
	      result += arc;
	    }
	  pos = end;
	}
      // Copy the separating slash to the result.
      if(path[pos] == '/')
	{
	  result += "/";
	  // Squash multiple slashes down to one (not that they should
	  // occur)
	  while(path[pos] == '/') pos++;
	}
    }
  return result;
}

// Count the number of double-quote characters in a string
static unsigned int CountQuotes(const Text &string)
{
  unsigned int result = 0;
  const char *cur = string.cchars();
  while(*cur)
    {
      if(*cur == '"') result++;
      cur++;
    }
  return result;
}

static bool NewVersion(const Import *imp, /*OUT*/ Text &newPath,
  /*OUT*/ Text &toWrite)
  throw (FS::DoesNotExist, FS::Failure, ParseImports::Error, 
	 VestaConfig::failure, SRPC::failure)
/* Given the import "imp", return "true" iff the import needs to be
   updated. If "true" is returned, "newPath" is set to the complete
   path of the new import, and "toWrite" is set to the portion of
   "newPath" that should be written to the new model. If "false" is
   returned, the values of "newPath" and "toWrite" are undefined.

   In the event that "imp" refers to a model in a checkout session, and
   if the checkout session has since been checked in, "NewVersion" will
   return "true" and set "newPath" and "toWrite" so as to refer to the
   latest version along the checked-in branch. */
{
    int prefixLen, suffixPos, verNum;
    bool checkedIn = false;
    char buff[20]; // for storing new version number

    // determine directory containing versions
    Text cur_path = imp->path;
    VersionDir(cur_path, /*OUT*/ prefixLen, /*OUT*/ suffixPos,
      /*OUT*/ verNum, /*INOUT*/ checkedIn, /*doStat=*/ true);
    Text verDir(cur_path.Sub(0, prefixLen));
    Text suffix(cur_path.Sub(suffixPos));

    bool continuationFollowed = false;
    if(followContinuations)
      {
	int required_prefix_len = FindRequiredPrefixLen(imp);
	Text continuedVerDir;
	int continuedVerNum;
	if(FollowContinuation(verDir, required_prefix_len,
			      continuedVerDir, continuedVerNum))
	  {
	    // Make sure continuedVerDir ends in a slash (as verDir does)
	    if(continuedVerDir[continuedVerDir.Length()-1] != '/')
	      continuedVerDir += "/";
	    cur_path = Text::printf("%s%d%s", continuedVerDir.cchars(),
				    continuedVerNum, suffix.cchars());
	    if(continuedVerDir.FindText("/checkout/") > 0)
	      {
		// We call VersionDir again in case continuedVerDir is
		// an exclusive session that has since been checked
		// in.
		checkedIn = false;
		VersionDir(cur_path, /*OUT*/ prefixLen, /*OUT*/ suffixPos,
			   /*OUT*/ verNum, /*INOUT*/ checkedIn, /*doStat=*/ true);
		verDir = cur_path.Sub(0, prefixLen);
		assert(cur_path.Sub(suffixPos) == suffix);
	      }
	    else
	      {
		verDir = continuedVerDir;
		prefixLen = verDir.Length();
		verNum = continuedVerNum;
	      }
	    continuationFollowed = true;
	  }
      }

    // update and/or advance imported checkout session if needed
    if (!checkedIn) RecurseOnCheckout(cur_path, suffix);

    // determine the new version
    int newVerNum = HighestVersion(verDir);

    // check for version in model that is too large
    if (verNum > newVerNum) {
        // If the '-attr' switch was used, it's quite possible that there
        // was no match, or that the match was to a smaller version number
        // than the existing import. In that case, simply return 'false'
        // to indicate that the import should not be updated.
        if (attrName != (char *)NULL) return false;

        // Otherwise, the existing import names a non-existant version
        // number, so report an error.
	Text msg("version number exceeds largest existing version:\n  ");
	msg += cur_path;
	throw ParseImports::Error(msg);
    }

    // ----------------------------------------------------------------------
    // Determine whether it has been checked out again by looking for
    // a new version stub.  This will fail if the path we'd be
    // updating to is still in a checkout session (because there
    // aren't ever any version stubs in checkout sessions).
    bool newest_is_checkout = false;
    Text l_session_path;
    int l_session_ver_num;
    if (toCheckout) {
	int printRes = sprintf(buff, "%d", newVerNum + 1);
	assert(printRes > 0);
	Text newVerTxt(buff);
	// Build the path of the stub
	Text l_stub_path = verDir + newVerTxt;
	// Look up the stub, if it exists
	VestaSource *l_stub = 0;
	// ReposUI::filenameToVS may throw an exception.  We need to
	// catch it if it does, but we really only care if we got
	// something.
	try
	  {
	    l_stub = ReposUI::filenameToVS(l_stub_path);
	  }
	catch (...) { }
	if (l_stub && l_stub->type == VestaSource::stub) {
	  const char *l_session_dir = l_stub->getAttrib("session-dir");
	  if(l_session_dir != NULL)
	    {
	      l_session_path = l_session_dir + Text('/');
	      // If we don't care about who this checkout belongs to
	      // or it belongs to us, we'll update to this. Also, we
	      // have to make sure that the session path starts with
	      // the right prefix, otherwise we won't be able to munge
	      // the model text in place.
	      if ((!onlyMine || !OthersCheckout(l_session_path)) &&
		  (l_session_path.Sub(0, prefixLen) == verDir))
		{
		  newest_is_checkout = true;
		  // Update/advance imported checkout session if
		  // needed.
		  RecurseOnCheckout(l_session_path, suffix);
		  l_session_ver_num = HighestVersion(l_session_path);
		}
	      delete [] l_session_dir;
	    }
	}
	if(l_stub != 0)
	  delete l_stub;
    }

    // return if the highest version is already specified
    if (newVerNum == verNum && !checkedIn && !newest_is_checkout &&
	!continuationFollowed) {
        return false;
    }
    assert((verNum < newVerNum && !checkedIn)
           || (verNum <= newVerNum && checkedIn)
	   || newest_is_checkout
	   || continuationFollowed);

    Text newVerTxt;
    if (newest_is_checkout) {
	// format "l_session_ver_num" as a text
	int printRes = sprintf(buff, "%d", l_session_ver_num);
	assert(printRes > 0);

	// The new version is everything after the prefix in the
	// session path, plus a path separator and the session version
	// number.
	newVerTxt = l_session_path.Sub(prefixLen) + buff;

	// set "newPath"
	newPath = verDir + newVerTxt + suffix;
    } else {
	// format "newVerNum" as a text
	int printRes = sprintf(buff, "%d", newVerNum); assert(printRes > 0);
	newVerTxt = buff;

	// set "newPath"
	newPath = verDir + newVerTxt + suffix;

	// If the user said "-only-mine", and we would be updating a
	// checkout session reference belonging to another user,
	// report that we don't have an update to make.
	if (onlyMine && OthersCheckout(newPath)) return false;
    }

    // set "toWrite" to the new path without the leading and trailing
    // portions.
    toWrite = newPath.Sub(imp->origOffset);
    int to_remove = (imp->path.Length()
		     // The suffix we want to remove is the path
		     // without the leading portion and the original
		     // text...
		     - imp->origOffset
		     - imp->orig.Length()
		     // ...but imp->orig may include quotes that were
		     // removed in imp->path, so we need to account
		     // for those
		     + CountQuotes(imp->orig));
    if(to_remove > 0)
      {
	toWrite = toWrite.Sub(0, toWrite.Length() - to_remove);
      }
    // Be sure to quote any path components that are SDL reserved
    // words or not legal identifiers.
    toWrite = QuotePath(toWrite);
    return true;
} // end NewVersion

static void RenameTempFile(const Text &temp, const Text &orig)
  throw (FS::DoesNotExist, FS::Failure)
{
    // first, try using rename(2)
    if (rename(temp.cchars(), orig.cchars()) != 0) {
	if (errno == EXDEV) {
	    // in event of "cross-device link" error, copy instead
	    ifstream ifs;
	    ofstream ofs;
	    FS::OpenReadOnly(temp, /*OUT*/ ifs);
	    FS::OpenForWriting(orig, /*OUT*/ ofs);
	    CopyFromTo(ifs, ofs);
	    FS::Close(ifs);
	    FS::Close(ofs);

	    // delete temp file
	    (void)unlink(temp.cchars());
	} else {
	    throw FS::Failure("rename(2)", temp + " " + orig);
	}
    }
} // RenameTempFile

static Text TempSuffix()
{
  char pidBuff[40];
  FP::Tag uid = UniqueId();
  int sres = sprintf(pidBuff, "-%016" FORMAT_LENGTH_INT_64 "x-%016" FORMAT_LENGTH_INT_64 "x~",
		     uid.Word0(), uid.Word1());
  assert(sres > 0);
  assert(sres < sizeof(pidBuff));

  return pidBuff;
}

/* Return the name of a temporary file, and open "ofs" on that file. This
   function guarantees that a unique filename is chosen across an entire
   Vesta site by using the "UniqueId" interface. */
static Text OpenTempFile(/*OUT*/ ofstream &ofs) throw (FS::Failure)
{
    char pidBuff[40];
    Text prefix = (VestaConfig::get_Text("UserInterface", "TempDir") +
		   "/vup");
    // Pick a unique temporary filename
    Text res = FS::TempFname(prefix, TempSuffix);
    // Open it.
    FS::OpenForWriting(res, /*OUT*/ ofs);
    // Return the filename.
    return res;
}

static bool LimiterMatch(const Text &p_path) throw ()
/* Return true iff 'p_path' contains all of the strings in the command-line
   'limiters' as substrings. */
{
    for (int i = 0; i < limiters.size(); i++) {
        if (p_path.FindText(limiters.get(i)) < 0) return false;
    }
    return true;
}

static void Update(const Text &model, /*INOUT*/ TextSeq &localModels)
  throw (FS::DoesNotExist, FS::Failure, ParseImports::Error, 
	 VestaConfig::failure, SRPC::failure)
/* Update the model named "model", which is expected to be an absolute path.
   If "doWork" is "true", then the model file is rewritten on disk. If "query"
   is true, then "doWork" is required to be true, and the program prompts the
   user on each query. If "verbose != Quiet", then messages are printed
   indicating the old/new versions of any updated imports. If a model does
   not have any updates, its name is printed only if "verbose == Verbose."
   As a side-effect (to support recursive updates), the names of any
   local models imported by "model" are appended to "localModels".

   Implementation: This function takes care of reading and parsing the
   input model, communicating update information to the user, and
   writing the revised model. The work of determining which non-local
   models require updating and the path to update to is handled by the
   NewVersion function above. */
{
    ImportSeq imports(/*sizehint=*/ 10);
    ParseImports::P(model, /*INOUT*/ imports);

    // open input file and temporary output file
    ifstream ifs;
    ofstream ofs;
    Text newModel;
    if (doWork) {
	FS::OpenReadOnly(model, /*OUT*/ ifs);
	newModel = OpenTempFile(/*OUT*/ ofs);
    }

    try {
	// write new version
	bool modelPrinted = false;
	if (verbose == Verbose) {
	    cout << model << endl;
	    modelPrinted = true;
        }
	bool modelChanged = false; // monotonically increasing only
	while (imports.size() > 0) {
	    // skip local imports
	    Import imp(*(imports.getlo()));
	    delete imports.remlo();
	    if (imp.local) {
		localModels.addhi(imp.path);
		continue;
	    }

	    // skip imports preceeded by "noupdate" pragma
	    if (imp.noUpdate) continue;

	    // skip imports that don't match the limiting strings (if any)
	    if (!LimiterMatch(imp.path)) continue;

	    // If the user said "-only-mine", and this import
	    // currently points into a checkout session reference
	    // belonging to another user, don't modify it.
	    if (onlyMine && OthersCheckout(imp.path)) continue;

	    // copy part of input file if necessary
	    if (doWork) {
		CopyFromTo(ifs, ofs, imp.start);
		FS::Seek(ifs, imp.end);
	    }

	    try
	      {
		// determine replacement for "imp.path"
		Text newPath, pathToWrite;
		if (NewVersion(&imp, /*OUT*/ newPath, /*OUT*/ pathToWrite)) {
		  // write report if necessary
		  if (verbose != Silent) {
		    if (!modelPrinted) {
		      cout << model << endl;
		      modelPrinted = true;
		    }
		    cout << "     " << imp.path << endl;
		    cout << "  -> " << newPath << endl;
		  }

		  // write new path
		  if (doWork) {
		    bool useNewVersion = true;
		    if (query) {
		      cout << "  Update this import (y/n)? "; cout.flush();
		      const int BuffLen = 20; char buff[BuffLen];
		      cin.getline(buff, BuffLen);
		      useNewVersion = (buff[0] == 'y');
		    }
		    ofs << (useNewVersion ? pathToWrite : imp.orig);

		    // note change was made
		    if (useNewVersion) modelChanged = true;
		  }
		  //if (verbose != Silent) cout << endl;
		} else {
		  // copy original path
		  if (doWork) ofs << imp.orig;
		}
	      }
	    // In the event of the kind of exception thrown by
	    // VersionDir...
	    catch (const ParseImports::Error &err)
	      {
		// If these should be fatal, re-throw the exception.
		if(parse_errors_stop)
		  {
		    throw;
		  }
		else
		  {
		    // Report the parsing problem
		    cerr << "vupdate: " << err << endl;

		    // copy original path
		    if (doWork) ofs << imp.orig;
		  }
	      }
	    // Any othe exceptions we just pass on
	    catch(...)
	      {
		throw;
	      }
	}
	if (doWork) {
	    // copy the tail portion of the file
	    if (modelChanged) CopyFromTo(ifs, ofs);

	    // close files and rename or delete temp file
	    FS::Close(ifs);
	    FS::Close(ofs);
	    if (modelChanged) {
		RenameTempFile(newModel, model);
	    } else {
		// delete temp file
		(void)unlink(newModel.cchars());
	    }
	}
    }
    catch (...) {
	// in the event of a FS exception, delete the temp file
	(void)unlink(newModel.cchars());
	// Free any remaining Import objects.
	while (imports.size() > 0)
	  delete imports.remlo();
	throw;
    }
} // Update

void CheckExclusive(/*INOUT*/ bool &exclusive) throw ()
/* If "exclusive" is already "true", invoke "Syntax" with the correct error
   message. Otherwise, set "exclusive" to "true". */
{
    if (exclusive) {
	Syntax("more than one mutually exclusive argument specified");
    }
    exclusive = true;
}


int main(int argc, char *argv[]) 
{
    // parse command-line
    bool nqsExclusive = false, svExclusive = false;
    int arg;
    for (arg = 1; arg < argc && *argv[arg] == '-'; arg++) {
        char *argp = argv[arg];
	if (strncmp(argp, "--", 2) == 0) argp++;
	if (strcmp(argp, "-no-action") == 0 ||
	    strcmp(argp, "-n") == 0) {
	    CheckExclusive(/*INOUT*/ nqsExclusive);
	    doWork = false;
	} else if (strcmp(argp, "-query") == 0 ||
		   strcmp(argp, "-q") == 0) {
	    CheckExclusive(/*INOUT*/ nqsExclusive);
	    query = true;
	} else if (strcmp(argp, "-silent") == 0 ||
		   strcmp(argp, "-s") == 0) {
	    CheckExclusive(/*INOUT*/ nqsExclusive);
	    CheckExclusive(/*INOUT*/ svExclusive);
	    verbose = Silent;
	} else if (strcmp(argp, "-update-local") == 0 ||
		   strcmp(argp, "-r") == 0) {
	    update_local = true;
	} else if (strcmp(argp, "-update-checkout") == 0 ||
		   strcmp(argp, "-c") == 0) {
	    update_co = true;
	    advance_co = true;
	} else if (strcmp(argp, "-update-all") == 0 ||
		   strcmp(argp, "-u") == 0) {
	    update_local = true;
	    update_co = true;
	    advance_co = true;
	} else if (strcmp(argp, "-verbose") == 0 ||
		   strcmp(argp, "-v") == 0) {
	    CheckExclusive(/*INOUT*/ svExclusive);
	    verbose = Verbose;
	} else if (strcmp(argp, "-advance-checkout") == 0 ||
		   strcmp(argp, "-a") == 0) {
	    advance_co = true;
	} else if (strcmp(argp, "-limit") == 0 ||
		   strcmp(argp, "-l") == 0) {
	    // Move up one argument to get the substring argument
	    arg++;
	    // As long as we haven't run out of arguments...
	    if (arg < argc) {
		// Add this string to the list of limiters.
		limiters.addhi(Text(argv[arg]));
	    } else {
		// Complain that the user didn't give us an argument
		Syntax("-limit requires a string argument");
	    }
	} else if (strcmp(argp, "-limit-checkout") == 0 ||
		   strcmp(argp, "-L") == 0) {
	    // Add the string "/checkout/" to the list of limiters.
	    // (Sort of a hack, but the vupdate documentation already
	    // says it makes this assumption about checkout directories.)
	    limiters.addhi("/checkout/");
	} else if (strcmp(argp, "-only-mine") == 0 ||
		   strcmp(argp, "-m") == 0) {
	    onlyMine = true;
	} else if (strcmp(argp, "-to-checkout") == 0 ||
		   strcmp(argp, "-t") == 0) {
	    toCheckout = true;
	} else if (strcmp(argp, "-attr") == 0 ||
		   strcmp(argp, "-A") == 0) {
	    // Move up one argument to get the substring argument
	    arg++;
	    // As long as we haven't run out of arguments...
	    if (arg < argc) {
	        // read attribute name
		char *colon = index(argv[arg], ':');
		char *circum = index(argv[arg], '^');
		if (colon != (char *)NULL && circum != (char *)NULL) {
		    Syntax("illegal argument to '-attr' switch");
                }
		char *nextVal = (char *)NULL;
		if (colon != (char *)NULL) {
                    *colon = '\0';
                    attrMatchSense = true;
		    nextVal = colon + 1;
                } else if (circum != (char *)NULL) {
                    *circum = '\0';
                    attrMatchSense = false;
		    nextVal = circum + 1;
                }
                attrName = argv[arg];

                // parse attribute values (if any)
                if (nextVal != NULL) {
		    attrVals = NEW(TextSeq);
                    char *lastChar = nextVal + strlen(nextVal);
		    *lastChar = ','; // sentinel
                    while (nextVal <= lastChar) {
		    	char *end = index(nextVal, ',');
                    	*end++ = '\0'; // replace ',' with '\0'; advance
                        attrVals->addhi(Text(nextVal));
                        nextVal = end;
                    }
                }
	    } else {
		// Complain that the user didn't give us an argument
		Syntax("-attr requires an attr-spec argument");
	    }
	} else if (strcmp(argp, "-parse-errors-stop") == 0 ||
		   strcmp(argp, "-e") == 0) {
	    parse_errors_stop = true;
	} else if (strcmp(argp, "-follow-continuations") == 0 ||
		   strcmp(argp, "-f") == 0) {
	   followContinuations = true;
        } else {
	    Syntax("illegal argument", argv[arg]);
	}
    }
    if (toCheckout && attrName != (char *)NULL) {
        Syntax("-to-checkout and -attr cannot both be specified");
    }
    if (arg < argc - 1) Syntax("too many arguments");
    Text model((arg < argc) ? argv[arg] : ".main.ves");

    // get the working directory
    char wd_buff[PATH_MAX+1];
    char *res = getcwd(wd_buff, PATH_MAX+1); assert(res != (char *)NULL);
    res = strcat(wd_buff, "/"); assert(res != (char *)NULL);

    // scan the model
    try {
	// update the specified model
	TextSeq localModels(/*sizeHint=*/ 10);
	Update(ParseImports::ResolvePath(model, Text(wd_buff)),
          /*INOUT*/ localModels);

	// if "-r" was specified, update imported local models also
	if (update_local) {
	    while (localModels.size() > 0) {
		model = localModels.remlo();
		Update(model, /*INOUT*/ localModels);
	    }
	}
    } catch (FS::DoesNotExist) {
	cerr << "vupdate: model file does not exist" << endl;
	exit(2);
    } catch (const FS::Failure &f) {
	cerr << "vupdate: " << f << endl;
	exit(2);
    } catch (const ParseImports::Error &err) {
	cerr << "vupdate: " << err << endl;
	exit(2);
    } catch (const VestaConfig::failure &f) {
	cerr << "vupdate: " << f.msg << endl;
	exit(2);
    } catch (const SRPC::failure &f) {
        cerr << "vupdate: error contacting Vesta repository: " << f.msg <<endl;
	exit(2);
    }

    // print warning message if "-n" specified
    if (!doWork && verbose) {
        cout << "WARNING: At your request, nothing was updated!" << endl;
    }

    if(vadvance_error_count > 0)
      {
	exit(3);
      }

    return 0;
}
