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
// VestaAttribs.C
//
// Mutable attributes for Vesta sources.
//

#include "VestaAttribs.H"
#include "VestaAttribsRep.H"
#include "CharsKey.H"
#include "VestaLog.H"
#include "VRConcurrency.H"
#include "VLogHelp.H"
#include "logging.H"
#include "DebugDumpHelp.H"

using std::endl;
using std::hex;
using std::dec;

// Invariants.  See comments in VestaSource.H for context.  The
// invariants use the notation w1 > w2 to mean that w1 sorts later in
// the history than w2.  Note: we write all operations here as if they
// had a value argument.  The value argument to clear has no effect on
// the attributes as viewed from the function level, and clients can
// provide only "" as the value for this argument; however, the
// implementation can generate non-empty strings as values via the
// set/clear replacement mentioned in invariant (3) below.
//
// 1) If K contains w1 = (o1, n, v1, t1) with o1 = clear or o1 = set,
//    then K does not contain a w2 = (o2, n, v2, t2) with w1 > w2
//    for any o2, v2, t2.
//
// 2) If K contains w1 = (o1, n, v, t1) with o1 = remove or o1 = add,
//    then K does not contain any w2 = (o2, n, v, t2) with o2 != clear
//    and w1 > w2.
//
// 3) K is a subset of H, except that if H contains wh = (set, n, v, t),
//    K might instead contain wk = (clear, n, v, t).
//
// 4) K and H are equivalent.


typedef Table<CharsKey, bool>::Default CharsTable;

VestaAttribsRep*
VestaAttribsRep::create(VestaSource::attribOp op, const char* name,
			const char* value, time_t timestamp) throw ()
{
    int nameLen = strlen(name);
    int valueLen = strlen(value);
    VestaAttribsRep* ar = (VestaAttribsRep*)
      VMemPool::allocate(VMemPool::vAttrib,
			 VATTR_MINSIZE + nameLen + valueLen);
    ar->setOp(op);
    ar->setNext(0);
    Bit32 tt = (Bit32) timestamp;
    memcpy(&ar->rep[VATTR_TIMESTAMP], &tt, 4);
    strcpy((char*) &ar->rep[VATTR_NAME], name);
    strcpy((char*) &ar->rep[VATTR_NAME + nameLen + 1], value);
    return ar;
}


Bit32
VestaAttribs::firstAttrib() throw ()
{
    if (attribs == NULL) return 0;
    Bit32 firstsp;
    memcpy(&firstsp, attribs, sizeof(Bit32));
    return firstsp;
}

void
VestaAttribs::setFirstAttrib(Bit32 newval) throw ()
{
    Bit32 firstsp;
    memcpy(attribs, &newval, sizeof(Bit32));
}

bool
VestaAttribs::inAttribs(const char* name, const char* value)
  throw (SRPC::failure /*can't really happen*/)
{
    // Examine history in reverse order
    VestaAttribsRep* cur =
      (VestaAttribsRep*) VMemPool::lengthenPointer(firstAttrib());
    while (cur) {
	assert(VMemPool::type(cur) == VMemPool::vAttrib);
	if (strcmp(cur->name(), name) == 0) {
	    switch (cur->op()) {
	      case opSet:
		if (strcmp(cur->value(), value) == 0) {
		    // This value is in the set
		    return true;
		} else {
		    // There can be no more values in the set
		    return false;
		}
	      case opClear:
		// There can be no more values in the set
		return false;
	      case opAdd:
		if (strcmp(cur->value(), value) == 0) {
		    // This value is in the set
		    return true;
		}
		break;
	      case opRemove:
		if (strcmp(cur->value(), value) == 0) {
		    // This value cannot be in the set
		    return false;
		}
		break;
	    }
	}
	cur = (VestaAttribsRep*) VMemPool::lengthenPointer(cur->next());
    }
    // Value was not found in the set
    return false;
}

