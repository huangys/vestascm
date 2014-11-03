// Copyright (C) 2004, Kenneth C. Schalk
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

// timing.H - declarations for fine-grained NFS call timing, one of
// the performance debugging features

// Last modified on Thu Dec  9 17:07:49 EST 2004 by ken@xorian.net

#include <sys/time.h> /* struct timeval */

#if defined(REPOS_PERF_DEBUG)

#if defined(__cplusplus)

class LongId;

extern "C" {
#endif // defined(__cplusplus)

  /* Log the start of a call */
  void timing_start_call(unsigned int proc);

#define TIMING_START_CALL(proc) timing_start_call(proc)

  /* Log the duplicate status of a call, and when it was
     determined. */
  void timing_dupe_status(int new_call);

#define TIMING_DUPE_STATUS(new_call) timing_dupe_status(new_call)

  /* Log an intermediate time point. */
  void timing_record_time_point(const char *file, unsigned int line);

#define RECORD_TIME_POINT timing_record_time_point(__FILE__, __LINE__)

  /* Log the end of a call, returning to the caller how long the call
     took. */
  void timing_end_call(/*OUT*/ struct timeval *delta_time);

#define TIMING_END_CALL(delta) timing_end_call(delta)

  /* Determine whether we're in a call or not. */
  int timing_in_call();

#define TIMING_IN_CALL timing_in_call()

#if defined(__cplusplus)

  /* Turn timing recording on or off. */
  void timing_control(bool enable);

  void timing_record_LongId(const LongId &lid);

#define TIMING_RECORD_LONGID(lid) timing_record_LongId(lid)

}

#include <stdio.h> // FILE *

#define TIMING_RECORDER_BUF_SIZE 32768

extern "C"
{
  void Timing_Recorder_exit_cleanup_setup() throw();
  void Timing_Recorder_exit_cleanup() throw();
}

class Timing_Recorder
{
private:
  // File descriptor we're writing our data to.
  FILE *timing_record_stream;

  // The time at which we should switch to a new timing file.
  unsigned long file_end_time;

  // Start time of the call, used to compute deltas.
  struct timeval start_time;

  // Buffer with data prepared to write out.
  char write_buffer[TIMING_RECORDER_BUF_SIZE];

  // Number of bytes in the buffer so far.
  unsigned int current_byte_count;

  // Record 6 bytes for a delta from the start time.
  struct timeval record_delta();

  // Linked list of all recorders, used for cleanup.
  static Timing_Recorder *first;
  Timing_Recorder *next;

  // When we need to, start a new timing file.
  void start_new_file(struct timeval *current_time = 0);

public:
  Timing_Recorder();
  ~Timing_Recorder();

  void start(unsigned int proc);

  void dupe_status(bool new_call);

  void time_point(const char *file, unsigned short line);

  struct timeval end();

  inline bool in_call()
  {
    return (current_byte_count > 0);
  }

  void record_LongId(const LongId &lid);

  friend void Timing_Recorder_exit_cleanup_setup() throw();
  friend void Timing_Recorder_exit_cleanup() throw();
};
#endif // defined(__cplusplus)

#else // defined(REPOS_PERF_DEBUG)

#define TIMING_START_CALL(proc) ((void)0)
#define TIMING_DUPE_STATUS(new_call) ((void)0)
#define RECORD_TIME_POINT ((void)0)
#define TIMING_END_CALL(delta) ((void)0)
#define TIMING_IN_CALL 0
#define TIMING_RECORD_LONGID(lid) ((void)0)

#endif // defined(REPOS_PERF_DEBUG)
