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

#include "FPStream.H"

FP::FPStreambuf::FPStreambuf()
  : total_bytes(0), tag("", 0)
{
  setp(this->buf.bytes,
       this->buf.bytes +sizeof(Word));
}

FP::FPStreambuf::FPStreambuf(const FP::Tag &init_tag)
  : total_bytes(0)
{
  this->tag = init_tag;
  setp(this->buf.bytes,
       this->buf.bytes +sizeof(Word));
}

void FP::FPStreambuf::flush()
{
  unsigned int count = pptr() - this->buf.bytes;
  assert(count <= sizeof(Word));
  if(count > 0)
    this->tag.Extend(this->buf.bytes, count);
  this->total_bytes += count;
  setp(this->buf.bytes,
       this->buf.bytes +sizeof(Word));
}

FP::FPStreambuf::pos_type
FP::FPStreambuf::seekoff(off_type offset,
			      seekdir way,
			      openmode mode)
{
  // We don't seek
  this->flush();
  return this->total_bytes;
}

FP::FPStreambuf::pos_type
FP::FPStreambuf::seekpos(pos_type pos, 
			      openmode mode)
{
  // We don't seek
  this->flush();
  return this->total_bytes;
}

int FP::FPStreambuf::underflow()
{
  // We don't do reading.
  return EOF;
}

std::streamsize FP::FPStreambuf::xsgetn(char* s, std::streamsize n)
{
  // We don't do reading.
  return 0;
}

int FP::FPStreambuf::overflow(int c)
{
  this->flush();

  // Append the byte to be written.
  if(c != EOF)
    {
      assert(pptr() == this->buf.bytes);

      *(pptr()) = (char) c;
      pbump(1);
    }

  // Indicate that everything is fine by returning the character
  // passed (converting an EOF into a non-EOF).
  return (c & 0x00ff);
}

std::streamsize FP::FPStreambuf::xsputn(const char* s, std::streamsize n)
{
  this->flush();

  this->tag.Extend(s, n);

  total_bytes += n;
  
  return n;
}
