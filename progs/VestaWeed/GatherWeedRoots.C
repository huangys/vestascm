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

// Created on Fri Feb 28 15:27:07 PST 1997 by heydon

// Unix includes
#include <sys/types.h>
#if defined(__linux__) && !defined(__GNUC__)
// For some reason, the Linux sys/types.h only defines int64_t under
// the GNU compilers.  We need it for subsequent system includes.
typedef long int int64_t;
#endif
#include <sys/param.h> // for MAXPATHLEN
#include <fnmatch.h>
#include <string.h>
#include <strings.h>

// Vesta includes
#include <Basics.H>
#include <Table.H>
#include <FS.H>
#include <FP.H>
#include <SourceOrDerived.H> // for NullShortId
#include <VestaSource.H>
#include <ReadConfig.H>
#include <Debug.H>
#include "PkgBuild.H"
#include "CommonErrors.H"
#include "RootTbl.H"
#include "GatherWeedRoots.H"

using std::ifstream;
using std::cout;
using std::cerr;
using std::endl;

// FPPkgTbl
typedef Table<FP::Tag,const PkgBuild*>::Default FPPkgTbl;
typedef Table<FP::Tag,const PkgBuild*>::Iterator FPPkgIter;

// maximum input line length
static const int MaxPatternLen = MAXPATHLEN;
static const int MaxPathLen = MAXPATHLEN;

// constants
static const char *FirstStr = "FIRST";
static const char *LastStr = "LAST";
static const char Pound = '#';        // comment character
static const char Plus = '+';         // char for adding to set
static const char Minus = '-';        // char for removing from set
static const char OpenBracket = '[';  // start of interval pattern
static const char CloseBracket = ']'; // end of interval pattern
static const char Slash = '/';        // pathname separator
static const char Null = '\0';        // end-of-string character
static const char OpenCurly = '{';    // start of alternative set
static const char CloseCurly = '}';   // end of alternative set
static const char Comma = ',';        // separator in interval/alternative set
static const FP::Tag EmptyFP("");     // dummy fingerprint

enum Op { Add, Remove };

// Global variables -----------------------------------------------------------

// true iff "echoModels" argument specified
static bool GatherWeedRoots_printModelNames;

// number of models in weeder instructions printed
static int GatherWeedRoots_instructionModelCnt;

// table mapping FP(pathname) -> PkgBuild
static FPPkgTbl *GatherWeedRoots_pkgTbl = (FPPkgTbl *)NULL;

// repository root (appendable)
static VestaSource *GatherWeedRoots_reposRoot = (VestaSource *)NULL;

// name of appendable root
static Text GatherWeedRoots_rootName;
static int GatherWeedRoots_rootNameLen; // length of "rootName"

static inline bool IsEmpty(const char *str) throw ()
/* Return true iff "str" is the empty string. */
{ return *str == Null; }

// Error message printing -----------------------------------------------------

static void GatherWeedRoots_MaxPathLenErr() throw ()
{
    cerr << "Fatal error: built-in constant 'MaxPathLen' too small; ";
    cerr << "exiting..." << endl;
    exit(1);
}

// Model file processing ------------------------------------------------------

static PkgBuild* GatherWeedRoots_GetPkgBuild(VestaSource *vs,
  const FP::Tag &parentFP, VestaSource::typeTag parentType)
  throw (ReposError, SRPC::failure)
/* Allocate and return a new "PkgBuild" structure corresponding to the
   source "vs" whose parent directory has fingerprint "parentFP" and type
   "parentType". Returns NULL if "vs" is not an immutable file or "parentType"
   is not an immutable directory. */
{
    if (vs->type != VestaSource::immutableFile
        || parentType != VestaSource::immutableDirectory) {
	return (PkgBuild *)NULL;
    }
    ShortId shortId = vs->shortId();
    assert(shortId != NullShortId);
    return NEW_PTRFREE_CONSTR(PkgBuild, (parentFP, shortId));
} // GatherWeedRoots_GetPkgBuild

