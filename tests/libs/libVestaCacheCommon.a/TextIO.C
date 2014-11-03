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

// Created on Mon Mar 30 09:59:04 PST 1998 by heydon

#include <Basics.H>
#include <FS.H>
#include <VestaLog.H>
#include <Recovery.H>
#include "TextIO.H"

using std::ostream;
using std::istream;
using std::streamoff;
using std::ios;

void TextIO::Log(VestaLog &log, const Text &t)
  throw (VestaLog::Error)
{
  unsigned long len = t.Length();
  Basics::uint16 len_short = (Basics::uint16) len;
  // Check for 16-bit integer overflow
  assert(((unsigned long) len_short) == len);
  log.write((char *)(&len_short), sizeof(len_short));
  log.write(t.chars(), len);
}

void TextIO::Recover(RecoveryReader &rd, /*OUT*/ Text &t)
  throw (VestaLog::Error, VestaLog::Eof)
{
    Basics::uint16 len;
    rd.readAll((char *)(&len), sizeof(len));
    char *buff = NEW_PTRFREE_ARRAY(char, len+1);
    rd.readAll(buff, len);
    buff[len] = '\0';
    t.Init((const char *) buff);
}

void TextIO::Write(ostream &ofs, const Text &t)
  throw (FS::Failure)
{
  unsigned long len = t.Length();
  Basics::uint16 len_short = (Basics::uint16) len;
  // Check for 16-bit integer overflow
  assert(((unsigned long) len_short) == len);
  FS::Write(ofs, (char *)(&len_short), sizeof(len_short));
  FS::Write(ofs, t.chars(), len);
}

void TextIO::Read(istream &ifs, /*OUT*/ Text &t)
  throw (FS::Failure, FS::EndOfFile)
{
    Basics::uint16 len;
    FS::Read(ifs, (char *)(&len), sizeof(len));
    char *buff = NEW_PTRFREE_ARRAY(char, len+1);
    FS::Read(ifs, buff, len);
    buff[len] = '\0';
    assert(strlen(buff) == len);
    t.Init((const char *) buff);
}

int TextIO::Skip(istream &ifs) throw (FS::Failure, FS::EndOfFile)
{
    Basics::uint16 len;
    FS::Read(ifs, (char *)(&len), sizeof(len)); // read length
    FS::Seek(ifs, (streamoff)len, ios::cur);    // skip "len" bytes
    return sizeof(len) + len;
}
