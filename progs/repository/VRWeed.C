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

//
// VRWeed.C
//
// Miscellaneous repository weeding code.  The mark-and-sweep work
// for source weeding is done in VDirChangeable.C and VMemPool.C.
// Actual SourceOrDerived deletion is done in ShortIdImpl.C.

#if __linux__
#include <stdint.h>
#endif
#include "VRWeed.H"
#include "VMemPool.H"
#include "VestaSource.H"
#include "VDirChangeable.H"
#include "VDirEvaluator.H"
#include "VForward.H"
#include "ReadersWritersLock.H"
#include "SourceOrDerived.H"
#include "VestaLog.H"
#include "Recovery.H"
#include "VestaConfig.H"
#include "ShortIdImpl.H"
#include "VRConcurrency.H"
#include "VestaAttribsRep.H"
#include "DirShortId.H"
#include "Mastership.H"
#include "logging.H"
#include <Thread.H>
#include <fstream>
#include <sys/time.h>
#include <BufStream.H>
#include "MutableSidref.H"
#include "SidSort.H"
#include <Units.H>

using Basics::OBufStream;
using std::hex;
using std::dec;
using std::ifstream;
using std::fstream;
using std::ios;

#include "lock_timing.H"

// Module globals
static Basics::mutex state_mu; // protects the following:
static ShortId keepSourceSid = NullShortId, keepDerivedSid = NullShortId;
static time_t keepSourceTime = 0, keepDerivedTime = 0;
static bool deletionsDone = true;
static bool sourceWeedDone = false;
static bool checkpointInProgress = false;
static bool checkpointRequested = false;
static bool deletionsInProgress = false;
static bool sourceWeedInProgress = false; // logical lock, protects:
static FILE* keepSourceSidFile = NULL;
// end protected by sourceWeedInProgress
static Basics::cond state_cond; // wait for an inProgress or requested
				// flag to change
// end protected by state_mutex

// Recovery code
static void
KeepCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    unsigned long ulsid;
    long lsec;
    char kind;
    rr->skipWhite(c);
    kind = c;
    rr->get(c);
    rr->getULong(c, ulsid);
    rr->getLong(c, lsec);
    switch (kind) {
      case 's':
	keepSourceSid = (ShortId) ulsid;
	keepSourceTime = lsec;
	break;
      case 'd':
	keepDerivedSid = (ShortId) ulsid;
	keepDerivedTime = lsec;
	keepSourceSid = NullShortId;
	keepSourceTime = 0;
	break;
      default:
	assert(false);
    }
    deletionsDone = false;
}

static void
DdunCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    deletionsDone = true;
}

static void
NpinCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    // Obsolete; read and ignore
    unsigned long ulnpin;
    rr->getULong(c, ulnpin);
}

static void
FsidCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    unsigned long ulsid;
    rr->skipWhite(c);
    while (c != ')') {
	rr->getULong(c, ulsid);
	DeleteDirShortId((ShortId) ulsid);
	rr->skipWhite(c);
    }
}

void
InitVRWeed()
{
    RegisterRecoveryCallback("keep", KeepCallback);
    RegisterRecoveryCallback("ddun", DdunCallback);
    RegisterRecoveryCallback("npin", NpinCallback);
    RegisterRecoveryCallback("fsid", FsidCallback);

    // Register garbage collection routines
    VMemPool::registerCallbacks(VMemPool::vDirChangeable,
				VDirChangeable::markCallback, NULL,
				VDirChangeable::sweepCallback,
				(void*) &keepSourceSidFile,
				VDirChangeable::rebuildCallback, NULL);

    VMemPool::registerCallbacks(VMemPool::vDirImmutable,
				VDirChangeable::markCallback, NULL,
				VDirChangeable::sweepCallback,
				(void*) &keepSourceSidFile,
				VDirChangeable::rebuildCallback, NULL);
    
    VMemPool::registerCallbacks(VMemPool::vDirAppendable,
				VDirChangeable::markCallback, NULL,
				VDirChangeable::sweepCallback,
				(void*) &keepSourceSidFile,
				VDirChangeable::rebuildCallback, NULL);
    
    VMemPool::registerCallbacks(VMemPool::vForward,
				VForward::markCallback, NULL,
				VForward::sweepCallback, NULL,
				VForward::rebuildCallback, NULL);
    
    VMemPool::registerCallbacks(VMemPool::vDirEvaluator,
				VDirEvaluator::markCallback, NULL,
				VDirEvaluator::sweepCallback, NULL,
				VDirEvaluator::rebuildCallback, NULL);

    VMemPool::registerCallbacks(VMemPool::vAttrib,
				VestaAttribsRep::markCallback, NULL,
				VestaAttribsRep::sweepCallback, NULL,
				VestaAttribsRep::rebuildCallback, NULL);
}