const char*
VestaAttribs::getAttribConst(const char* name) throw ()
{
    VestaAttribsRep* cur =
      (VestaAttribsRep*) VMemPool::lengthenPointer(firstAttrib());
    while (cur) {
	assert(VMemPool::type(cur) == VMemPool::vAttrib);
	if (strcmp(cur->name(), name) == 0) {
	    switch (cur->op()) {
	      case opSet:
		// There is exactly one value; return it.  By (1) this 
		// opSet would not be here if the value had been
		// deleted by a later clear or set (and moreover we
		// would have exited the loop already if there had
		// been one), and by (2) this opSet would not be here
		// if the value had been deleted by a remove.
		return cur->value();
	      case opClear:
		// There are no values; return NULL.
		return NULL;
	      case opAdd:
		// This value is in the set.  By (1) this opAdd would
		// not be here if the value had been deleted by a
		// clear or set (and moreover we would have exited the
		// loop already if there had been one), and by (2)
		// this opAdd would not be here if the value had been
		// deleted by a remove.
		return cur->value();
	      case opRemove:
		// There might be some values; keep looking.
		break;
	    }
	}
	cur = (VestaAttribsRep*) VMemPool::lengthenPointer(cur->next());
    }
    // No values were found.
    return NULL;
}

char*
VestaAttribs::getAttrib(const char* name)
  throw (SRPC::failure /*can't really happen*/)
{
    const char* value = getAttribConst(name);
    if (value == NULL) return NULL;
    return strdup(value);
}

void
VestaAttribs::getAttrib(const char* name,
		       VestaAttribs::valueCallback cb, void* cl)
{
    VestaAttribsRep* cur =
      (VestaAttribsRep*) VMemPool::lengthenPointer(firstAttrib());
    bool cont = true;
    while (cur && cont) {
	assert(VMemPool::type(cur) == VMemPool::vAttrib);
	if (strcmp(cur->name(), name) == 0) {
	    switch (cur->op()) {
	      case opSet:
		// There is exactly one more value; return it and stop.
                // By (1) this opSet would not be here if the value
                // had been deleted by a later clear or set (and
		// moreover we would have exited the loop already if
		// there had been one), and by (2) this opSet would
		// not be here if the value had been deleted by a
		// remove. 
		cont = cb(cl, cur->value());
		return;
	      case opClear:
		// There are no more values; stop.
		return;
	      case opAdd:
		// This value is in the set.  By (1) this opAdd would
		// not be here if the value had been deleted by a
		// clear or set (and moreover we would have exited the
		// loop already if there had been one), and by (2)
		// this opAdd would not be here if the value had been
		// deleted by a remove.
		cont = cb(cl, cur->value());
		break;
	      case opRemove:
		// There might be some more values; keep looking.
		break;
	    }
	}
	cur = (VestaAttribsRep*) VMemPool::lengthenPointer(cur->next());
    }
}

void
VestaAttribs::listAttribs(VestaAttribs::valueCallback cb, void* cl)
{
    CharsTable listed;
    CharsKey k;
    bool v;
    VestaAttribsRep* cur =
      (VestaAttribsRep*) VMemPool::lengthenPointer(firstAttrib());
    bool cont = true;
    while (cur && cont) {
	assert(VMemPool::type(cur) == VMemPool::vAttrib);
	switch (cur->op()) {
	  case opSet:
	  case opAdd:
	    // There is something bound to this name.  By (1) and (2)
	    // this op would not be here if the value it set or added
	    // had been deleted.
	    k.s = cur->name();
	    if (!listed.Get(k, v)) {
		cont = cb(cl, k.s);
		listed.Put(k, true);
	    }
	    break;
	  case opClear:
	  case opRemove:
	    // No useful information.
	    break;
	}
	cur = (VestaAttribsRep*) VMemPool::lengthenPointer(cur->next());
    }
}

void
VestaAttribs::getAttribHistory(VestaAttribs::historyCallback cb, void* cl)
{
    VestaAttribsRep* cur =
      (VestaAttribsRep*) VMemPool::lengthenPointer(firstAttrib());
    bool cont = true;
    while (cur && cont) {
	assert(VMemPool::type(cur) == VMemPool::vAttrib);
	attribOp curop = cur->op();
	cont = cb(cl, curop, cur->name(), cur->value(), cur->timestamp());
	cur = (VestaAttribsRep*) VMemPool::lengthenPointer(cur->next());
    }
}

static int
compareWrite(VestaAttribs::attribOp o1,
	     const char* n1, const char* v1, time_t t1, 
	     VestaAttribs::attribOp o2,
	     const char* n2, const char* v2, time_t t2)
{
    if (t1 > t2) return 1;
    if (t1 < t2) return -1;
    int nc = strcmp(n1, n2);
    if (nc != 0) return nc;
    nc = strcmp(v1, v2);
    if (nc != 0) return nc;
    // Compare o1, o2 last, so that different ops with same arguments
    //  are adjacent in order.
    if (((int) o1) > ((int) o2)) return 1;
    if (((int) o1) < ((int) o2)) return -1;
    return 0;
}    

