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
// VForward.C
//

#include "VestaSource.H"
#include "VMemPool.H"
#include "VForward.H"
#include <assert.h>
#include "logging.H"
#include "VLogHelp.H"
#include "DebugDumpHelp.H"
#include <BufStream.H>

using std::endl;
using std::hex;
using std::dec;
using Basics::OBufStream;

Bit32
VForward::create(const LongId& longid) throw ()
{
    VForward* vf;
    vf = (VForward*) VMemPool::allocate(VMemPool::vForward, sizeof(VForward));
    *(vf->longid()) = longid;
    return VMemPool::shortenPointer(vf);
}

void
VForward::markCallback(void* closure, VMemPool::typeCode type) throw ()
{
    // Nothing to do!
}

bool
VForward::sweepCallback(void* closure, VMemPool::typeCode type,
			void* addr, Bit32& size) throw ()
{
    VForward* vf = (VForward*) addr;
    bool ret = vf->visited();
    vf->setVisited(false);
    size = sizeof(VForward);
    return ret;
}

void
VForward::rebuildCallback(void* closure, VMemPool::typeCode type,
			  void* addr, Bit32& size) throw ()
{
    // Just compute size
    VForward* vf = (VForward*) addr;
    size = sizeof(VForward);
}

Bit32
VForward::checkpoint(Bit32& nextSP, std::fstream& ckpt) throw ()
{
    if (visited()) return redirection();
    Bit32 newSP = nextSP;
    int pad = (-sizeof(VForward)) & VMemPool::alignmentMask;
    nextSP += sizeof(VForward) + pad;
    ckpt.write((char *) rep, sizeof(VForward));
    while (pad--) {
        ckpt.put(VMemPool::freeByte);
    }
    // Crash if writing to the checkpoint file is failing
    if (!ckpt.good()) {
        int errno_save = errno;
	Text err = Basics::errno_Text(errno_save);
	char longid_buf[65];
	OBufStream longid_name(longid_buf, sizeof(longid_buf));
	longid_name << longid();
	Repos::dprintf(DBG_ALWAYS,
		       "VForward to LongId %s "
		       "write to checkpoint file failed: %s (errno = %d)\n",
		       longid_name.str(), err.cchars(), errno_save);
	assert(ckpt.good());  // crash
    }
    setVisited(true);
    setRedirection(newSP);  // reuse (smash) longid field
    return newSP;
}

void VForward::debugDump(std::ostream &dump) throw ()
{
  assert(VMemPool::type(this) == VMemPool::vForward);

  if(visited()) return;

  PrintBlockID(dump, VMemPool::shortenPointer(this));
  dump << endl
       << "  longid = " << *(this->longid()) << endl;

  setVisited(true);
}
