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


/* This is the daemon/driver for the invocation of tools in an encapsulated
   context. It receives requests from remote Vesta evaluators (the _run_tool
   primitive) via the RunTool client and invokes a specified tool in a
   controlled context. */

#include "RunToolDaemonConfig.h"

#if defined(HAVE_STDINT_H)
// need this for rpc headers. 
# include <stdint.h>
#endif

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <Basics.H>
#include <BufStream.H>
#include <OS.H>
#include <VestaConfig.H>
#include <TCP_sock.H>
#include <SRPC.H>
#include <VestaSource.H>
#include <UniqueId.H>

#include <sys/utsname.h>         // Note: Unix specific
#include <sys/types.h>

#if defined(HAVE_SYS_SYSINFO_H)
# include <sys/sysinfo.h>
#endif
#if defined(HAVE_MACHINE_HAL_SYSINFO_H)
# include <machine/hal_sysinfo.h>
#endif

#if defined(HAVE_SYS_SYSCTL_H)
# include <sys/sysctl.h>
#endif

#if defined(HAVE_SYS_PROCESSOR_H)
# include <sys/processor.h>
#endif

#include "RunToolClient.H"
#include "RunToolDaemon.H"
#include "Launcher.h"

extern "C" {
#include "get_load.h"
}

using std::ios;
using std::ifstream;
using std::cout;
using std::cin;
using std::cerr;
using std::endl;
using Basics::IBufStream;

// Symbolic constants =======================================================

const Text DEVNULL("/dev/null");
const int UNUSED = SYSERROR; // indicates an unused slot in 'children'

// Data structures for request processing ===================================

static void close_fd(fd_t &fd) throw ();     // forward declaration

/* A 'std_info' object encapsulates a stream for relaying output
   (stdout or stderr) from a forked tool back to the Vesta evalutator.
*/
class std_info {
  public:
    sockaddr_in sock;      // remote endpoint (evaluator IP/port)
    TCP_sock *s;           // TCP connection to 'sock' (NULL => /dev/null)
    fd_t f;                // corresponding file descriptor
			   
    std_info() throw ();   // initialization
    ~std_info() throw ();  // cleanup
};

std_info::std_info() throw ()
{
    s = NULL;
    f = NO_FD;
}

std_info::~std_info() throw ()
{
    if (s == NULL) close_fd(f); 
    else delete s;
}

typedef Sequence<Basics::cond*> CondSeq;

/* Since the RunToolServer is a single, multi-threaded process that
   forks child processes, and the Unix mechanism for discovering when
   children complete does not understand about threads, a shared data
   structure is needed to record all child processes and their
   completion status. A child is entered into this data structure by
   the thread that forks it, which then waits for it to complete with
   waitpid(2).

   If the client requesting the tool invocation drops the connection
   before the child process completes, the child process is killed by
   a background thread.  This data structure records that the child
   was killed which informs the waiting thread of the child's fate
   when it wakes up.  */
class children {
  public:
    static void init(int running, int pending) throw ();
    /* Initialize the 'children' data structure. 'runninng' is the
       maximum number of concurrent child processes. 'pending' is the
       maximum number of tool invocations held pending until a slot is
       available. */

    static bool my_turn() throw(); 
    /* Called to decide whether a thread can start a tool to service a
       client request.  The return value indicates whether the caller
       can proceed.  A return value of false indicates that the
       maximum number of running and pending calls has been reached.
       May block for a period of time before returning. */

    static int wait_for(pid_t pid, SRPC *client, /*OUT*/ bool &killed)
      throw ();
    /* Enters the child process with ID 'pid' into this data structure
       and Blocks until it completes. Sets 'killed' to true or false
       as the child process was killed by the background "killer"
       thread or not. */

    static void killer_body() throw ();
    /* This procedure implements the body of a background thread that
       kills child processes whose corresponding clients have died. */

    static void call_failed() throw ();
    /* De-allocates a "running" slot allocated by my_turn in the event
       of failure to start the tool.  If a thread has called my_turn
       (above) and been allocated a slot for executing a tool (my_turn
       returned true), it must either call wait_for or this
       function. */

  private:
    // 'child_info' contains information about each child process
    class child_info {
      public:
	pid_t pid;            // pid defining a child process (may be UNUSED)
	SRPC *client;         // handle on connection to clien (may be NULL)
        bool killed;          // true iff child was killed by "killer" thread

	child_info() throw ()
        { this->pid = UNUSED; this->client = NULL;
          this->killed = false; }
    };

    // global fields
    static Basics::mutex mu;      // protects the following fields
    static int maxChildren;       // max running tools; set by 'init' method
    static int maxPending;        // max pending tools; set by 'init' method
    static child_info *info;      // array [0..maxChildren) of child_info
    static int nInUse;            // card(i | info[i].pid != UNUSED)
    static int nPending;          // Count of "pending" threads.
    static Basics::thread killer; // thread that kills children when
				  // client dies
    static CondSeq pending;       // condition variables used to wake
			          // up threads pending for their
			          // turn.

    friend void Info(SRPC *srpc, int intfVersion, int procId) throw(SRPC::failure);
};

/* A 'req_data' object represents the arguments and associated data
   structures associated with a single _run_tool request.
*/
class req_data {
  public:
    // RPC arguments
    Text stdin_file;          // non-empty => set up stdin from here
    std_info out;	      // relay for tool's stdout
    std_info err;	      // relay for tool's stderr
    sockaddr_in fsroot_sock;  // socket for server for fsroot
    LongId fsroot_fh;         // file handle for fsroot
    Text wd;                  // fsroot-relative path for working directory
    chars_seq ev;             // vector of "env=val" strings
    chars_seq argv;           // command and arguments
			      
