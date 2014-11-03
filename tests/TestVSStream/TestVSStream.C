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

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <limits.h>

#include <iostream>
#include <fstream>

#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif

#include "VSStream.H"
#include "SourceOrDerived.H"

#include <ReposUI.H>

using std::cout;
using std::cerr;
using std::endl;
using std::ios;
using std::hex;
using std::dec;
using std::ifstream;
using std::fstream;

Text temp_root_path;
VestaSource* temp_root_vs = 0;

void print_stream_state(std::ostream &out, unsigned int state)
{
  bool first = true;
  if(state & ios::eofbit)
    {
      out << "eof";
      state &= ~((unsigned int) ios::eofbit);
      first = false;
    }
  if(state & ios::failbit)
    {
      if(!first)
	out << "|";
      out << "fail";
      state &= ~((unsigned int) ios::failbit);
      first = false;
    }
  if(state & ios::badbit)
    {
      if(!first)
	out << "|";
      out << "bad";
      state &= ~((unsigned int) ios::badbit);
      first = false;
    }
  if(state != 0)
    {
      if(!first)
	out << "|";
      out << "0x" << hex << state << dec;
    }
}

void print_state_if_not_good(const ios &stream,
			     const char *name, const char *marker)
{
  if(!stream.good())
    {
      cout << name << " state (" << marker << "): ";
      print_stream_state(cout, stream.rdstate());
      cout << endl;
    }  
}

static Text temp_suffix()
{
  static unsigned int counter = 0;

  unsigned int number = time(0) ^ getpid() ^ (counter++);

  char buff[40];
  int sres = sprintf(buff, "-%08x", number);

  assert(sres > 0);
  assert(sres < sizeof(buff));

  return buff;

}

static void copy_file(const Text &from, const Text &to)
{
  Text cmd = "cp ";
  cmd += from;
  cmd += " ";
  cmd += to;
  system(cmd.cchars());
}

#if !defined(MAX)
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#if !defined(MIN)
#define MIN(a, b) (((a) > (b)) ? (b) : (a))
#endif

#define MAX_READ_SIZE (BUFSIZ<<2)
#define SMALL_READ_SIZE (BUFSIZ>>2)

static bool verbose_read = false, verbose_write = false;

void random_read_test(const char *fname,
		      unsigned int count)
{
  // Copy the file to the repository
  Text fcopy_name = FS::TempFname(temp_root_path+"/TestVSStream-read_test", temp_suffix);
  copy_file(fname, fcopy_name);

  ifstream test_fstream(fcopy_name.cchars());
  VestaSource *fcopy_vs = ReposUI::filenameToVS(fcopy_name);
  IVSStream test_vsstream(fcopy_vs);

  struct stat stat_buf;
  if(stat(fcopy_name.cchars(), &stat_buf) != 0)
    {
      cerr << "fstat of \"" << fname << "\"" << endl;
      exit(1);
    }

  for(unsigned int i = 0; i < count; i++)
    {
      switch(random() % 3)
	{
	case 0:
	  // Perform a read of a random and possibly large size from
	  // both streams and compare the results.
	  {
	    char f_buf[MAX_READ_SIZE], vs_buf[MAX_READ_SIZE];

	    unsigned int rand_read_size = (random() % MAX_READ_SIZE)+1;

	    if(verbose_read)
	      cout << "R[" << i << "]: Read " << rand_read_size
		   << " bytes at offset " << test_fstream.tellg() << endl;

	    test_fstream.read(f_buf, rand_read_size);
	    long f_count = test_fstream.gcount();
	    test_vsstream.read(vs_buf, rand_read_size);
	    long vs_count = test_vsstream.gcount();
	    assert(f_count == vs_count);
	    assert(memcmp(f_buf, vs_buf, f_count) == 0);
	  }
	  break;
	case 1:
	  // Perform a read of a random but small size from both
	  // streams and compare the results.
	  {
	    char f_buf[SMALL_READ_SIZE], vs_buf[SMALL_READ_SIZE];

	    unsigned int rand_read_size = (random() % SMALL_READ_SIZE)+1;

	    if(verbose_read)
	      cout << "R[" << i << "]: Read " << rand_read_size
		   << " bytes at offset " << test_fstream.tellg() << endl;

	    test_fstream.read(f_buf, rand_read_size);
	    long f_count = test_fstream.gcount();
	    test_vsstream.read(vs_buf, rand_read_size);
	    long vs_count = test_vsstream.gcount();
	    assert(f_count == vs_count);
	    assert(memcmp(f_buf, vs_buf, f_count) == 0);
	  }
	  break;
	case 2:
	  {
	    unsigned int rand_file_pos = (random() % stat_buf.st_size);

	    if(verbose_read)
	      cout << "R[" << i << "]: Seek to " << rand_file_pos
		   << endl;

	    test_fstream.seekg(rand_file_pos);
	    long f_pos = test_fstream.tellg();
	    test_vsstream.seekg(rand_file_pos);
	    long vs_pos = test_vsstream.tellg();
	    assert(f_pos == vs_pos);
	  }
	  break;
	}
    }

  cout << "R(" << count <<"): "
       << test_vsstream.read_calls() << " read calls" << endl;

  FS::Delete(fcopy_name);
}

