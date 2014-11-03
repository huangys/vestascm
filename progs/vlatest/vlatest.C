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
// vlatest.C
//

// Print the latest versions of packages
// See documentation in vlatest.1.mtex

#include <Basics.H>
#include <Text.H>
#include <VestaConfig.H>
#include <VestaSource.H>
#include <VDirSurrogate.H>
#include "ReposUI.H"

#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif

using std::cout;
using std::cerr;
using std::endl;

enum Recursion {
    r_none, r_pkg, r_branch, r_checkout
  };

Text program_name, defpkgpar;
int errcount = 0;

void
Usage()
{
    cerr << "Usage: " << program_name
      <<  " [-n | -p | -b | -c] [-m] [-t] [-v] [-R repos] [package]" << endl;
    exit(1);
}

struct verclosure {
    VestaSource* parent;
    Text prefix;
    Recursion recurse;
    bool mast;
    int elide;
    int high;
    bool comment;
    Text highText;
    unsigned int highIndex;
    VestaSource* highvervs;
    Text lhost, lport;
};

static bool
vercallback(void* closure, VestaSource::typeTag type, Arc arc,
	    unsigned int index, Bit32 pseudoInode, ShortId filesid,
	    bool master);

static void
printlatest(VestaSource* vs, Text prefix, Recursion recurse,
	    bool mast, int elide, bool comment, Text lhost, Text lport)
{
    verclosure cl;
    cl.parent = vs;
    cl.prefix = prefix;
    cl.recurse = recurse;
    cl.mast = mast;
    cl.elide = elide;
    cl.high = -1;
    cl.comment = comment;
    cl.highvervs = NULL;
    cl.lhost = lhost;
    cl.lport = lport;
    VestaSource::errorCode err = vs->list(0, vercallback, &cl);
    if (err != VestaSource::ok) {
	cerr << program_name 
	  << ": error listing directory " << cl.prefix << ": "
	    << ReposUI::errorCodeText(err) << endl;
	errcount++;
    }
    if (cl.high != -1) {
      cout << prefix.Sub(elide) << cl.highText << endl;
      if(cl.comment) {
	VestaSource *vs_highver = cl.highvervs;
	if(vs_highver == 0)
	  {
	    err = vs->lookupIndex(cl.highIndex, vs_highver);
	    if (err != VestaSource::ok) {
	      cerr << program_name << ": error on lookupIndex: "
		   << ReposUI::errorCodeText(err) << endl;
	      errcount++;
	    }
	  }
	if(vs_highver != 0 )
	  {
	    char* msg = vs_highver->getAttrib("message");
	    if(msg) {
	      cout << "\t" << msg << endl;
	      delete msg;
	    }
	    delete vs_highver;
	  }
      }
    }
}

static bool maybe_highest(verclosure* cl, Arc arc, long &val)
{
  char* endptr;
  if (arc[0] != '0' || arc[1] == '\0') {
    val = strtol(arc, &endptr, 10);
    if (*endptr == '\0' && val > cl->high) {
      return true;
    }
  }
  return false;
}

static VestaSource *lookup_child(verclosure* cl, Arc arc, unsigned int index, bool master)
{
  VestaSource *vs = 0;
  if (cl->mast && !master) {
    try {
      vs = ReposUI::filenameToMasterVS(cl->prefix + arc,
				       cl->parent->host() + ":" +
				       cl->parent->port() + " " +
				       cl->lhost + ":" + cl->lport);
    } catch (ReposUI::failure f) {
      cerr << program_name << ": " << f.msg << endl;
      errcount++;
    }
  } else {
    VestaSource::errorCode err = cl->parent->lookupIndex(index, vs);
    if (err != VestaSource::ok) {
      cerr << program_name << ": error on lookupIndex: "
	   << ReposUI::errorCodeText(err) << endl;
      errcount++;
    }
  }
  return vs;
}

static void maybe_recurse(verclosure* cl, Arc arc, VestaSource* vs)
{
  if ((cl->recurse >= r_checkout ||
       !vs->inAttribs("type", "checkout")) &&
      (cl->recurse >= r_branch ||
       !vs->inAttribs("type", "branch"))) {
    // OK to recurse
    printlatest(vs, cl->prefix + arc + "/", cl->recurse, cl->mast, 
		cl->elide, cl->comment, cl->lhost, cl->lport);
  }
}

static bool is_symlink(verclosure* cl, unsigned int index, VestaSource*& vs)
{
  vs = 0;
  VestaSource::errorCode err = cl->parent->lookupIndex(index, vs);
  bool result = false;
  if (err == VestaSource::ok)
    {
      char *symlink_to = vs->getAttrib("symlink-to");
      if(symlink_to != 0)
	{
	  result = true;
	  delete [] symlink_to;
	}
    }
  return result;
}

