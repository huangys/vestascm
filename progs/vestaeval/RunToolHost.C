// Copyright (c) 2000, Compaq Computer Corporation 
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

// File: RunToolHost.C

#include <Basics.H>
#include <VestaConfig.H>
#include <RunToolClient.H>
#include <fnmatch.h>
#include "RunToolHost.H"
#include "RunToolClient.H"
#include "ThreadData.H"

using std::ostream;
using std::endl;
using OS::cio;

// Cached information about a host
struct Host {
  Text name;         // hostname[:port] from list
  FP::Tag uniqueid;  // unique ID the host's runtool server has chosen
  int usecount;      // local threads using host
  bool bad;          // host appears to be down, does not match platform,
                     //  or is the same as another host on the list
  Host() : usecount(0), bad(false) { }
};

// Descriptor for a platform
struct Platform {
  Platform* next;
  Text name;
  // glob patterns that the host must match to be this platform
  Text sysname, release, version, machine;
  // minimum hardware characteristics
  int cpus, cpuMHz, memKB;
  int nhosts;
  Host* hosts;
  // true if first host was given as "localhost"
  bool anchorfirst;
  // true if we've printed a message about having difficult finding a
  // runtool server slot for this platform.
  bool lowSlotsMessagePrinted;
};

// Data global to this file, protected by mu
static Basics::mutex mu;
static Platform* platforms;
static bool initialized = false;
static float negligibleExternalLoadPerCPU = 0.75;
static unsigned int myhosthash;
static Basics::cond toolDone;
static unsigned int usecountTotal = 0;

// Forward 
static Platform* LookupPlatform(TextVC *platform, SrcLoc *loc);

