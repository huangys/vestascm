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

// TestBufStream.C

// Tests for the Basics::OBufStream class.

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "BufStream.H"
#include "Basics.H"

#include <iostream>
#include <iomanip>
#include <sstream>

using std::cout;
using std::cerr;
using std::endl;
using std::ios;
using std::hex;
using std::dec;
using std::setw;
using std::setfill;
using std::ostringstream;
using std::string;

// ----------------------------------------------------------------------
// Support code for generating strings similar to what goes in the
// repository's transaction log.
// ----------------------------------------------------------------------

// A toy version of the repository LongId class, just used for
// formatting messages like what the repository puts in its
// transaction log.

struct LongId
{
  struct { unsigned char byte[32]; } value;

  int length() const throw()
  {
    unsigned int len = 1;
    while((len < sizeof(value.byte)) && (value.byte[len] != 0))
      {
	len++;
      }
    return len;
  }

  void append(unsigned int index) throw ()
  {
    unsigned char* start = &value.byte[0];
    unsigned char* end =
      (unsigned char*) memchr((const void*) (start + 1), 0, sizeof(value)-1);
    while (index > 0) {
	if (end >= &value.byte[sizeof(value)]) {
	    // This index won't fit
	    return;
	}
	int newbyte = index & 0x7f;
	index >>= 7;
	if (index > 0) newbyte |= 0x80;
	*end++ = newbyte;
    }	    
  }

  LongId()
  {
    memset(&(this->value), 0, sizeof(this->value));

    // Fill this LongId with random stuff.
    int target_bytes = (random() % 16) + 8;
    while(length() < target_bytes)
      append(random() % 8192);
  }

};

using std::ostream;

// Functions from VLogHelp.C in the repository.

ostream&
operator<<(ostream& s, const LongId& longid)
{
    int i;
    int len = longid.length();
    for (i=0; i<len; i++) {
	s << setw(2) << setfill('0') << hex
	  << (int) (longid.value.byte[i] & 0xff);
    }
    s << setfill(' ') << dec;
    return s;
}

ostream&
PutQuotedString(ostream& s, const char* string)
{
    s << '\"';
    while (*string) {
	if (*string == '\"' || *string == '\\') {
	    s << '\\';
	}
	s << *string++;
    }
    s << '\"';
    return s;
}

// Generate a random arc for a repository transaction
string random_arc()
{
  string result;
  int length = 4 + (random() % 20);
  static const char *letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static const char *digits = "0123456789";
  static const char *specials = "_.";
  while(result.size() < length)
    {
      switch(random() % 8)
	{
	case 0:
	  result += specials[random() % 2];
	  break;
	case 1:
	case 2:
	  result += digits[random() % 10];
	  break;
	default:
	  result += letters[random() % 52];
	  break;
	}
    }

  return result;
}

// ----------------------------------------------------------------------

void print_stream_state(unsigned int state)
{
  bool first = true;
  if(state & ios::eofbit)
    {
      cerr << "eof";
      state &= ~((unsigned int) ios::eofbit);
      first = false;
    }
  if(state & ios::failbit)
    {
      if(!first)
	cerr << "|";
      cerr << "fail";
      state &= ~((unsigned int) ios::failbit);
      first = false;
    }
  if(state & ios::badbit)
    {
      if(!first)
	cerr << "|";
      cerr << "bad";
      state &= ~((unsigned int) ios::badbit);
      first = false;
    }
  if(state != 0)
    {
      if(!first)
	cerr << "|";
      cerr << "0x" << hex << state << dec;
    }
}

void print_state_if_not_good(const ios &stream,
			     const char *name, const char *marker)
{
  if(!stream.good())
    {
      cerr << name << " state (" << marker << "): ";
      print_stream_state(stream.rdstate());
      cerr << endl;
      abort();
    }  
}

