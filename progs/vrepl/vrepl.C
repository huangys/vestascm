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
// vrepl.C
//

// Command-line driver program for Replicator interface

#include <Basics.H>
#include <BufStream.H>
#include <Text.H>
#include <Generics.H>
#include <FP.H>
#include <FS.H>
#include <ReposUI.H>
#include <VDirSurrogate.H>
#include <AccessControl.H>
#include <ParseImports.H>
#include <RemoteModelSpace.H>
#include "Replicator.H"

#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif

using std::istream;
using std::ifstream;
using std::cout;
using std::cin;
using std::cerr;
using std::endl;

using ParseImports::RemoteModelSpace;
using ParseImports::OptimizedModelSpace;

#define DEFAULTS (Replicator::attrNew | Replicator::attrOld | \
                  Replicator::attrAccess | \
		  Replicator::inclStubs | Replicator::inclGhosts | \
                  Replicator::latest)
#define MAX_LINE_LEN (PATH_MAX + 10)

Text program_name;

// Exit with an error message.  If err is supplied, take it as an 
//  errno value and include the standard system message for that value.
static void
abandon(Text msg, int err = -1)
{
    if (err == -1) {
	cerr << program_name << ": "
	  << msg << endl;
    } else {
	cerr << program_name << ": " << Basics::errno_Text(err) << ", "
	  << msg << endl;	
    }
    exit(2);
}

// Exit with a usage message.
void
Usage()
{
    cerr << "Usage: " << program_name <<
      " -n -o -i -a -r -m -b -g -l -v -t -w -s -c"
      " srchost[:port] -d dsthost[:port]"
      << endl << "  -e directive [-f] directives-file" << endl;
    exit(1);
}

// Recursive function to process model imports.  Replicates the given
// model (and thus the package version where it lives) and recursively
// each model it imports.  Works by adding '+' directives to the given
// DirectiveSeq.
//
// The recurseOnly flag prevents the model itself from being
// replicated; it is used internally to avoid replicating the same
// package repeatedly due to local imports. 
//
void
importSearch(Replicator::DirectiveSeq* directives, Replicator::Flags flags,
	     ParseImports::ModelSpace* space,
	     Table<Text,bool>::Default &modelsVisited,
	     const Text &model, bool recurseOnly = false)
     throw (Replicator::Failure, FS::Failure, ParseImports::Error)
{
#if VERBOSE_IMPORTS
    if (flags & Replicator::verbose) {
	cout << "model\t" << model << endl;
    }
#endif
    // Note that we've visited this one so we don't process it a
    // second time.
    modelsVisited.Put(model, true);
    // Add a + directive if needed.
    if (!recurseOnly) {
	Replicator::Directive d;
	d.sign = '+';
	try
	{
	  d.pattern = ReposUI::stripSpecificRoot(model.cchars(), ReposUI::VESTA);
	}
	catch(ReposUI::failure f)
	{
	  abandon(f.msg);
	}
	catch(VestaConfig::failure f)
	{
	  abandon(f.msg);
	}
	directives->addhi(d);
    }
    ImportSeq imports(/*sizehint=*/ 10);
    try
      {
	ParseImports::P(model, /*INOUT*/ imports, space);
      }
    // In the event of problems reading or parsing the model, either
    // issue a warning and continue or throw the exception up to the
    // caller, depending on whether we're supposed to continue on
    // problems with imports.
    catch(FS::Failure f)
      {
	if(flags & Replicator::warnBadImp)
	  {
	    cerr << program_name << ": " << f << endl;
	  }
	else
	  {
	    throw;
	  }
      }
    catch(ParseImports::Error f)
      {
	if(flags & Replicator::warnBadImp)
	  {
	    cerr << program_name << ": " << f << endl;
	  }
	else
	  {
	    throw;
	  }
      }
    while (imports.size() > 0) {
	Import* imp = imports.remlo();
	// Recurse on this import only if we haven't already visited
	// it.
	bool unused;
	if(!modelsVisited.Get(imp->path, unused))
	  {
	    importSearch(directives, flags, space, modelsVisited,
			 imp->path, imp->local);
	  }
    }
}

