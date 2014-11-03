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
// vcollapsebase.C - Utility for asking the repository to replace the
// chain of basis directories of a mutable or immutable directory with
// a collapsed representation for increased efficiency.
//

#include <Basics.H>
#include <Text.H>
#include <VestaConfig.H>
#include <VestaSource.H>
#include <VDirSurrogate.H>
#include <signal.h>
#include <ReposUI.H>

#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif

using std::cerr;
using std::endl;

Text program_name;

void usage()
{
  cerr << "Usage: " << program_name << endl <<
    "        [-c] [-R repos]" << endl <<
    "        directory" << endl << endl;
  exit(1);
}

struct EntryData
{
  struct EntryData *next;

  VestaSource::typeTag type;
  Text arc;
  unsigned int index;
  Bit32 pseudoInode;
  ShortId fileSid;
  bool master;

  EntryData(VestaSource::typeTag type, Arc arc,
	    unsigned int index, Bit32 pseudoInode,
	    ShortId fileSid, bool master)
    : type(type), arc(arc), index(index), pseudoInode(pseudoInode),
      fileSid(fileSid), master(master), next(0)
  { }
};

struct RecordClosure
{
  struct EntryData *first, *last;

  RecordClosure()
    : first(0), last(0)
    { }
};

bool recordListCB(void* closure, VestaSource::typeTag type, Arc arc,
		  unsigned int index, Bit32 pseudoInode,
		  ShortId fileSid, bool master)
{
  RecordClosure *rclosure = (RecordClosure *) closure;

  EntryData *thisEntry = NEW_CONSTR(EntryData,
				    (type, arc, index, pseudoInode,
				     fileSid, master));

  if(rclosure->last == 0)
    {
      rclosure->last = thisEntry;
      rclosure->first = thisEntry;
    }
  else
    {
      assert(rclosure->last->next == 0);
      rclosure->last->next = thisEntry;
      rclosure->last = thisEntry;
      assert(rclosure->first != 0);
    }

  return true;
}

struct CompareClosure
{
  const struct EntryData *list;
  unsigned int raw_index;
  bool miscompare;

  CompareClosure(const struct EntryData *list)
    : list(list), raw_index(0), miscompare(false)
    { }

  void advance()
    {
      raw_index++;
      if(list != 0)
	list = list->next;
    }
};

bool compareListCB(void* closure, VestaSource::typeTag type, Arc arc,
		   unsigned int index, Bit32 pseudoInode,
		   ShortId fileSid, bool master)
{
  CompareClosure *cclosure = (CompareClosure *) closure;

  if(cclosure->list == 0)
    {
      cclosure->miscompare = true;
      cerr << "directory entry " << cclosure->raw_index
	   << " not present in original:" << endl
	   << "\tarc   = \"" << arc << "\"" << endl
	   << "\ttype  = " << VestaSource::typeTagString(type) << endl
	   << "\tindex = " << index << endl;
    }
  else
    {
      bool type_match = (cclosure->list->type == type);
      bool arc_match = (cclosure->list->arc == arc);
      bool index_match = (cclosure->list->index == index);
      bool pinode_match = (cclosure->list->pseudoInode == pseudoInode);
      bool sid_match = (cclosure->list->fileSid == fileSid);
      bool master_match = ((cclosure->list->master && master) ||
			   (!cclosure->list->master && !master));

      // Mismatch on anything?
      if(!type_match || !arc_match || !index_match ||
	 !pinode_match || !sid_match || !master_match)
	{
	  cclosure->miscompare = true;
	  cerr << "directory entry " << cclosure->raw_index
	       << "mismatch:" << endl;

	  // First print information we want to display if it matched
	  // even though there was a mismatch on something else.
	  if(arc_match)
	    {
	      cerr << "\tarc   = \"" << arc << "\"" << endl;
	    }
	  if(type_match)
	    {
	      cerr << "\ttype  = " << VestaSource::typeTagString(type) << endl;
	    }
	  if(index_match)
	    {
	      cerr << "\tindex = " << index << endl;
	    }

	  if(!arc_match)
	    {
	      cerr << "\tarc was \"" << cclosure->list->arc 
		   << "\", is \"" << arc << "\"" << endl;
	    }
	  if(!type_match)
	    {
	      cerr << "\ttype was "
		   << VestaSource::typeTagString(cclosure->list->type)
		   << ", is " << VestaSource::typeTagString(type) << endl;
	    }
	  if(!index_match)
	    {
	      cerr << "\tindex was " << cclosure->list->index
		   << ", is " << index << endl;
	    }
	  if(!pinode_match)
	    {
	      cerr << "\tpseudoInode was " << cclosure->list->pseudoInode
		   << ", is " << pseudoInode << endl;
	    }
	  if(!pinode_match)
	    {
	      cerr << "\tfileSid was " << cclosure->list->fileSid
		   << ", is " << fileSid << endl;
	    }
	  if(!master_match)
	    {
	      cerr << "\tmaster was " << (cclosure->list->master
					  ?"true":"false")
		   << ", is " << (master?"true":"false") << endl;
	    }
	}
    }

  // Advance the closure to the next directory entry.
  cclosure->advance();

  return true;
}

