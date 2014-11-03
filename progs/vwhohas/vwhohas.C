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
// vwhohas.C
//

// Who has a given Vesta package checked out?
// See documentation in vwhohas.1.mtex

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
    r_none, r_pkg, r_branch
  };

Text program_name, defpkgpar;
int errcount = 0;

void
Usage()
{
    cerr << "Usage: " << program_name
      <<  " [-n | -p | -b] [-m] [-t] [-v] [-R repos] [package]" << endl;
    exit(1);
}

struct verclosure {
    VestaSource* parent;
    Text prefix;
    Recursion recurse;
    bool mast;
    int elide;
    bool comment;
    Text lhost, lport;
};

static bool
vercallback(void* closure, VestaSource::typeTag type, Arc arc,
	    unsigned int index, Bit32 pseudoInode, ShortId filesid,
	    bool master);

static void
printwhohas(VestaSource* vs, Text prefix, Recursion recurse,
	    bool mast, int elide, bool comment, Text lhost, Text lport)
{
    verclosure cl;
    cl.parent = vs;
    cl.prefix = prefix;
    cl.recurse = recurse;
    cl.mast = mast;
    cl.elide = elide;
    cl.comment = comment;
    cl.lhost = lhost;
    cl.lport = lport;
    VestaSource::errorCode err = vs->list(0, vercallback, &cl);
    if (err != VestaSource::ok) {
	cerr << program_name 
	  << ": error listing directory " << cl.prefix << ": " 
	    << ReposUI::errorCodeText(err) << endl;
	errcount++;
    }
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
  if (!vs->inAttribs("type", "checkout") &&
      (cl->recurse >= r_branch ||
       !vs->inAttribs("type", "branch"))) {
    // OK to recurse
    printwhohas(vs, cl->prefix + arc + "/", cl->recurse,
		cl->mast, cl->elide, cl->comment, cl->lhost, cl->lport);
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

static void print_one_reservation(verclosure* cl, Arc arc, VestaSource* vs)
{
  char* owner = vs->getAttrib("checkout-by");
  if (owner) {
    cout << cl->prefix.Sub(cl->elide) << arc << "  " << owner;
    delete [] owner;
    char* dest = vs->getAttrib("checkout-to");
    if (dest) {
      cout << "  " << dest;
      delete [] dest;
    }
    cout << endl;
    if(cl->comment)
      {
	char* message = vs->getAttrib("message");
	if(message)
	  {
	    cout << "\t" << message << endl;
	    delete [] message;
	  }
      }
  }
}

static bool
vercallback(void* closure, VestaSource::typeTag type, Arc arc,
	    unsigned int index, Bit32 pseudoInode, ShortId filesid,
	    bool master)
{
    verclosure* cl = (verclosure*) closure;
    VestaSource* vs = 0;

    if((type == VestaSource::immutableDirectory) ||
       (type == VestaSource::immutableFile))
      {
	// Just continue on immutableDirectory/immutableFile
	return true;
      }
    else if(type == VestaSource::appendableDirectory)
      {
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

	// Optimize out "latest" and other stubs with a "symlink-to"
	// attribute
	if((type == VestaSource::stub) && is_symlink(cl, index, vs))
	  {
	    delete vs;
	    return true;
	  }

	// If we have a vs from is_symlink, but it's not the
	// master, we need to get another one.
	if((vs != 0) && !master)
	  {
	    delete vs;
	    vs = 0;
	  }

	// If we don't have vs yet, find the master copy
	if(vs == 0) vs = lookup_child(cl, arc, index, master);

	// If we failed to find the master copy, continue with the
	// next directory entry.
	if(vs == 0) return true;

	if(vs != 0)
	  {
	    if (vs->type == VestaSource::stub)
	      {
		print_one_reservation(cl, arc, vs);
	      }
	    else if (vs->type == VestaSource::appendableDirectory)
	      {
		if (cl->recurse > r_none)
		  maybe_recurse(cl, arc, vs);
	      }
	    delete vs;
	  }
      }
    else if(type == VestaSource::stub)
      {
	// We don't need to go to the master, just print the
	// reservation based on the local stub.
	vs = lookup_child(cl, arc, index, master);
	if(vs == 0) return true;

	print_one_reservation(cl, arc, vs);
	delete vs;
      }

    return true;
}

void
doit(Text pkg, Recursion recurse, bool mast, bool fullpath, bool comment,
     Text lhost, Text lport)
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
    printwhohas(vs_pkg, cpkg + "/", recurse, mast, 
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
	    int c = getopt(argc, argv, "npbmtvR:");
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
	}
	if (optind == argc) {
	    doit("/vesta", recurse, mast, fullpath, comment, lhost, lport);
	} else {
	    if (!fullpath) {
		cerr << program_name <<
		  ": only one package allowed with -t flag" << endl;
		exit(errcount + 1);
	    }
	    while (optind < argc) {
		doit(argv[optind++], recurse, mast, fullpath, 
		     comment, lhost, lport);
	    }
	}
	
    } catch (VestaConfig::failure f) {
	cerr << program_name << ": " << f.msg << endl;
	exit(errcount + 1);
    } catch (SRPC::failure f) {
	cerr << program_name
	  << ": SRPC failure; " << f.msg << " (" << f.r << ")" << endl;
	exit(errcount + 1);
    } catch (ReposUI::failure f) {
	cerr << program_name << ": " << f.msg << endl;
	exit(errcount + 1);
    }
    return errcount;
}