static void GatherWeedRoots_HandleFile(Op op, VestaSource *vs,
  const char *filename, const FP::Tag &parentFP,
  VestaSource::typeTag parentType)
  throw (ReposError, SRPC::failure)
/* Add or remove the file "vs" named by "filename" to/from "pkgTbl" according
   to "op", where the parent directory of "vs" has fingerprint "parentFP" and
   type "parentType". */
{
    if (GatherWeedRoots_printModelNames) {
	cout << "  ";
	cout << ((op == Add) ? Plus : Minus) << ' ' << filename << endl;
	GatherWeedRoots_instructionModelCnt++;
    }
    FP::Tag fp(filename);
    switch (op) {
      case Add:
	// only add an association if none has already been established
	const PkgBuild *pkg;
	if (!(GatherWeedRoots_pkgTbl->Get(fp, /*OUT*/ pkg))) {
	    pkg = GatherWeedRoots_GetPkgBuild(vs, parentFP, parentType);
	    if (pkg != (PkgBuild *)NULL) {
		bool inTbl = GatherWeedRoots_pkgTbl->Put(fp, pkg);
		assert(!inTbl);
	    } else {
		cerr << "Warning: ignoring '" << filename << "'" << endl;
	    }
	}
	break;
      case Remove:
	const PkgBuild *dummy;
	(void) GatherWeedRoots_pkgTbl->Delete(fp, /*OUT*/ dummy);
	break;
      default:
	assert(false);
    }
} // GatherWeedRoots_HandleFile

// Pattern matching -----------------------------------------------------------

static int GatherWeedRoots_DecimalValue(/*INOUT*/ const char* &str,
  bool allowSuffix = false) throw ()
/* If "str" is a string consisting entirely of decimal digits that does not
   begin with any superfluous leading zeroes, return the value of the digits
   (interpreted as a base-10 number). Otherwise, return -1. If "allowSuffix"
   is "true", then "str" must just have a string of decimal digits as its
   prefix. In any case "str" will be set to point to the first character after
   the prefix of decimal digits. */
{
    const char *init = str;
    int res = 0;
    while (isdigit(*str)) {
	res = (10 * res) + (*str++ - '0');
    }
    if (init == str) return -1; // empty sequence of digits
    if (*init == '0' && (str - init) > 1) return -1; // extra leading zero
    return ((IsEmpty(str) || allowSuffix) ? res : -1);
} // GatherWeedRoots_DecimalValue

struct FirstLastState {
    VestaSource *parentVS;
    char *path;
    int first;
    int last;
};

static bool lookup_child(VestaSource *parent, Arc arc, unsigned int index,
			 /*OUT*/ VestaSource *&child, char *path = "")
  throw (ReposError, SRPC::failure)
{
  child = 0;
  VestaSource::errorCode errCode;
  errCode = parent->lookupIndex(index, /*OUT*/ child);
  if (errCode == VestaSource::longIdOverflow) {
    return true;
  } else if (errCode == VestaSource::notFound) {
    // Maybe an object was replaced while we were listing
    // (e.g. ghost/stub -> real object or real object -> ghost)?
    // Try a lookup by name.
    errCode = parent->lookup(arc, /*OUT*/ child);
    if (errCode == VestaSource::longIdOverflow) {
      return true;
    } else if (errCode != VestaSource::ok) {
      throw ReposError(errCode, "VestaSource::lookup", path);
    }
  } else if (errCode != VestaSource::invalidArgs) {
    // Maybe an enclosing directory was ghosted?  Only continue
    // listing if this directory is still valid.
    return parent->longid.valid();
  } else if (errCode != VestaSource::ok) {
    throw ReposError(errCode, "VestaSource::lookupIndex", path);
  }
  assert(child != 0);
  return true;
}

static bool GatherWeedRoots_FirstLastCallback(void *arg,
  VestaSource::typeTag type, Arc arc, unsigned int index,
  Bit32 pseudoInode, ShortId filesid, bool master) 
    throw (ReposError, SRPC::failure)