int
main(int argc, char* argv[])
{
  // Save the program name
  program_name = Text(argv[0]);

  bool consistency_check = false;
  Text repos;
  bool default_repos = true;

  VestaSource::errorCode err;

  try
    {
      // Use the repository from the config file by default
      Text host(VDirSurrogate::defaultHost());
      Text port(VDirSurrogate::defaultPort());

      // Parse command line
      opterr = 0;
      for(;;)
	{
	  int c = getopt(argc, argv, "cR:");
	  if(c == EOF) break;
	  switch(c)
	    {
	    case 'c':
	      consistency_check = true;
	      break;
	    case 'R':
	      repos = optarg;
	      default_repos = false;
	      break;
	    case '?':
	    default:
	      usage();
	    }
	}
      if (argc <= optind)
	{
	  usage();
	}

      // Use a nondefault repository if specified
      if (!default_repos)
	{
	  int colon = repos.FindCharR(':');
	  if (colon == -1)
	    {
	      host = repos;
	      repos = repos + ":" + port;
	    }
	  else
	    {
	      host = repos.Sub(0, colon);
	      port = repos.Sub(colon+1);
	    }
	}

      for(unsigned int i = optind; i < argc; i++)
	{
	  // Canonicalize name
	  Text name = Text(argv[i]);
	  Text cname = ReposUI::canonicalize(name);

	  // Look up in the repository
	  VestaSource *vs = ReposUI::filenameToVS(cname, host, port);

	  // If we're doing consistency checking, list the directory and
	  // record its contents before calling collapseBase.
	  RecordClosure recorded;
	  if(consistency_check)
	    {
	      err = vs->list(0, recordListCB, &recorded);
	      if (err != VestaSource::ok)
		{
		  cerr << program_name 
		       << ": error listing directory " << name
		       << " (recording): "
		       << ReposUI::errorCodeText(err) << endl;
		  exit(1);
		}
	    }

	  // Collapse the base of this directory
	  err = vs->collapseBase();
	  if (err != VestaSource::ok)
	    {
	      cerr << program_name 
		   << ": error collapsing base of directory " << name << ": "
		   << ReposUI::errorCodeText(err) << endl;
	      exit(1);
	    }

	  // Now that the base has been replaced with a collapsed
	  // representation, perform the consistency check.
	  if(consistency_check)
	    {
	      CompareClosure compare(recorded.first);
	      err = vs->list(0, compareListCB, &compare);
	      if (err != VestaSource::ok)
		{
		  cerr << program_name 
		       << ": error listing directory " << name
		       << " (comparing): "
		       << ReposUI::errorCodeText(err) << endl;
		  exit(1);
		}
	      // In the event of a mis-compare, exit with error status.
	      if(compare.miscompare)
		{
		  cerr << program_name 
		       << ": collapseBase changed directory " << name << "!"
		       << endl;
		  exit(1);
		}
	    }
	}
    }
  catch(VestaConfig::failure f)
    {
      cerr << program_name << ": " << f.msg << endl;
      exit(2);
    }
  catch(SRPC::failure f)
    {
      cerr << program_name
	   << ": SRPC failure; " << f.msg << " (" << f.r << ")" << endl;
      exit(2);
    }
  catch(ReposUI::failure f)
    {
      cerr << program_name << ": " << f.msg << endl;
      exit(2);
    }
}
