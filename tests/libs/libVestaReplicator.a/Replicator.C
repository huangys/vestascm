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

// Replicator.C

// Replicator class.

#include <VestaConfig.H>
#include <VDirSurrogate.H>
#include "Replicator.H"
#include "UniqueId.H"

using std::cout;
using std::cerr;
using std::endl;

// Copy attributes of svs to dvs.  Not recursive.
// Copy security attributes only if access is true.
void
Replicator::copyAttribs(VestaSource *svs, Text svsname,
			VestaSource *dvs, Replicator::Flags flags)
  throw (Replicator::Failure, SRPC::failure)
{
  if (svs == NULL || !svs->hasAttribs()) return;
  if (dvs && svs->type != dvs->type) return;
  if (flags & verbose) {
    cout << "attribs\t" << svsname << endl;
  }
  if (flags & test) return;
  const char* pathname = svsname.cchars() + strlen("/vesta");
  if (*pathname == '/') pathname++;
  VestaSource::errorCode err =
    VDirSurrogate::replicateAttribs(pathname, (flags & attrAccess),
				    dhost, dport, shost, sport,
				    PathnameSep, dwho, swho);
  if (err != VestaSource::ok) {
    if(err == VestaSource::longIdOverflow) {
      // Continue on LongId overflow
      cerr << "warning: " << ReposUI::errorCodeText(err) 
	   << ", copying attribs for " << svsname << endl;
      return;
    }
    throw(Failure(err, Text("copying attribs for ") + svsname));
  }
}

static void
clash(VestaSource *svs, VestaSource *dvs, Text svsname)
{
  throw(Replicator::Failure(Text("agreement invariant violation (") +
			    (svs->master ? "master " : "") +
			    VestaSource::typeTagString(svs->type) +
			    " vs. " +
			    (dvs->master ? "master " : "") +
			    VestaSource::typeTagString(dvs->type) +
			    ") on " + svsname));
}

struct FirstLastClosure {
  int first;
  int last;
};

struct DirClosure {
  VestaSource* sparent;
  AccessControl::Identity swho;
  Text parentName;
  Arc parentArc; // last arc of parentName
  bool firstLastKnown, destFirstLastKnown;
  int first, last;
  DirClosure* parentcl;
  // first/last at destination
  VestaSource* dparent;
  AccessControl::Identity dwho;
  int dfirst, dlast;
};

struct ReplicatorDirClosure : public DirClosure
{
  Replicator* replicator;
  Replicator::DirectiveSeq* directives;
  Replicator::Flags flags;
  int matchcount;
};

struct ExpansionDirClosure : public DirClosure
{
  Text pattern;
  Replicator::matchCallback callback;
  void *closure;
};

//
// Replicate a "latest" link from svs to dvs
//
void
copyLatest(VestaSource *svs, VestaSource *dvs, Text svsname,
	   ReplicatorDirClosure* cl)
{
  Text latestname = svsname + "/latest";
  const char* pathname = latestname.cchars() + strlen("/vesta");
  if (*pathname == '/') pathname++;
  VestaSource::errorCode err;
  VestaSource* lvs = 0;
  VestaSource::typeTag
    sl_type = VestaSource::deleted,
    dl_type = VestaSource::deleted;

  // Look for existing "latest" in svs
  err = svs->lookup("latest", lvs, cl->replicator->swho);
  if (err == VestaSource::notFound) {
    return;
  } else if (err != VestaSource::ok) {
    throw(Replicator::Failure(err, "looking up " + latestname));
  } else if (lvs->type != VestaSource::stub &&
	     lvs->type != VestaSource::ghost) {
    delete lvs;
    return;
  } else {
    sl_type = lvs->type;
    delete lvs;
  }

  // Look for existing "latest" in dvs
  err = dvs->lookup("latest", lvs, cl->replicator->dwho);
  if ((err == VestaSource::notFound) ||
      ((err == VestaSource::ok) && (lvs->type == VestaSource::ghost))) {
    // Missing or ghosted; insert it
    bool latest_ghosted = (err == VestaSource::ok);
    if(latest_ghosted)
      {
	dl_type = lvs->type;
	delete lvs;
      }
    if (cl->flags & Replicator::verbose) {
      cout << "latest\t" << latestname << endl;
    }
    if (!(cl->flags & Replicator::test)) {
      err = VDirSurrogate::
	replicate(pathname, true, false,
		  cl->replicator->dhost, cl->replicator->dport,
		  cl->replicator->shost, cl->replicator->sport, PathnameSep,
		  cl->replicator->dwho, cl->replicator->swho);
      if (err != VestaSource::ok) {
	if(err == VestaSource::longIdOverflow) {
	  // Continue on LongId overflow
	  cerr << "warning: " << ReposUI::errorCodeText(err) 
	       << ", inserting " << latestname << endl;
	  return;
	}
	else if(!latest_ghosted || (err != VestaSource::nameInUse))
	  {
	    throw(Replicator::Failure(err, Text("inserting ") + latestname));
	  }
	// Older repositories won't let us replicate a stub over a
	// ghost.  In that case (latest_ghosted && (err ==
	// VestaSource::nameInUse)) we tried, but it's still a ghost.
      } else {
	// The destination now has a stub
	dl_type = VestaSource::stub;
      }
    }
  } else if (err != VestaSource::ok) {
    throw(Replicator::Failure(err, "looking up " + latestname));
  } else if (lvs->type != VestaSource::stub &&
	     lvs->type != VestaSource::ghost) {
    delete lvs;
    return;
  } else {
    dl_type = lvs->type;
    delete lvs;
  }
  if (cl->flags & Replicator::test) return;

  // Replicate attribs on "latest".  We do this even if it already
  // existed, just in case they're wrong or missing.
  err = VDirSurrogate::
    replicateAttribs(pathname,
		     (cl->flags & Replicator::attrAccess),
		     cl->replicator->dhost, cl->replicator->dport,
		     cl->replicator->shost, cl->replicator->sport, PathnameSep,
		     cl->replicator->dwho, cl->replicator->swho);
  if (err != VestaSource::ok) {
    if(err == VestaSource::longIdOverflow) {
      // Continue on LongId overflow
      cerr << "warning: " << ReposUI::errorCodeText(err) 
	   << ", copying attribs for " << latestname << endl;
      return;
    } else if ((err == VestaSource::inappropriateOp) && (dl_type != sl_type)) {
      // Source and destination types don't match.  (One is a ghost
      // and the otehr is a stub.)  Print a warning if we're in
      // verbose mode and continue.
      if (cl->flags & Replicator::verbose) {
	cerr << "warning: source " << VestaSource::typeTagString(sl_type)
	     << " vs. destination " << VestaSource::typeTagString(dl_type)
	     << " on " << latestname << endl;
      }
      return;
    }
    throw(Replicator::Failure(err, Text("copying attribs for ") + latestname));
  }
}

