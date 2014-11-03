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

 // Created on Wed Jul  2 16:10:28 PDT 1997 by heydon

// vimports(1) - print the models imported by a specified model

#include <unistd.h>    // for getcwd(3)
#include <Basics.H>
#include <Sequence.H>
#include <Generics.H>  // for TextSeq
#include <FS.H>
#include <ParseImports.H>

using std::ostream;
using std::cout;
using std::cerr;
using std::endl;

class ShowEntry {
  public:
    Text name; // name of partial path to elide
    int depth; // depth to elide that subtree (-1 => no ellision)
    ShowEntry() throw () { /* SKIP */ }
    ShowEntry(const Text &name, int depth) throw ()
	: name(name), depth(depth) { /*SKIP*/ }
};

typedef Sequence<ShowEntry> ShowEntrySeq;

void Syntax(char *msg, char *arg = (char *)NULL) throw ()
{
    cerr << "Error: " << msg;
    if (arg != (char *)NULL) cerr << ": '" << arg << "'";
    cerr << endl;
    cerr << "Syntax: vimports [-depth num] "
	 << "[-elide name]... [-show name [depth]]... "
         << "[model]" << endl;
    exit(1); /* Status == 1 implies command line parse error */
}

void Indent(ostream &os, int n) throw ()
{
    for (/*SKIP*/; n > 0; n--) os << "| ";
}

typedef Table<Text,bool>::Default SeenModelTbl;

int Search(const Text &model, int depth, int elideDepth,
	   const TextSeq &elideNames, const ShowEntrySeq &showNames,
	   // Should the output be just model names and nothing else?
	   bool plain,
	   // Should the output only show each model once?  If so,
	   // "seenModels" is used to remember which ones we have
	   // already processed.
	   bool unique, SeenModelTbl &seenModels)
  throw ()
{
    // Before we do anything else, skip this model if we're only
    // printing each once and we've seen this model before
    bool unused;
    if(unique && seenModels.Get(model, /*OUT*/ unused)) return 0;

  /* Status = 0:  Success
              1:  Command line parse error
              2:  Model-file-not-found error
              3:  Model file parse error
	      4:  Filesystem (e.g. read) error
  */
    int status = 0; /* Assume success */
    if(!plain) Indent(cout, depth);
    cout << model;
    if(unique) (void) seenModels.Put(model, true);
    ImportSeq imports(/*sizehint=*/ 10);
    try {
	ParseImports::P(model, /*INOUT*/ imports);

	// test if this subtree should be elided
	bool elide = false;
	for (int i = 0; i < elideNames.size(); i++) {
	    if (model.FindText(elideNames.get(i)) >= 0) {
		elide = true;
		break;
	    }
	}

	if (!elide) {
	    // test if this subtree should be shown
	    bool show = false;
	    int maxShowDepth = 0;
	    for (int i = 0; i < showNames.size(); i++) {
		if (model.FindText(showNames.get(i).name) >= 0) {
		    show = true;
		    if (maxShowDepth >= 0) {
			int thisDepth = showNames.get(i).depth;
			if (thisDepth < 0) maxShowDepth = thisDepth;
			else maxShowDepth = max(maxShowDepth, thisDepth);
		    }
		}
	    }
	    if (show) {
		// set "elideDepth" from "maxShowDepth"
		elideDepth = maxShowDepth;
		if (elideDepth >= 0) elideDepth += depth;
	    }
	}

	// compute whether to recurse or not
	if (!elide && (elideDepth < 0 || depth < elideDepth)) {
	    cout << endl;
	    while (imports.size() > 0) {
	      Import imp(*(imports.getlo()));
	      delete imports.remlo();
	      Search(imp.path, depth+1, elideDepth, elideNames, showNames,
		     plain, unique, seenModels);
	    }
	} else {
	    if (!plain && (imports.size() > 0)) cout << "...";
	    cout << endl;
	}
    }
    catch (FS::DoesNotExist) {
	cerr << endl;
	if(!plain) Indent(cerr, depth);
	cerr << "vimports: model file " << model << " does not exist" << endl;
        status = 2;
    }
    catch (const FS::Failure &f) {
	cerr << endl << "vimports: " << f << endl;
        status = 4;
    }
    catch (const ParseImports::Error &err) {
	cerr << endl;
	if(!plain) Indent(cerr, depth);
	cerr << endl << "vimports: " << err << endl;
        status = 3;
    }
    // Free any remaining Import objects.
    while (imports.size() > 0)
      delete imports.remlo();
    return status;
}

int main(int argc, char *argv[]) 
{
  /* Status = 0:  Success
              1:  Command line parse error
              2:  Model-file-not-found error
              3:  Model file parse error
	      4:  Filesystem (e.g. read) error
  */
  int status; /* Set only by Search(); parse errors exit via Syntax() call... */
    // command-line arguments
    int elideDepth = -1;
    TextSeq elideNames;
    ShowEntrySeq showNames;
    bool plain = false;
    bool unique = false;
    SeenModelTbl seenModels;

    // parse command-line
    int arg = 1;
    while (arg < argc && *(argv[arg]) == '-') {
	if (strcmp(argv[arg], "-depth") == 0) {
	    arg++;
	    if (arg < argc) {
		int res = sscanf(argv[arg], "%d", &elideDepth);
		if (res != 1) {
		    Syntax("argument to '-depth' not a number", argv[arg]);
		}
		arg++;
	    } else {
		Syntax("no argument to '-depth' switch");
	    }
	} else if (strcmp(argv[arg], "-elide") == 0) {
	    arg++;
	    if (arg < argc) {
		elideNames.addhi(argv[arg++]);
	    } else {
		Syntax("no argument to '-elide' switch");
	    }
	} else if (strcmp(argv[arg], "-show") == 0) {
	    arg++;
	    if (arg < argc) {
		int depth = -1; // default if none specified
		Text name(argv[arg++]);
		if (arg < argc) {
		    int res = sscanf(argv[arg], "%d", &depth);
		    if (res == 1) {
			arg++;
		    } else if (*argv[arg] != '-' && arg != argc - 1) {
			Syntax("bad depth argument to '-show' switch",
			       argv[arg]);
		    }
		}
		showNames.addhi(ShowEntry(name, depth));
	    } else {
		Syntax("no argument to '-show' switch");
	    }
	} else if (strcmp(argv[arg], "-plain") == 0) {
	  arg++;
	  // Note that internally the "plain" and "unique" options are
	  // separate, but the -plain command-line argument sets both
	  // of them (mainly because we couldn't think of how one
	  // would use the output from "plain && !unique").
	  plain = true;
	  unique = true;
	} else {
	    Syntax("unrecognized argument", argv[arg]);
	}
    }
    if (argc > arg+1) {
      Syntax("too many arguments");
    }
    Text model((arg < argc) ? argv[arg] : ".main.ves");

    // get the working directory
    char wd_buff[PATH_MAX+1];
    char *res = getcwd(wd_buff, PATH_MAX+1); assert(res != (char *)NULL);
    res = strcat(wd_buff, "/"); assert(res != (char *)NULL);
    Text wd(wd_buff);

    // search for imports.
    // Could return the status directly, but this gives some additional debuggability.
    status = Search(ParseImports::ResolvePath(model, wd),
      /*depth=*/ 0, /*elideDepth=*/ elideDepth,
		    elideNames, showNames, plain,
		    unique, seenModels);
    return status;
}
    
