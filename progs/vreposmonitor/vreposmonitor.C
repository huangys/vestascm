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

// vreposmonitor -- a program to monitor the Vesta-2 repository server
// state

#include <math.h>

// basics
#include <Basics.H>
#include <Units.H>
#include <SRPC.H>

#include <ReposStatsSRPC.H>
#include <VDirSurrogate.H>

using std::cout;
using std::cerr;
using std::endl;

// The name of this program
Text program_name;

// The time format used for printing the server start time
Text time_format;

// The repository we're monitoring
Text repos_host, repos_port;

ReposStats::StatsResult *operator-(const ReposStats::StatsResult &lhs,
				   const ReposStats::StatsResult &rhs)
  throw()
{
  ReposStats::StatsResult *result = NEW(ReposStats::StatsResult);

  ReposStats::StatKey k;
  ReposStats::Stat *lhs_v, *rhs_v;
  ReposStats::StatsResultIter it(&lhs);
  while(it.Next(k,lhs_v))
    {
      if(rhs.Get(k, rhs_v))
	{
	  switch(k.kind)
	    {
	    case ReposStats::fdCache:
	      {
		ReposStats::FdCacheStats
		  *lhs_fd_stats = (ReposStats::FdCacheStats *) lhs_v,
		  *rhs_fd_stats = (ReposStats::FdCacheStats *) rhs_v;
		result->Put(k,
			    NEW_CONSTR(ReposStats::FdCacheStats,
				       (*lhs_fd_stats -	*rhs_fd_stats)));
	      }
	      break;
	    case ReposStats::dupeTotal:
	      {
		ReposStats::DupeStats
		  *lhs_dupe_stats = (ReposStats::DupeStats *) lhs_v,
		  *rhs_dupe_stats = (ReposStats::DupeStats *) rhs_v;
		result->Put(k,
			    NEW_CONSTR(ReposStats::DupeStats,
				       (*lhs_dupe_stats - *rhs_dupe_stats)));
	      }
	      break;
	    case ReposStats::srpcTotal:
	      {
		ReposStats::TimedCalls
		  *lhs_srpc_stats = (ReposStats::TimedCalls *) lhs_v,
		  *rhs_srpc_stats = (ReposStats::TimedCalls *) rhs_v;
		result->Put(k,
			    NEW_CONSTR(ReposStats::TimedCalls,
				       (*lhs_srpc_stats - *rhs_srpc_stats)));
	      }
	      break;
	    case ReposStats::nfsTotal:
	      {
		ReposStats::TimedCalls
		  *lhs_nfs_stats = (ReposStats::TimedCalls *) lhs_v,
		  *rhs_nfs_stats = (ReposStats::TimedCalls *) rhs_v;
		result->Put(k,
			    NEW_CONSTR(ReposStats::TimedCalls,
				       (*lhs_nfs_stats - *rhs_nfs_stats)));
	      }
	      break;
	    case ReposStats::memUsage:
	      {
		// We always preserve the left hand (new) memory stats.
		ReposStats::MemStats
		  *lhs_mem_stats = (ReposStats::MemStats *) lhs_v;
		result->Put(k, NEW_CONSTR(ReposStats::MemStats,
					  (*lhs_mem_stats)));
	      }
	      break;
	    case ReposStats::vMemPool:
	      {
		ReposStats::VMemPoolStats
		  *lhs_vmp_stats = (ReposStats::VMemPoolStats *) lhs_v,
		  *rhs_vmp_stats = (ReposStats::VMemPoolStats *) rhs_v;
		result->Put(k, NEW_CONSTR(ReposStats::VMemPoolStats,
					  (*lhs_vmp_stats - *rhs_vmp_stats)));
	      }
	      break;
	    }
	}
    }

  return result;
}