/* This is the callback function past to the "list" method in
   "GetFirstLast" below. For each directory entry it is passed,
   it updates its "FirstLastState" argument's "first" and "last"
   fields if "arc" is a decimal value and if the corresponding
   "VestaSource" is an immutable or appendable directory. */
{
    FirstLastState *s = (FirstLastState *)arg;

    const char *dummy_name = arc;
    int value = GatherWeedRoots_DecimalValue(/*INOUT*/ dummy_name);
    if (value >= 0) {
	// only update values if it this entry is a directory
	VestaSource *childVS;
	if(!lookup_child(s->parentVS, arc, index, /*OUT*/ childVS, s->path))
	  {
	    // lookup_child determined that we can't continue
	    // (e.g. this directory has been ghosted)
	    return false;
	  }
	if(childVS == 0)
	  {
	    // We couldn't process this item, but maybe we can still
	    // handle others in this directory
	    return true;
	  }
	if (childVS->type == VestaSource::immutableDirectory
            || childVS->type == VestaSource::appendableDirectory) {
	    s->first = (s->first == -1) ? value : min(s->first, value);
	    s->last = (s->last == -1) ? value : max(s->last, value);
	}
    }
    return true;
} // GatherWeedRoots_FirstLastCallback

static void GatherWeedRoots_GetFirstLast(VestaSource *vs, char *path,
  /*OUT*/ int &first, /*OUT*/ int &last) throw (ReposError, SRPC::failure)
/* Sets "first" and "last" to the minimum and maximum values, respectively,
   of entries in the directory "vs" (whose corresponding pathname is "path")
   whose names consist entirely of decimal digits and that are directories.
   If there are no such child entries, "first" and "last" are set to "-1". */
{
    // initialize closure for callback
    FirstLastState s;
    s.parentVS = vs;
    s.path = path;
    s.first = s.last = -1;

    // invoke callback on each directory entry
    VestaSource::errorCode errCode;
    errCode = vs->list(/*firstIndex=*/ 0,
      GatherWeedRoots_FirstLastCallback, (void*)(&s));
    if ((errCode != VestaSource::ok) &&
	// If this directory or an enclosing one are ghosted while
	// we're scanning, we could get notFound
	(errCode != VestaSource::notFound))
      {
	throw ReposError(errCode, "VestaSource::list", path);
      }

    // return results from callback state
    first = s.first;
    last = s.last;
} // GatherWeedRoots_GetFirstLast

static char* GatherWeedRoots_ReplaceFirstLast(VestaSource *vs, char *path,
  char *pattern) throw (ReposError, SRPC::failure)
/* Return a version of "pattern" in which all occurrences of the special
   strings "FIRST" and "LAST" in "pattern" have been replaced by the
   names of the entries in the directory "dirPath" consisting entirely of
   decimal digits (with no leading zeroes) whose values are the smallest and
   largest, respectively. Assumes that "dirPath" does in fact name a
   directory. "end" points to the end of "dirPath", and "rem" is the number of
   characters remaining in the buffer pointed to by "dirPath". "dirPath" is
   assumed not to end in a slash unless it is the root directory. On exit,
   the string pointed to by "dirPath" must be unchanged. */
{
    // return if neither FIRST nor LAST occurs in "pattern"
    if (strstr(pattern, FirstStr) == (char *)NULL &&
	strstr(pattern, LastStr) == (char *)NULL)
	return pattern;

    // determine "first" and "last" numerical values
    int first, last;
    GatherWeedRoots_GetFirstLast(vs, path, /*OUT*/ first, /*OUT*/ last);

    // replace all occurrences of FIRST and LAST in "pattern"
    Text res;
    while (!IsEmpty(pattern)) {
	// find occurrence of each string
	char *firstLoc = strstr(pattern, FirstStr);
	char *lastLoc = strstr(pattern, LastStr);

	// set "minLoc" to the first occurrence of either
	char *minLoc;
	if (firstLoc == (char *)NULL) minLoc = lastLoc;
	else if (lastLoc == (char *)NULL) minLoc = firstLoc;
	else minLoc = (firstLoc < lastLoc) ? firstLoc : lastLoc;

	// set "value", "skipLen" to the matched value & its length
	int value = -1, skipLen = 0;
	if (minLoc == (char *)NULL) {
	    minLoc = index(pattern, Null); // set "minLoc" to end of string
	} else {
	    if (minLoc == firstLoc) {
		value = first; skipLen = strlen(FirstStr);
            } else {
		value = last; skipLen = strlen(LastStr);
	    }
	    assert(skipLen > 0);
	}

	// copy characters up to the match
	if (minLoc > pattern) {
	    res += Text(pattern, minLoc - pattern);
	}
	pattern = (minLoc + skipLen);

	// copy the digits for "value";
	if (value >= 0) {
	    char buff[20];
	    int spres = sprintf(buff, "%d", value);
	    assert(spres >= 0);
	    res += buff;
	} else if (skipLen > 0) {
	    // no numbered directories; fill in string that
	    // won't match anything
	    res += "_NO_MATCH_";
	}
    }
    return res.chars();
} // GatherWeedRoots_ReplaceFirstLast