static void
LogKeep(char kind, ShortId sid, time_t time)
{
    StableLock.acquireWrite();
    RWLOCK_LOCKED_REASON(&StableLock, "Weeding:LogKeep");
    {
	char logrec[512];
	OBufStream ost(logrec, sizeof(logrec));
	// ('keep' kind sid time)
	ost << "(keep " << kind
	  << " 0x" << hex << sid << " " << dec
	  << time << ")\n";
	VRLog.start();
	VRLog.put(ost.str());
	VRLog.commit();
    }
    StableLock.releaseWrite();
}

int
KeepDerived(ShortIdsFile ds, time_t dt, bool force)
{
    if (ds != NullShortId) {
      // Sanity check that ds exists
      int fd = SourceOrDerived::fdopen(ds, O_RDONLY);
      if (fd == -1) {
	assert(errno != 0);
	return errno;
      }
      close(fd);
    }

    // Log the new ds, dt
    state_mu.lock();
    while (checkpointInProgress || sourceWeedInProgress ||
	   sourceWeedDone || deletionsInProgress) {
      state_cond.wait(state_mu);
    }
    if (!force && keepDerivedSid == ds && keepDerivedTime == dt &&
	deletionsDone) {
      // This weed already succeeded
      state_mu.unlock();
      return 0;
    }
    Repos::dprintf(DBG_ALWAYS, 
		   "keep derived: keepDerivedTime %d, sid 0x%08x\n", dt, ds);
    keepDerivedSid = ds;
    keepDerivedTime = dt;
    keepSourceSid = NullShortId;
    keepSourceTime = 0;
    deletionsDone = false;
    LogKeep('d', ds, dt);
    state_mu.unlock();

    // Do source weed and deletions
    int err = SourceWeed();
    if (err) return err;
    return DoDeletions();
}

int
SourceWeed() throw ()
{
    int err = 0, res;
    state_mu.lock();
    while (checkpointInProgress ||
	   sourceWeedInProgress || deletionsInProgress) {
	state_cond.wait(state_mu);
    }	
    if (deletionsDone) {
	// Nothing to do
	state_mu.unlock();
	return 0;
    } 
    sourceWeedInProgress = true;
    state_mu.unlock();
    state_cond.broadcast();

    time_t newKeepSourceTime = time(NULL) - 30;  // fudge to be extra safe
    ShortId newKeepSourceSid;
    int fd = SourceOrDerived::fdcreate(newKeepSourceSid);
    if (fd < 0) {
	assert (errno != 0);
	err = errno;
	goto done;
    }
    keepSourceSidFile = fdopen(fd, "w");
    if (fd < 0 || keepSourceSidFile == NULL) {
	assert (errno != 0);
	err = errno;
	goto done;
    }
    Repos::dprintf(DBG_ALWAYS, "source weed: keepSourceTime %d, sid 0x%08x\n",
		   newKeepSourceTime, newKeepSourceSid);
    // Keep this ShortIdsFile itself, because we want the deletion phase
    //  to be restartable after a crash.
    res = fprintf(keepSourceSidFile, "%08x\n", newKeepSourceSid);
    if (res < 0) {
	assert (errno != 0);
	err = errno;
	goto done;
    }

    VDirVolatileRoot::lockAll();
    StableLock.acquireWrite();
    RWLOCK_LOCKED_REASON(&VolatileRootLock, "Weeding:SourceWeed");
    RWLOCK_LOCKED_REASON(&StableLock, "Weeding:SourceWeed");
    VMemPool::gc(keepDerivedSid);
    StableLock.releaseWrite();
    VDirVolatileRoot::unlockAll();

    res = fflush(keepSourceSidFile);
    if (res == EOF) {
	assert (errno != 0);
	err = errno;
	goto done;
    }
    res = fsync(fd);
    if (res < 0) {
	assert (errno != 0);
	err = errno;
	goto done;
    }
    res = fclose(keepSourceSidFile);
    if (res == EOF) {
	assert (errno != 0);
	err = errno;
	goto done;
    }

  done:
    state_mu.lock();
    if (err == 0) {
	keepSourceTime = newKeepSourceTime;
	keepSourceSid = newKeepSourceSid;
	deletionsDone = false;
	LogKeep('s', newKeepSourceSid, newKeepSourceTime);
	Repos::dprintf(DBG_ALWAYS, "source weed done\n");
    } else {
	Text msg = Basics::errno_Text(err);
	Repos::dprintf(DBG_ALWAYS, "source weed failed: %s (errno = %d)."
			"  keepSourceSidFile is 0x%08x\n",
		       msg.cchars(), err, newKeepSourceSid);
    }
    sourceWeedInProgress = false;
    sourceWeedDone = true;
    state_mu.unlock();
    state_cond.broadcast();
    return err;
}