// A match handler used with Replicator::expandPattern for import (@)
// directives which contain metachracaters
class ImportSearchMatchHandler
{
private:
  // These are passed to the constructor and passed through to
  // importSearch for each match.
  Replicator::DirectiveSeq &directives;
  Replicator::Flags flags;
  ParseImports::ModelSpace* space;

  // This is initialized to empty for each instance.
  Table<Text,bool>::Default modelsVisited;
public:
  ImportSearchMatchHandler(Replicator::DirectiveSeq &direcs,
			   Replicator::Flags fl,
			   ParseImports::ModelSpace* sp)
    : directives(direcs), flags(fl), space(sp)
  { }

  void operator()(const Text &match)
  {
    importSearch(&directives, flags, space,
		 modelsVisited,
		 match);

    // Note that we don't clear the set of models visited between
    // matches.  Since the imports of all these models will expand to
    // a contiguous sequence of '+' directives, it's safe to process
    // each model only once across all matches of the pattern.
  }
};

// Forward
void
parseDirectives(Replicator* repl, Replicator::Flags flags, istream& ist,
		Replicator::DirectiveSeq& directives, bool& plusOrAtSeen,
		Replicator::Pattern& lastPlus)
     throw (Replicator::Failure, FS::Failure, ParseImports::Error, 
	    VestaConfig::failure, ReposUI::failure);

// 
// Parse one directive
//
void
parseDirective(Replicator* repl, Replicator::Flags flags,
	       Replicator::DirectiveSeq& directives, char* directive,
	       bool& plusOrAtSeen, Replicator::Pattern& lastPlus)
     throw (Replicator::Failure, ParseImports::Error, FS::Failure,
	    VestaConfig::failure, ReposUI::failure)
{
  char oper;
  char *t;
  Replicator::Directive d;
  static RemoteModelSpace* space = NULL;

  oper = *directive++;
  while (isspace(*directive)) directive++;
  // trim trailing whitespace  (assume no newline)
  t = directive + (strlen(directive) - 1);
  while (isspace(*t)) t--;
  *(++t) = '\0';

  switch (oper) {
  case '+':
    // Replicate tree rooted here
    plusOrAtSeen = true;
    d.sign = '+';
    lastPlus = d.pattern = 
      ReposUI::stripSpecificRoot(directive, ReposUI::VESTA);
    directives.addhi(d);
    break;

  case '-':
    // Exclude tree rooted here
    if (!plusOrAtSeen) {
      abandon(Text("- pattern before first + or @ pattern"));
    }
    d.sign = '-';
    // - patterns may be relative to last + pattern
    if (directive[0] != '/') {
      if (lastPlus.Empty() || lastPlus[lastPlus.Length()-1] == '/') {
	d.pattern = lastPlus + directive;
      } else {
	d.pattern = lastPlus + "/" + directive;
      }
    } else {
      d.pattern = ReposUI::stripSpecificRoot(directive, ReposUI::VESTA);
    }
    directives.addhi(d);
    break;

  case '@':
  case '>': // backward compatibility
    plusOrAtSeen = true;
    if (!space) {
      space = NEW_CONSTR(RemoteModelSpace, (repl->sroot, repl->swho));
    }
    // If the pattern has metacharacters, expand it in the source
    // repository.
    if(Replicator::hasMetacharacters(directive))
      {
	// This will call importSearch once for each match.  (See the
	// class definition above.)
	ImportSearchMatchHandler handler(directives, flags, space);

	// Expand the pattern and process the imports of each match
	// found.
	Replicator::expandPattern(directive, handler,
				  repl->shost, repl->sport, repl->swho, 
				  repl->dhost, repl->dport, repl->dwho);
      }
    // Otherwise it's an absolute path, so just convert its imports to
    // directives.
    else
      {
	Table<Text,bool>::Default modelsVisited;
	importSearch(&directives, flags, space, modelsVisited, directive);
      }
    break;

  case '#':
    // Comment
    break;

  case '.':
    // Indirection file
    if (strcmp(directive, "-") == 0) {
      parseDirectives(repl, flags, cin, directives, plusOrAtSeen, lastPlus);
    } else {
      ifstream ifs;
      if(FS::IsDirectory(directive)) {
	abandon(Text(directive) + " is a directory. (Did you mean '-e +" 
		+ Text(directive) + "'?)");
      }
      try {   
	FS::OpenReadOnly(directive, ifs);
      }
      catch (FS::DoesNotExist) {
	abandon(Text("nonexistent directive file: ") + Text(directive));
      } 
      parseDirectives(repl, flags, ifs, directives, plusOrAtSeen, lastPlus);
      FS::Close(ifs);
    }
    break;

  case '\n':
  case '\r':
  case '\0':
    break;

  default:
    abandon(Text("invalid directive: ") + Text((int)oper) + " " + directive); 
    break;
  }
}

