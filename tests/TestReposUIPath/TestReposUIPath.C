// Copyright (C) 2007, Vesta Free Software Project
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

#include <ReposUIPath.H>

using std::cout;
using std::cerr;
using std::endl;
using std::ostream;

unsigned int verbose = 0;

ostream &operator<<(ostream &out, ReposUI::RootLoc root)
{
  switch(root)
    {
    case ReposUI::VESTA:
      out << "appendable";
      break;
    case ReposUI::VESTAWORK:
      out << "mutable";
      break;
    case ReposUI::NOWHERE:
      out << "NOWHERE (not in repository)";
      break;
    default:
      out << "<<INVALID>>";
      break;
    }
  return out;
}

void dump_path(const ReposUI::Path &p)
{
  cout << "root      = " << p.root() << endl
       << "relative  = " << p.relative() << endl
       << "canonical = " << p.canonical() << endl
       << "mounted   = " << p.mounted() << endl;
}

int test_one_failing(const char *arg, bool canonical)
{
  try
    {
      ReposUI::Path p(arg, canonical);
      cerr << "========== Expected failure, but didn't get one! ==========" << endl;
      cerr << "========== path = \"" << arg << "\" ==========" << endl;
      cerr << "========== canonical = " << (canonical ? "true" : "false") << " ==========" << endl;
      dump_path(p);
      return 1;
    }
  catch(ReposUI::failure f)
    {
      if(verbose)
	cout << "Got expected ReposUI::failure: " << f.msg << endl;
      return 0;
    }
}

int test_one(const Text &arg, bool canonical,
	     ReposUI::RootLoc expected_root,
	     const Text &expected_relative,
	     const Text &expected_canonical,
	     const Text &expected_mounted)
{
  if(verbose)
    {
      cout << "---------- Testing \"" << arg << "\" ----------" << endl;
    }
  try
    {
      ReposUI::Path p(arg, canonical);
      if(verbose > 1)
	{
	  dump_path(p);
	}
      if(p.root() != expected_root)
	{
	  cerr << "========== Incorrect root ==========" << endl
	       << "  [in] path      = \"" << arg << "\"" << endl
	       << "  [in] canonical = " << (canonical ? "true" : "false") << endl
	       << "  [out] expected root = " << expected_root << endl
	       << "  [out] result root   = " << p.root() << endl;
	  return 1;
	}
      if(p.relative() != expected_relative)
	{
	  cerr << "========== Incorrect relative path ==========" << endl
	       << "  [in] path      = \"" << arg << "\"" << endl
	       << "  [in] canonical = " << (canonical ? "true" : "false") << endl
	       << "  [out] expected relative = " << expected_relative << endl
	       << "  [out] result relative   = " << p.relative() << endl;
	  return 1;
	}
      if(p.canonical() != expected_canonical)
	{
	  cerr << "========== Incorrect canonical path ==========" << endl
	       << "  [in] path      = \"" << arg << "\"" << endl
	       << "  [in] canonical = " << (canonical ? "true" : "false") << endl
	       << "  [out] expected canonical = " << expected_canonical << endl
	       << "  [out] result canonical   = " << p.canonical() << endl;
	  return 1;
	}
      if(p.mounted() != expected_mounted)
	{
	  cerr << "========== Incorrect mounted path ==========" << endl
	       << "  [in] path      = \"" << arg << "\"" << endl
	       << "  [in] canonical = " << (canonical ? "true" : "false") << endl
	       << "  [out] expected mounted = " << expected_mounted << endl
	       << "  [out] result mounted   = " << p.mounted() << endl;
	  return 1;
	}
    }
  catch(ReposUI::failure f)
    {
      cerr << "========== Unexpected ReposUI::failure ==========" << endl
	   << "  [in] path      = \"" << arg << "\"" << endl
	   << "  [in] canonical = " << (canonical ? "true" : "false") << endl
	   << "  [failure] " << f.msg << endl;
      return 1;
    }

  return 0;
}

void test_one_user(const char *arg, bool canonical)
{
  try
    {
      ReposUI::Path p(arg, canonical);
      dump_path(p);
    }
  catch(VestaConfig::failure f)
    {
      cout << "VestaConfig::failure: " << f.msg << endl;
    }
  catch(ReposUI::failure f)
    {
      cout << "ReposUI::failure: " << f.msg << endl;
    }
}

int main(int argc, char **argv)
{
  unsigned int arg_i = 1;
  while((arg_i < argc) && (strcmp(argv[arg_i], "-v") == 0))
    {
      verbose++;
      arg_i++;
    }

  if(arg_i < argc)
    {
      // Test user-supplied paths
      for(; arg_i < argc; arg_i++)
	{
	  cout << "========== " << argv[arg_i] << " ==========" << endl;
	  cout << "---------- Not assuming canonical ----------" << endl;
	  test_one_user(argv[arg_i], false);
	  cout << "---------- Assuming canonical ----------" << endl;
	  test_one_user(argv[arg_i], true);
	}
    }
  else
    {
      // Fixed tests
      Text app_mounted = VestaConfig::get_Text("UserInterface", "AppendableRootName");
      Text mut_mounted = VestaConfig::get_Text("UserInterface", "MutableRootName");

      if(test_one("/vesta", true,
		  ReposUI::VESTA, "", "/vesta", app_mounted))
	return 1;
      if(test_one(app_mounted, false,
		  ReposUI::VESTA, "", "/vesta", app_mounted))
	return 1;
      if(test_one("/vesta/vestasys.org", true,
		  ReposUI::VESTA, "vestasys.org", "/vesta/vestasys.org", app_mounted+"/vestasys.org"))
	return 1;
      if(test_one(app_mounted+"/vestasys.org", false,
		  ReposUI::VESTA, "vestasys.org", "/vesta/vestasys.org", app_mounted+"/vestasys.org"))
	return 1;
      if(test_one("/vesta/foo.example.com/../bar.example.com/.", false,
		  ReposUI::VESTA, "bar.example.com", "/vesta/bar.example.com", app_mounted+"/bar.example.com"))
	return 1;
      if(test_one(app_mounted+"/foo.example.com/../bar.example.com/.", false,
		  ReposUI::VESTA, "bar.example.com", "/vesta/bar.example.com", app_mounted+"/bar.example.com"))
	return 1;

      if(test_one("/vesta-work", true,
		  ReposUI::VESTAWORK, "", "/vesta-work", mut_mounted))
	return 1;
      if(test_one(mut_mounted, false,
		  ReposUI::VESTAWORK, "", "/vesta-work", mut_mounted))
	return 1;
      if(test_one("/vesta-work/jsmith", true,
		  ReposUI::VESTAWORK, "jsmith", "/vesta-work/jsmith", mut_mounted+"/jsmith"))
	return 1;
      if(test_one(mut_mounted+"/jsmith", false,
		  ReposUI::VESTAWORK, "jsmith", "/vesta-work/jsmith", mut_mounted+"/jsmith"))
	return 1;
      if(test_one("/vesta-work/./jsmith/../kjones/.", false,
		  ReposUI::VESTAWORK, "kjones", "/vesta-work/kjones", mut_mounted+"/kjones"))
	return 1;
      if(test_one(mut_mounted+"/./jsmith/../kjones/.", false,
		  ReposUI::VESTAWORK, "kjones", "/vesta-work/kjones", mut_mounted+"/kjones"))
	return 1;

      cout << "All tests passed!" << endl;
    }

  return 0;
}
