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

#ifndef VS_STREAM_H
#define VS_STREAM_H

#include <assert.h>
#include <stdio.h>
#include <iosfwd>
#include <iostream>
#include "VestaSource.H"


class VSStreambuf: public std::streambuf
{
public:
#if defined(__GNUC__) && (__GNUC__ < 3)
  typedef streampos pos_type;
  typedef streamoff off_type;
  typedef ios::seek_dir seekdir;
  typedef int openmode;
#else
  typedef std::streambuf::pos_type pos_type;
  typedef std::streambuf::off_type off_type;
  typedef std::ios_base::seekdir seekdir;
  typedef std::ios_base::openmode openmode;
#endif

private:
  char* buf;
  unsigned int buf_size;
  VestaSource* vs_file;
  Basics::uint64 file_put_offset;  // The offset of the beggining of the file
  Basics::uint64 file_get_offset;  // The offset of the beggining of the file
  Text error_msg;

  Text dummy;

  void flush();

  unsigned int read_call_count, write_call_count;

  // If we've read through part of a get area, update file_get_offset
  // with the correct file position.  Reset the get area in any case.
  void update_file_get_offset();

  // Copy bytes from the get area to fulfill part or all of a read
  // request.  "s" is the destination address and will be incremented
  // past any bytes placed there.  "n" is the total read size
  // requested.  "read_so_far" is how many bytes have already been
  // read (so n-read_so_far is how many are still needed) and will be
  // incrememnted by the number of additional bytes placed there.
  void consume_from_gptr(char *&s, std::streamsize n,
			 std::streamsize &read_so_far);
   
public:
  VSStreambuf(VestaSource* vs);
  virtual ~VSStreambuf();

  Text getContents();

  bool error();
  Text get_error() { return this->error_msg; }

  pos_type seekoff(off_type offset,
		   seekdir way,
		   openmode mode=(std::ios::in|
				  std::ios::out));
  pos_type seekpos(pos_type pos, 
		   openmode mode=(std::ios::in|
				  std::ios::out));


  unsigned int read_calls() { return read_call_count; }
  unsigned int write_calls() { return write_call_count; }

protected:
  int underflow();
  std::streamsize xsgetn(char* s, std::streamsize n);
  int overflow(int c = EOF);
  std::streamsize xsputn(const char* s, std::streamsize n);
  int sync();
};


class VSStreamBase: virtual public std::ios
{
protected:
  VSStreambuf my_buf;
public:
  VSStreamBase(VestaSource* vs)
    : my_buf(vs) {}

  VSStreambuf *rdbuf() { return &my_buf; }

  unsigned int read_calls() { return my_buf.read_calls(); }
  unsigned int write_calls() { return my_buf.write_calls(); }

  inline bool error() { return my_buf.error(); }

  inline Text get_error() { return my_buf.get_error(); }
};

class VSStream: public VSStreamBase, public std::iostream
{
public:
  VSStream(VestaSource* vs)
    : VSStreamBase(vs), std::iostream(&my_buf) {}
};

class OVSStream: public VSStreamBase, public std::ostream
{
public:
  OVSStream(VestaSource* vs)
    : VSStreamBase(vs), std::ostream(&my_buf) {}
};

class IVSStream: public VSStreamBase, public std::istream
{
public:
  IVSStream(VestaSource* vs)
    : VSStreamBase(vs), std::istream(&my_buf) {}
};

#endif