//
// Parse the directive stream ist
//
void
parseDirectives(Replicator* repl, Replicator::Flags flags, istream& ist,
		Replicator::DirectiveSeq& directives, bool& plusOrAtSeen,
		Replicator::Pattern& lastPlus)
     throw (Replicator::Failure, FS::Failure, ParseImports::Error, 
	    VestaConfig::failure, ReposUI::failure)
{
  char buf[MAX_LINE_LEN+1];

  // Until the stream ends or becomes unusable...
  while(!ist.eof() && !ist.bad())
  {
    // Make sure it doesn't already look like a zero-length string so
    // we don't mistake another failure for an empty line.
    if(buf[0] == 0)
      buf[0] = 1;

    // Get the next line.
    ist.get(buf, sizeof(buf));

    // Empty line: failbit may be set by some stream implementations.
    if(buf[0] == 0)
    {
      if(ist.fail() && !ist.bad())
        ist.clear(ist.rdstate() & ~istream::failbit);
    }

    // If we haven't reached the end of the file, make sure the next
    // character is the delimiter.
    if(!ist.eof())
    {
      char c;
      if(ist.get(c) && c != '\n')
      {
        abandon(Text("directive line too long: ") + buf + "...");
      }
    }

    // Parse this line
    parseDirective(repl, flags, directives, buf, plusOrAtSeen, lastPlus);
  }
  if (!ist.eof()) {
    abandon("directive file read error");
  }
}

void
parserepos(/*IN*/const char* arg, /*OUT*/Text& host, /*OUT*/Text& port,
	   /*OUT*/AccessControl::Identity& who)
{
  // Establish defaults
  host = VestaConfig::get_Text("Repository", "VestaSourceSRPC_host");
  port = VestaConfig::get_Text("Repository", "VestaSourceSRPC_port");
  who = NULL;

  if (strcmp(arg, "local") == 0) return;

  char *colon = strchr(arg, ':');
  if (colon == NULL) {
    host = arg;
    return;
  }
  host = Text(arg, colon - arg);
  
  char *colon2 = strchr(colon + 1, ':');
  if (colon2 == NULL) {
    port = Text(colon + 1);
    return;
  }
  int portlen = colon2 - (colon + 1);
  if (portlen > 0) {
    port = Text(colon + 1, portlen);
  }

  who = NEW_CONSTR(AccessControl::GlobalIdentityRep, (strdup(colon2 + 1)));
}