void compare_files(const char *fname1, const char *fname2)
{
  struct stat stat_buf1, stat_buf2;
  if(stat(fname1, &stat_buf1) != 0)
    {
      cerr << "stat of \"" << fname1 << "\"" << endl;
      exit(1);
    }
  if(stat(fname2, &stat_buf2) != 0)
    {
      cerr << "stat of \"" << fname2 << "\"" << endl;
      exit(1);
    }
  assert(stat_buf1.st_size == stat_buf2.st_size);

  ifstream stream1(fname1), stream2(fname2);

  while(!stream1.eof())
    {
      char buf1[MAX_READ_SIZE], buf2[MAX_READ_SIZE];
      stream1.read(buf1, MAX_READ_SIZE);
      int count1 = stream1.gcount();
      stream2.read(buf2, MAX_READ_SIZE);
      int count2 = stream2.gcount();
      assert(count1 == count2);
      if(memcmp(buf1, buf2, count1) != 0)
	{
	  cerr << "Files \"" << fname1 << "\" and \"" << fname2 << "\" differ"
	       << endl;
	  for(unsigned int i = 0; i < count1; i++)
	    {
	      if(buf1[i] != buf2[i])
		{
		  cerr << "Differences start at offset "
		       << (((int) stream1.tellg()) - count1 + i) << endl;
		  abort();
		}
	    }
	}
    }
  assert(stream2.eof());
}