// Helper for getFirstLast, see below
bool
gflCallback(void* closure, VestaSource::typeTag type, Arc arc,
	    unsigned int index, Bit32 pseudoInode, ShortId filesid,
	    bool master)
{
  FirstLastClosure* cl = (FirstLastClosure*) closure;
  if (type != VestaSource::appendableDirectory &&
      type != VestaSource::immutableDirectory) return true;
  if (!isdigit(*arc)) return true;
  if (arc[0] == '0' && arc[1] != '\0') return true;
  char* tail;
  int val = strtol(arc, &tail, 10);
  if (*tail) return true;
  if (cl->first == -1 || val < cl->first) cl->first = val;
  if (val > cl->last) cl->last = val;
  return true;
}

// Fill in the first and last fields in an DirClosure.  These
// represent the lowest and highest valued arcs in an appendable
// directory sparent, out of all those that consist entirely of
// decimal digits with no extra leading zeros, or -1 if there are no
// such arcs or the directory is not appendable.
//
void
getFirstLast(DirClosure *cl)
{
  assert(!cl->firstLastKnown);
  cl->first = cl->last = -1;
  FirstLastClosure flcl;
  // first last at source
  assert(cl->sparent);
  if (cl->sparent->type != VestaSource::appendableDirectory) return;
  flcl.first = flcl.last = -1;
  VestaSource::errorCode err =
    cl->sparent->list(0, gflCallback, &flcl, cl->swho);
  if (err != VestaSource::ok) {
    throw(Replicator::Failure(err, "listing " + cl->parentName));
  }
  cl->first = flcl.first;
  cl->last = flcl.last;
  cl->firstLastKnown = true;
}

void
getDestFirstLast(DirClosure *cl)
{
  assert(!cl->destFirstLastKnown);
  cl->dfirst = cl->dlast = -1;
  // If dparent isn't set, we'll just go with -1/-1.  This makes it
  // possible to use a range like [DFIRST,LAST] in a directory not
  // present in the destination repository.
  if(cl->dparent)
    {
      FirstLastClosure flcl;
      // first last at destination
      if(cl->dparent->type != VestaSource::appendableDirectory) return;
      flcl.first = flcl.last = -1;
      VestaSource::errorCode err =
	cl->dparent->list(0, gflCallback, &flcl, cl->dwho);
      if (err != VestaSource::ok) {
	throw(Replicator::Failure(err, "listing " + cl->parentName));
      } 
      cl->dfirst = flcl.first;
      cl->dlast = flcl.last;
    }
  cl->destFirstLastKnown = true;
}

// Parse an expression, consisting of a sequence of decimal numbers
// or "FIRST" or "LAST", separated by "+" or "-" signs.  Return -1 if
// FIRST or LAST is used but has no value.  Otherwise compute the
// value of the expression and return it.
int 
getExpr(const char* e, const char* end, DirClosure* cl)
{
  int val = 0, sign = 1;
  if (e == end) {
    throw(Replicator::Failure("empty expression"));
  }
  for (;;) {
    if (isdigit(*e)) {
      val += strtol(e, (char **)&e, 10) * sign;
    } 
    else if (strncmp(e, "FIRST", 5) == 0) {
      if (!cl->firstLastKnown) getFirstLast(cl);
      if (cl->first == -1) return -1;
      val += cl->first * sign;
      e += 5;
    } 
    else if (strncmp(e, "LAST", 4) == 0) {
      if (!cl->firstLastKnown) getFirstLast(cl);
      if (cl->last == -1) return -1;
      val += cl->last * sign;
      e += 4;
    } 
    else if(strncmp(e, "DFIRST", 6) == 0) {
      if (!cl->destFirstLastKnown) getDestFirstLast(cl);
      if (cl->dfirst == -1) return -1;
      val += cl->dfirst * sign;
      e += 6;
    } 
    else if (strncmp(e, "DLAST", 5) == 0) {
      if (!cl->destFirstLastKnown) getDestFirstLast(cl);
      if (cl->dlast == -1) return -1;
      val += cl->dlast * sign;
      e += 5;
    } 
    else {	    
      throw(Replicator::Failure("bad number in expression"));
    } 
	
    if (e == end) return val;
	
    if (*e == '-') {
      sign = -1;
    } else if (*e == '+') {
      sign = 1;
    } else {
      throw(Replicator::Failure("bad operator in expression"));
    }
    e++;
  }	    
}