    // auxilliary structures  
    fd_t stdin_fd;	      // file descriptor for tool's stdin
    Text chroot_to;           // chroot to here
    fd_t error_pipe[2];       // [0] is read end, [1] is write end
      /* 'error_pipe' is a pipe for reporting errors from the forked child
         process back to the parent. It is only used to report errors that
         occur in the child code that prepares for the launching of the
         external tool (namely, an error in either the chroot(2) or
         execv(2) system calls). */
			      
    req_data() throw ();      // initialization
    ~req_data() throw ();     // cleanup
};

req_data::req_data() throw ()
{
    /* Note: the default constructors for Text fields and the
       fields 'ev' and 'args' do just what we want. */
    stdin_fd = NO_FD;
    error_pipe[0] = NO_FD;
    error_pipe[1] = NO_FD;
}

req_data::~req_data() throw ()
{
    close_fd(stdin_fd);
    close_fd(error_pipe[0]);
    close_fd(error_pipe[1]);
}

// Global variables =========================================================

// "children" class variables (see comments above)
Basics::mutex children::mu;
int children::maxChildren;
int children::maxPending;
int children::nInUse;
int children::nPending;
children::child_info *children::info;
Basics::thread children::killer;
CondSeq children::pending;

// The following are constant after initialization by 'RunToolServerInit'
static Text helper;                // name of "helper" program
static chars_seq helper_switches;  // "helper" switches from config file
static fd_t trouble_fd;            // error reporting channel
static bool debug_mode = false;    // does 'helper_switches" contain "-d"?
static Text volatile_root;         // repos volatile root from config file
static Text sysname, release, version, machine;
static FP::Tag uniqueid;
static unsigned int cpus, cpuMHz, memKB;
static int sync_after_tool = 1;
static int hold_volatile_mount = -1;

// Utilities ================================================================

struct call_failure { int code; Text msg; };

static void call_failed(int c, const Text &m) throw (call_failure)
{
    call_failure f;
    f.code = c;
    f.msg = m;
    throw(f);
}

static void fail_with_errno(const Text &m, int e = 0) throw (call_failure)
{
    if (e == 0) e = errno;
    Text emsg = Basics::errno_Text(e);
    call_failed(implementation_error, m + " (" + emsg + ")");
}

static void PrintMsgWithErrno(const Text &msg) throw ()
{
  int saved_errno = errno;
  cerr << "\n*** Error: " << msg;
  cerr << "("
       << Basics::errno_Text(saved_errno)
       << ")" << endl;
  cerr << "*** Continuing..." << endl;
}

static Text config_lookup(const Text &key) throw (call_failure)
{
    try {
	Text val(VestaConfig::get_Text(RUN_TOOL_CONFIG_SECTION, key));
	return val;
    } catch (VestaConfig::failure f) {
	call_failed(implementation_error, f.msg);
    }
    return "";  // not reached
}

static void close_fd(fd_t &fd) throw()
{
    if (fd == NO_FD) return;
    close(fd);
    fd = NO_FD;
}

static void move_fd(fd_t from_fd, fd_t to_fd) throw ()
/* Renumbers the file descriptor 'from_fd' to 'to_fd' (preserving
   the reference count). */
{
    dup2(from_fd, to_fd);
    close_fd(from_fd);
}

// Support routines for RunTool =============================================

static void setup_stdin(/*INOUT*/ req_data &d) throw (call_failure)
/* Open 'd.fd' on 'd.stdin_file', or on DEVNULL if 'd.stdin_file' is
   the empty text. */
{
    const Text f(d.stdin_file.Empty() ? DEVNULL : d.stdin_file);
    d.stdin_fd = open(f.cchars(), O_RDONLY, 0);
    if (d.stdin_fd < 0) fail_with_errno("Can't open stdin file " + f);
}

static void setup_std_info(/*INOUT*/ std_info &i)
  throw (call_failure, TCP_sock::failure)
/* Set 'i.s' and 'i.f' from 'i.sock'. */
{
    if (i.sock.sin_addr.s_addr == 0 && i.sock.sin_port == 0) {
	i.s = NULL;
	i.f = open(DEVNULL.cchars(), O_WRONLY, 0);
	if (i.f == SYSERROR) {
	    fail_with_errno("Can't open " + DEVNULL + " for std{out,err}");
	}
    } else {
	// Create a TCP socket; connect it remotely
      i.s = NEW(TCP_sock);
	i.s->connect_to(i.sock);
	i.f = i.s->get_fd();
    }
}

// Note that simulate_child_failure must be async-signal-dafe: no
// memory allocations, formatted I/O, etc.  We also avoid C++
// exceptions, as those are probably not safe either.
static void simulate_child_failure(fd_t error_wr, fd_t fallback_wr,
				   int r,
				   const char *msg,
				   int e = 0)
     throw ()
{
  // Determine the errno, if any, and try to convert it to a useful
  // text message.
  if (e == 0) e = errno;
  Text emsg = Basics::errno_Text(e);

  // First try to write the error message to the error file
  // descriptor.
  if((write(error_wr, msg, strlen(msg)) == SYSERROR) ||
     (write(error_wr, " (", 2) == SYSERROR) ||
     (write(error_wr, emsg.cchars(), emsg.Length()) == SYSERROR) ||
     (write(error_wr, ")\n", 2) == SYSERROR) ||
     ((fsync(error_wr) == SYSERROR) &&
      // We ignore errors from fsync which can occur if we're writing
      // to a non-file (i.e. a pipe), which we probably are.
      (errno != EINVAL)))
    {
      // If we can't do that, try to write the error message to the
      // fall-back file descriptor.  In these we ignore errors,
      // because we're out of options if these fail.
      write(fallback_wr, msg, strlen(msg));
      write(fallback_wr, " (", 2);
      write(fallback_wr, emsg.cchars(), emsg.Length());
      write(fallback_wr, ")\n", 2);
      fsync(fallback_wr);
    }

  // Always exit with the status code passed to us.  Note that we must
  // use _exit(2), not exit(3), as exit(3) can call code which is not
  // aync-signal-safe.
  _exit(r);
}

