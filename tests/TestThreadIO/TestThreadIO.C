// Copyright (C) 2006, Vesta Free Software Project
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

#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <Basics.H>
#include <Generics.H>
#include <ThreadIO.H>
#include <FdStream.H>

using std::cerr;
using std::cout;
using std::endl;
using std::flush;
using std::ostream;
using std::istream;
using OS::cio;
using FS::OFdStream;
using FS::IFdStream;

// Should we print lots of stuff about the test's progress?
bool verbose = false;

struct SlowPrintThreadArgs
{
  Text label;
  unsigned int min_delay;
  unsigned int max_delay;
  unsigned int limit;
  unsigned int err_rate;
  unsigned int prompt_rate;
  unsigned int rand_seed;
  SlowPrintThreadArgs(const Text &l,
		      unsigned int min_d, unsigned int max_d, 
		      unsigned int e_rate, unsigned int p_rate,
		      unsigned int lim)
    : label(l), min_delay(min_d), max_delay(max_d),
      err_rate(e_rate), prompt_rate(p_rate), limit(lim)
  {
    rand_seed = random();
  }
};

ostream &operator<<(ostream &o, const SlowPrintThreadArgs &args)
{
  o << "-" << args.label
    << " [" << args.min_delay << "," << args.max_delay << "] "
    << " up to " << args.limit
    << " error every " << args.err_rate
    << " prompt every " << args.prompt_rate
    << endl;
  return o;
}

void my_sleep(unsigned int msecs)
{
  // Sleeping for milliseconds seems to give a useful spread of
  // intervals among the threads printing messages.
  Basics::thread::pause(0, msecs*1000);
}

void random_sleep(unsigned int low, unsigned int high, unsigned int &rand_seed)
{
  assert(high > low);
  unsigned int range = high - low;
  unsigned int length = (((unsigned int) rand_r(&rand_seed)) % range) + low;

  assert(length >= low);
  assert(length <= high);

  my_sleep(length);
}

void* SlowPrintThread(void *arg)
{
  SlowPrintThreadArgs *args = (SlowPrintThreadArgs *) arg;
  unsigned int count = 0;

  while(1)
    {
      if(count > args->limit)
	{
	  cio().start_err() << args->label << "e." << endl;
	  cio().end_err(/*aborting=*/ true);
	  exit(0);
	}
      cio().start_out() << args->label << "o" << count << endl;
      if((count % args->err_rate) == 0)
	{
	  cio().start_err() << args->label << "e" << count << endl;
	  cio().end_err();
	}
      cio().end_out();
      if((count % args->prompt_rate) == 0)
	{
	  ostream *out; istream *in;

	  cio().start_prompt(out, in);

	  *out << ":" << args->label << count << endl;
      
	  char enter;
	  in->get(enter);

	  cio().end_prompt();
	}
      count++;
      random_sleep(args->min_delay, args->max_delay, args->rand_seed);
    }
}

void be_application(int in_fd, int out_fd, int err_fd)
{
  if(in_fd  != STDIN_FILENO)  dup2(in_fd, STDIN_FILENO);
  if(out_fd != STDOUT_FILENO) dup2(out_fd, STDOUT_FILENO);
  if(err_fd != STDERR_FILENO) dup2(err_fd, STDERR_FILENO);

  unsigned int l_rand_seed = random();

  {
    ostream &out = cio().start_out();

    unsigned int nthreads = (((unsigned int) random()) % 10) + 4;
    for(unsigned int i = 0; i < nthreads; i++)
      {
	char label[2];
	label[0] = 'A' + i;
	label[1] = 0;
	unsigned int min_delay = 10 + (((unsigned int) random()) % 50);
	unsigned int max_delay = min_delay + 10 + (((unsigned int) random()) % 40);
	unsigned int limit = 500 + (((unsigned int) random()) % 3000);

	SlowPrintThreadArgs *th_args = NEW_CONSTR(SlowPrintThreadArgs, (label, min_delay, max_delay, i+2, (3*i)+11, limit));

	out << *th_args;

	Basics::thread th;

	th.fork_and_detach(SlowPrintThread, (void *) th_args);
      }

    cio().end_out();
  }

  unsigned int prompt_count = 0;
  while(1)
    {
      ostream *out; istream *in;

      cio().start_prompt(out, in);

      *out << ":-" << prompt_count << endl;
      
      char enter;
      in->get(enter);

      cio().end_prompt();

      random_sleep(100,1000, l_rand_seed);

      prompt_count++;
    }
}

