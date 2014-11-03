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
// VestaAttribsRep.H
//
// Mutable attributes for Vesta sources.
//

#ifndef _VATTRR
#define _VATTRR 1

#include "Basics.H"
#include "VMemPool.H"
#include "VestaSource.H"


// Packed representation of a VestaAttribsRep, using the
// pseudo-C++ notation described in VDirChangeable.H.
//
// packed struct VestaAttribsRep {
//     Bit8 spare: 1;
//     Bit8 visited: 1;          // bool, used by GC
//     Bit8 op: 2;               // VestaSource::attribOp
//     Bit8 typecode: 4;         // Must equal VMemPool::vAttrib
//     VestaAttribsRep@ next;
//     Bit32 timestamp;          // time of this change; used in replication
//     char name[];              // NUL-terminated
//     char value[];             // NUL-terminated
// };

// Some numbers derived from above
#define VATTR_FLAGS 0
#define VATTR_NEXT 1
#define VATTR_TIMESTAMP 5
#define VATTR_NAME 9
#define VATTR_MINSIZE 11         // if name = value = ""

class VestaAttribsRep {
  public:
    Bit8 rep[VATTR_MINSIZE]; // plus strlen(name) + strlen(value)
    inline bool visited() throw()
      { return (bool) ((rep[0] & 2) != 0); };
    inline void setVisited(bool newval) throw()
      { rep[0] = (rep[0] & 0xfd) | ((int)newval << 1); };
    inline VestaSource::attribOp op() throw()
      { return (VestaSource::attribOp) ((rep[VATTR_FLAGS] >> 2) & 3); };
    inline void setOp(VestaSource::attribOp op) throw()
      { rep[VATTR_FLAGS] = (rep[VATTR_FLAGS] & 0xf3) | (((int) op) << 2); };
    inline Bit32 next() throw()
      { Bit32 val; memcpy(&val, &rep[VATTR_NEXT], 4); return val; };
    inline void setNext(Bit32 newval) throw()
      { memcpy(&rep[VATTR_NEXT], &newval, 4); };
    inline time_t timestamp() throw()
      { Bit32 val; memcpy(&val, &rep[VATTR_TIMESTAMP], 4);
	return (time_t) val; };
    inline const char* name() throw()
      { return (const char*) &rep[VATTR_NAME]; };
    inline const char* value(int namelen) throw() // if you know strlen(name)
      { return (const char*) &rep[VATTR_NAME + namelen + 1]; };
    inline const char* value() throw() // if you don't know strlen(name)
      { return (const char*) &rep[VATTR_NAME + strlen(name()) + 1]; };

    // Return a pointer to a new VestaAttribsRep allocated in
    // the VMemPool.
    static VestaAttribsRep*
      create(VestaSource::attribOp op, const char* name,
	     const char* value, time_t timestamp) throw();

    // VMemPool GC support
    void mark() throw();
    static void markCallback(void* closure, VMemPool::typeCode type) throw();
    static bool sweepCallback(void* closure, VMemPool::typeCode type,
			      void* addr, Bit32& size) throw();
    static void rebuildCallback(void* closure, VMemPool::typeCode type,
				void* addr, Bit32& size) throw();

    // Checkpoint support
  Bit32 checkpoint(Bit32& nextSP, std::fstream& ckpt) throw();

  void debugDump(std::ostream &dump) throw ();
};

#endif //_VATTRR