// Implementation of class 'children' =======================================

static void *children_killer_body(void *arg) throw ()
/* Wrapper for service thread body; simply calls 'children::killer_body'. */
{
    children::killer_body();
    return (void *)NULL;
}

void children::init(int running, int pending) throw ()
{
    children::nInUse = 0;
    children::nPending = 0;
    children::maxChildren = running;
    children::maxPending = pending;
    children::info = NEW_ARRAY(child_info, running);
    children::killer.fork_and_detach(children_killer_body, NULL);
}

bool children::my_turn() throw()
{
  bool result;

  children::mu.lock();
  
  // Compute the number of running or soon-to-be-running threads.
  int nRunning =
    // Number currently running
    children::nInUse +
    // Number that have been signaled to wake up and run but haven't
    // been scheduled yet.
    (children::nPending - children::pending.size());
  assert(children::nPending >= children::pending.size());
  assert(nRunning >= 0);

  // If we can run more tools, return immediately.
  if(nRunning < children::maxChildren)
    {
      // For this to be true, all pending threads should have at least
      // been signaled.
      assert(children::pending.size() == 0);

      // There's one more thread running, and the caller can proceed.
      assert(children::nInUse < children::maxChildren);
      children::nInUse++;
      result = true;
    }
  // If we're below the pending limit, wait our turn.
  else if(children::pending.size() < children::maxPending)
    {
      // We're now waiting.
      children::nPending++;

      // There better be somebody to wake us up when they're done.
      assert(nRunning > 0);

      // When it's our turn, a thread completing a tool will signal
      // this condition variable for us.
      Basics::cond c;
      children::pending.addhi(&c);
      c.wait(children::mu);

      // There's now one one less thread pending, more thread running,
      // and the caller can proceed.
      assert(children::nInUse < children::maxChildren);
      children::nPending--;
      children::nInUse++;
      result = true;
    }
  // Too many requests: the caller doesn't get a turn.
  else
    {
      result = false;
    }

  children::mu.unlock();

  return result;
}

int children::wait_for(pid_t pid, SRPC *client, /*OUT*/ bool &killed) throw ()
/* This routine is called by the parent thread. It blocks until the child
   process 'pid' completes, and then returns the completion status. 'client'
   should be a handle on the connection back to the client that initiated
   the tool; if the connection back to the client dies, the child will be
   killed by the background "killer" thread, in which case 'killed' will
   be set to true. */
{
    int status;	     // completion status
    int slot = -1;   // index of an empty slot

    children::mu.lock();

    for(int i = 0; (i < children::maxChildren) && (slot < 0); i++)
      {
	if(children::info[i].pid == UNUSED)
	  {
	    slot = i; // slot to use for this child
	  }
      }
    assert(slot >= 0);

    // Add this child to 'children'
    assert(info[slot].pid == UNUSED);
    children::info[slot].pid = pid;
    children::info[slot].client = client;

    // Release our lock on the child data before waiting for process
    // completion.
    children::mu.unlock();

    // wait until child completes
    pid_t done_pid = waitpid(pid, &status, 0);
    // If there was an error, report it.
    if(done_pid == SYSERROR)
    {
      int l_errno = errno;
      cerr << "waitpid(pid = " << pid << ") returned an error: "
	   << strerror(l_errno) << " (" << l_errno << ")"
	   << endl;
    }
    // ...but we don't expect an error.
    assert(done_pid == pid);

    // Re-acquire a lock on the child data so we can free up our slot
    // and complete.
    children::mu.lock();

    // extract exit status and clean up
    killed = children::info[slot].killed;
    children::info[slot].pid = UNUSED;  // mark slot free
    children::nInUse--;
    children::info[slot].client = NULL; // drop SRPC object for GC
    children::info[slot].killed = false;

    // If there are some pending requests, wake up the next one in
    // line.
    if(children::pending.size() > 0)
      {
	Basics::cond *c = children::pending.remlo();
	assert(c != 0);
	c->signal();
      }
    assert(children::nPending >= children::pending.size());

    children::mu.unlock();
    return status;
} // children::wait_for

void children::killer_body() throw ()
/* This is the body of a background "killer" thread. This thread is needed
   to kill off child processes when the client that initiated them dies.
   It works by periodically walking over the 'children' list. Any child
   whose corresponding 'client' connection has died is then killed using
   the kill(2) system call. The data structure is updated and the appropriate
   parent thread is signalled. */
{
    while (true) {
	// pause for 30 seconds
	Basics::thread::pause(30);

	// look for children to kill
	children::mu.lock();
	for (int i = 0; i < children::maxChildren; i++) {
	    child_info *slot = children::info + i; // local
	    if (slot->pid != UNUSED && slot->client != NULL) {
		// test if the connection to the client is still alive
		if (!slot->client->alive()) {
		    // kill child process group
                    int sig = (slot->killed) ? SIGKILL : SIGTERM;
		    if (kill(-slot->pid, sig) != 0) {
		      // This kill() can fail if we lose a race with
		      // the process dying in some other way.  In that
		      // case wait_for will receive the process's exit
		      // status and clean up as soon as it gets a
		      // chance to run, so we do nothing here.
		    } else {
		      slot->killed = true;
		    }
		}
	    }
	}
	children::mu.unlock();
    }
} // children::killer_body

