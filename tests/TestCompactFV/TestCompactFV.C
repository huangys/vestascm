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

// Created on Mon Mar 30 18:32:29 PST 1998 by heydon

/* A test of the "CompactFV::List" class. */

#include <Basics.H>
#include <FS.H>
#include <ThreadIO.H>
#include <LimService.H>
#include <MultiSRPC.H>
#include "FV.H"
#include "CompactFV.H"

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

int do_write(const Text &fname)
{
    // read from stdin and create the table
    const int MaxLineLen = 1024;
    char line[MaxLineLen + 1];
    FV::ListApp names;
    cout << "Adding paths:" << endl;
    while (cin.peek() != EOF) {
	cin.getline(line, MaxLineLen, '\n');
	FV::T path(line);
	(void) names.Append(path);
	cout << "  " << line << endl;
    }
    cout << endl;

    // print the table
    cout << "Here are the resulting names in compact form:" << endl << endl;
    CompactFV::List names2(names);
    names2.Print(cout, /*indent=*/ 2);
    cout << endl;

    try {
	// write the table out to disk
	ofstream ofs;
	FS::OpenForWriting(fname, /*OUT*/ ofs);
	names2.Write(ofs);
	FS::Close(ofs);

	// read it back in and print it
	ifstream ifs;
	FS::OpenReadOnly(fname, /*OUT*/ ifs);
	CompactFV::List newNames2(ifs);
	if (!FS::AtEOF(ifs)) {
	    cerr << "Error: stream not at end-of-file as expected!" << endl;
	}
	FS::Close(ifs);
	if(newNames2 != names2)
	  {
	    cerr << "Table read from disk differs from the one we wrote!"
		 << endl;
	    return 1;
	  }
	cout << "Here is the unpickled version of the names:" << endl << endl;
	newNames2.Print(cout, /*indent=*/ 2);
    } catch (const FS::Failure &f) {
	cerr << "Error pickling table: " << f << "; exiting..." << endl;
	return 1;
    } catch (const FS::EndOfFile &f) {
      cerr << "Unexpected end-of-file reading table: " << f << "; exiting..." << endl;
      return 1;
    }
    return 0;
}

int do_read(const Text &fname)
{
    try {
	// read the file in and print it
	ifstream ifs;
	FS::OpenReadOnly(fname, /*OUT*/ ifs);
	CompactFV::List newNames2(ifs);
	if (!FS::AtEOF(ifs)) {
	    cerr << "Error: stream not at end-of-file as expected!" << endl;
	}
	FS::Close(ifs);
	cout << "Here is the unpickled version of the names:" << endl << endl;
	newNames2.Print(cout, /*indent=*/ 2);
    } catch (const FS::Failure &f) {
	cerr << "Error pickling table: " << f << "; exiting..." << endl;
	return 1;
    } catch (const FS::EndOfFile &f) {
      cerr << "Unexpected end-of-file reading table: " << f << "; exiting..." << endl;
      return 1;
    }
    return 0;
}

static const Basics::uint32 small_max = ((1 << 16) - 1);

