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

#ifdef __linux__
// To get pread/pwrite:
#define _XOPEN_SOURCE 500
// To keep _XOPEN_SOURCE from messing up Linux system headers:
#define _BSD_SOURCE
#endif

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <Basics.H>
#include "FdStream.H"

// ==================== FS::FdStreambuf ====================

// ---------- private members ----------

void FS::FdStreambuf::get_pos_to_fd_pos()
{
  // If there's a get area, we need to figure out the exact stream
  // position we're at.
  if(gptr() && (gptr() < egptr()))
    {
      long int unread_count = (egptr() - gptr());
      fd_pos -= unread_count;
    }

  // Reset the get area.
  setg(0,0,0);
}

// ---------- constructors/destructor ----------

FS::FdStreambuf::FdStreambuf(int fd, off_t initial_pos)
  : _fd(-1), fd_pos(0), should_close(false), buffer(0), buf_size(0),
    saved_errno(0)
{
  this->attach(fd, initial_pos);
}

FS::FdStreambuf::FdStreambuf(const char *filename, stream_mode_t mode,
			     mode_t prot,
			     bool create, bool replace)
  : _fd(-1), fd_pos(0), should_close(false), buffer(0), buf_size(0),
    saved_errno(0)
{
  this->open(filename, mode, prot, create, replace);
}

FS::FdStreambuf::~FdStreambuf()
{
  (void) this->close();

  // Free the buffer if we've allocated it.
  if(buffer)
    delete [] buffer;
}

// ---------- public members ----------

FS::FdStreambuf* FS::FdStreambuf::attach(int fd, off_t initial_pos)
{
  // Don't overwrite existing fd
  if(is_open())
    return NULL;
  this->_fd = fd;
  this->should_close = false;
  // Check whether this is a valid fd with dup
  int fd_copy = dup(fd);
  if(fd_copy < 0)
    {
      this->saved_errno = errno;
      this->_fd = -1;
      return 0;
    }
  // Close the duplicate
  int close_res;
  do
    close_res = ::close(fd_copy);
  while((close_res != 0) && (errno == EINTR));
  // Assume read/write.
  this->mode = std::ios::in|std::ios::out;
  // Start at the specified position. (Note that the default is the
  // beginning of the file.)
  this->fd_pos = initial_pos;
  // OK, we're attached
  return this;
}

FS::FdStreambuf* FS::FdStreambuf::open(const char *filename,
				       stream_mode_t mode,
				       mode_t prot,
				       bool create, bool replace)
{
  // Determine whether we're opening for read, write, or read/write.
  int openflag;
  if((mode & std::ios::in) && (mode & std::ios::out))
    openflag = O_RDWR;
  else if(mode & std::ios::in)
    openflag = O_RDONLY;
  else if(mode & std::ios::out)
    openflag = O_WRONLY;
  else
    // Bogus mode: fail
    return 0;

  // Additional flags for the open(2) syscall.
  if(create)
    openflag |= O_CREAT;
  if(!replace)
    openflag |= O_EXCL;
  if(mode & std::ios::trunc)
    openflag |= O_TRUNC;
  if(mode & std::ios::app)
    openflag |= O_APPEND;
    
  // Open the file
  do
    this->_fd = ::open(filename, openflag, prot);
  while((this->_fd < 0) && (errno == EINTR));
  if(this->_fd < 0)
    {
      this->saved_errno = errno;
      return 0;
    }
  else
    // We opened it, so we should close it.
    this->should_close = true;

  // Save the mode we used to open the file.
  this->mode = mode;

  // Position at the end if that was requested.
  if(mode & std::ios::ate)
    {
      // Use fstat to get the position of the end of the file.
      struct stat st;
      if(fstat(this->_fd, &st) < 0)
	{
	  // Indicate an error to the caller
	  return 0;
	}
      this->fd_pos = st.st_size;
    }
  else
    {
      // Start at the beginning of the file.
      this->fd_pos = 0;
    }

  // All is well.
  return this;
}

