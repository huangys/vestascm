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

// Created on Sat Mar 28 23:02:19 PST 1998 by heydon

/* A test of the "PrefixTbl" class. */

#include <Basics.H>
#include <FS.H>
#include <ThreadIO.H>
#include <LimService.H>
#include <MultiSRPC.H>
#include "PrefixTbl.H"

using std::ofstream;
using std::ifstream;
using std::cout;
using std::cin;
using std::cerr;
using std::endl;
using OS::cio;
using std::ostream;

int do_write(const Text &fname);
int do_read(const Text &fname);
int do_tests();

Text program_name;
int verbose = 0;

void Usage()
{
    cerr << "Usage: " << program_name
	 <<  " [-v] [{-r|-w} filename]" << endl;
}

int main(int argc, char *argv[]) {
  bool do_debug_write = false, do_debug_read = false;
  Text debug_write_fname, debug_read_fname;

  program_name = argv[0];
  
  opterr = 0;
  for (;;)
    {
      char* slash;
      int c = getopt(argc, argv, "vr:w:");
      if (c == EOF) break;
      switch (c)
	{
	case 'v':
	  verbose++;
	  break;
	case 'r':
	  do_debug_read = true;
	  debug_read_fname = optarg;
	  break;
	case 'w':
	  do_debug_write = true;
	  debug_write_fname = optarg;
	  break;
	case '?':
	default:
	  Usage();
	  exit(1);
	}
    }

  if(optind != argc)
    {
      ostream &err = cio().start_err();
      err << "Ignoring extra command-line arguments: ";
      while (optind < argc)
	{
	  err << argv[optind++] << " ";
	}
      err << endl;
      cio().end_err();
    }

  if(do_debug_write)
    {
      return do_write(debug_write_fname);
    }
  else if(do_debug_read)
    {
      return do_read(debug_read_fname);
    }

  return do_tests();
}

void read_from_stdin(PrefixTbl &tbl, PrefixTbl::PutTbl &putTbl)
{
    ostream &out = cio().start_out();
    out << "Reading paths from standard input:" << endl;

    // read from stdin and create the table
    const int MaxLineLen = 1024;
    char line[MaxLineLen + 1];
    while (!cin.eof()) {
	cin.getline(line, MaxLineLen, '\n');
	FV2::T seq(line);
	PrefixTbl::index_t idx = tbl.Put(seq, /*INOUT*/ putTbl);
	out << "  " << line << " -> " << idx << endl;
    }
    out << endl;

    // print the table
    out << "Here is the resulting table:" << endl << endl;
    tbl.Print(out);
    out << endl;

    cio().end_out();
}

// Generate entries for a PrefixTbl until it has at least
// "target_size" entries.
void generate_table(PrefixTbl &tbl, PrefixTbl::PutTbl &putTbl,
		    Basics::uint32 target_size)
{
  // We use characters out of this array as path elements
  char *path_elems = ".abcdefghijlkmnopqrstuvwxyz0123456789ABCDEFGHIJLKMNOPQRSTUVWXYZ";
  unsigned int n_path_elems = strlen(path_elems);

  // We build up a path in this local buffer.  The three slashes and
  // the final null character are fixed.
  char path[8];
  path[1] = path[3] = path[5] = '/';
  path[7] = 0;

  ostream &out = (verbose > 2) ? cio().start_out() : cout;
  if(verbose > 2)
    out << "Generating paths:" << endl;

  for(unsigned int i1 = 0;
      (i1 < n_path_elems) &&
	(tbl.NumArcs() < target_size);
      i1++)
    {
      path[0] = path_elems[i1];
      for(unsigned int i2 = 0;
	  (i2 < n_path_elems) &&
	    (tbl.NumArcs() < target_size);
	  i2++)
	{
	  path[2] = path_elems[(i2+1) % n_path_elems];
	  for(unsigned int i3 = 0;
	      (i3 < n_path_elems) &&
		(tbl.NumArcs() < target_size);
	      i3++)
	    {
	      path[4] = path_elems[(i3+2) % n_path_elems];
	      for(unsigned int i4 = 0;
		  (i4 < n_path_elems) &&
		    (tbl.NumArcs() < target_size);
		  i4++)
		{
		  path[6] = path_elems[(i4+3) % n_path_elems];

		  // Put this new path into the table
		  PrefixTbl::index_t idx = tbl.Put(path, putTbl);
		  if(verbose > 2)
		    out << "  " << path << " -> " << idx << endl;
		}
	    }
	}
    }

  if(verbose > 2)
    cio().end_out();
} 

