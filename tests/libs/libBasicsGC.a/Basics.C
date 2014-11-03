// Copyright (C) 2001, Compaq Computer Corporation

// This file is part of Vesta.

// Vesta is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of
// the License, or (at your option) any later version.

// Vesta is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA

#include "Basics.H"
#include "BasicsConfig.h"

#include <string.h>
#include <errno.h>

const char* const BoolName[] = { "false", "true" };

Text Basics::errno_Text(int err)
{
  char msg_buf[512];
  const char *message;
#if defined(STRERROR_R_SUSV3) || defined(STRERROR_R_RETURNS_ERANGE)
  // strerror_r returns an integer that's 0 in case of success
  int convert_err = strerror_r(err, msg_buf, sizeof(msg_buf));
  assert(convert_err == 0);
  message = msg_buf;
#elif defined(STRERROR_R_GLIBC)
  // glibc uses a non-standard prototype for strerror_r that returns a
  // char * (maybe allocated from the heap if the buffer passed as an
  // argument is too small?).  The man page says it "must be regarded
  // as obsolete", and yet strerror_r behaves differently than SUSv3
  // says.  *sigh*
  message = strerror_r(err, msg_buf, sizeof(msg_buf));
#elif defined(__sun__)
  // Solaris has no strerror_r, but the man page claims that strerror
  // is thread-safe and it looks like strerror returns pointers into a
  // table of string constants.
  message = strerror(err);
#elif defined(HAVE_STRERROR)
#warning Using default code for non-thread-safe strerror
  // Default case: assume we need a private mutex for a
  // non-thread-safe strerror.
  static Basics::mutex strerror_mu;
  memset((void *) msg_buf, 0, sizeof(msg_buf));
  strerror_mu.lock();
  strncpy(msg_buf, strerror(err), sizeof(msg_buf)-1);
  strerror_mu.lock();
  message = msg_buf;
#else
#error No usable strerror_r and no strerror
#endif

  assert(message != 0);
  return Text(message);
}

// compare two Texts lexically, but with recognizing embedded integers
// and comparing them numerically
bool Basics::TextLessThanWithInts(const Text &a, const Text &b)
{
  const char *sa = a.cchars();
  const char *sb = b.cchars();

  while(1)
    {
      // Compare embedded integers as integers
      if(isdigit(*sa) && isdigit(*sb))
	{
	  char *sa_end, *sb_end;
	  unsigned long int ia = strtoul(sa, &sa_end, 10);
	  unsigned long int ib = strtoul(sb, &sb_end, 10);

	  sa = sa_end;
	  sb = sb_end;

	  if(ia < ib)
	    {
	      return true;
	    }
	  if(ia > ib)
	    {
	      break;
	    }
	}
      // If the characters differ, the strings differ.
      else if(*sa < *sb)
	{
	  return true;
	}
      else if(*sa > *sb)
	{
	  break;
	}
      // If we've reached the end of both strings, they're the same.
      else if(!*sa)
	{
	  assert(!*sb);
	  break;
	}
      // Otherwise, advance to the next character.
      else
	{
	  sa++;
	  sb++;
	}
    }

  // If we get here, then a >= b, so we return false.
  return false;
}