static bool
vercallback(void* closure, VestaSource::typeTag type, Arc arc,
	    unsigned int index, Bit32 pseudoInode, ShortId filesid,
	    bool master)
{
    verclosure* cl = (verclosure*) closure;
    VestaSource* vs = 0;
    bool delete_vs = true;

    if ((type == VestaSource::immutableDirectory) ||
	(type == VestaSource::immutableFile)) {
      long val;
      if(maybe_highest(cl, arc, val))
	{
	  cl->high = val;
	  cl->highText = arc;
	  cl->highIndex = index;
	  cl->highvervs = 0;
	}

      return true;
    }
    else if (type == VestaSource::appendableDirectory) {
	if (cl->recurse == r_none) return true;

	vs = lookup_child(cl, arc, index, master);
	if(vs == 0) return true;

	maybe_recurse(cl, arc, vs);
    }
    else if(cl->mast)
      {
	// More work is required if we must go to the master and we've
	// reached a stub or ghost.
	assert((type == VestaSource::stub) ||
	       (type == VestaSource::ghost));

	// Optimize out "latest" and other stubs with a "symlink-to" attribute
	if((type != VestaSource::stub) || !is_symlink(cl, index, vs))
	  {
	    // If we have a vs from is_symlink, but it's not the
	    // master, we need to get another one.
	    if((vs != 0) && !master)
	      {
		delete vs;
		vs = 0;
	      }

	    // If we don't have vs yet, find the master copy
	    if(vs == 0)	vs = lookup_child(cl, arc, index, master);

	    // If we failed to find the master copy, continue with the
	    // next directory entry.
	    if(vs == 0) return true;

	    if (vs->type == VestaSource::immutableDirectory) {
	      long val;
	      if(maybe_highest(cl, arc, val))
		{
		  cl->high = val;
		  cl->highText = arc;
		  cl->highvervs = vs;
		  delete_vs = false;
		}
	    } else if (vs->type == VestaSource::appendableDirectory) {
	      if (cl->recurse == r_none)
		{
		  delete vs;
		  return true;
		}
	      maybe_recurse(cl, arc, vs);
	    }
	  }
      }

    if(delete_vs && (vs != 0))
      delete vs;

    return true;
}

void
doit(Text pkg, Recursion recurse, bool mast, bool fullpath,
     bool comment, Text lhost, Text lport)
{
    Text cpkg = ReposUI::canonicalize(pkg, defpkgpar);
    VestaSource* vs_pkg;
    if (mast) {
      vs_pkg = ReposUI::filenameToMasterVS(cpkg, lhost + ":" + lport);
    } else {
      vs_pkg = ReposUI::filenameToVS(cpkg, lhost, lport);
    }
    
    // Sanity checking
    if (vs_pkg->type != VestaSource::appendableDirectory) {
	// No error message; could be a ghost (etc.) that matched a "*"
	return;
    }
    if (recurse == r_none && !vs_pkg->inAttribs("type", "package")) {
	cerr << program_name << ": " << cpkg << " is not a package" << endl;
	errcount++;
	return;
    }
    printlatest(vs_pkg, cpkg + "/", recurse, mast,
		fullpath ? 0 : (cpkg.Length() + 1), comment, lhost, lport);
}

int
main(int argc, char* argv[])
{
    program_name = argv[0];
    try {
	//
	// Read config file
	//
	defpkgpar = VestaConfig::get_Text("UserInterface",
					  "DefaultPackageParent");
	
	Recursion recurse = r_pkg;
	bool mast = false;
	bool fullpath = true;
	bool comment = false;
	Text repos;
	
	// 
	// Parse command line
	//
	opterr = 0;
	for (;;) {
	    char* slash;
	    int c = getopt(argc, argv, "npbcmtvR:");
	    if (c == EOF) break;
	    switch (c) {
	      case 'n':
		recurse = r_none;
		break;
	      case 'p':
		recurse = r_pkg;
		break;
	      case 'b':
		recurse = r_branch;
		break;
	      case 'c':
		recurse = r_checkout;
		break;
	      case 'm':
		mast = true;
	        break;
	      case 't':
		fullpath = false;
		break;
	      case 'v':
		comment = true;
		break;
	      case 'R':
		repos = optarg;
		break;
	      case '?':
	      default:
		Usage();
	    }
	}

	Text lhost(VDirSurrogate::defaultHost());
	Text lport(VDirSurrogate::defaultPort());
	if (repos != "") {
	  int colon = repos.FindCharR(':');
	  if (colon == -1) {
	    lhost = repos;
	    repos = repos + ":" + lport;
	  } else {
	    lhost = repos.Sub(0, colon);
	    lport = repos.Sub(colon+1);
	  }
	}

	if (optind > argc) {
	    Usage();
	    /* not reached */
	} else if (optind == argc) {
	    doit("/vesta", recurse, mast, fullpath, comment, lhost, lport);
	} else /* optind < argc */ {
	    if (argc - optind > 1 && !fullpath) {
		cerr << program_name <<
		  ": only one package allowed with -t flag" << endl;
		exit(errcount+1);
	    }
	    while (optind < argc) {
		doit(argv[optind++], recurse, mast, fullpath, 
		     comment, lhost, lport);
	    }
	}
	
    } catch (VestaConfig::failure f) {
	cerr << program_name << ": " << f.msg << endl;
	exit(errcount+1);
    } catch (SRPC::failure f) {
	cerr << program_name
	  << ": SRPC failure; " << f.msg << " (" << f.r << ")" << endl;
	exit(errcount+1);
    } catch (ReposUI::failure f) {
	cerr << program_name << ": " << f.msg << endl;
	exit(errcount+1);
    }
    return errcount;
}



