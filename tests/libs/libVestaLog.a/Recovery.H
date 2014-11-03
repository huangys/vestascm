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

//
// Recovery.H
//
// Code to drive recovery from log for repository server
// 

#ifndef _VSRECOVER
#define _VSRECOVER 1

#include "VestaLog.H"
#include "Basics.H"

// Link with -L/proj/vesta/lib -llog to use the VestaLog and Recovery
// interfaces.

// Wrapper class that makes reading from istreams and VestaLogs look
// the same.  (It would make for nicer packaging if we had made VestaLog 
// an iostream instead, but that involves a lot of hacking and is 
// nonportable between different C++ libraries.)
//
class RecoveryReader {
  public:
    // Constructors
    RecoveryReader(VestaLog* vl) throw();
    RecoveryReader(std::istream* is) throw();

    // Ordinary read routines
    void get(char &c)
      throw(VestaLog::Eof, VestaLog::Error);
    void get(char* p, int n, char term='\n')
      throw(VestaLog::Eof, VestaLog::Error);
    int read(char* p, int n)
      throw(VestaLog::Error);
    void readAll(char* p, int n)
      throw(VestaLog::Eof, VestaLog::Error);
    bool eof()
      throw(VestaLog::Error);

    // Special read routines for parsing the log.  The c argument
    //  is a lookahead character.  All the routines skip leading
    //  whitespace.  Within a quoted string, " and \ characters must
    //  be escaped as \" and \\.
    typedef char Ident[256]; // null-terminated alphanumeric string
    void skipWhite(char& c)
      throw(VestaLog::Eof, VestaLog::Error);
    void requireChar(char& c, char required)
      throw(VestaLog::Eof, VestaLog::Error);
    void getIdent(char& c, Ident id)
      throw(VestaLog::Eof, VestaLog::Error);
    void getLong(char& c, long& n)
      throw(VestaLog::Eof, VestaLog::Error);
    void getULong(char& c, unsigned long& n)
      throw(VestaLog::Eof, VestaLog::Error);
    void getLongId(char& c, Byte32& value) // value is the LongId.value field
      throw(VestaLog::Eof, VestaLog::Error);
    void getQuotedString(char& c, char* buf, int maxLen)
      throw(VestaLog::Eof, VestaLog::Error);  // too long => Error
    void getNewQuotedString(char& c, char*& string)
      throw(VestaLog::Eof, VestaLog::Error);  // caller must free string

  private:
    VestaLog* vl_;
    std::istream* is_;
};

// Driver for recovery.  Assumes each log record is of the form:
//  '(' ident arbitrary-data ')'
// Here single-quotes surround literal data, ident is a string of
// alphanumeric characters terminated by a non-alphanumeric (which is
// included in arbitrary-data), and arbitrary-data is a string of
// arbitrary bytes.  RecoverFrom parses the fixed part of this syntax,
// calling a registered callback for each ident it finds to parse the
// arbitrary-data part.  When the callback is made, c is a lookahead
// character that has already been read from the reader.  On return, c
// should be a new lookahead character unless Eof (or Error) is
// thrown.
extern void RecoverFrom(RecoveryReader* rr) throw(VestaLog::Error);

// Function type for callbacks made by RecoverFrom
typedef void RecoveryCallback(RecoveryReader* rr, char &c)
     /*throw(VestaLog::Eof, VestaLog::Error)*/;

extern void RegisterRecoveryCallback(RecoveryReader::Ident id,
				     RecoveryCallback* rc)
     throw();

#endif // _VSRECOVER