bool operator<(const ReposStats::StatsResult &lhs,
	       const ReposStats::StatsResult &rhs)
  throw()
{
  ReposStats::StatKey k;
  ReposStats::Stat *lhs_v, *rhs_v;
  ReposStats::StatsResultIter it(&lhs);
  while(it.Next(k,lhs_v))
    {
      if(rhs.Get(k, rhs_v))
	{
	  switch(k.kind)
	    {
	    case ReposStats::fdCache:
	      {
		ReposStats::FdCacheStats
		  *lhs_fd_stats = (ReposStats::FdCacheStats *) lhs_v,
		  *rhs_fd_stats = (ReposStats::FdCacheStats *) rhs_v;
		if((lhs_fd_stats->hits < rhs_fd_stats->hits) ||
		   (lhs_fd_stats->open_misses < rhs_fd_stats->open_misses) ||
		   (lhs_fd_stats->try_misses < rhs_fd_stats->try_misses) ||
		   (lhs_fd_stats->evictions < rhs_fd_stats->evictions) ||
		   (lhs_fd_stats->expirations < rhs_fd_stats->expirations))
		  return true;
	      }
	      break;
	    case ReposStats::dupeTotal:
	      {
		ReposStats::DupeStats
		  *lhs_dupe_stats = (ReposStats::DupeStats *) lhs_v,
		  *rhs_dupe_stats = (ReposStats::DupeStats *) rhs_v;
		if((lhs_dupe_stats->non < rhs_dupe_stats->non) ||
		   (lhs_dupe_stats->inProcess < rhs_dupe_stats->inProcess) ||
		   (lhs_dupe_stats->completed < rhs_dupe_stats->completed))
		  return true;
	      }
	      break;
	    case ReposStats::srpcTotal:
	      {
		ReposStats::TimedCalls
		  *lhs_srpc_stats = (ReposStats::TimedCalls *) lhs_v,
		  *rhs_srpc_stats = (ReposStats::TimedCalls *) rhs_v;
		if((lhs_srpc_stats->call_count < rhs_srpc_stats->call_count) ||
		   (lhs_srpc_stats->elapsed_secs <
		    rhs_srpc_stats->elapsed_secs) ||
		   ((lhs_srpc_stats->elapsed_secs ==
		     rhs_srpc_stats->elapsed_secs) &&
		    (lhs_srpc_stats->elapsed_usecs <
		     rhs_srpc_stats->elapsed_usecs)))
		  return true;
	      }
	      break;
	    case ReposStats::nfsTotal:
	      {
		ReposStats::TimedCalls
		  *lhs_nfs_stats = (ReposStats::TimedCalls *) lhs_v,
		  *rhs_nfs_stats = (ReposStats::TimedCalls *) rhs_v;
		if((lhs_nfs_stats->call_count < rhs_nfs_stats->call_count) ||
		   (lhs_nfs_stats->elapsed_secs <
		    rhs_nfs_stats->elapsed_secs) ||
		   ((lhs_nfs_stats->elapsed_secs ==
		     rhs_nfs_stats->elapsed_secs) &&
		    (lhs_nfs_stats->elapsed_usecs <
		     rhs_nfs_stats->elapsed_usecs)))
		  return true;
	      }
	      break;
	    case ReposStats::vMemPool:
	      {
		ReposStats::VMemPoolStats
		  *lhs_vmp_stats = (ReposStats::VMemPoolStats *) lhs_v,
		  *rhs_vmp_stats = (ReposStats::VMemPoolStats *) rhs_v;
		if((lhs_vmp_stats->allocate_calls < rhs_vmp_stats->allocate_calls) ||
		   (lhs_vmp_stats->free_calls < rhs_vmp_stats->free_calls) ||
		   (lhs_vmp_stats->grow_calls < rhs_vmp_stats->grow_calls))
		  return true;
	      }
	      break;
	    }
	}
    }
  return false;
}

