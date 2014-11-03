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

// ----------------------------------------------------------------------

// BufStream.H

// A simple stream class for formatting text into a character buffer.
// Similar to the old ostrstream class.  Able to avoid heap
// allocations if a buffer is provided, unlike the ostringstream
// class.

#ifndef BUFSTREAM_H
#define BUFSTREAM_H

// Local configuration results
#include "BasicsConfig.h"

#include <assert.h>
#include <iosfwd>
#include <iostream>
#include <string.h>

namespace Basics
{
  class BufStreambuf : public std::streambuf
  {
  public:
#if defined(IOSTREAM_TYPES_PREISO)
    typedef streampos pos_type;
    typedef streamoff off_type;
    typedef ios::seek_dir seekdir;
    typedef int openmode;
#elif defined(IOSTREAM_TYPES_ISO)
    typedef std::streambuf::pos_type pos_type;
    typedef std::streambuf::off_type off_type;
    typedef std::ios_base::seekdir seekdir;
    typedef std::ios_base::openmode openmode;
#else
#error Unknown iostream types
#endif

  private:
    // Buffer where we're putting more characters.
    char *buf;

    // The length of the buffer
    unsigned int buf_len;

    // The current get/put positions.
    pos_type get_pos, put_pos;

    // The maximum number of bytes that have been written into the buffer.
    pos_type valid_limit;

    // Should we delete the current buffer?  (Did we allocate it, or was
    // it provided to us?)
    bool delete_buf;

    // Should we allocate a new buffer if the size of the current one is
    // exceeded, or should that be an error?
    bool allocate_new_buf;

    void update_get_pos();
    void update_put_pos();

    bool alloc_if_needed(unsigned int new_byte_count);

  public:
    // Construct a buffer that allocates its own storage and grows as
    // needed.
    BufStreambuf(unsigned int initial_buf_len = 256);

    // Construct a buffer that uses the supplied storage (at least
    // initially).
    BufStreambuf(char *initial_buf, unsigned int initial_buf_len,
		 unsigned int initial_valid_len = 0,
		 bool allocate_replacement = false)
      : buf(initial_buf), buf_len(initial_buf_len),
	delete_buf(false), allocate_new_buf(allocate_replacement),
	get_pos(0), put_pos(0), valid_limit(initial_valid_len)
    {
      // there must be at least one byte for the string terminator
      assert(initial_buf_len > 0);

      setp(buf, buf+(buf_len-1));
      setg(buf, buf, buf+valid_limit);
    }

    // Construct a buffer for reading only that uses the supplied
    // storage.  "initial_buf" must be a null-terminated string.  The
    // length will be determined with strlen.
    BufStreambuf(const char *initial_buf) 
      : buf((char*)initial_buf), delete_buf(false), allocate_new_buf(false),
	get_pos(0), put_pos(0)
    {
      unsigned int len = strlen(initial_buf);
      buf_len = valid_limit = len;
      // the buffer will not be used for writing
      setp(0,0);
      setg(buf, buf, buf+valid_limit);
    }

    ~BufStreambuf()
    {
      if(delete_buf)
	delete [] buf;
    }

    // Return the contents of the buffer.  Note that this is not a
    // copy, so further writes can change this string and/or
    // de-allocate this block.
    inline const char *str()
    {
      // Update valid_limit
      update_put_pos();

      // Add terminating null byte.  (We always save space in the
      // buffer for the terminating null.)
      assert(buf != 0);
      assert(valid_limit < (pos_type) buf_len);
      buf[valid_limit] = 0;

      // Return the buffer.
      return buf;
    }

    pos_type seekoff(off_type offset,
		     seekdir way,
		     openmode mode=(std::ios::in|
				    std::ios::out));
    pos_type seekpos(pos_type pos, 
		     openmode mode=(std::ios::in|
				    std::ios::out));

    // Remove the contents of the buffer.  (This makes it possible to
    // re-use an output buffer several times.)  Note that this will
    // invalidate what str() previously returned.
    void erase();

    // Is this buffer empty?
    inline bool empty()
    {
      update_put_pos();
      return ((unsigned int) valid_limit) == 0;
    }

  protected:
    int underflow();
    std::streamsize xsgetn(char* s, std::streamsize n);
    int overflow(int c);
    std::streamsize xsputn(const char* s, std::streamsize n);
  };

  class BufStreamBase : virtual public std::ios
  {
  protected:
    BufStreambuf my_buf;
  public:
    BufStreamBase(unsigned int initial_buf_len = 256)
      : my_buf(initial_buf_len)
    {
      init(&my_buf);
    }

    BufStreamBase(char *initial_buf, unsigned int initial_buf_len,
		  unsigned int initial_valid_len = 0,
		  bool allocate_replacement = false)
      : my_buf(initial_buf, initial_buf_len,
	       initial_valid_len, allocate_replacement)
    {
      init(&my_buf);
    }

    // Initialize from a null-terminated string, determining the
    // length automatically.
    BufStreamBase(const char *initial_buf) 
      : my_buf(initial_buf)
    {
      init(&my_buf);
    }

    BufStreambuf *rdbuf() { return &my_buf; }
  };

  class IBufStream : public BufStreamBase, public std::istream
  {
  public:
    IBufStream(unsigned int initial_buf_len = 256)
      : BufStreamBase(initial_buf_len),
	std::istream(&my_buf)
    { }

    IBufStream(char *initial_buf, unsigned int initial_buf_len,
	       unsigned int initial_valid_len = 0,
	       bool allocate_replacement = false)
      : BufStreamBase(initial_buf, initial_buf_len,
		      initial_valid_len, allocate_replacement),
	std::istream(&my_buf)
    { }

    // Initialize from a null-terminated string, determining the
    // length automatically.
    IBufStream(const char *initial_buf)
      : BufStreamBase(initial_buf),
	std::istream(&my_buf)
    { }


    // Same as str on BufStreambuf
    inline const char *str() { return my_buf.str(); }
  };

  class OBufStream : public BufStreamBase, public std::ostream
  {
  public:
    OBufStream(unsigned int initial_buf_len = 256)
      : BufStreamBase(initial_buf_len),
	std::ostream(&my_buf)
    { }

    OBufStream(char *initial_buf, unsigned int initial_buf_len,
	       unsigned int initial_valid_len = 0,
	       bool allocate_replacement = false)
      : BufStreamBase(initial_buf, initial_buf_len,
		      initial_valid_len, allocate_replacement),
	std::ostream(&my_buf)
    { }

    // Same as str, erase, and empty on BufStreambuf
    inline const char *str() { return my_buf.str(); }
    inline void erase() { my_buf.erase(); }
    inline bool empty() { return my_buf.empty(); }
  };

  class BufStream : public BufStreamBase, public std::iostream
  {
  public:
    BufStream(unsigned int initial_buf_len = 256)
      : BufStreamBase(initial_buf_len),
	std::iostream(&my_buf)
    { }

    BufStream(char *initial_buf, unsigned int initial_buf_len,
	      unsigned int initial_valid_len = 0,
	      bool allocate_replacement = false)
      : BufStreamBase(initial_buf, initial_buf_len,
		      initial_valid_len, allocate_replacement),
	std::iostream(&my_buf)
    { }

    // Same as str, erase, and empty on BufStreambuf
    inline const char *str() { return my_buf.str(); }
    inline void erase() { my_buf.erase(); }
    inline bool empty() { return my_buf.empty(); }
  };
}

#endif
