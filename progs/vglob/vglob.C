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

// vglob.C
//

// Expand patterns of the kind that vrepl accepts.

// See documentation in vglob.1.mtex

#include <Replicator.H>
#include <ReposUI.H>
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

void usage()
{
  cerr << "Usage: " << program_name << endl <<
    "        [-v]" << endl <<
    "        [-R repos]" << endl <<
    "        pattern..." << endl << endl;
  exit(1);
}

class PrintMatchHandler
{
private:
  bool verbose, &matched;
public:
  PrintMatchHandler(bool v, bool &m)
    : verbose(v), matched(m)
  { }

  void operator()(const Text &match)
  {
    if(verbose)
      {
	cout << "\t";
      }
    cout << match << endl;

    matched = true;
  }
};

int main(int argc, char **argv)
{
  bool verbose = false;

  Text repos;
  bool default_repos = true;

  program_name = argv[0];

  opterr = 0;
  for (;;)
    {
      int c = getopt(argc, argv, "?hvR:");
      if (c == EOF) break;
      switch (c)
	{
	case 'v':
	  verbose = true;
	  break;
	case 'R':
	  repos = optarg;
	  default_repos = false;
	  break;
	case 'h':
	case '?':
	default:
	  usage();
	}
    }
  if (argc <= optind)
    {
      usage();
    }

  Text rhost(VDirSurrogate::defaultHost());
  Text rport(VDirSurrogate::defaultPort());

  // Use a nondefault repository if specified.
  if (!default_repos)
    {
      int colon = repos.FindCharR(':');
      if (colon == -1)
	{
	  rhost = repos;
	  repos = repos + ":" + rport;
	}
      else
	{
	  rhost = repos.Sub(0, colon);
	  rport = repos.Sub(colon+1);
	}
    }

  try
    {
      // Loop over te remaining arguments.
      for(int i = optind; i < argc; i++)
	{
	  // If we're being verbose, print the pattern before any
	  // matches
	  if(verbose)
	    {
	      cout << argv[i] << " ->" << endl;
	    }

	  // Expand this argument, printing as each match is found
	  bool was_matched = false;
	  PrintMatchHandler handler(verbose, was_matched);
	  Replicator::expandPattern(argv[i], handler,
				    rhost, rport);

	  // If we're being verbose and no matches were found for thie
	  // pattern, print a note about that.
	  if(verbose && !was_matched)
	    {
	      cout << "\t<<nothing>>" << endl;
	    }
	}
    }
  catch (SRPC::failure f)
    {
      cerr << program_name
	   << ": SRPC::failure " << f.msg << " (" << f.r << ")" << endl;
      exit(2);
    }
  catch(const Replicator::Failure &f)
    {
      if (f.code == (VestaSource::errorCode) -1)
	{
	  cerr << program_name << ": " << f.msg << endl;
	}
      else
	{
	  cerr << program_name << ": " << ReposUI::errorCodeText(f.code)
	       << ", " << f.msg << endl;
	}
      exit(2);
    }
  catch (ReposUI::failure f) 
    {
      cerr << program_name << ": " << f.msg << endl;
      exit(2);
    }
  catch (VestaConfig::failure f) 
    {
      cerr << program_name << ": " << f.msg << endl;
      exit(2);
    }
  return 0;
}