void freeStatsResult(ReposStats::StatsResult *trash) throw()
{
  // Loop over the table and delete any statistic data it contains.
  ReposStats::StatKey key;
  ReposStats::Stat *stat;
  ReposStats::StatsResultIter it(trash);
  while(it.Next(key, stat))
    {
      delete stat;
    }
  // Delete the table itself.
  delete trash;
}

static void Error(char *msg, char *arg = (char *)NULL) throw ()
{
    cerr << "Error: " << msg;
    if (arg != (char *)NULL) cerr << ": `" << arg << "'";
    cerr << endl;
    cerr << "SYNTAX: vreposmonitor [ -update time ] "
         << "[ -ts time ] [ -n num ] [ -rows num ]" << endl;
    exit(1);
}

// The statistics we're going to ask the server for.  Set in main.
ReposStats::StatsRequest statsRequest;

static void PrintHeader() throw ()
{
  Text header_lines[3];
  
  // Form the header based on the statistics we'll be printing
  for(int i = 0; i < statsRequest.size(); i++)
    {
      switch(statsRequest.get(i))
	{
	case ReposStats::nfsTotal:
	  header_lines[0] += "   NFS    ";
	  header_lines[1] += "NUM   AVG ";
	  header_lines[2] += "---- -----";
	  break;
	case ReposStats::srpcTotal:
	  header_lines[0] += "   SRPC   ";
	  header_lines[1] += "NUM   AVG ";
	  header_lines[2] += "---- -----";
	  break;
	case ReposStats::dupeTotal:
	  header_lines[0] += "     DUPE     ";
	  header_lines[1] += "NEW  INPR DONE";
	  header_lines[2] += "---- ---- ----";
	  break;
	case ReposStats::fdCache:
	  header_lines[0] += "           FDCACHE           ";
	  header_lines[1] += "HELD HITS OMIS TMIS EVIC EXPR";
	  header_lines[2] += "---- ---- ---- ---- ---- ----";
	  break;
	case ReposStats::memUsage:
	  header_lines[0] += " MEMORY  ";
	  header_lines[1] += "SIZE RES ";
	  header_lines[2] += "---- ----";
	  break;
	case ReposStats::vMemPool:
	  header_lines[0] += "                                          VMEMPOOL                                         ";
	  header_lines[1] += "SIZE NEFL FBLK FBYT B/FB WBYT ALOC AAVG  BRJS BRJL FB/A ASBL ANBL FREE FAVG  FCOB FCOA GROW";
	  header_lines[2] += "---- ---- ---- ---- ---- ---- ---- ----- ---- ---- ---- ---- ---- ---- ----- ---- ---- ----";
	  break;
	}
      header_lines[0] += "|";
      header_lines[1] += "|";
      header_lines[2] += " ";
    }

  cout << endl
       << header_lines[0] << endl
       << header_lines[1] << endl
       << header_lines[2] << endl;
}

static void PrintVal(Basics::uint64 val) throw ()
{
  Text print_val = Basics::FormatUnitVal(val).PadLeft(4);
  cout << print_val << ' ';
}

static void PrintTime(Basics::uint64 secs, Basics::uint32 usecs,
		      Basics::uint64 divisor)
{
  // Avoid divide by zero
  if(divisor == 0)
    {
      cout << "N/A   ";
      return;
    }

  // Compute total usecs divide by the divisor
  double time = secs;
  time *= USECS_PER_SEC;
  time += usecs;
  time /= divisor;

  const char *units;
  // Less than 0.1ms?
  if(time < 100)
    {
      // Print as micorseconds
      units = "us";
    }
  // Less than 0.1s?
  else if(time < 100000)
    {
      // Print as milliseconds
      time /= 1000;
      units = "ms";
    }
  // Less than 0.1m?
  else if(time < (6*USECS_PER_SEC))
    {
      // Print as seconds
      time /= USECS_PER_SEC;
      units = "s ";
    }
  else
    {
      // Print as minutes
      time /= USECS_PER_SEC;
      time /= 60;
      units = "m ";
    }

    char buff[12];
    if (time < 9.95) { // assures won't round up to 10.0
      sprintf(buff, "%3.1f", time);
    } else {
      unsigned int time_int = (unsigned int) rint(time);
      sprintf(buff, "%3d", time_int);
    }

    cout << buff << units << ' ';
}

