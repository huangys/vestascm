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

// ----------------------------------------------------------------------
//
// TestBigLog - Exercise the log library by writing a very large .log
// file.  (This was primarily created to ensure that bugs around .log
// files larger than 2^31 bytes were fixed.)
//
// Usage:
//
//   TestBigLog [-t <target size>] [-c <max commit size>] [<log directory>]
//
// The target size of the .log file defaults to 2G (2^31).  Entries
// will be written to it until a little more than this amount has been
// written.  Since there's a small amount of overhead in each block in
// the physical file, the .log file will be a little larger still.
// You can change the target size on the command line with the -t
// flag.  For example, adding "-t 5G" will write log entries until
// five gigabytes have been written to the log.
//
// The log directory used with the test can be specified with the last
// argument on the command line.  If it is omitted, it defaults to the
// current working directory.  The log in this directory should either
// be empty or should have been written by a previous run of
// TestBigLog.
//
// Before writing any new log entries, TestBigLog will recover all
// existing log entries.  The log entries each have an index and some
// other information computed from the index which makes it possible
// to verify that they were stored correctly and in the proper
// sequence.  For this reason, one should always run TestBigLog a
// minimum of twice to test both writing to a large transaction log
// and recovery from a large transaction log.
//
// Because it writes a very large number of transaction log entries,
// committing after each log entry is not practical.  (It would make
// the test run very slowly as each commit requires an fsync.)  For
// that reason, commits are only performed after a certain number of
// bytes have been written to the log.  Specifically, a commit is
// performed after half of the remaining bytes to reach the size
// target have been written or 128MB have been written to the log,
// whichever is smaller.  The commit maximum of 128MB can be changed
// with the -c command line flag (e.g. "-c 512M").  Note that the
// commit size also affects the amount of memory used on subsequent
// runs, as each commit is read completely into memory (see
// VestaLogPrivate::makeBytesAvail which repeatedly calls
// VestaLogPrivate::extendCur to read in blocks until reaching the end
// of the commit).
//
// ----------------------------------------------------------------------
//
// Here's an example of how one might use TestBigLog.
//
// 1. Create an empty directory for the transaction log for the test:
//
//   % mkdir log
//
// 2. Run TestBigLog once with the default target size to create an
// initial log:
//
//   % TestBigLog ./log
//   ./log/0.log: 0 bytes
//   Recovering...done.
//   Committing 128M bytes in 1838600 records...done.
//   [...]
//   Committing 73 bytes in 1 records...done.
//   Wrote 2.0G bytes in 29417585 records and 36 commits
//   New size is (approximately) 2.0G
//
// 3. Run TestBigLog a second time with a larger target size to test
// recovery of the log entries written by the first run and to write
// additional log entries:
//
//   % TestBigLog -t 3G ./log
//   ./log/0.log: 2.0G bytes
//   Recovering...10%...20%...30%...40%...50%...60%...70%...80%...90%...done.
//   Recovered 29417585 records
//   Committing 128M bytes in 1838600 records...done.
//   [...]
//   Committing 73 bytes in 1 records...done.
//   Wrote 1024M bytes in 14708792 records and 28 commits
//   New size is (approximately) 3.0G
//
// 4. If desired, repeat step 3 with another larger target size.  (It
// may be worth going above 4G or 2^32.)
//
// ----------------------------------------------------------------------

#include <Basics.H>
#include <iomanip>
#include <BufStream.H>
#include <Units.H>
#include <RegExp.H>
#include <Sequence.H>
#include "VestaLog.H"
#include "Recovery.H"
#include <stdlib.h>
#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

using std::cin;
using std::cerr;
using std::cout;
using std::endl;
using std::flush;
using std::setw;
using std::setfill;
using std::hex;
using std::dec;
using std::ostream;
using std::fstream;

char* program_name;

void
Usage()
{
    cerr << "Usage: " << program_name << " [-n lognum] [-f file] [logdir]" << endl;
    exit(2);
}

