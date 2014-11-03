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

// Created on Mon Mar 30 09:54:57 PST 1998 by heydon
// Last modified on Tue Aug  3 14:17:40 EDT 2004 by ken@xorian.net
//      modified on Mon Mar 30 15:01:50 PST 1998 by heydon

#ifndef _TEXT_IO_H
#define _TEXT_IO_H

/* Defines functions for reading and writing texts from/to files
   and logs. */

#include <Basics.H>
#include <FS.H>
#include <SRPC.H>
#include <VestaLog.H>
#include <Recovery.H>

class TextIO {
public:
  // log/recover
  static void Log(VestaLog &log, const Text &t) throw (VestaLog::Error);
  static void Recover(RecoveryReader &rd, /*OUT*/ Text &t)
    throw (VestaLog::Error, VestaLog::Eof);

  // write/read
  static void Write(std::ostream &ofs, const Text &t) throw (FS::Failure);
  static void Read(std::istream &ifs, /*OUT*/ Text &t)
    throw (FS::Failure, FS::EndOfFile);

  // send/receive
  // Use SRCP::send_Text and SRPC::recv_text

  static int Skip(std::istream &ifs) throw (FS::Failure, FS::EndOfFile);
  /* Assuming "ifs" is positioned at a text written by "Write", skip
     the text and return the number of bytes skipped. */
};

#endif // _TEXT_IO_H
