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
// vadvance.C
//

// Advance to the next version in a Vesta checkout session.
// See vadvance.1.mtex for documentation.

#include <Basics.H>
#include <Text.H>
#include <VestaConfig.H>
#include <VestaSource.H>
#include <VDirSurrogate.H>
#include <VestaSourceAtomic.H>
#include "ReposUI.H"
#include <Units.H>

#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif

using std::cout;
using std::cerr;
using std::endl;

Text program_name;
const unsigned long MAXUL = (0UL - 1UL);
const Text default_junk = "*~ .*~ core";
const unsigned long default_maxsize = 1024*1024;

void
usage()
{
  cerr << "Usage: " << program_name << endl <<
    "        [-q] [-Q] [-v] [-f]" << endl <<
    "        [-F fp-content]" << endl <<
    "        [-j junk | -J]" << endl <<
    "        [-z maxsize | -Z]" << endl <<
    "        [-s session-dir]" << endl <<
    "        [-a session-ver-arc]" << endl <<
    "        [-R repos]" << endl <<
    "        [ [-w] work-dir]" << endl << endl;
  exit(1);
}

Text program_name_tail() throw()
{
  int last_slash = program_name.FindCharR('/');
  if(last_slash == -1)
    return program_name;
  return program_name.Sub(last_slash+1);
}

void fix_work_dir_attrib(const Text &cworkdir, const Text &ctarget,
			 VestaSource *vs_target)
{
  // Get the current value of work-dir on the target
  char *workdir_val = vs_target->getAttrib("work-dir");
  // If it's unset or has a value other than cworkdir, set it to
  // cworkdir
  if((workdir_val == 0) || (cworkdir != workdir_val))
    {
      VestaSource::errorCode err =
	vs_target->setAttrib("work-dir", cworkdir.cchars());
      if(err != VestaSource::ok)
	{
	  // Print a warning if we fail to set the attribute.
	  cerr << program_name 
	       << ": WARNING: the 'work-dir' attribute on " << ctarget
	       << " seems incorrect (maybe you renamed ";
	  if(workdir_val != 0)
	    cerr << workdir_val << " to ";
	  cerr << cworkdir << "?) and correcting it failed: "
	       << ReposUI::errorCodeText(err) << endl;
	}
    }
  // If the attribute was non-empty, free its storage.
  if(workdir_val != 0)
    delete [] workdir_val;
}

void fix_work_dir_attrib(const Text &cworkdir, const Text &ctarget,
			 const Text &lhost, const Text &lport)
{
  VestaSource *vs_target;
  try
    {
      vs_target = ReposUI::filenameToVS(ctarget, lhost, lport);
    }
  catch(ReposUI::failure)
    {
      // Maybe we're trying to chase a new-version attribute that's
      // invalid (points to something non-existent)?  Silently ignore
      // such an error.
      return;
    }
  fix_work_dir_attrib(cworkdir, ctarget, vs_target);
  delete vs_target;
}

static unsigned int fp_content;

static bool
gradual_make_immutable_callback(void* closure, VestaSource::typeTag type,
				Arc arc, unsigned int index, Bit32 pseudoInode,
				ShortId filesid, bool master)
{
  VestaSource *parent = (VestaSource *) closure;
  VestaSource* vs;

  // We can skip anything that's not mutable
  if((type != VestaSource::mutableDirectory) &&
     (type != VestaSource::mutableFile))
    return true;

  VestaSource::errorCode err = parent->lookupIndex(index, vs);

  if(err != VestaSource::ok)
    return true;

  switch (type)
    {
    case VestaSource::mutableDirectory:
      err = vs->list(0, gradual_make_immutable_callback, (void *) vs,
		     /*who=*/ 0, true);
      break;
    case VestaSource::mutableFile:
      err = vs->makeFilesImmutable(fp_content);
      break;
    }
  delete vs;
  return true;
}

static VestaSource::errorCode gradual_make_immutable(VestaSource* vs)
{
  sync();
  VestaSource::errorCode err =
    vs->list(0, gradual_make_immutable_callback, (void *) vs,
	     /*who=*/ 0, true);

  // The final call to makeFilesImmutable shouldn't need to do
  // anything unless some writes reached the server while we were
  // making the files immutable.
  return vs->makeFilesImmutable(fp_content);
}

