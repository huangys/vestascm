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

#include "VSStream.H"
#include "SourceOrDerived.H"

#include <ReposUI.H>

#include <Timer.H>

#include <FdStream.H>

using std::cout;
using std::cerr;
using std::endl;
using std::ios;
using std::hex;
using std::dec;
using std::ifstream;
using std::ofstream;
using std::fstream;
using FS::OFdStream;
using FS::IFdStream;

// The temporary directory to do our work intaken from the setting
// [UserInterface]TempDir.  (Normally this is "/vesta-work/.tmp".)
Text temp_root_path;
VestaSource* temp_root_vs = 0;

// Random seed re-used for each test (so that we do the same
// psuedo-random thing which each type of stream)
unsigned int seed;

// Default file size to use in the tests
unsigned int file_size = (BUFSIZ<<5) + (BUFSIZ>>2);

// Write bytes to a stream until some limit is reached.  The exact
// size of different writes is randomized.
void write_bytes(std::ostream &out, unsigned int size)
{
  unsigned int written = 0;
  while(written < size)
    {
      switch(random() % 5)
	{
	case 0:
	case 1:
	  // Write one integer of bytes 
	  {
	    unsigned int data = 0xaa55a55a;
	    out.write((char *) &data, sizeof(data));
	    written += sizeof(data);
	  }
	  break;
	case 2:
	  // Write a random number of bytes smaller thn a buffer
	  {
	    unsigned int bytes = (random() % 128) + 1;
	    char buf[128];
	    for(unsigned int i = 0; i < bytes; i++)
	      {
		buf[i] = random();
	      }
	    out.write(buf, bytes);
	    written += bytes;
	  }
	  break;
	case 3:
	case 4:
	  // Write a single byte
	  {
	    char b = 0xa5;
	    out << b;
	    written++;
	  }
	  break;
	}
    }
}

// Read bytes from a stream until we reach the end of file.  The exact
// size of different reads is randomized.
unsigned int read_to_eof(std::istream &in)
{
  unsigned int total = 0;
  while(!in.eof())
    {
      switch(random() % 5)
	{
	case 0:
	case 1:
	  // Read one integer of bytes 
	  {
	    unsigned int data;
	    in.read((char *) &data, sizeof(data));
	  }
	  break;
	case 2:
	  // Read a random number of bytes smaller thn a buffer
	  {
	    unsigned int bytes = (random() % 128) + 1;
	    char buf[128];
	    in.read(buf, bytes);
	  }
	  break;
	case 3:
	case 4:
	  // Read a single byte
	  {
	    char b;
	    in >> b;
	  }
	  break;
	}
      total += in.gcount();;
    }
  return total;
}

// Prepare a file of a specified length to use in the read test.
void prepare_file(const Text &path, unsigned int size)
{
  ofstream out(path.cchars());
  unsigned int written = 0;
  unsigned int data = 1;
  while(written < size)
    {
      out.write((char *) &data, sizeof(data));
      written += sizeof(data);
      data++;
    }
}

// Generate a filename suffix for use in the temporary filename.
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
      char buf1[BUFSIZ], buf2[BUFSIZ];
      stream1.read(buf1, BUFSIZ);
      int count1 = stream1.gcount();
      stream2.read(buf2, BUFSIZ);
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

// Perform a test of sequential writing to several different stream
// types, comparing their performance.
void linear_write_test()
{
  Text f_name = "TestVSStream-wf";
  f_name += temp_suffix();

  Text fd_name = "TestVSStream-wfd";
  fd_name += temp_suffix();

  Text vs_name = "TestVSStream-wv";
  vs_name += temp_suffix();

  Text f_path = temp_root_path + "/" + f_name;
  Text fd_path = temp_root_path + "/" + fd_name;
  Text vs_path = temp_root_path + "/" + vs_name;

  cout << "Linear write test:" << endl;

  Timer::MicroSecs fstream_msecs;
  {
    srandom(seed);
    Timer::T timer;
    timer.Start();
    {
      ofstream test_fstream(f_path.cchars(), ios::out|ios::trunc);
      write_bytes(test_fstream, file_size);
      test_fstream.flush();
      test_fstream.close();
    }
    fstream_msecs = timer.Stop();
    double secs = fstream_msecs;
    secs /= 1000000;
    char sec_buf[10];
    sprintf(sec_buf, "%.2g", secs);
    cout << "  fstream write: " << sec_buf << "s" << endl;
  }

  Timer::MicroSecs fdstream_msecs;
  {
    srandom(seed);
    Timer::T timer;
    timer.Start();
    {
      OFdStream test_fdstream(fd_path.cchars(), ios::out|ios::trunc);
      write_bytes(test_fdstream, file_size);
      test_fdstream.flush();
      test_fdstream.close();
    }
    fdstream_msecs = timer.Stop();
    double secs = fdstream_msecs;
    secs /= 1000000;
    char sec_buf[10];
    sprintf(sec_buf, "%.2g", secs);
    cout << "  FdStream write: " << sec_buf << "s" << endl;
  }

  Timer::MicroSecs vsstream_msecs;
  {
    srandom(seed);
    VestaSource* temp_vs = NULL;
    VestaSource::errorCode err = 
      temp_root_vs->insertMutableFile(vs_name.cchars(), NullShortId, true,
				      NULL, VestaSource::replaceDiff, &temp_vs);
    assert(temp_vs != NULL);

    unsigned int write_calls;
    Timer::T timer;
    timer.Start();
    {
      OVSStream test_vsstream(temp_vs);
      write_bytes(test_vsstream, file_size);
      write_calls = test_vsstream.write_calls();
    }
    vsstream_msecs = timer.Stop();
    double secs = vsstream_msecs;
    secs /= 1000000;
    char sec_buf[10];
    sprintf(sec_buf, "%.2g", secs);
    cout << "  VSStream write: " << sec_buf << "s" << endl;
    cout << "  VSStream write calls: " << write_calls << endl;

    delete temp_vs;
  }

  char ratio_buf[10];
  double ratio = vsstream_msecs;
  ratio /= fstream_msecs;
  sprintf(ratio_buf, "%.2g", ratio);
  cout << "  VSStream:fstream slowdown ratio: " << ratio_buf << endl;

  ratio = vsstream_msecs;
  ratio /= fdstream_msecs;
  sprintf(ratio_buf, "%.2g", ratio);
  cout << "  VSStream:FdStream slowdown ratio: " << ratio_buf << endl;

  compare_files(f_path.cchars(), vs_path.cchars());

  FS::Delete(f_path);
  FS::Delete(fd_path);
  FS::Delete(vs_path);
}