static void PrintAvg(Basics::uint64 numerator,
		     Basics::uint64 divisor)
{
  // Avoid divide by zero
  if(divisor == 0)
    {
      cout << " N/A ";
      return;
    }

  // Compute total usecs divide by the divisor
  double average = numerator;
  average /= divisor;

  const char *units;
  // Less than 100?
  if(average < 99.95)
    {
      // Print as small flaoting-point number
      char buff[12];
      sprintf(buff, "%4.1f", average);
      cout << buff << ' ';
    }
  else
    {
      // Use integer division and K/M/etc. units
      PrintVal(numerator/divisor);
    }
}

static void filterStatsRequest(ReposStats::StatsResult *received)
{
  // We'll accumulate the statistics actually received here, in the
  // same order they were requested.
  ReposStats::StatsRequest statsReceived;
  // Loop over the requested statistics.
  for(int i = 0; i < statsRequest.size(); i++)
    {
      ReposStats::StatKind kind = statsRequest.get(i);
      ReposStats::Stat *stats = 0;
      // If the server did send us this statistic, add it to the
      // received sequence.
      if(received->Get(kind, stats))
	{
	  statsReceived.addhi(kind);
	}
    }
  // In future, we'll only request those statistics the server can
  // actually provide.  (This also controls the appearance of the
  // header.)
  statsRequest = statsReceived;
}

// Format the repository server start time as a text string
Text TextStartTime(time_t when) throw()
{
  char timebuf[256];
  strftime(timebuf, sizeof(timebuf), time_format.cchars(), localtime(&when));

  return timebuf;
}

// Format the repository server uptime as a text string
Text TextUptime(Basics::uint32 uptime) throw()
{
  Basics::uint32 seconds, minutes, hours, days;

  seconds = uptime % 60;
  uptime /= 60;

  minutes = uptime % 60;
  uptime /= 60;

  hours = uptime % 24;
  uptime /= 24;

  days = uptime;

  Text result;
  char buf[20];
  if(days > 0)
    {
      sprintf(buf, "%d", days);
      result += buf;
      result += " day";
      if(days > 1) result += "s";
    }
  if(hours > 0)
    {
      if(!result.Empty())
	result += " ";
      sprintf(buf, "%d", hours);
      result += buf;
      result += " hour";
      if(hours > 1) result += "s";
    }
  if(minutes > 0)
    {
      if(!result.Empty())
	result += " ";
      sprintf(buf, "%d", minutes);
      result += buf;
      result += " minute";
      if(minutes > 1) result += "s";
    }
  if(seconds > 0)
    {
      if(!result.Empty())
	result += " ";
      sprintf(buf, "%d", seconds);
      result += buf;
      result += " second";
      if(seconds > 1) result += "s";
    }
  return result;
}