static int GatherWeedRoots_ParseExpr(/*INOUT*/ const char* &curr)
  throw (InputError)
{
    int res, sign = 0;
    while (isdigit(*curr)) {
	// read next number
	int num =
          GatherWeedRoots_DecimalValue(/*INOUT*/ curr, /*allowSuffix=*/ true);
	if (num < 0) {
	    throw InputError("expected number in interval expression");
	}

	// incorporate it into the result
	if (sign == 0) res = num;
	else res += (sign * num);

	// look for '+' or '-' to continue
	if (*curr == Plus) { sign = 1; curr++; }
	else if (*curr == Minus) { sign = -1; curr++; }
    }
    return res;
} // GatherWeedRoots_ParseExpr

static char* GatherWeedRoots_ReplaceInterval(const char *pattern)
  throw (SysError, InputError)
/* Return a version of "pattern" in which all interval expressions have been
   replaced by a set expression representing the integer values of the
   interval. For example, the pattern ``A[8,12]B'' is converted to the
   expression ``A{8,9,10,11,12}B''. */
{
    Text res;
    while (!IsEmpty(pattern)) {
	char *intvStart = index(pattern, OpenBracket);
	char *intvEnd, *comma;
	int low, high;
	if (intvStart != (char *)NULL) {
	    comma = index(intvStart, Comma);
	    intvEnd = index(intvStart, CloseBracket);
	    // test for an interval expression
	    if (comma != NULL && intvEnd != NULL &&
                intvStart < comma && comma < intvEnd &&
                intvStart+1 != comma && comma != intvEnd-1) {
		// compute "low" and "high" values
		const char *curr = intvStart + 1;
		low = GatherWeedRoots_ParseExpr(/*INOUT*/ curr);
		if (*curr++ != Comma) {
		    throw InputError("expected comma in interval");
		}
		high = GatherWeedRoots_ParseExpr(/*INOUT*/ curr);
		if (*curr++ != CloseBracket) {
		    throw InputError("expected ']' at end of interval");
		}
	    } else {
		// this is the case of a normal character set expression
		if (intvEnd == NULL) {
		    throw InputError("no matching ']' in pattern");
		}
	    }
	} else {
	    intvStart = index(pattern, Null); // set start to end of string
	}

	// copy the characters up to the match
	if (intvStart > pattern) {
	    res += Text(pattern, intvStart - pattern);
	}

	// add expression for interval
	if (IsEmpty(intvStart)) {
	    pattern = intvStart;
	} else {
	    pattern = intvEnd + 1;
	    if (low <= high) {
		// non-empty interval
		const int MaxNumLen = 20;
		// Allocate temporary buffer:
		//   2 chars for '{' and '}'
		//   MaxNumLen chars for each number in [ low, high ]
		//   1 char for each comma separating the numbers
		char *buff = NEW_PTRFREE_ARRAY(char,
					       2 + ((MaxNumLen+1) * (high-low+1)));
		char *curr = buff;
		*curr++ = OpenCurly;
		for (int i = low; i <= high; i++) {
		    char numBuff[MaxNumLen];
		    int spres = sprintf(numBuff, "%d", i);
		    assert(spres >= 0);
		    if (strcpy(curr, numBuff) == NULL)
			throw SysError("strcpy(3)");
		    curr += strlen(numBuff);
		    if (i < high) *curr++ = Comma;
		}
		*curr++ = CloseCurly;
		res += Text(buff, curr - buff);
	    } else {
		// empty interval: fill in a string that won't match anything
		res += "_NO_MATCH_";
	    }
	}
    }
    return res.chars();
} // GatherWeedRoots_ReplaceInterval