Basics::uint64 GetHexUint64(RecoveryReader* rr, char& c)
{
  int i, j;
  char chr[3];
  chr[2] = '\000';
  rr->skipWhite(c);
  Basics::uint64 result = 0;
  for (i=0; i<sizeof(Basics::uint64); i++) {
    result <<= 8;
    for (j=0; j<2; j++) {
      if (!isxdigit(c)) {
	throw VestaLog::Error(0, Text("GetHexUint64: ") +
			      "bad hex digit: " + c);
      }
      chr[j] = c;
      rr->get(c);
    }
    result |= strtol(chr, NULL, 16);
  }
  return result;
}

Basics::uint64 hash_number(Basics::uint64 n, bool which_base)
{
  // Some numbers we can xor with other numbers to scramble things up
  // a bit
  static const Basics::uint64 hash_base[2] =
    {CONST_INT_64(0x2b590719937a25c7),
     CONST_INT_64(0x97e05773d6f3b9bc)};

  // Convert n to an ASCII decimal representation
  char dec_n[30];
  sprintf(dec_n, "%" FORMAT_LENGTH_INT_64 "u", n);

  // Use Text::Hash on the decimal representation and XOR that with n
  // with the bytes reversed and XOR that with a fixed but fairly
  // random number.
  return Text(dec_n).Hash() ^ Basics::swab64(n) ^ hash_base[which_base?1:0];
}

// Simple computation of the Fibonacci sequence
Basics::uint64 next_fib()
{
  static Sequence<Basics::uint64, true> fibs;

  Basics::uint64 result;
  if(fibs.size() < 2)
    {
      result = 1;
    }
  else
    {
      result = fibs.remlo();
      result += fibs.getlo();
    }
  fibs.addhi(result);
  return result;
}

// This is the index of the next record to be read from the log or
// written to the log
Basics::uint64 g_record_index = 0;

Basics::uint64 write_one_entry(VestaLog &log)
{
  try { log.start(); }
  catch (VestaLog::Error e)
    {
      cerr << program_name << ": error during inner start: "
	   << e.msg << endl;
      exit(1);
    }

  // Get the next Fibonacci number which we'll include in the record
  Basics::uint64 l_fib = next_fib();

  // Construct a log record
  Basics::OBufStream record;
  record << "(xx "
    // Print all our numbers in hex 0-filled to 16 characters
	 << setw(16) << setfill('0') << hex
	 << g_record_index << " "
	 << setw(16) << setfill('0') << hex
	 << hash_number(g_record_index, false) << " "
	 << setw(16) << setfill('0') << hex
	 << l_fib << " "
	 << setw(16) << setfill('0') << hex
	 << hash_number(l_fib, true)
	 << ")" << endl;

  try
    {
      log.write(record.str(), record.tellp());
    }
  catch (VestaLog::Error e)
    {
      cerr << program_name << ": error writing to log: "
	   << e.msg << endl;
      exit(1);
    }

  try { log.commit(); }
  catch (VestaLog::Error e)
    {
      cerr << program_name << ": error during inner commit: "
	   << e.msg << endl;
      exit(1);
    }

  // Increment the record index
  g_record_index++;

  // Return the number of bytes written to the log
  return record.tellp();
}

Basics::uint64 g_recovery_estimate = 0;
Basics::uint64 g_recovered_bytes = 0;

unsigned int g_next_recovery_marker = 10;

