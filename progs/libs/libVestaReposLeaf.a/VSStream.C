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

#include <assert.h>
#include <BufStream.H>
#include "VSStream.H"

VSStreambuf::VSStreambuf(VestaSource* vs)
  : vs_file(vs), file_put_offset(0), file_get_offset(0),
    read_call_count(0), write_call_count(0)
{
  assert(vs != NULL);
  this->buf_size = (BUFSIZ<<1);
  this->buf = NEW_PTRFREE_ARRAY(char, this->buf_size);
  // leave 1 byte for an overflow
  setp(this->buf, this->buf + this->buf_size - 1); 
  setg(0,0,0);
}

VSStreambuf::~VSStreambuf()
{
  this->flush();
  delete [] buf;
}

void VSStreambuf::flush()
{
  if(pptr() != 0) {
    unsigned int count = pptr() - this->buf;
    assert(count <= this->buf_size);
    if(count > 0) {
      bool failed = false;
      int nbytes = count;
      try {
	VestaSource::errorCode err = 
	  this->vs_file->write(this->buf, &nbytes, this->file_put_offset);
	this->write_call_count++;
	if(err != VestaSource::ok) { 
	  this->error_msg = "VSStreambuf::flush: VestaSource::write failed: "
	    + Text(VestaSource::errorCodeString(err));
	  failed = true;
	}
      }
      catch(SRPC::failure f) {
	this->error_msg = "VSStreambuf::flush: SRPC failure: " + f.msg;
	failed = true;
      }
      if(!failed) {
	this->file_put_offset += count;
	// leave 1 byte for an overflow
	setp(this->buf, this->buf + this->buf_size - 1);
      }
    }
  }
}

void VSStreambuf::update_file_get_offset()
{
  // If there's a get area with unconsumed bytes, we need to figure
  // out the exact stream position we're at.
  if(gptr() && (gptr() < egptr()))
    {
      long int unread_count = (egptr() - gptr());
      this->file_get_offset -= unread_count;
    }

  // Reset the get area.
  setg(0,0,0);
}

bool VSStreambuf::error() 
{
  return !this->error_msg.Empty();
}

VSStreambuf::pos_type  
VSStreambuf::seekoff(off_type offset, seekdir way, openmode mode)
{
  VSStreambuf::pos_type ret = -1; 

  // flush any bytes waiting to be written and reset put area
  this->flush();
  if(this->error())
    return ret;
  setp(0,0);

  // make sure there is nothing in get area and reset it 
  update_file_get_offset();
  
  bool in = mode & std::ios::in;
  bool out = mode & std::ios::out;

  switch(way) {
    case(std::ios::beg):
      if(in) this->file_get_offset = offset;
      if(out) this->file_put_offset = offset;
      break;
    case(std::ios::cur):
      // if seek affects both streams get and put offsets should be equal
      if(in && out && (this->file_get_offset != this->file_put_offset)) {
	setp(this->buf, this->buf + this->buf_size - 1);
	return ret;
      }
      if(in) this->file_get_offset += offset;
      if(out) this->file_put_offset += offset;
      break;
    case(std::ios::end):
      try {
	Basics::uint64 size = this->vs_file->size();
	if(in) this->file_get_offset = size + offset;
	if(out) this->file_put_offset = size + offset;
      }
      catch(SRPC::failure f) {
	this->error_msg = "VSStreambuf::seekoff: SRPC failure: " + f.msg;
	return ret;
      }
      break;
  default: break;
  }    
  
  if(in) {
    ret = this->file_get_offset;
  }
  if(out) {
    ret = this->file_put_offset;
  }

  // set put area
  setp(this->buf, this->buf + this->buf_size - 1);
  return ret;
}

VSStreambuf::pos_type VSStreambuf::seekpos(pos_type pos, openmode mode)
{
  return this->seekoff(pos, std::ios::beg, mode);
}

int VSStreambuf::underflow()
{
  // Write to the file if there is anything in the put area
  this->flush();
  if(this->error())
    return EOF;

  // Reset the put area while we're reading.
  setp(0,0);

  // Read to the buffer
  bool failed = false;
  int read_count = this->buf_size;
  try {
    VestaSource::errorCode err = 
      this->vs_file->read(this->buf, &read_count, this->file_get_offset);
    this->read_call_count++;
    if(err != VestaSource::ok) {
      failed = true;
      this->error_msg = "VSStreambuf::underflow: VestaSource::read failed: "
	+ Text(VestaSource::errorCodeString(err));
    }
  }
  catch(SRPC::failure f) {
    failed = true;
    this->error_msg = "VSStreambuf::underflow: SRPC failure: " + f.msg;
  }
  if(!failed && read_count) {
    // Set the get area to the bytes we read.
    this->file_get_offset += read_count;
    assert(read_count <= this->buf_size);
    setg(this->buf, this->buf, this->buf + read_count);
    // Return the first character of the get area.
    return ((*eback())&0x00ff);
  }
  setg(0,0,0);
  return EOF;
}

