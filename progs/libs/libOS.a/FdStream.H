// Copyright (C) 2004, Kenneth C. Schalk
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

#ifndef _FDSTREAM_H_
#define _FDSTREAM_H_

#include <sys/types.h>
#include <iosfwd>
#include <iostream>

namespace FS
{
  // streambuf class for reading from/writing to a file descriptor.
  // Does all the real work.  Analogous to filebuf.

  class FdStreambuf : public std::streambuf
  {
  public:
#if defined(__GNUC__) && (__GNUC__ < 3)
    typedef streampos stream_pos_t;
    typedef streamoff stream_off_t;
    typedef ios::seek_dir stream_seek_t;
    typedef int stream_mode_t;
#else
    typedef std::streambuf::pos_type stream_pos_t;
    typedef std::streambuf::off_type stream_off_t;
    typedef std::ios_base::seekdir stream_seek_t;
    typedef std::ios_base::openmode stream_mode_t;
#endif
  protected:
    int _fd;

    // Rather than relying on the file position maintained by the
    // kernel, we keep our own file offset.  The facilitates sharing
    // file descriptors between multiple threads.

    // Note that this position is at the end of the last read or write
    // to the file descriptor.  The logical position of the stream may
    // be different (e.g. if we've buffered bytes for read but not all
    // of them have been read yet, or if we've buffered bytes for
    // write but not written them to the file descriptor yet).
    off_t fd_pos;

    // Should we close the file descriptor when this object is
    // destroyed?
    bool should_close;

    // Buffers used for the get and put areas.
    char *buffer;
    unsigned int buf_size;
    stream_mode_t mode;

    // If we've read through part of a get area, seek the file
    // descriptor to the same position.  Reset the get area in any
    // case.
    void get_pos_to_fd_pos();

    // Saved error code for a previous failed operation
    int saved_errno;

  public:

    FdStreambuf()
      : _fd(-1), fd_pos(0), should_close(false), buffer(0), buf_size(0),
	saved_errno(0)
    { }
    FdStreambuf(int fd, off_t initial_pos = 0);
    FdStreambuf(const char *filename, stream_mode_t mode,
		mode_t prot = 0644,
		bool create = true, bool replace = true);

    ~FdStreambuf();

    FdStreambuf* attach(int fd, off_t initial_pos = 0);

    FdStreambuf* open(const char *filename, stream_mode_t mode,
		      mode_t prot = 0644,
		      bool create = true, bool replace = true);
    inline bool is_open() const { return this->_fd >= 0; }
    inline int fd() const { return is_open() ? this->_fd : -1; }
    FdStreambuf* close();
    virtual stream_pos_t seekoff(stream_off_t offset,
				 stream_seek_t way,
				 // NOTE: mode ignored!
				 stream_mode_t mode=(std::ios::in|
						     std::ios::out));
    stream_pos_t seekpos(stream_pos_t pos, 
			 // NOTE: mode ignored!
			 stream_mode_t mode=(std::ios::in|
					     std::ios::out));

    int previous_error() { return saved_errno; }
  protected:
    int underflow();
    std::streamsize xsgetn(char* s, std::streamsize n);
    int overflow(int c = EOF);
    std::streamsize xsputn(const char* s, std::streamsize n);
    int sync();
  };

  // Base class for FdStream classes below.  Analogous to fstreambase.

  class FdStreamBase : virtual public std::ios
  {
  protected:
    FdStreambuf my_fdbuf;
  public:
    FdStreamBase()
      : my_fdbuf()
    {
      init(&my_fdbuf);
    }
    FdStreamBase(int fd)
      : my_fdbuf(fd)
    {
      init(&my_fdbuf);
      // Bad file descriptor?
      if(!is_open())
	clear(std::ios::badbit); // set badbit
      else
	clear(std::ios::goodbit); // all is well
    }
    FdStreamBase(const char *filename, FdStreambuf::stream_mode_t mode,
		 mode_t prot = 0644,
		 bool create = true, bool replace = true)
      : my_fdbuf(filename, mode, prot, create, replace)
    {
      init(&my_fdbuf);
      // Open failed?
      if(!is_open())
	clear(std::ios::badbit); // set badbit
      else
	clear(std::ios::goodbit); // all is well
    }