FS::FdStreambuf* FS::FdStreambuf::close()
{
  // If we have an open file descriptor.
  if(is_open())
    {
      // Flush buffered data.
      sync();
      // Close the file descriptor if we're supposed to.
      if(should_close)
	{
	  // Loop on EINTR
	  int close_res;
	  do
	    close_res = ::close(this->_fd);
	  while((close_res != 0) && (errno == EINTR));
	  // Indicate any other errors to the caller
	  if(close_res != 0)
	    {
	      this->saved_errno = errno;
	      return 0;
	    }
	}
      // Reset the file descriptor.
      this->_fd = -1;

      return this;
    }

  return 0;
}

FS::FdStreambuf::stream_pos_t FS::FdStreambuf::seekoff(stream_off_t offset,
						       stream_seek_t way,
						       // NOTE: mode ignored!
						       stream_mode_t mode)
{
  // Flush any bytes waiting to be written.
  if(pptr() && (pptr() > pbase()))
    {
      // If overflow fails, indicate failure to the caller.
      if(overflow() == EOF)
	return ((stream_pos_t)-1);
    }
  else
    // If there's a partially-read get area, update fd_pos to the
    // logical get position.
    get_pos_to_fd_pos();

  // Reset the get and put areas before seeking.
  setg(0,0,0);
  setp(0,0);

  if(way == std::ios::beg)
    {
      // Offset is absolute position
      this->fd_pos = offset;
    }
  else if(way == std::ios::cur)
    {
      // Offset is relative to current position
      this->fd_pos += offset;
    }
  else
    {
      // Note: assume  (way == std::ios::end)

      // Offset is relative to the end of the file.  Use fstat to get
      // that.
      struct stat st;
      if(fstat(this->_fd, &st) < 0)
	{
	  // Indicate an error to the caller
	  return ((stream_pos_t)-1);
	}
      this->fd_pos = (st.st_size + offset);
    }

  return this->fd_pos;
}

FS::FdStreambuf::stream_pos_t FS::FdStreambuf::seekpos(stream_pos_t pos, 
						       // NOTE: mode ignored!
						       stream_mode_t mode)
{
  return this->seekoff(pos, std::ios::beg, mode);
}

// ---------- protected members ----------

int FS::FdStreambuf::underflow()
{
  // No good if we're not in input mode or don't have a valid file
  // descriptor.
  if(!(mode & std::ios::in))
    return EOF;
  if(!is_open())
    return EOF;

  // Flush any bytes waiting to be written.
  if(pptr() && (pptr() > pbase()))
    if(overflow() == EOF)
      return EOF;

  // Reset the put area while we're reading.
  assert(pptr() == pbase());
  setp(0,0);

  // Allocate space if neccessary
  if(buffer == 0)
    {
      buf_size = BUFSIZ;
      buffer = NEW_PTRFREE_ARRAY(char, buf_size);
    }

  int read_count;
  do
    read_count = pread(this->_fd, buffer, buf_size, this->fd_pos);
  while((read_count == -1) && (errno == EINTR));
  if(read_count < 1)
    {
      // Error or end-of-file: reset the get area and indicate a
      // failure to read more bytes to the caller.
      setg(0,0,0);
      if(read_count == -1)
	{
	  this->saved_errno = errno;
	}
      return EOF;
    }
  this->fd_pos += read_count;

  // Set the get area to the bytes we read.
  assert(read_count <= buf_size);
  setg(buffer, buffer, buffer+read_count);

  // Return the first character of the get area.
  return ((*eback())&0x00ff);
}

std::streamsize FS::FdStreambuf::xsgetn(char* s, std::streamsize n)
{
  std::streamsize result = 0;
    
  // Flush any un-written bytes first.
  if(pptr() && (pptr() > pbase()))
    if(overflow() == EOF)
      return -1;

  // If there's a get area with some uncomsumed bytes, take from that
  // first.
  if(gptr() && (gptr() < egptr()))
    {
      unsigned int unread_count = (egptr() - gptr());

      unsigned int use_count = (unread_count < n) ? unread_count : n;

      memcpy(s, gptr(), use_count);
	
      gbump(use_count);
      result += use_count;
      s += use_count;
    }

  // Not enough from the get area?  Read straight from the file
  // descriptor.
  if(result < n)
    {
      int read_count;
      do
	read_count = pread(this->_fd, s, n-result, this->fd_pos);
      while((read_count == -1) && (errno == EINTR));
      if(read_count >= 0)
	{
	  result += read_count;
	  this->fd_pos += read_count;
	}
      else
	{
	  this->saved_errno = errno;
	}
    }

  return result;
}