void be_user3(int in_fd, int out_fd, int err_fd)
{
  OFdStream to_in(in_fd);
  IFdStream from_out(out_fd), from_err(err_fd);

  TextIntTbl out_counts, err_counts;

  unsigned int l_rand_seed = random();

  bool done = false;
  while(!done)
    {
      struct pollfd poll_data[2];
      poll_data[0].fd = out_fd;
      poll_data[0].events = POLLIN;
      poll_data[0].revents  = 0;
      poll_data[1].fd = err_fd;
      poll_data[1].events = POLLIN;
      poll_data[1].revents  = 0;

      errno = 0;
      int nready = poll(poll_data, 2, -1);

      if(nready < 1)
	{
	  int errno_save = errno;
	  if(errno_save == EINTR) 
	    {
	      if(verbose) cout << "poll EINTR" << endl;
	      continue;
	    }
	  cerr << "poll error: " << Basics::errno_Text(errno_save) << endl;
	  exit(1);
	}

      char buff[100];

      // Check for errors (POLLERR or POLLHUP or POLLNVAL) from poll
      if(poll_data[0].revents & POLLERR)
	{
	  cerr << "POLLERR on output stream" << endl;
	  exit(1);
	}
      else if(poll_data[0].revents & POLLHUP)
	{
	  cerr << "POLLHUP on output stream" << endl;
	  exit(1);
	}
      else if(poll_data[0].revents & POLLNVAL)
	{
	  cerr << "POLLNVAL on output stream" << endl;
	  exit(1);
	}
      if(poll_data[1].revents & POLLERR)
	{
	  cerr << "POLLERR on error stream" << endl;
	  exit(1);
	}
      else if(poll_data[1].revents & POLLHUP)
	{
	  cerr << "POLLHUP on error stream" << endl;
	  exit(1);
	}
      else if(poll_data[1].revents & POLLNVAL)
	{
	  cerr << "POLLNVAL on error stream" << endl;
	  exit(1);
	}

      if(poll_data[0].revents & POLLIN)
	{
	  do
	    {
	      from_out.getline(buff, sizeof(buff));
	      if(verbose)
		cout << "Read output: {" << buff << "}" << endl;
	      if(buff[0] == ':')
		{
		  // Prompt: send a newline after a delay
		  random_sleep(100,400, l_rand_seed);
		  if(verbose)
		    cout << "Acknowledging prompt" << endl;
		  to_in << endl;
		}
	      else if(buff[0] == '-')
		{
		  // Debug output
		  continue;
		}
	      else if(isalpha(buff[0]))
		{
		  assert(buff[1] == 'o');
		  char *end = 0;
		  errno = 0;
		  int i = strtoul(buff+2, &end, 0);
		  assert(errno == 0);
		  assert(end != 0);
		  assert(*end == 0);

		  int cur_count = 0;
		  Text label(buff[0]);
		  (void) out_counts.Get(label, cur_count);
		  assert(i == cur_count);
		  cur_count++;
		  bool inTbl = out_counts.Put(label, cur_count);
		  assert(inTbl || (cur_count == 1));
		}
	      else
		{
		  cerr << "Unexpected output line: {" << buff << "}" << endl;
		  exit(1);
		}

	      if(from_out.eof())
		{
		  cerr << "EOF on output stream" << endl;
		  exit(1);
		}
	      else if(!from_out)
		{
		  cerr << "Error on output stream" << endl;
		  exit(1);
		}
	    }
	  // Make sure we consume everything we pulled from the pipe
	  // into the stream's buffer.
	  while(from_out.rdbuf()->in_avail() > 0);
	  // Read output lines before error
	  continue;
	}
      if(poll_data[1].revents & POLLIN)
	{
	  do
	    {
	      from_err.getline(buff, sizeof(buff));
	      if(verbose)
		cout << "Read error: {" << buff << "}" << endl;
	      if(isalpha(buff[0]))
		{
		  assert(buff[1] == 'e');
		  if(buff[2] == '.')
		    {
		      done = true;
		    }
		  else
		    {
		      char *end = 0;
		      errno = 0;
		      int i = strtoul(buff+2, &end, 0);
		      assert(errno == 0);
		      assert(end != 0);
		      assert(*end == 0);

		      int cur_count = -1;
		      Text label(buff[0]);
		      (void) err_counts.Get(label, cur_count);
		      assert(cur_count < i);
		      cur_count = i;
		      (void) err_counts.Put(label, cur_count);
		    }
		}
	      else
		{
		  cerr << "Unexpected error line: {" << buff << "}" << endl;
		  exit(1);
		}

	      if(from_err.eof())
		{
		  cerr << "EOF on error stream" << endl;
		  exit(1);
		}
	      else if(!from_err)
		{
		  cerr << "Error on error stream" << endl;
		  exit(1);
		}
	    }
	  // Make sure we consume everything we pulled from the pipe
	  // into the stream's buffer.
	  while(from_err.rdbuf()->in_avail() > 0);
	}
    }

}

