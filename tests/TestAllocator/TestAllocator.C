// Copyright (C) 2003, Kenneth C. Schalk

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

// Last modified on Thu May 27 14:58:26 EDT 2004 by ken@xorian.net        

#include <list>
#include <vector>
#include <algorithm>

#include "BasicsAllocator.H"

// Text used in the test.
static const Text testTexts[] = {
  "The quick brown fox jumped over the lazy dog.",
  "Now is the Time for All Good Men to Come to the Aid of their Country.",
  "It was the best of times, it was the worst of times"
  };

using std::list;
using std::vector;
using std::cout;
using std::endl;
using std::reverse;

// Typedefs for the STL types we'll be using to test
// Basics::Allocator.
typedef list<char, Basics::Allocator<char> > charList;
typedef vector<char, Basics::Allocator<char> > charVector;

// These functions convert a Text to either a charList or a
// charVector.

charList listFromText(const Text &in)
{
  const char *s = in.cchars();
  charList result;
  while(*s)
    result.push_back(*s++);
  return result;
}

charVector vectorFromText(const Text &in)
{
  const char *s = in.cchars();
  charVector result;
  while(*s)
    result.push_back(*s++);
  return result;
}

// Main body of the test.

int main(void)
{
  unsigned int nTexts = sizeof(testTexts) / sizeof(testTexts[0]);

  for(unsigned int i = 0; i < nTexts; i++)
    {
      const Text &testText = testTexts[i];
      // cout << "text[" << i << "] = \"" << testText << "\"" << endl;

      // Convert the text to both a list and a vector.
      charList l = listFromText(testText);
      charVector v = vectorFromText(testText);

      // Reverse both of them.
      reverse(l.begin(), l.end());
      reverse(v.begin(), v.end());

      // Loop forward over the text string.
      for(unsigned int j = 0; j < testText.Length(); j++)
	{
	  // The lengths of both containers should be equal to the number
	  // of characters remaining in the text string.
	  assert(v.size() == l.size());
	  assert(v.size() == (testText.Length() - j));

	  // Get the next character from the back of both of the
	  // containers and the next character in sequence from the text.
	  char cv = v.back(), cl = l.back(), ct = testText[j];

	  // All three of those should be the same.
	  assert(cv == ct);
	  assert(cl == ct);

	  v.pop_back();
	  l.pop_back();
	}
    }

  cout << "All tests passed!" << endl;
  return 0;
}