void children::call_failed() throw ()
{
  // Acquire the lock and decrement the "in use" counter for this
  // thread that failed.
  children::mu.lock();
  children::nInUse--;

  // If there are some pending requests, wake up the next one in
  // line.
  if(children::pending.size() > 0)
    {
      Basics::cond *c = children::pending.remlo();
      assert(c != 0);
      c->signal();
    }
  assert(children::nPending >= children::pending.size());

  // Release the lock
  children::mu.unlock();
}

// Main request handler =====================================================

static void RunTool(SRPC *srpc, int intfVersion) throw(SRPC::failure)
{
  // Have we acquired and not yeat released an "in use" slot?
  bool in_use_slot = false;

    try {
	req_data d; // the main request object for this call

	/* Unmarshal the arguments into 'd'. See RunToolClientPrivate.H for
           the calling sequence. */
	srpc->recv_Text(/*OUT*/ d.stdin_file);
	d.out.sock = srpc->recv_socket();
	d.err.sock = srpc->recv_socket();
	d.fsroot_sock = srpc->recv_socket();
	int len = sizeof(d.fsroot_fh);
	srpc->recv_bytes_here(/*OUT*/ (char *)&d.fsroot_fh, /*INOUT*/ len);
	if (len != sizeof(d.fsroot_fh)) {
	    call_failed(implementation_error, "Invalid fsroot");
	}
	srpc->recv_Text(/*OUT*/ d.wd);
	srpc->recv_chars_seq(/*OUT*/ d.ev);
	srpc->recv_chars_seq(/*OUT*/ d.argv);
	if (d.argv[0] == NULL) {
	    call_failed(implementation_error, "Null command");
	}
	srpc->recv_end();

	// Wait for it to be our turn to run.
	if(!children::my_turn())
	  {
	    // Limit on running/pending requests reached: deny this
	    // request.
	    call_failed(client_error, "request denied: server too busy");
	  }
	// We've now been given an "in use" slot.
	in_use_slot = true;
    
	/* Ugly way to find out where to chroot to.  We do it this way for
	   historical reasons, to avoid changing too many interfaces. */
	unsigned int index;
	(void) d.fsroot_fh.getParent(&index);
	char arc[(sizeof(index) * 2) + 1];
	sprintf(arc, "%08x", index);
	d.chroot_to = volatile_root + PathnameSep + arc;
    
	// Set up input/output channels
	setup_stdin(/*INOUT*/ d);
	setup_std_info(/*INOUT*/ d.out);
	setup_std_info(/*INOUT*/ d.err);

	// Pause if appropriate environment variable is set
	if (getenv("STOP_BEFORE_TOOL")) {
	    cerr << "Stop before tool; root = " << d.chroot_to << endl;
	    cerr << "Hit enter to continue: ";
	    char enter;
	    cin.get(enter);
	}
    
	// Create pipe as error-reporting channel for child
	if (pipe(d.error_pipe) == SYSERROR) {
	    fail_with_errno("pipe call failed!");
	}
    
	// ------------------------------------------------------------
	// Perform setup needed by the forked child.  The "Single UNIX
	// Specification" says that a child forked from a
	// multi-threaded program "may only execute async-signal-safe
	// operations until such time as one of the exec functions is
	// called".  This means that we can't perform memory
	// allocations or anything else fancy in the child, so we do
	// all that stuff now.

	// Setup working directory we will chdir to.
	Text wd(d.chroot_to + PathnameSep + d.wd);

	// Setup the error message text to be used if chdir fails.
	Text chdir_emsg("Can't establish working directory " + wd);

	/* Now set up to run a "helper" program (typically the
	   "tool_launcher" program), which is a small executable
	   that runs setuid-ed to root so that it can perform the
	   chroot(2) operation.  The essence of this program is the
	   following actions:


	   (1) chroot(d.chroot_to);
	   (2) seteuid(geteuid()); // become unprivileged
	   (3) execve(d.argv[0], d.argv, d.ev)

	   We use execv(2) to invoke this "helper" program, passing
	   it the two necessary pieces of information (mnt_pt, argv)
	   on the command line. Here 'argv' is a a command-line
	   encoding of: the helper switches, the file descriptor of
	   the error reporting channel, 'd.chroot_to', 'd.ev',
	   and 'd.argv'. */
	char **helper_cmd =
	  NEW_ARRAY(char *, 
		    1+			      // helper name
		    helper_switches.length()+ // switches from config file
		    2+		              // -err <arg>
		    2+	                      // -root <arg>
		    1+d.ev.length()+	      // -env <list>
		    1+d.argv.length()+        // -cmd <list>
		    1);		              // terminator      
	int i = 0; // current arg
	helper_cmd[i++] = helper.chars();
	int j;
	for (j = 0; j < helper_switches.length(); /*SKIP*/) {
	  helper_cmd[i++] = helper_switches[j++];
	}
	helper_cmd[i++] = "-err";
	char error_wr[20];
	sprintf(error_wr, "%d", d.error_pipe[1]);
	helper_cmd[i++] = error_wr;
	helper_cmd[i++] = "-root";
	helper_cmd[i++] = d.chroot_to.chars();
	helper_cmd[i++] = "-env";
	for (j = 0; j < d.ev.length(); /*SKIP*/) {
	  helper_cmd[i++] = d.ev[j++];
	}
	helper_cmd[i++] = "-cmd";
	for (j = 0; j < d.argv.length(); /*SKIP*/) {
	  helper_cmd[i++] = d.argv[j++];
	}
	helper_cmd[i++] = NULL;
	// ------------------------------------------------------------

	// Fork subprocess to complete preparations and execute command
	pid_t child_pid = fork();
	switch (child_pid) {
	  case -1:
	    fail_with_errno("Couldn't fork process to execute command");
	    break;			// keep compiler happy

	  // child code -----------------------------------------------------
	  case 0:
	    // Start a new process group
	    ::setpgid(0, 0);

	    // Discard read end of error_pipe
	    close_fd(d.error_pipe[0]);
      
	    // Establish file descriptors.
	    move_fd(d.stdin_fd, STDIN_FILENO);
	    move_fd(d.out.f, STDOUT_FILENO);
	    move_fd(d.err.f, STDERR_FILENO);
      
	    /* Change working directory to be at the specified path
	       from the volatile root.. */
	    if (chdir(wd.chars()) == SYSERROR) {
	      simulate_child_failure(d.error_pipe[1], trouble_fd,
				     implementation_error,
				     chdir_emsg.cchars());
	    }

	    // run the "helper" program
	    execv(helper_cmd[0], helper_cmd);

	    // execution continues only if execv failed
	    simulate_child_failure(d.error_pipe[1], trouble_fd,
				   configuration_error,
				   "Can't exec helper");

	    // Should be unreachable, but just to be safe...
	    _exit(implementation_error);

	    // unreachable
	    break;

          // parent code ----------------------------------------------------
	  default:
	    // Write debugging information
	    if (debug_mode) {
	      cerr << "\n*** Launching child process " << child_pid
		   << " ***" << endl;
	    }
      
	    // Discard write end of error_pipe
	    close_fd(d.error_pipe[1]);
      
	    // wait for child to finish
	    bool child_killed;
	    int s = children::wait_for(child_pid, srpc, /*OUT*/ child_killed);
	    // children::wait_for released our "in use" slot.
	    in_use_slot = false;

	    // post debugging
	    if (debug_mode) {
		if (child_killed) {
		    cerr << "*** Child process " << child_pid
                         << " killed due to client termination ***" << endl;
		} else {
		    cerr << "*** Finished child process " << child_pid
                	 << ": exit status " << WEXITSTATUS(s)
                	 << ", signal " << WTERMSIG(s) << " ***" << endl;
		}
	    }

	    // pause execution if appropriate environment variables set
	    if (getenv("STOP_AFTER_TOOL")) {
		cerr << "Stop after tool; root = " << d.chroot_to << endl;
		cerr << "Hit enter to continue: ";
		char enter;
		cin.get(enter);
	    } else if (WIFSIGNALED(s) && getenv("STOP_AFTER_TOOL_SIGNALED")) {
		cerr << "Stop after tool signaled " << WTERMSIG(s)
		     << "; root = " << d.chroot_to << endl;
		cerr << "Hit enter to continue: ";
		char enter;
		cin.get(enter);
	    } else if (WIFEXITED(s) && WEXITSTATUS(s) &&
		       getenv("STOP_AFTER_TOOL_ERROR")) {
		cerr << "Stop after tool error " << WEXITSTATUS(s)
		     << "; root = " << d.chroot_to << endl;
		cerr << "Hit enter to continue: ";
		char enter;
		cin.get(enter);
	    }

	    // Check for error message reported on 'd.error_pipe'
	    Text msg("");
	    while (true) {
		char buf[100];
		int ct = read(d.error_pipe[0], buf, sizeof(buf)-1);
		if (ct == 0) break;
		if (ct == SYSERROR)
		    fail_with_errno("Can't read from error_pipe!");
		buf[ct] = 0;
		msg += buf;
	    }
	    close_fd(d.error_pipe[0]);
      
	    // handle error condition
	    if (!msg.Empty()) {
		/* Tool was not launched; 's' explains why, and a
		   human-readable message is in 'msg'. */
		int r;
		switch (WEXITSTATUS(s)) {
		  case unrecognized_switch:
		  case chroot_failure:
		  case wd_not_in_root:
		    r = configuration_error;
		    break;
		  case execve_failure:
		    r = client_error;
		    break;
		  default:
		    r = implementation_error;
		    break;
		}
		call_failed(r, msg);
		// does not return
	    }
      
	    // Tool was launched and has terminated.

	    int i = sync_after_tool;
	    while (i--) {
	      // There is evidence that the Tru64 kernel is willing to
	      // tell the parent that a child is done before the NFS
	      // write-through-on-close for all the child's files has
	      // really been flushed to the server.  Optionally try to
	      // avoid this problem by calling sync().  This is
	      // expensive on a shared machine with other file writing
	      // going on, so we would rather not do it, but it might
	      // be necessary.  Note that even the man page for sync
	      // does not guarantee that all the writes are really
	      // done when it returns, but some implementations may
	      // ensure this anyway.  I don't know for sure about
	      // Tru64.  Historically, people called sync() repeatedly
	      // to work around this problem, but I don't believe that
	      // really guarantees anything either; certainly the man
	      // page doesn't mention it.
	      ::sync();
	    }

	    // Return results to caller.
	    srpc->send_int(WIFEXITED(s) ? WEXITSTATUS(s) : 0);
	    srpc->send_int(WIFSIGNALED(s) ? WTERMSIG(s) : 0);
	    if(intfVersion >= RUN_TOOL_CORE_VERSION) {
		    srpc->send_bool(WCOREDUMP(s) ? true : false);
	    }
	    srpc->send_end();
	    break;
	} /* switch */

	// Free the command-line for invoking the helper.
	delete [] helper_cmd;
	helper_cmd = NULL; // help out the garbage collector
    } /* try */
    catch(const TCP_sock::failure &tf) {
	// If we're still holding an "in use" slot, release it now.
	if(in_use_slot)
	  {
	    children::call_failed();
	  }

	srpc->send_failure(implementation_error, "TCP failure: " + tf.msg);
    }
    catch(const call_failure &cf) {
	// If we're still holding an "in use" slot, release it now.
	if(in_use_slot)
	  {
	    children::call_failed();
	  }

	srpc->send_failure(cf.code, cf.msg);
    }
    catch (SRPC::failure) {
	// If we're still holding an "in use" slot, release it now.
	// (I don't believe this case is actually possible, but it
	// doesn't hurt.)
	if(in_use_slot)
	  {
	    children::call_failed();
	  }

	throw;
    }

    // We should never reach this point and still be holding an "in
    // use" slot.
    assert(!in_use_slot);
}

