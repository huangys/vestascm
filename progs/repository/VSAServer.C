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
// VSAServer.C
//
// Server side of VestaSourceAtomic
//

#include <BufStream.H>
#include "SRPC.H"
#include "MultiSRPC.H"
#include "VestaSource.H"
#include "VestaSourceAtomic.H"
#include "VestaSourceImpl.H"
#include "VestaSourceSRPC.H"
#include "VRConcurrency.H"
#include "logging.H"
#include "Mastership.H"

#include "lock_timing.H"

using Basics::OBufStream;

class VSAStep; //forward

struct VSAProgramState {
  int pc;
  VSAStep* first;
  VSAStep* last;
  VestaSource::errorCode target1, target2;
  VestaSource::errorCode okreplace;
  Sequence<VestaSource*> vss;
  ReadersWritersLock* lock;
  AccessControl::Identity who;
  time_t now;
  SRPC* srpc;
  int intf_ver;
};

class VSAStep {
 public:
  VSAStep* next;
  // Returns true if successful
  virtual VestaSource::errorCode
    execute(VSAProgramState* state) = 0;
  // Virtual destructor to ensure subclass destructors get called.
  virtual ~VSAStep() { }
};

class SetTargetStep : public VSAStep {
  VestaSource::errorCode target1;
  VestaSource::errorCode target2;
  VestaSource::errorCode okreplace;
 public:
  SetTargetStep(VSAProgramState* state) {
    next = NULL;
    target1 = (VestaSource::errorCode) state->srpc->recv_int();
    target2 = (VestaSource::errorCode) state->srpc->recv_int();
    okreplace = (VestaSource::errorCode) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    state->target1 = target1;
    state->target2 = target2;
    state->okreplace = okreplace;
    return target1; // trick to get this step to always succeed
  };
};

class DeclStep : public VSAStep {
  LongId longid;
 public:
  DeclStep(VSAProgramState* state) {
    next = NULL;
    int len = sizeof(longid.value);
    state->srpc->recv_bytes_here((char *) &longid.value, len);
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    VestaSource* vs = longid.lookup(LongId::checkLock, &state->lock);
    state->vss.addhi(vs);
    if (vs == NULL) {
      return VestaSource::notFound;
    } else {
      return VestaSource::ok;
    }
  };
};

class ResyncStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
 public:
  ResyncStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == NULL) return VestaSource::invalidArgs;
    vs->resync();
    return VestaSource::ok;
  };
};

class SetTimestampStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  time_t ts;
 public:
  SetTimestampStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    ts = (time_t) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == NULL) return VestaSource::invalidArgs;
    return vs->setTimestamp(ts, state->who);
  };
};

class LookupStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  char arc[MAX_ARC_LEN+1];
 public:
  LookupStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    int len = MAX_ARC_LEN+1;
    state->srpc->recv_chars_here(arc, len);
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    int result_idx = state->vss.size();
    state->vss.addhi(0);
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == 0) return VestaSource::invalidArgs;
    VestaSource* result = 0;
    VestaSource::errorCode ret = vs->lookup(arc, result, state->who);
    state->vss.put(result_idx, result);
    return ret;
  };
};

class LookupPathnameStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  char* pathname;
  char pathnameSep;
 public:
  LookupPathnameStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    pathname = state->srpc->recv_chars();
    pathnameSep = (char) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    int result_idx = state->vss.size();
    state->vss.addhi(0);
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == 0) return VestaSource::invalidArgs;
    VestaSource* result = 0;
    VestaSource::errorCode ret =
      vs->lookupPathname(pathname, result, state->who);
    state->vss.put(result_idx, result);
    return ret;
  };
  ~LookupPathnameStep() {
    delete[] pathname;
  };
};

class LookupIndexStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  unsigned int index;
 public:
  LookupIndexStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    index = state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    int result_idx = state->vss.size();
    state->vss.addhi(0);
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == 0) return VestaSource::invalidArgs;
    VestaSource* result = 0;
    VestaSource::errorCode ret =
      vs->lookupIndex(index, result);
    state->vss.put(result_idx, result);
    return ret;
  };
};

class ReallyDeleteStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  char arc[MAX_ARC_LEN+1];
  bool existCheck;
  time_t timestamp;
 public:
  ReallyDeleteStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    int len = MAX_ARC_LEN+1;
    state->srpc->recv_chars_here(arc, len);
    existCheck = (bool) state->srpc->recv_int();
    timestamp = (time_t) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == NULL) return VestaSource::invalidArgs;
    VestaSource::errorCode ret =
      vs->reallyDelete(arc, state->who, existCheck,
		       timestamp ? timestamp : state->now);
    return ret;
  };
};

class InsertFileStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  char arc[MAX_ARC_LEN+1];
  ShortId sid;
  bool master;
  VestaSource::dupeCheck chk;
  time_t timestamp;
  FP::Tag fptag;
  bool fptagPresent;
 public:
  InsertFileStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    int len = MAX_ARC_LEN+1;
    state->srpc->recv_chars_here(arc, len);
    sid = (ShortId) state->srpc->recv_int();
    master = (bool) state->srpc->recv_int();
    chk = (VestaSource::dupeCheck) state->srpc->recv_int();
    timestamp = (time_t) state->srpc->recv_int();
    len = FP::ByteCnt;
    unsigned char fpbytes[FP::ByteCnt];
    state->srpc->recv_bytes_here((char*)fpbytes, len);
    fptagPresent = (len != 0);
    if(fptagPresent)
      {
	fptag.FromBytes(fpbytes);
      }
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    int result_idx = state->vss.size();
    state->vss.addhi(0);
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == 0) return VestaSource::invalidArgs;
    VestaSource* result = 0;
    VestaSource::errorCode ret =
      vs->insertFile(arc, sid, master, state->who,
		     chk, &result, timestamp ? timestamp : state->now,
		     fptagPresent ? &fptag : NULL);
    state->vss.put(result_idx, result);
    return ret;
  };
};

class InsertMutableFileStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  char arc[MAX_ARC_LEN+1];
  ShortId sid;
  bool master;
  VestaSource::dupeCheck chk;
  time_t timestamp;
 public:
  InsertMutableFileStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    int len = MAX_ARC_LEN+1;
    state->srpc->recv_chars_here(arc, len);
    sid = (ShortId) state->srpc->recv_int();
    master = (bool) state->srpc->recv_int();
    chk = (VestaSource::dupeCheck) state->srpc->recv_int();
    timestamp = (time_t) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    int result_idx = state->vss.size();
    state->vss.addhi(0);
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == 0) return VestaSource::invalidArgs;
    VestaSource* result = 0;
    VestaSource::errorCode ret =
      vs->insertMutableFile(arc, sid, master, state->who, chk,
			    &result, timestamp ? timestamp : state->now);
    state->vss.put(result_idx, result);
    return ret;
  };
};

class InsertImmutableDirectoryStep : public VSAStep {
  VestaSourceAtomic::VSIndex parenti;
  char arc[MAX_ARC_LEN+1];
  VestaSourceAtomic::VSIndex diri;
  bool master;
  VestaSource::dupeCheck chk;
  time_t timestamp;
  FP::Tag fptag;
  bool fptagPresent;
 public:
  InsertImmutableDirectoryStep(VSAProgramState* state) {
    next = NULL;
    parenti = state->srpc->recv_int();
    int len = MAX_ARC_LEN+1;
    state->srpc->recv_chars_here(arc, len);
    diri = state->srpc->recv_int();
    master = (bool) state->srpc->recv_int();
    chk = (VestaSource::dupeCheck) state->srpc->recv_int();
    timestamp = (time_t) state->srpc->recv_int();
    len = FP::ByteCnt;
    unsigned char fpbytes[FP::ByteCnt];
    state->srpc->recv_bytes_here((char*)fpbytes, len);
    fptagPresent = (len != 0);
    if(fptagPresent)
      {
	fptag.FromBytes(fpbytes);
      }
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    int result_idx = state->vss.size();
    state->vss.addhi(0);
    if (parenti < 0 || parenti >= state->vss.size() ||
	diri < -1 || diri >= state->vss.size())
      return VestaSource::invalidArgs;
    VestaSource* parent = state->vss.get(parenti);
    if (parent == 0) return VestaSource::invalidArgs;
    VestaSource* dir = 0;
    if (diri != -1) dir = state->vss.get(diri);
    VestaSource* result = 0;
    VestaSource::errorCode ret =
      parent->insertImmutableDirectory(arc, dir, master, state->who,
				       chk, &result,
				       timestamp ? timestamp : state->now,
				       fptagPresent ? &fptag : NULL);
    state->vss.put(result_idx, result);
    return ret;
  };
};

class InsertAppendableDirectoryStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  char arc[MAX_ARC_LEN+1];
  bool master;
  VestaSource::dupeCheck chk;
  time_t timestamp;
 public:
  InsertAppendableDirectoryStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    int len = MAX_ARC_LEN+1;
    state->srpc->recv_chars_here(arc, len);
    master = (bool) state->srpc->recv_int();
    chk = (VestaSource::dupeCheck) state->srpc->recv_int();
    timestamp = (time_t) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    int result_idx = state->vss.size();
    state->vss.addhi(0);
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == 0) return VestaSource::invalidArgs;
    VestaSource* result = 0;
    VestaSource::errorCode ret =
      vs->insertAppendableDirectory(arc, master, state->who,
				    chk, &result,
				    timestamp ? timestamp : state->now);
    if((ret == VestaSource::ok) && master && !vs->master &&
       !myMasterHint.Empty())
      {
	// Inserting a master appendable directory into a
	// non-master directory (probably a top-leve directory
	// under /vesta): add a master-repository attribute to the
	// new directory.
	result->setAttrib("master-repository", myMasterHint.cchars(), NULL);
      }
    state->vss.put(result_idx, result);
    return ret;
  };
};

class InsertMutableDirectoryStep : public VSAStep {
  VestaSourceAtomic::VSIndex parenti;
  char arc[MAX_ARC_LEN+1];
  VestaSourceAtomic::VSIndex diri;
  bool master;
  VestaSource::dupeCheck chk;
  time_t timestamp;
 public:
  InsertMutableDirectoryStep(VSAProgramState* state) {
    next = NULL;
    parenti = state->srpc->recv_int();
    int len = MAX_ARC_LEN+1;
    state->srpc->recv_chars_here(arc, len);
    diri = state->srpc->recv_int();
    master = (bool) state->srpc->recv_int();
    chk = (VestaSource::dupeCheck) state->srpc->recv_int();
    timestamp = (time_t) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    int result_idx = state->vss.size();
    state->vss.addhi(0);
    if (parenti < 0 || parenti >= state->vss.size() ||
	diri < -1 || diri >= state->vss.size())
      return VestaSource::invalidArgs;
    VestaSource* parent = state->vss.get(parenti);
    if (parent == 0) return VestaSource::invalidArgs;
    VestaSource* dir = 0;
    if (diri != -1) dir = state->vss.get(diri);
    VestaSource* result = 0;
    VestaSource::errorCode ret =
      parent->insertMutableDirectory(arc, dir, master, state->who,
				     chk, &result,
				     timestamp ? timestamp : state->now);
    state->vss.put(result_idx, result);
    return ret;
  };
};

class InsertGhostStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  char arc[MAX_ARC_LEN+1];
  bool master;
  VestaSource::dupeCheck chk;
  time_t timestamp;
 public:
  InsertGhostStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    int len = MAX_ARC_LEN+1;
    state->srpc->recv_chars_here(arc, len);
    master = (bool) state->srpc->recv_int();
    chk = (VestaSource::dupeCheck) state->srpc->recv_int();
    timestamp = (time_t) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    int result_idx = state->vss.size();
    state->vss.addhi(0);
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == 0) return VestaSource::invalidArgs;
    VestaSource* result = 0;
    VestaSource::errorCode ret =
      vs->insertGhost(arc, master, state->who, chk, &result,
		      timestamp ? timestamp : state->now);
    state->vss.put(result_idx, result);
    return ret;
  };
};

class InsertStubStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  char arc[MAX_ARC_LEN+1];
  bool master;
  VestaSource::dupeCheck chk;
  time_t timestamp;
 public:
  InsertStubStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    int len = MAX_ARC_LEN+1;
    state->srpc->recv_chars_here(arc, len);
    master = (bool) state->srpc->recv_int();
    chk = (VestaSource::dupeCheck) state->srpc->recv_int();
    timestamp = (time_t) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    int result_idx = state->vss.size();
    state->vss.addhi(0);
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == 0) return VestaSource::invalidArgs;
    VestaSource* result = 0;
    VestaSource::errorCode ret =
      vs->insertStub(arc, master, state->who, chk, &result,
		     timestamp ? timestamp : state->now);
    state->vss.put(result_idx, result);
    return ret;
  };
};

class RenameToStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  char arc[MAX_ARC_LEN+1];
  VestaSourceAtomic::VSIndex fromDirI;
  char fromArc[MAX_ARC_LEN+1];
  VestaSource::dupeCheck chk;
  time_t timestamp;
 public:
  RenameToStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    int len = MAX_ARC_LEN+1;
    state->srpc->recv_chars_here(arc, len);
    fromDirI = state->srpc->recv_int();
    len = MAX_ARC_LEN+1;
    state->srpc->recv_chars_here(fromArc, len);
    chk = (VestaSource::dupeCheck) state->srpc->recv_int();
    timestamp = (time_t) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    if (vsi < 0 || vsi >= state->vss.size() ||
	fromDirI < 0 || fromDirI >= state->vss.size()) {
      return VestaSource::invalidArgs;
    }
    VestaSource* vs = state->vss.get(vsi);
    if (vs == NULL) return VestaSource::invalidArgs;
    VestaSource* fromDir = state->vss.get(fromDirI);
    if (fromDir == NULL) return VestaSource::invalidArgs;
    VestaSource::errorCode ret =
      vs->renameTo(arc, fromDir, fromArc, state->who, chk,
		   timestamp ? timestamp : state->now);
    return ret;
  };
};

class MakeFilesImmutableStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  unsigned int threshold;
 public:
  MakeFilesImmutableStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    threshold = (unsigned int) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == NULL) return VestaSource::invalidArgs;
    return vs->makeFilesImmutable(threshold, state->who);
  };
};

class TestMasterStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  bool master;
  VestaSource::errorCode err;
 public:
  TestMasterStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    master = (bool) state->srpc->recv_int();
    err = (VestaSource::errorCode) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == NULL) return VestaSource::invalidArgs;
    if (vs->master == master) {
      return VestaSource::ok;
    } else {
      return err;
    }
  };
};

class SetMasterStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  bool master;
 public:
  SetMasterStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    master = (bool) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == NULL) return VestaSource::invalidArgs;
    return vs->setMaster(master, state->who);
  };
};

class InAttribsStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  char* name;
  char* value;
  bool expected;
  VestaSource::errorCode err;
 public:
  InAttribsStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    name = state->srpc->recv_chars();
    value = state->srpc->recv_chars();
    expected = (bool) state->srpc->recv_int();
    err = (VestaSource::errorCode) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == NULL) return VestaSource::invalidArgs;
    if (vs->inAttribs(name, value) == expected) {
      return VestaSource::ok;
    } else {
      return err;
    };
  };
  ~InAttribsStep() {
    delete[] name;
    delete[] value;
  };
};

class WriteAttribStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  VestaSource::attribOp op;
  char* name;
  char* value;
  time_t timestamp;
 public:
  WriteAttribStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    op = (VestaSource::attribOp) state->srpc->recv_int();
    name = state->srpc->recv_chars();
    value = state->srpc->recv_chars();
    timestamp = (time_t) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == NULL) return VestaSource::invalidArgs;

    // If this is an object under the mutable root that doesn't have
    // attributes, it must still be in the immutable base directory
    // (i.e. an immutable directory unmodified since checkout).  To
    // change its attributes, we need to copy it to the mutable
    // portion.
    if(!vs->hasAttribs() && MutableRootLongId.isAncestorOf(vs->longid))
      {
	VestaSource *new_vs = 0;
	VestaSource::errorCode err = vs->copyToMutable(new_vs, state->who);
	if(err != VestaSource::ok) return err;
	assert(new_vs != 0);
	delete vs;
	vs = new_vs;
	state->vss.put(vsi, vs);
	// Unfortunately, that operation may have made other
	// VestaSource objects in state->vss mutable as well.  Replace
	// any that have been made mutable with new valid objects.
	for(int i = 0; i < state->vss.size(); i++)
	  {
	    if(i == vsi) continue;
	    VestaSource *other_vs = state->vss.get(i);
	    if((other_vs != 0) &&
	       (other_vs->type == VestaSource::immutableDirectory) &&
	       (other_vs->longid.isAncestorOf(vs->longid)))
	      {
		VestaSource *new_other_vs = other_vs->longid.lookup();
		assert(new_other_vs != 0);
		delete other_vs;
		other_vs = new_other_vs;
		state->vss.put(i, other_vs);
	      }
	  }
      }
    return vs->writeAttrib(op, name, value, state->who,
			   timestamp ? timestamp : state->now);
  };
  ~WriteAttribStep() {
    delete[] name;
    delete[] value;
  };
};


class AccessCheckStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  AccessControl::Class cls;
  bool expected;
  VestaSource::errorCode err;
 public:
  AccessCheckStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    cls = (AccessControl::Class) state->srpc->recv_int();
    expected = (bool) state->srpc->recv_int();
    err = (VestaSource::errorCode) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == NULL) return VestaSource::invalidArgs;
    if (vs->ac.check(state->who, cls) == expected) {
      return VestaSource::ok;
    } else {
      return err;
    };
  };
};

class TypeCheckStep : public VSAStep {
  VestaSourceAtomic::VSIndex vsi;
  unsigned int allowed;
  VestaSource::errorCode err;
 public:
  TypeCheckStep(VSAProgramState* state) {
    next = NULL;
    vsi = state->srpc->recv_int();
    allowed = (unsigned int) state->srpc->recv_int();
    err = (VestaSource::errorCode) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    if (vsi < 0 || vsi >= state->vss.size()) return VestaSource::invalidArgs;
    VestaSource* vs = state->vss.get(vsi);
    if (vs == NULL) return VestaSource::invalidArgs;
    if (VestaSourceAtomic::typebit(vs->type) & allowed) {
      return VestaSource::ok;
    } else {
      return err;
    };
  };
};

static bool mergeAttribCallback(void* closure, const char* name);
static bool mergeValueCallback(void* closure, const char* value);

class MergeAttribStep : public VSAStep {
  VestaSourceAtomic::VSIndex fromvsi;
  VestaSourceAtomic::VSIndex tovsi;
  char* name;
  time_t timestamp;
  // used at runtime:
  VSAProgramState* state;
  VestaSource* fromvs;
  VestaSource* tovs;
  const char* curname;
  VestaSource::errorCode err;
  friend bool mergeAttribCallback(void* closure, const char* name);
  friend bool mergeValueCallback(void* closure, const char* value);
 public:
  MergeAttribStep(VSAProgramState* state) {
    next = NULL;
    fromvsi = state->srpc->recv_int();
    tovsi = state->srpc->recv_int();
    name = state->srpc->recv_chars();
    timestamp = (time_t) state->srpc->recv_int();
  };
  VestaSource::errorCode execute(VSAProgramState* state) {
    if (fromvsi < 0 || fromvsi >= state->vss.size()) {
      return VestaSource::invalidArgs;
    }
    fromvs = state->vss.get(fromvsi);
    if (fromvs == NULL) return VestaSource::invalidArgs;
    if (tovsi < 0 || tovsi >= state->vss.size()) {
      return VestaSource::invalidArgs;
    }
    tovs = state->vss.get(tovsi);
    if (tovs == NULL) return VestaSource::invalidArgs;
    err = VestaSource::ok;
    this->state = state;
    if (*name == '\0') {
      fromvs->listAttribs(mergeAttribCallback, (void*) this);
    } else {
      mergeAttribCallback((void*) this, name);
    }
    return err;
  };
  ~MergeAttribStep() {
    delete[] name;
  };
};

