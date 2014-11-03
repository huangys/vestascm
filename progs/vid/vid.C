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
// vid.C
// Last modified on Sun Jun  5 21:49:51 EDT 2005 by ken@xorian.net
//      modified on Wed Jul 11 22:03:49 PDT 2001 by mann
//
// Get information about a user from the repository.
//

#include <Basics.H>
#include <Text.H>
#include <VestaConfig.H>
#include <VestaSource.H>
#include <VDirSurrogate.H>
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
Usage()
{
    cerr << "Usage: " << program_name <<
      " [-R repos] [user]" << endl << endl;
    exit(3);
}

int
main(int argc, char* argv[])
{
    try {
	program_name = argv[0];
	Text repos;

	int c;
	for (;;) {
	    c = getopt(argc, argv, "h?R:");
	    if (c == EOF) break;
	    switch (c) {
	      case 'R':	repos = optarg;	break;
	      case 'h':
	      case '?':
	      default:
		Usage();
	    }
	}

	Text lhost(VDirSurrogate::defaultHost());
	Text lport(VDirSurrogate::defaultPort());
	bool default_repos = true;
	if (repos != "") {
	  default_repos = false;
	  int colon = repos.FindCharR(':');
	  if (colon == -1) {
	    lhost = repos;
	    repos = repos + ":" + lport;
	  } else {
	    lhost = repos.Sub(0, colon);
	    lport = repos.Sub(colon+1);
	  }
	}

	Text subject;
	AccessControl::Identity subject_ident = 0;
	if (optind < argc) {
	    subject = argv[optind];
	    if(subject.FindChar('@') == -1)
	      {
		subject += "@";
		subject += AccessControl::realm;
	      }
	    subject_ident =
	      NEW_CONSTR(AccessControl::GlobalIdentityRep, (subject.cchars()));
	}

	AccessControl::IdInfo result;

	// Note that we always pass NULL for "who", so there's no way
	// to override the user who's requesting access.
	VDirSurrogate::getUserInfo(result, NULL, subject_ident, lhost, lport);

	cout << "User names and aliases:" << endl;
	for(unsigned int i = 0; i < result.names.length(); i++)
	  {
	    cout << "  " << result.names[i] << endl;
	  }

	cout << "Groups:" << endl;
	for(unsigned int i = 0; i < result.groups.length(); i++)
	  {
	    cout << "  " << result.groups[i] << endl;
	  }

	cout << "Unix (NFS) user ID:          " << result.unix_uid << endl;

	// If we're inquiring about ourselves at the local repository,
	// make sure our UNIX user ID matches what the repository has.
	// If not, print a warning.
	if((subject_ident == 0) && default_repos)
	  {
	    uid_t
	      local_uid = getuid(),
	      local_euid = geteuid();
	    if((result.unix_uid != local_uid) &&
	       (result.unix_uid != local_euid))
	      {
		cerr << endl
		     << "WARNING: Your local Unix user ID ("
		     << local_uid << ") doesn't match this." << endl
		     << ("         This indicates that your repository "
			 "server and this")
		     << endl
		     << ("         machine don't share Unix user tables, "
			 "which could be a")
		     << endl
		     << "         serious problem."
		     << endl << endl;
	      }
	  }
	cout << "Unix (NFS) primary group ID: " << result.unix_gid << endl;

	if(result.is_root || result.is_admin ||
	   result.is_wizard || result.is_runtool)
	  {
	    cout << "Special permissions:" << endl;
	    if(result.is_root)
	      {
		cout << "  root" << endl;
	      }
	    if(result.is_admin)
	      {
		cout << "  admin" << endl;
	      }
	    if(result.is_wizard)
	      {
		cout << "  wizard" << endl;
	      }
	    if(result.is_runtool)
	      {
		cout << "  runtool" << endl;
	      }
	  }

    } catch (VestaConfig::failure f) {
	cerr << program_name << ": " << f.msg << endl;
	exit(1);
    } catch (SRPC::failure f) {
	cerr << program_name
	  << ": SRPC failure; " << f.msg << " (" << f.r << ")" << endl;
	exit(2);
    }

    return 0;
}

