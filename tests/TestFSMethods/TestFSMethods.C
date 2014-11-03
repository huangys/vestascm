// Copyright (C) 2006, Intel

// This file is part of Vesta.

// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// ----------------------------------------------------------------------
// TestFSMethods [-v] [-c path1 path2] [-s path] [-a path]
//
// Performs tests on a few of the functions in FS:: for manipulating
// paths.  By default just performs tests and indicates success or
// failure.  The user can perform calls of their own with -c, -s, and
// -a.
//
// -v : Produce verbose output for built-in tests
//
// -c path1 path2 : Test FS::CommonParent on the two given paths.
//
// -s path : Test FS::SplitPath on the given path
//
// -a path : Test FS::GetAbsolutePath and FS::RemoveSpecialArcs on the
// given path
// ----------------------------------------------------------------------

#include <stdio.h>
#include "FS.H"

using std::cerr;
using std::cout;
using std::endl;

// Should the test produce lots of output?

bool verbose = false;

// Input for test_CommonParent

const char *path[10] = {
               "/abcd/abcd/abcd",
               "/vesta", 
	       "/vesta/vestasys.org/vesta/eval", 
	       "/vesta/vestasys.org/vesta/eval/43",
	       "/vesta/vestasys.org/vesta/eval/43.foo",
	       "/vesta/vestasys.org/vesta/eval/43.foo/3",
	       "/vesta/vestasys.org/vesta/eval/checkout/43", 
	       "/vesta/vestasys.org/vesta/eval/checkout/43/4", 
               "/vesta/vestasys.org/vesta/eval/43.foo/checkout/3", 
               "/vesta/vestasys.org/vesta/eval/43.foo/checkout/3/2"
              }; 

// Expected outputs for test_CommonParent

const char *expected_rest1[10][9] = {
  { "/abcd/abcd/abcd",
    "/abcd/abcd/abcd",
    "/abcd/abcd/abcd",
    "/abcd/abcd/abcd",
    "/abcd/abcd/abcd",
    "/abcd/abcd/abcd",
    "/abcd/abcd/abcd",
    "/abcd/abcd/abcd",
    "/abcd/abcd/abcd" },
  { /* unused: */ "",
    "", "", "", "", "", "", "", "" },
  { /* unused: */  "", "",
    "", "", "", "", "", "", "" },
  { /* unused: */  "", "", "",
    "43", "43", "43", "43", "43", "43" },
  { /* unused: */  "", "", "", "",
    "", "43.foo", "43.foo", "", "" },
  { /* unused: */  "", "", "", "", "",
    "43.foo/3", "43.foo/3", "3", "3" },
  { /* unused: */  "", "", "", "", "", "",
    "", "checkout/43", "checkout/43" },
  { /* unused: */  "", "", "", "", "", "", "",
    "checkout/43/4", "checkout/43/4" },
  { /* unused: */  "", "", "", "", "", "", "", "",
    "" },
};

const char *expected_rest2[10][9] = {
  { "/vesta", 
    "/vesta/vestasys.org/vesta/eval", 
    "/vesta/vestasys.org/vesta/eval/43",
    "/vesta/vestasys.org/vesta/eval/43.foo",
    "/vesta/vestasys.org/vesta/eval/43.foo/3",
    "/vesta/vestasys.org/vesta/eval/checkout/43", 
    "/vesta/vestasys.org/vesta/eval/checkout/43/4", 
    "/vesta/vestasys.org/vesta/eval/43.foo/checkout/3", 
    "/vesta/vestasys.org/vesta/eval/43.foo/checkout/3/2" },
  { /* unused: */ "",
    "vestasys.org/vesta/eval", 
    "vestasys.org/vesta/eval/43",
    "vestasys.org/vesta/eval/43.foo",
    "vestasys.org/vesta/eval/43.foo/3",
    "vestasys.org/vesta/eval/checkout/43", 
    "vestasys.org/vesta/eval/checkout/43/4", 
    "vestasys.org/vesta/eval/43.foo/checkout/3", 
    "vestasys.org/vesta/eval/43.foo/checkout/3/2" },
  { /* unused: */  "", "",
    "43", "43.foo", "43.foo/3", "checkout/43", "checkout/43/4", "43.foo/checkout/3", "43.foo/checkout/3/2" },
  { /* unused: */  "", "", "",
    "43.foo", "43.foo/3", "checkout/43", "checkout/43/4", "43.foo/checkout/3", "43.foo/checkout/3/2" },
  { /* unused: */  "", "", "", "",
    "3", "checkout/43", "checkout/43/4", "checkout/3", "checkout/3/2" },
  { /* unused: */  "", "", "", "", "",
    "checkout/43", "checkout/43/4", "checkout/3", "checkout/3/2" },
  { /* unused: */  "", "", "", "", "", "",
    "4", "43.foo/checkout/3", "43.foo/checkout/3/2" },
  { /* unused: */  "", "", "", "", "", "", "",
    "43.foo/checkout/3", "43.foo/checkout/3/2" },
  { /* unused: */  "", "", "", "", "", "", "", "",
    "2" },
};