// Pattern match one arc of a pathname pattern against a literal
//  arc.  Neither pat nor arc may contain a "/".  The cl argument
//  is used if pat contains the FIRST or LAST token; expansions
//  for those tokens are taken from a cache in cl, or generated
//  and filled in to cl if the cache is empty.
bool
arcPatternMatch(const char* arc, const char* pat, DirClosure* cl,
		int bracedepth = 0)
{
  const char *closebrak = NULL;
  const char *comma = NULL;
    
  for (;;) {
    bool first = strncmp(pat, "FIRST", 5) == 0;
    bool last = strncmp(pat, "LAST", 4) == 0;
    bool first_at_dest = strncmp(pat, "DFIRST", 6) == 0;
    bool last_at_dest = strncmp(pat, "DLAST", 5) == 0;
    if(first || last || first_at_dest || last_at_dest) {
      // See if decimal value matches arc
      if (!isdigit(*arc)) return false;
      // Get first/last version at source if needed
      if((first || last) && !cl->firstLastKnown)
	{
	  getFirstLast(cl);
	  if(cl->first == -1) return false;
	}
      // Get first/last version at destination if needed
      if((first_at_dest || last_at_dest) && !cl->destFirstLastKnown)
	{
	  getDestFirstLast(cl);
	  if(cl->dfirst == -1) return false;
	}
      char* arctail;
      long arcval = strtol(arc, &arctail, 10);
      if(first) {
	if (arcval != cl->first) return false;
	pat += 5;
      } 
      else if(last) {
	if(arcval != cl->last) return false;
	pat += 4;
      }
      else if(first_at_dest) {
	if(arcval != cl->dfirst) return false;
	pat += 6;
      }
      else {
	if(arcval != cl->dlast) return false;
	pat += 5;
      }

      arc = arctail;
      continue;
    }
  	
    switch (*pat) {
    case '\000':
      // Empty pattern matches if and only if arc is empty.
      if (*arc != '\000') return false;
      if (bracedepth > 0) {
	throw(Replicator::Failure("unmatched { or ["));
      }
      return true;
	    
    case '?':
      // Match one non-null character
      if (*arc == '\000') return false;
      pat++;
      arc++;
      break;
	    
    case '*':
      // Match zero or more characters
      if (*arc == '\000') {
	// Nothing left in arc, so match zero characters and
	// consume the '*'.
	pat++;
	break;
      }
      // Recurse to try to match one or more characters.
      if (arcPatternMatch(arc + 1, pat, cl, bracedepth)) {
	return true;
      }
      // Failed to match one or more characters, so
      // match zero characters and consume the '*'.
      pat++;
      break;
	    
    case '#':
      // Match zero or more digits
      if (!isdigit(*arc)) {
	// No digits, so match zero characters and
	// consume the '#'.
	pat++;
	break;
      }
      // Recurse to try to match one or more characters.
      if (arcPatternMatch(arc + 1, pat, cl, bracedepth)) {
	return true;
      }
      // Failed to match one or more characters, so
      // match zero characters and consume the '#'.
      pat++;
      break;
	    
    case '[':
      // Disambiguate [exprlo,exprhi] from [charlist] 
      if (pat[1] == '\000' ||
	  (closebrak = strchr(pat + 2, ']')) == NULL) {
	// No closing ']'
	throw(Replicator::Failure("unmatched { or ["));
      }
      comma = strchr(pat + 1, ',');
      if (comma && comma > pat + 1 && comma < closebrak - 1) {
	// [exprlo,exprhi]
	if (!isdigit(*arc)) return false;
	int lo = getExpr(pat + 1, comma, cl);
	int hi = getExpr(comma + 1, closebrak, cl);
	char* arctail;
	long arcval = strtol(arc, &arctail, 10);
	if (arcval < lo || arcval > hi) return false;
	arc = arctail;
	pat = closebrak + 1;
      } else {
	// [charlist] or [^charlist]
	if (*arc == '\000') return false;
	pat++;
	bool negated = (*pat == '^');
	if (negated) {
	  pat++;
	}
	for (;;) {
	  if (pat[1] == '-' && pat[2] != ']') {
	    // This is a range; does it match?
	    if (pat[0] <= *arc && *arc <= pat[2]) {
	      // Yes
	      if (negated) return false;
	      pat = closebrak + 1;
	      arc++;
	      break;
	    } else {
	      // No
	      pat += 2;
	    }
	  } else if (*pat == *arc) {
	    // Matched a character
	    if (negated) return false;
	    pat = closebrak + 1;
	    arc++;
	    break;
	  }
	  // Did not match yet; try next char in list
	  pat++;
	  if (*pat == ']') {
	    // Exhausted list, no match
	    if (negated) {
	      arc++;
	      pat++;
	      break;
	    }
	    return false;
	  }
	}
      }
      break;
	    
    case '{':
      // Handle { } alternatives
      // Use recursion so that we can backtrack in arc
      //  when an alternative fails.  The recursion tests
      //  the entire tails of pat and arc, not just up to
      //  the matching '}' in pat.  It also makes sure that
      //  we see enough closing '}' characters.
      pat++;
      for (;;) {
	if (arcPatternMatch(arc, pat, cl, bracedepth+1)) {
	  // Matched on this alternative
	  return true;
	}
	// Skip to next comma at this bracedepth if any
	int brd = 0;
	int bkd = 0;
	bool skipping = true;
	while (skipping) {
	  switch (*pat++) {
	  case '\000':
	    // Ill-formed alternatives
	    throw(Replicator::Failure("unmatched { or ["));
	  case '{':
	    if (!bkd) brd++;
	    break;
	  case '}':
	    if (!bkd) {
	      if (brd == 0) {
				// No more alternatives; fail
		return false;
	      } else {
		brd--;
	      }
	    }
	    break;
	  case '[':
	    bkd = 1;
	    if (*pat == '[') pat++; // "[[" is a special case
	    break;
	  case ']':
	    if (bkd) bkd--;
	    break;
	  case ',':
	    if (!bkd && !brd) {
	      skipping = false;
	    }
	    break;
	  default:
	    break;
	  }
	}
      }
      //break; // not reached
	    
    case ',':
      if (bracedepth == 0) {
	goto plain;
      } else {
	// End of current alternative; skip to matching '}'
	int brd = 0, bkd = 0;
	bool skipping = true;
	pat++;
	while (skipping) {
	  switch (*pat) {
	  case '\000':
	    // Ill-formed alternatives
	    throw(Replicator::Failure("unmatched { or ["));
	  case '{':
	    if (!bkd) brd++;
	    break;
	  case '}':
	    if (!bkd) {
	      if (brd == 0) {
		skipping = false;	// done
	      } else {
		brd--;
	      }
	    }
	    break;
	  case '[':
	    bkd = 1;
	    break;
	  case ']':
	    if (bkd) bkd--;
	    break;
	  default:
	    break;
	  }
	  pat++;
	}
	bracedepth--;
      }
      break;
	    
    case '}':
      if (bracedepth == 0) goto plain;
      // End of current alternative, and no others to skip
      pat++;
      bracedepth--;
      break;
	    
    default:
    plain:
      if (*pat != *arc) return false;
      pat++;
      arc++;
      break;
    }
  }
  //return true; // not reached
}


// Split pattern into head and tail
void
splitHeadTail(const Text& pattern, Text& head, Text& tail)
{
  int slash = pattern.FindChar('/');
  if (slash < 0) {
    head = pattern;
    tail = "";
  } else {
    head = pattern.Sub(0, slash);
    while (pattern[++slash] == '/') /*skip*/ ;
    tail = pattern.Sub(slash);
  }
}

const Replicator::Directive plusEmpty('+', Text(""));
const Replicator::DirectiveSeq nothingAS(0);
const Replicator::DirectiveSeq everythingAS(&plusEmpty, 1);

