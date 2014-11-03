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
#include <iosfwd>
#include <iostream>
#include "FP.H"

namespace FP {
  class FPStreambuf : public std::streambuf
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
    union {
       // Really we just want to make sure that the character array is
       // word-aligned
      Word unused;
      char bytes[sizeof(Word)];
    } buf;

    FP::Tag tag;

    void flush();

    unsigned int total_bytes;

  public:
    FPStreambuf();
    FPStreambuf(const FP::Tag &init_tag);

    inline const FP::Tag &get_tag() throw()
    {
      this->flush();
      return this->tag;
    }

    pos_type seekoff(off_type offset,
		     seekdir way,
		     openmode mode=(std::ios::in|
				    std::ios::out));
    pos_type seekpos(pos_type pos, 
		     openmode mode=(std::ios::in|
				    std::ios::out));

  protected:
    int underflow();
    std::streamsize xsgetn(char* s, std::streamsize n);
    int overflow(int c);
    std::streamsize xsputn(const char* s, std::streamsize n);
  };

  class FPStreamBase : virtual public std::ios
  {
  protected:
    FPStreambuf my_buf;
  public:
    FPStreamBase()
      : my_buf()
    {
    }
    FPStreamBase(const FP::Tag &init_tag)
      : my_buf(init_tag)
    {
    }
    FPStreambuf *rdbuf() { return &my_buf; }
  };

  class FPStream : public FPStreamBase, public std::ostream
  {
  public:
    FPStream()
      : FPStreamBase(),
	std::ostream(&my_buf)
    { }
    FPStream(const FP::Tag &init_tag)
      : FPStreamBase(init_tag),
	std::ostream(&my_buf)
    { }

    inline const FP::Tag &tag()
    {
      return my_buf.get_tag();
    }
  };

}
