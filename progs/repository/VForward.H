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
// VForward.H
//
// A forwarding pointer for a renamed object, so that its old LongId
// still works.

#ifndef _VFWD
#define _VFWD 1

// Below we describe the packed representation of a VForward.  See
// VDirChangeable.H for a description of the pseudo-C++ notation.
//
// packed struct VForward {
//     Bit8 spare1: 1;
//     Bit8 visited: 1;               // bool, used by GC
//     Bit8 spare2: 2;
//     Bit8 typecode: 4;              // Must be VMemPool::vForward, used by GC
//     LongId longid;
// };

struct VForward {
    Bit8 rep[1 + sizeof(LongId)];

    inline bool visited() throw()
      { return (bool) ((rep[0] & 2) != 0); };
    inline void setVisited(bool newval) throw()
      { rep[0] = (rep[0] & 0xfd) | ((int)newval << 1); };

    inline LongId* longid() throw() { return (LongId*) &rep[1]; };

  private:
    // Warning: these methods reuse (smash) part of the longid
    inline Bit32 redirection() throw()
      { Bit32 ret; memcpy(&ret, &rep[1], sizeof(Bit32)); return ret; };
    inline void setRedirection(Bit32 val) throw()
      { memcpy(&rep[1], &val, sizeof(Bit32)); };

  public:
    // Return a short pointer to a new VForward allocated in the VMemPool
    static Bit32 create(const LongId& longid) throw();

    // Garbage collection support
    static void markCallback(void* closure, VMemPool::typeCode type) throw();
    static bool sweepCallback(void* closure, VMemPool::typeCode type,
			      void* addr, Bit32& size) throw();
    static void rebuildCallback(void* closure, VMemPool::typeCode type,
				void* addr, Bit32& size) throw();

    // Checkpointing
    Bit32 checkpoint(Bit32& nextSP, std::fstream& ckpt) throw();

    void debugDump(std::ostream &dump) throw ();
};

#endif // _VFWD
