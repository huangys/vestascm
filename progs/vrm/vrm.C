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
// vrm.C
//

// Remove a file or directory from the Vesta repository.  Removing an
// object under the appendable root leaves a ghost behind.  Provides
// an alternative to the NFS interface on platforms with obstinate NFS
// client code (e.g. some of the Linux 2.4 series).

// See documentation in vrm.1.mtex

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
using std::cerr;
using std::endl;

Text program_name;

void usage()
{
  cerr << "Usage: " << program_name << " [-q] [-Q] [-v] [-R repos] name" << endl << endl;
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

    Text host(VDirSurrogate::defaultHost());
    Text port(VDirSurrogate::defaultPort());
    const int vlen = 7; // ==strlen("/vesta/")

    bool verbose_triggers = false;
    TextTextTbl trigger_vars;
    trigger_vars.Put("VESTA_TOOL", program_name_tail());

    //
    // Parse command line
    //
    Text name;
    bool query = false;
    bool quiet = false;

    opterr = 0;
    for (;;) {
      int c = getopt(argc, argv, "R:qQv");
      if (c == EOF) break;
      switch (c) {
      case 'R':
	repos = optarg;
	default_repos = false;
	break;
      case 'q':
	quiet = true;
	break;
      case 'Q':
	query = true;
	break;
      case 'v':
	verbose_triggers = true;
	break;
      case '?':
      default:
	usage();
      }
    }
    if (argc <= optind) {
      usage();
    }

    for (int num=optind; num < argc; num++) {

      trigger_vars.Put("VESTA_QUIET", quiet?"1":"0");
   
      name = Text(argv[num]);
   
      //
      // Use a nondefault repository if specified
      //
      if (!default_repos) {
        int colon = repos.FindCharR(':');
        if (colon == -1) {
          host = repos;
          repos = repos + ":" + port;
        } else {
          host = repos.Sub(0, colon);
          port = repos.Sub(colon+1);
        }
      }
   
      trigger_vars.Put("VESTA_REPOS", host+":"+port);
   
      //
      // Qualify and canonicalize name
      //
      Text cname, cparent, newpart;
      cname = ReposUI::canonicalize(name);
      ReposUI::split(cname, cparent, newpart, "name");
   
      trigger_vars.Put("VESTA_PATH", cname);
   
      //
      // Look up parent
      //
      VestaSource* vs_parent =
        ReposUI::filenameToVS(cparent, host, port);
   
      if (!quiet) {
        cout << "Deleting " << cname << endl;
      }
   
      // Make environment variables available for the triggers
      ReposUI::setTriggerVars(trigger_vars,
          		    verbose_triggers);
   
      // Run the "pre" triggers.
      ReposUI::runTriggers("vrm pre trigger",
          		 verbose_triggers, !query);
   
      if (!query)
        {
          //
          // Do the deed
          //
          VestaSource::errorCode err;
          switch (vs_parent->type) {
          case VestaSource::immutableDirectory:
            {
              // Try to do copy-on-write
              VestaSource* newvs;
              err = vs_parent->makeMutable(newvs);
              if (err != VestaSource::ok) {
                goto finish;
              }
              delete vs_parent;
              vs_parent = newvs;
            }
            // fall through
   
          case VestaSource::mutableDirectory:
            // Do the deletion
            err = vs_parent->reallyDelete(newpart.cchars(), 0, true);
            break;
          
          case VestaSource::appendableDirectory:
            // Check that this isn't already a ghost
            VestaSource* newvs;
            err = vs_parent->lookup(newpart.cchars(), newvs);
            if (err != VestaSource::ok) {
              goto finish;
            }
            if (newvs->type == VestaSource::ghost) {
              delete newvs;
              err = VRErrorCode::noPermission;
              goto finish;
            }
            // Replace with a ghost
            VestaSource* ghostvs;
            err = vs_parent->insertGhost(newpart.cchars(), newvs->master, 0,
          			       VestaSource::replaceDiff, &ghostvs);
            delete newvs;
            break;
          
          default:
            err = VRErrorCode::notADirectory;
            goto finish;
          }
   
        finish:
          if (err != VestaSource::ok) {
            cerr << program_name << ": " << name << ": "
                 << ReposUI::errorCodeText(err) << endl;
            exit(2);
          }
        }
      if (vs_parent != NULL) delete vs_parent;
   
      // Run the "post" triggers
      ReposUI::runTriggers("vrm post trigger",
          		 verbose_triggers, !query);
   
      if(query)
        {
          if(!quiet)
            cerr << program_name << ": nothing done (-Q flag given)" << endl;
          exit(0);
        }
    } // loop over names

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
