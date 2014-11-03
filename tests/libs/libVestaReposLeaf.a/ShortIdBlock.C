// Copyright (C) 2001, Compaq Computer Corporation
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

//
// ShortIdBlock.C
//
// Implementation for ShortIdBlock.H
// 

#include "ShortIdBlock.H"
#include "VestaConfig.H"
#include "Basics.H"
#include <stdio.h>

using std::cerr;
using std::endl;

static Text sid_dir;
static int sid_dir_len;
static pthread_once_t once = PTHREAD_ONCE_INIT;

// Highest possible time, which is in January 2038 with 32-bit time_t.
// Presumably time_t will get larger before then.
const time_t ShortIdBlock::leaseNonexpiring = ((~((time_t) 0)) &
					       (~(((time_t) 1) << ((sizeof(time_t) * 8) - 1))));

extern "C"
{
  static void
  ShortIdBlockInit()
  {
    try {
	bool ok = VestaConfig::get("Repository", "sid_dir", sid_dir);
	if (!ok) sid_dir = "";
	sid_dir_len = sid_dir.Length();
    } catch (VestaConfig::failure f) {
	cerr << "VestaConfig::failure " << f.msg << endl;
	throw; // likely to be uncaught and fatal
    }
  }
}

// This method is local
ShortId ShortIdBlock::assignNextAvail() throw ()
{
    for (;;) {
	if (next >= start + size) {
	    return NullShortId;
	} else if (!assigned(next)) {
	    set(next);
	    return next++;
	} else {
	    next++;
	}
    }
}

char* ShortIdBlock::shortIdToName(ShortId sid, const char* prepend) throw ()
{
    // The encoding of ShortId as filename is hard-coded here,
    // relative to the current process working directory.  See also
    // DeleteAllShortIdsBut in ShortIdImpl.C, and note that
    // ShortIdBlock::size must be equal to the range of the last
    // pathname component (i.e., 0x100 if the last component is sid &
    // 0xff).
    //
    pthread_once(&once, ShortIdBlockInit);
    char* retval = NEW_PTRFREE_ARRAY(char, sid_dir_len + strlen(prepend) + 11);
    sprintf(retval, "%s%s%03x%c%03x%c%02x",
	    prepend, sid_dir.cchars(),
	    (sid >> 20) & 0xfff, PathnameSep,
	    (sid >> 8) & 0xfff,  PathnameSep,
	    sid & 0xff);
    return retval;
}
