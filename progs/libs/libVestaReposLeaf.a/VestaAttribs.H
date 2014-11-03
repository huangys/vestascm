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
// VestaAttribs.H
//
// Mutable attributes for Vesta sources.
//

#ifndef _VATTR
#define _VATTR 1

#include "Basics.H"
#include "VRErrorCode.H"
#include "SRPC.H"

class VestaAttribs {

// A VestaSource has mutable attributes if it or its parent directory
// is appendable or mutable.

// At the highest level of abstraction (the "function" level), the
// attributes of a Vesta source are a total function F from strings
// (names) to sets of strings (values).  Initially every string is
// bound to the empty set of values.  Operations are available to read
// and write the current state.  Abstractly, there is just one read
// operation, which returns F.  The write operations are:
//
//  set(name, value)       // F(name) = {value}
//  clear(name)            // F(name) = {}
//  add(name, value)       // F(name) = F(name) u {value}
//  remove(name, value)    // F(name) = F(name) - {value}
//

// At a lower level of abstraction (the "history" level), the write
// operations include a timestamp argument in addition to the
// arguments listed above, and the abstract state is the set H of
// all write operations that have been requested, called the
// history.  

// The operation to read F remains.  This F(H) is computed by sorting
// the history H into timestamp order, with ties broken by taking the
// operation, name, and value as secondary sort keys, and applying the
// resulting sequence of operations to the empty function.

// Also provided is an operation to return a history, but this
// operation does not necessarily return H.  Rather, it returns some
// history K that is *equivalent* to H, in the following sense.
// Histories H and K are equivalent if for any history L, 
// F(H u L) = F(K u L).  That is, K might as well have been the real
// history, because one cannot tell the difference by observing the
// present and future behavior of F.

// Note:  As you might guess, the implementation does not really store
// H, but rather stores an equivalent history K that is smaller, and
// it is this history that is returned by the history operation.
// There are some additional invariants on K that are important only
// inside the implementation, and are described in the .C file.

// The history level is present to support partial replication.  If
// operations are done independently to two objects (in particular,
// two objects at different sites that are considered partial replicas
// of one another), the changes can later be merged with well-defined
// results.

  public:
    enum attribOp { opSet = 0, opClear, opAdd, opRemove };
      // Warning: the implementation relies on the ordering of this type, 
      //  and the log relies on the integer values.  Do not change it.

    // The following function returns a pointer to static storage
    static const char* attribOpString(attribOp op) throw ();
    // (s)et, (c)lear, (a)dd, (r)emove:
    static char attribOpChar(attribOp op) throw ();

    
    typedef bool (*valueCallback)(void* closure, const char* value);
    typedef bool (*historyCallback)(void* closure, attribOp op,
				    const char* name, const char* value,
				    time_t timestamp);

    // attribs is actually a VestaAttribsRep@*, unaligned.
    Bit8* attribs;

    inline bool hasAttribs() throw () { return attribs != NULL; }

    // Methods to read the current state of the attributes.

    // Is value a member of the set bound to name?  Always returns
    // false if this source does not have attributes.
    virtual bool inAttribs(const char* name, const char* value)
       throw (SRPC::failure);

    // If name is bound to {} (or this source does not have
    // attributes), return NULL.  If name is bound to the singleton
    // set {value}, return value.  If name is bound to a set
    // containing more than one element, return some value in the set;
    // it is unspecified which value is returned.  The caller is
    // responsible for freeing the storage returned by getAttrib.
    // getAttribConst (available only within the repository server
    // address space) avoids allocating storage, but the return value
    // is guaranteed valid only as long as the caller holds the
    // repository read lock.
    virtual char* getAttrib(const char* name) throw (SRPC::failure);
    const char* getAttribConst(const char* name) throw (); //private

    // Call cb once for each element in the value set bound to name.
    // If the callback returns true, the listing is continued; if not,
    // it is stopped.  If this source does not have attributes, returns
    // the empty set.
    virtual void getAttrib(const char* name, valueCallback cb, void* cl)
      /*throw (SRPC::failure or anything thrown by the callback)*/;

    // Call cb once for each name that is bound to a nonempty value
    // set.  If the callback returns true, the listing is continued;
    // if not, it is stopped.  If this source does not have
    // attributes, returns the empty set.
    virtual void listAttribs(valueCallback cb, void* cl)
      /*throw (SRPC::failure or anything thrown by the callback)*/;

    // Lower-level method to read the recorded history K.  Calls cb
    // once for each write in the recorded history.  If the callback
    // returns true, the listing is continued; if not, it is stopped.
    // Write operations are returned in an unspecified order.  If this
    // source does not have attributes, returns the empty set.
    virtual void getAttribHistory(historyCallback cb, void* cl)
      /*throw (SRPC::failure or anything thrown by the callback)*/;

    // Write method.  Ordinary clients should default the timestamp
    // argument; that is, they should view the attributes at the
    // function level of abstraction.  (If the timestamp is zero, the
    // current time is used, possibly adding a small increment to keep
    // this change from sorting after existing attribute history
    // records.  If the timestamp is defaulted, timestamp will be
    // updated with the time which was actually used.)
    //
    // opSet: Bind name to the singleton set {value}.  Equivalent to clear
    //   followed by add, but atomic even in the presence of other
    //   changes with the same timestamp.
    // opClear: Bind name to the empty set; i.e., unbind it.
    //   Client should pass "" as value.
    // opAdd: Add value to the set that is bound to name.
    // opRemove: Remove value from the set that is bound to name, if present.
    //
    // (Note: May return nameInUse if the write has no effect.  This
    // is not an error, it just tells the caller not to log this
    // operation as a change.)
    VRErrorCode::errorCode
    writeAttrib(attribOp op, const char* name,
		const char* value, /*INOUT*/ time_t &timestamp) throw ();

    // Low-level routines that work only inside the repository
    // address space:

    // Test whether calling writeAttrib with the given arguments would
    // change K, but don't actually change it.  (As with writeAttrib,
    // if timestamp os zero it will be defaulted and updated with the
    // time used.)
    bool wouldWriteAttrib(attribOp op, const char* name,
			  const char* value,
			  /*INOUT*/ time_t &timestamp) throw ();

    // Return a short pointer to the first VestaAttribsRep
    Bit32 firstAttrib() throw ();
    // Set the first VestaAttribsRep
    void setFirstAttrib(Bit32 newval) throw ();
    // Make a copy of the attribs of an object.  Accepts and returns
    // a VestaAttribsRep@.
    static Bit32 copyAttribs(Bit32 from) throw();
};

#endif //_VATTR
