// Copyright (C) 2001, Compaq Computer Corporation

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
// TestSizes.C - A simple program to make sure that some fundamental
// data types defined in Basics.H are the correct size.  Handy when
// porting to a new platform.  Also tests conversion to/from network
// byte order.
// ----------------------------------------------------------------------

// Last modified on Thu May 27 00:57:35 EDT 2004 by ken@xorian.net

// Include the type definitions.
#include <Basics.H>

// A macro to shorten the following code.
#define CHECK_SIZE(type, size) \
  if(sizeof(type) != size) \
    { \
      cout << "Error: " #type " should be " #size " byte(s) but is " \
	   << sizeof(type) << endl; \
      return 1; \
    } \
  (void) 0

using std::cout;
using std::endl;

int main(void)
{
  // Check the size of the fundamental types.
  CHECK_SIZE(Basics::int8, 1);
  CHECK_SIZE(Basics::uint8, 1);
  CHECK_SIZE(Basics::int16, 2);
  CHECK_SIZE(Basics::uint16, 2);
  CHECK_SIZE(Basics::int32, 4);
  CHECK_SIZE(Basics::uint32, 4);
  CHECK_SIZE(Basics::int64, 8);
  CHECK_SIZE(Basics::uint64, 8);

  // These are redundant since they should be based on the above, but
  // it doesn't hurt.  (And who knows, it could get confused in the
  // future.)
  CHECK_SIZE(Bit8, 1);
  CHECK_SIZE(Bit16, 2);
  CHECK_SIZE(Bit32, 4);
  CHECK_SIZE(Bit64, 8);
  CHECK_SIZE(Word, 8);

  CHECK_SIZE(bool8, 1);

  CHECK_SIZE(Byte32, 32);

  // Make sure PointerInt is the same size as a pointer.
  if(sizeof(PointerInt) != sizeof(void *))
    {
      cout << "Error: PointerInt is not the same size as void *"
	   << endl
	   << "\tPointerInt is " << sizeof(PointerInt) << " byte(s)" << endl
	   << "\tvoid * is " << sizeof(void *) << " byte(s)" << endl;
      return 1;
    }

  // The SRPC send/recv_float functions expect that "float" is the
  // same size as Basics::int32 (i.e. that it's an IEEE
  // single-precision floating-point number).
  if(sizeof(float) != sizeof(Basics::int32))
    {
      cout << "Error: float is not the same size as Basics::int32"
	   << endl
	   << "\tfloat is " << sizeof(float) << " byte(s)" << endl
	   << "\tBasics::int32 is " << sizeof(Basics::int32) << " byte(s)"
	   << endl;
      return 1;
    }

  // If we make it down to here without exiting, then the sizes must
  // be OK.
  cout << "All types seem to be the right size!" << endl;

  // Now test the network byte-order conversions.
  Basics::uint16 l_host16 = 0x1234;
  Basics::uint16 l_net16 = Basics::hton16(l_host16);
  if(Basics::ntoh16(l_net16) != l_host16)
    {
      char orig_buff[5], net_buff[5], final_buff[5];
      sprintf(orig_buff,  "%04hx", l_host16);
      sprintf(net_buff,   "%04hx", l_net16);
      sprintf(final_buff, "%04hx", Basics::ntoh16(l_net16));
      cout << "Error: Conversion of 16-bit values to/from network "
	   << "byte-order is broken:" << endl
	   << "\tx                 = 0x" << orig_buff << endl
	   << "\thton16(x)         = 0x" << net_buff << endl
	   << "\tntoh16(hton16(x)) = 0x"
	   << final_buff << endl
	   << endl;
      return 1;
    }

  Basics::uint32 l_host32 = 0x12345678;
  Basics::uint32 l_net32 = Basics::hton32(l_host32);
  if(Basics::ntoh32(l_net32) != l_host32)
    {
      char orig_buff[9], net_buff[9], final_buff[9];
      sprintf(orig_buff,  "%08x", l_host32);
      sprintf(net_buff,   "%08x", l_net32);
      sprintf(final_buff, "%08x", Basics::ntoh32(l_net32));
      cout << "Error: Conversion of 32-bit values to/from network "
	   << "byte-order is broken:" << endl
	   << "\tx                 = 0x" << orig_buff << endl
	   << "\thton32(x)         = 0x" << net_buff << endl
	   << "\tntoh32(hton32(x)) = 0x"
	   << final_buff << endl
	   << endl;
      return 1;
    }

  Basics::uint64 l_host64 = CONST_INT_64(0x123456789abcdef0);
  Basics::uint64 l_net64 = Basics::hton64(l_host64);
  if(Basics::ntoh64(l_net64) != l_host64)
    {
      char orig_buff[17], net_buff[17], final_buff[17];
      sprintf(orig_buff,  "%016" FORMAT_LENGTH_INT_64 "x", l_host64);
      sprintf(net_buff,   "%016" FORMAT_LENGTH_INT_64 "x", l_net64);
      sprintf(final_buff, "%016" FORMAT_LENGTH_INT_64 "x",
	      Basics::ntoh64(l_net64));
      cout << "Error: Conversion of 64-bit values to/from network "
	   << "byte-order is broken:" << endl
	   << "\tx                 = 0x" << orig_buff << endl
	   << "\thton64(x)         = 0x" << net_buff << endl
	   << "\tntoh64(hton64(x)) = 0x" << final_buff << endl
	   << endl;
      return 1;
    }

  // If we make it here, then the network byte-order conversions must
  // be working.
  cout << "All network byte-order conversions work!" << endl;

  return 0;
}