static bool
mergeValueCallback(void* closure, const char* value)
{
  MergeAttribStep* step = (MergeAttribStep*) closure;
  step->err = step->tovs->addAttrib(step->curname, value,
				    step->state->who, step->timestamp ?
				    step->timestamp : step->state->now);
  return (step->err == VestaSource::ok);
}

static bool
mergeAttribCallback(void* closure, const char* name)
{
  MergeAttribStep* step = (MergeAttribStep*) closure;
  step->curname = name;
  step->fromvs->getAttrib(name, mergeValueCallback, closure);
  return (step->err == VestaSource::ok);
}


void
DeleteProgram(VSAProgramState* state)
{
  while (state->first) {
    VSAStep *vsac;
    vsac = state->first;
    state->first = state->first->next;
    delete vsac;
  }
  int i, n;
  n = state->vss.size();
  for (i=0; i<n; i++) {
    VestaSource* vs = state->vss.get(i);
    if (vs) delete vs;
  }
  delete state->who;
}

void
VSAtomic(SRPC* srpc, int intf_ver)
{
  VSAProgramState state;
  VestaSource::errorCode err;

  // Initialize program state
  state.pc = 0;
  state.first = state.last = NULL;
  state.target1 = VestaSource::ok;
  state.target2 = VestaSource::ok;
  state.okreplace = VestaSource::ok;
  state.lock = &StableLock;
  state.who = srpc_recv_identity(srpc, intf_ver);
  state.srpc = srpc;
  state.intf_ver = intf_ver;

#if defined(REPOS_PERF_DEBUG)
  OBufStream l_locked_reason;
  bool first_action = true;
  l_locked_reason << "VSAtomic:{";
#endif

  // Receive the program
  try {
    for (;;) {
      // Receive one step
      int opcode = srpc->recv_int();
      VSAStep *vsac;
    
      switch (opcode) {
      case VestaSourceSRPC::AtomicRun:
	// Break out of for loop
	goto done;
      case VestaSourceSRPC::AtomicCancel:
	srpc->recv_end();
	srpc->send_end();
	DeleteProgram(&state);
	return;
      case VestaSourceSRPC::AtomicTarget:
	vsac = NEW_CONSTR(SetTargetStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "SetTarget";
#endif
	break;
      case VestaSourceSRPC::AtomicDeclare:
	vsac = NEW_CONSTR(DeclStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "Decl";
#endif
	break;
      case VestaSourceSRPC::AtomicResync:
	vsac = NEW_CONSTR(ResyncStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "Resync";
#endif
	break;
      case VestaSourceSRPC::SetTimestamp:
	vsac = NEW_CONSTR(SetTimestampStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "SetTime";
#endif
	break;
      case VestaSourceSRPC::Lookup:
	vsac = NEW_CONSTR(LookupStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "Lookup";
#endif
	break;
      case VestaSourceSRPC::LookupPathname:
	vsac = NEW_CONSTR(LookupPathnameStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "LookupPath";
#endif
	break;
      case VestaSourceSRPC::LookupIndex:
	vsac = NEW_CONSTR(LookupIndexStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "LookupIdx";
#endif
	break;
      case VestaSourceSRPC::ReallyDelete:
	vsac = NEW_CONSTR(ReallyDeleteStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "ReallyDel";
#endif
	break;
      case VestaSourceSRPC::InsertFile:
	vsac = NEW_CONSTR(InsertFileStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "InsertFile";
#endif
	break;
      case VestaSourceSRPC::InsertMutableFile:
	vsac = NEW_CONSTR(InsertMutableFileStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "InsertMutFile";
#endif
	break;
      case VestaSourceSRPC::InsertImmutableDirectory:
	vsac = NEW_CONSTR(InsertImmutableDirectoryStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "InsertImmutDir";
#endif
	break;
      case VestaSourceSRPC::InsertAppendableDirectory:
	vsac = NEW_CONSTR(InsertAppendableDirectoryStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "InsertAppDir";
#endif
	break;
      case VestaSourceSRPC::InsertMutableDirectory:
	vsac = NEW_CONSTR(InsertMutableDirectoryStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "InsertMutDir";
#endif
	break;
      case VestaSourceSRPC::InsertGhost:
	vsac = NEW_CONSTR(InsertGhostStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "InsertGhost";
#endif
	break;
      case VestaSourceSRPC::InsertStub:
	vsac = NEW_CONSTR(InsertStubStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "InsertStub";
#endif
	break;
      case VestaSourceSRPC::RenameTo:
	vsac = NEW_CONSTR(RenameToStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "RenameTo";
#endif
	break;
      case VestaSourceSRPC::MakeFilesImmutable:
	vsac = NEW_CONSTR(MakeFilesImmutableStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "MakeFilesImmut";
#endif
	break;
      case VestaSourceSRPC::AtomicTestMaster:
	vsac = NEW_CONSTR(TestMasterStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "TestMaster";
#endif
	break;
      case VestaSourceSRPC::AtomicSetMaster:
	vsac = NEW_CONSTR(SetMasterStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "SetMaster";
#endif
	break;
      case VestaSourceSRPC::InAttribs:
	vsac = NEW_CONSTR(InAttribsStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "InAttribs";
#endif
	break;
      case VestaSourceSRPC::WriteAttrib:
	vsac = NEW_CONSTR(WriteAttribStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "WriteAttrib";
#endif
	break;
      case VestaSourceSRPC::AtomicAccessCheck:
	vsac = NEW_CONSTR(AccessCheckStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "AccessCheck";
#endif
	break;
      case VestaSourceSRPC::AtomicTypeCheck:
	vsac = NEW_CONSTR(TypeCheckStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "TypeCheck";
#endif
	break;
      case VestaSourceSRPC::AtomicMergeAttrib:
	vsac = NEW_CONSTR(MergeAttribStep, (&state));
#if defined(REPOS_PERF_DEBUG)
	if(first_action)
	  first_action = false;
	else
	  l_locked_reason << ",";
	l_locked_reason << "MergeAttrib";
#endif
	break;
      default:
	Repos::dprintf(DBG_ALWAYS,
		       "VestaSourceReceptionist got misplaced Atomic opcode %d; "
		       "client %s\n", (int) opcode, srpc->remote_socket().cchars());
	// Throw SRPC::failure both locally and remotely
	srpc->send_failure(SRPC::version_skew,
			   "VestaSourceSRPC: Unknown Atomic opcode", false);
	break;
      }

      if (state.first == NULL) {
	state.first = vsac;
      } else {
	state.last->next = vsac;
      }
      state.last = vsac;
    }
  done:
    srpc->recv_end();

  } catch (SRPC::failure f) {
    DeleteProgram(&state);
    throw;
  }

  // Run the program inside failure-atomic brackets
  state.lock->acquireWrite();
  RWLOCK_LOCKED_REASON(state.lock, l_locked_reason.str());
  VRLog.start();
  state.now = time(NULL);
  err = VestaSource::ok;
  try {
    VSAStep* vsac = state.first;
    while (vsac) {
      err = vsac->execute(&state);
      if (err != state.target1 && err != state.target2) {
	break;
      }
      state.pc++;
      vsac = vsac->next;
    }

    VRLog.commit();
    state.lock->releaseWrite();
    state.lock = NULL;

    // ...and send the results
    srpc->send_int(state.pc);
    srpc->send_int((int) err);
    srpc->send_int((int) state.okreplace);
    srpc->send_int(vsac == NULL);
    srpc->send_end();

  } catch (SRPC::failure f) {
    if (state.lock) {
      VRLog.commit();
      state.lock->releaseWrite();
    }
    DeleteProgram(&state);
    throw;
  }

  // Clean up
  DeleteProgram(&state);
}
