// Copyright (C) 2005, Vesta Free Software Project
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

// vcheckagreement.C

// Check for violations of the replication agreement invariant.

// See documentation in vcheckagreement.1.mtex

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

#define BUFSIZE 4096

using std::cout;
using std::cerr;
using std::endl;

Text program_name;
Text timefmt;

void
usage()
{
  cerr << "Usage: " << program_name << endl <<
    "        [-l level] [-s source] repos1 [repos2]" << endl << endl;
  exit(2);
}


void print_error(VestaSource* vs, Text msg) 
  throw (ReposUI::failure, VestaConfig::failure, SRPC::failure)
{
  Text source = ReposUI::vsToFilename(vs);
  cerr << program_name << ": Error: " << source << " " << msg << endl;
}


// functions declarations
bool check_agreement(VestaSource* vs1, VestaSource* vs2, 
		     const int level)
  throw(ReposUI::failure, VestaConfig::failure, SRPC::failure);

bool compare_files_properties(VestaSource* vs1, VestaSource* vs2)
  throw(ReposUI::failure, VestaConfig::failure, SRPC::failure);

bool compare_files_contents(VestaSource* vs1, VestaSource* vs2)
  throw(ReposUI::failure, VestaConfig::failure, SRPC::failure);

bool check_delta(VestaSource* vs1, VestaSource* vs2) 
  throw (SRPC::failure);

int main(int argc, char* argv[])
{
  program_name = argv[0];
  bool in_agreement = true;
  
  try {

    // 
    // Parse command line
    //
    int level = 0;
    opterr = 0;
    for (;;) {
      int c = getopt(argc, argv, "l:");
      if (c == EOF) break;
      switch (c) {
      case 'l':
       	level = atoi(optarg);
	if(level > 2)
	  level = 2;
	if(level < 0)
	  level = 0;
	break;
      case '?':
      default:
	usage();
      }
    }

    // There must be at least one more argument (the other repository
    // to check against).
    if(argc < optind + 1) 
      usage();

    TextSeq paths;

    Text default_port = VDirSurrogate::defaultPort();

    // The next argument must be a repositiory address

    Text host1, port1;
    Text repos1 = Text(argv[optind]);
    int colon = repos1.FindCharR(':');
    if (colon == -1) {
      host1 = repos1;
      port1 = default_port; 
    } else {
      host1 = repos1.Sub(0, colon);
      port1 = repos1.Sub(colon+1);
    }

    // The following argument might be a repository address, or it
    // might be a path in the repository

    Text host2, port2;
    if(argc >= optind + 2) {
      Text repos_or_path(argv[optind+1]);
      if(repos_or_path.FindChar('/') == -1)
	{
	  // This argument doesn't contain a slash, so we assume it's
	  // a repository address

	  Text repos2 = Text(argv[optind+1]);
	  colon = repos2.FindCharR(':');
	  if (colon == -1) {
	    host2 = repos2;
	    port2 = default_port; 
	  } else {
	    host2 = repos2.Sub(0, colon);
	    port2 = repos2.Sub(colon+1);
	  }
	}
      else
	{
	  Text csource = ReposUI::canonicalize(argv[optind+1]);
	  paths.addhi(csource);

	  // If we parsed this argument as a path, then the second
	  // repository is the local one.
	  host2 = VDirSurrogate::defaultHost();
	  port2 = default_port;
	}
    }
    else {
      // No additional arguments, so the second repository is the
      // local one.
      host2 = VDirSurrogate::defaultHost();
      port2 = default_port;
    }

    // Any additional arguments must be paths.
    for(unsigned int i = optind + 2; i < argc; i++)
      {
	Text csource = ReposUI::canonicalize(argv[i]);
	paths.addhi(csource);
      }

    // If no paths have been provided, default to checking agreement
    // throughout the entire repository.
    if(paths.size() == 0)
      {
	paths.addhi("/vesta");
      }

    // Loop over all the paths, comparing them for agreement
    while(paths.size() > 0)
      {
	Text csource = paths.remlo();

	VestaSource* vs_root1 = NULL;
	VestaSource* vs_root2 = NULL;
	
	// get the roots
	vs_root1 = ReposUI::filenameToVS(csource, host1, port1);
	vs_root2 = ReposUI::filenameToVS(csource, host2, port2);

	in_agreement = check_agreement(vs_root1, vs_root2, level) && in_agreement;

	delete vs_root1;
	delete vs_root2;
      }

    if(in_agreement) {
      cout << "true" << endl;
      exit(0);
    }
    else {
      cout << "false" << endl;
      exit(1);
    }

  } 
  catch(VestaConfig::failure f) {
    cerr << program_name << ": " << f.msg << endl;
    exit(2);
  }
  catch(SRPC::failure f) {
    cerr << program_name
	 << ": SRPC failure; " << f.msg << " (" << f.r << ")" << endl;
    exit(2);
  }
  catch(ReposUI::failure f) {
    cerr << program_name << ": " << f.msg << endl;
    exit(2);
  }
}