// Check whether a file exists and has a size greater than 0.  Used
// for sanity check on the file of shortids to keep.
static bool file_is_non_empty(const char *p_fname) throw()
{
  struct stat l_info;

  // If we can stat the file and its size is greater than 0, it's
  // non-empty.
  return ((stat(p_fname, &l_info) == 0) &&
	  (l_info.st_size > 0));
}

// Check whether the final character in a file is a newline.  Used for
// sanity check on the file of shortids to keep.
static bool file_ends_with_newline(const char *p_fname) throw()
{
  // Try to open the file
  ifstream l_file(p_fname);
  if(l_file.good())
    {
      // Go to one character before the end of the file.
      l_file.seekg(-1, fstream::end);

      Repos::dprintf(DBG_ALWAYS,
		     "file_ends_with_newline: position after seek: %d\n",
		     (unsigned int) l_file.tellg());

      if(l_file.good())
	{
	  // If it's a newline, return true.
	  int l_next = l_file.peek();
	  Repos::dprintf(DBG_ALWAYS,
			 "file_ends_with_newline: last character in file: %d\n",
			 l_next);
	  if(l_next == '\n')
	    {
	      return true;
	    }
	}
      else
	{
	  Repos::dprintf(DBG_ALWAYS,
			 "file_ends_with_newline: seek caused error "
			 "(l_file.good() is false after seek)\n");
	}
    }
  else
    {
      Repos::dprintf(DBG_ALWAYS,
		     "file_ends_with_newline: couldn't open %s "
		     "(l_file.good() is false after open)\n", p_fname);
    }

  return false;
}

static void prepare_sidfile_env_vars(/*OUT*/ Sequence<Text> &env_vars,
				     const Text &md_root,
				     ShortId sid,
				     const char *name)
{
  if(sid  == NullShortId)
    {

      env_vars.addhi(Text::printf("VWEED_%s_SHORTID=0", name));
      env_vars.addhi(Text::printf("VWEED_%s_FILE=", name));
    }
  else
    {
      env_vars.addhi(Text::printf("VWEED_%s_SHORTID=%08x", name, sid));
      char *sid_fname = SourceOrDerived::shortIdToName(sid);
      env_vars.addhi(Text::printf("VWEED_%s_FILE=%s%s", name, md_root.cchars(), sid_fname));
      delete [] sid_fname;
    }
}

// Not all platofrms provide this extern declaration in the system
// headers.  It's harmless to repeat, so we just add it here.
extern char **environ;

