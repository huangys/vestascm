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

// Last modified on Wed Dec  8 01:33:47 EST 2004 by ken@xorian.net

#include <VestaSource.H>
#include "VLogHelp.H"

using std::cout;
using std::cerr;
using std::endl;

// The number of generated LongIds of maximum length that have been
// generated.

unsigned int max_length_count = 0;

// Perform consistency checks on a generated LongId.  If any of the
// checks fail, exit with a non-zero status.

void check(const LongId &parent, const LongId &child, unsigned int child_index)
{
  if(child.length() > sizeof(child.value))
    {
      cerr << "child length exceeds maximum:" << endl
	   << "  child = " << child << endl;
      exit(1);
    }
  else if(child.length() == sizeof(child.value))
    {
      max_length_count++;
    }

  // The parent should be an ancestor of the child
  if(!parent.isAncestorOf(child))
    {
      cerr << "parent.isAncestorOf(child) failed:" << endl
	   << "  parent = " << parent << endl
	   << "  child = " << child << endl;
      exit(1);
    }

  // A LongId is its own ancestor
  if(!child.isAncestorOf(child))
    {
      cerr << "child.isAncestorOf(child) failed:" << endl
	   << "  child = " << child << endl;
      exit(1);
    }

  // The child should not be an ancestor fo the parent
  if(child.isAncestorOf(parent))
    {
      cerr << "child.isAncestorOf(parent) failed:" << endl
	   << "  parent = " << parent << endl
	   << "  child = " << child << endl;
      exit(1);
    }

  // Determine parent and index from child
  unsigned int computed_child_index;
  LongId computed_parent = child.getParent(&computed_child_index);

  // The computed parent should be the same as the parent.
  if(!(computed_parent == parent))
    {
      cerr << "child.getParent != parent:" << endl
	   << "  child           = " << child << endl
	   << "  child.getParent = " << computed_parent << endl
	   << "  parent          = " << parent << endl;
      exit(1);
    }

  // The child index computed by getParent should be the same as the
  // index appended.
  if(computed_child_index != child_index)
    {
      cerr << "child index from child.getParent wrong:" << endl
	   << "  child          = " << child << endl
	   << "  parent         = " << parent << endl
	   << "  appended index = " << child_index << endl
	   << "  computed index = " << computed_child_index << endl;
      exit(1);
    }
}

// Generate a random number with a specified number of significant
// bits.

unsigned int random_bits(int bits)
{
  // Get a random positive (31 bit) integer
  unsigned int result = 0;

  // Make sure we get a non-zero random number.
  while(!result)
  {
    // random only returns positive 32-bit integers, so we use a
    // second one to randomize the upper bit.
    unsigned int r1 = random();
    unsigned int r2 = random();

    result = (r1 ^ (r2 << 1));
  }

  // Right shift it to remove unwanted bits, but make sure we don't
  // shit off all non-zero bits.
  int shift = 32 - bits;
  while(((result >> shift) == 0) && (shift > 0))
    {
      shift--;
    }

  return (result >> shift);
}

// Generate LongIds by extending the specified base with random
// numbers up to max_bits in size.

unsigned int test(const LongId &base, int max_bits)
{
  unsigned int test_count = 0;
  LongId last = base;

  // Loop until the newly generated LongId won't fit.
  while(!(last == NullLongId))
    {
      // Chose a random number of bits to use to extend the LongId.
      int append_bits = (random() % max_bits) + 1;
      // Chose a random index.
      unsigned int index = random_bits(append_bits);
      // Extend the LongId
      LongId next = last.append(index);
      // If we didn't overflow, perform consistency checks.
      if(!(next == NullLongId))
	{
	  check(last, next, index);
	  test_count++;
	}
      last = next;
    }

  return test_count;
}

int main()
{
  // Initialize the random number generator
  srandom(time(0));

  // Used to cound the total number of LongIds generated
  unsigned int test_count = 0;

  for(int bits = 3; bits < 33; bits++)
    {
      test_count += test(RootLongId, bits);
      test_count += test(MutableRootLongId, bits);
      test_count += test(VolatileRootLongId, bits);
    }

  cout << test_count << " randomly generated LongIds passed" << endl
       << "LongId length limit reached " << max_length_count << " times"
       << endl;

  return 0;
}