const char *expected_common[10][9] = {
  { "", "", "", "", "", "", "", "", "" },
  { /* unused: */ "",
    "/vesta", "/vesta", "/vesta", "/vesta",
    "/vesta", "/vesta", "/vesta", "/vesta" },
  { /* unused: */  "", "",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval" },
  { /* unused: */  "", "", "",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval" },
  { /* unused: */  "", "", "", "",
    "/vesta/vestasys.org/vesta/eval/43.foo",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval/43.foo",
    "/vesta/vestasys.org/vesta/eval/43.foo" },
  { /* unused: */  "", "", "", "", "",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval/43.foo",
    "/vesta/vestasys.org/vesta/eval/43.foo" },
  { /* unused: */  "", "", "", "", "", "",
    "/vesta/vestasys.org/vesta/eval/checkout/43",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval" },
  { /* unused: */  "", "", "", "", "", "", "",
    "/vesta/vestasys.org/vesta/eval",
    "/vesta/vestasys.org/vesta/eval" },
  { /* unused: */  "", "", "", "", "", "", "", "",
    "/vesta/vestasys.org/vesta/eval/43.foo/checkout/3" },
};

void test_CommonParent()
{
  if(verbose)
    cout << "FS::CommonParent() test" << endl << endl;

  Text common, rest1, rest2;
  for(int i = 0; i < 9; i++)
    for(int j = i+1; j < 10; j++)
    {
      common = FS::CommonParent(path[i], path[j], &rest1, &rest2);
      bool fail = false;
      if(common.Empty() && i != 0)
	{
	  cerr << "test_CommonParent: Error0: " << path[i] << " and " << path[j]
	       << " do not nave common parent" << endl;
	  fail = true;
	}
      if(i == 1 && common != "/vesta")
	{
	  cerr << "test_CommonParent: Error1: " << path[i] << " and " << path[j]
	       << " have common parent: " << common << endl;
	  fail = true;
	}
      if(i > 1 && common.FindText("/vesta/vestasys.org/vesta/eval") < 0)
	{
	  cerr << "test_CommonParent: Error2: " << path[i] << " and " << path[j]
	       << " have common parent: " << common << endl;
	  fail = true;
	}
      if(expected_common[i][j-1] != common)
	{
	  cerr << "test_CommonParent: Error3: expected common = \""
	       << expected_common[i][j-1] << "\", got common = \""
	       << common << "\"" << endl;
	  fail = true;
	}
      if(expected_rest1[i][j-1] != rest1)
	{
	  cerr << "test_CommonParent: Error4: expected rest1 = \""
	       << expected_rest1[i][j-1] << "\", got rest1 = \""
	       << rest1 << "\"" << endl;
	  fail = true;
	}
      if(expected_rest2[i][j-1] != rest2)
	{
	  cerr << "test_CommonParent: Error5: expected rest2 = \""
	       << expected_rest2[i][j-1] << "\", got rest2 = \""
	       << rest2 << "\"" << endl;
	  fail = true;
	}
      if(verbose || fail)
	{
	  cout << "*******************************" << endl;
	  cout << "fname1: " << path[i] << endl;
	  cout << "fname2: " << path[j] << endl;
	  cout << "common: " << common << endl;
	  cout << "rest1: " << rest1 << endl;
	  cout << "rest2: " << rest2 << endl;
	  cout << "*******************************" << endl << endl;
	}
      if(fail)
	exit(1);
    }
}


const Text getFirstArcPaths[14] = {
  "/vesta/",
  "/vesta",
  "vesta/",
  "vesta",
  "/vesta/vestasys.org/",
  "/vesta/vestasys.org",
  "vesta/vestasys.org/",
  "vesta/vestasys.org",
  "/vesta/vestasys.org/vesta/",
  "/vesta/vestasys.org/vesta",
  "vesta/vestasys.org/vesta/",
  "vesta/vestasys.org/vesta",
  "/",
  ""
}; 

const char *getFirstArc_expected_first[14] = {
  "vesta",
  "vesta",
  "vesta",
  "vesta",
  "vesta",
  "vesta",
  "vesta",
  "vesta",
  "vesta",
  "vesta",
  "vesta",
  "vesta",
  "",
  ""
};

const char *getFirstArc_expected_rest[14] = {
  "",
  "",
  "",
  "",
  "vestasys.org",
  "vestasys.org",
  "vestasys.org",
  "vestasys.org",
  "vestasys.org/vesta",
  "vestasys.org/vesta",
  "vestasys.org/vesta",
  "vestasys.org/vesta",
  "",
  ""
}; 

void test_GetFirstArc()
{
  if(verbose)
    cout << endl << "FS::GetFirstArc() test" << endl << endl;

  for(int i = 0; i < 14; i++)
    {
      Text rest;
      Text first;
      first = FS::GetFirstArc(getFirstArcPaths[i], &rest);
      bool fail = false;
      if(first != getFirstArc_expected_first[i])
	{
	  cerr << "test_GetFirstArc: Error1: expected first = \""
	       << getFirstArc_expected_first[i] << "\", got first = \""
	       << first << "\"" << endl;
	  fail = true;
	}
      if(rest != getFirstArc_expected_rest[i])
	{
	  cerr << "test_GetFirstArc: Error2: expected rest = \""
	       << getFirstArc_expected_rest[i] << "\", got rest = \""
	       << rest << "\"" << endl;
	  fail = true;
	}
      if(verbose || fail)
	{
	  cout << endl << "path = " << getFirstArcPaths[i] << endl;
	  cout << "first arc = " << first << endl;
	  cout << "rest = " << rest << endl;
	}
      if(fail)
	exit(1);
    }
}

void test_misc()
{
  Text cwd = FS::getcwd();

  Text cwd2 = FS::RemoveSpecialArcs(FS::GetAbsolutePath("."));

  if(cwd2 != cwd)
    {
      cerr << "test_misc: cwd mismatch: \"" << cwd
	   << "\" vs. \"" << cwd2 << "\"" << endl;
      exit(1);
    }

  Text cwdp = FS::ParentDir(cwd);

  Text cwdp2 = FS::RemoveSpecialArcs(FS::GetAbsolutePath(".."));

  if(cwdp2 != cwdp)
    {
      cerr << "test_misc: cwd parent mismatch: \"" << cwdp
	   << "\" vs. \"" << cwdp2 << "\"" << endl;
      exit(1);
    }

  Text cwdp3 = FS::ParentDir(".");

  if(cwdp3 != cwdp)
    {
      cerr << "test_misc: cwd parent mismatch: \"" << cwdp
	   << "\" vs. \"" << cwdp3 << "\"" << endl;
      exit(1);
    }
}

int main(int argc, char* argv[])
{
  try
    {
      for(unsigned int i = 1; i < argc; i++)
	{
	  if(strcmp(argv[i], "-v") == 0)
	    {
	      verbose = true;
	    }
	  else if(strcmp(argv[i], "-c") == 0)
	    {
	      if(argc > i+2)
		{
		  Text fname1 = argv[i+1];
		  Text fname2 = argv[i+2];
		  i += 2;
		  Text rest1, rest2;
		  Text common  = FS::CommonParent(fname1, fname2,
						  &rest1, &rest2);
		  cout << "*******************************" << endl;
		  cout << "fname1: " << fname1 << endl;
		  cout << "fname2: " << fname2 << endl;
		  cout << "common: " << common << endl;
		  cout << "rest1: " << rest1 << endl;
		  cout << "rest2: " << rest2 << endl;
		  cout << "*******************************" << endl << endl;
		}
	      else
		{
		  cerr << "-c needs 2 arguments" << endl;
		  exit(1);
		}
	    }
	  else if(strcmp(argv[i], "-s") == 0)
	    {
	      if(argc > i+1)
		{
		  Text fname = argv[i+1];
		  i += 1;
		  Text name, parent;
		  FS::SplitPath(fname, name, parent);
		  cout << "*******************************" << endl;
		  cout << "fname:  " << fname << endl;
		  cout << "name:   " << name << endl;
		  cout << "parent: " << parent << endl;
		  cout << "*******************************" << endl << endl;
		}
	      else
		{
		  cerr << "-s needs 1 argument" << endl;
		  exit(1);
		}
	    }
	  else if(strcmp(argv[i], "-a") == 0)
	    {
	      if(argc > i+1)
		{
		  Text fname = argv[i+1];
		  i += 1;
		  Text absolute =
		    FS::RemoveSpecialArcs(FS::GetAbsolutePath(fname));
		  cout << "*******************************" << endl;
		  cout << "fname:     " << fname << endl;
		  cout << "absolute:  " << absolute << endl;
		  cout << "*******************************" << endl << endl;
		}
	      else
		{
		  cerr << "-a needs 1 argument" << endl;
		  exit(1);
		}
	    }
	}

      test_CommonParent();
      test_GetFirstArc();
      test_misc();
    }
  catch(FS::Failure f)
    {
      cerr << f << endl;
    }

  cout << "All tests passed!" << endl;
    
  return 0;
}