static void run_weeding_hook(const Text &config_var_name,
			     time_t keep_time,
			     ShortId also_keep_list_sid,
			     ShortId keep_list_sid = NullShortId,
			     ShortId delete_list_sid = NullShortId)
{
  Text command;
  if(!VestaConfig::get("Repository", config_var_name, command))
    // Do nothing if the config variable isn't set
    return;

  // Get the repository metadata root.  The repository normally
  // accesses shortid files as relative paths, but we want to pass
  // full paths to the hook command.
  Text md_root = VestaConfig::get_Text("Repository", "metadata_root");

  Sequence<Text> new_env_vars_seq;
  unsigned int old_env_vars_count = 0;
  char **env_loop = environ;
  while((env_loop != 0) && (*env_loop != 0))
    {
      old_env_vars_count++;
      env_loop++;
    }

  // Set environment variables to communicate information about the
  // files to be kept and deleted to the hook command.
  new_env_vars_seq.addhi(Text::printf("VWEED_KEEP_TIME=%u", keep_time));
  prepare_sidfile_env_vars(new_env_vars_seq, md_root, keep_list_sid, "KEEP");
  prepare_sidfile_env_vars(new_env_vars_seq, md_root, delete_list_sid, "DELETED");
  prepare_sidfile_env_vars(new_env_vars_seq, md_root, also_keep_list_sid, "ALSO_KEEP");

  // Copy existing environment variables and new environment variables
  // into something we can pass to execve.
  char **new_env_vars = NEW_ARRAY(char *, old_env_vars_count + new_env_vars_seq.size() + 1);
  env_loop = environ;
  unsigned int new_env_vars_count = 0;
  while((env_loop != 0) && (*env_loop != 0))
    {
      new_env_vars[new_env_vars_count++] = *env_loop;
      env_loop++;
    }
  for(unsigned int new_env_i = 0; new_env_i < new_env_vars_seq.size(); new_env_i++)
    {
      new_env_vars[new_env_vars_count++] = new_env_vars_seq.get_ref(new_env_i).chars();
    }
  assert(new_env_vars_count == (old_env_vars_count + new_env_vars_seq.size()));
  new_env_vars[new_env_vars_count] = 0;

  // Create the command line to execute.
  char *hook_argv[4];
  hook_argv[0] = "/bin/sh";
  hook_argv[1] = "-c";
  hook_argv[2] = command.chars();
  hook_argv[3] = 0;

  //Remember the maximum number of file descriptors
  int max_fd = getdtablesize();

  Repos::dprintf(DBG_ALWAYS,
		 "Running %s\n", config_var_name.chars());

  pid_t child_pid = fork();
  switch (child_pid)
    {
    case -1:
      {
	int errno_save = errno;
	Text err_str = Basics::errno_Text(errno_save);
	Repos::dprintf(DBG_ALWAYS,
		       "Couldn't start %s (fork(2) failed): %s, (errno = %d)\n",
		       config_var_name.chars(), err_str.chars(), errno_save);
      }
      break;
    case 0:
      // We're the child
      {
	// Set close-on-exec for file descriptors, ignoring errors
	for(int fd = 3; fd < max_fd; fd++)
	  fcntl(fd, F_SETFD, FD_CLOEXEC);
	execve("/bin/sh", hook_argv, new_env_vars);
	// This should be unreachable
	abort();
      }
      break;
    default:
      // We're the parent
      {
	int child_status;
	pid_t done_pid = waitpid(child_pid, &child_status, 0);
	assert(done_pid == child_pid);
	if(WEXITSTATUS(child_status))
	  {
	    // Hook indicates failure
	    Repos::dprintf(DBG_ALWAYS, "%s exited with error (%d)\n",
			   config_var_name.chars(), WEXITSTATUS(child_status));
	  }
	else if(WIFSIGNALED(child_status))
	  {
	    // Hook command was killed by a signal
	    Repos::dprintf(DBG_ALWAYS, "%s was was killed by a signal (%d)\n",
			   config_var_name.chars(), WTERMSIG(child_status));
	  }
	else
	  {
	    Repos::dprintf(DBG_ALWAYS,
			   "%s finished\n", config_var_name.chars());

	  }
      }
      break;
    }

  delete [] new_env_vars;
}

// We want to make certain files read-only before invoking the hooks.
static void remove_write_permission(const char *path)
{
  assert(path != 0);
  assert(path[0] != 0);
  struct stat st;
  int res = stat(path, &st);
  assert(res != -1);
  res = chmod(path, (st.st_mode & ~0222));
  assert(res != -1);
}

static void remove_write_permission(ShortId file_sid)
{
  char *fname = SourceOrDerived::shortIdToName(file_sid);
  remove_write_permission(fname);
  delete [] fname;
}

