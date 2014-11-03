// Copyright (C) 2006, Intel
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

#include <iostream>
#include <set>
#include <string>
#include <stdio.h>
#include <iomanip>

#include "SidSort.H"
#include "SourceOrDerived.H"

using std::ofstream;
using std::ifstream;
using std::endl;
using std::hex;
using std::setw;
using std::setfill;
using std::cerr;
using std::string;
using std::set;

// Read file "fname" with a list of shortids one per line in hex and
// add them all to the set "sids".

static void read_sid_file(const char *fname, /*OUT*/ set<ShortId> &sids)
  throw(Text, FS::DoesNotExist, FS::Failure)
{
  assert(fname != 0);

  ifstream fin;
  FS::OpenReadOnly(fname, fin);

  string line;
  while(fin >> line) {
    ShortId sid;
    char *endptr;
    unsigned int len = strlen(line.c_str());
    errno = 0;
    sid = strtoul(line.c_str(), &endptr, 16);
    if((errno != 0) ||
       (endptr != (line.c_str() + len)) ||
       (len != 8) ||
       (sid == NullShortId)) {
      Text err("invalid shortid line \"");
      err += line.c_str();
      err += "\" in ";
      err += fname;
      throw err;
    }
    sids.insert(sid);
  }

  FS::Close(fin);
}

// Write the set of shortids "sids" into file "fname" one per line in
// hex.

static void write_sid_file(const char *fname, const set<ShortId> &sids)
  throw(FS::Failure)
{
  assert(fname != 0);

  ofstream fout;

  FS::OpenForWriting(fname, fout);

  set<ShortId>::const_iterator iter;
  for(iter = sids.begin(); iter != sids.end(); iter++) {
    char sid_buf[10];
    int nwritten = sprintf(sid_buf, "%08x\n", *iter);
    assert(nwritten == 9);
    FS::Write(fout, sid_buf, 9);
  }

  FS::Close(fout);
}

// Read the two files of shortids "sname" (sources) and "dname"
// (deriveds) and write the combined sorted list of shortids into file
// "oname".

void sidsort(const char *oname, const char *sname, const char *dname, const char *ename)
  throw(Text, FS::DoesNotExist, FS::Failure)
{
  set<ShortId> sids;

  if((sname != 0) && sname[0])
    read_sid_file(sname, sids);
  if((dname != 0) && dname[0])
    read_sid_file(dname, sids);
  if((ename != 0) && ename[0])
    read_sid_file(ename, sids);

  if(sids.size() == 0) {
    throw Text("no sids found.");
  }

  write_sid_file(oname, sids);
}