int do_write(const Text &fname)
{
    PrefixTbl tbl(/*sizeHint=*/ 10);
    PrefixTbl::PutTbl tempTbl;
    read_from_stdin(tbl, tempTbl);

    try {
	// write the table out to disk
        {
	  ostream &out = cio().start_out();
	  out << "Pickling table to " << fname << "..." << endl << endl;
	  cio().end_out();
	}
	ofstream ofs;
	FS::OpenForWriting(fname, /*OUT*/ ofs);
	tbl.Write(ofs);
	FS::Close(ofs);

	// read it back in and print it
	ifstream ifs;
	FS::OpenReadOnly(fname, /*OUT*/ ifs);
	PrefixTbl newTbl(ifs);
	if (!FS::AtEOF(ifs)) {
	  cio().start_err()
	    << "Error: stream not at end-of-file as expected!" << endl
	    << "Final file position = " << FS::Posn(ifs) << endl;
	  cio().end_err();
	}
	FS::Close(ifs);
	if(newTbl != tbl)
	  {
	    cio().start_err()
	       << "Table read from disk differs from the one we wrote!"
	       << endl;
	    cio().end_err();
	    return 1;
	  }
	{
	  ostream &out = cio().start_out();
	  out << "Here is the unpickled version of the table:" << endl << endl;
	  newTbl.Print(out);
	  cio().end_out();
	}
    } catch (const FS::Failure &f) {
      cio().start_err()
	 << "Error pickling table: " << f << "; exiting..." << endl;
      cio().end_err();
      return 1;
    } catch (const FS::EndOfFile &f) {
      cio().start_err()
	<< "Unexpected end-of-file reading table: " << f << "; exiting..." << endl;
      cio().end_err();
      return 1;
    }
    return 0;
}

int do_read(const Text &fname)
{
  bool leftover = false;
  try
    {
      // read it back in and print it
      ifstream ifs;
      FS::OpenReadOnly(fname, /*OUT*/ ifs);
      PrefixTbl newTbl(ifs);
      if (!FS::AtEOF(ifs)) {
	cio().start_err()
	  << "Error: stream not at end-of-file as expected!" << endl
	  << "Final file position = " << FS::Posn(ifs) << endl;
	cio().end_err();
	leftover = true;
      }
      FS::Close(ifs);
      {
	ostream &out = cio().start_out();
	out << "Here is the unpickled version of the table:" << endl << endl;
	newTbl.Print(out);
	out << endl
	    << "Here are the paths represented by each index of the table:" << endl << endl;
	for(int i = 0; i < newTbl.NumArcs(); i++)
	  {
	    char path[2048];
	    if(newTbl.GetString(i, path, sizeof(path)))
	      {
		out << i << " : " << path << endl;
	      }
	  }
	cio().end_out();
      }
    }
  catch (const FS::Failure &f)
    {
      cio().start_err()
	<< "Error reading table: " << f << "; exiting..." << endl;
      cio().end_err();
      return 1;
    }
  catch (const FS::EndOfFile &f)
    {
      cio().start_err()
	<< "Unexpected end-of-file reading table: " << f << "; exiting..." << endl;
      cio().end_err();
      return 1;
    }
  return leftover ? 1 : 0;
}

std::ostream& operator << (std::ostream &os, const SRPC::failure &f) throw ()
/* Print a "SRPC::failure" object to "os" .*/
{
    int i;
    for (i = 0; i < 70; i++) os << '*'; os << endl;
    os << "SRPC failure (error " << f.r << ") :" << endl;
    os << "  " << f.msg << endl;
    for (i = 0; i < 70; i++) os << '*'; os << endl << endl;
    return os;
}

const PrefixTbl *server_compare_tbl = 0;

const int IntfVersion = 1;
const int ProcIdOld = 1;
const int ProcIdNew = 2;

class ServerHandler : public LimService::Handler
{
public:
  ServerHandler() { }