// Come up with a timestamp for a new attribute history record such
// that it won't be shadowes by an existing record.
static time_t new_timestamp(VestaAttribsRep* first,
			    VestaAttribs::attribOp op,
			    const char* name, const char* value)
{
  time_t result = time(NULL);
  if(first == 0) return result;
  if(compareWrite(op, name, value, result,
		  first->op(), first->name(),
		  first->value(), first->timestamp()) < 1)
    {
      // This would sort after the first history record, so we take
      // that timestamp and add one.
      result = first->timestamp() + 1;
    }
  return result;
}

//
// This version of the method does the real work, but does not
// do access checking, setuid/getuid magic, or logging
//
VestaSource::errorCode
VestaAttribs::writeAttrib(VestaAttribs::attribOp op, const char* name,
			  const char* value, time_t &timestamp) throw ()
{
    if (!hasAttribs()) return VestaSource::invalidArgs;

    VestaAttribsRep* prev = NULL;
    VestaAttribsRep* cur =
      (VestaAttribsRep*) VMemPool::lengthenPointer(firstAttrib());

    if (timestamp == 0) {
      timestamp = new_timestamp(cur, op, name, value);
    }

    while (cur != NULL) {
	assert(VMemPool::type(cur) == VMemPool::vAttrib);
	int cw = compareWrite(op, name, value, timestamp,
			      cur->op(), cur->name(),
			      cur->value(), cur->timestamp());
	if (cw > 0) {
	    break;
	} else if (cw == 0) {
	    // Exact duplicate write; has no effect
	    return VestaSource::nameInUse;
	}
	// Preserve invariant
	if (strcmp(name, cur->name()) == 0) {
	    switch (cur->op()) {
	      case opSet:
	      case opClear:
		// Invariant says this op should be discarded
		return VestaSource::ok;
	      case opAdd:
	      case opRemove:
		if (strcmp(value, cur->value()) == 0) {
		    switch (op) {
		      case opAdd:
		      case opRemove:
			// Invariant says this op should be discarded
			return VestaSource::ok;
		      case opSet:
			// Change the new opSet to a opClear.  This
			// cannot cause us to put the operation in the
			// wrong order in the list, because the change
			// makes the operation compare (epsilon)
			// smaller, so at worst its place could be
			// further down the list than we have looked yet. 
			op = opClear;
			break;
		      case opClear:
			break;
		    }
		}
		break;
	    }
	}
	prev = cur;
	cur = (VestaAttribsRep*) VMemPool::lengthenPointer(cur->next());
    }

    // Insert into chain.
    //   prev is the predecessor, if any.
    //   cur is the successor, if any.
    VestaAttribsRep* wr =
      VestaAttribsRep::create(op, name, value, timestamp);
    if (prev == NULL) {
	wr->setNext(firstAttrib());
	setFirstAttrib(VMemPool::shortenPointer(wr));
    } else {
	wr->setNext(prev->next());
	prev->setNext(VMemPool::shortenPointer(wr));
    }

    // Restore invariant
    prev = wr;
    if (op == opSet || op == opClear) {
	while (cur != NULL) {
	    assert(VMemPool::type(cur) == VMemPool::vAttrib);
	    if (strcmp(name, cur->name()) == 0) {
		// Remove cur
		prev->setNext(cur->next());
		attribOp curop = cur->op();
		VMemPool::free(cur, VATTR_MINSIZE + strlen(cur->name())
			       + strlen(cur->value()),
			       VMemPool::vAttrib);
		cur = prev;
		
		switch (curop) {
		  case opAdd:
		  case opRemove:
		    break;
		  case opSet:
		  case opClear:
		    return VestaSource::ok;
		}
	    }
	    prev = cur;
	    cur = (VestaAttribsRep*) VMemPool::lengthenPointer(cur->next());
	}
    } else {
	while (cur != NULL) {
	    assert(VMemPool::type(cur) == VMemPool::vAttrib);
	    if (strcmp(name, cur->name()) == 0) {
		switch (cur->op()) {
		  case opAdd:
		  case opRemove:
		    if (strcmp(cur->value(), value) == 0) {
			// Remove cur
			prev->setNext(cur->next());
			VMemPool::free(cur, VATTR_MINSIZE + strlen(cur->name())
				       + strlen(cur->value()),
				       VMemPool::vAttrib);
			// Can't be any more ops on this (name, value)
			return VestaSource::ok;
		    }
		    break;
		  case opSet:
		    if (strcmp(cur->value(), value) == 0) {

			// Change the opSet to a opClear.  This can't
                        // change the ordering of the operation with
			// respect to the rest of the history,
			// because the old and new operations sort
			// immediately adjacent to each other, and it
			// can't create a duplicate operation in the
			// history, because if there is an opSet for a
			// name, the invariants guarantee there can't
			// be an opClear for the same name.

			cur->setOp(opClear);
		    }
		    // There can't be any more ops on this name
		    return VestaSource::ok;
		  case opClear:
		    // There can't be any more ops on this name
		    return VestaSource::ok;
		}
	    }
	    prev = cur;
	    cur = (VestaAttribsRep*) VMemPool::lengthenPointer(cur->next());
	}
    }
    return VestaSource::ok;
}