// ('time' longid timestamp)
void format_time(LongId longid, time_t timestamp, std::ostream &out)
{
  out << "(time " << longid << " " << timestamp << ")\n";
}

// ('del' longid arc timestamp)
void format_del(LongId longid, time_t timestamp, string &arc, std::ostream &out)
{
  out << "(del " << longid << " ";
  PutQuotedString(out, arc.c_str());
  out << " " << timestamp << ")\n";
}

// ('insf' longid arc sid master timestamp)
void format_insf(LongId longid, time_t timestamp, string &arc,
		 unsigned int sid, bool mast,
		 // FP::Tag *forceFPTag,
		 std::ostream &out)
{
  out << "(insf " << longid << " ";
  PutQuotedString(out, arc.c_str());
  out << " 0x" << hex << sid << dec << " " 
	   << (int) mast << " " << timestamp;
  /*
    if (forceFPTag) {
    out << " ";
    PutFPTag(out, *forceFPTag);
    }
  */
  out << ")\n";
}

// ('insu' longid arc sid master timestamp)
void format_insu(LongId longid, time_t timestamp, string &arc,
		 unsigned int sid, bool mast,
		 std::ostream &out)
{
  out << "(insu " << longid << " ";
  PutQuotedString(out, arc.c_str());
  out << " 0x" << hex << sid << dec << " " 
	   << (int) mast << " " << timestamp << ")\n";
}

// ----------------------------------------------------------------------