  void call(SRPC *srpc, int intfVersion, int procId) throw(SRPC::failure)
  {
    switch (procId) {
      case ProcIdOld:
	{
	  PrefixTbl arg;
	  arg.Recv(*srpc, true);
	  srpc->recv_end();
	  if(server_compare_tbl == 0)
	    {
	      srpc->send_failure(2,
				 "No table provided for compatison");
	    }
	  else if(arg != *server_compare_tbl)
	    {
	      srpc->send_failure(1,
				 "Received table doesn't match sent table");
	    }
	  // Empty result
	  srpc->send_end();
	}
	break;
      case ProcIdNew:
	{
	  PrefixTbl arg;
	  arg.Recv(*srpc);
	  srpc->recv_end();
	  if(server_compare_tbl == 0)
	    {
	      srpc->send_failure(2,
				 "No table provided for compatison");
	    }
	  else if(arg != *server_compare_tbl)
	    {
	      srpc->send_failure(1,
				 "Received table doesn't match sent table");
	    }
	  // Empty result
	  srpc->send_end();
	}
	break;
      default:
	assert(false);
    }
  }

  void call_failure(SRPC *srpc, const SRPC::failure &f) throw()
  {
    // Ignore when the client closes the connection
    if (f.r == SRPC::partner_went_away) return;

    // Ignore the "too large for old protocol" message we intend to
    // generate a few times
    if(f.msg.FindText("too large for old protocol") != -1)
      return;

    cio().start_err() << "(Server, in call) "  << f << endl;
    cio().end_err();
  }
  void accept_failure(const SRPC::failure &f) throw()
  {
    cio().start_err() << "(Server, accept) "  << f << endl;
    cio().end_err();
  }
  void other_failure(const char *msg) throw()
  {
    cio().start_err() << "(Server) "  << msg << endl;
    cio().end_err();
  }
  void listener_terminated() throw()
  {
    cio().start_err()
      << "(Server) Fatal error: unable to accept new connections; exiting"
      << endl;
    cio().end_err();
    abort();
  }
};

ServerHandler ls_handler;
LimService ls(1, ls_handler);

MultiSRPC client_multi;
const Text server_host("localhost");

void ClientCall(const PrefixTbl &arg, bool old_protocol)
  throw (SRPC::failure)
{
  SRPC *srpc;
  MultiSRPC::ConnId connId;
  connId = client_multi.Start(server_host, ls.IntfName(), /*OUT*/ srpc);

  server_compare_tbl = &arg;
  srpc->start_call(old_protocol ? ProcIdOld : ProcIdNew, IntfVersion);
  arg.Send(*srpc, old_protocol);
  srpc->send_end();

  // empty result
  srpc->recv_end();

  server_compare_tbl = 0;

  // close connection
  client_multi.End(connId);
}

Text temp_dir_suffix()
{
  static int i = 1;
  static pid_t pid = getpid();
  char buf[25];
  sprintf(buf, "-%d-%d", pid, i++);
  return Text(buf);
}

Text make_temp_dir()
{
  return FS::MakeTempDir("/tmp/TestPrefixTbl", temp_dir_suffix);
}

void remove_temp_dir(const Text &path)
{
  Text cmd = Text::printf("rm -rf %s", path.cchars());
  system(cmd.cchars());
}

