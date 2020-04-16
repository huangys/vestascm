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
// vmaster.C
//
// Last modified on Wed Dec 21 12:00:47 EST 2005 by irina.furman@intel.com
//      modified on Thu Aug  5 16:40:27 EDT 2004 by ken@xorian.net
//      modified on Mon Apr 25 19:32:16 EDT 2005 by ken@xorian.net
//      modified on Wed Nov 15 13:04:55 PST 2000 by mann
//

// Simple program to invoke VDirSurrogate::acquireMastership

#include <Basics.H>
#include <Text.H>
#include <ReposUI.H>
#include <VDirSurrogate.H>
#include "Replicator.H"

using std::cerr;
using std::endl;

Text program_name;

void
usage()
{
  cerr << "Usage: " << program_name <<
    " [-L] [-s srcrepos] [-d dstrepos] [-h hints] pathname ..." << endl;
  exit(1);
}

void
exitIfVSError(Text name, VestaSource::errorCode err,
	      VestaSource* droot =NULL, AccessControl::Identity dwho =NULL)
     throw (SRPC::failure)
{
  if (err == VestaSource::ok) return;
  cerr << program_name << ": " << name << ": "
       << ReposUI::errorCodeText(err) << endl;
  if (err == VestaSource::rpcFailure && droot != NULL) {
    VestaSource* vs;
    Text path;
    try {
      path = ReposUI::stripSpecificRoot(name.cchars(), ReposUI::VESTA);
    }
    catch(ReposUI::failure f) {
      cerr << program_name << ": " << f.msg << endl;
      exit(2);
    }
    catch(VestaConfig::failure f) {
      cerr << program_name << ": " << f.msg << endl;
      exit(2);
    }
      
    VestaSource::errorCode err2 = 
      droot->lookupPathname(path.cchars(), vs, dwho);
    if (err2 != VestaSource::ok) {
      cerr << "Error getting transfer status: "
	   << ReposUI::errorCodeText(err2) << endl;
    } else {
      const char* mg = vs->getAttrib("#master-request");
      if (mg != NULL) {
	cerr << "Note: transfer is still in progress; may finish later"
	     << endl;
      } else {
	if (vs->master) {
	  cerr << "Note: transfer was recovered and completed (!?)" << endl;
	} else {
	  cerr << "Note: transfer was not started" << endl;
	}
      }
      delete vs;
    }
  }
  exit(2);
}

void
parserepos(/*IN*/const char* arg, /*OUT*/Text& host, /*OUT*/Text& port,
	   /*OUT*/AccessControl::Identity& who)
{
  // Establish defaults
  host = VestaConfig::get_Text("Repository", "VestaSourceSRPC_host");
  port = VestaConfig::get_Text("Repository", "VestaSourceSRPC_port");
  who = NULL;

  if (strcmp(arg, "local") == 0) return;

  const char *colon = strchr(arg, ':');
  if (colon == NULL) {
    host = arg;
    return;
  }
  host = Text(arg, colon - arg);
  
  if (host.FindChar('.') == -1) {
    // Attempt to fully qualify the name
    struct hostent *he = gethostbyname(host.cchars());
    if (he != NULL) host = Text(he->h_name);
  }

  const char *colon2 = strchr(colon + 1, ':');
  if (colon2 == NULL) {
    port = Text(colon + 1);
    return;
  }
  int portlen = colon2 - (colon + 1);
  if (portlen > 0) {
    port = Text(colon + 1, portlen);
  }

  who = NEW_CONSTR(AccessControl::GlobalIdentityRep, (strdup(colon2 + 1)));
}

// Wrapper around VDirSurrogate::acquireMastership that does a more
// thorough search for the current master when shost:sport is omitted.
VestaSource::errorCode
acquireMastership(const char* pathname, Text dhost, Text dport,
		  Text shost, Text sport, char sep,
		  AccessControl::Identity dwho,
		  AccessControl::Identity swho, Text hints)
{
  if (shost == "") {
    VestaSource* vs =
      ReposUI::filenameToMasterVS(Text("/vesta/") + pathname, hints);
    shost = vs->host();
    sport = vs->port();
    delete vs;
  }
  return VDirSurrogate::acquireMastership(pathname, dhost, dport,
					  shost, sport, sep, dwho, swho);
}