static void
RecordCallback(RecoveryReader* rr, char& c)
  throw(VestaLog::Error, VestaLog::Eof)
{
  // Get our four numbers
  Basics::uint64 index = GetHexUint64(rr, c);
  Basics::uint64 index_hash = GetHexUint64(rr, c);
  Basics::uint64 fib = GetHexUint64(rr, c);
  Basics::uint64 fib_hash = GetHexUint64(rr, c);

  if(g_record_index == index)
    {
      g_record_index++;
    }
  else
    {
      cerr << program_name << ": wrong index in log record during recovery: "
	   << "expected " << g_record_index
	   << " got " << index << endl;
      exit(1);
    }

  Basics::uint64 index_hash_expected = hash_number(index, false);

  if(index_hash_expected != index_hash)
    {
      cerr << program_name << ": wrong index hash in log record "
	   << index << " during recovery: "
	   << "expected " << index_hash_expected
	   << " got " << index_hash << endl;
      exit(1);
    }

  Basics::uint64 fib_expected = next_fib();

  if(fib_expected != fib)
    {
      cerr << program_name << ": wrong Fibonacci in log record "
	   << index << " during recovery: "
	   << "expected " << fib_expected
	   << " got " << fib_hash << endl;
      exit(1);
    }

  Basics::uint64 fib_hash_expected = hash_number(fib, true);

  if(fib_hash_expected != fib_hash)
    {
      cerr << program_name << ": wrong Fibonacci hash in log record "
	   << index << " during recovery: "
	   << "expected " << fib_hash_expected
	   << " got " << fib_hash << endl;
      exit(1);
    }

  // Count the bytes in this record by re-constructing it.  We do this
  // so we know the initial size of the log.
  {
    g_recovered_bytes += 9 /*"(xx    )\n"*/ + (16 * 4);

    // Print out a marker about how much we've recovered every 10% or
    // so.  This isn't precise, but it doesn't need to be as it's just
    // meant to give a crude indication of recovery progress.
    if((g_recovery_estimate > 0) &&
       (((g_recovered_bytes * 100) / g_recovery_estimate) >=
	g_next_recovery_marker))
      {
	cout << g_next_recovery_marker << "%..." << flush;
	g_next_recovery_marker += 10;
      }
  }
}

bool is_log_fname(const char *fname)
{
  static Basics::RegExp *log_re = 0;
  if(log_re == 0)
    {
      try
	{
	  log_re = NEW_CONSTR(Basics::RegExp, ("[0-9]+\\.(log|ckp)"));
	}
      catch(Basics::RegExp::ParseError e)
	{
	  cerr << program_name
	       << ": error setting up log filename match regular expression: "
	       << e.msg << endl;
	}
    }

  return log_re->match(fname);
}

Basics::uint64 count_current_log_size(const char *path)
{
  Basics::uint64 result = 0;
  DIR *dir = opendir(path);
  if(dir != 0)
    {
      for(dirent *de = readdir(dir); de != 0; de = readdir(dir))
	{
	  if((strcmp(de->d_name, ".") == 0) ||
	     (strcmp(de->d_name, "..") == 0))
	    continue;

	  // If this looks like it could be a .log file
	  if((de->d_reclen >= 5) && is_log_fname(de->d_name))
	    {
	      struct stat l_stat_buf;
	      Text fname(path);
	      fname += "/";
	      fname += de->d_name;

	      if(stat(fname.cchars(), &l_stat_buf) == 0)
		{
		  if(S_ISREG(l_stat_buf.st_mode))
		    {
		      cout << fname << ": "
			   << Basics::FormatUnitVal(l_stat_buf.st_size)
			   << " bytes" << endl;
		      result += l_stat_buf.st_size;
		    }
		  else
		    {
		      cerr << fname << ": not a file" << endl;
		    }
		}
	      else
		{
		  int errno_save = errno;
		  cerr << fname << ": stat(2) failed: "
		       << Basics::errno_Text(errno_save) << endl;
		}
	    }
	}
    }

  return result;
}

