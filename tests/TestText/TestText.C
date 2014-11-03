// Copyright (C) 2001, Compaq Computer Corporation

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

// TestText -- tests the Text interface and implementation

#include "Basics.H"
#include "Text.H"
#include "BufStream.H"

using std::cout;
using std::cerr;
using std::endl;

void TestHash(const Text& t)
{
  char hash_buff[17];
  sprintf(hash_buff, "%016" FORMAT_LENGTH_INT_64 "x", t.Hash());
  cout << hash_buff << " = Hash(\"" << t << "\")\n";
}

void IndexChar(const Text& t, int i)
{
  cout << "T[" << i << "] = '" << t[i] << "'\n";
}

void TestPad(const Text& t, const Text &expected, const Text &expected2)
{
  Text padded = t.PadLeft(10).PadRight(20,"-");
  if(padded != expected)
    {
      cerr << "Padding test failed:" << endl
	   << "  expected = \"" << expected << "\"" << endl
	   << "  actual   = \"" << padded << "\"" << endl;
      exit(1);
    }

  Text padded2 = t.PadLeft(10, "/|\\|").PadRight(20,"+-=");
  if(padded2 != expected2)
    {
      cerr << "Padding test failed:" << endl
	   << "  expected = \"" << expected2 << "\"" << endl
	   << "  actual   = \"" << padded2 << "\"" << endl;
      exit(1);
    }

  // Useful if changing the tests:
  /*
  cout << "TestPad(\"" << t << "\"," << endl
       << "        \"" << padded << "\"," << endl
       << "        \"" << padded2 << "\");" << endl;
  */
}

void TestPrintf(const Text& t, int i)
{
  Basics::OBufStream expected;
  expected << "{{{" << t << "}}} : [[[" << i << "]]]";

  Text actual = Text::printf("{{{%s}}} : [[[%d]]]", t.cchars(), i);

  if(actual != expected.str())
    {
      cerr << "Text::printf test failed:" << endl
	   << "  expected = \"" << expected.str() << "\"" << endl
	   << "  actual   = \"" << actual << "\"" << endl;
      exit(1);
    }
}