// Support for recursion
struct RecurClosure {
  VestaSource* vs;
  Text vsname, dhost, dport, shost, sport, hints;
  AccessControl::Identity dwho, swho;
  VestaSource* droot;
};

bool
recurCallback(void* closure, VestaSource::typeTag type, Arc arc,
	      unsigned int index, Bit32 pseudoInode, ShortId filesid,
	      bool master)
{
  RecurClosure* cl = (RecurClosure*) closure;
  Text childname = cl->vsname + "/" + arc;
  VestaSource::errorCode err;
  if (type == VestaSource::appendableDirectory) {
    // Recurse further
    RecurClosure cl2 = *cl;
    cl2.vsname = childname;
    err = cl->vs->lookupIndex(index, cl2.vs);
    exitIfVSError(childname, err);
    err = cl2.vs->list(0, recurCallback, &cl2, cl->dwho);
    exitIfVSError(childname, err);
    delete cl2.vs;
  }
  // Do parent after children
  if (!master) {
    err = acquireMastership(childname.cchars(),
			    cl->dhost, cl->dport, cl->shost, cl->sport,
			    '/', cl->dwho, cl->swho, cl->hints);
    exitIfVSError(childname, err, cl->droot, cl->dwho);
  }
  return true;
}

int
main(int argc, char* argv[])
{
  Text shost, sport, dhost, dport, hints;
  AccessControl::Identity swho = NULL, dwho = NULL;
  const char *src = "", *dst = "local";
  bool recursive = false;

  program_name = argv[0];

  try {
    opterr = 0;
    for (;;) {
      int c = getopt(argc, argv, "s:d:rh:");
      if (c == EOF) break;
      switch (c) {
      case 's':
	src = optarg;
	break;
      case 'd':
	dst = optarg;
	break;
      case 'r':
	recursive = true;
	break;
      case 'h':
	hints = optarg;
	break;
      case '?':
      default:
	usage();
	// not reached
	break;
      }
    }

    parserepos(src, shost, sport, swho);
    parserepos(dst, dhost, dport, dwho);

    VestaSource* droot = VDirSurrogate::repositoryRoot(dhost, dport);

    while (optind < argc) {
      // !!Maybe optionally call the replicator here to be sure we
      // have a copy, at least of the object itself.
      // Maybe +pathname -pathname/*

      Text pathname = ReposUI::stripSpecificRoot(argv[optind], ReposUI::VESTA);

      // Do recursion in postorder, to maximize the number of
      // master-repository attributes that are inherited rather than
      // explicitly set.
      if (recursive) {
	VestaSource* vs;
	VestaSource::errorCode err =
	  droot->lookupPathname(pathname.cchars(), vs, dwho);
	exitIfVSError(argv[optind], err);
	RecurClosure cl;
	cl.vs = vs;
	cl.vsname = pathname;
	cl.dhost = dhost;
	cl.dport = dport;
	cl.shost = shost;
	cl.sport = sport;
	cl.hints = hints;
	cl.dwho = dwho;
	cl.swho = swho;
	cl.droot = droot;
	err = vs->list(0, recurCallback, &cl, dwho);
	exitIfVSError(argv[optind], err);
      }

      VestaSource::errorCode err =
	acquireMastership(pathname.cchars(), dhost, dport, shost, sport,
			  '/', dwho, swho, hints);
      exitIfVSError(argv[optind], err, droot, dwho);

      optind++;
    }
  } catch (ReposUI::failure f) {
    cerr << program_name << ": " << f.msg << endl;
    exit(2);
  } catch (SRPC::failure f) {
    cerr << program_name
	 << ": SRPC::failure " << f.msg << " (" << f.r << ")" << endl;
    if (f.r == SRPC::transport_failure ||
	f.r == SRPC::partner_went_away) {
      cerr << "Note: transfer might have been started" << endl;
    }
    exit(2);
  } catch (VestaConfig::failure f) {
    cerr << program_name << ": " << f.msg << endl;
    exit(2);
  }
  catch(Replicator::Failure f) {
    cerr << program_name << ": " << f.msg << endl;;
    exit(2);
  }
  return 0;
}