//
// Process directives against arc.  If arc itself matched, return
// true; if not, return false.  Orthogonally, return childDirectives
// to use when processing arc's children; if nothing below can match,
// this will be a 0-length sequence.
//
static bool
processDirectives(ReplicatorDirClosure *cl,
		  Replicator::DirectiveSeq* directives,
		  Arc arc, Replicator::DirectiveSeq** childDirectivesOut)
{
  // Loop through directives, find those for which arc
  // matches the head, and gather their tails in childDirectives.
  assert(directives != NULL);
  Replicator::DirectiveSeq* childDirectives =
    NEW_CONSTR(Replicator::DirectiveSeq, (directives->size()));
  bool matchhere = false;  // is this arc itself included?

  for (int i = 0; i < directives->size(); i++) {
    Replicator::Directive d = directives->get(i);

    // This loop is needed due to %-patterns; see below
    for (;;) {
      if (d.pattern.Empty()) {
	// This empty pattern dominates previous directives,
	// so clear them.
	if (d.sign == '+') {
	  *childDirectives = everythingAS;
	  matchhere = true;
	} else {
	  *childDirectives = nothingAS;
	  matchhere = false;
	}
	// Not a %-pattern, so we are done with this pattern
	break;

      } else if (d.pattern[0] != '%') {
	// Nonempty, non-% pattern
	Text head, tail;
	splitHeadTail(d.pattern, head, tail);
		
	if (arcPatternMatch(arc, head.cchars(), cl)) {
	  // Head matches
	  if (tail.Empty()) {
	    // Tail is empty:
	    // 1) This pattern includes or excludes arc itself.
	    // 2) Its empty tail dominates previous
	    // directives for arc's children, so clear them.
	    if (d.sign == '+') {
	      matchhere = true;
	      *childDirectives = everythingAS;
	    } else {
	      matchhere = false;
	      *childDirectives = nothingAS;
	    }
	  } else {
	    // Tail is nonempty: 
	    // 1) This pattern does not affect arc itself.  
	    // 2) Its tail does need to be part of the directives for
	    // arc's children, except that we can omit a '-' pattern
	    // if it has nothing to subtract from.
	    if (!(d.sign == '-' && childDirectives->size() == 0)) {
	      d.pattern = tail;
	      childDirectives->addhi(d);
	    }
	  }
	}
	// Not a %-pattern, so we are done with this pattern
	break;

      } else {
	// Process %-patterns according to their recursive
	// definition:  %X :== head(X)/%X | tail(X).  Instead
	// of recursing, though, we iterate down the tail.
	// The loop ends when the remaining tail is empty or
	// we have processed a non-% pattern.

	// Split pattern
	Text head, tail;
	splitHeadTail(d.pattern.Sub(1), head, tail);
	if (head.Empty()) {
	  throw(Replicator::Failure("syntax error using %"));
	}
		
	// Deal with head(X)/%X case now
	if (arcPatternMatch(arc, head.cchars(), cl)) {
	  // Head matches.  Use %X itself as tail (which is, of
	  // course, nonempty, so we don't have to deal with
	  // that case here).  As above:
	  // 1) This pattern does not affect arc itself.
	  // 2) Its tail does need to be part of the directives
	  // for arc's children, except that we can omit a '-' pattern
	  // if it has nothing to subtract from.
	  if (!(d.sign == '-' && childDirectives->size() == 0)) {
	    childDirectives->addhi(d);
	  }
	}
		
	// Prepare to deal with tail(X) case on next iteration
	// (including the possibility that tail(X) is empty).
	d.pattern = tail;

      }
    }
  }
  *childDirectivesOut = childDirectives;
  return matchhere;
}

//
// Helper for doOneEntry; create a missing parent directory
//
void
createParent(ReplicatorDirClosure* cl)
  throw (SRPC::failure, Replicator::Failure)
{
  ReplicatorDirClosure *parent_cl = ((ReplicatorDirClosure *) cl->parentcl);

  // If grandparent is also missing, recurse upward to create it
  if (!parent_cl->dparent ||
      parent_cl->dparent->type == VestaSource::stub ||
      parent_cl->dparent->type == VestaSource::ghost) {
    createParent(parent_cl);
    if (!parent_cl->dparent ||
	parent_cl->dparent->type == VestaSource::immutableDirectory) {
      // Either (1) grandparent could not be created because that
      // would requre replacing a ghost, and flags don't permit that,
      // or (2) grandparent was immutable, so the recursion created
      // the whole tree below it, including the parent we were asked
      // to create.
      return;
    }
  }
  if (cl->dparent &&
      cl->dparent->type == VestaSource::ghost &&
      !(cl->flags & ((cl->dparent->master &&
		      cl->sparent->type == VestaSource::appendableDirectory)
		     ? Replicator::reviveMA : Replicator::revive))) {
    // Do not replace the ghost
    if (cl->flags & Replicator::verbose) {
      cout << "ghost\t" << cl->parentName << endl;
    }
    cl->dparent = NULL;
    return;
  }
  if (cl->flags & Replicator::verbose) {
    cout << (cl->sparent->type == VestaSource::appendableDirectory
	     ? "create\t" : "copy\t") << cl->parentName << endl;
  }
  if (cl->flags & Replicator::test) {
    // Kludge...
    if (cl->sparent->type == VestaSource::immutableDirectory) {
      cl->dparent = cl->sparent;
    } else {
      cl->dparent = cl->replicator->droot; //ugh
    }
    return;
  }
  const char* pathname = cl->parentName.cchars() + strlen("/vesta");
  if (*pathname == '/') pathname++;
  VestaSource::errorCode err =
    VDirSurrogate::replicate(pathname, false, false,
			     cl->replicator->dhost, cl->replicator->dport,
			     cl->replicator->shost, cl->replicator->sport,
			     PathnameSep,
			     cl->replicator->dwho, cl->replicator->swho);
  if (err != VestaSource::ok) {
    if(err == VestaSource::longIdOverflow) {
      // Continue on LongId overflow
      cerr << "warning: " << ReposUI::errorCodeText(err) 
	   << ", inserting " << cl->parentName << endl;
      return;
    }
    else
      throw(Replicator::Failure(err, Text("inserting ") + cl->parentName));
  }   
  err = ((ReplicatorDirClosure *) cl->parentcl)
    ->dparent->lookup(cl->parentArc, cl->dparent,
		      cl->replicator->dwho);
  if (err != VestaSource::ok) {
    throw(Replicator::Failure(err, Text("lookup ") + cl->parentName
			      + Text(" in destination")));
  }
}