void random_read_write_test(const char *source_fname,
			    const char *dest_fname_base,
			    unsigned int count)
{
  ifstream source(source_fname);
  struct stat stat_buf;
  if(stat(source_fname, &stat_buf) != 0)
    {
      cerr << "stat of \"" << source_fname << "\"" << endl;
      exit(1);
    }

  Text f_name = "TestVSStream.fstream_rw";
  f_name += temp_suffix();

  Text vs_name = "TestVSStream.vsstream_rw";
  vs_name += temp_suffix();

  Text f_path = temp_root_path + "/" + f_name;
  Text vs_path = temp_root_path + "/" + vs_name;

  {
    fstream test_fstream(f_path.cchars(), ios::in|ios::out|ios::trunc);

    VestaSource* temp_vs = NULL;
    VestaSource::errorCode err = 
      temp_root_vs->insertMutableFile(vs_name.cchars(), NullShortId, true,
				      NULL, VestaSource::replaceDiff, &temp_vs);
    assert(temp_vs != NULL);
    
    VSStream test_vsstream(temp_vs);

    // The maximum position in the files we've written so far.
    unsigned long max_written = 0;

    // What the current seek position in the files should be.
    unsigned long current_pos = 0;

    enum { op_seek, op_read, op_write } last_op = op_seek;

    for(unsigned int i = 0; i < count; i++)
      {
	bool f_good = test_fstream.good(), vs_good = test_vsstream.good();
	if((f_good && !vs_good) || (!f_good && vs_good))
	  {
	    cerr << "RW[" << i << "]: only one stream is 'good'" << endl
		 << "  fstream  : ";
	    print_stream_state(cerr, test_fstream.rdstate());
	    cerr << endl
		 << "  vsstream : ";
	    print_stream_state(cerr, test_vsstream.rdstate());
	    cerr << endl;
	    abort();
	  }
      
	// Pick one of several things to do at random.
	unsigned int random_op = random() % 6;
	unsigned int chosen_op = random_op;
	if(random_op > 2)
	  {
	    // Some of the time, follow a write by another write or a
	    // read by another read
	    if(last_op == op_write)
	      {
		chosen_op = 2;
	      }
	    else
	      {
		// Last was wither a read or a seek, so do a read
		chosen_op = 0;
	      }
	  }
	switch(random_op)
	  {
	  case 0:
	    // Perform a read of a random size from both streams and
	    // compare the results.
	    {
	      // Pick an upper limit for the read that's either likely
	      // to be larger than or smaller than the in-memory
	      // buffer.
	      unsigned long read_upper_bound = ((random() % 1)
						? MAX_READ_SIZE
						: SMALL_READ_SIZE);

	      // Figure out how much we can realistically read at this
	      // point.
	      unsigned long max_read_size =
		MIN(read_upper_bound,
		    max_written - current_pos);

	      if(max_read_size > 0)
		{
		  unsigned int rand_read_size = (random() % max_read_size)+1;

		  if(verbose_write)
		    cout << "RW[" << i << "]: Read " << rand_read_size
			 << " bytes at offset " << current_pos
			 << endl;

		  char f_buf[MAX_READ_SIZE], vs_buf[MAX_READ_SIZE];

		  if(last_op == op_write)
		    {
		      // Make sure we start reading from where we
		      // finished writing
		      test_fstream.seekg(test_fstream.tellp());
		      test_vsstream.seekg(test_vsstream.tellp());
		    }
		  test_fstream.read(f_buf, rand_read_size);
		  int f_count = test_fstream.gcount();
		  test_vsstream.read(vs_buf, rand_read_size);
		  int vs_count = test_vsstream.gcount();
		  assert(f_count == vs_count);
		  if(memcmp(f_buf, vs_buf, f_count) != 0)
		    {
		      cerr << "RW[" << i << "]: " << rand_read_size
			   << " bytes read at offset " << current_pos
			   << " don't match."
			   << endl;
		      for(unsigned int i = 0; i < f_count; i++)
			{
			  if(f_buf[i] != vs_buf[i])
			    {
			      cerr
				<< "Differences in this read start at offset "
				<< (current_pos + i) 
				<< ":" << endl
				<< "  fstream[" << (current_pos + i) << "]  == "
				<< hex << (((unsigned int) f_buf[i]) & 0x00ff) << dec << endl
				<< "  vsstream[" << (current_pos + i) << "] == "
				<< hex << (((unsigned int) vs_buf[i]) & 0x00ff) << dec << endl
				<< "(Actual differences may start earlier.)"
				<< endl;
			      abort();
			    }
			}
		    }

		  // We moved forward that many bytes.
		  current_pos += vs_count;

		  last_op = op_read;

		  break;
		}
	    }
	    // Note: fall through if we haven't written enough yet.
	  case 1:
	    if(max_written > 0)
	      {
		// Seek to a random location.
		unsigned int rand_file_pos = (random() % max_written);
		if(verbose_write)
		  {
		    cout << "RW[" << i << "]: Seek to " << rand_file_pos
			 << endl;
		  }
		test_fstream.seekg(rand_file_pos);
		test_vsstream.seekg(rand_file_pos);
		test_fstream.seekp(rand_file_pos);
		test_vsstream.seekp(rand_file_pos);

		// We're now at this position.
		current_pos = rand_file_pos;

		last_op = op_seek;

		break;
	      }
	    // Note: fall through if we haven't written anything yet.
	  case 2:
	    {
	      // Perform a read of a random size from the source and
	      // write it to both streams.
	      char read_buf[MAX_READ_SIZE];

	      unsigned long read_upper_bound = ((random() % 1)
						? MAX_READ_SIZE
						: SMALL_READ_SIZE);

	      unsigned int rand_read_size = (random() % read_upper_bound)+1;
	      unsigned long rand_read_start = (random() %
					       (stat_buf.st_size - rand_read_size));
	      source.seekg(rand_read_start);
	      source.read(read_buf, rand_read_size);
	      int read_count = source.gcount();

	      if(verbose_write)
		{
		  cout << "RW[" << i << "]: Write " << read_count
		       << " bytes from source offset " << rand_read_start
		       << " at offset " << current_pos << endl;
		}

	      if(last_op == op_read)
		{
		  // Make sure we start writing where we finished
		  // reading
		  test_fstream.seekp(test_fstream.tellg());
		  test_vsstream.seekp(test_fstream.tellg());
		}
	      test_fstream.write(read_buf, read_count);
	      test_vsstream.write(read_buf, read_count);

	      // We moved forward that many bytes.
	      current_pos += read_count;

	      // Update out idea of the end-of-file position.
	      if(current_pos > max_written)
		max_written = current_pos;
	    }

	    last_op = op_write;

	    break;
	  }

	// With some probability, compare the stream positions.  We
	// avoid doing this on every operation, as these may cause
	// flushes to hapen, which could cause things to be exercised
	// less.
	unsigned int random_compare = random() % 4;
	if(random_compare == 0)
	  {
	    if(last_op == op_read)
	      {
		if(verbose_write)
		  cout << "RW[" << i << "]: Check get position" << endl;
		unsigned long vs_pos = test_vsstream.tellg();
		assert(vs_pos == current_pos);
	      }
	    else if(last_op == op_write) 
	      {
		if(verbose_write)
		  cout << "RW[" << i << "]: Check put position" << endl;
		unsigned long vs_pos = test_vsstream.tellp();
		assert(vs_pos == current_pos);
	      }
	  }
      }

    cout << "RW(" << count <<"): "
	 << test_vsstream.read_calls() << " read calls, "
	 << test_vsstream.write_calls() << " write calls, "
	 << endl;
  }

  // Make sure the files turned out the same.
  compare_files(f_path.cchars(), vs_path.cchars());

  FS::Delete(f_path);
  FS::Delete(vs_path);
}