void be_user2(int in_fd, int out_fd)
{
  OFdStream to_in(in_fd);
  IFdStream from_out(out_fd);

  TextIntTbl out_counts;

  unsigned int l_rand_seed = random();

  bool done = false;
  while(!done)
    {
      struct pollfd poll_data[1];
      poll_data[0].fd = out_fd;
      poll_data[0].events = POLLIN;
      poll_data[0].revents  = 0;

      errno = 0;
      int nready = poll(poll_data, 1, -1);

      if(nready < 1)
	{
	  int errno_save = errno;
	  if(errno_save == EINTR) 
	    {
	      if(verbose) cout << "poll EINTR" << endl;
	      continue;
	    }
	  cerr << "poll error: " << Basics::errno_Text(errno_save) << endl;
	  exit(1);
	}

      char buff[100];

      // Check for errors (POLLERR or POLLHUP or POLLNVAL) from poll
      if(poll_data[0].revents & POLLERR)
	{
	  cerr << "POLLERR on output stream" << endl;
	  exit(1);
	}
      else if(poll_data[0].revents & POLLHUP)
	{
	  cerr << "POLLHUP on output stream" << endl;
	  exit(1);
	}
      else if(poll_data[0].revents & POLLNVAL)
	{
	  cerr << "POLLNVAL on output stream" << endl;
	  exit(1);
	}

      if(poll_data[0].revents & POLLIN)
	{
	  do
	    {
	      from_out.getline(buff, sizeof(buff));
	      if(verbose)
		cout << "Read output: {" << buff << "}" << endl;
	      if(buff[0] == ':')
		{
		  // Prompt: send a newline after a delay
		  random_sleep(100,400,l_rand_seed);
		  if(verbose)
		    cout << "Acknowledging prompt" << endl;
		  to_in << endl;
		}
	      else if(buff[0] == '-')
		{
		  // Debug output
		  continue;
		}
	      else if(isalpha(buff[0]))
		{
		  if(buff[1] == 'o')
		    {
		      char *end = 0;
		      errno = 0;
		      int i = strtoul(buff+2, &end, 0);
		      assert(errno == 0);
		      assert(end != 0);
		      assert(*end == 0);

		      int cur_count = 0;
		      Text label(buff[0]);
		      (void) out_counts.Get(label, cur_count);
		      assert(i == cur_count);
		      cur_count++;
		      bool inTbl = out_counts.Put(label, cur_count);
		      assert(inTbl || (cur_count == 1));
		    }
		  else if(buff[1] == 'e')
		    {
		      if(buff[2] == '.')
			{
			  done = true;
			}
		      else
			{
			  char *end = 0;
			  errno = 0;
			  int i = strtoul(buff+2, &end, 0);
			  assert(errno == 0);
			  assert(end != 0);
			  assert(*end == 0);

			  int cur_count = 0;
			  Text label(buff[0]);
			  (void) out_counts.Get(label, cur_count);
			  assert(i == (cur_count - 1));
			}
		    }
		  else
		    {
		      cerr << "Bad output line: {" << buff << "}" << endl;
		      exit(1);
		    }
		}
	      else
		{
		  cerr << "Unexpected output line: {" << buff << "}" << endl;
		  exit(1);
		}

	      if(from_out.eof())
		{
		  cerr << "EOF on output stream" << endl;
		  exit(1);
		}
	      else if(!from_out)
		{
		  cerr << "Error on output stream" << endl;
		  exit(1);
		}
	    }
	  // Make sure we consume everything we pulled from the pipe
	  // into the stream's buffer.
	  while(from_out.rdbuf()->in_avail() > 0);
	}
    }

}