void Info(SRPC *srpc, int intfVersion, int procId) throw(SRPC::failure)
{
    float load = GetLoadPoint();
    children::mu.lock();
    int max_tools = children::maxChildren;
    int cur_tools, cur_pending, max_pending;
    if(intfVersion >= RUN_TOOL_LOAD_VERSION) {
	    cur_tools = children::nInUse;
	    cur_pending = children::nPending;
	    max_pending = children::maxPending;
    } else {
	    cur_tools = children::nInUse + children::nPending;
    }
    children::mu.unlock();

    try {
        /* See RunToolClientPrivate.H for the calling sequence. */

        /* No arguments */
	srpc->recv_end();
    
	/* Return results */
	srpc->send_Text(sysname);
	srpc->send_Text(release);
	srpc->send_Text(version);
	srpc->send_Text(machine);
	srpc->send_int(cpus);
	srpc->send_int(cpuMHz);
	srpc->send_int(memKB);
	srpc->send_int(max_tools);
	srpc->send_int(cur_tools);
	if (intfVersion >= RUN_TOOL_LOAD_VERSION) {
		srpc->send_float(load);
		srpc->send_int(cur_pending);
		srpc->send_int(max_pending);
	}
	if (procId != RUN_TOOL_OLDINFO) uniqueid.Send(*srpc);
	srpc->send_end();

    } /* try */
    catch(const TCP_sock::failure &tf) {
	srpc->send_failure(implementation_error, "TCP failure: " + tf.msg);
    }
}

