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

// ----------------------------------------------------------------------
// ThreadIO.H
// ----------------------------------------------------------------------

// This file declares an interface for handling I/O in a
// multi-threaded applications.  This is more sophisticated than a
// single mutex in that it buffers output from other threads while one
// thread is prompting the user for a response.  This can be
// neccessary to avoid deadlock or temporary hangs caused by
// contention for I/O.

#ifndef _THREAD_IO_H
#define _THREAD_IO_H

#include <iostream>
#include <Basics.H>
#include <BufStream.H>
#include <Sequence.H>

namespace OS
{
  class ThreadIOHelper;

  class ThreadIO
  {
  private:

    // Mutex which is held while a thread is between start_out/err and
    // end_out/err.  (Note that it's not held between
    // start_prompt/end_prompt.)
    Basics::mutex mu;

    // This is used to signal another thread waiting to prompt the
    // user when one finishes.
    Basics::cond prompt_ready;

    // Are we currently waiting for the user to respond to a prompt?
    bool prompting;

    // The "real" streams.
    std::ostream *out, *err;
    std::istream *in;

    // We gather output in tmp from a thread which wants to produce
    // output while we're prompting the user.
    Basics::OBufStream tmp;

    // We accumulate output in out_bits to preserve its order (since
    // we don't know whether out/err are really the same stream).
    struct out_bit
    {
      bool err;
      Text t;
    };
    Sequence<out_bit> out_bits;

    // We support nested start/end blocks, which means that we need to
    // remember the nesting.
    Sequence<bool, true> state_nesting;

    // Store the current contents of "tmp" into "out_bits" using the
    // sate from "state_nesting".
    void buffer_current();

    void flush_buffered();

  public:
    ThreadIO(std::ostream *o, std::ostream *e, std::istream *i);
    // A thread wanting to prompt the user is required to call thexe
    // functions.  Between start_prompt and end_prompt, output from
    // other threads are buffered.
    void start_prompt(std::ostream *&o, std::istream *&i);
    void end_prompt();

    // A thread which produces output must call these functions.  When
    // no other thread is prompting the user, it simply returns the
    // normal output stream.  Otherwise it returns a buffer (tmp
    // above).

    // In the event that the process is about to exit due to a fatal
    // error, aborting=true should be passed to end_out.  This will
    // flush all buffered output, even when another thread is
    // currently prompting the user.
    std::ostream &start_out();
    void end_out(bool aborting = false);

    // A thread which produces error messages must call these
    // functions.
    std::ostream &start_err();
    void end_err(bool aborting = false);

    // Create a helper for this instance.
    ThreadIOHelper helper(bool err);
  };

  // This returns a static ThreadIO for cin/cout/cerr.
  ThreadIO &cio();

  // A small proxy class which allows code to use a ThreadIO for
  // either output or error without knowing which.
  class ThreadIOHelper
  {
  private:
    ThreadIO& io;
    bool err;

  public:
    ThreadIOHelper(ThreadIO& th_io, bool err_stream):
      io(th_io), err(err_stream) {};

    // Equivalent to either ThreadIO::start_out or
    // ThreadIO::start_err, depending on the value of "err".
    std::ostream& start() const;

    // Equivalent to either ThreadIO::end_out or ThreadIO::end_err,
    // depending on the value of "err".
    void end(bool aborting = false) const;
           
  };
}

#endif //_THREAD_IO_H