//
// Consider one directory entry for replication
//
bool
doOneEntry(void* closure, VestaSource::typeTag type, Arc arc,
	   unsigned int index, Bit32 pseudoInode, ShortId filesid, bool master)
{
  ReplicatorDirClosure *cl = (ReplicatorDirClosure*) closure;

  // Check for match against directives
  bool matchhere = true;   // this object matches
  bool trybelow = true;    // a child may match (recursion needed)
  bool matchbelow = false; // a child does match (discovered in recursion)
  Replicator::DirectiveSeq* childDirectives = NULL;
  if (cl->directives) {
    matchhere = processDirectives(cl, cl->directives, arc,
				  &childDirectives);
    trybelow = !(childDirectives && childDirectives->size() == 0);
  }
  if (!matchhere && !trybelow) {
    // Early exit if neither current object nor anything below can match
    if (childDirectives) delete childDirectives;
    return true; // a sibling could still match
  }

  // Look up current object
  Text svsname = cl->parentName + "/" + arc;
  VestaSource* svs;
  VestaSource::errorCode err = cl->sparent->lookupIndex(index, svs);
  if(err == VestaSource::longIdOverflow)
    {
      // Continue on LongId overflow
      return true;
    }
  else if (err != VestaSource::ok) {
    throw(Replicator::Failure(err, Text("lookup index ") + svsname));
  }

  // Look up corresponding object in destination if possible
  VestaSource* dvs = NULL;
  if (cl->dparent &&
      (cl->dparent->type == VestaSource::appendableDirectory ||
       cl->dparent->type == VestaSource::immutableDirectory)) {
    err = cl->dparent->lookup(arc, dvs, cl->replicator->dwho);
    if(err == VestaSource::longIdOverflow)
      {
	// Continue on LongId overflow
	return true;
      }
    else if((err == VestaSource::notFound) && (cl->flags & Replicator::dontCopyNew)) {
      return true;
    }
    if (err != VestaSource::ok && err != VestaSource::notFound) {
      throw(Replicator::Failure(err, Text("lookup ") + svsname
				+ Text(" in destination")));
    }
  }
  if (dvs && svs->master && dvs->master) {
    clash(svs, dvs, svsname);
  }

  if((cl->flags & Replicator::dontCopyNew) && (svs->type != dvs->type))
    return true;
  

  bool copyhere = matchhere;  // this object (to be) copied here
  bool copybelow = false;     // this object copied in a recursive call
  if (matchhere) {
    // Current object itself matches (so far)
    // Check for various special or error cases
    switch (svs->type) {
    case VestaSource::appendableDirectory:
      if (dvs) {
	switch (dvs->type) {
	case VestaSource::stub:
	  // Stub: replace it if not master
	  if (dvs->master) {
	    clash(svs, dvs, svsname);
	  }
	  break;
	case VestaSource::ghost:
	  // Ghost: replace it if flags say to
	  if ((cl->flags & (dvs->master ? Replicator::reviveMA :
			    Replicator::revive)) == 0) {
	    // Do not replace the ghost
	    if (cl->flags & Replicator::verbose) {
	      cout << "ghost\t" << svsname << endl;
	    }
	    copyhere = false;
	  }
	  break;
	case VestaSource::appendableDirectory:
	  // Already there; done
	  if (cl->flags & Replicator::verbose) {
	    cout << "exists\t" << svsname << endl;
	  }
	  copyhere = false;
	  break;
	default:
	  // Something else; error
	  clash(svs, dvs, svsname);
	  break;
	}
      }
      break;

    case VestaSource::immutableDirectory:
      if (dvs) {
	switch (dvs->type) {
	case VestaSource::stub:
	  // Stub: replace it if not master
	  if (dvs->master) {
	    clash(svs, dvs, svsname);
	  }
	  break;
	case VestaSource::ghost:
	  // Ghost: replace it if flags say to
	  if ((cl->flags & Replicator::revive) == 0) {
	    // Do not replace the ghost
	    if (cl->flags & Replicator::verbose) {
	      cout << "ghost\t" << svsname << endl;
	    }
	    copyhere = false;
	  }
	  break;
	case VestaSource::immutableDirectory:
	  // Already there; done
	  if (cl->flags & Replicator::verbose) {
	    cout << "exists\t" << svsname << endl;
	  }
	  copyhere = false;
	  break;
	default:
	  // Something else; error
	  clash(svs, dvs, svsname);
	  break;
	}
      }
      trybelow = false;
      break;

    case VestaSource::immutableFile:
      if (dvs) {
	switch (dvs->type) {
	case VestaSource::stub:
	  // Stub: replace it if not master
	  if (dvs->master) {
	    clash(svs, dvs, svsname);
	  }
	  break;
	case VestaSource::ghost:
	  // Ghost: replace it if flags say to
	  if ((cl->flags & Replicator::revive) == 0) {
	    // Do not replace the ghost
	    if (cl->flags & Replicator::verbose) {
	      cout << "ghost\t" << svsname << endl;
	    }
	    copyhere = false;
	  }
	  break;
	case VestaSource::immutableFile:
	  // Already there; done
	  if (cl->flags & Replicator::verbose) {
	    cout << "exists\t" << svsname << endl;
	  }
	  copyhere = false;
	  break;
	default:
	  // Something else; error
	  clash(svs, dvs, svsname);
	  break;
	}
      }
      break;

    case VestaSource::stub:
      if (!(cl->flags & Replicator::inclStubs)) {
	matchhere = copyhere = false;
	break;
      }
      if (dvs) {
	if (svs->master &&
	    dvs->type != VestaSource::stub &&
	    dvs->type != VestaSource::ghost) {
	  clash(svs, dvs, svsname);
	}
	if (cl->flags & Replicator::verbose) {
	  cout << "exists\t" << svsname << endl;
	}
	copyhere = false;
	break;
      }
      break;

    case VestaSource::ghost:
      if (!(cl->flags & Replicator::inclGhosts)) {
	matchhere = copyhere = false;
	break;
      }
      if (dvs) {
	if (cl->flags & Replicator::verbose) {
	  cout << "exists\t" << svsname << endl;
	}
	copyhere = false;
	break;
      }
      break;

    default:
      assert(false);
    }

    // Inform caller of match
    if (matchhere) {
      cl->matchcount++;
    }

    // Create parent in destination if needed
    if (copyhere && (!cl->dparent || cl->dparent->type == VestaSource::stub ||
		     cl->dparent->type == VestaSource::ghost)) {
      createParent(cl);
      if (!cl->dparent ||
	  cl->sparent->type == VestaSource::immutableDirectory) {
	// Either (1) parent could not be created because that would
	// requre replacing a ghost, and flags don't permit that, or 
	// (2) parent was immutable, so createParent created the whole tree
	// below it, including the current object.
	copyhere = false;
	trybelow = false;
      } else {
	if (!(cl->flags & Replicator::test)) {
	  assert(cl->dparent != NULL);
	}
      }
    }

    // Replicate the current object into destination if needed
    if (copyhere) {
      if (cl->flags & Replicator::verbose) {
	cout << (svs->type == VestaSource::appendableDirectory
		 ? "create\t" : "copy\t") << svsname << endl;
      }
      if (cl->flags & Replicator::test) {
	// Kludge...
	if (svs->type == VestaSource::immutableDirectory) {
	  dvs = svs;
	} else {
	  dvs = cl->replicator->droot; //ugh
	}
      } else {
	const char* pathname = svsname.cchars() + strlen("/vesta");
	if (*pathname == '/') pathname++;
	err = VDirSurrogate::replicate(pathname, false, false,
				       cl->replicator->dhost,
				       cl->replicator->dport,
				       cl->replicator->shost,
				       cl->replicator->sport,
				       PathnameSep,
				       cl->replicator->dwho,
				       cl->replicator->swho);
	if (err != VestaSource::ok) {
	  if(err == VestaSource::longIdOverflow) {
	    // Continue on LongId overflow
	    cerr << "warning: " << ReposUI::errorCodeText(err) 
		 << ", inserting " << svsname << endl;
	    return true;
	  }
	  else
	    throw(Replicator::Failure(err, Text("inserting ") + svsname));
	}   
	err = cl->dparent->lookup(arc, dvs, cl->replicator->dwho);
	if(err == VestaSource::longIdOverflow)
	  {
	    // Continue on LongId overflow
	    return true;
	  }
	else if (err != VestaSource::ok) {
	  throw(Replicator::Failure(err, Text("lookup ") + svsname
				    + Text(" in destination")));
	}
      }
    }
  }

  // Recurse if something below might match
  if (trybelow && (svs->type == VestaSource::appendableDirectory ||
		   svs->type == VestaSource::immutableDirectory)) {
    ReplicatorDirClosure childcl;
    childcl.replicator = cl->replicator;
    childcl.sparent = svs;
    childcl.swho = cl->replicator->swho;
    childcl.dwho = cl->replicator->dwho;
    childcl.dparent = dvs;
    childcl.parentName = svsname;
    childcl.parentArc = arc;
    childcl.directives = childDirectives;
    childcl.flags = cl->flags;
    childcl.firstLastKnown = false;
    childcl.destFirstLastKnown = false;
    childcl.first = childcl.last = -1;
    childcl.dfirst = childcl.dlast = -1;
    childcl.parentcl = cl;
    childcl.matchcount = 0;
    err = svs->list(0, doOneEntry, &childcl, cl->replicator->swho);
    if (err != VestaSource::ok) {
      throw(Replicator::Failure(err, Text("listing ") + svsname));
    }

    if (childcl.dparent &&
	(!dvs || childcl.dparent->type != dvs->type)) {
      copybelow = true;
      if (dvs) delete dvs;
      dvs = childcl.dparent;
    }
    matchbelow = (childcl.matchcount > 0);
    if (matchbelow && !matchhere) cl->matchcount++;
  }
   
  // Copy "latest" link if directed by flags
  if ((cl->flags & Replicator::latest) && (matchhere || matchbelow) && dvs &&
      svs->type == VestaSource::appendableDirectory &&
      dvs->type == VestaSource::appendableDirectory &&
      (svs->inAttribs("type", "package") ||
       svs->inAttribs("type", "checkout") ||
       svs->inAttribs("type", "session"))) {
    copyLatest(svs, dvs, svsname, cl);
  }

  // Copy attribs if directed by flags
  if ((cl->flags & Replicator::attrNew) && (copyhere || copybelow) ||
      (cl->flags & Replicator::attrOld) && matchhere && dvs ||
      (cl->flags & Replicator::attrInner) && !matchhere && matchbelow) {
    cl->replicator->copyAttribs(svs, svsname, dvs, cl->flags);
  }

  // Clean up
  delete svs;
  if (dvs) delete dvs;
  if (childDirectives) delete childDirectives;

  // Return false to stop the caller's list() method in the special
  // case where (1) we matched the current object or a child copied
  // it, (2) its parent is immutable.  In this case nothing can be
  // changed by examining the siblings.
  if ((matchhere || copybelow) &&
      cl->sparent->type == VestaSource::immutableDirectory) {
    return false;
  }

  return true;
}