void output_tests()
{
  {
    Basics::OBufStream foo;

    print_state_if_not_good(foo, "foo", "start");
    foo << "Here's a simple test.";
    print_state_if_not_good(foo, "foo", "1");

    const char *foo_str = foo.str();

    if(strcmp(foo_str, "Here's a simple test.") != 0)
      {
	cerr << "foo.str() = \"" << foo.str() << "\"" << endl
	     << "strlen(foo.str()) = " << strlen(foo.str()) << endl;
	abort();
      }
  }

  {
    char foo2_buf[256];
    Basics::OBufStream foo2(foo2_buf, sizeof(foo2_buf));

    print_state_if_not_good(foo2, "foo2", "start");
    foo2 << "Here's another test, with some text that's a bit longer.";
    print_state_if_not_good(foo2, "foo2", "1");
    foo2 << endl;
    print_state_if_not_good(foo2, "foo2", "2");
    foo2 << "And a second sentence on another line.";
    print_state_if_not_good(foo2, "foo2", "3");
    foo2 << endl;
    print_state_if_not_good(foo2, "foo2", "4");

    const char *foo2_str = foo2.str();

    if(strcmp(foo2_str, ("Here's another test, with some text that's a bit longer.\n"
			 "And a second sentence on another line.\n")) != 0)
      {
	cerr << "foo2.str() = \"" << foo2.str() << "\"" << endl
	     << "strlen(foo2.str()) = " << strlen(foo2.str()) << endl;
	abort();
      }

    assert(foo2.str() == foo2_buf);
  }

  {
    char logrec[512], logrec2[10];
    // Fixed buffer, no reallocation
    Basics::OBufStream time_tos(logrec, sizeof(logrec));
    // No buffer
    Basics::OBufStream time_tos2;
    // Small buffer, re-allocate when it fills
    Basics::OBufStream time_tos3(logrec2, sizeof(logrec2), 0, true);
    ostringstream time_oss;

    // Arguments
    LongId longid;
    time_t timestamp = time(0);

    format_time(longid, timestamp, time_tos);
    format_time(longid, timestamp, time_tos2);
    format_time(longid, timestamp, time_tos3);
    format_time(longid, timestamp, time_oss);

    assert(strcmp(time_oss.str().c_str(), time_tos.str()) == 0);
    assert(strcmp(time_oss.str().c_str(), time_tos2.str()) == 0);
    assert(strcmp(time_oss.str().c_str(), time_tos3.str()) == 0);
    assert(time_tos.str() == logrec);
    assert(time_tos3.str() != logrec2);
  }

  {
    char logrec[512], logrec2[10];
    // Fixed buffer, no reallocation
    Basics::OBufStream del_tos(logrec, sizeof(logrec));
    // No buffer
    Basics::OBufStream del_tos2;
    // Small buffer, re-allocate when it fills
    Basics::OBufStream del_tos3(logrec2, sizeof(logrec2), 0, true);
    ostringstream del_oss;

    // Arguments
    LongId longid;
    time_t timestamp = time(0);
    string arc = random_arc();

    // Fill the different streams
    format_del(longid, timestamp, arc, del_tos);
    format_del(longid, timestamp, arc, del_tos2);
    format_del(longid, timestamp, arc, del_tos3);
    format_del(longid, timestamp, arc, del_oss);

    // They should all have the same contents
    assert(strcmp(del_oss.str().c_str(), del_tos.str()) == 0);
    assert(strcmp(del_oss.str().c_str(), del_tos2.str()) == 0);
    assert(strcmp(del_oss.str().c_str(), del_tos3.str()) == 0);

    // The one with sufficient buffer space shouldn't have
    // re-allocated
    assert(del_tos.str() == logrec);
    assert(del_tos3.str() != logrec2);
  }

  {
    char logrec[512], logrec2[10];
    // Fixed buffer, no reallocation
    Basics::OBufStream insf_tos(logrec, sizeof(logrec));
    // No buffer
    Basics::OBufStream insf_tos2;
    // Small buffer, re-allocate when it fills
    Basics::OBufStream insf_tos3(logrec2, sizeof(logrec2), 0, true);
    ostringstream insf_oss;

    // Arguments
    LongId longid;
    time_t timestamp = time(0);
    string arc = random_arc();
    unsigned int sid = random();
    bool mast = ((random() % 2) == 0) ? true : false;

    format_insf(longid, timestamp, arc, sid, mast, insf_tos);
    format_insf(longid, timestamp, arc, sid, mast, insf_tos2);
    format_insf(longid, timestamp, arc, sid, mast, insf_tos3);
    format_insf(longid, timestamp, arc, sid, mast, insf_oss);

    assert(strcmp(insf_oss.str().c_str(), insf_tos.str()) == 0);
    assert(strcmp(insf_oss.str().c_str(), insf_tos2.str()) == 0);
    assert(strcmp(insf_oss.str().c_str(), insf_tos3.str()) == 0);
    assert(insf_tos.str() == logrec);
    assert(insf_tos3.str() != logrec2);
  }

  // ('insu' longid arc sid master timestamp)
  {
    char logrec[512], logrec2[10];
    // Fixed buffer, no reallocation
    Basics::OBufStream insu_tos(logrec, sizeof(logrec));
    // No buffer
    Basics::OBufStream insu_tos2;
    // Small buffer, re-allocate when it fills
    Basics::OBufStream insu_tos3(logrec2, sizeof(logrec2), 0, true);
    ostringstream insu_oss;

    LongId longid;
    time_t timestamp = time(0);
    string arc = random_arc();
    unsigned int sid = random();
    bool mast = ((random() % 2) == 0) ? true : false;

    format_insu(longid, timestamp, arc, sid, mast, insu_tos);
    format_insu(longid, timestamp, arc, sid, mast, insu_tos2);
    format_insu(longid, timestamp, arc, sid, mast, insu_tos3);
    format_insu(longid, timestamp, arc, sid, mast, insu_oss);

    assert(strcmp(insu_oss.str().c_str(), insu_tos.str()) == 0);
    assert(strcmp(insu_oss.str().c_str(), insu_tos2.str()) == 0);
    assert(strcmp(insu_oss.str().c_str(), insu_tos3.str()) == 0);
    assert(insu_tos.str() == logrec);
    assert(insu_tos3.str() != logrec2);
  }
}