int
main(int argc, char* argv[])
{
  program_name = argv[0];
  try {
    //
    // Read config file
    //
    Text defpkgpar, defworkpar;
    defpkgpar = VestaConfig::get_Text("UserInterface",
				      "DefaultPackageParent");
    defworkpar = VestaConfig::get_Text("UserInterface",
				       "DefaultWorkParent");
    Text lhost(VDirSurrogate::defaultHost());
    Text lport(VDirSurrogate::defaultPort());
    time_t now = time(NULL);
    Text cuser(AccessControl::self()->user());
    Text user(cuser.Sub(0, cuser.FindChar('@')));
    Text workdir, sessdir, sessverarc, repos;
    bool default_workdir = true, default_sessdir = true;
    bool default_sessverarc = true, default_repos = true;
    bool quiet = false, query = false, force = false;
    bool test = false;
    bool verbose_triggers = false;
    int c;
    VestaSource::errorCode err;
    fp_content = VestaConfig::get_int("UserInterface",
				      "FpContent");
    Text junk = default_junk;
    VestaConfig::get("UserInterface", "vadvance_junk", junk);
    unsigned long maxsize = default_maxsize;
    Text t;
    if (VestaConfig::get("UserInterface", "vadvance_maxsize", t)) {
      try
	{
	  maxsize = Basics::ParseUnitVal(t);
	}
      catch(Basics::ParseUnitValFailure f)
	{
	  cerr << program_name << ": Error parsing [UserInterface]vadvance_maxsize setting: " << f.emsg << endl
	       << "\tUsing default value (" + Basics::FormatUnitVal(maxsize) + ")" << endl;
	}
    }

    TextTextTbl trigger_vars;
    trigger_vars.Put("VESTA_TOOL", program_name_tail());

    // 
    // Parse command line
    //
    opterr = 0;
    for (;;) {
      c = getopt(argc, argv, "tqQfF:j:Jz:Zw:s:a:R:v");
      if (c == EOF) break;
      switch (c) {
      case 't':
	test = true;
	query = true;
	break;
      case 'q':
	quiet = true;
	break;
      case 'Q':
	query = true;
	break;
      case 'f':
	force = true;
	break;
      case 'F':
	fp_content = strtol(optarg, 0, 0);
	break;
      case 'j':
	junk = optarg;
	break;
      case 'J':
	junk = "";
	break;
      case 'z':
	maxsize = Basics::ParseUnitVal(optarg);
	break;
      case 'Z':
	maxsize = MAXUL;
	break;
      case 'w':
	workdir = optarg;
	default_workdir = false;
	break;
      case 's':
	sessdir = optarg;
	default_sessdir = false;
	break;
      case 'a':
	sessverarc = optarg;
	default_sessverarc = false;
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

    trigger_vars.Put("VESTA_JUNK", junk);
    {
      char buf[30];
      sprintf(buf, "%lu", maxsize);
      trigger_vars.Put("VESTA_MAXSIZE", buf);
    }
    {
      char buf[15];
      sprintf(buf, "%d", fp_content);
      trigger_vars.Put("VESTA_FP_CONTENT", buf);
    }
    trigger_vars.Put("VESTA_QUIET", quiet?"1":"0");
    trigger_vars.Put("VESTA_FORCE", force?"1":"0");

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
    // Canonicalize names, fill in defaults, and map names to
    // VestaSource objects as needed.
    //
    if (default_workdir) {
      workdir = ".";
    }
    Text cworkdir = ReposUI::canonicalize(workdir, defworkpar + "/" + user);
    VestaSource* vs_workdir = ReposUI::filenameToVS(cworkdir, lhost, lport);

    if(default_sessdir)
      {
	// Defaults to the "session-dir" attribute of work-dir

	// When defaulting session directory, search upwards for the
	// closest directory with a "session-dir" attribute.  This
	// facilitates using vadvance in subdirectories of working
	// directories, but may change the directory of which we take
	// a snapshot.
	Text cstartdir = cworkdir;
	char* attribval;
	while((attribval = vs_workdir->getAttrib("session-dir")) == 0)
	  {
	    VestaSource *vs_parent = vs_workdir->getParent();
	    if((vs_parent == 0) ||
	       (vs_parent->longid == MutableRootLongId))
	      {
		cerr << program_name << ": " << cstartdir
		     << " is not a session working directory (or a subdirectory of one)" << endl;
		exit(2);
	      }
	    delete vs_workdir;
	    vs_workdir = vs_parent;
	    Text new_workdir, unused_tail;
	    ReposUI::split(cworkdir, new_workdir, unused_tail,
			   Text::printf("work-dir (%s)", cworkdir.cchars()));
	    cworkdir = new_workdir;
	  }
	// Save the attribute value
	sessdir = attribval;
	delete [] attribval;
      }

    trigger_vars.Put("VESTA_WORK_DIR", cworkdir);

    Text csessdir;
    if (default_sessdir) {
      csessdir = ReposUI::canonicalize(sessdir); // just in case
    } else {
      csessdir = ReposUI::canonicalize(sessdir, defpkgpar);
    }
    VestaSource* vs_sessdir = ReposUI::filenameToVS(csessdir, lhost, lport);

    trigger_vars.Put("VESTA_SESSION_DIR", csessdir);
	
    // Try to find previous version in session
    Text prev_sessverarc;
    long high = ReposUI::highver(vs_sessdir);
    if (high < 0) high = 0;  // skip 0; reserved for base version
    char buf[64];
    sprintf(buf, "%ld", high);
    prev_sessverarc = buf;
    if (default_sessverarc) {
      // Defaults to 1 + previous version in session 
      char buf[64];
      sprintf(buf, "%ld", high + 1);
      sessverarc = buf;
    }
    Text csessver = csessdir + "/" + sessverarc;

    trigger_vars.Put("VESTA_SESSION_VER_ARC", sessverarc);
    trigger_vars.Put("VESTA_SESSION_VER", csessver);
    trigger_vars.Put("VESTA_PREV_SESSION_VER_ARC", prev_sessverarc);
    trigger_vars.Put("VESTA_PREV_SESSION_VER", csessdir+"/"+prev_sessverarc);
	
    // Sanity checking and cleanup
    if (!force) {
      if (!vs_sessdir->inAttribs("type", "session")) {
	cerr << program_name << ": " << csessdir
	  << " is not a session directory" << endl;
	exit(2);
      }
    }

    // Make environment variables available for the triggers
    ReposUI::setTriggerVars(trigger_vars,
			    verbose_triggers);

    // Run the "pre" triggers.
    ReposUI::runTriggers("vadvance pre trigger",
			 verbose_triggers, !query);

    if (!query && (junk != "" || maxsize != MAXUL)) {
      ReposUI::cleanup(vs_workdir, junk, maxsize, cworkdir);
    }

    if(!query)
      {
	// If the user happened to rename their working direcotry, fix
	// up the work-dir attributes on the session directory and
	// reservation stub (if any).

	fix_work_dir_attrib(cworkdir, csessdir, vs_sessdir);
	char *cnewver = vs_workdir->getAttrib("new-version");
	if(cnewver != 0)
	  {
	    fix_work_dir_attrib(cworkdir, cnewver, lhost,lport);
	    delete [] cnewver;
	  }
      }

    if (!force) {
      // Use the modified time of the session directory as the
      // time of last vadvance.
      if (!ReposUI::changed(vs_workdir, vs_sessdir->timestamp())) {
	if (!quiet) {
	  cout << "Unchanged from "
	       << csessdir << "/" << prev_sessverarc << endl;
	}
	exit(0);
      }
    }

    if (!quiet) {
      cout << "Advancing to " << csessver << endl;
    }

    if(!query)
      {
	if (fp_content) {
	  // Fingerprint content of files below size fp_content
	  err = gradual_make_immutable(vs_workdir);
	  if (err != VestaSource::ok) {
	    cerr << program_name 
		 << ": error in makeFilesImmutable: "
		 << ReposUI::errorCodeText(err) << endl;
	    exit(2);
	  }
	}

	//
	// Do the advance, with failure atomicity
	//
	VestaSourceAtomic vsa(lhost, lport);

	// Declare session directory and make sure it is master
	VestaSourceAtomic::VSIndex vsi_sessdir = vsa.decl(csessdir,
							  vs_sessdir);
	vsa.testMaster(csessdir, vsi_sessdir, true, VestaSource::notMaster);

	// Declare workdir and check that we have write permission
	// so that attribs can be set.
	VestaSourceAtomic::VSIndex vsi_workdir = vsa.decl(cworkdir,
							  vs_workdir);
	vsa.accessCheck(cworkdir, vsi_workdir, AccessControl::write,
			true, VestaSource::noPermission);

	// All is well; we can start making changes.

	// Insert snapshot of workdir into sessdir
	vsa.insertImmutableDirectory(csessver, vsi_sessdir, sessverarc,
				     vsi_workdir, true,
				     VestaSource::dontReplace);
				     
	// Set attribs
	vsa.setAttrib(cworkdir, vsi_workdir, "session-ver-arc", sessverarc);

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
      }

    // Run the "post" triggers
    ReposUI::runTriggers("vadvance post trigger",
			 verbose_triggers, !query);

    if (query) {
      if (!quiet) {
	 cerr << program_name << ": nothing done (-Q or -t flag given)"
	      << endl;
      }
      if (test) {
         exit(255);
      } else {
	 exit(0);
      }
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
    if(f.msg.FindText("max file size") > 0)
      {
	cerr << "\tUse '-z maxsize' to override or '-Z' to disable" << endl;
      }
    exit(2);
  } catch(Basics::ParseUnitValFailure f) {
    cerr << program_name << ": " << f.emsg << endl;
    exit(2);
  }
    
  return 0;
}
