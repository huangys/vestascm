// Copyright (C) 2006, Vesta Free Software Project
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

#include <Basics.H>
#include <BufStream.H>

#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif

#include "FPStream.H"
#include "UniqueId.H"

using std::cout;
using std::cerr;
using std::endl;
using std::flush;
using std::ostream;
using Basics::OBufStream;
using FP::FPStream;

bool verbose = false;

void write_nbytes(unsigned int len, ostream &stream1, ostream &stream2)
{
  char *buf = NEW_PTRFREE_ARRAY(char, len);
  if(verbose)
    {
      cout << "Writing " << len << " bytes = {";
    }
  for(unsigned int i = 0; i < len; i++)
    {
      char c = (((unsigned int) random()) % 255);
      if(verbose)
	{
	  if(i > 0)
	    cout << ", ";
	  cout << (unsigned int) ((unsigned char) c);
	}
      buf[i] = c;
    }
  if(verbose)
    {
      cout << "}" << endl;
    }
  stream1.write(buf, len);
  stream2.write(buf, len);
  delete [] buf;
}

void write_once(ostream &stream1, ostream &stream2)
{
  switch(((unsigned int) random()) % 4)
    {
    case 0:
      {
	// A random single character
	char c = (((unsigned int) random()) % 255);
	if(verbose)
	  {
	    cout << "Writing single byte = "
		 << (unsigned int) ((unsigned char) c) << endl;
	  }
	stream1.put(c);
	stream2.put(c);
      }
      break;
    case 1:
      {
	// A random integer (formatted as ASCII)
	int i = random();
	if(verbose)
	  {
	    cout << "Writing integer = \"" << i << "\"" << endl;
	  }
	stream1 << i;
	stream2 << i;
      }
      break;
    case 2:
      {
	// A string of random bytes up to one Word in length
	unsigned int len = (((unsigned int) random()) % sizeof(Word)) + 1;
	write_nbytes(len, stream1, stream2);
      }
      break;
    case 3:
      {
	// A string of random bytes more than one Word in length
	unsigned int len = (((unsigned int) random()) % BUFSIZ) + sizeof(Word);
	write_nbytes(len, stream1, stream2);
      }
      break;
    }
}

void do_one_test(unsigned int maxwrites)
{
  // We'll start with a base tag from UniqueId
  FP::Tag base = UniqueId();

  // Will we use the base tag at all?
  bool use_base = (((unsigned int) random()) % 2);

  OBufStream buf_stream;

  FPStream *fp_stream;
  if(use_base)
    {
      fp_stream = NEW_CONSTR(FPStream, (base));
    }
  else
    {
      fp_stream = NEW(FPStream);
    }

  unsigned int nwrites = (((unsigned int) random()) % maxwrites) + 4;

  for(unsigned int i = 0; i < nwrites; i++)
    {
      write_once(buf_stream, *fp_stream);
    }

  unsigned int len = buf_stream.tellp();

  FP::Tag buf_tag, fp_tag;
  if(use_base)
    {
      buf_tag = base;
      buf_tag.Extend(buf_stream.str(), len);
    }
  else
    {
      buf_tag = FP::Tag(buf_stream.str(), len);
    }

  fp_tag = fp_stream->tag();
  unsigned int fp_count = fp_stream->tellp();

  if(fp_count != len)
    {
      cerr << "Length mismatch:" << endl
	   << "\t" << len << endl
	   << "\t" << fp_count << endl;
      exit(1);
    }

  if(buf_tag != fp_tag)
    {
      cerr << "Mismatch (total bytes " << len << "):" << endl
	   << "\t" << buf_tag << endl
	   << "\t" << fp_tag << endl;
      exit(1);
    }

  delete fp_stream;
}

Text program_name;

void usage()
{
  cerr << "Usage: " << program_name << endl
       << "\t[-s seed-vale]   Random seed" << endl
       << "\t[-c test-count]  Number of tests to perform" << endl
       << "\t[-w write-count] Number of writes per test" << endl
       << "\t[-v]             Verbose: describe each write" << endl;
  exit(1);
}

int main(int argc, char* argv[])
{
  program_name = argv[0];
  unsigned int seed = time(0);
  unsigned int count = 100;
  unsigned int maxwrites = 1000;
  for (;;)
    {
      char* slash;
      int c = getopt(argc, argv, "s:c:w:v");
      if (c == EOF) break;
      switch (c)
      {
      case 's':
	seed = strtoul(optarg, NULL, 0);
	break;
      case 'c':
	count = strtoul(optarg, NULL, 0);
	break;
      case 'w':
	maxwrites = strtoul(optarg, NULL, 0);
	break;
      case 'v':
	verbose = true;
	break;
      case '?':
      default:
	usage();
      }
    }

  cout << "To reproduce: -s " << seed
       << " -c " << count
       << " -w " << maxwrites  << endl;
  srandom(seed);

  for(unsigned int i = 0; i < count; i++)
    {
      do_one_test(maxwrites);
    }

  cout << "All tests passed!" << endl;

  return 0;
}
