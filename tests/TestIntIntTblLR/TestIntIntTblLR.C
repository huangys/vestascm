/* A test of the "IntIntTblLR" class. */

#include <Basics.H>
#include <FS.H>
#include <ThreadIO.H>
#include <LimService.H>
#include <MultiSRPC.H>
#include <IntIntTblLR.H>

#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif

using std::ofstream;
using std::ifstream;
using std::cout;
using std::cin;
using std::cerr;
using std::endl;
using OS::cio;
using std::ostream;

int do_tests();

Text program_name;
int verbose = 0;

void Usage()
{
    cerr << "Usage: " << program_name
	 <<  " [-v]" << endl;
}

int main(int argc, char *argv[])
{
  program_name = argv[0];
  
  opterr = 0;
  for (;;)
    {
      char* slash;
      int c = getopt(argc, argv, "v");
      if (c == EOF) break;
      switch (c)
	{
	case 'v':
	  verbose++;
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

  return do_tests();
}

void generate_identity(IntIntTblLR &tbl, IntIntTblLR::int_type target)
{
  for(IntIntTblLR::int_type i = 0; i < target; i++)
    {
      tbl.Put(i, i);
    }

  // Now check that it is correct
  assert(tbl.Size() == target);
  for(IntIntTblLR::int_type i = 0; i < target; i++)
    {
      IntIntTblLR::int_type v;
      bool inTbl = tbl.Get(i, v);
      assert(inTbl);
      assert(i == v);
    }
}

void generate_inverse(IntIntTblLR &tbl, IntIntTblLR::int_type target)
{
  for(IntIntTblLR::int_type i = 0; i < target; i++)
    {
      tbl.Put(i, target - i);
    }

  // Now check that it is correct
  assert(tbl.Size() == target);
  for(IntIntTblLR::int_type i = 0; i < target; i++)
    {
      IntIntTblLR::int_type v;
      bool inTbl = tbl.Get(i, v);
      assert(inTbl);
      assert((target - i) == v);
    }
}

void generate_shifted_up(IntIntTblLR &tbl, IntIntTblLR::int_type target, IntIntTblLR::int_type shift)
{
  for(IntIntTblLR::int_type i = 0; i < target; i++)
    {
      tbl.Put(i, i + shift);
    }

  // Now check that it is correct
  assert(tbl.Size() == target);
  for(IntIntTblLR::int_type i = 0; i < target; i++)
    {
      IntIntTblLR::int_type v;
      bool inTbl = tbl.Get(i, v);
      assert(inTbl);
      assert((i + shift) == v);
    }
}

void generate_shifted_down(IntIntTblLR &tbl, IntIntTblLR::int_type target, IntIntTblLR::int_type shift)
{
  for(IntIntTblLR::int_type i = 0; i < target; i++)
    {
      tbl.Put(i + shift, i);
    }

  // Now check that it is correct
  assert(tbl.Size() == target);
  for(IntIntTblLR::int_type i = 0; i < target; i++)
    {
      IntIntTblLR::int_type v;
      bool inTbl = tbl.Get(i + shift, v);
      assert(inTbl);
      assert(i == v);
    }
}

void generate_sparse(IntIntTblLR &tbl, IntIntTblLR::int_type target, IntIntTblLR::int_type factor)
{
  for(IntIntTblLR::int_type i = 0; i < target; i++)
    {
      tbl.Put(i * factor, i);
    }

  // Now check that it is correct
  assert(tbl.Size() == target);
  for(IntIntTblLR::int_type i = 0; i < target; i++)
    {
      IntIntTblLR::int_type v;
      bool inTbl = tbl.Get(i * factor, v);
      assert(inTbl);
      assert(i == v);
    }
}

bool write_old(const IntIntTblLR &tbl, const Text &fname)
{
  if(!tbl.CanWriteOld())
    return false;

  if(verbose > 1)
    {
      cio().start_out()
	<< "Writing table to " << fname << " in old format..." << endl;
      cio().end_out();
    }

  ofstream ofs;
  FS::OpenForWriting(fname, /*OUT*/ ofs);
  tbl.Write(ofs, true);
  FS::Close(ofs);

  return true;
}

int test_one_tbl(const IntIntTblLR &tbl, const Text &fname)
{
  if(verbose > 1)
    {
      cio().start_out()
	<< "Writing table to " << fname << " ..." << endl;
      cio().end_out();
    }

  try
    {
      ofstream ofs;
      FS::OpenForWriting(fname, /*OUT*/ ofs);
      tbl.Write(ofs);
      FS::Close(ofs);

      if(verbose > 1)
	{
	  cio().start_out()
	    << "Reading table from " << fname << " ..." << endl;
	  cio().end_out();
	}

      // read it back in and print it
      ifstream ifs;
      FS::OpenReadOnly(fname, /*OUT*/ ifs);
      IntIntTblLR newTbl(ifs);
      if (!FS::AtEOF(ifs))
	{
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
	  newTbl.Print(out, 2);
	  cio().end_out();
	}

    }
  catch (const FS::Failure &f)
    {
      cio().start_err() << "Error writing table: " << f << "; exiting..." << endl;
      cio().end_err();
      return 1;
    }
  catch (const FS::EndOfFile &f)
    {
      cio().start_err() << "Unexpected end-of-file reading table: " << f << "; exiting..." << endl;
      cio().end_err();
      return 1;
    }

  try
    {
      Text fname_old = fname+".old";
      if(write_old(tbl, fname_old))
	{
	  ifstream ifs;
	  FS::OpenReadOnly(fname_old, /*OUT*/ ifs);
	  IntIntTblLR newTbl2(ifs, true);
	  if (!FS::AtEOF(ifs))
	    {
	      cio().start_err()
		<< "Error: stream for old format not at end-of-file as expected!" << endl
		<< "Final file position = " << FS::Posn(ifs) << endl;
	      cio().end_err();
	      return 1;
	    }
	  FS::Close(ifs);
	  if(newTbl2 != tbl)
	    {
	      cio().start_err()
		<< "Table read from disk in old format differs from the one we wrote!"
		<< endl;
	      cio().end_err();
	      return 1;
	    }
	}
    }
  catch (const FS::Failure &f)
    {
      cio().start_err() << "Error writing old format table: " << f << "; exiting..." << endl;
      cio().end_err();
      return 1;
    }
  catch (const FS::EndOfFile &f)
    {
      cio().start_err() << "Unexpected end-of-file reading old format table: " << f << "; exiting..." << endl;
      cio().end_err();
      return 1;
    }

  return 0;
}

int test_one_size(const Text &temp_dir_name,
		  IntIntTblLR::int_type target)
{
  Text base_fname = temp_dir_name + Text::printf("/%d.IntIntTblLR", target);

  if(verbose)
    {
      cio().start_out()
	<< "Testing table of size " << target << "..." << endl;
      cio().end_out();
    }

  {
    Text id_fname = base_fname + ".identity";
    IntIntTblLR id_tbl;
    generate_identity(id_tbl, target);
    if(test_one_tbl(id_tbl, id_fname))
      return 1;
  }

  {
    Text inv_fname = base_fname + ".inverse";
    IntIntTblLR inv_tbl;
    generate_inverse(inv_tbl, target);
    if(test_one_tbl(inv_tbl, inv_fname))
      return 1;
  }

  {
    Text shift_up_fname = base_fname + ".shifted_up";
    IntIntTblLR shift_up_tbl;
    generate_shifted_up(shift_up_tbl, target, target/2);
    if(test_one_tbl(shift_up_tbl, shift_up_fname))
      return 1;
  }

  {
    Text shift_down_fname = base_fname + ".shifted_down";
    IntIntTblLR shift_down_tbl;
    generate_shifted_down(shift_down_tbl, target, target/2);
    if(test_one_tbl(shift_down_tbl, shift_down_fname))
      return 1;
  }

  {
    Text sparse_2_fname = base_fname + ".sparse_2";
    IntIntTblLR sparse_2_tbl;
    generate_sparse(sparse_2_tbl, target, 2);
    if(test_one_tbl(sparse_2_tbl, sparse_2_fname))
      return 1;
  }

  {
    Text sparse_3_fname = base_fname + ".sparse_3";
    IntIntTblLR sparse_3_tbl;
    generate_sparse(sparse_3_tbl, target, 3);
    if(test_one_tbl(sparse_3_tbl, sparse_3_fname))
      return 1;
  }

  return 0;
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
  return FS::MakeTempDir("/tmp/TestIntIntTblLR", temp_dir_suffix);
}

void remove_temp_dir(const Text &path)
{
  Text cmd = Text::printf("rm -rf %s", path.cchars());
  system(cmd.cchars());
}

int do_tests()
{
  Text temp_dir_name = make_temp_dir();

  // First the empty table
  {
    if(verbose)
      {
	cio().start_out()
	  << "Testing empty table..." << endl;
	cio().end_out();
      }

    Text empty_fname = temp_dir_name + "/0.IntIntTblLR.empty";
    IntIntTblLR empty_tbl;
    if(test_one_tbl(empty_tbl, empty_fname))
      return 1;
  }

  // Next a small table
  if(test_one_size(temp_dir_name, 1000))
    return 1;

  // Then ones around the 16-bit limit
  Basics::uint32 line = 1;
  line <<= 16;
  for(int offset = -2; offset <= 2; offset++)
    {
      if(test_one_size(temp_dir_name, line+offset))
	return 1;
    }

  // Finally one clear beyond the 16-bit limit.
  if(test_one_size(temp_dir_name, 100000))
    return 1;

  cio().start_out()
    << "All tests passed!" << endl;
  cio().end_out();

  remove_temp_dir(temp_dir_name);

  return 0;
}