void 
RunToolServer(SRPC *srpc, int intfVersion, int procId)
  throw(SRPC::failure)
/* This is the server for the RunToolServer interface. It expects to
   be invoked via the LimService interface, which establishes the RPC
   connection with the client. */
{
  
  if(intfVersion > RUN_TOOL_INTERFACE_VERSION) { 
    srpc->send_failure(SRPC::version_skew,
		       "RunToolServer: Unsupported interface version");
    return;
  }

  /* 'arg' is unused; all information comes via the RPC connection. */
  switch (procId) {
  case RUN_TOOL_DOIT:
  case SRPC::default_proc_id:
    RunTool(srpc, intfVersion);
    break;
    
  case RUN_TOOL_OLDINFO:
  case RUN_TOOL_INFO:
    Info(srpc, intfVersion, procId);
    break;

  default:
    srpc->send_failure(SRPC::version_skew, "Unknown proc_id");
    break;
  }
}

#if defined(__linux__)

// get_hw_clock_freq: Attempt to discover the system clock rate under
// Linux.

// This is an ugly hack, but we haven't found a standard, architecture
// independent way to get this information.

static unsigned int get_hw_clock_freq()
{
  ifstream pstr("/proc/cpuinfo");

  char line[1000];

  if(pstr.bad())
    {
      return 1; 
    }

  // Look for a line like one of these:

  // cycle frequency [Hz]    : 432900432
  // cpu MHz         : 399.068
  // clock           : 375MHz

  // Then pull out the number, round up to the nearest MHz (when
  // appropriate) and return it.
  for(pstr.getline(line, sizeof(line));
      !pstr.eof();
      pstr.getline(line, sizeof(line)))
    {
      const char *l_digits;

      // Alpha Linux style
      // cycle frequency [Hz]    : 432900432
      if(strncmp(line, "cycle frequency [Hz]", 20) == 0)
	{
	  // Look for a number
	  l_digits = strpbrk(line+20, "0123456789");
	  if(l_digits != NULL)
	    {
	      // Convert Hz to MHz, rounding up
	      int freq = atoi(l_digits);
	      int l_result = (freq + 500000) / 1000000; 
	      return l_result;
	    }
	}
      // x86 Linux style
      // cpu MHz         : 399.068
      else if(strncmp(line, "cpu MHz", 7) == 0)
	{
	  // Look for a number
	  l_digits = strpbrk(line+7, "0123456789");
	  if(l_digits != NULL)
	    {
	      int l_result = atoi(l_digits);
	      return l_result;
	    }
	}
      // PPC Linux style
      // clock           : 375MHz
      else if((strncmp(line, "clock", 5) == 0) && isspace(line[5]))
	{
	  // Look for a number
	  l_digits = strpbrk(line+6, "0123456789");
	  if(l_digits != NULL)
	    {
	      int l_result = atoi(l_digits);
	      return l_result;
	    }
	}

      // Handle lines longer than our buffer.
      while(pstr.fail())
	{
	  // Clear the failbit
	  pstr.clear(pstr.rdstate() & ~ios::failbit);
	  // Read the next chunk of the line, possibly still too
	  // little to complete it, hence the enclosing while.
	  pstr.getline(line, sizeof(line));
	}
    }

  // If we fail to find something which tells us the CPU speed, return
  // 1.
  return 1; 
}

// get_n_cpus: Return the number of CPUs in this machine under Linux.

// It seems that the glibc function get_nprocs parses /proc/cpuinfo
// and is broken on non-x86 architectures.  See:

// http://www.geocrawler.com/mail/msg.php3?msg_id=3052218&list=16

// This function uses a similar method suggested in the above message:
// parsing /proc/stat which is apparently not as architecutre
// dependent as /proc/cpuinfo.  Specifically, on SMP machines it
// contains one line per-CPU that starts with "cpu[0-9]".  On
// uni-processor machines, no such lines will be found.