static void NewReposStats(const int ts, const int timeout,
			  /*INOUT*/ ReposStats::StatsResult* &oldStats,
			  /*INOUT*/ int &rowsPrinted,
			  int rows) throw (SRPC::failure)
{
    // time at which to print next time stamp
    static time_t nextStamp = -1;

    // get new stats
    ReposStats::StatsResult *newStats = NEW(ReposStats::StatsResult);
    ReposStats::getStats(*newStats, statsRequest,
			 0,
			 repos_host, repos_port,
			 true, timeout);

    // There's a possibility that we're talking to an older server
    // that doesn't support one of the statistic types we requested.
    // Figure out if it decided to send us a subset of the statistics
    // we asked for so we can display an appropriate header.
    if((oldStats == 0) && (rowsPrinted == 0))
      {
	filterStatsRequest(newStats);
      }

    // test if server has been restarted since last sample
    if ((oldStats == 0) ||
	(*newStats < *oldStats))
      {
	ReposStats::ServerInfo server_info;
	ReposStats::getServerInfo(server_info,
				  0,
				  repos_host, repos_port,
				  true, timeout);
	cout << "Repository:     " << repos_host << ":" << repos_port << endl
	     << "Server Version: " << server_info.version << endl
	     << "Started:        " << TextStartTime(server_info.start_time)
	     << endl
	     << "Uptime:         " << TextUptime(server_info.uptime) << endl
	     << endl;

	rowsPrinted = 0;
	if(oldStats != 0)
	  {
	    freeStatsResult(oldStats);
	    oldStats = 0;
	  }
      }

    // Print a header if we need to.
    if (rowsPrinted == 0 || (rows > 0 && rowsPrinted % rows == 0))
      PrintHeader();

    // see if it is time to print a timestamp
    if (ts >= 0)
      {
	time_t now = time((time_t *)NULL);
	assert(now >= 0);
	if (now >= nextStamp)
	  {
	    // don't print a timestamp the first time
	    if (nextStamp >= 0)
	      {
		char buffer[64];
		(void) ctime_r(&now, buffer);
		buffer[strlen(buffer)-1] = '\0'; // suppress '\n'
		cout << "------------------------ " << buffer
		     << " ------------------------" << endl;
	      }
	    nextStamp = now + ts;
	  }
      }

    // print either the absolute counts since the server was started,
    // or counts since the last call
    ReposStats::StatsResult *printedStats =
      ((oldStats == 0)
       ? newStats
       : *newStats - *oldStats);

    for(int i = 0; i < statsRequest.size(); i++)
      {
	ReposStats::StatKind kind = statsRequest.get(i);
	ReposStats::Stat *stats = 0;
	if(printedStats->Get(kind, stats))
	  {
	    switch(kind)
	      {
	      case ReposStats::nfsTotal:
		{
		  ReposStats::TimedCalls *nfsStats =
		    (ReposStats::TimedCalls *) stats;
		  PrintVal(nfsStats->call_count);
		  PrintTime(nfsStats->elapsed_secs,
			    nfsStats->elapsed_usecs,
			    nfsStats->call_count);
		}
		break;
	      case ReposStats::srpcTotal:
		{
		  ReposStats::TimedCalls *srpcStats =
		    (ReposStats::TimedCalls *) stats;
		  PrintVal(srpcStats->call_count);
		  PrintTime(srpcStats->elapsed_secs,
			    srpcStats->elapsed_usecs,
			    srpcStats->call_count);
		}
		break;
	      case ReposStats::dupeTotal:
		{
		  ReposStats::DupeStats *dupeStats =
		    (ReposStats::DupeStats *) stats;
		  PrintVal(dupeStats->non);
		  PrintVal(dupeStats->inProcess);
		  PrintVal(dupeStats->completed);
		}
		break;
	      case ReposStats::fdCache:
		{
		  ReposStats::FdCacheStats *fdCacheStats =
		    (ReposStats::FdCacheStats *) stats;
		  PrintVal(fdCacheStats->n_in_cache);
		  PrintVal(fdCacheStats->hits);
		  PrintVal(fdCacheStats->open_misses);
		  PrintVal(fdCacheStats->try_misses);
		  PrintVal(fdCacheStats->evictions);
		  PrintVal(fdCacheStats->expirations);
		}
		break;
	      case ReposStats::memUsage:
		{
		  ReposStats::MemStats *memStats =
		    (ReposStats::MemStats *) stats;
		  PrintVal(memStats->total);
		  PrintVal(memStats->resident);
		}
		break;
	      case ReposStats::vMemPool:
		{
		  ReposStats::VMemPoolStats *vmpStats =
		    (ReposStats::VMemPoolStats *) stats;
		  PrintVal(vmpStats->size);
		  PrintVal(vmpStats->nonempty_free_list_count);
		  PrintVal(vmpStats->free_list_blocks);
		  PrintVal(vmpStats->free_list_bytes);
		  PrintAvg(vmpStats->free_list_bytes,
			   vmpStats->free_list_blocks);
		  PrintVal(vmpStats->free_wasted_bytes);
		  PrintVal(vmpStats->allocate_calls);
		  PrintTime(vmpStats->allocate_time.secs,
			    vmpStats->allocate_time.usecs,
			    vmpStats->allocate_calls);
		  PrintVal(vmpStats->allocate_rej_sm_blocks);
		  PrintVal(vmpStats->allocate_rej_lg_blocks);
		  PrintAvg(vmpStats->allocate_rej_sm_blocks +
			   vmpStats->allocate_rej_lg_blocks,
			   vmpStats->allocate_calls);
		  PrintVal(vmpStats->allocate_split_blocks);
		  PrintVal(vmpStats->allocate_new_blocks);
		  PrintVal(vmpStats->free_calls);
		  PrintTime(vmpStats->free_time.secs,
			    vmpStats->free_time.usecs,
			    vmpStats->free_calls);
		  PrintVal(vmpStats->free_coalesce_before);
		  PrintVal(vmpStats->free_coalesce_after);
		  PrintVal(vmpStats->grow_calls);
		}
		break;
	      }
	  }
      }

    cout << endl;
    rowsPrinted++;

    // If the printed stats were the result of taking the difference
    // between the old and new stats, free the printed stats.
    if(printedStats != newStats)
      {
	freeStatsResult(printedStats);
      }

    // If there are old stats, free them.
    if(oldStats != 0)
      {
	freeStatsResult(oldStats);
      }

    // The new stats are now the old stats.
    oldStats = newStats;
}