int do_read_write_test(const Text &fname,
		       Basics::uint32 target_size)
{
  PrefixTbl tbl(/*sizeHint=*/ 100);
  if(target_size > 0)
    {
      PrefixTbl::PutTbl tempTbl;
      generate_table(tbl, tempTbl, target_size);
    }

  try {
    // write the table out to disk
    if(verbose)
      {
	ostream &out = cio().start_out();
	out << "Pickling table of size " << target_size << " to " << fname
	    << "..." << endl << endl;
	cio().end_out();
      }

    ofstream ofs;
    FS::OpenForWriting(fname, /*OUT*/ ofs);
    tbl.Write(ofs);
    FS::Close(ofs);

    // read it back in and print it
    ifstream ifs;
    FS::OpenReadOnly(fname, /*OUT*/ ifs);
    PrefixTbl newTbl(ifs);
    if (!FS::AtEOF(ifs)) {
      cio().start_err()
	<< "Error: stream not at end-of-file as expected!" << endl
	<< "Final file position = " << FS::Posn(ifs) << endl;
      cio().end_err();
      return 1;
    }
    FS::Close(ifs);
    if(newTbl != tbl)
      {
	cio().start_err()
	  << "Table read from disk differs from the one we wrote!"
	  << endl;
	cio().end_err();
	return 1;
      }
    if(verbose > 2)
      {
	ostream &out = cio().start_out();
	out << "Here is the unpickled version of the table:" << endl << endl;
	newTbl.Print(out);
	cio().end_out();
      }

    try
      {
	ClientCall(tbl, false);
      }
    catch(SRPC::failure f)
      {
	cio().start_err() << f;
	cio().end_err();
	return 1;
      }

    if(tbl.CanWriteOld())
      {
	// This table is small enough to be written in the old format.
	// Test that.
	Text old_fname = fname;
	old_fname += "-old";

        if(verbose)
	  {
	    ostream &out = cio().start_out();
	    out << "Pickling same table to " << fname
		<< " in old format..." << endl << endl;
	    cio().end_out();
	  }

	{
	  ofstream old_ofs;
	  FS::OpenForWriting(old_fname, /*OUT*/ old_ofs);
	  tbl.Write(old_ofs, true);
	  FS::Close(old_ofs);
	}
	{
	  // read it back in and print it
	  ifstream old_ifs;
	  FS::OpenReadOnly(old_fname, /*OUT*/ old_ifs);
	  PrefixTbl newTbl2(old_ifs, true);
	  if (!FS::AtEOF(old_ifs)) {
	    cio().start_err()
	      << "Error: stream not at end-of-file as expected!" << endl
	      << "Final file position = " << FS::Posn(old_ifs) << endl;
	    cio().end_err();
	  }
	  FS::Close(old_ifs);
	  if(newTbl2 != tbl)
	    {
	      cio().start_err()
		<< ("Table read from disk (old format) "
		    "differs from the one we wrote!")
		<< endl;
	      cio().end_err();
	      return 1;
	    }
	}

	// Send to the server in the old protocol
	try
	  {
	    ClientCall(tbl, true);
	  }
	catch(SRPC::failure f)
	  {
	    cio().start_err() << f;
	    cio().end_err();
	    return 1;
	  }
      }
    else
      {
	// Send to the server in the old protocol, which should fail
	// as the table is too big
	try
	  {
	    ClientCall(tbl, true);
	    cio().start_err()
	      << ("Expected a failure when sending with old network protocol, "
		  "but didn't get one!")
	      << endl;
	    cio().end_err();
	    return 1;
	  }
	catch(SRPC::failure f)
	  {
	    bool expected_error =
	      (f.msg.FindText("too large for old protocol") != -1);
	    if((verbose > 1) || !expected_error)
	      {
		cio().start_err() << f;
		cio().end_err();
	      }
	    if(!expected_error) return 1;
	  }
      }
  } catch (const FS::Failure &f) {
    cio().start_err() << "Error pickling table: " << f << "; exiting..." << endl;
    cio().end_err();
    return 1;
  } catch (const FS::EndOfFile &f) {
    cio().start_err() << "Unexpected end-of-file reading table: " << f << "; exiting..." << endl;
    cio().end_err();
    return 1;
  }
  return 0;
}

int do_tests()
{
  Basics::thread server_thread = ls.Forked_Run();

  sleep(1);

  Text temp_dir_name = make_temp_dir();

  // First test an empty PrefixTbl
  {
    Text fname = temp_dir_name+"/empty.PrefixTbl";
    if(do_read_write_test(fname, 0))
      return 1;
  }

  // Nexttest a small PrefixTbl
  {
    Text fname = temp_dir_name+"/small.PrefixTbl";
    if(do_read_write_test(fname, 1000))
      return 1;
  }

  // Now some that are right near the cross-over point from 16-bit to
  // 32-bit
  Basics::uint32 line = 1;
  line <<= 16;
  line -= 1;
  for(int offset = -2; offset <= 2; offset++)
    {
      Text fname;
      if(offset < 0)
	{
	  fname = Text::printf("%s/line%d.PrefixTbl",
			       temp_dir_name.cchars(), offset);
	}
      else if(offset == 0)
	{
	  fname = temp_dir_name+"/line.PrefixTbl";
	}
      else
	{
	  fname = Text::printf("%s/line+%d.PrefixTbl",
			       temp_dir_name.cchars(), offset);
	}
      
      if(do_read_write_test(fname, line + offset))
	return 1;
    }

  // Finally one that is way over the line
  {
    Text fname = temp_dir_name+"/large.PrefixTbl";
    if(do_read_write_test(fname, 100000))
      return 1;
  }

  cio().start_out()
    << "All tests passed!" << endl;
  cio().end_out();

  remove_temp_dir(temp_dir_name);

  return 0;
}