int main(int argc, char* argv[])
{
  Text program_name = argv[0];
  unsigned int seed = time(0);
  unsigned int count = 10000;
  for (;;)
    {
      char* slash;
      int c = getopt(argc, argv, "s:c:vRW");
      if (c == EOF) break;
      switch (c)
      {
      case 's':
	seed = strtoul(optarg, NULL, 0);
	break;
      case 'c':
	count = strtoul(optarg, NULL, 0);
	break;
      case 'v':
	verbose_read = true;
	verbose_write = true;
	break;
      case 'R':
	verbose_read = true;
	break;
      case 'W':
	verbose_write = true;
	break;
      }
    }

  cout << "To reproduce: -s " << seed
       << " -c " << count << endl;
  srandom(seed);

  if(optind >= argc)
    {
      cerr << program_name
	   << ": One or more files of input data required on command line"
	   << endl;
      exit(3);
    }

  try
    {
      // Get the directory where we'll place temporary files
      temp_root_path =
	VestaConfig::get_Text("UserInterface", "TempDir");

      temp_root_vs =
	ReposUI::filenameToVS(temp_root_path);

      for(unsigned int i = optind; i < argc; i++)
	{
	  random_read_test(argv[i], count);
	  random_read_write_test(argv[i],
				 argv[i],
				 count);
	}
    }
  catch (VestaConfig::failure f)
    {
      cerr << program_name << ": " << f.msg << endl;
      exit(2);
    }
  catch (SRPC::failure f)
    {
      cerr << program_name
	   << ": SRPC failure; " << f.msg << " (" << f.r << ")" << endl;
      exit(2);
    }
  catch (ReposUI::failure f)
    {
      cerr << program_name << ": " << f.msg << endl;
      exit(2);
    }
  catch(FS::Failure f)
    {
      cerr << program_name << ": " << f << endl;
      exit(2);
    }

  return 0;
}