int main(int argc, char* argv[])
{
  unsigned int seed = time(0);
  bool dont_fork = false;
  for (;;)
    {
      char* slash;
      int c = getopt(argc, argv, "s:vf");
      if (c == EOF) break;
      switch (c)
      {
      case 's':
	seed = strtoul(optarg, NULL, 0);
	break;
      case 'v':
	verbose = true;
	break;
      case 'f':
	dont_fork = true;
	break;
      }
    }

  cout << "To reproduce: -s " << seed << endl;
  srandom(seed);

  if(dont_fork)
    {
      be_application(STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
      return 0;
    }

  // First we test with the output and error on separate streams to
  // make sure that nothing winds up on the wrong channel.
  cout << "3-pipe test..." << endl;

  int in_pipe[2], out_pipe[2], err_pipe[2];

  // Set up three pipes
  if(pipe(in_pipe) != 0)
    {
      int errno_save = errno;
      cerr << "pipe error: " << Basics::errno_Text(errno_save) << endl;
      exit(1);
    }
  if(pipe(out_pipe) != 0)
    {
      int errno_save = errno;
      cerr << "pipe error: " << Basics::errno_Text(errno_save) << endl;
      exit(1);
    }
  if(pipe(err_pipe) != 0)
    {
      int errno_save = errno;
      cerr << "pipe error: " << Basics::errno_Text(errno_save) << endl;
      exit(1);
    }

  pid_t child = fork();
  if(child == 0)
    {
      be_application(in_pipe[0], out_pipe[1], err_pipe[1]);
    }
  else
    {
      be_user3(in_pipe[1], out_pipe[0], err_pipe[0]);
      int status;
      pid_t wait_r = waitpid(child, &status, 0);
      assert(wait_r == child);
      assert(WIFEXITED(status));
      assert(WEXITSTATUS(status) == 0);
    }

  // Close the first set of pipes
  close(in_pipe[0]); close(in_pipe[1]);
  close(out_pipe[0]); close(out_pipe[1]);
  close(err_pipe[0]); close(err_pipe[1]);

  // Now test with error and output on the same channel to make sure
  // that error and output stay correctly ordered in this case.
  cout << "2-pipe test..." << endl;

  // Set up two pipes
  if(pipe(in_pipe) != 0)
    {
      int errno_save = errno;
      cerr << "pipe error: " << Basics::errno_Text(errno_save) << endl;
      exit(1);
    }
  if(pipe(out_pipe) != 0)
    {
      int errno_save = errno;
      cerr << "pipe error: " << Basics::errno_Text(errno_save) << endl;
      exit(1);
    }

  child = fork();
  if(child == 0)
    {
      be_application(in_pipe[0], out_pipe[1], out_pipe[1]);
    }
  else
    {
      be_user2(in_pipe[1], out_pipe[0]);
      int status;
      pid_t wait_r = waitpid(child, &status, 0);
      assert(wait_r == child);
      assert(WIFEXITED(status));
      assert(WEXITSTATUS(status) == 0);
    }

  cout << "All tests passed!" << endl;
  return 0;
}