// Return true if writing this attrib tuple would change K,
// but do not actually write it.  Mostly code cribbed from writeAttrib.
bool
VestaAttribs::wouldWriteAttrib(VestaAttribs::attribOp op, const char* name,
			 const char* value, time_t &timestamp) throw ()
{
    if (!hasAttribs()) return false;

    VestaAttribsRep* prev = NULL;
    VestaAttribsRep* cur =
      (VestaAttribsRep*) VMemPool::lengthenPointer(firstAttrib());

    if (timestamp == 0) {
	timestamp = new_timestamp(cur, op, name, value);
    }

    while (cur != NULL) {
	assert(VMemPool::type(cur) == VMemPool::vAttrib);
	int cw = compareWrite(op, name, value, timestamp,
			      cur->op(), cur->name(),
			      cur->value(), cur->timestamp());
	if (cw > 0) {
	    break;
	} else if (cw == 0) {
	    // Exact duplicate write; has no effect
	    return false;
	}
	// Preserve invariant
	if (strcmp(name, cur->name()) == 0) {
	    switch (cur->op()) {
	      case opSet:
	      case opClear:
		// Invariant says this op should be discarded
		return false;
	      case opAdd:
	      case opRemove:
		if (strcmp(value, cur->value()) == 0) {
		    switch (op) {
		      case opAdd:
		      case opRemove:
			// Invariant says this op should be discarded
			return false;
		      case opSet:
			// Change the new opSet to a opClear.  This
			// cannot cause us to put the operation in the
			// wrong order in the list, because the change
			// makes the operation compare (epsilon)
			// smaller, so at worst its place could be
			// further down the list than we have looked yet. 
			op = opClear;
			break;
		      case opClear:
			break;
		    }
		}
		break;
	    }
	}
	prev = cur;
	cur = (VestaAttribsRep*) VMemPool::lengthenPointer(cur->next());
    }
    return true;
}

Bit32
VestaAttribs::copyAttribs(Bit32 from) throw ()
{
    if (from == 0) return 0;
    VestaAttribsRep* fromp =
      (VestaAttribsRep*) VMemPool::lengthenPointer(from);
    assert(VMemPool::type(fromp) == VMemPool::vAttrib);
    VestaAttribsRep* first =
      VestaAttribsRep::create(fromp->op(), fromp->name(),
			      fromp->value(), fromp->timestamp());
    VestaAttribsRep* prev = first;
    VestaAttribsRep* cur;

    from = fromp->next();
    while (from != 0) {
	fromp = (VestaAttribsRep*) VMemPool::lengthenPointer(from);
	assert(VMemPool::type(fromp) == VMemPool::vAttrib);
	cur = VestaAttribsRep::create(fromp->op(), fromp->name(),
				      fromp->value(), fromp->timestamp());
	prev->setNext(VMemPool::shortenPointer(cur));
	prev = cur;
	from = fromp->next();
    }
    return VMemPool::shortenPointer(first);
}


void
VestaAttribsRep::mark() throw ()
{
    VestaAttribsRep* ar = this;
    while (ar != NULL) {
	assert(VMemPool::type(ar) == VMemPool::vAttrib);
	ar->setVisited(true);
	ar = (VestaAttribsRep*) VMemPool::lengthenPointer(ar->next());
    }
}


