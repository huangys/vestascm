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
// vcheckin.C
//

// Check in the current version from a Vesta checkout session.
// See vcheckin.1.mtex for documentation.


#include <Basics.H>
#include <Text.H>
#include <VestaConfig.H>
#include <VestaSource.H>
#include <VDirSurrogate.H>
#include <VestaSourceAtomic.H>
#include <Replicator.H>
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

Text program_name;

void
usage()
{
  cerr << "Usage: " << program_name << endl <<
    "        [-q] [-Q] [-v] [-f]" << endl <<
    "        [-m message | -M]" << endl <<
    "        [-s session-dir | -S]" << endl <<
    "        [-c content | -C]" << endl <<
    "        [-n new-version]" << endl <<
    "        [-R repos]" << endl <<
    "        [-d dst] | -D]" << endl <<
    "        [[-w] work-dir | -W]" << endl << endl;
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
    Text defpkgpar, defworkpar, timefmt;
    defpkgpar = VestaConfig::get_Text("UserInterface",
				      "DefaultPackageParent");
    defworkpar = VestaConfig::get_Text("UserInterface",
				       "DefaultWorkParent");
    timefmt = VestaConfig::get_Text("UserInterface", "TimeFormat");

    bool prompt_msg = true, omit_msg = false;
    if(VestaConfig::is_set("UserInterface", "vcheckin_message")) {
      if(!VestaConfig::get_bool("UserInterface", "vcheckin_message")) {
	prompt_msg = false;
	omit_msg = true;
      }
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
	
    bool verbose_triggers = false;
    TextTextTbl trigger_vars;
    trigger_vars.Put("VESTA_TOOL", program_name_tail());

    // 
    // Parse command line
    //
    Text sessdir, content, newver, workdir, message, dstrepos, repos;
    bool no_workdir = false, no_sessdir = false;
    bool default_sessdir = true, default_content = true;
    bool default_newver = true, default_workdir = true;
    bool quiet = false, query = false, force = false;
    bool omit_dstrepos = false;
    bool default_repos = true;
    opterr = 0;
    for (;;) {
      int c = getopt(argc, argv, "qQfm:Ms:Sc:Cn:w:Wd:DR:v");
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
      case 's':
	sessdir = optarg;
	default_sessdir = false;
	no_sessdir = false;
	break;
      case 'S':
	sessdir = "NONE";
	default_sessdir = false;
	no_sessdir = true;
	break;
      case 'c':
	content = optarg;
	default_content = false;
	break;
      case 'C':
	content = "-1";
	default_content = false;
	break;
      case 'n':
	newver = optarg;
	default_newver = false;
	break;
      case 'w':
	workdir = optarg;
	default_workdir = false;
	no_workdir = false;
	break;
      case 'W':
	workdir = "NONE";
	default_workdir = false;
	no_workdir = true;
	break;
      case 'd':
	dstrepos = optarg;
	omit_dstrepos = false;
	break;
      case 'D':
	omit_dstrepos = true;
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
	
    switch (argc - optind) {
    case 1:
      if (default_workdir) {
	workdir = argv[optind];
	default_workdir = false;
      }
      break;
    case 0:
      break;
    default:
      usage();
      /* not reached */
    }
	
    trigger_vars.Put("VESTA_QUIET", quiet?"1":"0");
    trigger_vars.Put("VESTA_FORCE", force?"1":"0");

    //
    // Use a nondefault repository if specified
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
    }

    trigger_vars.Put("VESTA_REPOS", lhost+":"+lport);

    //
    // Fill in defaults as needed and look things up.
    //
    if (default_workdir) {
      workdir = ".";
    }
    VestaSource* vs_workdir = NULL;
    VestaSource* vs_workpar = NULL;
    Text cworkdir, cworkpar, workarc;
    if (!no_workdir) {
      cworkdir = ReposUI::canonicalize(workdir, defworkpar + "/" + user);
      ReposUI::split(cworkdir, cworkpar, workarc, "work-dir");
      vs_workdir = ReposUI::filenameToVS(cworkdir, lhost, lport);
      vs_workpar = ReposUI::filenameToVS(cworkpar, lhost, lport);
    }

    trigger_vars.Put("VESTA_WORK_DIR", cworkdir);
    trigger_vars.Put("VESTA_OMIT_WORK", no_workdir?"1":"0");

    if (default_sessdir) {
      if (no_workdir) {
	cerr << program_name <<
	  ": can't default session-dir; " <<
	  "no working directory" << endl;
	exit(2);
      }
      // Get session-dir attribute of workdir
      char* attribval = vs_workdir->getAttrib("session-dir");
      if (attribval == NULL) {
	cerr << program_name <<
	  ": can't default session-dir; " <<
	  "work-dir has no session-dir attribute" << endl;
	exit(2);
      }
      sessdir = attribval;
      delete attribval;
    }
    VestaSource* vs_sessdir = NULL;
    Text csessdir;
    if (!no_sessdir) {
      csessdir = ReposUI::canonicalize(sessdir, defpkgpar);
      vs_sessdir = ReposUI::filenameToVS(csessdir, lhost, lport);
    }

    trigger_vars.Put("VESTA_SESSION_DIR", csessdir);
    trigger_vars.Put("VESTA_OMIT_SESSION_DIR", no_sessdir?"1":"0");

    if (default_content) {
      if (no_sessdir) {
	cerr << program_name <<
	  ": can't default content; " <<
	  "no session directory to search in" << endl;
	exit(2);
      }
      long high = ReposUI::highver(vs_sessdir);
      char buf[64];
      sprintf(buf, "%ld", high);
      content = buf;
    }
    VestaSource* vs_content = NULL;
    Text ccontent;
    if (content != "-1") {
      ccontent = ReposUI::canonicalize(content, csessdir);
      vs_content = ReposUI::filenameToVS(ccontent, lhost, lport);
    }	

    trigger_vars.Put("VESTA_CONTENT", ccontent);
    trigger_vars.Put("VESTA_OMIT_CONTENT", (vs_content == 0)?"1":"0");

    if (default_newver) {
      // Get new-version attribute of session-dir, if any, else
      //  of work-dir, if any.
      char* attribval;
      if (!no_sessdir) {
	attribval = vs_sessdir->getAttrib("new-version");
	if (attribval == NULL) {
	  cerr << program_name <<
	    ": can't default new-version; " <<
	    "session-dir has no new-version attribute" << endl;
	  exit(2);
	}
      } else if (vs_workdir != NULL) {
	attribval = vs_workdir->getAttrib("new-version");
	if (attribval == NULL) {
	  cerr << program_name <<
	    ": can't default new-version; " <<
	    "work-dir has no new-version attribute" << endl;
	  exit(2);
	}
      } else {
	cerr << program_name <<
	  ": can't default new-version; " <<
	  "no session-dir or work-dir to examine" << endl;
	exit(2);
      }
      newver = attribval;
      delete attribval;
    }
    Text cnewver = ReposUI::canonicalize(newver, defpkgpar);
    Text cpkg, newverarc;
    ReposUI::split(cnewver, cpkg, newverarc, "new-version");
    VestaSource* vs_pkg = ReposUI::filenameToVS(cpkg, lhost, lport);
	
    trigger_vars.Put("VESTA_NEW_VERSION", cnewver);

    // Look up newver stub and remember key attributes
    VestaSource *vs_newverstub;
    VestaSource::errorCode err =
      vs_pkg->lookup(newverarc.cchars(), vs_newverstub);
    if (err != VestaSource::ok) {
      cerr << program_name << ": " << cnewver << ": "
	   << ReposUI::errorCodeText(err) << endl;
      exit(2);
    }

    // get stub attributes
    const char* stub_dstrepos = vs_newverstub->getAttrib("checkout-from");
    char* stub_msg = vs_newverstub->getAttrib("message");

    // --------------------------------------------------------------
    // TBD: This could be deleted in a future version once everyone
    // has a version of the repository that preserves attributes when
    // replacing a stub with an immutable directory.
    const char* stub_oldver = vs_newverstub->getAttrib("old-version");
    const char* stub_mstrepos = vs_newverstub->getAttrib("master-repository");
    // --------------------------------------------------------------

    if (dstrepos == "" && stub_dstrepos != NULL) {
      dstrepos = stub_dstrepos;
    }
    Text dhost, dport;
    if (!omit_dstrepos && dstrepos != "") {
      int colon = dstrepos.FindCharR(':');
      if (colon == -1) {
	dhost = dstrepos;
	dport = lport;
      } else {
	dhost = dstrepos.Sub(0, colon);
	dport = dstrepos.Sub(colon+1);
      }
      if ((lhost == dhost) && (lport == dport))
	{
	  dstrepos = "";
	}
      else
	{
	  dstrepos = dhost+":"+dport;
	}
    }

    trigger_vars.Put("VESTA_DEST_REPOS", dstrepos);
    trigger_vars.Put("VESTA_OMIT_DEST_REPOS",
		     (omit_dstrepos || dstrepos == "")?"1":"0");

    // Determine the real value to use for the "old-version" attribute
    // on the version we're about to check in.
    Text new_oldver;
    if (vs_content != 0) {
      // If we have a content version, use ReposUI::prevVersion
      new_oldver = ReposUI::prevVersion(vs_content,
					omit_dstrepos ? "" : dstrepos);
    } else if (!no_sessdir) {
      // No content (maybe -C was used), use the "old-version"
      // attribute from the session directory.
      assert(vs_sessdir != 0);
      const char *sess_oldver = vs_sessdir->getAttrib("old-version");
      if(sess_oldver != 0)
	{
	  new_oldver = sess_oldver;
	  delete [] sess_oldver;
	}
    } else if (stub_oldver != 0) {
      // No session directory either, use the "old-version" from the
      // version reservation stub..
      new_oldver = stub_oldver;
    }

    trigger_vars.Put("VESTA_OLD_VERSION", new_oldver);

    // edit history message
    if(!query && prompt_msg) {
      message = ReposUI::getMessage("change history message",
				    "Checkout description",
				    stub_msg);
      // Save the edited message back on the new version reservation
      // stub.  If we fail after this point, the message will be
      // preserved for the next attempt to check in.
      assert(vs_newverstub != 0);
      vs_newverstub->setAttrib("message", message.cchars());
    }

    trigger_vars.Put("VESTA_MESSAGE", message);
    trigger_vars.Put("VESTA_OMIT_MESSAGE", omit_msg?"1":"0");

    // Sanity checking
    if (!force) {
      // Check types
      if (!no_sessdir && !vs_sessdir->inAttribs("type", "session")) {
	cerr << program_name << ": " << csessdir
	  << " is not a session directory" << endl;
	exit(2);
      }
      if (!no_workdir &&
	  vs_workdir->getAttrib("checkout-time") == NULL) {
	cerr << program_name << ": " << cworkdir
	  << " is not a working directory" << endl;
	exit(2);
      }
      // Check that workdir has not been modified since the
      // last vadvance.  Omit check if the content was specified
      // explicitly; in that case we assume the user knew what
      // he was doing.
      //
      if (default_content && vs_workdir != NULL
	  && ReposUI::changed(vs_workdir, vs_sessdir->timestamp())) {
	cerr << program_name << ": " << cworkdir 
	     << " has been modified since last advance" << endl;
	exit(2);
      }
      // Check that newver is really a reservation stub
      if (vs_newverstub->type != VestaSource::stub ||
	  !vs_newverstub->master ||
	  vs_newverstub->getAttrib("checkout-time") == NULL) {
	cerr << program_name << ": " << cnewver
	     << " is not a reservation stub" << endl;
	exit(2);
      }
      delete vs_newverstub;
    }

    if (!quiet) {
      cout << "Checking in " << cnewver << endl;
      if (!no_workdir) {
	cout << "Deleting " << cworkdir << endl;
      }
    }

    // Make environment variables available for the triggers
    ReposUI::setTriggerVars(trigger_vars,
			    verbose_triggers);

    // Run the "pre" triggers.
    ReposUI::runTriggers("vcheckin pre trigger",
			 verbose_triggers, !query);

    if (!query)
      {
	//
	// Do it, with failure atomicity
	//
	VestaSourceAtomic vsa(lhost, lport);
	VestaSourceAtomic::VSIndex vsi_content = -1,
	  vsi_pkg = -1, vsi_workpar = -1, vsi_newverstub = -1,
	  vsi_sessdir = -1, vsi_workdir = -1;

	// Do all error checking first, because once the atomic program
	// starts making changes, it can't roll back unless the
	// repository crashes.

	vsi_pkg = vsa.decl(cpkg, vs_pkg);
	vsa.typeCheck(cpkg, vsi_pkg, VestaSourceAtomic::
		      typebit(VestaSource::appendableDirectory),
		      VestaSource::inappropriateOp);
	vsa.accessCheck(cpkg, vsi_pkg, AccessControl::write,
			true, VestaSource::noPermission);

	vsi_newverstub = vsa.lookup(cnewver, vsi_pkg, newverarc);
	vsa.typeCheck(cnewver, vsi_newverstub, VestaSourceAtomic::
		      typebit(VestaSource::stub), VestaSource::nameInUse);
	vsa.testMaster(cnewver, vsi_newverstub, true,
		       VestaSource::notMaster);
	vsa.accessCheck(cnewver, vsi_newverstub, AccessControl::write,
			true, VestaSource::noPermission);

	if (vs_content) {
	  vsi_content = vsa.decl(ccontent, vs_content);
	  vsa.typeCheck(ccontent, vsi_content, VestaSourceAtomic::
			typebit(VestaSource::immutableDirectory),
			VestaSource::inappropriateOp);
	  vsa.accessCheck(ccontent, vsi_content, AccessControl::read,
			  true, VestaSource::noPermission);
	}

	if (vs_workpar) {
	  vsi_workpar = vsa.decl(cworkpar, vs_workpar);
	  vsa.typeCheck(cworkpar, vsi_workpar, VestaSourceAtomic::
			typebit(VestaSource::mutableDirectory),
			VestaSource::inappropriateOp);
	  vsa.accessCheck(cworkpar, vsi_workpar, AccessControl::write,
			  true, VestaSource::noPermission);
	  vsi_workdir = vsa.lookup(cworkdir, vsi_workpar, workarc);
	}

	if (!no_sessdir) {
	  vsi_sessdir = vsa.decl(csessdir, vs_sessdir);
	  vsa.accessCheck(csessdir, vsi_sessdir, AccessControl::write,
			  true, VestaSource::noPermission);
	}	  
	
	// All is well if atomic program runs up to this step; start
	// making the changes.

	VestaSourceAtomic::VSIndex vsi_newver = 
	  vsa.insertImmutableDirectory(cnewver, vsi_pkg, newverarc, vsi_content,
				       true, VestaSource::replaceDiff);

	if (!no_workdir) {
	  vsa.reallyDelete(cworkdir, vsi_workpar, workarc, true);
	}

	// Set attributes.
	// We must have write permission on the new version, because
	// we have write permission on the parent, and the new version
	// as yet has no access control attribs of its own.

	// session dir
	if (!no_sessdir) {
	  vsa.setAttrib(csessdir, vsi_sessdir, "checkin-by", cuser);
	  vsa.setAttrib(csessdir, vsi_sessdir, "checkin-time", timebuf);
	  // set the session-dir attrib on the new version
	  vsa.setAttrib(cnewver, vsi_newver, "session-dir", csessdir);
	}

	// new version
	vsa.setAttrib(cnewver, vsi_newver, "checkin-by", cuser);
	vsa.setAttrib(cnewver, vsi_newver, "checkin-time", timebuf);
	if(!omit_msg && !message.Empty()) {
	  vsa.setAttrib(cnewver, vsi_newver, "message", message);
	}
	if (vs_content) {
	  vsa.setAttrib(cnewver, vsi_newver, "content", ccontent);
	}
	// clear some attribs set at checkout
	vsa.clearAttrib(cnewver, vsi_newver, "work-dir");
	vsa.clearAttrib(cnewver, vsi_newver, "#mode");

	vsa.setAttrib(cnewver, vsi_newver, "old-version", new_oldver);
	if (stub_mstrepos) {
	  vsa.setAttrib(cnewver, vsi_newver, "master-repository", stub_mstrepos);
	}
	// --------------------------------------------------------------

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

	// Replicate if needed
	if (!omit_dstrepos && dstrepos != "") {
	  assert(lhost != dhost || lport != dport);
	  if (!quiet) {
	    cout << "Replicating " << cnewver
		 << " to " << dhost << ":" << dport << endl;
	  }
	  Replicator repl(lhost, lport, dhost, dport);
	  Replicator::DirectiveSeq direcs;
	  Replicator::Directive d('+', cnewver.Sub(vlen));
	  direcs.addhi(d);
	  repl.replicate(&direcs, rflags);
	}
      }

    // Run the "post" triggers
    ReposUI::runTriggers("vcheckin post trigger",
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