int FS::FdStreambuf::overflow(int c)
{
  // No good if we're not in output mode or don't have a valid file
  // descriptor.
  if(!(mode & std::ios::out))
    return EOF;
  if(!is_open())
    return EOF;

  // Before writing, update fd_pos to the correct position and reset
  // the get area.
  get_pos_to_fd_pos();

  // Make sure we have a put area.
  if(!pptr())
    {
      // Allocate space if neccessary
      if(buffer == 0)
	{
	  buf_size = BUFSIZ;
	  buffer = NEW_PTRFREE_ARRAY(char, buf_size);
	}

      // Set the put area.  We leave one byte extra to handle the
      // character passed into an overflow call.
      setp(buffer, buffer+(buf_size-1));
    }
  assert(pbase() == buffer);

  // Count the number of bytes currently in the put area.
  unsigned int count = pptr() - pbase();
  assert(count < buf_size);

  // If this was a real character being written (not just a request
  // that we flush), stick it in the put area before performing a
  // write.
  if(c != EOF)
    {
      *pptr() = c;
      count++;
    }

  if(count > 0)
    {
      // Write to the file descriptor.
      unsigned int written_count = 0;
      while(written_count < count)
	{
	  ssize_t written_this_time =
	    pwrite(this->_fd,
		   pbase() + written_count,
		   (count - written_count),
		   this->fd_pos);

	  if(written_this_time > 0)
	    {
	      this->fd_pos += written_this_time;
	      written_count += written_this_time;
	    }

	  if(written_this_time < 0)
	    {
	      if((errno == EINTR) || (errno == EAGAIN))
		continue;
	      this->saved_errno = errno;
	      setp(0,0);
	      return EOF;
	    }
	}

      assert(written_count == count);
    }

  // Reset the put area (again leaving one extra byte).
  assert(buffer != 0);
  setp(buffer, buffer+(buf_size-1));

  // Indicate that everything is fine by returning the character
  // passed (converting an EOF into a non-EOF).
  return (c & 0x00ff);
}

std::streamsize FS::FdStreambuf::xsputn(const char* s, std::streamsize n)
{
  // Handle degenrate case
  if(n < 1)
    return 0;
    
  // Allocate buffer if neccessary
  if(buffer == 0)
    {
      assert(pptr() == 0);
      buf_size = BUFSIZ;
      buffer = NEW_PTRFREE_ARRAY(char, buf_size);

      // Set the put area.  We leave one byte extra to handle the
      // character passed into an overflow call.
      setp(buffer, buffer+(buf_size-1));
    }

  // If we can fit this in the current put area, just stuff it in.
  if((pptr() != 0) &&
     (((pptr() - pbase()) + n) <= (buf_size - 1)))
    {
      memcpy(pptr(), s, n);
      pbump(n);
      return n;
    }

  // Too big to fit in the current buffer (or no buffer): overflow.
  // If overflow fails, indicate failure to the caller.
  if(overflow() == EOF)
    return -1;

  // If it would fit in the put area now that if it's empty,
  // copy it in.
  if(n <= (buf_size - 1))
    {
      memcpy(pptr(), s, n);
      pbump(n);
      return n;
    }

  // Too big to fit in the put area.  Write it directly to the
  // file descriptor.
  assert(n >= buf_size);
  assert(pptr() == pbase());
  unsigned int written_count = 0;
  while(written_count < n)
    {
      ssize_t written_this_time =
	pwrite(this->_fd,
	       s + written_count,
	       n - written_count,
	       this->fd_pos);

      if(written_this_time > 0)
	{
	  this->fd_pos += written_this_time;
	  written_count += written_this_time;
	}

      // If the write failed, reset the put area.
      if(written_this_time < 0)
	{
	  if((errno == EINTR) || (errno == EAGAIN))
	    continue;
	  this->saved_errno = errno;
	  setp(0,0);
	  break;
	}
    }

  // Pass the result of the write(2) call up to the caller.
  return written_count;
}

int FS::FdStreambuf::sync()
{
  // Flush any waiting output.
  if(pptr() && (pptr() > pbase()))
    // Return -1 on error, 0 on success.
    return (overflow() == EOF) ? -1 : 0;

  // Otherwise, there's nothing to do.
  return 0;
}
