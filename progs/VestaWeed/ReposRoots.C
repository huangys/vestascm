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

// Created on Wed Mar 12 20:42:15 PST 1997 by heydon

#include <sys/types.h>
#if defined(__linux__) && !defined(__GNUC__)
// For some reason, the Linux sys/types.h only defines int64_t under
// the GNU compilers.  We need it for subsequent system includes.
typedef long int int64_t;
#endif
#include <string.h>
#include <dirent.h>
#include <Basics.H>
#include <Table.H>
#include <SourceOrDerived.H>
#include <VestaSource.H>
#include <ReadConfig.H>
#include <Debug.H>
#include "PkgBuild.H"
#include "Pathname.H"
#include "CommonErrors.H"
#include "ReposRoots.H"

using std::cout;
using std::endl;

// character constants
static const char Slash = '/';         // pathname separator
static const char Null = '\0';         // end-of-string character

// forward reference
static void ReposRoots_SearchRepos(VestaSource *reposDir, Pathname::T pathT,
  /*INOUT*/ PkgTbl *pkgTbl) throw (SRPC::failure, ReposError);

static bool ReposRoots_ValidFilename(Arc arc) throw ()
/* Return "true" iff "arc" names a file ending with a ".ves" extension. */
{
    static const char *ext = ".ves";       // magic extension
    static const int extLen = strlen(ext); // and its length

    int len = strlen(arc);
    if (len <= extLen) return false;
    return (strncmp(arc + (len - extLen), ext, extLen) == 0);
}

struct CallbackArgs {
    VestaSource *parent;
    Pathname::T parentPath;
    PkgTbl *pkgTbl;
};

static bool ReposRoots_DirCallback(void *closure, VestaSource::typeTag type,
  Arc arc, unsigned int index, /*UNUSED*/ Bit32 pseudoInode, 
  ShortId filesid, /*UNUSED*/ bool master)
  throw (SRPC::failure, ReposError)
{
    CallbackArgs *args = (CallbackArgs *)closure;
    VestaSource::errorCode errCode;

    // Handle an immutableFile first before performing a lookup
    if(type == VestaSource::immutableFile)
      {
	// skip this file if the parent dir type or the file name are wrong
	if (args->parent->type == VestaSource::immutableDirectory
	     && ReposRoots_ValidFilename(arc)) {
	    // form the PkgBuild and Pathname::T
	    PkgBuild pkgBuild(args->parent->fptag, filesid);
	    Pathname::T newPathT = Pathname::New(args->parentPath, arc);

	    // add to the table
	    (void) args->pkgTbl->Put(pkgBuild, newPathT);
	}

	return true;
      }

    // Look up this object.
    VestaSource *child = 0;
    errCode = args->parent->lookupIndex(index, /*OUT*/ child);
    if (errCode == VestaSource::longIdOverflow) {
      // We couldn't process this item, but maybe we can still handle
      // others in this directory
      return true;
    } else if (errCode == VestaSource::notFound) {
      // Maybe an object was replaced while we were listing
      // (e.g. ghost/stub -> real object or real object -> ghost)?
      // Try a lookup by name.
      errCode = args->parent->lookup(arc, /*OUT*/ child);
      if (errCode == VestaSource::longIdOverflow) {
	return true;
      } else if (errCode != VestaSource::ok) {
	Text path = Pathname::Get(args->parentPath);
	throw ReposError(errCode, "VestaSource::lookup", path);
      }
    } else if (errCode != VestaSource::invalidArgs) {
      // Maybe an enclosing directory was ghosted?  Only continue
      // listing if this directory is still valid.
      return args->parent->longid.valid();
    } else if (errCode != VestaSource::ok) {
      Text path = Pathname::Get(args->parentPath);
      throw ReposError(errCode, "VestaSource::lookupIndex", path);
    }

    // handle directory and file cases differently
    assert(child != 0);
    switch (child->type) {
      case VestaSource::immutableFile:
	// skip this file if the parent dir type or the file name are wrong
	if (args->parent->type == VestaSource::immutableDirectory
	     && ReposRoots_ValidFilename(arc)) {
	    // form the PkgBuild and Pathname::T
	    PkgBuild pkgBuild(args->parent->fptag, filesid);
	    Pathname::T newPathT = Pathname::New(args->parentPath, arc);

	    // add to the table
	    (void) args->pkgTbl->Put(pkgBuild, newPathT);
	}
	break;
      case VestaSource::appendableDirectory:
      case VestaSource::immutableDirectory:
	// form new Pathname::T
	Pathname::T newPathT = Pathname::New(args->parentPath, arc);
	
	// make recursive call
	ReposRoots_SearchRepos(child, newPathT, /*INOUT*/ args->pkgTbl);
	break;
    }
    return true;
} // ReposRoots_DirCallback

static void ReposRoots_SearchRepos(VestaSource *reposDir, Pathname::T pathT,
  /*INOUT*/ PkgTbl *pkgTbl) throw (SRPC::failure, ReposError)
/* Recursively search the subtree rooted at "reposDir", which is required to
   be an appendable or immutable directory. "pathT" is the "Pathname::T"
   corresponding to "path". For each file in an immutable directory with
   extension ".ves", add a mapping to "pkgTbl" for the file's complete
   pathname. Also add a mapping to "pkgTbl" for each immutable directory; the
   latter uses "NullShortId" for the shortid of the corresponding model. */
{
    // add a mapping to "pkgTbl" if "reposDir" is immutable
    if (reposDir->type == VestaSource::immutableDirectory) {
	PkgBuild pkgBuild(reposDir->fptag, NullShortId);
	(void) pkgTbl->Put(pkgBuild, pathT);
    } else {
	assert(reposDir->type == VestaSource::appendableDirectory);
    }

    // fill in closure args
    CallbackArgs args;
    args.parent = reposDir;
    args.parentPath = pathT;
    args.pkgTbl = pkgTbl;

    VestaSource::errorCode errCode;
    errCode = reposDir->list(/*firstIndex=*/ 0,
      ReposRoots_DirCallback, (void *)(&args));
    if ((errCode != VestaSource::ok) &&
	// If this directory or an enclosing one are ghosted while
	// we're scanning, we could get notFound
	(errCode != VestaSource::notFound))
      {
	Text path = Pathname::Get(pathT);
	throw ReposError(errCode, "VestaSource::list", path);
      }
} // ReposRoots_SearchRepos

PkgTbl *ReposRoots::Scan(bool debug) throw (SRPC::failure, ReposError)
{
    // pre debugging
    if (debug) {
	cout << Debug::Timestamp() << "Starting repository scan" << endl;
	cout << endl;
    }

    // allocate result
    PkgTbl *res = NEW_CONSTR(PkgTbl, (/*sizeHint=*/ 2000, /*useGC=*/ true));

    // get repository appendable root
    VestaSource *reposRoot = VestaSource::repositoryRoot();
    assert(reposRoot->type == VestaSource::appendableDirectory);

    // read appendable root name from config file
    Text rootName(ReadConfig::TextVal("UserInterface", "AppendableRootName"));
    int rootNameLen = rootName.Length();
    Pathname::T rootPathT = Pathname::New(-1, rootName.chars());

    // recursively search subtree rooted at "path"
    ReposRoots_SearchRepos(reposRoot, rootPathT, /*INOUT*/ res);

    // post debugging
    if (debug) {
	cout << Debug::Timestamp() << "Finished repository scan" << endl;
	cout << "  total models found = " << res->Size() << endl;
	cout << endl;
    }
    return res;
} // ReposRoots::Scan