    FdStreambuf *rdbuf() { return &my_fdbuf; }
    void close()
    {
      if(my_fdbuf.close() != &my_fdbuf)
	clear(std::ios::badbit); // set badbit
    }
    bool is_open() const { return my_fdbuf.is_open(); }
    int fd() const { return my_fdbuf.fd(); }
    void attach(int fd)
    {
      if(my_fdbuf.attach(fd) != &my_fdbuf)
	clear(std::ios::badbit); // set badbit
      else
	clear(std::ios::goodbit); // all is well
    }

    void open(const char *filename, FdStreambuf::stream_mode_t mode,
	      mode_t prot = 0644,
	      bool create = true, bool replace = true)
    {
      if(my_fdbuf.open(filename, mode, prot, create, replace) != &my_fdbuf)
	clear(std::ios::badbit); // set badbit
      else
	clear(std::ios::goodbit); // all is well
    }
    int previous_error() { return my_fdbuf.previous_error(); }
  };

  // Input fd stream.  Analogous to ifstream.

  class IFdStream : public FdStreamBase, public std::istream
  {
  public:
    IFdStream() : FdStreamBase(), std::istream(&my_fdbuf) { }
    IFdStream(int fd) : FdStreamBase(fd), std::istream(&my_fdbuf) { }
    IFdStream(const char *filename,
	      FdStreambuf::stream_mode_t mode = std::ios::in,
	      mode_t prot = 0644,
	      bool create = true, bool replace = true)
      : FdStreamBase(filename, mode, prot, create, replace),
	std::istream(&my_fdbuf)
    { }

    void open(const char *filename,
	      FdStreambuf::stream_mode_t mode = std::ios::in,
	      mode_t prot = 0644,
	      bool create = true, bool replace = true)
    {
      FdStreamBase::open(filename, mode, prot, create, replace);
    }
  };

  // Output fd stream.  Analogous to ofstream.

  class OFdStream : public FdStreamBase, public std::ostream
  {
  public:
    OFdStream() : FdStreamBase(), std::ostream(&my_fdbuf) { }
    OFdStream(int fd) : FdStreamBase(fd), std::ostream(&my_fdbuf) { }
    OFdStream(const char *filename,
	      FdStreambuf::stream_mode_t mode = std::ios::out,
	      mode_t prot = 0644,
	      bool create = true, bool replace = true)
      : FdStreamBase(filename, mode, prot, create, replace),
	std::ostream(&my_fdbuf)
    { }

    void open(const char *filename,
	      FdStreambuf::stream_mode_t mode = std::ios::out,
	      mode_t prot = 0644,
	      bool create = true, bool replace = true)
    {
      FdStreamBase::open(filename, mode, prot, create, replace);
    }
  };

  // Generic fd stream.  Analogous to fstream.

  class FdStream : public FdStreamBase, public std::iostream
  {
  public:
    FdStream() : FdStreamBase(), std::iostream(&my_fdbuf) { }
    FdStream(int fd) : FdStreamBase(fd), std::iostream(&my_fdbuf) { }
    FdStream(const char *filename, FdStreambuf::stream_mode_t mode,
	     mode_t prot = 0644,
	     bool create = true, bool replace = true)
      : FdStreamBase(filename, mode, prot, create, replace),
	std::iostream(&my_fdbuf)
    { }

    void open(const char *filename, FdStreambuf::stream_mode_t mode,
	      mode_t prot = 0644,
	      bool create = true, bool replace = true)
    {
      FdStreamBase::open(filename, mode, prot, create, replace);
    }
  };
}

#endif