static void Monitor(int update, int ts, int num, int rows, bool check) throw ()
{
    // repostiroy stats
    ReposStats::StatsResult *oldStats = 0;

    // loop until enough rows have been printed
    int rowsPrinted = 0;
    while (num < 0 || rowsPrinted < num) {
	try {
	  NewReposStats(ts, update*2,
			/*INOUT*/ oldStats, /*INOUT*/ rowsPrinted, rows);
	}
	catch (SRPC::failure) {
	  // If we're just doing a quick check, exit now.
	  if(check) exit(1);
	  cout << "Error contacting repository server; retrying..." << endl;
	  if(oldStats != 0)
	    {
	      freeStatsResult(oldStats);
	      oldStats = 0;
	    }
	}

	// If we're just doing a quick check, exit now.
	if(check) exit(0);

	// pause for "update" seconds
	Basics::thread::pause(update);
    }
}

/* Return the number of seconds denoted by "tm". Except for an optional
   suffix character, "tm" must be a non-negative integer value. If
   present, the suffix character specifies a time unit as follows:

     s  seconds (default)
     m  minutes
     h  hours
     d  days
*/
static int ParseTime(char *flag, char *tm) throw ()
{
    char errBuff[80];
    int len = strlen(tm);
    if (len == 0) {
	sprintf(errBuff, "argument to `%s' is empty", flag);
        Error(errBuff, tm);
    }
    char lastChar = tm[len - 1];
    int multiplier = 1;
    switch (lastChar) {
      case 'd':
	multiplier *= 24;
	// fall through
      case 'h':
	multiplier *= 60;
	// fall through
      case 'm':
	multiplier *= 60;
	// fall through
      case 's':
	tm[len - 1] = '\0';
	break;
      default:
	if (!isdigit(lastChar)) {
	    sprintf(errBuff, "illegal unit specifier `%c' in `%s' argument",
		    lastChar, flag);
	    Error(errBuff, tm);
	}
    }
    int res;
    if (sscanf(tm, "%d", &res) != 1) {
	sprintf(errBuff, "argument to `%s' not an integer", flag);
	Error(errBuff, tm);
    }
    return multiplier * res;
}