static unsigned int get_n_cpus()
{
  // Open /proc/stat.
  ifstream l_proc_stat("/proc/stat");

  // We'll count CPUs in this variable.
  unsigned int l_result = 0;

  // Read one line at a time.
  char l_line[1000];
  for(l_proc_stat.getline(l_line, sizeof(l_line));
      !l_proc_stat.eof();
      l_proc_stat.getline(l_line, sizeof(l_line)))
    {
      // Count lines which start with cpu[0-9]
      if((strncmp(l_line, "cpu", 3) == 0) && isdigit(l_line[3]))
	{
	  l_result++;
	}

      // Handle lines longer than our buffer.
      while(l_proc_stat.fail())
	{
	  // Clear the failbit
	  l_proc_stat.clear(l_proc_stat.rdstate() & ~ios::failbit);
	  // Read another the next chunk of the line, possibly still too
	  // little to complete it, hence the enclosing while.
	  l_proc_stat.getline(l_line, sizeof(l_line));
	}
    }

  // If we found any lines starting with cpu[0-9], then the number we
  // found is the number of CPUs.
  if(l_result > 0)
    {
      return l_result;
    }

  // If we didn't find any, this must be a uni-processor.
  return 1;
}

#endif // defined (__linux__)

// Initialization routine ===================================================