struct Pair
{
  VestaSource* vs1;
  VestaSource* vs2;
};

// The structure is used by get_child_pairs function for filling in a list 
// of PAIRS of corresponding children Appendable or Immutable directories 
// on repos1 and repos2.
//
// For every child of VS_ALL get_child_pairs tries to find a corresponding 
// child of VS_SOME. According to repository agreement rules it is decided 
// whether VS_ALL is a source on repos1 or repos2. In a case when VS_ALL is 
// on repos2 the REVERSE_ORDER should be set to make sure that we check 
// agreement between repos1 and repos2 at every level.
//
// In a case of immutable directories after filling in the PAIRS list we 
// check there is no name that is on one side and not on the other by  
// calling get_child_pairs again with REVERSE_ORDER and CHECK_PAIRS parameters
// set to true. CHECK_PAIRS prevents filling in the PAIRS list again.
//
// The NOT_ALL parameter should be set if not for every VS_ALL child 
// a corresponding VS_SOME child should be found like when neither VS_ALL nor 
// VS_SOME is master and they could have different names subsets. 
struct PairClosure
{
  Table<Text, Pair>::Default* pairs;
  VestaSource* vs_all;
  VestaSource* vs_some;
  bool reverse_order;  // true if vs_all on repos2    
  bool not_all;        // true if not for every VS_ALL child a corresponding 
                       // VS_SOME child should be found
  bool check_pairs;    // true if we don't need to fill in pairs list
  bool* in_agreement;
};


void print_not_present_error(VestaSource* dir, VestaSource* child, Text repos = "")
  throw (ReposUI::failure, VestaConfig::failure, SRPC::failure);

VestaSource* get_vs_all_child(VestaSource* vs_all, unsigned int index, Arc arc)
  throw(ReposUI::failure, VestaConfig::failure, SRPC::failure);