static int GatherWeedRoots_CurlyMatchHelper(char *buff, char *end,
  const char *pattern, const char *name) throw (InputError, SysError)
{
    // test for base case
    char *openCurly, *closeCurly; // both pointers into "pattern"
    if ((openCurly = index(pattern, OpenCurly)) == NULL) {
	if (strcpy(end, pattern) == NULL) throw SysError("strcpy(3)");
	return fnmatch(buff, name, FNM_PERIOD);
    }

    // check for closing curly
    if ((closeCurly = index(openCurly+1, CloseCurly)) == NULL) {
	throw InputError("opening '{' not matched by a closing '}'");
    }

    // copy prefix before curly into "buff"
    int len = openCurly - pattern;
    if (strncpy(end, pattern, len) == NULL) throw SysError("strncpy(3)");
    end += len;

    // iterate and recurse
    int res = FNM_NOMATCH;
    char *curr; // pointer into "pattern", between "openCurly" & "closeCurly"
    for (curr = openCurly + 1; (curr-1) != closeCurly; curr++) {
	char *wdEnd = index(curr, Comma);
	if (wdEnd == (char *)NULL || wdEnd > closeCurly) wdEnd = closeCurly;
	int len = wdEnd - curr;
	if (strncpy(end, curr, len) == NULL) throw SysError("strncpy(3)");
	res = GatherWeedRoots_CurlyMatchHelper(buff,
          end + len, closeCurly + 1, name);
	if (res != FNM_NOMATCH) return res;
	curr = wdEnd;
    }
    return res;
} // GatherWeedRoots_CurlyMatchHelper

static int GatherWeedRoots_CurlyMatch(const char *pattern, const char *name)
  throw (SysError, InputError)
/* Return the result of calling "fnmatch(pattern', name)" for all patterns
   "pattern'" obtained by replacing instances of "{...}" in "pattern" by the
   listed strings. If any replacement produces a match, return 0. If all
   replacements return FNM_MATCH, return FNM_MATCH. If any other value is
   returned, return it immediately. */
{
    // handle no-work case
    if (index(pattern, OpenCurly) == NULL) {
	return fnmatch(pattern, name, FNM_PERIOD);
    }

    // allocate a buffer into which the instantiated pattern will be copied
    /* To be conservative, we allocate a buffer as large as the pattern. This
       is safe because it is impossible for an instantiation of "{...}" to be
       longer than the pattern itself. */
    char *buff = NEW_PTRFREE_ARRAY(char, strlen(pattern));
    return GatherWeedRoots_CurlyMatchHelper(buff, buff, pattern, name);
} // GatherWeedRoots_CurlyMatch

// Search ---------------------------------------------------------------------

// forward declaration
static void GatherWeedRoots_SearchPath(Op op, VestaSource *vs, char *path,
  char *end, int rem, char *rest, const FP::Tag &parentFP,
  VestaSource::typeTag parentType)
  throw (SysError, ReposError, InputError, SRPC::failure);

struct SearchDirState {
    Op op;
    VestaSource *parentVS;
    char *path;
    char *end;
    int rem;
    char *dirPattern;
    char *rest;
};

static bool GatherWeedRoots_SearchDirCallback(void *closure,
  VestaSource::typeTag type, Arc arc, unsigned int index,
  Bit32 pseudoInode, ShortId filesid, bool master)
  throw (SysError, ReposError, InputError, SRPC::failure)
