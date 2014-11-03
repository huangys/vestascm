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

// Last modified on Thu Feb 16 14:34:54 EST 2006 by ken@xorian.net        

#include <assert.h>
#include <string.h>

#include "BufStream.H"
#include "Basics.H"

Basics::BufStreambuf::BufStreambuf(unsigned int initial_buf_len)
  : buf(NEW_PTRFREE_ARRAY(char, initial_buf_len)),
    buf_len(initial_buf_len), delete_buf(true), allocate_new_buf(true),
    get_pos(0), put_pos(0), valid_limit(0)
{
  // there must be at least one byte for the string terminator
  assert(initial_buf_len > 0);

  setp(buf, buf+(buf_len-1));
}

void Basics::BufStreambuf::update_get_pos()
{
  if(gptr() != 0)
    get_pos = gptr() - buf;
}

void Basics::BufStreambuf::update_put_pos()
{
  if(pptr() != 0)
    put_pos = pptr() - buf;
  if(put_pos > valid_limit)
    valid_limit = put_pos;
}

bool Basics::BufStreambuf::alloc_if_needed(unsigned int new_byte_count)
{
  // Figure out how many characters have been written already
  update_put_pos();

  // Do we need to more space to fit this byte into the buffer
  // (leaving room for a terminating null byte)?
  if(((unsigned int) valid_limit + new_byte_count + 1) > buf_len)
    {
      // If we're not supposed to allocate a new buffer, we can't
      // write this byte.
      if(!allocate_new_buf)
	{
	  return false;
	}

      // Figure out where in the buffer the get pointer is.
      update_get_pos();

      // Determine the size of the new buffer we'll allocate.  Don't
      // allocate more than BUFSIZ extra bytes beyond what we need for
      // the current write.
      unsigned int new_buf_len;
      if((buf_len + new_byte_count) < BUFSIZ)
	new_buf_len = (buf_len + new_byte_count) * 2;
      else
	new_buf_len = buf_len + new_byte_count + BUFSIZ;

      // Allocate the new buffer.
      char *new_buf = NEW_PTRFREE_ARRAY(char, new_buf_len);

      // Copy the existing bytes.
      memcpy(new_buf, buf, valid_limit);

      // Delete the old buffer if we need to.
      if(delete_buf)
	delete [] buf;

      // This is our buffer now.
      buf = new_buf;
      buf_len = new_buf_len;

      // The put area is the new buffer.  Make sure to mark the
      // already written portion as used.
      setp(buf, buf+(buf_len-1));
      pbump(put_pos);

      // Update the get area for the new buffer as well.
      setg(buf, buf+get_pos, buf+valid_limit);

      // We're responsible for deleting this new buffer.
      delete_buf = true;

      // It better be big enough.
      assert(((unsigned int) valid_limit  + new_byte_count + 1) < buf_len);
    }

  return true;
}

Basics::BufStreambuf::pos_type
Basics::BufStreambuf::seekoff(off_type offset,
			      seekdir way,
			      openmode mode)
{
  pos_type result = 0;

  if(mode & std::ios::out)
    {
      update_put_pos();

      if(way == std::ios::beg)
	put_pos = offset;
      else if(way == std::ios::cur)
	put_pos += offset;
      else
	put_pos = valid_limit + offset;

      if(put_pos < 0)
	put_pos = 0;
      if(put_pos > valid_limit)
	put_pos = valid_limit;

      if((buf != 0) && (buf_len > 0) && (put_pos < (pos_type) (buf_len-1)))
	setp(buf+put_pos, buf+(buf_len-1));
      else
	setp(0,0);

      result = put_pos;
    }

  if(mode & std::ios::in)
    {
      update_get_pos();

      if(way == std::ios::beg)
	get_pos = offset;
      else if(way == std::ios::cur)
	get_pos += offset;
      else
	get_pos = valid_limit + offset;

      if(get_pos < 0)
	get_pos = 0;
      if(get_pos > valid_limit)
        get_pos = valid_limit;

      // If we have data and the get position is within the valid
      // portion...
      if((buf != 0) && (valid_limit > 0) && (get_pos < valid_limit))
	// Set the get area
	setg(buf, buf+get_pos, buf+valid_limit);
      else
	setg(0,0,0);

      result = get_pos;
    }

  return result;
}

Basics::BufStreambuf::pos_type
Basics::BufStreambuf::seekpos(pos_type pos, 
			      openmode mode)
{
  // Check to see how much has been written up to now, as that affects
  // where we can seek to.
  update_put_pos();

  if(pos < 0)
    pos = 0;
  if(pos > valid_limit)
    pos = valid_limit;

  if(mode & std::ios::in)
    {
      get_pos = pos;

      // If we have data and the get position is within the valid
      // portion...
      if((buf != 0) && (valid_limit > 0) && (get_pos < valid_limit))
	// Set the get area
	setg(buf, buf+get_pos, buf+valid_limit);
      else
	setg(0,0,0);
    }

  if(mode & std::ios::out)
    {
      put_pos = pos;

      assert(buf != 0);
      assert(put_pos <= (pos_type) (buf_len-1));
      setp(buf+put_pos, buf+(buf_len-1));
    }

  return pos;
}

void Basics::BufStreambuf::erase()
{
  put_pos = 0;
  get_pos = 0;
  valid_limit = 0;

  setp(buf, buf+(buf_len-1));
  setg(0,0,0);
}

int Basics::BufStreambuf::underflow()
{
  // Check to see how much has been written (as that can affect how
  // much can be read).
  update_put_pos();

  update_get_pos();

  // If we have data and the get position is within the valid
  // portion...
  if((buf != 0) && (valid_limit > 0) && (get_pos < valid_limit))
    {
      // Set the get area
      setg(buf, buf+get_pos, buf+valid_limit);

      // Return the first character of the get area.
      return ((*gptr())&0x00ff);
    }

  // If we make it here, we have no data to use.
  return EOF;
}

std::streamsize Basics::BufStreambuf::xsgetn(char* s, std::streamsize n)
{
  std::streamsize result = 0;

  // Check to see how much has been written (as that can affect how
  // much can be read).
  update_put_pos();

  update_get_pos();

  if(buf != 0)
    {
      if(get_pos < valid_limit)
	{
	  unsigned int unread_count = valid_limit - get_pos;

	  unsigned int use_count = (unread_count < n) ? unread_count : n;

	  memcpy(s, buf + get_pos, use_count);
	
	  result += use_count;
	  get_pos += (pos_type) use_count;
	}

      // Set the get area
      setg(buf, buf+get_pos, buf+valid_limit);
    }

  return result;
}

int Basics::BufStreambuf::overflow(int c)
{
  // Append the byte to be written.
  if(c != EOF)
    {
      // If we don't have enough space and can't allocate more, we
      // can't write this byte.
      if(!alloc_if_needed(1))
	return EOF;

      assert(pptr() != 0);

      *(pptr()) = (char) c;
      pbump(1);
    }

  // Indicate that everything is fine by returning the character
  // passed (converting an EOF into a non-EOF).
  return (c & 0x00ff);
}

std::streamsize Basics::BufStreambuf::xsputn(const char* s, std::streamsize n)
{
  // If we don't have enough space and can't allocate more, we can't
  // write these bytes.
  if(!alloc_if_needed(n))
    return -1;

  assert(pptr() != 0);

  // Copy these
  memcpy(pptr(), s, n);
  pbump(n);
  return n;
}