// the function finds corresponding objects in two repositories
// and if it is not a checking operation 
// (check dir contents for immutable dirs) 
// inserts pair of corresponding sources to the list. 
static bool
findpaircallback(void* closure, VestaSource::typeTag type, Arc arc,
		 unsigned int index, Bit32 pseudoInode, ShortId filesid,
		 bool master) 
  throw(ReposUI::failure, VestaConfig::failure, SRPC::failure)
{

  PairClosure* cl = (PairClosure*)closure;

  if(cl->check_pairs) { 
    assert(cl->pairs->Size() > 0);
    assert(!cl->not_all);
    Pair pair;
    Text name(arc);
    if(!cl->pairs->Get(name, pair)) {
      *(cl->in_agreement) = false;
      VestaSource* vs_all_child = get_vs_all_child(cl->vs_all, index, arc);
      if(!vs_all_child)
	return true;
      print_not_present_error(cl->vs_some, vs_all_child);
      delete vs_all_child;
    }
  }
  else {
    // Look up current object
    VestaSource* vs_all_child = get_vs_all_child(cl->vs_all, index, arc);
    if(!vs_all_child)
      return true;
    
    // Look up corresponding object 
    VestaSource* vs_some_child;
    VestaSource::errorCode err = cl->vs_some->lookup(arc, vs_some_child); 
    if(err != VestaSource::ok) {
      Text repos = cl->vs_some->host() + ":" + cl->vs_some->port();
      if(err == VestaSource::notFound) {
	if(!cl->not_all) { 
	  *(cl->in_agreement) = false;
	  print_not_present_error(cl->vs_some, vs_all_child, repos);
	}
	// there is no violation if not for every entry of vs_all a 
	// corresponding object is found
	delete vs_all_child;
	return true;
      }
      Text source = ReposUI::vsToFilename(vs_all_child);
      delete vs_all_child;
      if(err == VestaSource::noPermission) { 
	cerr << program_name << ": Warning: failed looking up " << source << " at " 
	     << repos << ": " << ReposUI::errorCodeText(err) << endl;
	return true;
      }
      throw(ReposUI::failure("failed looking up " + source + 
			     " at " + repos + ": " + ReposUI::errorCodeText(err),
			     -1, err));
    }
    
    // create a new pair and insert it to the list if it is not 
    // an immutable-directories checking operation
    Pair pair;
    if(cl->reverse_order) {
      pair.vs1 = vs_some_child;
      pair.vs2 = vs_all_child;
    }
    else {
      pair.vs1 = vs_all_child;
      pair.vs2 = vs_some_child;
    }
    cl->pairs->Put(Text(arc), pair);   
  }
  return true;
}


// print error if CHILD is not present in DIR on other repository  
void print_not_present_error(VestaSource* dir, VestaSource* child, Text repos)
  throw (ReposUI::failure, VestaConfig::failure, SRPC::failure)
{
  Text dir_master; 
  if(dir->master)
    dir_master = "master";
  else
    dir_master = "non-master";

  Text dir_type;
  if(dir->type == VestaSource::immutableDirectory)
    dir_type = "immutable directory";
  else
    dir_type = "appendable directory";

  if(repos.Empty()) {
    repos = dir->host() + ":" + dir->port();
  }
  print_error(child, "is not present in " + dir_master + " " +
	      dir_type + " at " + repos);
}

VestaSource* get_vs_all_child(VestaSource* vs_all, unsigned int index, Arc arc)
  throw(ReposUI::failure, VestaConfig::failure, SRPC::failure)
{
  VestaSource* vs_all_child;
  VestaSource::errorCode err = vs_all->lookupIndex(index, vs_all_child); 
  if(err != VestaSource::ok) {   
    Text parent = ReposUI::vsToFilename(vs_all);
    Text source = parent + "/" + arc;
    Text repos = vs_all->host() + ":" + vs_all->port();
    if(err == VestaSource::noPermission) { 
      cerr << program_name << ": Warning: failed looking up " << source << " at " 
	   << repos << ": " << ReposUI::errorCodeText(err) << endl;
      return NULL;
    }
    throw(ReposUI::failure("failed looking up " + source + 
			   " at " + repos + ": " + ReposUI::errorCodeText(err),
			   -1, err));
  }
  return vs_all_child;
}