/* This is the callback function passed to VestaSource::list by the
   "SearchDir" function below. It processes a single directory entry. */
{
    // "closure" is actually of type "SearchDirState *"
    SearchDirState *s = (SearchDirState *)closure;

    // compare name of directory entry to "dirPattern"
    int match = GatherWeedRoots_CurlyMatch(s->dirPattern, arc);
    switch (match) {
      case 0: // match
	{ // check that there's enough space in the buffer
	  int namlen = strlen(arc);
	  if (namlen >= s->rem) GatherWeedRoots_MaxPathLenErr();

	  // append entry name to "s->path"
	  if (strcpy(s->end, arc) == (char *)NULL) {
	      throw SysError("strcpy(3)");
	  }

	  // get the "VestaSource" object for "arc" in "thisVS"
	  VestaSource *childVS;
	  if(!lookup_child(s->parentVS, arc, index, /*OUT*/ childVS, s->path))
	    {
	      // lookup_child determined that we can't continue
	      // (e.g. this directory has been ghosted)
	      return false;
	    }
	  if(childVS == 0)
	    {
	      // We couldn't process this item, but maybe we can still
	      // handle others in this directory
	      return true;
	    }

	  // search recursively
	  GatherWeedRoots_SearchPath(s->op, childVS, s->path,
	    s->end + namlen, s->rem - namlen, s->rest, s->parentVS->fptag,
            s->parentVS->type);
        }
	break;
      case FNM_NOMATCH:
	break;
      default:
	throw SysError("fnmatch(3)");
    }
    return true;
} // GatherWeedRoots_SearchDirCallback

static void GatherWeedRoots_SearchDir(Op op, VestaSource *vs, char *path,
  char *end, int rem, char *rest, const FP::Tag &parentFP)
  throw (SysError, ReposError, InputError, SRPC::failure)
/* Like "SearchPath" below, but "vs" is known to be an immutable or
   appendable directory. */
{
    // isolate pattern for this directory
    char *restStart = rest;
    char *dirPattern;
    if ((rest = index(restStart, Slash)) != NULL) {
	int dirPatternLen = rest - restStart;
	dirPattern = NEW_PTRFREE_ARRAY(char, dirPatternLen+1);
	if (strncpy(dirPattern, restStart, dirPatternLen) == NULL) {
	    throw SysError("strncpy(3)");
	}
	dirPattern[dirPatternLen] = Null; // terminate pattern
	rest++;       // advance past Slash to start of next pattern
    } else {
	dirPattern = restStart;
	rest = restStart + strlen(restStart); // set "rest" to end of pattern
    }

    // handle "FIRST", "LAST", "[expr,expr]" in "dirPattern"
    dirPattern = GatherWeedRoots_ReplaceFirstLast(vs, path, dirPattern);
    dirPattern = GatherWeedRoots_ReplaceInterval(dirPattern);

    // append slash to "path" if necessary
    assert(*(end-1) != Slash);
    if (rem == 0) GatherWeedRoots_MaxPathLenErr();
    *end++ = Slash; *end = Null;
    rem--;

    // initialize state for callback
    SearchDirState s;
    s.op = op;
    s.parentVS = vs;
    s.path = path;
    s.end = end;
    s.rem = rem;
    s.dirPattern = dirPattern;
    s.rest = rest;

    // enumerate directory, matching each entry against "dirPattern"
    VestaSource::errorCode errCode;
    errCode = vs->list(/*firstIndex=*/ 0,
      GatherWeedRoots_SearchDirCallback, (void *)(&s));
    if ((errCode != VestaSource::ok) &&
	// If this directory or an enclosing one are ghosted while
	// we're scanning, we could get notFound
	(errCode != VestaSource::notFound))
      {
	throw ReposError(errCode, "VestaSource::list", path);
      }
} // GatherWeedRoots_SearchDir

static void GatherWeedRoots_SearchPath(Op op, VestaSource *vs, char *path,
  char *end, int rem, char *rest, const FP::Tag &parentFP,
  VestaSource::typeTag parentType)
  throw (SysError, ReposError, InputError, SRPC::failure)