void RunToolServerInit(int maxTools, int maxPending) throw(SRPC::failure)
{
    /* We tuck away a file descriptor for stderr in 'trouble_fd' so that
       it can be used as an emergency error reporting channel.  This channel
       is used to report problems detected in the Vesta implementation that
       occur after the forked launcher/tool process has set its stderr in
       preparation for the tool execution and before the tool has actually
       begun execution. */
    try {
	if ((trouble_fd = dup(STDERR_FILENO)) == SYSERROR) {
	    fail_with_errno(
              "Can't get file descriptor to report launcher errors");
	}
    } catch (const call_failure &f) {
	cerr << f.msg << endl;
	return;
    }
  
    // Get local operating system and hardware information
    static struct utsname u;
    uname(&u);
    sysname = u.sysname;
    release = u.release;
    version = u.version;
    machine = u.machine;

#if defined(USE_GETSYSINFO_CPU_INFO)
    // This will give us both the CPU count and speed
    {
      struct cpu_info cpuinfo;
      int start = 0, res;
      res = getsysinfo(GSI_CPU_INFO, (caddr_t) &cpuinfo, sizeof(cpuinfo),
		       &start, NULL, NULL);
      if(res < 1)
	{
	  cerr << "Error getsysinfo(GSI_CPU_INFO), CPU count will be reported as 1 and CPU speed will be reported as zero"
	       << endl;
	  cpus = 1;
	  cpuMHz = 0;
	}
      else
	{
	  cpus = cpuinfo.cpus_in_box;
	  cpuMHz = cpuinfo.mhz;
	}
    }
#else
    // First look for a method to get the CPU count
# if defined(USE_SYSCONF_NPROCESSORS)
    cpus = sysconf(_SC_NPROCESSORS_ONLN);
# elif defined(USE_SYSCTL_NCPU)
    {
      size_t cpus_len = sizeof(cpus);
      int res = sysctlbyname("hw.ncpu", &cpus, &cpus_len, (void *) 0, 0);
      if(res != 0)
	{
	  cerr << "Error getting sysctl hw.ncpu, CPU count will be reported as zero"
	       << endl;
	  cpus = 0;
	}
    }
# elif defined (__linux__)
    // See function above which scans /proc/stat looking for the
    // per-processor lines present on SMP machines.
    cpus = get_n_cpus();
    assert(cpus > 0);
# else
# warning No CPU count code
# warning CPU count will be reported as zero
# endif

    // Next look for a method to get the CPU speed
# if defined(USE_PROCESSOR_INFO)
    {
      processor_info_t proc_info;
      int res = processor_info(0, &proc_info);
      if(res != 0)
	{
	  cerr << "Error from processor_info, CPU speed will be reported as zero"
	       << endl;
	  cpuMHz = 0;
	}
      else
	{
	  cpuMHz = proc_info.pi_clock;
	}
    }
# elif defined(USE_SYSCTL_CPUFREQ)
    {
      size_t cpumhz_len = sizeof(cpuMHz);
      char *name = "hw.cpufrequency";
      int res = sysctlbyname(name, &cpuMHz, &cpumhz_len, (void *) 0, 0);
      if(res == 0)
	{
	  cpuMHz /= 1000000;
	}
      else
	{
	  unsigned long cpumhz_l = 0;
	  cpumhz_len = sizeof(cpumhz_l);
	  res = sysctlbyname(name, &cpumhz_l, &cpumhz_len, (void *) 0, 0);
	  if(res != 0)
	    {
	      cerr << "Error from sysctlbyname(\"" << name
		   << "\"), CPU speed will be reported as zero"
	       << endl;
	      cpuMHz = 0;
	    }
	  else
	    {
	      cpuMHz = (cpumhz_l / 1000000);
	    }
	}
    }
# elif defined(USE_SYSCTL_CPUFREQ_INT64)
    {
      u_int64_t cpumhz_64 = 0;
      size_t cpumhz_len = sizeof(cpumhz_64);
      char *name = "hw.cpufrequency";
      int res = sysctlbyname(name, &cpumhz_64, &cpumhz_len, (void *) 0, 0);
      if(res != 0)
	{
	  cerr << "Error from sysctlbyname(\"" << name
	       << "\"), CPU speed will be reported as zero"
	       << endl;
	  cpuMHz = 0;
	}
      else
	{
	  cpuMHz = (cpumhz_64 / 1000000);
	}
    }
# elif defined (__linux__)
    // See function above which scans /proc/cpuinfo looking for a line
    // which that tells us the CPU speed.
    cpuMHz = get_hw_clock_freq();
    assert(cpuMHz > 0);
# else
# warning No CPU speed code
# warning CPU speed will be reported as zero
# endif

#endif

    // Now look for a method to get the memory size
#if defined(USE_GETSYSINFO_PHYSMEM)
    {
      int start = 0, res;
      res = getsysinfo(GSI_PHYSMEM, (caddr_t) &memKB, sizeof(memKB),
		       &start, NULL, NULL);
      if(res < 1)
	{
	  cerr << "Error getsysinfo(GSI_PHYSMEM), memory size will be reported as zero"
	       << endl;
	  memKB = 0;
	}
    }
#elif defined(USE_SYSCONF_PHYSPAGES)
    {
      unsigned long page_size;
      unsigned long page_count;
# if defined(_SC_PAGESIZE)
      page_size = sysconf(_SC_PAGESIZE);
# elif defined(_SC_PAGE_SIZE)
      page_size = sysconf(_SC_PAGE_SIZE);
# else
# error Need page size
# endif
      page_count = sysconf(_SC_PHYS_PAGES);

      memKB = page_count * page_size / 1024;
    }
#elif defined(USE_SYSCTL_PHYSMEM)
    {
      unsigned long physmem;
      size_t physmem_len = sizeof(physmem);
      char *name = "hw.physmem";
      int res = sysctlbyname(name, &physmem, &physmem_len, (void *) 0, 0);
      if(res != 0)
	{
	  cerr << "Error from sysctlbyname(\"" << name
	       << "\"), memory size will be reported as zero"
	       << endl;
	  memKB = 0;
	}
      else
	{
	  memKB = (physmem >> 10);
	}
    }
#else
#warning No memory size code
#warning Memory size will be reported as zero
#endif

    // Allow overrides for the bootstrapping case, where the helper
    // (tool launcher) runs the tools on a different machine.
    VestaConfig::get(RUN_TOOL_CONFIG_SECTION, "sysname", sysname);
    VestaConfig::get(RUN_TOOL_CONFIG_SECTION, "release", release);
    VestaConfig::get(RUN_TOOL_CONFIG_SECTION, "version", version);
    VestaConfig::get(RUN_TOOL_CONFIG_SECTION, "machine", machine);
    try {
      cpus = VestaConfig::get_int(RUN_TOOL_CONFIG_SECTION, "cpus");
    } catch (VestaConfig::failure) { /* ignore */ }
    try {
      cpuMHz = VestaConfig::get_int(RUN_TOOL_CONFIG_SECTION, "cpuMHz");
    } catch (VestaConfig::failure) { /* ignore */ }
    try {
      memKB = VestaConfig::get_int(RUN_TOOL_CONFIG_SECTION, "memKB");
    } catch (VestaConfig::failure) { /* ignore */ }

    // Invent a unique ID for this server instance
    uniqueid = UniqueId();

    // Get other Vesta configuration information
    helper = config_lookup("helper");
    try {
	Text val(VestaConfig::get_Text(RUN_TOOL_CONFIG_SECTION,
				       "helper_switches"));
	IBufStream is(val.chars());
	char s[100];
	while ( is >> s && strlen(s) != 0) {
	    if (strcmp(s, "-d") == 0) {
		int dbgfd;
		try {
		    if ((dbgfd = dup(STDERR_FILENO)) == SYSERROR) {
			fail_with_errno(
			  "Can't get file descriptor for debug output");
		    }
		} catch (call_failure f) {
		    cerr << f.msg << '\n';
		    continue;
		}
		debug_mode = true;
		helper_switches += "-dbg";
		sprintf(s, "%d", dbgfd);
	    }
	    helper_switches += s;
	}
    } catch(const VestaConfig::failure &f) {
	/* ignore */
    }
    try {
      sync_after_tool = VestaConfig::get_int(RUN_TOOL_CONFIG_SECTION,
					     "sync_after_tool");
    } catch(const VestaConfig::failure &f) {
	/* ignore */
    }
    if (debug_mode) {
	cout << "helper command = " << helper;
	for (int i = 0; i < helper_switches.length(); i++) {
	    cout << " " << helper_switches[i];
	}
	cout << endl;
    }

    volatile_root = config_lookup("VolatileRootName");

    // Hold the volatile root mounted by opening it.  This prevents it
    // from being un-mounted while the RunToolServer is running.
    hold_volatile_mount = open(volatile_root.cchars(), O_RDONLY);
    if(hold_volatile_mount < 0)
      {
	unsigned int errno_save = errno;
	cerr << "Can't open(2) the volatile root "
	     << volatile_root << ": "
	     << strerror(errno_save) << endl;
	if(errno_save == ENOENT)
	  cerr << "Possible cause: enclosing filesystem not mounted?" << endl;
	exit(1);
      }
    // Check that the volatile root is mounted.  If mounted, its inode
    // number is always 3.  (See the initialization of the volatile
    // root in VestaSource.C in the repository server code.)  If not
    // mounted, it will be the inode of the mount point directory in
    // the enclosing filesystem.
    struct stat buf;
    if (fstat(hold_volatile_mount, &buf) < 0) {
	unsigned int errno_save = errno;
	cerr << "Can't fstat(2) the volatile root " << volatile_root << ": "
	     << strerror(errno_save) << endl;
	exit(1);
    }
    if (buf.st_ino != 3) {
      cerr << "The volatile root (" << volatile_root
	   << ") doesn't appear to be mounted.  "
	   << "Its inode number should be 3, but it is "
	   << buf.st_ino << endl;
      exit(1);
    }
    // As long as we have the stat data, also make sure it's a
    // directory.
    if (!S_ISDIR(buf.st_mode))
      {
	cerr << "The volatile root (" << volatile_root
	     << ") isn't a directory." << endl;
	exit(1);
      }

    children::init(maxTools, maxPending);
}	

void RunToolServerCleanup() throw ()
{
    // Nothing to do on most platforms.
}