// the function fills a list PAIRS of corresponding sources looking 
// for VS_SOME_child for every VS_ALL_child. If NOT_ALL parameter is set 
// (=true) then not for every VS_ALL_child a corresponding object should be
// found. 
bool get_child_pairs(VestaSource* vs_all, VestaSource* vs_some, 
		     Table<Text, Pair>::Default& pairs, bool reverse = false, 
		     bool not_all = false, bool check_pairs = false)
  throw(ReposUI::failure, VestaConfig::failure, SRPC::failure)
{

  bool in_agreement = true;

  PairClosure paircl;
  paircl.pairs = &pairs;
  paircl.vs_all = vs_all;
  paircl.vs_some = vs_some;
  paircl.reverse_order = reverse;
  paircl.not_all = not_all;
  paircl.check_pairs = check_pairs;
  paircl.in_agreement = &in_agreement;

  VestaSource::errorCode err = 
    vs_all->list(0, findpaircallback, &paircl);
  if(err != VestaSource::ok) {
    Text dir = ReposUI::vsToFilename(vs_all);
    if(err == VestaSource::noPermission) { 
      cerr << program_name << ": Warning: failed listing directory: " << dir 
	   << ": " << ReposUI::errorCodeText(err) << endl;
    }
    else {
      throw ReposUI::failure("failed listing directory " + dir + ": " +
			     ReposUI::errorCodeText(err), -1, err);
    }
  }
  return in_agreement;
}

// check agreement of every pair in the list. 
bool check_pairs_agreement(Table<Text, Pair>::Default& pairs, const int level)
  throw(ReposUI::failure, VestaConfig::failure, SRPC::failure)
{
  bool in_agreement = true;
  Table<Text, Pair>::Iterator iter(&pairs);
  Text arc;
  Pair pair;
  while (iter.Next(arc, pair)) {
    in_agreement &= check_agreement(pair.vs1, pair.vs2, level);
    delete pair.vs1;
    delete pair.vs2;
    pairs.Delete(arc, pair, false);
  } 
  pairs.Resize();
  return in_agreement;
}


bool check_agreement(VestaSource* vs1, VestaSource* vs2, 
		     const int level)
  throw(ReposUI::failure, VestaConfig::failure, SRPC::failure)
{
  // 1. agreement violation if both have master attribute.
  if(vs1->master && vs2->master) {
    print_error(vs1, "is a master in both repositories");
    return false;
  }
  
  // 2. sources agree if at least one of them is a nonmaster stub.
  if((vs1->type == VestaSource::stub && !vs1->master) || 
     (vs2->type == VestaSource::stub && !vs2->master))
    return true;

  // 3. sources agree if one of them is a ghost.
  if(vs1->type == VestaSource::ghost || vs2->type == VestaSource::ghost)
    return true;

  // 4. sources do not agree if they are of different type and non of them 
  //    is a ghost or nonmaster stub.
  if(vs1->type != vs2->type) {
    Text msg = "is of type " + Text(VestaSource::typeTagString(vs1->type)) +
      " at " + vs1->host() + ":" + vs1->port() + 
      " and of type " + Text(VestaSource::typeTagString(vs2->type)) + 
      " at " + vs2->host() + ":" + vs2->port();
    print_error(vs1, msg);
    return false;
  }

  // 5. if both are immutable Directories and agreement-check-level is 0
  //    stop checking assuming that contents of the directories are the same.
  //    if the level is bigger then 0 check that directories structures are 
  //    the same. 
  else if(vs1->type == VestaSource::immutableDirectory) {
    if(level != 0) {
      // error if directories fingerprints are different
      if(vs1->fptag != vs2->fptag) {
	// if one of vs1 or vs2 has an empty delta over its base and the other's
	// fingerprint matches that of the base, then we treat it as a match.
	if(!check_delta(vs1, vs2)) {
	  if(!check_delta(vs2, vs1)) {
	    print_error(vs1, "has different fingerprints in two repositories");
	    return false;
	  }
	}
      }
      // define a list of corresponding pairs 
      Table<Text, Pair>::Default pairs;
      // for every child of vs1 find corresponding child of vs2
      bool in_agreement = true;
      in_agreement &= get_child_pairs(vs1, vs2, pairs);
      if(pairs.Size() != 0) {
	// check that there is no vs2 child with no corresponding vs1 child 
	in_agreement &= get_child_pairs(vs2, vs1, pairs, true, false, true);
      }
      // for every pair in list check agreement. Return false if there is
      // at least one pair that does not agree, and true if all of them agree.
      in_agreement &= check_pairs_agreement(pairs, level);
      return in_agreement;
    }
    else 
      return true;
  }

  // 6. if both are appendable directories recursive check their structures
  else if(vs1->type == VestaSource::appendableDirectory) {
    // define a list of corresponding pairs 
    Table<Text, Pair>::Default pairs;
    bool in_agreement = true;
    if(vs1->master) {
      in_agreement &= get_child_pairs(vs2, vs1, pairs, true);
    }
    else if(vs2->master) {
      in_agreement &= get_child_pairs(vs1, vs2, pairs);
    }
    else {
      in_agreement &= get_child_pairs(vs1, vs2, pairs, false, true);
    }
    // for every pair in list check agreement. Return false if there is
    // at least one pair that does not agree, and true if all of them agree.
    in_agreement &= check_pairs_agreement(pairs, level);
    return in_agreement;
  }

  // 7. if both are immutable files 
  else if(vs1->type == VestaSource::immutableFile) {
    switch(level) {
    case 0: return true; 
    case 1: return compare_files_properties(vs1, vs2);
    case 2: 
      if(compare_files_properties(vs1, vs2)) 
	return compare_files_contents(vs1, vs2);
      else return false; 
    }
  }

  // 8. if both are stubs - return
  else if(vs1->type == VestaSource::stub)
    return true;

  else // should never happen 
    assert(false);
}

