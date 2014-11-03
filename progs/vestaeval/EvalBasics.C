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

/* File: EvalBasics.C                                              */

#include "EvalBasics.H"

const Text emptyText;

Text IntToText(int n) {
  char buff[32];
  sprintf(buff, "%d", n);
  Text res(buff);
  return res;
}

Text IntArc(int n) {
  char buff[32];
  sprintf(buff, "##%d", n);
  Text res(buff);
  return res;
}

int ArcInt(const Text& t) {
  if (t.chars()[0] == '#' &&
      t.chars()[1] == '#') {
    char *start = t.chars()+2, *end;
    // Clear errno first, as strtoul only sets it when there's an
    // overflow error.
    errno = 0;
    unsigned long int l_n = strtoul(start, &end, 10);
    // If the value was too large...
    if((errno == ERANGE) ||
       // ...or there was no number...
       (start == end) ||
       // ...or there were left-over bytes...
       (*end != 0))
      // ...treat this as a non-integer arc
      return -1;
    int n = l_n;
    // If converting to a signed integer resulted in a different
    // value, treat this as a non-integer arc
    if(((unsigned long int) n) != l_n)
      return -1;
    return n;
  }
  return -1;
}

static Basics::mutex counterMu;

// Used to increment the various global counters.
int IncCounter(int* counter) {
  counterMu.lock();
  int c = ++*counter;
  counterMu.unlock();
  return c;
}