int
DoDeletions() throw ()
{
    state_mu.lock();
    while (checkpointInProgress ||
	   sourceWeedInProgress || deletionsInProgress) {
	state_cond.wait(state_mu);
    }	
    if (deletionsDone) {
	// Nothing to do
	state_mu.unlock();
	return 0;
    } 
    ShortIdsFile ds = keepDerivedSid;
    time_t dt = keepDerivedTime;
    ShortIdsFile ss = keepSourceSid;
    time_t st = keepSourceTime;
    time_t keep_time = (st < dt) ? st : dt;
    deletionsInProgress = true;
    sourceWeedDone = false;
    state_mu.unlock();
    state_cond.broadcast();
    
    Basics::uint32 deleted_count = 0;
    Basics::uint64 deleted_space = 0;

    int ret = 0;
    int status;
    char* oname;
    Repos::dprintf(DBG_ALWAYS, "starting deletions\n");
    const char* sname = (ss == NullShortId) ? ""
      : SourceOrDerived::shortIdToName(ss);
    const char* dname = (ds == NullShortId) ? ""
      : SourceOrDerived::shortIdToName(ds);
    int fd;
    // If a hook is configured to supply an additional keep list...
    const char *ename = 0;
    ShortIdsFile esidfile = NullShortId;
    if(VestaConfig::is_set("Repository", "also_keep_weed_hook"))
      {
	fd = SourceOrDerived::fdcreate(esidfile); 
	if (fd < 0) { 
	  ret = errno; 
	  Text emsg = Basics::errno_Text(ret); 
	  Repos::dprintf(DBG_ALWAYS,  
			 "failed to create extra keepSidFile: " 
			 "%s (errno = %d)\n", 
			 emsg.cchars(), ret); 
	  assert(ret != 0); 
	  goto finish; 
	} 
	close(fd); 
	ename = SourceOrDerived::shortIdToName(esidfile);

	// Run the pre-deletion weeding hook
	run_weeding_hook("also_keep_weed_hook",
			 keep_time, esidfile);

	// Make it read-only.
	remove_write_permission(ename);
      }

    // Create a file to contain the combined list of shortids.
    ShortIdsFile osidfile;
    fd = SourceOrDerived::fdcreate(osidfile);
    if (fd < 0) {
	ret = errno;
	Text emsg = Basics::errno_Text(ret);
	Repos::dprintf(DBG_ALWAYS, 
		       "failed to create combined keepSidFile: "
		       "%s (errno = %d)\n",
		       emsg.cchars(), ret);
	assert(ret != 0);
	goto finish;
    }
    close(fd);
    // Sort sids and eliminate duplicates.
    oname = SourceOrDerived::shortIdToName(osidfile);
    try
      {
	sidsort(oname, sname, dname, ename);
      }
    catch(Text e)
      {
	Repos::dprintf(DBG_ALWAYS, "sidsort failed: %s\n", e.cchars());
	ret = EINVAL;
      }
    catch(FS::DoesNotExist)
      {
	Repos::dprintf(DBG_ALWAYS,
		       "sidsort failed: one of the input files "
		       "(%s, %s) doesn't exist\n",
		       sname, dname);
	ret = ENOENT;
      }
    catch(FS::Failure f)
      {
	ret = f.get_errno();
	if(ret != 0)
	  {
	    Text emsg = Basics::errno_Text(ret);
	    Repos::dprintf(DBG_ALWAYS,
			   "sidsort failed: %s of %s: %s\n",
			   f.get_op().cchars(), f.get_arg().cchars(),
			   emsg.cchars());
	  }
	else
	  {
	    Repos::dprintf(DBG_ALWAYS,
			   "sidsort failed: %s of %s\n",
			   f.get_op().cchars(), f.get_arg().cchars());
	    ret = EINVAL;
	  }
      }
    delete [] sname;
    delete [] dname;
    if(ename != 0) delete [] ename;

    // No more changes should be made to the combined file of shortids
    // to keep.
    remove_write_permission(oname);

    // Perform two sanitry checks on the combined file of shortids to
    // keep:

    // 1. If it's empty (indicating that all shortids are to be
    // deleted), refuse to do deletions.  The only legitimate way for
    // this to happen is a degenerate case.  If you really want to
    // delete everything, delete the repository's metadata and start
    // fresh.  For the sake of safety, we assume that an empty keep
    // list means that something went wrong with sidsort or the
    // generation of one of the files fed to it.

    // 2. If the final character in the file is not a newline
    // (suggesting that it was truncated), refuse to do deletions.  If
    // there's any chance that the keep list is incomplete, we err on
    // the side of caution and don't proceed with deletions.
    if(!file_is_non_empty(oname))
      {
	Repos::dprintf(DBG_ALWAYS,
		       "combined keepSidFile (0x%08x) is empty;"
		       " not doing deletions\n", osidfile);
	ret = EINVAL;
	goto finish;
      }
    else if(!file_ends_with_newline(oname))
      {
	Repos::dprintf(DBG_ALWAYS,
		       "combined keepSidFile (0x%08x) doen't end with a newline "
		       "(may be truncated); not doing deletions\n", osidfile);
	ret = EINVAL;
	goto finish;
	
      }

    delete [] oname;

    Repos::dprintf(DBG_ALWAYS, "wrote combined keepSidFile 0x%08x\n", osidfile);

    // Allocate another shortid for the record of files deleted during weeding
    ShortIdsFile dsidfile;
    fd = SourceOrDerived::fdcreate(dsidfile);
    if (fd < 0) {
	ret = errno;
	Text emsg = Basics::errno_Text(ret);
	Repos::dprintf(DBG_ALWAYS, 
		       "failed to create deletedSidFile: "
		       "%s (errno = %d)\n",
		       emsg.cchars(), ret);
	assert(ret != 0);
	goto finish;
    }
    close(fd);

    // Run the pre-deletion weeding hook
    run_weeding_hook("pre_delete_weed_hook",
		     keep_time, esidfile, osidfile);

    ret = DeleteAllShortIdsBut(osidfile, keep_time, dsidfile,
			       deleted_count, deleted_space);

    // No more changes should be made to the file listing deleted
    // shortids.
    remove_write_permission(dsidfile);

    Repos::dprintf(DBG_ALWAYS, "wrote deletedSidFile 0x%08x\n", dsidfile);

    // Run the post-deletion weeding hook
    run_weeding_hook("post_delete_weed_hook",
		     keep_time, esidfile, osidfile, dsidfile);

    {
      Text deleted_space_print = Basics::FormatUnitVal(deleted_space);
      Repos::dprintf(DBG_ALWAYS, "deleted %u files reclaiming %s of disk space\n",
		     deleted_count, deleted_space_print.cchars());
    }
    
  finish:
    state_mu.lock();
    if (ret == 0 && ds == keepDerivedSid && dt == keepDerivedTime &&
	ss == keepSourceSid && st == keepSourceTime) {
	StableLock.acquireWrite();
	RWLOCK_LOCKED_REASON(&StableLock, "Weeding:deletions done");
	VRLog.start();
	VRLog.put("(ddun)\n");
	VRLog.commit();
	StableLock.releaseWrite();
	deletionsDone = true;
	Repos::dprintf(DBG_ALWAYS, "deletions done\n");
    } else {
	Repos::dprintf(DBG_ALWAYS, "deletions failed\n");
    }
    deletionsInProgress = false;
    state_mu.unlock();
    state_cond.broadcast();
    return ret;
}

