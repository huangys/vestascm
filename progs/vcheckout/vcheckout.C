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
// vcheckout.C
//

// Check out a package from the Vesta repository.
// See documentation in vcheckout.1.mtex

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

void
usage()
{
  cerr << "Usage: " << program_name << endl <<
    "        [-q] [-Q] [-v] [-f] [-F]" << endl <<
    "        [-o old-version | -O]" << endl <<
    "        [-n new-version | -N]" << endl <<
    "        [-s session-dir | -S]" << endl <<
    "        [-w work-dir | -W]" << endl <<
    "        [-m message | -M]" << endl <<
    "        [-h hints]" << endl <<
    "        [-R repos]" << endl <<
    "        package" << endl << endl;
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
    Text defpkgpar, defworkpar, timefmt, defhints, foreignsessdirpar;
    defpkgpar = VestaConfig::get_Text("UserInterface",
				      "DefaultPackageParent");
    defworkpar = VestaConfig::get_Text("UserInterface",
				       "DefaultWorkParent");
    timefmt = VestaConfig::get_Text("UserInterface", "TimeFormat");
    (void) VestaConfig::get("UserInterface", "DefaultHints", defhints);

    bool prompt_msg = true, omit_msg = false;
    if(VestaConfig::is_set("UserInterface", "vcheckout_message")) {
      if(!VestaConfig::get_bool("UserInterface", "vcheckout_message")) {
	prompt_msg = false;
	omit_msg = true;
      }
    }

    // get foreign session dir parent
    if(VestaConfig::get("UserInterface", "ForeignParent", foreignsessdirpar))
      {
	foreignsessdirpar = ReposUI::canonicalize(foreignsessdirpar);
	// add last "/" if it is not present
	if(foreignsessdirpar[foreignsessdirpar.Length()-1] != '/')
	  foreignsessdirpar += "/";
      }

    time_t now = time(NULL);
    char timebuf[256];
    strftime(timebuf, sizeof(timebuf), timefmt.cchars(), localtime(&now));
    Text cuser(AccessControl::self()->user());
    int atpos = cuser.FindChar('@');
    Text user(cuser.Sub(0, atpos));
    Text uuser(user + "_" + cuser.Sub(atpos+1)); // cuser with @ replaced by _
    char mode[8];
    mode_t oumask;
    umask(oumask = umask(0));
    sprintf(mode, "%03o", 0777 & ~oumask);
    Text lhost(VDirSurrogate::defaultHost());
    Text lport(VDirSurrogate::defaultPort());
    const int vlen = 7; // ==strlen("/vesta/")
    const Replicator::Flags rflags = (Replicator::Flags) 
      (Replicator::attrNew | Replicator::attrOld | Replicator::attrAccess |
       Replicator::revive | Replicator::inclStubs | Replicator::latest);
	
    TextTextTbl trigger_vars;
    bool verbose_triggers = false;
    trigger_vars.Put("VESTA_TOOL", program_name_tail());

    // 
    // Parse command line
    //
    Text oldver, newver, sessdir, workdir, pkg, hints, repos, message;
    bool default_old = true, default_new = true, default_sess = true;
    bool uniquify_nondefault_sess = false;
    bool default_work = true, default_repos = true;
    bool omit_old = false, omit_new = false, omit_sess = false;
    bool omit_work = false;
    bool quiet = false, query = false, force = false;
    bool foreign_sess_dir = false, foreign_not_default = false;

    opterr = 0;
    for (;;) {
      char* slash;
      int c = getopt(argc, argv, "qQfo:On:NFs:Suw:Wm:Mh:R:v");
      if (c == EOF) break;
      switch (c) {
      case 'q':
	quiet = true;
	break;
      case 'Q':
	query = true;
	break;
      case 'f':
	force = true;
	break;
      case 'o':
	oldver = optarg;
	default_old = false;
	break;
      case 'O':
	omit_old = true;
	oldver = "0";
	default_old = false;
	break;
      case 'n':
	newver = optarg;
	default_new = false;
	omit_new = false;
	foreign_sess_dir = false;
	break;
      case 'N':
	omit_new = true;
	newver = "1";
	default_new = false;
	foreign_sess_dir = false;
	break;
      case 'F':
	foreign_sess_dir = true;
	omit_new = true;
	newver = "1";
	default_new = false;
	default_sess = false;
	break;
      case 's':
	sessdir = optarg;
	default_sess = false;
	break;
      case 'S':
	omit_sess = true;
	break;
      case 'u':
	uniquify_nondefault_sess = true;
	break;
      case 'w':
	workdir = optarg;
	default_work = false;
	break;
      case 'W':
	omit_work = true;
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
	/* not reached */
      }
    }

    trigger_vars.Put("VESTA_QUIET", quiet?"1":"0");
    trigger_vars.Put("VESTA_FORCE", force?"1":"0");

    switch (argc - optind) {
    case 1:
      pkg = argv[optind];
      break;
    case 0:
      pkg = ".";
      break;
    default:
      usage();
      /* not reached */
    }

    if(foreign_sess_dir && foreignsessdirpar.Empty())
      {
	cerr << program_name
	     << (": [UserInterface]ForeignParent not set; "
		 "-F flag requires config setting") << endl;
	exit(1);
      }

    trigger_vars.Put("VESTA_USE_FOREIGN_TREE", foreign_sess_dir?"1":"0");

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

    // add to hints default repositories from vesta.cfg 
    hints = hints + " " + defhints;
    
    trigger_vars.Put("VESTA_MASTER_HINTS", hints);

    // get change history message
    if (!query && prompt_msg) {  
      message = ReposUI::getMessage("checkout description");
    }

    trigger_vars.Put("VESTA_MESSAGE", message);
    trigger_vars.Put("VESTA_OMIT_MESSAGE", omit_msg?"1":"0");

    //
    // Canonicalize pkg
    //
    Text cpkg = ReposUI::canonicalize(pkg, defpkgpar);

    trigger_vars.Put("VESTA_PACKAGE", cpkg);

    // in a case of making a checkout -F of a foreign branch the -F flag 
    // should not be set since it will cause creation of multiple 
    // foreign dirs  
    if((foreign_sess_dir) && 
       (cpkg.Sub(0, foreignsessdirpar.Length()) == foreignsessdirpar)) {
      foreign_sess_dir = false;
      if(sessdir.Empty())
	default_sess = true;
    }

    // set special flag for foreign checkout to a not default derectory 
    if(foreign_sess_dir && !sessdir.Empty()) {
      foreign_not_default = true;
      foreign_sess_dir = false;
    }

    //
    // Compute defaults for oldver and/or newver
    //
    if (default_old || default_new) {
      // Search for the highest version number in use (counting
      // ghosts, but not counting stubs)
      long high;
      if(foreign_sess_dir || foreign_not_default) {
	VestaSource* vs_pkg = ReposUI::filenameToVS(cpkg, lhost, lport);
	high = ReposUI::highver(vs_pkg);
      }
      else {
	VestaSource* vs_mpkg = ReposUI::filenameToMasterVS(cpkg, hints);
	high = ReposUI::highver(vs_mpkg, hints, cpkg);
      }
      if (default_old) {
	if (high == -1) {
	  // No existing versions, pretend old was version 0
	  omit_old = true;
	  oldver = "0";
	} else {
	  char valbuf[64];
	  sprintf(valbuf, "%ld", high);
	  oldver = valbuf;
	}
      }
      if (default_new) {
	// Set newver to next version after highest in use,
	// minimum 1.
	char valbuf[64];
	sprintf(valbuf, "%ld", (high < 0) ? 1 : high + 1);
	newver = valbuf;
      }
    }

    //
    // Canonicalize oldver and newver
    //
    Text coldver, coldverpar, oldverarc;
    Text cnewver, cnewverpar, newverarc;
    coldver = ReposUI::canonicalize(oldver, cpkg);
    ReposUI::split(coldver, coldverpar, oldverarc, "old-version");
    if (!omit_new) {
      cnewver = ReposUI::canonicalize(newver, cpkg);
      ReposUI::split(cnewver, cnewverpar, newverarc, "new-version");
    }
  
    trigger_vars.Put("VESTA_OLD_VERSION", coldver);
    trigger_vars.Put("VESTA_OMIT_OLD_VERSION", omit_old?"1":"0");
    trigger_vars.Put("VESTA_NEW_VERSION", cnewver);
    trigger_vars.Put("VESTA_OMIT_NEW_VERSION", omit_new?"1":"0");

    //
    // Compute default for sessdir
    //
    if (default_sess) {
      if (!omit_new) {
	// Set sessdir to newver with "checkout/" inserted before
	// last component.
	sessdir = cnewverpar + "/checkout/" + newverarc;
      } else {
	// Need to make up a unique name, since newver is
	// meaningless.  First, test whether oldver comes from a
	// checkout session.
	if (coldver.FindText("/checkout/") >= 0) {
	  // Oldver is from a checkout session, so set sessdir to
	  // oldver with the last "/" changed to a ".", and "." +
	  // uuser appended.  Later we will further append a
	  // uniquifying integer ".u" (starting at 1) to this.
	  sessdir = coldverpar + "." + oldverarc + "." + uuser;
	} else {
	  // Oldver is not from a checkout session, so set sessdir
	  // to oldver with "checkout/" inserted before the last
	  // component and "." + uuser appended.  Later we will
	  // further append a uniquifying integer ".u" (starting
	  // at 1) to this.
	  sessdir = coldverpar + "/checkout/" + oldverarc + "." + uuser;
	}			 
      }
    }

    //  
    // Compute default for foreign session directory
    //
    if(foreign_sess_dir) {
      assert(omit_new);
      // all foreign checkout session directories are placed in the 
      // foreignsessdirpar directory that is defined in vesta.cfg 
      // (usually /vesta/<current site>/foreign/). 
      // foreign_path_par in a case of default foreign sessdir
      // is the oldverpar without root, so sessdir
      // will contain following path:
      // /vesta/<current site>/foreign/<foreign site>/....
      Text foreign_path_par;
      if(coldverpar.Sub(0, foreignsessdirpar.Length()) == foreignsessdirpar) {
	// if oldver is under foreignsessdirpar we need to strip the 
	// foreignsessdirpar path in otder to avoid creation of mutiple 
	// foreign dirs
	foreign_path_par = coldverpar.Sub(foreignsessdirpar.Length());
      }
      else {
	foreign_path_par = 
	  ReposUI::stripSpecificRoot(coldverpar, ReposUI::VESTA, true);
      }
      if(coldver.FindText("/checkout/") >= 0) {
	sessdir = foreignsessdirpar + foreign_path_par + "." 
	  + oldverarc + "." + uuser;
      }
      else {
	sessdir = foreignsessdirpar + foreign_path_par + "/checkout/" 
	  + oldverarc + "." + uuser;
      } 
    }

    //
    // compute foreign session directory using path given by -s 
    //
    if(foreign_not_default) {
      assert(omit_new);
      // all foreign checkout session directories are placed in the 
      // foreignsessdirpar directory that is defined in vesta.cfg 
      // (usually /vesta/<current site>/foreign/). 
      // foreign_path_par in a case of not default foreign sessdir
      // is the sessdir defined by user in the -s option of command line,
      // qualified according to the package path and without the root, 
      // so sessdir will contain the following path:
      // /vesta/<current site>/foreign/<foreign site>/<pkg>/....
      Text csessdir = ReposUI::canonicalize(sessdir, cpkg);
      Text foreign_path_par = 
	ReposUI::stripSpecificRoot(csessdir, ReposUI::VESTA, true);
      sessdir = foreignsessdirpar + foreign_path_par;
    }

    //
    // Uniquify and canonicalize sessdir
    //
    Text csessdir, csessdirpar, sessdirarc, cslatest;
    VestaSource* vs_msessdirpar = NULL;
    if (!omit_sess) {
      if(foreign_sess_dir || foreign_not_default) {
	// just in case foreignsessdirpar is not properly defined in vesta.cfg
	csessdir = ReposUI::canonicalize(sessdir);
      }
      else {
	csessdir = ReposUI::canonicalize(sessdir, cpkg);
      }

      ReposUI::split(csessdir, csessdirpar, sessdirarc, "session-dir");
      cslatest = csessdirpar + "/latest";

      // find master of sessdir parent
      try {
	 vs_msessdirpar = ReposUI::filenameToMasterVS(csessdirpar, hints);
      }
      catch (ReposUI::failure f) {
	if(foreign_sess_dir || foreign_not_default) {
	  if(!query) {
	    Text pathname = csessdirpar.Sub(foreignsessdirpar.Length());
	    vs_msessdirpar = 
	      ReposUI::lookupCreatePath(foreignsessdirpar, pathname, hints);
	    // This directory might have just been created and have no
	    // type attribute.  Since it's going to be a session directory
	    // parent, it should have type checkout.
	    if(!vs_msessdirpar->inAttribs("type", "checkout")) {
	      vs_msessdirpar->addAttrib("type", "checkout");
	    }
	  }
	}
	else {
	  throw(f);
	}
      }

      // uniquify the session dir if needed
      if((default_sess && omit_new) ||
	 (!default_sess && uniquify_nondefault_sess) ||
	 (foreign_not_default && uniquify_nondefault_sess) ||
	 foreign_sess_dir) 
	{
	  if(vs_msessdirpar) 
	    {
	      sessdirarc = ReposUI::uniquify(vs_msessdirpar, sessdirarc);
	      csessdir = csessdirpar + "/" + sessdirarc;
	    }
	  else 
	    { // Can happen only in "-Q -F" case, when session parent dir 
	      // does not exist and should be created but was not because of 
	      // the Query flag.
	      csessdir = csessdirpar + "/" + sessdirarc + ".1";
	    }
	}
    }

    trigger_vars.Put("VESTA_SESSION_DIR", csessdir);
    trigger_vars.Put("VESTA_OMIT_SESSION_DIR", omit_sess?"1":"0");

    //
    // Compute default for workdir
    //
    if (default_work) {
      // Get last arc of absolute package name that does not
      // begin with a digit
      int i, j;
      j = cpkg.Length();
      for (;;) {
	i = cpkg.FindCharR('/', j-1);
	if (i == -1) {
	  // Should not happen
	  cerr << program_name
	       << ": can't find base package name in " << cpkg << endl;
	  exit(2);
	}
	if (!isdigit(cpkg[i+1])) {
	  workdir = cpkg.Sub(i+1, j-i-1);
	  break;
	}
	j = i;
      }
    }

    //
    // Canonicalize workdir
    //
    Text cworkdir, cworkdirpar, workdirarc;
    if (!omit_work) {
      cworkdir = ReposUI::canonicalize(workdir, defworkpar + "/" + user);
      ReposUI::split(cworkdir, cworkdirpar, workdirarc, "work-dir");
    }

    trigger_vars.Put("VESTA_WORK_DIR", cworkdir);
    trigger_vars.Put("VESTA_OMIT_WORK", omit_work?"1":"0");

    //
    // Look up oldver, first replicating here if needed
    //
    VestaSource *vs_oldver = NULL, *vs_roldver = NULL;
    if (!omit_old) {
      vs_roldver = ReposUI::filenameToRealVS(coldver, hints);
      if (vs_roldver->type == VestaSource::stub) {
	cerr << program_name << ": " << coldver
	     << " is still checked out";
	char *owner = vs_roldver->getAttrib("checkout-by");
	if(owner)
	  {
	    cerr << " by " << owner;
	    delete owner;
	  }
	char *dest = vs_roldver->getAttrib("checkout-to");
	if(dest)
	  {
	    cerr << " at " << dest;
	    delete dest;
	  }
	cerr << endl;
	exit(2);
      } else if (vs_roldver->type == VestaSource::ghost) {
	cerr << program_name << ": " << coldver
	     << " has been deleted" << endl;
	exit(2);
      }
      if (vs_roldver->host() == lhost && vs_roldver->port() == lport) {
	// A local replica exists
	vs_oldver = vs_roldver;
      } else if (!query) {
	// Get a local replica
	if (!quiet) {
	  cout << "Replicating " << coldver << " from "
	       << vs_roldver->host() << ":" << vs_roldver->port() << endl;
	}
	// We don't do the replication until later, after running
	// pre-triggers.
      }
    }

    trigger_vars.Put("VESTA_OLD_VERSION_REPOS",
		     (vs_roldver != 0)
		     ?(vs_roldver->host()+":"+vs_roldver->port())
		     :"");
    trigger_vars.Put("VESTA_OLD_VERSION_REMOTE",
		     ((vs_oldver == 0) && (vs_roldver != 0))?"1":"0");

    //
    // Look up master of newverpar and sanity check
    //
    VestaSource* vs_mnewverpar = NULL;
    Text hint_dir, master_hint;
    if (!omit_new) {
      vs_mnewverpar = ReposUI::filenameToMasterVS(cnewverpar, hints);
      if (!force && !vs_mnewverpar->inAttribs("type", "package")) {
	cerr << program_name << ": " << cnewverpar << " at "
	     << vs_mnewverpar->host() << ":" << vs_mnewverpar->port()
	     << " is not a package or branch" << endl;
	exit(2);
      }
      // check if the cnewver is not reserved by someone else 
      VestaSource* vs_newver = NULL;
      VestaSource::errorCode err =
	vs_mnewverpar->lookup(newverarc.chars(), vs_newver);
      if(err == VestaSource::ok) {
	assert(vs_newver->type == VestaSource::stub);
	cerr << program_name << ": error: " << cnewver 
	     << " is already reserved";
	char* owner = vs_newver->getAttrib("checkout-by");
	if (owner) {
	  cerr << " by " << owner;
	  delete owner;
	}
	char* dest = vs_newver->getAttrib("checkout-to");
	if (dest) {
	  cerr << " at " << dest;
	  delete dest;
	}
	char* time = vs_newver->getAttrib("checkout-time");
	if(time) {
	  cerr << " on " << time;
	  delete time;
	}
	char* msg = vs_newver->getAttrib("message");
	if(msg) {
	  cerr << " comment: " << msg;
	  delete msg;
	}
	cerr << endl;
	exit(2);
      } 

      if (!quiet) {
	cout << "Reserving version " << cnewver;
	if (vs_mnewverpar->host() != lhost ||
	    vs_mnewverpar->port() != lport) {
	  cout << " at "
	       << vs_mnewverpar->host() << ":" << vs_mnewverpar->port();
	}
	cout << endl;
      }
      
      // find directory whose master-repository attribute needs to be updated
      hint_dir = ReposUI::getMasterHintDir(vs_mnewverpar, cnewverpar);
      master_hint = vs_mnewverpar->getMasterHint();
    }

    trigger_vars.Put("VESTA_NEW_VERSION_REPOS",
		     (vs_mnewverpar != 0)
		     ?(vs_mnewverpar->host()+":"+vs_mnewverpar->port())
		     :"");
    trigger_vars.Put("VESTA_NEW_VERSION_REMOTE",
		     ((vs_mnewverpar != 0) &&
		      ((vs_mnewverpar->host() != lhost) ||
		       (vs_mnewverpar->port() != lport)))?"1":"0");

    //
    // Look up master of sessdirpar and sanity check
    //
    Text sess_hint_dir, sessdir_master_hint; 
    if (!omit_sess) {
      // master of sessdir parent should be found in previous step
      // unless -Q -F flags are set and session dir parent does not exist

      if(vs_msessdirpar && !force && 
	 !vs_msessdirpar->inAttribs("type", "checkout")) {
	cerr << program_name << ": " << csessdirpar << " at "
	     << vs_msessdirpar->host() << ":" << vs_msessdirpar->port()
	     << " is not a checkout directory" << endl;
	exit(2);
      }
      if(!quiet) {
	cout << "Creating session " << csessdir;
	if(vs_msessdirpar && (
	   vs_msessdirpar->host() != lhost ||
	   vs_msessdirpar->port() != lport)) {
	  cout << " at "
	       << vs_msessdirpar->host() << ":" << vs_msessdirpar->port();
	}
	cout << endl;
      }
      // find directory whose master-repository attribute needs to be updated
      if(vs_msessdirpar) {
	sess_hint_dir = ReposUI::getMasterHintDir(vs_msessdirpar, csessdirpar);
	sessdir_master_hint = vs_msessdirpar->getMasterHint();
	if(!omit_new && (sess_hint_dir == hint_dir))
	  // Don't try to update the master-repository attribute on the
	  // same directory twice.
	  sess_hint_dir = "";
      }
    }

    trigger_vars.Put("VESTA_SESSION_DIR_REPOS",
		     (vs_msessdirpar != 0)
		     ?(vs_msessdirpar->host()+":"+vs_msessdirpar->port())
		     :"");
    trigger_vars.Put("VESTA_SESSION_DIR_REMOTE",
		     ((vs_msessdirpar != 0) &&
		      ((vs_msessdirpar->host() != lhost) ||
		       (vs_msessdirpar->port() != lport)))?"1":"0");

    //
    // Look up workdirpar, creating it if necessary
    //
    VestaSource* vs_workdirpar = NULL;
    if (!omit_work) {
      try {
	vs_workdirpar = ReposUI::filenameToVS(cworkdirpar, lhost, lport);
      } catch (ReposUI::failure f) {
	// Try to auto-create workdirpar if it does not exist
	if (!query &&
	    (f.uerrno == ENOENT || f.code == VestaSource::notFound)) {
	  Text cworkdirparpar, workdirpararc;
	  ReposUI::split(cworkdirpar, cworkdirparpar, workdirpararc,
			 "work-dir parent");
	  VestaSource* vs_workdirparpar =
	    ReposUI::filenameToVS(cworkdirparpar, lhost, lport);
	  VestaSource::errorCode err = vs_workdirparpar->
	    insertMutableDirectory(workdirpararc.cchars(), NULL, true,
				   NULL, VestaSource::dontReplace,
				   &vs_workdirpar);
	  if (err != VestaSource::ok) {
	    cerr << program_name << ": error creating "
		 << cworkdirpar << ": "
		 << ReposUI::errorCodeText(err) << endl;
	    exit(2);
	  }
	  err = vs_workdirpar->setAttrib("#mode", mode);
	  assert(err == VestaSource::ok);
	} else {
	  throw;
	}
      }
      if (vs_workdirpar) {
	// Make name unique by adding numeric suffix if needed
	VestaSource* vs_dummy;
	VestaSource::errorCode err = 
	  vs_workdirpar->lookup(workdirarc.cchars(), vs_dummy);
	if (err != VestaSource::notFound) {
	  workdirarc = ReposUI::uniquify(vs_workdirpar, workdirarc);
	}
	cworkdir = cworkdirpar + "/" + workdirarc;
      }
      if (!quiet) {
	cout << "Making working directory " << cworkdir << endl;
      }
    }

    // Make environment variables available for the triggers
    ReposUI::setTriggerVars(trigger_vars,
			    verbose_triggers);

    // Run the "pre" triggers.
    ReposUI::runTriggers("vcheckout pre trigger",
			 verbose_triggers, !query);

    // Forward declarations of some variables for the part that does
    // the work
    VestaSourceAtomic* rvsa1 = NULL;
    VestaSourceAtomic* rvsa2 = NULL;
    VestaSource *vs_hint = NULL, *vs_sess_hint = NULL;

    if (!query)
      {
	// Make sure we have the parent directories where we're going
	// to create things.
	if(!omit_sess) assert(vs_msessdirpar);
	if(!omit_new) assert(vs_mnewverpar);
	if(!omit_work) assert(vs_workdirpar);

	if(!omit_old && (vs_oldver == 0))
	  {
	    // Now replicate the old version to the local repository.  We
	    // wait until after the pre-triggers to do this.
	    assert(vs_roldver != 0);
	    Replicator repl(vs_roldver->host(), vs_roldver->port(), lhost, lport);
	    Replicator::DirectiveSeq direcs;
	    Replicator::Directive d('+', coldver.Sub(vlen));
	    direcs.addhi(d);
	    repl.replicate(&direcs, rflags);
	    vs_oldver = ReposUI::filenameToVS(coldver, lhost, lport);
	  }

	//
	// Ready to construct the remote atomic action(s) and replicate
	// the results here.  May need to reference zero, one, or two
	// remote hosts.  If only one host is involved, we arrange to use
	// only one action and one call to the replicator.
	//
	Replicator* repl1 = NULL;
	Replicator* repl2 = NULL;
	Replicator::DirectiveSeq* direcs1 = NULL;
	Replicator::DirectiveSeq* direcs2 = NULL;
	// VS pointers and VSIndex for old version copies in each one of
	// two remote repositories
	VestaSource *vs_roldver1 = NULL, *vs_roldver2 = NULL;
	VestaSourceAtomic::VSIndex vsi_roldver1 = -1, vsi_roldver2 = -1;

	//
	// If needed, construct action to create newver remotely
	// and replicator directives to replicate it here.
	//
	VestaSource* vs_newverpar = NULL;
	VestaSource* vs_newver = NULL;
	if (!omit_new) {
	  if (vs_mnewverpar->host() == lhost &&
	      vs_mnewverpar->port() == lport) {
	    // Master is local
	    vs_newverpar = vs_mnewverpar;
	  } else {
	    // Master is remote
	    rvsa1 = NEW_CONSTR(VestaSourceAtomic, (vs_mnewverpar->host(),
						   vs_mnewverpar->port()));
	    VestaSourceAtomic::VSIndex vsi_mnewverpar =
	      rvsa1->decl(cnewverpar, vs_mnewverpar);
	    rvsa1->testMaster(cnewverpar, vsi_mnewverpar, true,
			      VestaSource::notMaster);
	    rvsa1->typeCheck(cnewverpar, vsi_mnewverpar, VestaSourceAtomic::
			     typebit(VestaSource::appendableDirectory),
			     VestaSource::inappropriateOp);
	    VestaSourceAtomic::VSIndex vsi_mnewver =
	      rvsa1->insertStub(cnewver, vsi_mnewverpar, newverarc,
				true, VestaSource::dontReplace);
	    if (!omit_old) {
	      rvsa1->setAttrib(cnewver, vsi_mnewver, "old-version", coldver);

	      // add next-versions attribute to the old version (newverpar repos)
	      try {
		vs_roldver1 = ReposUI::filenameToVS(coldver, vs_mnewverpar->host(),
						    vs_mnewverpar->port());
		vsi_roldver1 = rvsa1->decl(coldver, vs_roldver1);
		rvsa1->setTarget("", VestaSource::ok, VestaSource::noPermission);
		rvsa1->addAttrib(coldver + ": adding to next-versions",
				 vsi_roldver1, 
				 "next-versions", cnewver);
		rvsa1->setTarget("");
	      }
	      catch(ReposUI::failure){
		// there is no old version copy in newverpar repos
		vs_roldver1 = NULL;
	      }
	    }
	    if (!omit_sess) {
	      rvsa1->setAttrib(cnewver, vsi_mnewver, "session-dir", csessdir);
	    }
	    if(!omit_msg && !message.Empty()){
	      rvsa1->setAttrib(cnewver, vsi_mnewver, "message", message);
	    }
	    rvsa1->setAttrib(cnewver, vsi_mnewver, "checkout-time", timebuf);
	    rvsa1->setAttrib(cnewver, vsi_mnewver, "checkout-by", cuser);
	    rvsa1->setAttrib(cnewver, vsi_mnewver, "checkout-from",
			     vs_mnewverpar->host() + ":" + vs_mnewverpar->port());
	    rvsa1->setAttrib(cnewver, vsi_mnewver, "checkout-to",
			     lhost + ":" + lport);
	    rvsa1->setAttrib(cnewver, vsi_mnewver, "#mode", mode);

	    repl1 = NEW_CONSTR(Replicator, (vs_mnewverpar->host(),
					    vs_mnewverpar->port(),
					    lhost, lport));
	    direcs1 = NEW(Replicator::DirectiveSeq);
	    Replicator::Directive d('+', cnewver.Sub(vlen));
	    direcs1->addhi(d);
	  }
	}

	//
	// If needed, construct action to create sessdir remotely
	// and replicator directives to replicate it here.
	//
	VestaSource* vs_sessdirpar = NULL;
	VestaSource* vs_sessdir = NULL;
	if (!omit_sess) {
	  if (vs_msessdirpar->host() == lhost &&
	      vs_msessdirpar->port() == lport) {
	    // Master is local
	    vs_sessdirpar = vs_msessdirpar;
	  } else {
	    // Master is remote
	    if (rvsa1 != NULL &&
		vs_msessdirpar->host() == vs_mnewverpar->host() &&
		vs_msessdirpar->port() == vs_mnewverpar->port()) {
	      rvsa2 = rvsa1;
	      repl2 = repl1;
	      direcs2 = direcs1;
	      // In this case we can re-use the VestaSource and VSIndex
	      // for the old-version, since the repository and atomic
	      // action are the same.
	      vs_roldver2 = vs_roldver1;
	      vsi_roldver2 = vsi_roldver1;
	    } else {
	      rvsa2 = NEW_CONSTR(VestaSourceAtomic, (vs_msessdirpar->host(),
						     vs_msessdirpar->port()));
	      repl2 = NEW_CONSTR(Replicator, (vs_msessdirpar->host(),
					      vs_msessdirpar->port(),
					      lhost, lport));
	      direcs2 = NEW(Replicator::DirectiveSeq);
	      if(!omit_old)
		{
		  // Look up old version in session directory parent
		  // master.
		  try
		    {
		      vs_roldver2 =
			ReposUI::filenameToVS(coldver, 
					      vs_msessdirpar->host(),
					      vs_msessdirpar->port());
		    }
		  catch(ReposUI::failure)
		    {
		      // there is no old version copy in sessdirpar repos
		      vs_roldver2 = NULL;
		    }
		}
	    }
	    VestaSourceAtomic::VSIndex vsi_msessdirpar =
	      rvsa2->decl(csessdirpar, vs_msessdirpar);
	    rvsa2->testMaster(csessdirpar, vsi_msessdirpar, true,
			      VestaSource::notMaster);
	    rvsa2->typeCheck(csessdirpar, vsi_msessdirpar, VestaSourceAtomic::
			     typebit(VestaSource::appendableDirectory),
			     VestaSource::inappropriateOp);
	    VestaSourceAtomic::VSIndex vsi_msessdir =
	      rvsa2->insertAppendableDirectory(csessdir, vsi_msessdirpar,
					       sessdirarc, true,
					       VestaSource::dontReplace);
	    if (!omit_old) {
	      rvsa2->setAttrib(csessdir, vsi_msessdir, "old-version", coldver);
	      // add next-sessions attribute to the old version (sessdirpar repos)
	      if(vs_roldver2)
		{
		  if(vsi_roldver2 == -1)
		    vsi_roldver2 = rvsa2->decl(coldver, vs_roldver2);
		  rvsa2->setTarget("", VestaSource::ok, VestaSource::noPermission);
		  rvsa2->addAttrib(coldver + ": adding to next-sessions",
				   vsi_roldver2, 
				   "next-sessions", csessdir);
		  rvsa2->setTarget("");
		}
	    }
	    if (!omit_new) {
	      rvsa2->setAttrib(csessdir, vsi_msessdir, "new-version", cnewver);
	    }
	    if(!omit_msg && !message.Empty()) {
	      rvsa2->setAttrib(csessdir, vsi_msessdir, "message", message);
	    }

	    rvsa2->setAttrib(csessdir, vsi_msessdir, "checkout-time", timebuf);
	    rvsa2->setAttrib(csessdir, vsi_msessdir, "checkout-by", cuser);
	    rvsa2->setAttrib(csessdir, vsi_msessdir, "checkout-from",
			     (vs_msessdirpar->host() + ":" +
			      vs_msessdirpar->port()));
	    rvsa2->setAttrib(csessdir, vsi_msessdir, "checkout-to",
			     (lhost + ":" + lport));
	    rvsa2->setAttrib(csessdir, vsi_msessdir, "type", "session");
	    rvsa2->setAttrib(csessdir, vsi_msessdir, "#mode", mode);

	    Replicator::Directive d1('+', csessdir.Sub(vlen));
	    Replicator::Directive d2('+', cslatest.Sub(vlen));
	    direcs2->addhi(d1);
	    direcs2->addhi(d2);
	  }
	}

	//
	// Disable ^C signal to make it less likely that we will be
	// killed after having done one or more remote actions but
	// before having finished the whole job.
	//
	signal(SIGINT, SIG_IGN);

	//
	// Run the remote action(s) and replication(s)
	//
	if (rvsa1) {
	  if (!rvsa1->run()) {
	    VestaSource::errorCode err;
	    if (rvsa1->lasterr == VestaSource::ok) {
	      err = rvsa1->okreplace;
	    } else {
	      err = rvsa1->lasterr;
	    }
	    cerr << program_name << ": " << rvsa1->name(rvsa1->ndone) << " at "
		 << vs_mnewverpar->host() << ":" << vs_mnewverpar->port()
		 << ": " << ReposUI::errorCodeText(err) << endl;
	    exit(2);
	  }
	  repl1->replicate(direcs1, rflags);
	}
	if (rvsa2 && rvsa2 != rvsa1) {
	  if (!rvsa2->run()) {
	    VestaSource::errorCode err;
	    if (rvsa2->lasterr == VestaSource::ok) {
	      err = rvsa2->okreplace;
	    } else {
	      err = rvsa2->lasterr;
	    }
	    cerr << program_name << ": " << rvsa2->name(rvsa2->ndone) << " at "
		 << vs_msessdirpar->host() << ":" << vs_msessdirpar->port()
		 << ": " << ReposUI::errorCodeText(err) << endl;
	    exit(2);
	  }
	  repl2->replicate(direcs2, rflags);
	}

	// check if the next_versions attribute was added to the old
	// version(newverpar repos)
	if(vs_roldver1 && !quiet) {
	  assert(!omit_new);
	  if(!vs_roldver1->inAttribs("next-versions", cnewver.cchars())) {
	    cerr << program_name << ": warning: "
	      "next-versions attribute couldn't be added to version: "
		 << coldver << " at " << vs_mnewverpar->host() << ":" 
		 << vs_mnewverpar->port() << endl;
	  }
	}
	// check if the next_sessions attribute was added to the old
	// version(sessdirpar repos)
	if(vs_roldver2 && !quiet) {
	  assert(!omit_sess);
	  if(!vs_roldver2->inAttribs("next-sessions", csessdir.cchars())) {
	    cerr << program_name << ": warning: " 
	      "next-sessions attribute couldn't be added to version: " << coldver
		 << " at " << vs_msessdirpar->host() << ":" 
		 << vs_msessdirpar->port() << endl;
	  }
	}

	//
	// Do the mastership transfer(s)
	//
	if (!omit_new && vs_newverpar == NULL) {
	  VestaSource::errorCode err =
	    VDirSurrogate::acquireMastership(cnewver.Sub(vlen).cchars(),
					     lhost, lport,
					     vs_mnewverpar->host(),
					     vs_mnewverpar->port());
	  if (err != VestaSource::ok) {
	    cerr << program_name << ": error acquiring mastership of "
		 << cnewver << ": " << ReposUI::errorCodeText(err) << endl;
	    exit(2);
	  }
	  vs_newverpar = ReposUI::filenameToVS(cnewverpar, lhost, lport);
	  vs_newver = ReposUI::filenameToVS(cnewver, lhost, lport);
	}
	if (!omit_sess && vs_sessdirpar == NULL) {
	  VestaSource::errorCode err =
	    VDirSurrogate::acquireMastership(csessdir.Sub(vlen).cchars(),
					     lhost, lport,
					     vs_msessdirpar->host(),
					     vs_msessdirpar->port());
	  if (err != VestaSource::ok) {
	    cerr << program_name << ": error acquiring mastership of "
		 << csessdir << ": " << ReposUI::errorCodeText(err) << endl;
	    exit(2);
	  }
	  vs_sessdirpar = ReposUI::filenameToVS(csessdirpar, lhost, lport);
	  vs_sessdir = ReposUI::filenameToVS(csessdir, lhost, lport);
	}

	//
	// Construct the local atomic action
	//
	VestaSourceAtomic vsa(lhost, lport);
	VestaSourceAtomic::VSIndex vsi_oldver = -1, vsi_newverpar = -1,
	  vsi_sessdirpar = -1, vsi_workdirpar = -1,
	  vsi_newver = -1, vsi_sessdir = -1;

	// Do all error checking first, because once the atomic program
	// starts making changes, it can't roll back unless the
	// repository crashes.
	if (!omit_old) {
	  vsi_oldver = vsa.decl(coldver, vs_oldver);
	  vsa.typeCheck(coldver, vsi_oldver, VestaSourceAtomic::
			typebit(VestaSource::immutableDirectory),
			VestaSource::inappropriateOp);
	  vsa.accessCheck(coldver, vsi_oldver, AccessControl::read,
			  true, VestaSource::noPermission);
	}

	if (!omit_new) {
	  vsi_newverpar = vsa.decl(cnewverpar, vs_newverpar);
	  if (vs_newver) {
	    // newver already exists
	    vsi_newver = vsa.decl(cnewver, vs_newver);
	    vsa.typeCheck(cnewver, vsi_newver, VestaSourceAtomic::
			  typebit(VestaSource::stub),
			  VestaSource::inappropriateOp);
	    vsa.testMaster(cnewver, vsi_newver, true, VestaSource::notMaster);
	    vsa.accessCheck(cnewver, vsi_newver, AccessControl::write, true,
			    VestaSource::noPermission);
	  } else {
	    // newver will be created in newverpar
	    vsa.typeCheck(cnewverpar, vsi_newverpar, VestaSourceAtomic::
			  typebit(VestaSource::appendableDirectory),
			  VestaSource::inappropriateOp);
	    vsa.testMaster(cnewverpar, vsi_newverpar, true,
			   VestaSource::notMaster);
	    vsa.accessCheck(cnewverpar, vsi_newverpar, AccessControl::write, true,
			    VestaSource::noPermission);
	    // Be sure new version doesn't already exist
	    vsa.setTarget("", VestaSource::notFound,
			  VestaSource::notFound, VestaSource::nameInUse);
	    vsa.lookup(cnewver, vsi_newverpar, newverarc);
	    vsa.setTarget("");
	  }

	  // Update master-repository hint of newverpar or its ancestor
	  if(!master_hint.Empty())
	    {
	      vs_hint = ReposUI::filenameToVS(hint_dir, lhost, lport);
	      if(!vs_hint->inAttribs("master-repository", master_hint.cchars())) {
		VestaSourceAtomic::VSIndex vsi_hint = 
		  vsa.decl("update master hint on " + hint_dir, vs_hint);
		vsa.setTarget("", VestaSource::ok, VestaSource::noPermission);
		vsa.setAttrib("update master hint on " + hint_dir, vsi_hint,
			      "master-repository", master_hint);
		vsa.setTarget("");
	      }
	    }
	}

	if (!omit_sess) {
	  vsi_sessdirpar = vsa.decl(csessdirpar, vs_sessdirpar);
	  if (vs_sessdir) {
	    // sessdir already exists
	    vsi_sessdir = vsa.decl(csessdir, vs_sessdir);
	    vsa.typeCheck(csessdir, vsi_sessdir, VestaSourceAtomic::
			  typebit(VestaSource::appendableDirectory),
			  VestaSource::inappropriateOp);
	    vsa.testMaster(csessdir, vsi_sessdir, true, VestaSource::notMaster);
	    vsa.accessCheck(csessdir, vsi_sessdir, AccessControl::write, true,
			    VestaSource::noPermission);

	  } else {
	    // sessdir will be created in sessdirpar
	    vsa.typeCheck(csessdirpar, vsi_sessdirpar, VestaSourceAtomic::
			  typebit(VestaSource::appendableDirectory),
			  VestaSource::inappropriateOp);
	    vsa.testMaster(csessdirpar, vsi_sessdirpar, true,
			   VestaSource::notMaster);
	    vsa.accessCheck(csessdirpar, vsi_sessdirpar, AccessControl::write,
			    true, VestaSource::noPermission);
	    // Be sure sessdir doesn't already exist
	    vsa.setTarget("", VestaSource::notFound,
			  VestaSource::notFound, VestaSource::nameInUse);
	    vsa.lookup(csessdir, vsi_sessdirpar, sessdirarc);
	    vsa.setTarget("setTarget");
	  }

	  // Update master-repository hint of sessdirpar or its ancestor
	  if(!sess_hint_dir.Empty() && !sessdir_master_hint.Empty())
	    {
	      vs_sess_hint = ReposUI::filenameToVS(sess_hint_dir, lhost, lport);
	      if(!vs_sess_hint->inAttribs("master-repository", 
					  sessdir_master_hint.cchars())) {
		VestaSourceAtomic::VSIndex vsi_sesshint = 
		  vsa.decl("update master hint on " + sess_hint_dir, vs_sess_hint);
		vsa.setTarget("", VestaSource::ok, VestaSource::noPermission);
		vsa.setAttrib("update master hint on " + sess_hint_dir,
			      vsi_sesshint,
			      "master-repository", sessdir_master_hint);
		vsa.setTarget("");
	      }
	    }
	}

	if (!omit_work) {
	  vsi_workdirpar = vsa.decl(cworkdirpar, vs_workdirpar);
	  vsa.typeCheck(cworkdirpar, vsi_workdirpar, VestaSourceAtomic::
			typebit(VestaSource::mutableDirectory),
			VestaSource::inappropriateOp);
	  vsa.accessCheck(cworkdirpar, vsi_workdirpar, AccessControl::write, true,
			  VestaSource::noPermission);
	  // Be sure workdir doesn't already exist
	  vsa.setTarget("", VestaSource::notFound,
			VestaSource::notFound, VestaSource::nameInUse);
	  vsa.lookup(cworkdir, vsi_workdirpar, workdirarc);
	  vsa.setTarget("");
	}
	
	// All is well if atomic program runs up to this step; start
	// making the changes.
	if (!omit_new) {
	  if (!vs_newver) {
	    // Insert reservation stub for newver
	    vsi_newver =
	      vsa.insertStub(cnewver, vsi_newverpar, newverarc,
			     true, VestaSource::dontReplace);
	  }

	  // Set attributes on newver stub
	  if (!omit_old) {
	    vsa.setAttrib(cnewver, vsi_newver, "old-version", coldver);
	    // add next-versions attribute to the old version (local)
	    vsa.setTarget("", VestaSource::ok, VestaSource::noPermission);
	    vsa.addAttrib(coldver + ": adding to next-versions", vsi_oldver, 
			  "next-versions", cnewver);
	    vsa.setTarget("");
	  }
	  if (!omit_sess) {
	    vsa.setAttrib(cnewver, vsi_newver, "session-dir", csessdir);
	  }
	  if (!omit_work) {
	    vsa.setAttrib(cnewver, vsi_newver, "work-dir", cworkdir);
	  }
	  if(!omit_msg && !message.Empty()) {
	    vsa.setAttrib(cnewver, vsi_newver, "message", message);
	  }
	  vsa.setAttrib(cnewver, vsi_newver, "checkout-time", timebuf);
	  vsa.setAttrib(cnewver, vsi_newver, "checkout-by", cuser);
	  vsa.setAttrib(cnewver, vsi_newver, "#mode", mode);
	}

	if (!omit_sess) {
	  if (!vs_sessdir) {
	    // Insert session directory
	    vsi_sessdir =
	      vsa.insertAppendableDirectory(csessdir, vsi_sessdirpar, sessdirarc,
					    true, VestaSource::dontReplace);
	  }
	  // Set attribs on session directory
	  if (!omit_old) {
	    vsa.setAttrib(csessdir, vsi_sessdir, "old-version", coldver);
	    // add next-sessions attribute to the old version (local)
	    vsa.setTarget("", VestaSource::ok, VestaSource::noPermission);
	    vsa.addAttrib(coldver + ": adding to next-sessions", vsi_oldver, 
			  "next-sessions", csessdir);
	    vsa.setTarget("");
	  }
	  if (!omit_new) {
	    vsa.setAttrib(csessdir, vsi_sessdir, "new-version", cnewver);
	  }
	  if(!omit_msg && !message.Empty()) {
	    vsa.setAttrib(csessdir, vsi_sessdir, "message", message);
	  }
	  if (!omit_work) {
	    vsa.setAttrib(csessdir, vsi_sessdir, "work-dir", cworkdir);
	  }
	  vsa.setAttrib(csessdir, vsi_sessdir, "checkout-time", timebuf);
	  vsa.setAttrib(csessdir, vsi_sessdir, "checkout-by", cuser);
	  vsa.setAttrib(csessdir, vsi_sessdir, "type", "session");
	  vsa.setAttrib(csessdir, vsi_sessdir, "#mode", mode);

	  // Insert session version 0
	  Text csessver0 = csessdir + "/0";
	  if (!omit_old) {
	    vsa.insertImmutableDirectory(csessver0, vsi_sessdir, "0", vsi_oldver,
					 true, VestaSource::dontReplace);
	  }

	  // Insert "latest" link in session
	  Text clatest = csessdir + "/latest";
	  VestaSourceAtomic::VSIndex vsi_latest =
	    vsa.insertStub(clatest, vsi_sessdir, "latest", true,
			   VestaSource::dontReplace);
	  vsa.setAttrib(clatest, vsi_latest, "symlink-to", "$LAST");
	}

	if (!omit_work) {
	  // Insert working directory
	  // Note: okay for vsi_oldver to be -1 here;
	  // this makes workdir empty.
	  VestaSourceAtomic::VSIndex vsi_workdir =
	    vsa.insertMutableDirectory(cworkdir, vsi_workdirpar, workdirarc,
				       vsi_oldver, true, VestaSource::dontReplace);
	  // Set attribs on workdir
	  if (!omit_old) {
	    vsa.setAttrib(cworkdir, vsi_workdir, "old-version", coldver);
	  }
	  if (!omit_new) {
	    vsa.setAttrib(cworkdir, vsi_workdir, "new-version", cnewver);
	  }
	  if (!omit_sess) {
	    vsa.setAttrib(cworkdir, vsi_workdir, "session-dir", csessdir);
	    if (!omit_old) {
	      vsa.setAttrib(cworkdir, vsi_workdir, "session-ver-arc", "0");
	    }
	  }
	  vsa.setAttrib(cworkdir, vsi_workdir, "checkout-time", timebuf);
	  vsa.setAttrib(cworkdir, vsi_workdir, "checkout-by", cuser);
	}

	//
	// Run the local action
	//
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
      }

    // Run the "post" triggers
    ReposUI::runTriggers("vcheckout post trigger",
			 verbose_triggers, !query);

    if(query)
      {
	if(!quiet)
	  cerr << program_name << ": nothing done (-Q flag given)" << endl;
	exit(0);
      }

    // check if next-versions and/or next-sessions attributes were added 
    // to the old version (local)
    if(!omit_old && !quiet) {
      if(!omit_new) {
	if(!vs_oldver->inAttribs("next-versions", cnewver.cchars())) {
	  cerr << program_name << ": warning: " 
	    "next-versions attribute couldn't be added to version: "
	       << coldver;
	  // Only include the local repository host/port if we're
	  // doing operations in multiple repositories.
	  if(rvsa1 || rvsa2)
	    cerr << " at " << lhost << ":" << lport;
	  cerr << endl;
	}
      }
      if(!omit_sess) {
	if(!vs_oldver->inAttribs("next-sessions", csessdir.cchars())) {
	  cerr << program_name << ": warning: "
	    "next-sessions attribute couldn't be added to version: "
	       << coldver;
	  // Only include the local repository host/port if we're
	  // doing operations in multiple repositories.
	  if(rvsa1 || rvsa2)
	    cerr << " at " << lhost << ":" << lport;
	  cerr << endl;
	}
      }
    }
    //check if master-repository hint was updated on newverpar or its ancestor
    if(!omit_new && !quiet && vs_hint != 0) {
      if(!vs_hint->inAttribs("master-repository", master_hint.cchars()))
	cerr << program_name 
	     << ": warning: incorrect master-repository attribute of " 
	     << hint_dir << " couldn't be updated (should be "
	     << master_hint << ")" << endl;
    }
    //check if master-repository hint was updated on sessdirpar or its ancestor
    if(!omit_sess && !quiet && vs_sess_hint != 0) {
      assert(!sessdir_master_hint.Empty());
      if(!vs_sess_hint->inAttribs("master-repository", 
				  sessdir_master_hint.cchars()))
	cerr << program_name 
	     << ": warning: incorrect master-repository attribute of " 
	     << sess_hint_dir << " couldn't be updated (should be "
	     << sessdir_master_hint << ")" << endl;
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
  } catch (Replicator::Failure f) {
    if (f.code == (VestaSource::errorCode) -1) {
      cerr << program_name << ": " << f.msg << endl;
    } else {
      cerr << program_name << ": " << ReposUI::errorCodeText(f.code)
	   << ", " << f.msg << endl;
    }
    exit(2);
  } catch(FS::Failure f) {
    cerr << program_name << ": " << f << endl;
    exit(2);
  }
  return 0;
}