// Perform a test of sequential reading from several different stream
// types, comparing their performance.
void linear_read_test()
{
  Text f_name = "TestVSStream-r";
  f_name += temp_suffix();

  Text f_path = temp_root_path + "/" + f_name;

  VestaSource* temp_vs = NULL;
  VestaSource::errorCode err = 
    temp_root_vs->insertMutableFile(f_name.cchars(), NullShortId, true,
				    NULL, VestaSource::replaceDiff, &temp_vs);
  assert(temp_vs != NULL);

  prepare_file(f_path, file_size);

  cout << "Linear read test:" << endl;

  Timer::MicroSecs fstream_msecs;
  unsigned int fstream_read_count;
  {
    srandom(seed);
    Timer::T timer;
    timer.Start();
    {
      ifstream test_fstream(f_path.cchars());
      fstream_read_count = read_to_eof(test_fstream);
    }
    fstream_msecs = timer.Stop();
    double secs = fstream_msecs;
    secs /= 1000000;
    char sec_buf[10];
    sprintf(sec_buf, "%.2g", secs);
    cout << "  fstream: " << sec_buf << "s" << endl;
  }

  Timer::MicroSecs fdstream_msecs;
  unsigned int fdstream_read_count;
  {
    srandom(seed);
    Timer::T timer;
    timer.Start();
    {
      IFdStream test_fdstream(f_path.cchars());
      fdstream_read_count = read_to_eof(test_fdstream);
    }
    fdstream_msecs = timer.Stop();
    double secs = fdstream_msecs;
    secs /= 1000000;
    char sec_buf[10];
    sprintf(sec_buf, "%.2g", secs);
    cout << "  FdStream: " << sec_buf << "s" << endl;
  }

  Timer::MicroSecs vsstream_msecs;
  unsigned int vsstream_read_count;
  {
    srandom(seed);

    unsigned int read_calls;
    Timer::T timer;
    timer.Start();
    {
      IVSStream test_vsstream(temp_vs);
      vsstream_read_count = read_to_eof(test_vsstream);
      read_calls = test_vsstream.read_calls();
    }
    vsstream_msecs = timer.Stop();
    double secs = vsstream_msecs;
    secs /= 1000000;
    char sec_buf[10];
    sprintf(sec_buf, "%.2g", secs);
    cout << "  VSStream: " << sec_buf << "s" << endl;
    cout << "  VSStream read calls: " << read_calls << endl;
  }

  char ratio_buf[10];
  double ratio = vsstream_msecs;
  ratio /= fstream_msecs;
  sprintf(ratio_buf, "%.2g", ratio);
  cout << "  VSStream:fstream slowdown ratio: " << ratio_buf << endl;

  ratio = vsstream_msecs;
  ratio /= fdstream_msecs;
  sprintf(ratio_buf, "%.2g", ratio);
  cout << "  VSStream:fdstream slowdown ratio: " << ratio_buf << endl;

  if(vsstream_read_count != fstream_read_count)
    cout << "  read count mismatch:" << endl
	 << "    fstream read count: " << fstream_read_count << endl
	 << "    VSStream read count: " << vsstream_read_count << endl;

  delete temp_vs;
  FS::Delete(f_path);
}

int main(int argc, char* argv[])
{
  Text program_name = argv[0];
  unsigned int seed = time(0);

  for (;;)
    {
      char* slash;
      int c = getopt(argc, argv, "s:c:");
      if (c == EOF) break;
      switch (c)
      {
      case 's':
	seed = strtoul(optarg, NULL, 0);
	break;
      case 'c':
	file_size = strtoul(optarg, NULL, 0);
	break;
      }
    }

  cout << "To reproduce: -s " << seed << " -c " << file_size << endl;

  try
    {
      // Get the directory where we'll place temporary files
      temp_root_path =
	VestaConfig::get_Text("UserInterface", "TempDir");

      temp_root_vs =
	ReposUI::filenameToVS(temp_root_path);

      linear_write_test();
      linear_read_test();
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