// 
// Host selection for the _run_tool primitive.  
//
// Goals: 
//
// - Always select a host that can build for the specified platform.  
//
// - Try to use a different host for each parallel thread.
//
// - Optionally prefer the local host for the first thread.
//
// - When clients on several machines are all doing parallel builds,
// try to spread the load fairly evenly among the available hosts.
//
// - But from any particular client, try to use the same set of hosts
// repeatedly in order to increase NFS cache locality.
//
// - Try to avoid the following race condition: multiple threads in a
// _par_map try the same host at once, they all find it is not busy,
// and they all start tools on it.
//
// Algorithm:
//
//  1) Look up the platform in the Vesta configuration file to get a
//  pattern that the runtool server's operating system and machine
//  type info must match, and a list of candidate hosts.
//
//  2) Let p be a value that depends on the host where the evaluator
//  is running.  Cyclically permute the list by p places, except that
//  if the first host is the string "localhost", leave it in place and
//  permute only the remainder of the list.
//
//  3) Iterate through the list.  For each host, if the host (A) is
//  running a runtool server, (B) does not identify itself as being
//  the same as another host on the list, (C) matches the pattern, (D)
//  is currently running zero tools, and (E) reports a load average
//  below the threshold configured in
//  [Evaluator]NegligibleExternalLoadPerCPU, then select it and
//  return; otherwise continue.  To avoid the race condition mentioned
//  above, as soon as we select a host, we consider it to be running a
//  tool by incrementing a local counter, even if the tool has not
//  actually started yet.
//
//  4) If no host was selected in the previous step, then use the most
//  lightly loaded host of those considered, where load is measured as
//  the greater of the reported load average / number of CPUs or
//  cur_tools/max_tools.  Break ties in favor of hosts considered
//  earlier.
//
//  Note that in step 4, we perform a final check to make sure that we
//  don't submit more tools to the RunToolServer than it's configured
//  to accept.  If we would be, we wait for another thread to finish a
//  tool invocation, then search again for a host to submit the tool
//  to.
//
Text
RunToolHost(TextVC *platform, SrcLoc *loc, /*OUT*/ void*& handle)
{
  Host* host = NULL;
  mu.lock();
  try {
    // Look up the platform; see step (1) above.
    Platform* plat = LookupPlatform(platform, loc);
    int id = ThreadDataGet()->id;

    // Outer loop needed to handle the case where we can't find a slot
    // and need to wait.
    while(host == NULL)
      {
	// let's hope this is plenty big.  we really should use
	// numeric_limits<float>::infinity() but it doesn't exist in
	// our std_env
	float lightload = 3.4e38;

	int lighthost = 0;
	RunTool::Host_info light_hinfo;

	for (unsigned int ii=0; ii<plat->nhosts; ii++) {
	  unsigned int i;
	  // Permute host list; see step (2) above.
	  if (plat->anchorfirst) {
	    i = ii ? ((ii + myhosthash) % (plat->nhosts - 1)) + 1 : 0;
	  } else {
	    i = (ii + myhosthash) % plat->nhosts;
	  }

	  // Consider host i; see step (3) above.
	  if (plat->hosts[i].bad) continue;
	  RunTool::Host_info hinfo;
	  try {
	    RunTool::get_info(plat->hosts[i].name, /*OUT*/hinfo);
	  } catch (SRPC::failure f) {
	    cio().start_err() << "Warning: failed to contact runtool server on host \""
			      << plat->hosts[i].name << "\": " << f.msg << endl;
	    cio().end_err();	
	    plat->hosts[i].bad = true;
	    continue;
	  }

	  // On first look at host i, check that it's not a duplicate.
	  if (hinfo.uniqueid != plat->hosts[i].uniqueid) {
	    int j;
	    plat->hosts[i].uniqueid = hinfo.uniqueid;
	    for (j=0; j<plat->nhosts; j++) {
	      if (j != i && !plat->hosts[j].bad &&
		  plat->hosts[j].uniqueid == plat->hosts[i].uniqueid) {
		plat->hosts[i].bad = true;
		break;
	      }
	    }
	    if (plat->hosts[i].bad) continue;
	  }

	  // Check if platform description pattern matches this host
	  if(fnmatch(plat->sysname.cchars(), hinfo.sysname.cchars(), 0) != 0 ||
	     fnmatch(plat->release.cchars(), hinfo.release.cchars(), 0) != 0 ||
	     fnmatch(plat->version.cchars(), hinfo.version.cchars(), 0) != 0 ||
	     fnmatch(plat->machine.cchars(), hinfo.machine.cchars(), 0) != 0 ||
	     hinfo.cpus < plat->cpus ||
	     hinfo.cpuMHz < plat->cpuMHz ||
	     hinfo.memKB < plat->memKB) {

	    // No
	    if (i > 0) {
	      cio().start_err() << "Warning: runtool server on host \""
				<< plat->hosts[i].name 
				<< "\" does not match platform \""
				<< plat->name << "\"." << endl;
	      cio().end_err();
	    }
	    plat->hosts[i].bad = true;
	    continue;
	  }	

	  // Add usage by local threads to the load, since there is a race
	  // condition between asking for the load and starting another
	  // tool, and we're quite likely to race against other local
	  // threads from the same _par_map.  This will cause us to
	  // overestimate the load if the local threads have actually
	  // gotten their tools started already, but that's much better
	  // than undercounting it and dumping multiple tools on the same
	  // machine at once.
	  int cur_tools = hinfo.cur_tools + plat->hosts[i].usecount;

	  // If host has no running tool invocations and the reported
	  // load is below the threshold, pick it immediatly.
	  //
	  float max_load = hinfo.cpus * negligibleExternalLoadPerCPU;
	  if (cur_tools == 0 && hinfo.load < max_load) {
	    host = &plat->hosts[i];
	    break;
	  }

	  // Keep track of lightest load for step (3).
	  float tool_load = ((float) cur_tools)/((float) hinfo.max_tools);
	  float host_load = hinfo.load / hinfo.cpus;
	  float load;
	  if(host_load < tool_load) {
	    load = tool_load;
	  } else {
	    load = host_load;
	  }
	  if (load < lightload) {
	    lighthost = i;
	    lightload = load;
	    light_hinfo = hinfo;
	  }
	}

	if (host == NULL) {
	  if (plat->hosts[lighthost].bad) {
	    // We get here if there are no hosts that are up and match the
	    // pattern
	    ostream& err_stream = cio().start_err();
	    Error(err_stream, "No runtool server found for platform ", loc);
	    platform->PrintD(err_stream);
	    ErrorDetail(err_stream, ".\n");
	    cio().end_err();
	    throw(Evaluator::failure(Text("exiting"), false));
	  }

	  // Step 4: We didn't find an unloaded host; use the most
	  // lightly loaded.

	  // Compute the total number of tools we think may have been
	  // submitted to this RunToolServer.  As noted above, this can be
	  // an overestimate, but is needed to avoid a race.
	  int total_tools = (light_hinfo.cur_tools + light_hinfo.cur_pending +
			     plat->hosts[lighthost].usecount);

	  // If we think that submitting another tool may result in a "too
	  // busy" error from the RunToolServer, wait for another thread
	  // to finish its tool invocation and try again to select a
	  // RunToolServer.
	  if(total_tools >= (light_hinfo.max_tools + light_hinfo.max_pending))
	    {
	      // As long as there's at least one thread that's already
	      // performing a tool invocation, we'll wait for one to
	      // finish.
	      if(usecountTotal > 0)
		{
		  // If we haven't already printed a message about
		  // having trouble finding a RunToolServer slot for
		  // this platform, print one now.
		  if(!plat->lowSlotsMessagePrinted)
		    {
		      cio().start_err() << "Warning:  Difficulty finding an available "
					<< "runtool server slot for platform \""
					<< plat->name << "\".   You may see less "
					<< "parallelism than expected." << endl;
		      cio().end_err();
		      plat->lowSlotsMessagePrinted = true;
		    }

		  // Wait for another thread to complete a tool invocation.
		  toolDone.wait(mu);
		}
	      else
		{
		  // Error: we're not running any tools, so waiting for
		  // another thread to finish one would put us to sleep
		  // forever!
		  Text message =
		    Text("With no tools running, no available runtool server "
			 "slots for platform \"") +
		    plat->name + "\"";
		  Error(cio().start_err(), message, loc);
		  cio().end_err();
		  throw(Evaluator::failure(Text("can't start tool"), false));
		}
	    }
	  // We can safely submit another tool to this RunToolServer.
	  else
	    {
	      host = &plat->hosts[lighthost];
	    }
	}
      }

  } catch (...) {
    mu.unlock();
    throw;
  }
  host->usecount++;
  usecountTotal++;
  mu.unlock();
  handle = (void*) host;
  return host->name;
}