/* Recursively search the file or directory "vs" named by "path", where
   "end" is a pointer to the null character that terminates "path", and "rem"
   is the number of characters remaining in the buffer starting at "end".
   "rest" is the remainder of the search pattern. "op" indicates whether the
   entries matched by the path "path" combined with the pattern "rest" should
   be added or removed from the set of models. "parentFP" is the fingerprint
   of the parent directory of "vs", which has type "parentType"; "parentFP" is
   defined if and only if "parentType" represents an immutable directory.

   The last (non-null) character of "path" is assumed *not* to be a slash.
   "rest" should start with the full arc of the search pattern path; hence,
   its first character is assumed *not* to be a slash. */
{
    assert(IsEmpty(end) && *(end-1) != Slash);

    switch (vs->type) {
      case VestaSource::immutableFile:
	// handle the base case of the recursion
	if (IsEmpty(rest)) {
	    GatherWeedRoots_HandleFile(op, vs, path, parentFP, parentType);
	}
	break;
      case VestaSource::immutableDirectory:
      case VestaSource::appendableDirectory:
	GatherWeedRoots_SearchDir(op, vs, path, end, rem, rest, parentFP);
	break;
      case VestaSource::ghost:
      case VestaSource::stub:
	// silently ignore ghosts and stubs
	break;
      default:
	// print a warning message for everything else
	cerr << "Warning: ignoring '" << path << "'" << endl;
	break;
    }
} // GatherWeedRoots_SearchPath

static void GatherWeedRoots_ProcessPattern(Op op, char *pattern)
  throw (SysError, ReposError, InputError, SRPC::failure)
/* Process the single model pattern specified by "op" and "pattern". Throws
   "InputError" if "pattern" is an invalid path pattern. */
{
    // verify that repository root name is a '/' followed by 1 or more chars
    if (GatherWeedRoots_rootNameLen == 0) {
	throw InputError("repository root name is empty");
    } else if (GatherWeedRoots_rootName[0] != Slash) {
	throw InputError("repository root name is not an absolute path");
    } else if (GatherWeedRoots_rootNameLen == 1) {
	throw InputError("repository root name must be different from '/'");
    }

    // determine ``root'' directory of the search
    char dir[MaxPathLen];
    int rem = MaxPathLen; // number of characters remaining in "dir"
    char *end = dir;
    if (strlen(pattern) > GatherWeedRoots_rootNameLen
        && strncmp(pattern, GatherWeedRoots_rootName.cchars(),
                   GatherWeedRoots_rootNameLen) == 0
        && pattern[GatherWeedRoots_rootNameLen] == Slash)
    {
	strncpy(dir, pattern, GatherWeedRoots_rootNameLen);
	end += GatherWeedRoots_rootNameLen;
	rem -= GatherWeedRoots_rootNameLen;
        pattern += GatherWeedRoots_rootNameLen + 1; // skip '/'
    } else {
	throw InputError("weeder pattern does not start with repository root");
    }
    *end = Null;

    // invoke recursive routine to explore it
    try {
	GatherWeedRoots_SearchPath(op, GatherWeedRoots_reposRoot,
          dir, end, rem, pattern, EmptyFP, VestaSource::unused);
    }
    catch (InputError &err) {
	// fill in an "arg" if none was present
	if (err.arg.Empty()) err.arg = pattern;
	// re-throw the exception
	throw;
    }
} // GatherWeedRoots_ProcessPattern

// Read input file ------------------------------------------------------------

static void GatherWeedRoots_SkipWhite(/*INOUT*/ char* &ptr) throw ()
/* Advance "ptr" past spaces and tabs. */
{
    while (*ptr == ' ' || *ptr == '\t') ptr++;
}

static void GatherWeedRoots_GetLine(ifstream &ifs, char *buff, int rem)
  throw (FS::Failure)
{
    while (rem > 0) {
	int c = ifs.get();
	if (c == EOF || c == '\n') break;
	*buff++ = (char)c;
	rem--;
    }
    if (rem == 0) GatherWeedRoots_MaxPathLenErr();
    *buff = Null;
} // GatherWeedRoots_GetLine

