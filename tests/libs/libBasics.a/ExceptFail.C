// Copyright (C) 2004, Kenneth C. Schalk

// This file is part of Vesta.

// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// Last modified on Tue Sep 14 14:51:22 EDT 2004 by ken@xorian.net

// ----------------------------------------------------------------------
// This file defines replacement for the "unexpected" and "terminate"
// handlers which are part of the C++ exception handling system.
// Unlike the default handlers, these print an informative message
// before aborting, which at least makes it slightly easier to
// determine what happened.
// ----------------------------------------------------------------------

#include <stdlib.h>
#include <exception>
#include <iostream>

using std::cerr;
using std::endl;
using std::set_unexpected;
using std::set_terminate;

static void verbose_unexpected()
{
  // We try to use as few calls to the stream library as possible, as
  // we don't know what state the run-time is in overall.
  static char *msg =
    "FATAL ERROR: unexpected exception handler called; program will\n"
    "exit with a core dump\n\n"
    "This is almost certainly a bug.  Please report it.\n";
  cerr << msg;
  cerr.flush();

  // And we're outta here.
  abort();
}

static void verbose_terminate()
{
  static char *msg =
    "FATAL ERROR: exception terminate handler called; program will\n"
    "exit with a core dump\n\n"
    "This is almost certainly a bug.  Please report it.\n";
  cerr << msg;
  cerr.flush();

  // And we're outta here.
  abort();
}

// Use a static class instance to set the handlers before the prgoram
// starts.
class handler_setter
{
public:
  handler_setter()
  {
    set_unexpected(verbose_unexpected);
    set_terminate(verbose_terminate);
  }
};
handler_setter my_handler_setter;