int main()
{
    char *str = "Hello, World!";
    const int len = strlen(str);

    // Text(char *), Text(Text &)
    Text t1(str);
    assert(t1 == str);
    assert(strcmp(t1.cchars(), str) == 0);
    cout << "String copy of '" << str << "' = '" << t1 << "'\n";
    Text t2(t1);
    assert(t1 == str);
    assert(t1 == t2);
    assert(strcmp(t1.cchars(), str) == 0);
    assert(strcmp(t1.cchars(), t2.cchars()) == 0);
    cout << "Text copy of '" << str << "' = '" << t2 << "'\n";
    (cout << "\n").flush();

    // Text(char)
    cout << "Using FromChar = '" << t1 << Text(' ') << t1 << "'\n";
    (cout << "\n").flush();

    // explicit conversion to (char *)
    cout << "Text as (char *) = '" << t1.chars() << "'\n";
    cout << "Text as (const char *) = '" << t1.cchars() << "'\n";
    (cout << "\n").flush();

    // Text::Concat
    cout << "Concatenated twice = '" << (t1 + t1) << "'\n";
    t2 += t1;
    cout << "Destructively concatenated twice = '" << t2 << "'\n";
    (cout << "\n").flush();

    // Text::Length
    cout << "Length = " << t1.Length() << "\n";
    (cout << "\n").flush();

    // Text::Sub
    cout << "Sub(0) = '" << t1.Sub(0) << "'\n";
    cout << "Sub(1) = '" << t1.Sub(1) << "'\n";
    cout << "Sub(2) = '" << t1.Sub(2) << "'\n";
    cout << "Sub(7) = '" << t1.Sub(7) << "'\n";
    cout << "Sub(" << len-1 << ") = '" << t1.Sub(len-1) << "'\n";
    cout << "Sub(" << len << ") = '" << t1.Sub(len) << "'\n";
    cout << "Sub(7, 0) = '" << t1.Sub(7, 0) << "'\n";
    cout << "Sub(7, 1) = '" << t1.Sub(7, 1) << "'\n";
    cout << "Sub(7, 2) = '" << t1.Sub(7, 2) << "'\n";
    (cout << "\n").flush();

    // Text::FindChar
    cout << "FindChar('o') = " << t1.FindChar('o') << "\n";
    cout << "FindChar('o', 0) = " << t1.FindChar('o', 0) << "\n";
    cout << "FindChar('o', 4) = " << t1.FindChar('o', 4) << "\n";
    cout << "FindChar('o', 5) = " << t1.FindChar('o', 5) << "\n";
    cout << "FindChar('o', 8) = " << t1.FindChar('o', 8) << "\n";
    cout << "FindChar('o', 9) = " << t1.FindChar('o', 9) << "\n";
    cout << "FindChar('o', 13) = " << t1.FindChar('o', 13) << "\n";
    cout << "FindChar('o', 20) = " << t1.FindChar('o', 20) << "\n";
    (cout << "\n").flush();

    // Text::FindCharR
    cout << "FindCharR('o') = " << t1.FindCharR('o') << "\n";
    cout << "FindCharR('o', 12) = " << t1.FindCharR('o', 12) << "\n";
    cout << "FindCharR('o', 8) = " << t1.FindCharR('o', 8) << "\n";
    cout << "FindCharR('o', 7) = " << t1.FindCharR('o', 7) << "\n";
    cout << "FindCharR('o', 4) = " << t1.FindCharR('o', 4) << "\n";
    cout << "FindCharR('o', 3) = " << t1.FindCharR('o', 3) << "\n";
    cout << "FindCharR('o', -1) = " << t1.FindCharR('o', -1) << "\n";
    cout << "FindCharR('o', -5) = " << t1.FindCharR('o', -5) << "\n";
    (cout << "\n").flush();

    // Print a message about endian differences, and test Text::WordWrap.
    cout << Text("Note: the Hash values will be different between "
		 "big-endian and little-endian systems.  However "
		 "they should be the same on all systems of the "
		 "same byte order.").WordWrap() << endl << endl;

    // Text::Hash
    TestHash("defghabcdefghabcdefgh");
    TestHash("abcdefghabcdefghabcdefgh");
    TestHash("defg");
    TestHash("abcdefg");
    TestHash("a");
    TestHash("ab");
    TestHash("abc");
    TestHash("abcdefgh");
    TestHash("abcdefghabcdefgh");
    TestHash("abcdefghi");
    (cout << "\n").flush();

    // t[i]
    cout << "T = \"" << t1 << "\"\n";
    IndexChar(t1, 0);
    IndexChar(t1, 1);
    IndexChar(t1, t1.Length()-1);
    (cout << "\n").flush();

    TestPad("a",
	    "         a----------",
	    "/|\\|/|\\|/a+-=+-=+-=+");
    TestPad("ab",
	    "        ab----------",
	    "/|\\|/|\\|ab+-=+-=+-=+");
    TestPad("abc",
	    "       abc----------",
	    "/|\\|/|\\abc+-=+-=+-=+");
    TestPad("abcdefgh",
	    "  abcdefgh----------",
	    "/|abcdefgh+-=+-=+-=+");
    TestPad("abcdefghij",
	    "abcdefghij----------",
	    "abcdefghij+-=+-=+-=+");
    TestPad("abcdefghijkl",
	    "abcdefghijkl--------",
	    "abcdefghijkl+-=+-=+-");
    TestPad("abcdefghijklmn",
	    "abcdefghijklmn------",
	    "abcdefghijklmn+-=+-=");
    TestPad("abcdefghijklmnop",
	    "abcdefghijklmnop----",
	    "abcdefghijklmnop+-=+");
    TestPad("abcdefghijklmnopqr",
	    "abcdefghijklmnopqr--",
	    "abcdefghijklmnopqr+-");
    TestPad("abcdefghijklmnopqrst",
	    "abcdefghijklmnopqrst",
	    "abcdefghijklmnopqrst");
    TestPad("abcdefghijklmnopqrstu",
	    "abcdefghijklmnopqrstu",
	    "abcdefghijklmnopqrstu");
    TestPad("abcdefghijklmnopqrstuvwx",
	    "abcdefghijklmnopqrstuvwx",
	    "abcdefghijklmnopqrstuvwx");

    TestPrintf("abcdefgh", 1234);
    TestPrintf("abcdefghijkl", 123456);
    TestPrintf("abcdefghijklmnop", 12345678);
    TestPrintf("abcdefghijklmnopqrst", 1234567890);

    return 0;
}
