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

// Last modified on Thu Apr 21 18:13:11 EDT 2005 by ken@xorian.net        
//      modified on Thu Apr 21 09:34:29 EDT 2005 by irina.furman@intel.com

// TestTextSeq.C - Test code for TextSeq.  (Mostly this tests
// Sequence::Iterator and that the STL sort algorithm can be used on a
// Sequence.)

#include <Generics.H>

#include <algorithm>

#include <iostream>
#include <stdlib.h>
#include <assert.h>

using std::cout;
using std::endl;
using std::sort;

void init_random()
{
  srandom(time(0));
}

// Generate a random string
char *random_str()
{
  static const char *chars=("abcdefghijklmnopqrstuvwxyz"
			    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			    "0123456789"
			    "!@#$%^&*-_=+,.<>;:/?`~\\|'\"");
  unsigned int nchars = strlen(chars);

  unsigned int len = (random() % (nchars/1)) + 1;
  char* result = NEW_PTRFREE_ARRAY(char, len+1);
  for(unsigned int i = 0; i < len; i++)
    {
      result[i] = chars[random() % nchars];
    }
  result[len] = 0;
  return result;
}

extern "C"
{
  int qsort_strcmp(const void *va, const void *vb)
  {
    const char *sa = *((const char **) va);
    const char *sb = *((const char **) vb);
    
    return strcmp(sa, sb);
  }
}

void SortTest()
{
  unsigned int nstrings = (random() % 30) + 10;
  char** strings = NEW_ARRAY(char*, nstrings);
  TextSeq seq1(nstrings), seq2(nstrings);

  for(unsigned int i = 0; i < nstrings; i++)
    {
      strings[i] = random_str();
      // Note: we add the in opposite orders.
      seq1.addhi(strings[i]);
      seq2.addlo(strings[i]);
    }

  // Sort the array with qsort and the TextSeqs with STL sort.
  qsort(strings, nstrings, sizeof(char *), qsort_strcmp);
  sort(seq1.begin(), seq1.end());
  sort(seq2.begin(), seq2.end());

  // The sorted order should be the same.
  for(unsigned int i = 0; i < nstrings; i++)
    {
      assert(seq1.get(i) == strings[i]);
      assert(seq2.get(i) == strings[i]);
    }
}

int main(int argc, char **argv)
{
  init_random();
  SortTest();

  cout << "All tests passed!" << endl;
  return 0;
}