int main(int argc, char *argv[])
{
    int update = 10; // default: update every 10 seconds
    int ts = -1;     // default: do not print any timestamps
    int num = -1;    // default: update indefinitely
    int rows = -1;   // default: only write header lines once
    int arg = 1;
    bool check = false;

    program_name = argv[0];

    try
      {
	repos_host = VDirSurrogate::defaultHost();
	repos_port = VDirSurrogate::defaultPort();

	time_format = VestaConfig::get_Text("UserInterface", "TimeFormat");

	// process command-line
	while (arg < argc && *argv[arg] == '-') {
	  if (strcmp(argv[arg], "-update") == 0) {
	    if (++arg < argc) {
	      update = ParseTime("-update", argv[arg++]);
	    } else {
	      Error("no argument supplied for `-update'");
	    }
	  } else if (strcmp(argv[arg], "-ts") == 0) {
	    if (++arg < argc) {
	      ts = ParseTime("-ts", argv[arg++]);
	    } else {
	      Error("no argument supplied for `-ts'");
	    }
	  } else if (strcmp(argv[arg], "-n") == 0) {
	    if (++arg < argc) {
	      if (sscanf(argv[arg], "%d", &num) != 1) {
		Error("argument to `-n' not an integer", argv[arg]);
	      }
	      arg++;
	    } else {
	      Error("no argument supplied for `-n'");
	    }
	  } else if (strcmp(argv[arg], "-rows") == 0) {
	    if (++arg < argc) {
	      if (sscanf(argv[arg], "%d", &rows) != 1) {
		Error("argument to `-rows' not an integer", argv[arg]);
	      }
	      arg++;
	    } else {
	      Error("no argument supplied for `-rows'");
	    }
	  } else if (strcmp(argv[arg], "-check") == 0) {
	    check = true;
	    arg++;
	  } else if (strcmp(argv[arg], "-R") == 0) {
	    if (++arg < argc) {
	      Text repos = argv[arg];
	      arg++;
	      // Split up the argument into the host and port part
	      int colon = repos.FindCharR(':');
	      if (colon == -1) {
		repos_host = repos;
	      } else {
		repos_host = repos.Sub(0, colon);
		repos_port = repos.Sub(colon+1);
	      }
	    } else {
	      Error("no argument supplied for `-R'");
	    }
	  } else if (strcmp(argv[arg], "-mem") == 0) {
	    statsRequest.addhi(ReposStats::memUsage);
	    arg++;
	  } else if (strcmp(argv[arg], "-nfs") == 0) {
	    statsRequest.addhi(ReposStats::nfsTotal);
	    arg++;
	  } else if (strcmp(argv[arg], "-srpc") == 0) {
	    statsRequest.addhi(ReposStats::srpcTotal);
	    arg++;
	  } else if (strcmp(argv[arg], "-dupe") == 0) {
	    statsRequest.addhi(ReposStats::dupeTotal);
	    arg++;
	  } else if (strcmp(argv[arg], "-fdcache") == 0) {
	    statsRequest.addhi(ReposStats::fdCache);
	    arg++;
	  } else if (strcmp(argv[arg], "-vmempool") == 0) {
	    statsRequest.addhi(ReposStats::vMemPool);
	    arg++;
	  } else {
	    Error("unrecognized option", argv[arg]);
	  }
	}
	if (arg < argc) Error("too many command-line arguments");

	if(statsRequest.size() == 0)
	  {
	    // No explicit request, ask for a default set of
	    // statistics.
	    statsRequest.addhi(ReposStats::memUsage);
	    statsRequest.addhi(ReposStats::nfsTotal);
	    statsRequest.addhi(ReposStats::srpcTotal);
	    statsRequest.addhi(ReposStats::dupeTotal);
	    statsRequest.addhi(ReposStats::fdCache);
	  }

	Monitor(update, ts, num, rows, check);
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
    return(0);
}