int
main(int argc, char* argv[])
{
  Text shost, sport, dhost, dport;
  AccessControl::Identity swho = NULL, dwho = NULL;
  Replicator* repl;
  char *src = "local", *dst = "local";
  Text rootDirectives = "";

  program_name = argv[0];
  
  try {
    opterr = 0;
    int flags = DEFAULTS;
    for (;;) {
      int c = getopt(argc, argv, "noiarmbglvtwcNOIARMBGLVTWCs:d:e:f:");
      if (c == EOF) break;
      switch (c) {
      case 'n':
	flags |= Replicator::attrNew;
	break;
      case 'N':
	flags &= ~Replicator::attrNew;
	break;
      case 'o':
	flags |= Replicator::attrOld;
	break;
      case 'O':
	flags &= ~Replicator::attrOld;
	break;
      case 'i':
	flags |= Replicator::attrInner;
	break;
      case 'I':
	flags &= ~Replicator::attrInner;
	break;
      case 'a':
	flags |= Replicator::attrAccess;
	break;
      case 'A':
	flags &= ~Replicator::attrAccess;
	break;
      case 'r':
	flags |= Replicator::revive;
	break;
      case 'R':
	flags &= ~Replicator::revive;
	break;
      case 'm':
	flags |= Replicator::reviveMA;
	break;
      case 'M':
	flags &= ~Replicator::reviveMA;
	break;
      case 'b':
	flags |= Replicator::inclStubs;
	break;
      case 'B':
	flags &= ~Replicator::inclStubs;
	break;
      case 'g':
	flags |= Replicator::inclGhosts;
	break;
      case 'G':
	flags &= ~Replicator::inclGhosts;
	break;
      case 'l':
	flags |= Replicator::latest;
	break;
      case 'L':
	flags &= ~Replicator::latest;
	break;
      case 'v':
	flags |= Replicator::verbose;
	break;
      case 'V':
	flags &= ~Replicator::verbose;
	break;
      case 't':
	flags |= Replicator::test;
	break;
      case 'T':
	flags &= ~Replicator::test;
	break;
      case 'w':
	flags |= Replicator::warnBadImp;
	break;
      case 'W':
	flags &= ~Replicator::warnBadImp;
	break;
      case 'c':
	flags &= ~Replicator::dontCopyNew;
	break;
      case 'C':
	flags |= Replicator::dontCopyNew;	
	break;
      case 's':
	src = optarg;
	break;
      case 'd':
	dst = optarg;
	break;
      case 'e':
	if (strlen(optarg) == 1 && optind < argc) {
	  // allow a space after the operator character
	  rootDirectives =
	    rootDirectives + optarg + " " + argv[optind++] + "\n";
	} else {
	  rootDirectives = rootDirectives + optarg + "\n";
	}
	break;
      case 'f':
	rootDirectives = rootDirectives + ". " + optarg + "\n";
	break;
      case '?':
      default:
	Usage();
	// not reached
	break;
      }
    }

    while (optind < argc) {
      rootDirectives = rootDirectives + ". " + argv[optind++] + "\n";
    }

    parserepos(src, shost, sport, swho);
    parserepos(dst, dhost, dport, dwho);

    if (shost == dhost && sport == dport) {
      abandon("source and destination must be different repositories");
    }

    repl = NEW_CONSTR(Replicator, (shost, sport, dhost, dport, swho, dwho));

    Basics::IBufStream ist(rootDirectives.cchars());

    Replicator::DirectiveSeq directives;
    Replicator::Pattern lastPlus;
    bool plusOrAtSeen = false;

    parseDirectives(repl, (Replicator::Flags) flags, ist,
		    directives, plusOrAtSeen, lastPlus);

    repl->replicate(&directives, (Replicator::Flags) flags);

  } catch (VestaConfig::failure f) {
    cerr << program_name << ": " << f.msg << endl;
    exit(2);
  } catch (SRPC::failure f) {
    cerr << program_name
	 << ": SRPC::failure " << f.msg << " (" << f.r << ")" << endl;
    exit(2);
  } catch (Replicator::Failure f) {
    if (f.code == (VestaSource::errorCode) -1) {
      cerr << program_name << ": " << f.msg << endl;
    } else {
      cerr << program_name << ": " << ReposUI::errorCodeText(f.code)
	   << ", " << f.msg << endl;
    }
    exit(2);
  } catch (FS::Failure f) {
    // FS makes it impossible to print errors my own way, 
    // so I bow to its way of printing.
    cerr << program_name << ": " << f << endl;
    exit(2);
  } catch (ParseImports::Error f) {
    cerr << program_name << ": " << f << endl;
    exit(2);
  } catch (ReposUI::failure f) {
    cerr << program_name << ": " << f.msg << endl;
    exit(2);
  }
  return 0;
}