void
Replicator::replicate(DirectiveSeq* directives, Replicator::Flags flags)
  throw (Replicator::Failure, SRPC::failure)
{
  int i;
  bool allin = false;
  bool allout = false;
  bool rootin = false;
    
  // Sanity check that patterns are relative and signs are valid,
  // and check if "/vesta" itself is included (rootin).
  // Also (for the heck of it) optimize the special cases (1)
  // everything included, (2) everything excluded.
  if (directives) {
    for (i = 0; i < directives->size(); i++) {
      Directive d = directives->get(i);
      if (d.pattern[0] == '/') {
	throw(Failure(Text("pattern not relative: ")
		      + d.sign + " " + d.pattern));
      }
      if (d.sign == '+') {
	allout = false;
	if (d.pattern.Empty()) {
	  rootin = true;
	  allin = true;
	}
      } else if (d.sign == '-') {
	allin = false;
	if (d.pattern.Empty()) {
	  rootin = false;
	  allout = true;
	}
      } else {
	throw(Failure(Text("invalid directive operator: ")
		      + d.sign + " " + d.pattern));
      }
    }
  }
    
  // Handle special cases
  if (allin) {
    directives = NULL;
  } else if (allout) {
    return;
  }

  // Start the recursion
  ReplicatorDirClosure cl;
  cl.replicator = this;
  cl.sparent = sroot;
  cl.swho = this->swho;
  cl.dwho = this->dwho;
  cl.dparent = droot;
  cl.parentName = "/vesta";
  cl.parentArc = "vesta";
  cl.directives = directives;
  cl.flags = flags;
  cl.firstLastKnown = false;
  cl.destFirstLastKnown = false;
  cl.first = cl.last = -1;
  cl.dfirst = cl.dlast = -1;
  cl.parentcl = NULL;
  cl.matchcount = 0;
  sroot->list(0, doOneEntry, &cl, swho);

  // Copy attribs on /vesta itself if needed
  if ((flags & Replicator::attrOld) && rootin ||
      (flags & Replicator::attrInner) && cl.matchcount) {
    copyAttribs(sroot, "/vesta", droot, flags);
  }
}


