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


#include "Basics.H"
#include "OS.H"
#include "VestaConfig.H"

using std::cout;
using std::endl;

unsigned int error_count = 0;
  
void report_location()
{
  try {
    Text loc(VestaConfig::get_location());

    cout << "Configuration file found at " << loc.cchars() << endl;
  } catch (VestaConfig::failure f) { cout << f.msg <<  endl; }
}

void set_location(const Text &loc)
{
  cout << endl;
  cout << "Setting configuration file to " << loc << "...";
  try {
    VestaConfig::set_location(loc);
    cout << "ok\n";
  } catch (VestaConfig::failure f) { cout << f.msg <<  endl; }
}

void print_expected(const char *result)
{
  cout << endl;
  if (result == NULL) cout << "  Expected lookup error\n";
  else cout << "  Expected: " << result << endl;
}

void do_lookup(const char *s, const char *n, const char *result)
{
  Text v;
  cout << "Lookup [" << s << "]" << n << "...";
  try {
    if (VestaConfig::get(s, n, v))
      if (result != NULL && v == result) cout << "ok." << endl;
      else {
	print_expected(result);
	cout << "  Actual:   " << v << endl;
	error_count++;
      }
    else
      if (result == NULL) cout << "ok." << endl;
      else {
	print_expected(result);
	cout << "  Field not found" << endl;
	error_count++;
      }
  } catch (VestaConfig::failure f) {
    cout << "  Got error: " << f.msg << endl;
    error_count++;
  }
}

void do_lookup_int(const char *s, const char *n, const int result)
{
  int v;
  cout << "Lookup_int [" << s << "]" << n << "...";
  try {
    v = VestaConfig::get_int(s, n);
    if (v == result) cout << "ok." << endl;
    else {
      cout << "  Expected:  " << result << "  Actual:   " << v << endl;
      error_count++;
    }
  } catch (VestaConfig::failure f) {
    cout << "  Got error: " << f.msg << endl;
    error_count++;
  }
}


void test_bad(const char *name, const char *problem)
{
  cout << endl;
  set_location(name);
  cout << "Expected problem:  " << problem << endl;
  try {
    Text dummy;
    (void)VestaConfig::get("Doesn't", "matter", dummy);
    cout << "Failed to get an error report!" << endl;
    error_count++;
  } catch (VestaConfig::failure f) {
    cout << "Reported problem:  " << f.msg << endl;
  }
}

int main()
{
  // Test 1:  report default location.

  cout << "Default configuration file: ";
  report_location();

  // Test 2: set location to a good file; try some lookups.

  set_location("tests/good_file.cfg");

  do_lookup("Section 1", "Field 1", "Value 1");
  do_lookup("Section 2", "Odd_field+name", "even   <,,> odder <,,> value :-)");
  do_lookup("Section 3", "xyzzy", "crowther");
  do_lookup("Section 4", "No such field", NULL);
  do_lookup("Section 5", "Long_field", "This field is so long that it occupies more than one hundred characters, which is the internal buffer size and so requires some extra code to ensure that the whole line is processed.");
  do_lookup("Section 5", "The very end", "19");
  do_lookup_int("Section 5", "The very end", 19);
  do_lookup("Included Section", "FieldInIncludedSection", "Hello");
  do_lookup("Section 6", "Field", "Right");
  do_lookup("Section 7", "Field", "Right");
  do_lookup("Section 7", "Field2", "New");
  do_lookup_int("Included Dir Section", "foo", 10);
  do_lookup_int("Included Dir Section", "bar", 20);
  do_lookup_int("Included Dir Section", "foo2", 30);
  do_lookup_int("Included Dir Section", "bar2", 40);
  do_lookup_int("Included Dir Section", "shared", 20);
  do_lookup_int("Included Dir Section", "overwritten", 50);

  // Test 3: set location to various files with syntax errors.

  test_bad("tests/bad_file_1.cfg", "malformed [section] line");

  test_bad("tests/bad_file_2.cfg", "missing equal sign");

  test_bad("tests/bad_file_3.cfg", "missing name before equal sign");

  test_bad("tests/bad_file_4.cfg", "inclusion depth too large");
  
  test_bad("tests/bad_file_5.cfg", "no section name");

  test_bad("tests/no_such_file", "configuration file doesn't exist");
  
  return (error_count > 0) ? 1 : 0;
}