int
main(int argc, char* argv[]) 
{
    int lognum = -1;
    VestaLog VRLog;

    program_name = argv[0];
    Basics::uint64 size_target = 0x80000000;
    Basics::uint64 commit_max = (1 << 27);

    int c;
    for (;;) {
	c = getopt(argc, argv, "n:t:c:");
	if (c == EOF) break;
	switch (c) {
	case 'n':
	  lognum = strtol(optarg, NULL, 0);
	  break;
	case 't':
	  try
	    {
	      size_target = Basics::ParseUnitVal(optarg);
	    }
	  catch(Basics::ParseUnitValFailure f)
	    {
	      cerr << program_name << ": Error parsing \""
		   << optarg << "\", using default value ("
		   << Basics::FormatUnitVal(size_target) << ")" << endl;
	    }
	  break;
	case 'c':
	  try
	    {
	      commit_max = Basics::ParseUnitVal(optarg);
	    }
	  catch(Basics::ParseUnitValFailure f)
	    {
	      cerr << program_name << ": Error parsing \""
		   << optarg << "\", using default value ("
		   << Basics::FormatUnitVal(commit_max) << ")" << endl;
	    }
	  break;
	case '?':
	default:
	  Usage();
	}
    }

    char* dir;
    switch (argc - optind) {
      case 0:
	dir = ".";
	break;
      case 1:
	dir = argv[optind];
	break;
      default:
	Usage();
    }

    // Register a callback for recovering the records we write to the
    // log
    RegisterRecoveryCallback("xx", RecordCallback);

    try {
	VRLog.open(dir, lognum, false, true);
    } catch (VestaLog::Error) {
	cerr << program_name << ": error opening log" << endl;
	exit(1);
    }

    g_recovery_estimate = count_current_log_size(dir);

    cout << "Recovering..." << flush;

    // Recover existing log records.  (Note at this point we don't
    // actually create any checkpoint files, or even more than one
    // .log file, but this is included for completeness.)
    try {
      fstream* ckpt = VRLog.openCheckpoint();
      if (ckpt != NULL) {
	RecoveryReader crr(ckpt);
	RecoverFrom(&crr);
	ckpt->close();
	delete ckpt;
      }
    } catch (VestaLog::Error e) {
      cerr << program_name << ": error during recovery from checkpoint: "
	   << e.msg << endl;
      exit(1);
    }
    try {
      do {
	RecoveryReader lrr(&VRLog);
	RecoverFrom(&lrr);
      } while (VRLog.nextLog());
    } catch (VestaLog::Error e) {
      cerr << program_name << ": error during recovery: "
	   << e.msg << endl;
      exit(1);
    }

    cout << "done." << endl;

    if(g_record_index > 0)
      {
	cout << "Recovered " << g_record_index << " records" << endl;
      }

    // Now start logging.

    try
      {
	VRLog.loggingBegin();
      }
    catch (VestaLog::Error e)
      {
	cerr << program_name << ": error during loggingBegin: "
	     << e.msg << endl;
	exit(1);
      }

    Basics::uint64 bytes = g_recovered_bytes;
    Basics::uint64 commits = 0;
    Basics::uint64 records = 0;

    do
      {
	// Write enough records to get us approximately half-way to
	// the final target target in the next commit.  (Note, we
	// always write at least one record per commit, so that we
	// don't fall into Zeno's paradox.)

	Basics::uint64 commit_target = ((size_target > bytes)
					?(commit_target = (size_target - bytes) >> 1)
					:1);

	// Clamp the commit target to the specified max
	if(commit_target > commit_max) commit_target = commit_max;

	Basics::uint64 commit_bytes = 0;
	Basics::uint64 commit_records = 0;

	try { VRLog.start(); }
	catch (VestaLog::Error e)
	  {
	    cerr << program_name << ": error during outer start: "
		 << e.msg << endl;
	    exit(1);
	  }

	do
	  {
	    commit_bytes += write_one_entry(VRLog);
	    commit_records++;
	  }
	while(commit_bytes <= commit_target);

	cout << "Committing " << Basics::FormatUnitVal(commit_bytes)
	     << " bytes in " << commit_records
	     << " records..." << flush;

	try { VRLog.commit(); }
	catch (VestaLog::Error e)
	  {
	    cerr << endl
		 << program_name << ": error during outer commit: "
		 << e.msg << endl;
	    exit(1);
	  }

	cout << "done." << endl;

	bytes += commit_bytes;
	records += commit_records;
	commits++;
      }
    while(bytes <= size_target);

    VRLog.close();

    cout << "Wrote " << Basics::FormatUnitVal(bytes - g_recovered_bytes)
	 << " bytes in " << records << " records and "
	 << commits << " commits" << endl
	 << "New size is (approximately) " << Basics::FormatUnitVal(bytes)
	 << endl;

    return 0;
}