// Constructor for the Replicator class.  Remembers the source and
//  destination repositories and certain key directories in them.
Replicator::Replicator(Text source_host, Text source_port, 
		       Text dest_host, Text dest_port,
		       AccessControl::Identity source_who,
		       AccessControl::Identity dest_who)
: shost(source_host),
  sport(source_port),
  dhost(dest_host),
  dport(dest_port),
  swho(source_who),
  dwho(dest_who),
  sroot(VDirSurrogate::LongIdLookup(RootLongId, shost, sport, source_who)),
  droot(VDirSurrogate::LongIdLookup(RootLongId, dhost, dport, dest_who))
{
  VestaSource::errorCode err; 
  // Sanity checking
  assert(sroot->type == VestaSource::appendableDirectory);
  assert(droot->type == VestaSource::appendableDirectory);
}

bool Replicator::hasMetacharacters(const Text &string) throw()
{
  return ((string.FindChar('*') != -1) ||
	  (string.FindChar('#') != -1) ||
	  (string.FindChar('?') != -1) ||
	  (string.FindChar('[') != -1) ||
	  (string.FindChar('{') != -1) ||
	  (string.FindChar('%') != -1) ||
	  (string.FindText("FIRST") != -1) ||
	  (string.FindText("LAST") != -1) ||
	  (string.FindText("DFIRST") != -1) ||
	  (string.FindText("DLAST") != -1));
}

// Forward declaration of one of a pair of mutually recursive
// functions.
static void expandOneLevel(ExpansionDirClosure *cl);

static bool expandOneLevelCallback(void* closure,
				   VestaSource::typeTag type, Arc arc,
				   unsigned int index, Bit32 pseudoInode,
				   ShortId filesid, bool master)
{
  ExpansionDirClosure *cl = (ExpansionDirClosure*) closure;

  assert(!cl->pattern.Empty());

  Text head, tail;

  // Handle %-patterns first.
  if(cl->pattern[0] == '%')
    {
      // %X :== head(X)/%X | tail(X)

      // First we handle the head(X)/%X case.

      splitHeadTail(cl->pattern.Sub(1), head, tail);
      if(head.Empty())
	{
	  throw(Replicator::Failure("syntax error using %"));
	}

      // X matches.  Recurse using the current pattern to match
      // children.
      if(arcPatternMatch(arc, head.cchars(), cl))
	{
	  VestaSource *childvs = 0;
	  VestaSource::errorCode err =
	    cl->sparent->lookup(arc, childvs /*OUT*/, cl->swho);
	  if(err == VestaSource::longIdOverflow)
	    {
	      // Continue on LongId overflow
	      return true;
	    }
	  else if(err != VestaSource::ok)
	    {
	      throw(Replicator::Failure(err, ("looking up \"" + Text(arc) +
					      "\" in " + cl->parentName)));
	    }

	  ExpansionDirClosure childcl;
	  childcl.sparent = childvs;
	  childcl.swho = cl->swho;
	  childcl.parentName = cl->parentName + "/" + arc;
	  childcl.parentArc = arc;
	  childcl.firstLastKnown = false;
	  childcl.destFirstLastKnown = false;
	  childcl.first = childcl.last = -1;
	  childcl.parentcl = NULL;
	  childcl.pattern = cl->pattern;
	  childcl.callback = cl->callback;
	  childcl.closure = cl->closure;

	  // check the destination
	  childcl.dwho = cl->dwho;
	  childcl.dfirst = childcl.dlast = -1;
	  childcl.dparent = NULL;
	  if(cl->dparent) 
	    {
	      VestaSource* dchildvs = NULL;
	      VestaSource::errorCode dest_err = 
		cl->dparent->lookup(arc, dchildvs /*OUT*/, cl->dwho);
	      if(dest_err == VestaSource::ok)
		childcl.dparent = dchildvs;
	    }

	  expandOneLevel(&childcl);

	  // Now handle the tail(X) case.
	  childcl.pattern = tail;

	  expandOneLevel(&childcl);
	}
    }
  else
    {
      splitHeadTail(cl->pattern, head, tail);

      if(arcPatternMatch(arc, head.cchars(), cl))
	{
	  VestaSource *childvs = 0;
	  VestaSource::errorCode err =
	    cl->sparent->lookup(arc, childvs /*OUT*/, cl->swho);
	  if(err == VestaSource::longIdOverflow)
	    {
	      // Continue on LongId overflow
	      return true;
	    }
	  else if(err != VestaSource::ok)
	    {
	      throw(Replicator::Failure(err, ("looking up \"" + Text(arc) +
					      "\" in " + cl->parentName)));
	    }

	  ExpansionDirClosure childcl;
	  childcl.sparent = childvs;
	  childcl.swho = cl->swho;
	  childcl.parentName = cl->parentName + "/" + arc;
	  childcl.parentArc = arc;
	  childcl.firstLastKnown = false;
	  childcl.destFirstLastKnown = false;
	  childcl.first = childcl.last = -1;
	  childcl.parentcl = NULL;
	  childcl.pattern = tail;
	  childcl.callback = cl->callback;
	  childcl.closure = cl->closure;
	  
	  // check the destination
	  childcl.dwho = cl->dwho;
	  childcl.dfirst = childcl.dlast = -1;
	  childcl.dparent = NULL;
	  if(cl->dparent) 
	    {
	      VestaSource* dchildvs = NULL;
	      VestaSource::errorCode dest_err = 
		cl->dparent->lookup(arc, dchildvs /*OUT*/, cl->dwho);
	      if(dest_err == VestaSource::ok)
		childcl.dparent = dchildvs;
	    }
	  
	  expandOneLevel(&childcl);
	}
    }

  return true;
}

