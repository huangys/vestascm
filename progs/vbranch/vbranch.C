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
// vbranch.C
//

// Create a branch on a package in the Vesta repository.
// See documentation in vbranch.1.mtex

#include <Basics.H>
#include <Text.H>
#include <VestaConfig.H>
#include <VestaSource.H>
#include <VDirSurrogate.H>
#include <VestaSourceAtomic.H>
#include <Replicator.H>
#include <signal.h>
#include "ReposUI.H"

#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif

using std::cout;
using std::cerr;
using std::endl;

Text program_name;

void usage()
{
  cerr << "Usage: " << program_name << endl <<
    "        [-q] [-Q] [-v] [-a | -A] [-f] [-F]" << endl <<
    "        [-o old-version | -O]" << endl <<
    "        [-m message | -M]" << endl <<
    "        [-h hints]" << endl <<
    "        [-R repos]" << endl <<
    "        package/branch" << endl << endl;
  exit(1);
}

Text program_name_tail() throw()
{
  int last_slash = program_name.FindCharR('/');
  if(last_slash == -1)
    return program_name;
  return program_name.Sub(last_slash+1);
}

int
main(int argc, char* argv[])
{
  program_name = argv[0];
  try {
    //
    // Read config file
    //
    Text defpkgpar, timefmt, repos, defhints, foreignparentdir;
    bool default_repos = true;

    // If the parent directory is mastered remotely, should we have
    // the local repository acquire mastership of the created package?
    // Defaults to on.
    bool acquire_mastership = false;

    defpkgpar = VestaConfig::get_Text("UserInterface",
				      "DefaultPackageParent");
    timefmt = VestaConfig::get_Text("UserInterface", "TimeFormat");
    (void) VestaConfig::get("UserInterface", "DefaultHints", defhints);

    bool prompt_msg = true, omit_msg = false;
    if(VestaConfig::is_set("UserInterface", "vbranch_message")) {
      if(!VestaConfig::get_bool("UserInterface", "vbranch_message")) {
	prompt_msg = false;
	omit_msg = true;
      }
    }

    // get foreign parent dir
    if(VestaConfig::get("UserInterface", "ForeignParent", foreignparentdir))
      {
	foreignparentdir = ReposUI::canonicalize(foreignparentdir);
	// add last "/" if it is not present
	if(foreignparentdir[foreignparentdir.Length()-1] != '/')
	  foreignparentdir += "/";
      }

    time_t now = time(NULL);
    char timebuf[256];
    strftime(timebuf, sizeof(timebuf), timefmt.cchars(), localtime(&now));
    Text cuser(AccessControl::self()->user());
    Text user(cuser.Sub(0, cuser.FindChar('@')));
    Text lhost(VDirSurrogate::defaultHost());
    Text lport(VDirSurrogate::defaultPort());
    const int vlen = 7; // ==strlen("/vesta/")
    const Replicator::Flags rflags = (Replicator::Flags) 
      (Replicator::attrNew | Replicator::attrOld | Replicator::attrAccess |
       Replicator::revive | Replicator::inclStubs | Replicator::latest);

    if(VestaConfig::is_set("UserInterface", "vbranch_acquires"))
      {
	acquire_mastership = VestaConfig::get_bool("UserInterface",
						   "vbranch_acquires");
      }

    TextTextTbl trigger_vars;
    bool verbose_triggers = false;
    trigger_vars.Put("VESTA_TOOL", program_name_tail());

    //
    // Parse command line
    //
    bool quiet = false;
    bool query = false;
    bool force = false;
    bool omit_old = false, default_old = true;
    bool foreign_branch = false;

    Text pkg, oldver, hints, message;
   
    opterr = 0;
    for (;;) {
      int c = getopt(argc, argv, "qQaAfFo:Om:Mh:R:v");
      if (c == EOF) break;
      switch (c) {
      case 'q':
	quiet = true;
	break;
      case 'Q':
	query = true;
	break;
      case 'a':
	acquire_mastership = true;
	break;
      case 'A':
	acquire_mastership = false;
	break;
      case 'f':
	force = true;
	break;
      case 'F':
	foreign_branch = true;
	break;
      case 'o':
	oldver = optarg;
	omit_old = false;
	default_old = false;
	break;
      case 'O':
	omit_old = true;
	default_old = false;
	break;
      case 'm':
	message = optarg;
	omit_msg = false;
	if(message == "-") {
	  message = "";
	  prompt_msg = true;
	}
 	else 
	  prompt_msg = false;
	break;
      case 'M':
	omit_msg = true;
	prompt_msg = false;
	break;
      case 'h':
	hints = optarg;
	break;
      case 'R':
	repos = optarg;
	default_repos = false;
	break;
      case 'v':
	verbose_triggers = true;
	break;
      case '?':
      default:
	usage();
      }
    }

    trigger_vars.Put("VESTA_QUIET", quiet?"1":"0");
    trigger_vars.Put("VESTA_FORCE", force?"1":"0");

    if (argc != optind + 1) {
      usage();
    }
    pkg = Text(argv[optind]);

    // check that [UserInterface]ForeignParent configuration is set 
    if(foreign_branch && foreignparentdir.Empty()) {
      cerr << program_name
	   << (": [UserInterface]ForeignParent not set; "
	       "-F flag requires config setting") << endl;
      exit(1);
    }

    trigger_vars.Put("VESTA_USE_FOREIGN_TREE", foreign_branch?"1":"0");

    //
    // Use a nondefault repository if specified, and automatically
    // add it to the hints if not already present.
    //
    if (!default_repos) {
      int colon = repos.FindCharR(':');
      if (colon == -1) {
	lhost = repos;
	repos = repos + ":" + lport;
      } else {
	lhost = repos.Sub(0, colon);
	lport = repos.Sub(colon+1);
      }
      hints = hints + " " + repos;
    }

    trigger_vars.Put("VESTA_REPOS", lhost+":"+lport);

    hints = hints + " " + defhints;

    trigger_vars.Put("VESTA_MASTER_HINTS", hints);

    // get change history message
    if (!query && prompt_msg) {  
      message = ReposUI::getMessage("branch description");
    }

    trigger_vars.Put("VESTA_MESSAGE", message);
    trigger_vars.Put("VESTA_OMIT_MESSAGE", omit_msg?"1":"0");

    //
    // Qualify and canonicalize names
    //
    Text cpkg, cparent, newpart, coldver, ccheckout, clatest, cslatest, cver0;
    cpkg = ReposUI::canonicalize(pkg, defpkgpar);
    // in a case of making a branch under foreign branch the -F flag 
    // should not be set since it will cause creation of multiple 
    // foreign dirs  
    if(foreign_branch && 
       (cpkg.Sub(0, foreignparentdir.Length()) == foreignparentdir)) {
      foreign_branch = false;
    }
    ReposUI::split(cpkg, cparent, newpart, "branch name");

    // define old version
    if (!omit_old) {
      if (default_old) {
	int endpos = 0;
	while (isdigit(newpart[endpos])) {
	  endpos++;
	}
	if (endpos == 0) {
	  cerr << program_name <<
	    ": can't determine branch base: " <<
	    "branch name does not begin with digits" << endl;
	  exit(2);
	}
	coldver = cpkg.Sub(0, cparent.Length() + 1 + endpos);
      } else { 
	coldver = ReposUI::canonicalize(oldver, cparent);
      }
    }
    
    trigger_vars.Put("VESTA_OLD_VERSION", coldver);
    trigger_vars.Put("VESTA_OMIT_OLD_VERSION", omit_old?"1":"0");

    if(foreign_branch) {
      // Foreign branches should be placed under foreignparentdir that is 
      // defined in vesta.cfg (usually /vesta/<current site>/foreign/).
      // The following part of the foreign branch parent path is cparent 
      // without root, so foreign branch parent will conatain 
      // /vesta/<current site>/foreign/<foreign site>/...
      cparent = foreignparentdir + 
	ReposUI::stripSpecificRoot(cparent, ReposUI::VESTA, true);
      cpkg = cparent + "/" + newpart;
    } 

    trigger_vars.Put("VESTA_PACKAGE", cpkg);
    trigger_vars.Put("VESTA_BRANCH", cpkg);

    ccheckout = cpkg + "/checkout";
    clatest = cpkg + "/latest";
    cslatest = cpkg + "/checkout/latest";
    cver0 = cpkg + "/0";
    
    //
    // Look up master of parent and sanity check 
    //
    VestaSource* vs_mparent = NULL;
    try {
      vs_mparent = ReposUI::filenameToMasterVS(cparent, hints);
    }
    catch(ReposUI::failure f) {
      if(foreign_branch) {
	if(!query) {
	  Text pathname = cparent.Sub(foreignparentdir.Length());
	  vs_mparent = 
	    ReposUI::lookupCreatePath(foreignparentdir, pathname, hints);
	  if(!vs_mparent->inAttribs("type", "package")) {
	    vs_mparent->addAttrib("type", "package");
	  }
	}
      }
      else {
	throw(f);
      }
    }

    Text mhost, mport;
    if(vs_mparent) {
      // master of branch parent should be found in previous step
      // unless -Q -F flags are set and branch parent does not exist 
      if (!force && !vs_mparent->inAttribs("type", "package")) {
	cerr << program_name << ": " << cparent << " at "
	     << vs_mparent->host() << ":" << vs_mparent->port()
	     << " is not a package or branch" << endl;
	exit(2);
      }
      mhost = vs_mparent->host();
      mport = vs_mparent->port(); 
    }
    else {
      // find master of foreignparentdir to know repository, 
      // where branch parent should have been created if it was not 
      // for -Q flag.
      VestaSource* vs_mforeignparent = 
	ReposUI::filenameToMasterVS(foreignparentdir, hints);
      mhost = vs_mforeignparent->host();
      mport = vs_mforeignparent->port();
      delete vs_mforeignparent;
    }
     
    bool remote = (mhost != lhost || mport != lport);

    trigger_vars.Put("VESTA_MASTER_REPOS", mhost+":"+mport);

    trigger_vars.Put("VESTA_MASTER_REMOTE", remote ? "1" : "0");

    trigger_vars.Put("VESTA_ACQUIRE_MASTERSHIP",
		     (remote && acquire_mastership) ? "1" : "0");

    Text hint_dir, master_hint;
    if(remote && vs_mparent) {
      // find directory whose master-repository attribute needs to be updated
      hint_dir = ReposUI::getMasterHintDir(vs_mparent, cparent);
      master_hint = vs_mparent->getMasterHint();
    }

    //
    // Look up oldver, determine whether we need to replicate it to
    // the master repository where we'll create the branch.
    //
    VestaSource* vs_moldver = NULL;
    VestaSource* vs_roldver = NULL;
    if (!omit_old) {
      vs_roldver = ReposUI::filenameToRealVS(coldver, hints);
      if (vs_roldver->type == VestaSource::stub) {
	cerr << program_name << ": " << coldver
	     << " is not checked in yet" << endl;
	exit(2);
      } else if (vs_roldver->type == VestaSource::ghost) {
	cerr << program_name << ": " << coldver
	     << " has been deleted" << endl;
	exit(2);
      }

      // find old version in the master repository
      if(vs_roldver->host() == mhost && vs_roldver->port() == mport) {
	vs_moldver = vs_roldver;
      }
      else {
	try {
	  vs_moldver = ReposUI::filenameToVS(coldver, mhost, mport);
	  // If the target has a different type (e.g. stub or
	  // ghost), we won't use it.
	  if(vs_moldver->type != vs_roldver->type) {
	    delete vs_moldver;
	    vs_moldver = NULL;
	  }
	}
	catch(...){
	  // There is no old version in master repository
	}
	if(!vs_moldver && !query){
	  // We will nned to get a replica to the master repository
	  if (!quiet) {
	    cout << "Replicating " << coldver << " from "
		 << vs_roldver->host() << ":" << vs_roldver->port()
		 << " to " << mhost << ":" << mport << endl;
	  }
	  // We don't do the replication until later, after running
	  // pre-triggers.
	}
      }
    } // if(!omit_old)

    trigger_vars.Put("VESTA_OLD_VERSION_REPOS",
		     (vs_roldver != 0)
		     ?(vs_roldver->host()+":"+vs_roldver->port())
		     :"");
    trigger_vars.Put("VESTA_OLD_VERSION_REMOTE",
		     ((vs_moldver == 0) && (vs_roldver != 0))?"1":"0");

    if (!quiet) {
      cout << "Creating branch " << cpkg;
      if (remote) {
	cout << " at " << mhost << ":" << mport;
      }
      cout << endl;
    }

    // Make environment variables available for the triggers
    ReposUI::setTriggerVars(trigger_vars,
			    verbose_triggers);

    // Run the "pre" triggers.
    ReposUI::runTriggers("vbranch pre trigger",
			 verbose_triggers, !query);

    if (!query)
      {
	if(!omit_old && (vs_moldver == 0))
	  {
	    // Now replicate the old version to the repository where
	    // we're creating the branch.  We wait until after the
	    // pre-triggers to do this.
	    assert(vs_roldver != 0);
	    Replicator repl(vs_roldver->host(), vs_roldver->port(),
			    mhost, mport);
	    Replicator::DirectiveSeq direcs;
	    Replicator::Directive d('+', coldver.Sub(vlen));
	    direcs.addhi(d);
	    repl.replicate(&direcs, rflags);
	    vs_moldver = ReposUI::filenameToVS(coldver, mhost, mport);
	  }

	//
	// Construct atomic action to create the branch
	//
	VestaSourceAtomic vsa(mhost, mport);

	// Declare parent directory and make sure it is a master
	// appendable directory
	VestaSourceAtomic::VSIndex vsi_parent =
	  vsa.decl(cparent, vs_mparent);
	vsa.testMaster(cparent, vsi_parent, true,
		       VestaSource::notMaster);
	vsa.typeCheck(cparent, vsi_parent, VestaSourceAtomic::
		      typebit(VestaSource::appendableDirectory),
		      VestaSource::inappropriateOp);

	// We also insert the old version  in the same action.
	VestaSourceAtomic::VSIndex vsi_oldver = -1;
	if (!omit_old) {
	  assert(vs_moldver != 0);
	  vsi_oldver = vsa.decl(coldver, vs_moldver);
	  // Check that vs_moldver is an immutable directory, halt if not
	  vsa.typeCheck(coldver, vsi_oldver, VestaSourceAtomic::
			typebit(VestaSource::immutableDirectory),
			VestaSource::inappropriateOp);
	}

	// Make package directory.  This is the last step that should
	// be able to halt, so we just try it; no need to test first.
	VestaSourceAtomic::VSIndex vsi_pkg =
	  vsa.insertAppendableDirectory(cpkg, vsi_parent,
					newpart, true,
					VestaSource::dontReplace);

	// Insert version 0 either as an empty directory or as a copy
	// of the old version
	VestaSourceAtomic::VSIndex vsi_ver0 =
	  vsa.insertImmutableDirectory(cver0, vsi_pkg, "0",
				       omit_old ? -1 : vsi_oldver, true,
				       VestaSource::dontReplace);

	// Insert "checkout" subdirectory.
	VestaSourceAtomic::VSIndex vsi_checkout =
	  vsa.insertAppendableDirectory(ccheckout, vsi_pkg, "checkout", true,
					VestaSource::dontReplace);

	// Insert "latest" links
	VestaSourceAtomic::VSIndex vsi_latest =
	  vsa.insertStub(clatest, vsi_pkg, "latest", true,
			 VestaSource::dontReplace);
	VestaSourceAtomic::VSIndex vsi_latest2 =
	  vsa.insertStub(cslatest, vsi_checkout, "latest", true,
			 VestaSource::dontReplace);

	// Set attributes
	vsa.setAttrib(cpkg, vsi_pkg, "created-by", cuser);
	vsa.setAttrib(cpkg, vsi_pkg, "creation-time", timebuf);
	// (We use add for both of the following because they have the
	// same timestamp, and we don't want the tiebreak algorithm to
	// sort an set after an add.  Since vsi_pkg is new, we know
	// the attributes were initially empty.)
	vsa.addAttrib(cpkg, vsi_pkg, "type", "package");
	vsa.addAttrib(cpkg, vsi_pkg, "type", "branch");
	if (!omit_old) {
	  vsa.setAttrib(cpkg, vsi_pkg, "old-version", coldver);
	  vsa.setAttrib(cver0, vsi_ver0, "old-version", coldver);
	}
	// add message attribute
	if(!omit_msg && !message.Empty()){
	  vsa.setAttrib(cpkg, vsi_pkg, "message", message);
	}
	vsa.setAttrib(ccheckout, vsi_checkout, "type", "checkout");
	vsa.setAttrib(clatest, vsi_latest, "symlink-to", "$LAST");
	vsa.setAttrib(cslatest, vsi_latest2, "symlink-to", "$LAST");
	// add next-branches attribute to the old version (master)
	if(!omit_old) {
	  vsa.setTarget("", VestaSource::ok, VestaSource::noPermission);
	  vsa.addAttrib(coldver + ": adding to next-branches", vsi_oldver,
			"next-branches", cpkg);
	  vsa.setTarget("");
	}

	//
	// Disable ^C signal to make it less likely that we will be
	// killed after having done the remote action but
	// before having finished the whole job.
	//
	signal(SIGINT, SIG_IGN);

	// Commit
	if (!vsa.run()) {
	  VestaSource::errorCode err;
	  if (vsa.lasterr == VestaSource::ok) {
	    err = vsa.okreplace;
	  } else {
	    err = vsa.lasterr;
	  }
	  cerr << program_name << ": " << vsa.name(vsa.ndone) << ": "
	       << ReposUI::errorCodeText(err) << endl;
	  exit(2);
	}

	// check if the next-branches attribute was added to the old
	// version (master)
	if(!omit_old && !quiet) {
	  if(!vs_moldver->inAttribs("next-branches", cpkg.cchars())) {
	    cerr << program_name << ": warning: "   
	      "next-branches attribute couldn't be added to version: "
		 << coldver << " at "<< mhost << ":" << mport << endl;
	  }
	}

	// Done if master was local
	if (remote)
	  {
	    //
	    // Replicate the new package here
	    //
	    Replicator repl(mhost, mport, lhost, lport);
	    Replicator::DirectiveSeq direcs;
	    Replicator::Directive d('+', cpkg.Sub(vlen));
	    direcs.addhi(d);
	    repl.replicate(&direcs, rflags);

	    //
	    // Take mastership of the package and its checkout directory if
	    // requested.  (We don't bother getting mastership of the "latest"
	    // links.)
	    //
	    if(acquire_mastership)
	      {
		VestaSource::errorCode err;
		err = VDirSurrogate::acquireMastership(cpkg.Sub(vlen).cchars(),
						       lhost, lport, mhost, mport);
		if (err != VestaSource::ok) {
		  cerr << program_name << ": error acquiring mastership of "
		       << cpkg << ": " << ReposUI::errorCodeText(err) << endl;
		  exit(2);
		}
		err = VDirSurrogate::acquireMastership(ccheckout.Sub(vlen).cchars(),
						       lhost, lport, mhost, mport);
		if (err != VestaSource::ok) {
		  cerr << program_name << ": error acquiring mastership of "
		       << ccheckout << ": " << ReposUI::errorCodeText(err) << endl;
		  exit(2);
		}
	      } // if(acquire_mastership)

	    // add next-branches attribute to the old version in local repository
	    if(!omit_old && !query) {
	      VestaSource* vs_oldver = NULL;
	      if(vs_roldver->host() == lhost && vs_roldver->port() == lport)
		{
		  vs_oldver = vs_roldver;
		}
	      else
		{
		  try
		    {
		      vs_oldver = ReposUI::filenameToVS(coldver, lhost, lport);
		    }
		  catch(ReposUI::failure)
		    {
		      // old version not present in local repository: ignore
		      vs_oldver = NULL;
		    }
		}
      
	      // if there is old version in local repository add the attribute
	      if(vs_oldver)
		{
		  VestaSource::errorCode err =
		    vs_oldver->addAttrib("next-branches", cpkg.cchars());
		  if(!quiet && (err != VestaSource::ok))
		    {
		      cerr << program_name << ": warning: "   
			"next-branches attribute couldn't be added to version: "
			   << coldver << endl;
		    }
		}
	    } // if(!omit_old && !query)
    
	    // Update master-repository hint
	    if(!master_hint.Empty())
	      {
		VestaSource* vs_hint = NULL;
		vs_hint = ReposUI::filenameToVS(hint_dir, lhost, lport);
		if(!vs_hint->inAttribs("master-repository", master_hint.cchars())) {
		  VestaSource::errorCode err = 
		    vs_hint->setAttrib("master-repository", master_hint.cchars());
		  if(!quiet && (err != VestaSource::ok)) {
		    cerr << program_name 
			 << ": warning: incorrect master-repository attribute of " 
			 << hint_dir << " couldn't be updated (should be "
			 << master_hint << ")" << endl;
		  }
		}
	      }
	  } // if (remote)
      } // if (!query)

    // Run the "post" triggers
    ReposUI::runTriggers("vbranch post trigger",
			 verbose_triggers, !query);

    if (query) {
      if(!quiet)
	cerr << program_name << ": nothing done (-Q flag given)" << endl;
      exit(0);
    }


  } catch (VestaConfig::failure f) {
    cerr << program_name << ": " << f.msg << endl;
    exit(2);
  } catch (SRPC::failure f) {
    cerr << program_name
	 << ": SRPC failure; " << f.msg << " (" << f.r << ")" << endl;
    exit(2);
  } catch (ReposUI::failure f) {
    cerr << program_name << ": " << f.msg << endl;
    exit(2);
  } catch(FS::Failure f) {
    cerr << program_name << ": " << f << endl;
    exit(2);
  }

  return 0;
}
