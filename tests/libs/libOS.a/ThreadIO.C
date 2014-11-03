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

#if defined (__linux__)
// Needed to get PTHREAD_MUTEX_RECURSIVE on older platforms (RH7)
#define _XOPEN_SOURCE 600
#include <features.h>
#endif

#include "ThreadIO.H"

using std::flush;

OS::ThreadIO::ThreadIO(std::ostream *o, std::ostream *e, std::istream *i)
  : out(o), err(e), in(i), prompting(false), mu(PTHREAD_MUTEX_RECURSIVE)
{
}

void OS::ThreadIO::start_prompt(std::ostream *&o, std::istream *&i)
{
  mu.lock();
  while(prompting) prompt_ready.wait(mu);

  prompting = true;

  o = out;
  i = in;

  mu.unlock();
}

void OS::ThreadIO::flush_buffered()
{
  while(out_bits.size() > 0)
    {
      out_bit b = out_bits.remlo();
      if(b.err)
	{
	  *err << b.t << flush;
	}
      else
	{
	  *out << b.t << flush;
	}
    }
}

void OS::ThreadIO::buffer_current()
{
  if(!tmp.empty())
    {
      // We must be between start_out/err and end_out/err and we must
      // be prompting.
      assert(state_nesting.size() > 0);
      assert(prompting);

      // Take the current buffer contents and store them for the right
      // stream (output or error) based on state_nesting
      out_bit b; 
      b.err = state_nesting.gethi();
      b.t = tmp.str();
      out_bits.addhi(b);
      tmp.erase();
    }
}

void OS::ThreadIO::end_prompt()
{
  mu.lock();

  prompting = false;

  // Flush any buffered output.
  flush_buffered(); 

  // Any other thread waiting to prompt the user should wake up now.
  prompt_ready.signal();

  mu.unlock();
}

std::ostream &OS::ThreadIO::start_out()
{
  mu.lock();
  if(prompting)
    {
      // In case of start_out/err nesting, buffer anything currently
      // in "tmp".
      buffer_current();
      assert(tmp.empty());

      // Remember we're entering an output block
      state_nesting.addhi(false);
      return tmp;
    }
  else
    {
      // The background buffering state must all be empty
      assert(tmp.empty());
      assert(out_bits.size() == 0);
      assert(state_nesting.size() == 0);

      return *out;
    }
}

void OS::ThreadIO::end_out(bool aborting)
{
  if(prompting)
    {
      buffer_current();
      (void) state_nesting.remhi();

      if(aborting)
	// The caller claims we're about to exit with a fatal error:
	// flush all buffered output even though we're prompting.
	flush_buffered(); 
    }
  else
    // Flush output when not prompting, just to be sure we don't leave
    // something buffered if a prompt comes next
    *out << flush;

  assert(!aborting || (out_bits.size() == 0));

  if(!aborting)
    // Unlock unless the the caller said we're about to exit with a
    // fatal error.

    // If the caller said we're about to exit with a fatal error, no
    // more output shall be allowed.  (Note: if they were lying, we
    // deadlock.)
    mu.unlock();
}

std::ostream &OS::ThreadIO::start_err()
{
  mu.lock();
  if(prompting)
    {
      // In case of start_out/err nesting, buffer anything currently
      // in "tmp".
      buffer_current();
      assert(tmp.empty());

      // Remember we're entering an error block
      state_nesting.addhi(true);
      return tmp;
    }
  else
    {
      // The background buffering state must all be empty
      assert(tmp.empty());
      assert(out_bits.size() == 0);
      assert(state_nesting.size() == 0);

      return *err;
    }
}

void OS::ThreadIO::end_err(bool aborting)
{
  if(prompting)
    {
      buffer_current();
      (void) state_nesting.remhi();

      if(aborting)
	// The caller claims we're about to exit with a fatal error:
	// flush all buffered output even though we're prompting.
	flush_buffered(); 
    }
  else
    // Flush output when not prompting, just to be sure we don't leave
    // something buffered if a prompt comes next
    *err << flush;

  assert(!aborting || (out_bits.size() == 0));

  if(!aborting)
    // Unlock unless the the caller said we're about to exit with a
    // fatal error.

    // If the caller said we're about to exit with a fatal error, no
    // more output shall be allowed.  (Note: if they were lying, we
    // deadlock.)
    mu.unlock();
}

OS::ThreadIOHelper OS::ThreadIO::helper(bool err)
{
  return OS::ThreadIOHelper(*this, err);
}

static OS::ThreadIO _cio(&std::cout, &std::cerr, &std::cin);

OS::ThreadIO &OS::cio()
{
  return _cio;
}


std::ostream& OS::ThreadIOHelper::start() const
{
  if(err)
    return io.start_err();
  return io.start_out();
}

void OS::ThreadIOHelper::end(bool aborting) const
{
  if(err)
    io.end_err(aborting);
  else
    io.end_out(aborting);
}