bool compare_files_properties(VestaSource* vs1, VestaSource* vs2)
  throw(ReposUI::failure, VestaConfig::failure, SRPC::failure)
{
  bool in_agreement = true;
  
  // compare size
  Basics::uint64 size1 = vs1->size(); 
  Basics::uint64 size2 = vs2->size(); 
  if(size1 != size2) {
    char buf1[64], buf2[64];
    sprintf(buf1, "%"FORMAT_LENGTH_INT_64"d", size1);
    sprintf(buf2, "%"FORMAT_LENGTH_INT_64"d", size2);
    Text msg = 
      "is of size " + Text(buf1) + " at " + vs1->host() + ":" + vs1->port() +
      " and of size " + Text(buf2) + " at " + vs2->host() + ":" + vs2->port();
    print_error(vs1, msg);
    in_agreement = false;
  }

  // compare timestamp
  time_t timestamp1 = vs1->timestamp();
  time_t timestamp2 = vs2->timestamp();
  if(timestamp1 != timestamp2) {
    timefmt = VestaConfig::get_Text("UserInterface", "TimeFormat");
    char timebuf1[256], timebuf2[256];
    strftime(timebuf1, sizeof(timebuf1), timefmt.cchars(), 
	     localtime(&timestamp1));
    strftime(timebuf2, sizeof(timebuf2), timefmt.cchars(), 
	     localtime(&timestamp2));
    Text msg = 
      "has timestamp " + Text(timebuf1) + " at " + vs1->host() + ":" + 
      vs1->port() + " and " + Text(timebuf2) + " at " + vs2->host() + 
      ":" + vs2->port();
    print_error(vs1, msg);
    in_agreement = false;
  }

  // compare executable flag
  bool exec1 = vs1->executable();
  bool exec2 = vs2->executable();
  if((exec1 && !exec2) || (!exec1 && exec2)) {
    Text msg;
    if(exec1)
      msg = 
	"has executable status at " + vs1->host() + ":" + vs1->port() +
	" and nonexecutable status at " + vs2->host() + ":" + vs2->port();
    else
      msg = 
	"has nonexecutable status at " + vs1->host() + ":" + vs1->port() +
	" and executable status at " + vs2->host() + ":" + vs2->port();
    print_error(vs1, msg); 
    in_agreement = false;
  }

  // compare fingerprints
  if(vs1->fptag != vs2->fptag) {
    print_error(vs1,"has different fingerprints in two repositories");
    in_agreement = false;
  }
  return in_agreement;
}