// Generate entries for a PrefixTbl until it has at least
// "target_size" entries.
void generate_table(CompactFV::List &tbl,
		    Basics::uint32 target_PrefixTbl_size,
		    Basics::uint32 fvs_per_name = 4)
{
  PrefixTbl::PutTbl
    putTbl(/*sizeHint=*/ target_PrefixTbl_size, /*useGC=*/ true);

  // We use characters out of this array as path elements
  char *path_elems = ".abcdefghijlkmnopqrstuvwxyz0123456789ABCDEFGHIJLKMNOPQRSTUVWXYZ";
  unsigned int n_path_elems = strlen(path_elems);

  // We use characters out of thie array as free variable kinds.  Note
  // that none of the normal kinds (!BELNT) are included, and "." is
  // also left out to avoid confusion.
  char *fv_kinds = "A@C#D$F%G^H&I*J-K_N+O=P?Q|R:S;U,V<W>X~Y`Z";
  unsigned int n_fv_kinds = strlen(fv_kinds);

  // We build up a path in this local buffer.  The four slashes and
  // the final null character are fixed.
  char path[10];
  path[1] = path[3] = path[5] = path[7] = '/';
  path[9] = 0;

  FV::ListApp fv_list_in;

  if(verbose > 2)
    {
      cio().start_out() << "Generating paths:" << endl;
      cio().end_out();
    }

  Basics::uint32 fv_max = (target_PrefixTbl_size * fvs_per_name);
  tbl.types = NEW_PTRFREE_ARRAY(char, fv_max);
  if(target_PrefixTbl_size & ~small_max)
    {
      tbl.idx =  NEW_PTRFREE_ARRAY(Basics::uint32, fv_max);
      tbl.idx_sm = 0;
    }
  else
    {
      tbl.idx_sm =  NEW_PTRFREE_ARRAY(Basics::uint16, fv_max);
      tbl.idx = 0;
    }

  tbl.num = 0;
  for(unsigned int i1 = 0;
      (i1 < n_path_elems) &&
	(tbl.tbl.NumArcs() < target_PrefixTbl_size);
      i1++)
    {
      path[2] = path_elems[i1];
      for(unsigned int i2 = 0;
	  (i2 < n_path_elems) &&
	    (tbl.tbl.NumArcs() < target_PrefixTbl_size);
	  i2++)
	{
	  path[4] = path_elems[(i2+1) % n_path_elems];
	  for(unsigned int i3 = 0;
	      (i3 < n_path_elems) &&
		(tbl.tbl.NumArcs() < target_PrefixTbl_size);
	      i3++)
	    {
	      path[6] = path_elems[(i3+2) % n_path_elems];
	      for(unsigned int i4 = 0;
		  (i4 < n_path_elems) &&
		    (tbl.tbl.NumArcs() < target_PrefixTbl_size);
		  i4++)
		{
		  path[8] = path_elems[(i4+3) % n_path_elems];

		  // Put this new path into the table
		  PrefixTbl::index_t idx = tbl.tbl.Put(&(path[2]), putTbl);
		  if(verbose > 2)
		    {
		      cio().start_out() << "  " << &(path[2]) << " -> (PrefixTbl) " << idx << endl;
		      cio().end_out();
		    }

		  for(unsigned int i5 = 0; i5 < fvs_per_name; i5++)
		    {
		      assert(tbl.num < fv_max);

		      char fv_kind = fv_kinds[tbl.num % n_fv_kinds];
		      path[0] = fv_kind;

		      fv_list_in.Append(path);
		      tbl.types[tbl.num] = fv_kind;
		      if(tbl.idx != 0)
			{
			  tbl.idx[tbl.num] = idx;
			}
		      else
			{
			  assert(tbl.idx_sm != 0);
			  tbl.idx_sm[tbl.num] = idx;
			  assert(((Basics::uint32) tbl.idx_sm[tbl.num]) == idx);
			}
		      tbl.num++;
		    }
		}
	    }
	}
    }
  assert(tbl.num <= fv_max);

  FV::ListApp fv_list_out;
  tbl.ToFVList(fv_list_out);
  if(fv_list_in != fv_list_out)
    {
      cio().start_err()
	<< "Result of ToFVList doesn't match input!"
	<< endl;
      cio().end_err();
    }

  CompactFV::List tbl2(fv_list_out);
  if(tbl != tbl2)
    {
      cio().start_err()
	<< "Rebuilding CompactFV::List from FV::List doesn't match original!"
	<< endl;
      cio().end_err();
    }
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

const CompactFV::List *server_compare_tbl = 0;

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
	  CompactFV::List arg;
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
	  CompactFV::List arg;
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

void ClientCall(const CompactFV::List &arg, bool old_protocol)
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
  return FS::MakeTempDir("/tmp/TestCompactFV", temp_dir_suffix);
}

void remove_temp_dir(const Text &path)
{
  Text cmd = Text::printf("rm -rf %s", path.cchars());
  system(cmd.cchars());
}

int do_read_write_test(const Text &fname,
		       Basics::uint32 target_PrefixTbl_size,
		       Basics::uint32 fvs_per_name = 4)