void
RunToolDone(void* handle)
{
  Host* host = (Host*) handle;
  mu.lock();
  host->usecount--;
  usecountTotal--;
  mu.unlock();

  // Wake up any threads that were waiting for an available
  // RunToolServer slot.  We use broadcast as the conditions may have
  // changed significantly enough that more than one waiting thread
  // can now get a slot to start a tool.
  toolDone.broadcast();
}

//
// Look up a platform name.  mu must be held.
//
static Platform*
LookupPlatform(TextVC *platform, SrcLoc *loc)
{
  Text platname = platform->NDS().chars();
  Platform* plat = platforms;

  // Previously seen platform?
  while (plat) {
    if (platname == plat->name) return plat;
    plat = plat->next;
  }

  // No, look in config file
  plat = NEW(Platform);
  plat->name = platname;
  Text hosts;
  try {
    plat->sysname = VestaConfig::get_Text(platname, "sysname");
    plat->release = VestaConfig::get_Text(platname, "release");
    plat->version = VestaConfig::get_Text(platname, "version");
    plat->machine = VestaConfig::get_Text(platname, "machine");
    plat->cpus =    VestaConfig::get_int (platname, "cpus");
    plat->cpuMHz =  VestaConfig::get_int (platname, "cpuMHz");
    plat->memKB =   VestaConfig::get_int (platname, "memKB");
    hosts =         VestaConfig::get_Text(platname, "hosts");
  } catch (VestaConfig::failure f) {
    ostream& err_stream = cio().start_err();
    Error(err_stream, "Unknown platform ", loc);
    platform->PrintD(err_stream);
    ErrorDetail(err_stream, ": " + f.msg + ".\n");
    cio().end_err();
    throw(Evaluator::failure(Text("exiting"), false));
  }

  // Count hosts on list
  plat->nhosts = 0; 
  int len = hosts.Length();
  int i = 0;
  for (;;) {
    while (i < len && isspace(hosts[i])) i++;
    if (i >= len) break;
    plat->nhosts++;
    while (i < len && !isspace(hosts[i])) i++;
  }
      
  // Allocate and fill in data structure
  plat->hosts = NEW_ARRAY(Host, plat->nhosts);
  int h = 0;
  i = 0;
  for (;;) {
    while (i < len && isspace(hosts[i])) i++;
    if (i >= len) break;
    int j = i;
    while (j < len && !isspace(hosts[j])) j++;
    plat->hosts[h].name = hosts.Sub(i, j-i);
    i = j + 1;
    h++;
  }

  if (plat->anchorfirst = ((plat->nhosts > 0) &&
			   (plat->hosts[0].name == Text("localhost")))) {
    // Use canonical name for local host
    plat->hosts[0].name = TCP_sock::this_host();
  }

  plat->lowSlotsMessagePrinted = false;

  plat->next = platforms;
  platforms = plat;
  return plat;
}

//
// Initialize module.
//
void
RunToolHostInit()
{
  mu.lock();
  if (!initialized) {
    initialized = true;
    Word full_hash = TCP_sock::this_host().Hash();
    myhosthash = (unsigned int) full_hash ^ (full_hash >> (sizeof(unsigned int)*8));

    try
      {
	// Set negligibleExternalLoadPerCPU from
	// [Evaluator]NegligibleExternalLoadPerCPU (if set in the config
	// file).
	if(VestaConfig::is_set("Evaluator", "NegligibleExternalLoadPerCPU"))
	  {
	    negligibleExternalLoadPerCPU = VestaConfig::get_float("Evaluator",
								  "NegligibleExternalLoadPerCPU");
	  }
      }
    catch (VestaConfig::failure f)
      {
	Error(cio().start_err(), f.msg);
	cio().end_err();
      }
  }
  mu.unlock();
}