void input_tests()
{
  // Simple test: write an integer string into a character buffer, and
  // read it back using an IBufStream.  The integer we read back
  // should the the one we put in, and we should read the entire
  // string.
  {
    char buf[20];
    int r = random();
    sprintf(buf, "%d", r);

    Basics::IBufStream in_test(buf, sizeof(buf), strlen(buf));

    int r2;
    in_test >> r2;

    assert(r == r2);
    assert((unsigned int) in_test.tellg() == strlen(buf));
  }

  // Test based on evaluator unpickling code: reading binary data out
  // of an IBufStream.
  {
    char data[] = { 0x0, 0x0, 0x0, 0x2, 0x1, 0x4e, 0x0, 0x0,
		    0x0, 0x3, 0x4e, 0x0, 0x5, 0x45, 0x0, 0x1,
		    0x4c, 0x0, 0x6 };
    Basics::IBufStream in(data, sizeof(data), sizeof(data));

    // First four bytes are the pickle version
    Basics::uint32 pVers;
    in.read((char *) &pVers, sizeof(pVers));
    assert(in.gcount() == sizeof(pVers));
    assert(Basics::ntoh32(pVers) == 2);

    // Next byte is a boolean
    bool8 hasPath;
    in.read((char *) &hasPath, sizeof(hasPath));
    assert(in.gcount() == sizeof(hasPath));
    assert(hasPath == 1);

    // Next byte is a path kind
    char pk;
    in.read(&pk, sizeof(pk));
    assert(in.gcount() == sizeof(pk));
    assert(pk == 'N');

    // Next two bytes are an index
    Basics::uint16 index;
    in.read((char *) &index, sizeof(index));
    assert(in.gcount() == sizeof(index));
    assert(index == 0);

    // Next two bytes are the DPS length
    Basics::uint16 dpsLen;
    in.read((char *) &dpsLen, sizeof(dpsLen));
    assert(in.gcount() == sizeof(dpsLen));
    assert(Basics::ntoh16(dpsLen) == 3);

    // First DPS: N, 5
    in.read(&pk, sizeof(pk));
    assert(in.gcount() == sizeof(pk));
    assert(pk == 'N');

    in.read((char *) &index, sizeof(index));
    assert(in.gcount() == sizeof(index));
    assert(Basics::ntoh16(index) == 5);

    // Second DPS: E, 1
    in.read(&pk, sizeof(pk));
    assert(in.gcount() == sizeof(pk));
    assert(pk == 'E');

    in.read((char *) &index, sizeof(index));
    assert(in.gcount() == sizeof(index));
    assert(Basics::ntoh16(index) == 1);

    // Third DPS: L, 6
    in.read(&pk, sizeof(pk));
    assert(in.gcount() == sizeof(pk));
    assert(pk == 'L');

    in.read((char *) &index, sizeof(index));
    assert(in.gcount() == sizeof(index));
    assert(Basics::ntoh16(index) == 6);

    // At the end, we should have read the whole thing.
    assert((unsigned int) in.tellg() == sizeof(data));
  }
}

void io_tests()
{
  // Test using a bi-directional BufStream: write a random integer,
  // then read it back.
  for(unsigned int i = 0; i < 100; i++)
    {
      int r = random();
      Basics::BufStream test;
      test << r;
      int r2;
      test >> r2;
      assert(r == r2);
      assert(test.tellp() == test.tellg());
    }

  // Similar test, but reading and writing many with one stream.
  {
    Basics::BufStream test(10);
    for(unsigned int i = 0; i < 100; i++)
      {
	int r = random();
	test << r << endl;
	int r2;
	test >> r2;
	char c;
	test.get(c);
	assert(r == r2);
	assert(c == '\n');
	assert(test.tellp() == test.tellg());
      }
  }
}

int main(int argc, char* argv[])
{
  srandom(time(0));

  output_tests();

  input_tests();

  io_tests();

  cout << "All tests passed!" << endl;

  return 0;
}