static void expandOneLevel(ExpansionDirClosure *cl)
{
  // Empth pattern: this entry matches, we're done.
  if(cl->pattern.Empty())
    {
      cl->callback(cl->closure, cl->parentName);
    }
  else
    {
      // Split pattern
      Text head, tail;
      splitHeadTail(cl->pattern, head, tail);

      // With metacharacters, we list all entries in a directory.  If
      // it's not a directory, skip it.
      if(Replicator::hasMetacharacters(head) &&
	 (cl->sparent->type == VestaSource::appendableDirectory ||
	  cl->sparent->type == VestaSource::immutableDirectory))
	{
	  if(cl->dparent)
	    {
	      if(cl->dparent->type != VestaSource::appendableDirectory &&
		 cl->dparent->type != VestaSource::immutableDirectory)
		{
		  delete cl->dparent;
		  cl->dparent = NULL;
		}
	    }

	  VestaSource::errorCode err =
	    cl->sparent->list(0, expandOneLevelCallback, cl, cl->swho);
	  if (err != VestaSource::ok)
	    {
	      throw(Replicator::Failure(err, (Text("listing ") +
					      cl->parentName)));
	    }
	}
      // Simple case: no metacharacters.  Just lookup the child entry.
      else
	{
	  VestaSource *childvs = 0;
	  VestaSource::errorCode err =
	    cl->sparent->lookup(head.cchars(), childvs /*OUT*/, cl->swho);
	  if(err == VestaSource::ok)
	    {
	      ExpansionDirClosure childcl;
	      childcl.sparent = childvs;
	      childcl.swho = cl->swho;
	      childcl.parentName = cl->parentName + "/" + head;
	      childcl.parentArc = head.cchars();
	      childcl.firstLastKnown = false;
	      childcl.destFirstLastKnown = false;
	      childcl.first = childcl.last = -1;
	      childcl.parentcl = NULL;
	      childcl.pattern = tail;
	      childcl.callback = cl->callback;
	      childcl.closure = cl->closure;
	      
	      // check the destination
	      childcl.dwho = cl->dwho;
	      childcl.dfirst = childcl.dlast = -1;
	      childcl.dparent = NULL;
	      if(cl->dparent) 
		{
		  VestaSource* dchildvs = NULL;
		  VestaSource::errorCode dest_err = 
		    cl->dparent->lookup(head.cchars(), dchildvs /*OUT*/, cl->dwho);
		  if(dest_err == VestaSource::ok)
		    childcl.dparent = dchildvs;
		}

	      expandOneLevel(&childcl);
	    }
	  else if((err != VestaSource::notFound) &&
		  (err != VestaSource::notADirectory) &&
		  // Ignore LongId overflow
		  (err != VestaSource::longIdOverflow))
	    {
	      throw(Replicator::Failure(err, ("looking up \"" + head +
					      "\" in " + cl->parentName)));
	    }
	}
    }
}

// This is the "core" implementation of Replicator::expandPattern,
// using the callback mechanism.
void Replicator::expandPattern(const Text &pattern,
			       Replicator::matchCallback callback,
			       void *closure,
			       Text shost, Text sport,
			       AccessControl::Identity swho,
			       Text dhost, Text dport, 
			       AccessControl::Identity dwho)
  // Note: no throw spec because this can throw anything "callback"
  // throws.
{
  // Default host/port
  if(shost.Empty())
    {
      shost = VestaConfig::get_Text("Repository", "VestaSourceSRPC_host");
    }
  if(sport.Empty())
    {
      sport = VestaConfig::get_Text("Repository", "VestaSourceSRPC_port");
    }

  // in a case of checking destination for DFIRST and/or DLAST
  // the destination host has to be specified
  bool check_dest_spec_tokens = true;
  if(dhost.Empty())
    {
      check_dest_spec_tokens = false;
    }
  else if(dport.Empty())
    {
      dport = sport;
    }

  // If the pattern starts with "/vesta/" or [UserInterface]AppendableRootName
  // remove that to make it relative.
  Text l_pattern = ReposUI::stripSpecificRoot(pattern.cchars(), ReposUI::VESTA);
 
  // Make sure it's relative.
  if(l_pattern[0] == '/')
    {
      throw(Failure(Text("pattern not relative: ") + l_pattern));
    }

  // Get the root from the source repository
  VestaSource *root = VDirSurrogate::LongIdLookup(RootLongId, shost, sport, swho);

  // Get the root from the destinantion repository
  VestaSource *droot = NULL;
  if(check_dest_spec_tokens)
    {
      droot = VDirSurrogate::LongIdLookup(RootLongId, dhost, dport, dwho);
    }
 
  ExpansionDirClosure cl;
  cl.sparent = root;
  cl.swho = swho;
  cl.parentName = "/vesta";
  cl.parentArc = "vesta";
  cl.firstLastKnown = false;
  cl.destFirstLastKnown = false;
  cl.first = cl.last = -1;
  cl.parentcl = NULL;
  cl.callback = callback;
  cl.closure = closure;
  cl.pattern = l_pattern;
  // set values of destination repository 
  cl.dparent = droot;
  cl.dwho = dwho;
  cl.dfirst = cl.dlast = -1;

  expandOneLevel(&cl);
}

// This class helps implement the "accumulate into a TextSeq" version
// of Replicator::expandPattern
class AccumulateMatchHandler
{
private:
  TextSeq &result;
public:
  AccumulateMatchHandler(TextSeq &r)
    : result(r)
  { }

  void operator()(const Text &match)
  {
    result.addhi(match);
  }
};

// This is the "classic" implementation of Replicator::expandPattern,
// now done using the template functor mechanism.
TextSeq Replicator::expandPattern(const Text &pattern,
				  Text shost, Text sport,
				  AccessControl::Identity swho,
				  Text dhost, Text dport, 
				  AccessControl::Identity dwho)
  throw(Replicator::Failure, SRPC::failure, 
	VestaConfig::failure, ReposUI::failure)
{
  TextSeq result;
  AccumulateMatchHandler match_handler(result);

  Replicator::expandPattern(pattern, match_handler,
			    shost, sport, swho,
			    dhost, dport, dwho);

  return result;
}