static void GatherWeedRoots_ProcessFile(ifstream &ifs)
  throw (FS::Failure, SysError, ReposError, InputError, SRPC::failure)
/* Process the input file "ifs". Throws "FS::Failure" if there is an error
   reading from this stream. Throws "InputError" if the input is not of the
   correct format. */
{
    while (!FS::AtEOF(ifs)) {
	// read next line
	char buff[MaxPatternLen];
        GatherWeedRoots_GetLine(ifs, buff, MaxPatternLen);

	// skip blank lines and comments
	if (strlen(buff) == 0) continue;
        if (strlen(buff) > 0 && buff[0] == Pound) continue;

	// handle real input line
	Op op;
	char *curr = buff;
	GatherWeedRoots_SkipWhite(/*INOUT*/ curr);
	switch (*curr++) {
	  case Plus:
	    op = Add;
	    break;
	  case Minus:
	    op = Remove;
	    break;
	  default:
	    throw InputError("model pattern does not start with '+' or '-'");
	}
	GatherWeedRoots_SkipWhite(/*INOUT*/ curr);
	GatherWeedRoots_ProcessPattern(op, curr);
    }
} // GatherWeedRoots_ProcessFile

// Main method ----------------------------------------------------------------

void GatherWeedRoots::P(char *file, bool echoModels, /*OUT*/ RootTbl *rootTbl)
  throw (SRPC::failure, FS::Failure, FS::DoesNotExist,
         InputError, SysError, ReposError)
{
    // initialize globals
    GatherWeedRoots_printModelNames = echoModels;
    GatherWeedRoots_pkgTbl = NEW_CONSTR(FPPkgTbl,
					(/*sizeHint=*/ 500, /*useGC=*/ true));
    GatherWeedRoots_reposRoot = VestaSource::repositoryRoot();
    assert(GatherWeedRoots_reposRoot->type ==VestaSource::appendableDirectory);
    GatherWeedRoots_rootName =
      ReadConfig::TextVal("UserInterface", "AppendableRootName");
    GatherWeedRoots_rootNameLen = GatherWeedRoots_rootName.Length();

    // try opening the file
    ifstream ifs;
    FS::OpenReadOnly(file, /*OUT*/ ifs);
    try {
	try {
	    if (GatherWeedRoots_printModelNames) {
		cout << "Models in weeder instructions:" << endl;
		GatherWeedRoots_instructionModelCnt = 0;
	    }
	    GatherWeedRoots_ProcessFile(ifs);
	    if (GatherWeedRoots_printModelNames) {
		if (GatherWeedRoots_instructionModelCnt == 0) {
		    cout << "  NONE" << endl;
		}
		cout << endl;
	    }
	} catch (...) {
	    if (GatherWeedRoots_printModelNames) {
		if (GatherWeedRoots_instructionModelCnt == 0) {
		    cout << "  NONE SO FAR" << endl;
		}
		cout << endl;
	    }
	    FS::Close(ifs);
	    throw;
	}
	FS::Close(ifs);

	if (GatherWeedRoots_pkgTbl != (FPPkgTbl *)NULL) {
	    // copy range of "pkgTbl" to domain of "rootTbl"
	    FPPkgIter it(GatherWeedRoots_pkgTbl);
	    FP::Tag fp;
	    const PkgBuild *pkg;
	    while (it.Next(/*OUT*/ fp, /*OUT*/ pkg)) {
		(void) rootTbl->Put(*pkg, false);
	    }
	}

    } catch (...) {
	// drop pointers on floor for GC
	GatherWeedRoots_pkgTbl = (FPPkgTbl *)NULL;
	GatherWeedRoots_reposRoot = (VestaSource *)NULL;
	throw;
    }
    // drop global pointers on floor for GC
    GatherWeedRoots_pkgTbl = (FPPkgTbl *)NULL;
    GatherWeedRoots_reposRoot = (VestaSource *)NULL;
    GatherWeedRoots_rootName = "";
} // GatherWeedRoots::P
