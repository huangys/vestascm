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
// VDirEvaluator.H
//
// Directory of type evaluatorDirectory.  The contents of such a
// directory are a binding held by the evalutor.  The methods of this
// object type make remote calls to the evalutor.
//

#ifndef _VDIREV
#define _VDIREV 1

#include "VestaSource.H"
#include "MultiSRPC.H"
#include "VMemPool.H"

struct EvalDirInfo; // defined in VDirEvaluator.C

// Below we describe the packed representation of a VDirEvaluatorRep.
// See VDirChangeable.H for a description of the pseudo-C++ notation.
//
// packed struct VDirEvaluatorRep {
//     Bit8 hasName: 1;               // bool, used by GC
//     Bit8 visited: 1;               // bool, used by GC
//     Bit8 spare: 2;
//     Bit8 typecode: 4;              // Must be VMemPool::vDirEvaluator
//     struct EvalDirInfo* edi        // All real content here
// };

#define VDIREV_FLAGS 0
#define VDIREV_EDI 1
#define VDIREV_SIZE (VDIREV_EDI+sizeof(EvalDirInfo*))

class VDirEvaluator : public VestaSource {
    EvalDirInfo* edi;
  public:
    // Our versions of virtual methods from base class
    // There is no access checking on evaluator directories; the who
    //  arguments are all ignored.
    VestaSource::errorCode lookup(Arc arc, VestaSource*& result,
	         AccessControl::Identity who =NULL,
		 unsigned int indexOffset =0) throw();
    VestaSource::errorCode
      lookupIndex(unsigned int index, VestaSource*& result,
		 char* arcbuf =NULL) throw();
    VestaSource::errorCode
      list(unsigned int firstIndex,
	   VestaSource::listCallback callback, void* closure,
	   AccessControl::Identity who =NULL, bool deltaOnly =false, 
           unsigned int indexOffset =0) throw();

    // Construct a new VDirEvaluatorRep and a VDirEvaluator to
    // reference it.  Uses the current time if timestamp = 0.
    VDirEvaluator(VestaSource::typeTag type,
		  const char* hostname, const char* port,
		  Bit64 dirHandle, bool* alive, time_t timestamp =0) throw();
    // Construct a VDirEvaluator to point to an existing rep.
    VDirEvaluator(VestaSource::typeTag type, Bit8* rep) throw();
    VDirEvaluator(VestaSource::typeTag type, EvalDirInfo* edi) throw();

    // Garbage collection support
    void mark(bool byName =true, ArcTable* hidden =NULL) throw();
    static void markCallback(void* closure, VMemPool::typeCode type) throw();
    static bool sweepCallback(void* closure, VMemPool::typeCode type,
			      void* addr, Bit32& size) throw();
    static void rebuildCallback(void* closure, VMemPool::typeCode type,
				void* addr, Bit32& size) throw();

    // Checkpointing.  VDirEvaluator objects have to be written to
    // the checkpoint so that we can continue after checkpointing by
    // reading the checkpoint back into memory.  They are not needed
    // for cold start recovery because they are considered volatile.
    Bit32 checkpoint(Bit32& nextSP, std::fstream& ckpt) throw();

    // Explicit free support
    void freeTree() throw();

    // Check whether the evaluator is alive
    bool alive();

    // Evaluator is known to be dead; purge cached connections
    void purge();

    time_t timestamp() throw();

  void getClientHostPort(/*OUT*/ Text &hostname, /*OUT*/ Text &port)
    const throw();

    // Accessors for the packed representation
  public:
    inline bool hasName() throw()
      { return (bool) ((rep[VDIREV_FLAGS] & 1) != 0); };
    inline void setHasName(bool newval) throw()
      { rep[VDIREV_FLAGS] = (rep[VDIREV_FLAGS] & 0xfe) | (int)newval; };
    inline bool visited() throw()
      { return (bool) ((rep[VDIREV_FLAGS] & 2) != 0); };
    inline void setVisited(bool newval) throw()
      { rep[VDIREV_FLAGS] = (rep[VDIREV_FLAGS] & 0xfd) | ((int)newval << 1); };
  private:
    inline EvalDirInfo* repEDI() throw()
      { EvalDirInfo* ret;
        memcpy(&ret, &rep[VDIREV_EDI], sizeof(EvalDirInfo*)); return ret; };
    inline void setRepEDI(EvalDirInfo* repEDI) throw()
      { memcpy(&rep[VDIREV_EDI], &repEDI, sizeof(EvalDirInfo*)); };
};

#endif //_VDIREV