{
  CompactFV::List names;
  if(verbose)
    {
      cio().start_out()
	<< "Generating CompactFV with PrefixTbl size " << target_PrefixTbl_size
	<< ", and " << fvs_per_name << " free variables per path..." << endl;
      cio().end_out();
    }
  if(target_PrefixTbl_size > 0)
    {
      generate_table(names, target_PrefixTbl_size, fvs_per_name);
    }
  if(verbose)
    {
      cio().start_out()
	<< "Generated free variable count: " << names.num << endl;
      cio().end_out();
    }
    
  try {
    // write the table out to disk
    if(verbose)
      {
	ostream &out = cio().start_out();
	out << "Writing CompactFV to " << fname << "..." << endl << endl;
	cio().end_out();
      }

    ofstream ofs;
    FS::OpenForWriting(fname, /*OUT*/ ofs);
    names.Write(ofs);
    FS::Close(ofs);

    // read it back in and print it
    ifstream ifs;
    FS::OpenReadOnly(fname, /*OUT*/ ifs);
    CompactFV::List newNames(ifs);
    if (!FS::AtEOF(ifs)) {
      cio().start_err() << "Error: stream not at end-of-file as expected!" << endl;
      cio().end_err();
    }
    FS::Close(ifs);
    if(newNames != names)
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
	out << "Here is the unpickled version of the names:" << endl << endl;
	newNames.Print(out, /*indent=*/ 2);
      }

    try
      {
	ClientCall(names, false);
      }
    catch(SRPC::failure f)
      {
	cio().start_err() << f;
	cio().end_err();
	return 1;
      }

    if(names.CanWriteOld())
      {
	Text old_fname = fname;
	old_fname += "-old";

	if(verbose)
	  {
	    ostream &out = cio().start_out();
	    out << "Writing same CompactFV to " << old_fname
		<< " in old format..." << endl << endl;
	    cio().end_out();
	  }

	{
	  ofstream old_ofs;
	  FS::OpenForWriting(old_fname, /*OUT*/ old_ofs);
	  names.Write(old_ofs, true);
	  FS::Close(old_ofs);
	}
	{
	  // read it back in and print it
	  ifstream old_ifs;
	  FS::OpenReadOnly(old_fname, /*OUT*/ old_ifs);
	  CompactFV::List newNames2(old_ifs, true);
	  if (!FS::AtEOF(old_ifs)) {
	    cio().start_err()
	      << "Error: stream not at end-of-file as expected!" << endl
	      << "Final file position = " << FS::Posn(old_ifs) << endl;
	    cio().end_err();
	  }
	  FS::Close(old_ifs);
	  if(newNames2 != names)
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
	    ClientCall(names, true);
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
	    ClientCall(names, true);
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
    cio().start_err()
      << "Unexpected end-of-file reading table: " << f << "; exiting..." << endl;
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

  // First test an empty CompactFV
  {
    Text fname = temp_dir_name+"/empty.CompactFV";
    if(do_read_write_test(fname, 0))
      return 1;
  }

  // Next test a small CompactFV
  {
    Text fname = temp_dir_name+"/small.CompactFV";
    if(do_read_write_test(fname, 1000))
      return 1;
  }

  // Next test CompactFV's on either side of the first cross-over
  // point (where the PrefixTbl is still small, but the total number
  // of free variables hist 2^16 because of path re-use).
  {
    Text fname = temp_dir_name+"/medium1.CompactFV";
    if(do_read_write_test(fname, 13321, 5))
      return 1;
  }

  {
    Text fname = temp_dir_name+"/medium2.CompactFV";
    if(do_read_write_test(fname, 16651, 4))
      return 1;
  }

  // Now test the second cross-over point where the PrefixTbl passes
  // 2^16 entries
  Basics::uint32 line = 1;
  line <<= 16;
  for(int offset = -2; offset <= 2; offset++)
    {
      Text fname;
      if(offset < 0)
	{
	  fname = Text::printf("%s/line%d.CompactFV",
			       temp_dir_name.cchars(), offset);
	}
      else if(offset == 0)
	{
	  fname = temp_dir_name+"/line.CompactFV";
	}
      else
	{
	  fname = Text::printf("%s/line+%d.CompactFV",
			       temp_dir_name.cchars(), offset);
	}
      
      if(do_read_write_test(fname, line + offset))
	return 1;
    }

  // Finally one that is way over both lines
  {
    Text fname = temp_dir_name+"/large.CompactFV";
    if(do_read_write_test(fname, 100000))
      return 1;
  }

  cio().start_out()
    << "All tests passed!" << endl;
  cio().end_out();

  remove_temp_dir(temp_dir_name);

  return 0;
}