void VSStreambuf::consume_from_gptr(char *&s, std::streamsize n,
				    std::streamsize &read_so_far)
{
  if((gptr() != 0) && (gptr() < egptr())) {
    unsigned int unread_count = (egptr() - gptr());
    unsigned int use_count = ((unread_count < (n-read_so_far))
			      ? unread_count
			      : (n-read_so_far));
    memcpy(s, gptr(), use_count);
    gbump(use_count);
    read_so_far += use_count;
    s += use_count;
  }
}

std::streamsize VSStreambuf::xsgetn(char* s, std::streamsize n)
{
  std::streamsize ret = 0;
  
  // Write to the file if there is anything in the put area
  this->flush();
  if(this->error())
    return ret;
  
  // Reset the put area while we're reading.
  setp(0,0);

  // If there's a get area with some uncomsumed bytes, 
  // take from that first.
  consume_from_gptr(s, n, ret);

  // There wasn't enough in the get area, but would the remainder be
  // less than we normally would read to fill the get area?  If so,
  // fill the get area and use bytes from there.  We do this to avoid
  // doing a short read after we reach the end of every large block we
  // read from the file.
  if((ret < n) && ((n - ret) < this->buf_size))
    {
      if(this->underflow() == EOF)
	// No more bytes available.  Return now rather than falling
	// through and doing an additional read call to the server.
	return ret;
      consume_from_gptr(s, n, ret);
    }
  
  // Have we still not read enough?  Read straight from the file.
  if(ret < n) {
    bool failed = false;
    int read_count = n - ret;
    try {
      VestaSource::errorCode err = 
	this->vs_file->read(s, &read_count, this->file_get_offset);
      this->read_call_count++;
      if(err != VestaSource::ok) {
	failed = true;
	this->error_msg = "VSStreambuf::xsgetn: VestaSource::read failed: "
	  + Text(VestaSource::errorCodeString(err));
      }
    }  
    catch(SRPC::failure f) {
      failed = true;
      this->error_msg = "VSStreambuf::xsgetn: SRPC failure: " + f.msg;
    }  
    if(!failed) {
      this->file_get_offset += read_count;
      ret += read_count;
    }
  }

  return ret;
}

int VSStreambuf::overflow(int c)
{
  // make sure there is nothing in the get area
  update_file_get_offset();

  // set put area if it's not set
  if(pptr() == 0)
    setp(this->buf, this->buf + this->buf_size - 1);
  
  unsigned int used = pptr() - this->buf;
  // Append the byte to be written.
  if(c != EOF) {
    *(pptr()) = (char) c;
    used++;
  }
  
  if(used > 0) {
    this->flush();
    if(this->error())
      return EOF;
  }
  
  // Indicate that everything is fine by returning the character
  // passed (converting an EOF into a non-EOF).
  return (c & 0x00ff);
}


std::streamsize VSStreambuf::xsputn(const char* s, std::streamsize n)
{
  if(n < 1)
    return 0;

  // make sure there is nothing in the get area
  update_file_get_offset();

  // set put area if it's not set, leave 1 byte for an overflow
  if(pptr() == 0)
    setp(this->buf, this->buf + this->buf_size - 1);
  
  // check if those bytes fit in the buffer,
  unsigned int free_space = (this->buf_size - 1) - (pptr() - this->buf);
  if(n <= free_space) { // write those n bytes into the buffer
    memcpy(pptr(), s, n);
    pbump(n);
    return n;
  }
  // too big to fit in the buffer, flush to the file
  if(overflow() == EOF)
    return -1;
 
  // check if there is enough space in the buffer now
  if(n <= (buf_size - 1)) {
    memcpy(pptr(), s, n);
    pbump(n);
    return n;
  }
  // there is no enough space in the buffer
  // write directly to the file
  int count = n;
  bool failed = false;
  try { 
    VestaSource::errorCode err = 
      this->vs_file->write(s, &count, this->file_put_offset);
    this->write_call_count++;
    if(err != VestaSource::ok) { 
      failed = true;
      this->error_msg = "VSStreambuf::xsputn: VestaSource::write failed: "
	+ Text(VestaSource::errorCodeString(err));
    }
  }  
  catch(SRPC::failure f) {
    failed = true;
    this->error_msg = "VSStreambuf::xsputn: SRPC failure: " + f.msg;
  }
  if(!failed) {
    this->file_put_offset += count;
    return count;
  }
  return 0;
}

int VSStreambuf::sync()
{
  // Flush any waiting output.
  if(pptr() && (pptr() > pbase()))
    // Return -1 on error, 0 on success.
    return (overflow() == EOF) ? -1 : 0;

  // Otherwise, there's nothing to do.
  return 0;
}