void
VestaAttribsRep::markCallback(void* closure, VMemPool::typeCode type)
  throw ()
{
    // Most attribs get marked when the directory entry they are in
    // is marked, but the attribs for the root directories
    // are not in a directory entry.
    VestaAttribsRep* ar;
    ar = (VestaAttribsRep*)
      VMemPool::lengthenPointer(VestaSource::repositoryRoot()->firstAttrib());
    if (ar) ar->mark();
    ar = (VestaAttribsRep*)
      VMemPool::lengthenPointer(VestaSource::mutableRoot()->firstAttrib());
    if (ar) ar->mark();
    ar = (VestaAttribsRep*)
      VMemPool::lengthenPointer(VestaSource::volatileRoot()->firstAttrib());
    if (ar) ar->mark();
}


bool
VestaAttribsRep::sweepCallback(void* closure, VMemPool::typeCode type,
			       void* addr, Bit32& size) throw ()
{
    VestaAttribsRep* ar = (VestaAttribsRep*) addr;
    int namelen = strlen(ar->name());
    size = VATTR_MINSIZE + namelen + strlen(ar->value(namelen));
    bool ret = ar->visited();
    ar->setVisited(false);
    return ret;
}

void
VestaAttribsRep::rebuildCallback(void* closure, VMemPool::typeCode type,
				 void* addr, Bit32& size) throw ()
{
    // Just return the size; nothing else to do
    VestaAttribsRep* ar = (VestaAttribsRep*) addr;
    int namelen = strlen(ar->name());
    size = VATTR_MINSIZE + namelen + strlen(ar->value(namelen));
}

Bit32
VestaAttribsRep::checkpoint(Bit32& nextSP, std::fstream& ckpt) throw ()
{
    if (visited()) return next(); // reused field
    // Do iteratively to avoid deep recursion
    VestaAttribsRep *ar = this, *ar_next;
    while (ar != NULL) {
	assert(VMemPool::type(ar) == VMemPool::vAttrib);
	int namelen = strlen(ar->name());
	int size = VATTR_MINSIZE + namelen + strlen(ar->value(namelen));
	int pad = ((-size) & VMemPool::alignmentMask);
	Bit32 newSP = nextSP;
	nextSP += size + pad;
	Bit32 oldNextSP = ar->next();
	if (oldNextSP != 0)
	  {
	    ar_next =
	      (VestaAttribsRep*) VMemPool::lengthenPointer(oldNextSP);
	    // We now need to set ar's next pointer to the short
	    // pointer of the next enry in the checkpoint.
	    if(ar_next->visited())
	      {
		// The next VestaAttribsRep has already been
		// checkpointed, so its next pointer is its new short
		// pointer.
		Bit32 next_new_SP = ar_next->next();
		ar->setNext(next_new_SP);
		Repos::dprintf(DBG_ALWAYS,
			       "WARNING: multiply referenced attribute chain; "
			       "post-checkpoint short pointer = 0x%x\n",
			       next_new_SP);
		// Since the next entry has already been checkpointed,
		// this will be the last loop iteration.
		ar_next = 0;
	      }
	    else
	      // We'll be checkpointing the next VestaAttribsRep right
	      // after this one, so we know the next short pointer.
	      ar->setNext(nextSP);
	  }
	else
	  ar_next = 0;

	ckpt.write((char *) ar->rep, size);
	while (pad--) {
	  ckpt.put(VMemPool::freeByte);
	}
	ar->setVisited(true);
	ar->setNext(newSP);  // reuse (smash) this field
	ar = ar_next;
    }
    return next();
}

void VestaAttribsRep::debugDump(std::ostream &dump) throw ()
{
  VestaAttribsRep *ar = this;
  while (ar != NULL) {
    if (ar->visited()) return;

    assert(VMemPool::type(ar) == VMemPool::vAttrib);
    
    PrintBlockID(dump, VMemPool::shortenPointer(ar));
    dump << endl
	 << "  op = " << VestaSource::attribOpString(ar->op()) << endl
	 << "  timestamp = " << ((unsigned int) ar->timestamp()) << endl
	 << "  name = \"" << ar->name() << "\"" << endl
	 << "  value = \"" << ar->value() << "\"" << endl;
    if(ar->next() != 0)
      {
	dump << "  next = ";
	PrintBlockID(dump, ar->next());
	dump << endl;
      }

    ar->setVisited(true);

    ar = (VestaAttribsRep*) VMemPool::lengthenPointer(ar->next());
  }
}