bool  compare_files_contents(VestaSource* vs1, VestaSource* vs2)
  throw(ReposUI::failure, VestaConfig::failure, SRPC::failure)
{
  char buffer1[BUFSIZE];
  char buffer2[BUFSIZE];
  int nbytes = BUFSIZE;
  Basics::uint64 offset = 0;

  while(nbytes > 0) {
    VestaSource::errorCode err = vs1->read((void*)buffer1, &nbytes, offset);
    if(err != VestaSource::ok) {
      Text source = ReposUI::vsToFilename(vs1);
      Text repos = vs1->host() + ":" + vs1->port();
      throw(ReposUI::failure("failed reading file " + source + " at " + repos +
			     ": " + ReposUI::errorCodeText(err), -1, err));
    }
    int vs1_read_bytes = nbytes;
    if(vs1_read_bytes > 0) {
      err = vs2->read((void*)buffer2, &nbytes, offset);
      if(err != VestaSource::ok) {
	Text source = ReposUI::vsToFilename(vs2);
	Text repos = vs2->host() + ":" + vs2->port();
	throw(ReposUI::failure("failed reading file " + source + " at " + repos+
			       ": " + ReposUI::errorCodeText(err),-1, err));
      }
      if(nbytes != vs1_read_bytes) {
	print_error(vs1, "has different content in two repositories");
	return false;
      }
      if(memcmp(buffer1, buffer2, nbytes) != 0) {
	print_error(vs1, "has different content in two repositories");
	return false;
      } 
      // set parameters for next read
      offset += nbytes;
      if(nbytes < BUFSIZE) {
	// EOF has been reached
	nbytes = 0;
      }
    }
  }
  return true;
}

static bool 
check_delta_callback(void* closure, VestaSource::typeTag type, Arc arc,
		     unsigned int index, Bit32 pseudoInode, ShortId filesid,
		     bool master) 
{
  bool* empty_delta = (bool*) closure;
  *empty_delta = false;
  return false;
}

// if one of vs1 or vs2 has an empty delta over its base and the other's
// fingerprint matches that of the base, then we treat it as a match.
bool check_delta(VestaSource* vs, VestaSource* vs_other)
 throw (SRPC::failure)
{
  VestaSource* vs_base = NULL;
  VestaSource* vs_current = vs;
  VestaSource::errorCode err;
  bool match = false;
  while(!match) { 
    bool empty_delta = true;
    err = vs->list(0, check_delta_callback, (void*)&empty_delta,  NULL,
		   /*deltaOnly=*/ true);
    if(err != VestaSource::ok) {  // listing error occured => exit
      Text dir = ReposUI::vsToFilename(vs);
      Text repos = vs->host() + ":" + vs->port();
      cerr << program_name << ": Warning: failed listing " << dir 
	   << " or one of its bases at " << repos << ": " 
	   << ReposUI::errorCodeText(err) << endl;
      break;
    }

    if(empty_delta) { // vs_current has an empty delta over its base
      err = vs_current->getBase(vs_base);
      if(err != VestaSource::ok) { // getting base error occured => exit
	if(err != VestaSource::notFound) {
	  Text dir = ReposUI::vsToFilename(vs);
	  Text repos = vs->host() + ":" + vs->port();
	  cerr << program_name 
	       << ": Warning: failed getting one of the bases of " 
	       << dir << " at " << repos << ": " << ReposUI::errorCodeText(err) 
	       << endl;
	}
	break;
      }
      if(vs_base->fptag == vs_other->fptag) { // match is found
	match = true;
	break; 
      }
      else { // continue
	if(vs_current != vs)
	  delete vs_current;
	vs_current = vs_base;
	vs_base = NULL;
      }
    }
    else { // vs_current does not have an empty delta over its base
      break;
    }
  }

  if(vs_current != vs)
    delete vs_current;
  if(vs_base && vs_base != vs)
    delete vs_base;
  
  return match;
}
