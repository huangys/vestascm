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
// vmkdir.C
//

// Create a directory in the Vesta repository.  Provides an
// alternative to the NFS interface on platofrms with obstinate NFS
// client code (e.g. some of the Linux 2.4 series).

// See documentation in vmkdir.1.mtex

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
using std::cin;
using std::cerr;
using std::endl;
using std::flush;

Text program_name;

void usage()
{
  cerr << "Usage: " << program_name << endl <<
    "        [-q] [-Q] [-v] [-a | -A] [-p] [-f]" << endl <<
    "        [-h hints]" << endl <<
    "        [-R repos]" << endl <<
    "        directory" << endl << endl;
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
    Text repos;
    bool default_repos = true;

    // If the parent directory is mastered remotely, should we have
    // the local repository acquire mastership of the created
    // directory?  Defaults to on.
    bool acquire_mastership = false;

    Text cuser(AccessControl::self()->user());
    Text user(cuser.Sub(0, cuser.FindChar('@')));
    Text lhost(VDirSurrogate::defaultHost());
    Text lport(VDirSurrogate::defaultPort());
    const int vlen = 7; // ==strlen("/vesta/")
    const Replicator::Flags rflags = (Replicator::Flags) 
      (Replicator::attrNew | Replicator::attrOld | Replicator::attrAccess |
       Replicator::revive | Replicator::inclStubs | Replicator::latest);

    if(VestaConfig::is_set("UserInterface", "vmkdir_acquires"))
      {
	acquire_mastership = VestaConfig::get_bool("UserInterface",
						   "vmkdir_acquires");
      }
    Text defhints;
    (void) VestaConfig::get("UserInterface", "DefaultHints", defhints);

    TextTextTbl trigger_vars;
    trigger_vars.Put("VESTA_TOOL", program_name_tail());

    //
    // Parse command line
    //
    bool quiet = false;
    bool query = false;
    bool force = false;
    bool verbose_triggers = false;

    // Should we set the type attribute on the created directory to
    // package-parent?
    bool set_package_parent = false;

    Text dir, oldver, hints;

    opterr = 0;
    for (;;) {
      int c = getopt(argc, argv, "qQaApfh:R:v");
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
      case 'p':
	set_package_parent = true;
	break;
      case 'f':
	force = true;
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
    dir = Text(argv[optind]);

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
    hints = hints + " " + defhints;

    trigger_vars.Put("VESTA_REPOS", lhost+":"+lport);
    trigger_vars.Put("VESTA_MASTER_HINTS", hints);

    //
    // Qualify and canonicalize names
    //
    Text cdir, cparent, newpart;
    cdir = ReposUI::canonicalize(dir);
    ReposUI::split(cdir, cparent, newpart, "directory name");

    trigger_vars.Put("VESTA_DIR", cdir);

    // Is the parent the appendable root?
    bool parent_is_app_root =
      (strcmp(cparent.cchars(), "/vesta") == 0);

    // Is the directory to be created in the mutable root?
    bool in_mutable_root = 
      (strncmp(cdir.cchars(), "/vesta-work/", 12)  == 0);

    // The volatile root is often mounted inside the mutable root
    // (i.e. /vesta-work/.volatile).  We'll refuse to create a
    // directory in the volatile root.
    Text volatileRoot = VestaConfig::get_Text("Run_Tool", "VolatileRootName");
    if(volatileRoot[volatileRoot.Length()-1] != '/')
      {
	volatileRoot += "/";
      }
    if(strncmp(dir.cchars(),
	       volatileRoot.cchars(), volatileRoot.Length()) == 0)
      {
	cerr << program_name << ": " << dir
	     << " is inside the volatile root ("
	     << volatileRoot << ")" << endl;
	exit(2);
      }

    //
    // Look up parent and sanity check
    //
    VestaSource* vs_mparent;
    if(parent_is_app_root)
      {
	// Special case: there is no master of the appendable root.
	// User must be the wizard to have permission to create here.
	vs_mparent = VDirSurrogate::repositoryRoot(lhost, lport);
      }
    else if(in_mutable_root)
      {
	// Local repository is always the master of the mutable root.
	vs_mparent = ReposUI::filenameToVS(cparent, lhost, lport);

	// Don't set type to package-parent on mutable directories
	set_package_parent = false;
      }
    else
      {
	// If it's not in the mutable root or a top-level child of the
	// appendable root, find the master of the parent.
	vs_mparent = ReposUI::filenameToMasterVS(cparent, hints);
	if (!force)
	  {
	    // Be default, don't allow the user to created directories
	    // within a package, branch, checkout, or session directory.

	    if(vs_mparent->inAttribs("type", "package"))
	      {
		cerr << program_name << ": " << cparent
		     << " has type package" << endl;
		exit(2);
	      }
	    // This case is a little redundant, because branches normally
	    // also have "package" in their type (see the vbranch man
	    // page).  Can't hurt to check though.
	    else if(vs_mparent->inAttribs("type", "branch"))
	      {
		cerr << program_name << ": " << cparent
		     << " has type branch" << endl;
		exit(2);
	      }
	    else if(vs_mparent->inAttribs("type", "checkout"))
	      {
		cerr << program_name << ": " << cparent
		     << " has type checkout" << endl;
		exit(2);
	      }
	    else if(vs_mparent->inAttribs("type", "session"))
	      {
		cerr << program_name << ": " << cparent
		     << " has type session" << endl;
		exit(2);
	      }
	  }
      }
    Text mhost(vs_mparent->host());
    Text mport(vs_mparent->port());
    bool remote = (mhost != lhost || mport != lport);
    Text hint_dir;
    if(remote) {
      // find directory whose master-repository attribute needs to be updated
      hint_dir = ReposUI::getMasterHintDir(vs_mparent, cparent);
    }

    trigger_vars.Put("VESTA_MASTER_REPOS", mhost+":"+mport);

    trigger_vars.Put("VESTA_MASTER_REMOTE", remote ? "1" : "0");

    trigger_vars.Put("VESTA_ACQUIRE_MASTERSHIP",
		     (remote && acquire_mastership) ? "1" : "0");

    // We wait until now to set this, as we turn this option off if
    // the directory is in the mutable root.
    trigger_vars.Put("VESTA_PACKAGE_PARENT",
		     set_package_parent ? "1" : "0");

    // If the directory to be created is in the mutable root or
    // directly below the appendable root, it must be local.

    // (parent_is_app_root || in_mutable_root) => !remote
    assert(!remote || (!parent_is_app_root && !in_mutable_root));

    if (!quiet) {
      cout << "Creating directory " << cdir;
      if (remote) {
	cout << " at " << mhost << ":" << mport;
      }
      cout << endl;
    }

    // Make environment variables available for the triggers
    ReposUI::setTriggerVars(trigger_vars,
			    verbose_triggers);

    // Run the "pre" triggers.
    ReposUI::runTriggers("vmkdir pre trigger",
			 verbose_triggers, !query);

    if (!query)
      {
	// Creating a top-level directory beneath the appendable root
	// is a rare occurrence that has special rules to avoid
	// violating the replication invariant.
	if(parent_is_app_root)
	  {
	    cout
	      << endl
	      << "Warning: Top-level directories must be unique.  (If they"
	      << endl
	      << "         aren't, you can can violate the replication"
	      << endl
	      << "         invariant.)" << endl << endl
	      << "         The recommend method is to base top-level"
	      << endl
	      << "         directory names on a domain or e-mail address that"
	      << endl
	      << "         you own." << endl << endl
	      << "Are you sure you want to proceed (y/n)? " << flush;

	    const int BuffLen = 20; char buff[BuffLen];
	    cin.getline(buff, BuffLen);
	    if (buff[0] != 'y')
	      {
		cout << "Not creating " << cdir << endl;
		exit(0);
	      }
	  }

	//
	// Construct atomic action to create the directory.  (This is a
	// little bit of overkill, but it ensures that the package-parent
	// attribute gets added in the same action.)
	//
	VestaSourceAtomic vsa(mhost, mport);

	// Declare parent directory and make sure it is master
	VestaSourceAtomic::VSIndex vsi_parent =
	  vsa.decl(cparent, vs_mparent);
	if(!parent_is_app_root)
	  {
	    vsa.testMaster(cparent, vsi_parent, true,
			   VestaSource::notMaster);
	  }

	// Make directory.  This is the last step that should be able to
	// halt, so we just try it; no need to test first.
	VestaSourceAtomic::VSIndex vsi_dir;
	if(in_mutable_root)
	  {
	    vsi_dir =
	      vsa.insertMutableDirectory(cdir, vsi_parent,
					 newpart, -1, true,
					 VestaSource::dontReplace);
	  }
	else
	  {
	    vsa.typeCheck(cparent, vsi_parent, VestaSourceAtomic::
			  typebit(VestaSource::appendableDirectory),
			  VestaSource::inappropriateOp);
	    vsi_dir =
	      vsa.insertAppendableDirectory(cdir, vsi_parent,
					    newpart, true,
					    VestaSource::dontReplace);
	  }

	// Set attributes
	if(set_package_parent)
	  {
	    assert(!in_mutable_root);
	    vsa.setAttrib(cdir, vsi_dir, "type", "package-parent");
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
	  if((err == VestaSource::noPermission) && parent_is_app_root)
	    {
	      cerr << "(Maybe you need to be " 
		   << VestaConfig::get_Text("Repository", "vwizard_user")
		   << "?)" << endl;
	    }
	  exit(2);
	}

	// Done if master was local (or was a special case that's always
	// local)
	if(remote && !parent_is_app_root && !in_mutable_root)
	  {
	    //
	    // Replicate the new directory here
	    //
	    Replicator repl(mhost, mport, lhost, lport);
	    Replicator::DirectiveSeq direcs;
	    Replicator::Directive d('+', cdir.Sub(vlen));
	    direcs.addhi(d);
	    repl.replicate(&direcs, rflags);

	    //
	    // Take mastership of the directory if requested.
	    //
	    if(acquire_mastership)
	      {
		VestaSource::errorCode err =
		  VDirSurrogate::acquireMastership(cdir.Sub(vlen).cchars(),
						   lhost, lport, mhost, mport);
		if (err != VestaSource::ok) {
		  cerr << program_name << ": error acquiring mastership of "
		       << cdir << ": " << ReposUI::errorCodeText(err) << endl;
		  exit(2);
		}
	      }

	    // Update master-repository hint
	    Text master_hint = vs_mparent->getMasterHint();
	    if(!master_hint.Empty())
	      {
		VestaSource* vs_hint = NULL;
		vs_hint = ReposUI::filenameToVS(hint_dir, lhost, lport);
		if(!vs_hint->inAttribs("master-repository",
				       master_hint.cchars())) {
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
	  }
      }

    // Run the "post" triggers
    ReposUI::runTriggers("vmkdir post trigger",
			 verbose_triggers, !query);

    if(query)
      {
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
  }

  return 0;
}