// Implementation of the StartCheckpoint SRPC call.  Write a checkpoint,
//  then immediately read back the VMemPool part of it.  This uses a
//  "server" thread that is known to have a large enough stack to handle
//  deep recursion (namely, the main thread).
void
Checkpoint() throw ()
{
    // Kick the CheckpointServer and wait for it to finish
    state_mu.lock();
    while (checkpointRequested) {
	state_cond.wait(state_mu);
    }
    checkpointRequested = true;
    state_cond.broadcast();
    while (checkpointInProgress || checkpointRequested) {
	state_cond.wait(state_mu);
    }
    state_mu.unlock();
    return;
}

void
CheckpointServer() throw ()
{
    for (;;) {
	state_mu.lock();
	while (!checkpointRequested ||
	       sourceWeedInProgress || deletionsInProgress) {
	    state_cond.wait(state_mu);
	}
	Repos::dprintf(DBG_ALWAYS, "starting checkpoint\n");
	assert(!checkpointInProgress);
	checkpointInProgress = true;
	checkpointRequested = false;
	state_mu.unlock();
	state_cond.broadcast();

	VDirVolatileRoot::lockAll();
	StableLock.acquireWrite();

	RWLOCK_LOCKED_REASON(&VolatileRootLock, "Weeding:CheckpointServer");
	RWLOCK_LOCKED_REASON(&StableLock, "Weeding:CheckpointServer");	

	fstream *ckpt = 0;
	try {
	  ckpt = VRLog.checkpointBegin(ios::in|ios::out);
	} catch(VestaLog::Error e) {
	  Repos::dprintf(DBG_ALWAYS, 
			 "creation of checkpoint file failed: "
			 "%s (errno = %d)\n", e.msg.cchars(), e.r);
	  assert(ckpt != 0); // crash
	}
	assert(ckpt->good()); // belt & suspenders
	
	VMemPool::writeCheckpoint(*ckpt);
	if (!ckpt->good()) {
	  int errno_save = errno;
	  Text emsg = Basics::errno_Text(errno_save);
	  Repos::dprintf(DBG_ALWAYS, 
			 "write to checkpoint file version %d failed: "
			 "%s (errno = %d)\n", VRLog.logVersion(),
			 emsg.cchars(), errno_save);
	  assert(ckpt->good());	// crash
	}
	
	// Checkpoint log version
	*ckpt << "(vers " << VRLogVersion << ")\n";

	ShortIdBlockCheckpoint(*ckpt);
	if (!ckpt->good()) {
	  int errno_save = errno;
	  Text emsg = Basics::errno_Text(errno_save);
	  Repos::dprintf(DBG_ALWAYS,
			 "write to checkpoint file version %d failed: "
			 "%s (errno = %d)\n", VRLog.logVersion(),
			 emsg.cchars(), errno_save);
	  assert(ckpt->good());	// crash
	}
	
	MastershipCheckpoint(*ckpt);
	if (!ckpt->good()) {
	  int errno_save = errno;
	  Text emsg = Basics::errno_Text(errno_save);
	  Repos::dprintf(DBG_ALWAYS,
			 "write to checkpoint file version %d failed: "
			 "%s (errno = %d)\n", VRLog.logVersion(),
			 emsg.cchars(), errno_save);
	  assert(ckpt->good());	// crash
	}

	// The two keeps and the ddun must be in this order!
	*ckpt << "(keep d 0x" << hex << keepDerivedSid << " "
	  << dec << keepDerivedTime << ")\n";
	*ckpt << "(keep s 0x" << hex << keepSourceSid << " "
	  << dec << keepSourceTime << ")\n";
	if (deletionsDone) {
	    *ckpt << "(ddun)\n";
	}
	
	VDirVolatileRoot::finishCheckpoint(*ckpt);

	if (!ckpt->good()) {
	  int errno_save = errno;
	  Text emsg = Basics::errno_Text(errno_save);
	  Repos::dprintf(DBG_ALWAYS,
			 "write to checkpoint file version %d failed: "
			 "%s (errno = %d)\n", VRLog.logVersion(),
			 emsg.cchars(), errno_save);
	  assert(ckpt->good());	// crash
	}
	
	ckpt->seekg(0, fstream::beg);
	VMemPool::readCheckpoint(*ckpt, true);
	ckpt->close();
	
	VRLog.checkpointEnd();
	
	// Paranoid check of the mutable root shortid reference count
	MutableSidrefCheck(0, LongId::noLock);

	StableLock.releaseWrite();
	VDirVolatileRoot::unlockAll();

	Repos::dprintf(DBG_ALWAYS, "checkpoint done\n");
	state_mu.lock();
	checkpointInProgress = false;
	state_mu.unlock();
	state_cond.broadcast();
    }
}

void GetWeedingState(ShortIdsFile& ds, time_t& dt,
		     ShortIdsFile& ss, time_t& st,
		     bool& sourceWeedInProgress,
		     bool& deletionsInProgress,
		     bool& deletionsDone,
		     bool& checkpointInProgress) throw ()
{
    state_mu.lock();
    ds = keepDerivedSid;
    dt = keepDerivedTime;
    ss = keepSourceSid;
    st = keepSourceTime;
    sourceWeedInProgress = ::sourceWeedInProgress;
    deletionsInProgress = ::deletionsInProgress;
    deletionsDone = ::deletionsDone;
    checkpointInProgress = ::checkpointInProgress;
    state_mu.unlock();
}
