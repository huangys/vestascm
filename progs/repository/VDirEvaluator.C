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
// VDirEvaluator.C
//
// Directory of type evaluatorDirectory.  The contents of such a
// directory are a binding held by the evalutor.  The methods of this
// object type make remote calls to the evalutor.
//

#include <assert.h>
#include "Basics.H"
#include "VDirEvaluator.H"
#include "Evaluator_Dir_SRPC.H"
#include "MultiSRPC.H"
#include "VLeaf.H"
#include "VMemPool.H"
#include "VestaConfig.H"
#include "IndexKey.H"
#include "ArcTable.H"
#include "logging.H"
#include "ListResults.H"

#include "timing.H"

// Types
struct EntryInfo {
    // One entry from an evaluator[ROE]Directory
    VestaSource::typeTag type;
    char* name;
    unsigned int index;
    struct EvalDirInfo* edi; // if type= evaluator[ROE]Directory
    ShortId sid;             // if type= immutableFile
    FP::Tag fptag;           // if type= immutableFile
};
typedef Table<IndexKey, EntryInfo*>::Default IndexToEntryTable;
typedef Table<IndexKey, EntryInfo*>::Iterator IndexToEntryIter;
typedef Table<ArcKey, EntryInfo*>::Default ArcToEntryTable;
typedef Table<ArcKey, EntryInfo*>::Iterator ArcToEntryIter;

struct EvalDirInfo {
    // Local representation and entry cache for evaluator[ROE]Directory
    // Locking: the entry caches may need to be modified by threads holding
    //   only a read lock on the current volatile tree, so the locking
    //   rule is that you must hold either a write lock on the current
    //   volatile tree, or a read lock *and* cache_mu (below).
    char* hostname;
    char* port;
    Bit64 dirhandle;
    Bit32 timestamp;
    IndexToEntryTable itab;
    ArcToEntryTable atab;
    Bit8* rep; // pointer to rep in VMemPool
    bool* alive; // pointer to a member of evaluator top directory
                 // (VolatileRootEntry)
    Basics::mutex cache_mu;
};

// Module globals
static MultiSRPC* multi; // has internal mutex
static pthread_once_t once = PTHREAD_ONCE_INIT;
static int listChunkSize = 8300;
static int listPerEntryOverhead = 40;

// By default we give the evaluator a maximum of five minutes to respond
// to a call to the ToolDirectoryServer.  Of course normally it should
// be much less than this.  The limit is there just to prevent a
// suspended evaluator causing a lock to be held and a thread to be
// waiting indefinitely.
static unsigned int readTimeout = 300;

static FP::Tag nullFPTag("");

extern "C"
{
  static void
  VDirEvaluator_init()
  {
    try {
	Text val;
	multi = NEW(MultiSRPC);
	assert(DIR_HANDLE_BYTES == sizeof(Bit64));
	if (VestaConfig::get("Repository",
			     "EvaluatorDirSRPC_listChunkSize", val)) {
	    listChunkSize = atoi(val.cchars());
	}
	if (VestaConfig::get("Repository",
			     "EvaluatorDirSRPC_listPerEntryOverhead", val)) {
	    listPerEntryOverhead = atoi(val.cchars());
	}
	if (VestaConfig::get("Repository",
			     "EvaluatorDirSRPC_read_timeout", val)) {
	    readTimeout = atoi(val.cchars());
	}
    } catch (VestaConfig::failure f) {
	Repos::dprintf(DBG_ALWAYS, 
		       "VDirEvaluator_init got VestaConfig::failure: %s\n",
		       f.msg.cchars());
	abort();
    }
  }
}

VDirEvaluator::VDirEvaluator(VestaSource::typeTag type, 
			     const char* hostname, const char* port,
			     Bit64 dirhandle, bool* alive, time_t timestamp)
  throw ()
{
    pthread_once(&once, VDirEvaluator_init);
    this->type = type;

    rep = (Bit8*) VMemPool::allocate(VMemPool::vDirEvaluator,
				     VDIREV_SIZE);
    edi = NEW(EvalDirInfo);
    assert(VMemPool::type(rep) == VMemPool::vDirEvaluator);
    setRepEDI(edi);
    edi->hostname = strdup(hostname);
    edi->port = strdup(port);
    edi->dirhandle = dirhandle;
    edi->timestamp = timestamp;
    edi->rep = rep;
    edi->alive = alive;
    assert(VMemPool::type(rep) == VMemPool::vDirEvaluator);
}

VDirEvaluator::VDirEvaluator(VestaSource::typeTag type, Bit8* rep) throw ()
{
    this->type = type;
    this->rep = rep;
    this->edi = repEDI();
    assert(VMemPool::type(rep) == VMemPool::vDirEvaluator);
}

VDirEvaluator::VDirEvaluator(VestaSource::typeTag type, EvalDirInfo* edi)
  throw ()
{
    this->type = type;
    this->rep = edi->rep;
    this->edi = edi;
    assert(VMemPool::type(rep) == VMemPool::vDirEvaluator);
}

VestaSource::errorCode
VDirEvaluator::lookup(Arc arc, VestaSource*& result,
		      AccessControl::Identity who,
		      unsigned int indexOffset) throw ()
{
  // Start by initializing the result, in case we fail before setting
  // it to something useful.
  result = 0;

    bool hit = false;
    assert(VMemPool::type(rep) == VMemPool::vDirEvaluator);

    // Optimize out repeated SRPCs to dead evaluator
    if (!(*edi->alive)) return VestaSource::rpcFailure;

    // Look in cache first
    ArcKey ak(arc, strlen(arc));
    EntryInfo* ei;
    edi->cache_mu.lock();
    if (edi->atab.Get(ak, ei)) {
	// Hit
	hit = true;
	LongId result_longid;
	switch (ei->type) {
	  case VestaSource::evaluatorDirectory:
	  case VestaSource::evaluatorROEDirectory:
	    Repos::dprintf(DBG_VDIREVAL, 
			   "eval dir lookup %s in 0x%" FORMAT_LENGTH_INT_64 "x"
			   " -> dir 0x%" FORMAT_LENGTH_INT_64 "x (cache)\n",
			   arc, edi->dirhandle, ei->edi->dirhandle);
	    
	    // Check for LongId overflow.
	    result_longid = longid.append(ei->index);
	    if(result_longid == NullLongId)
	      {
		edi->cache_mu.unlock();
		return VestaSource::longIdOverflow;
	      }

	    result = NEW_CONSTR(VDirEvaluator, (ei->type, ei->edi));
	    result->ac = this->ac;
	    result->longid = result_longid;
	    result->pseudoInode = indexToPseudoInode(ei->index);
	    break;
	  case VestaSource::device:
	    Repos::dprintf(DBG_VDIREVAL, 
			   "eval dir lookup %s in 0x%" FORMAT_LENGTH_INT_64 "x"
			   " -> device 0x%x (cache)\n",
			   arc, edi->dirhandle, ei->sid);

	    // Check for LongId overflow.
	    result_longid = longid.append(ei->index);
	    if(result_longid == NullLongId)
	      {
		edi->cache_mu.unlock();
		return VestaSource::longIdOverflow;
	      }

	    result = NEW_CONSTR(VLeaf, (ei->type, ei->sid));
	    result->ac = this->ac;
	    result->longid = result_longid;
	    result->pseudoInode = indexToPseudoInode(ei->index);
	    break;
	  case VestaSource::immutableFile:
	    Repos::dprintf(DBG_VDIREVAL, 
			   "eval dir lookup %s in 0x%" FORMAT_LENGTH_INT_64 "x"
			   " -> sid 0x%08x (cache)\n",
			   arc, edi->dirhandle, ei->sid);

	    // Check for LongId overflow.  (We skip this if our type
	    // is evaluatorROEDirectory, as then the object will get a
	    // ShortId derived LongId.)
	    if((VestaSource::type != VestaSource::evaluatorROEDirectory) &&
	       ((result_longid = longid.append(ei->index))  == NullLongId))
	      {
		edi->cache_mu.unlock();
		return VestaSource::longIdOverflow;
	      }

	    result = NEW_CONSTR(VLeaf, (ei->type, ei->sid));
	    result->fptag = ei->fptag;
	    result->ac = this->ac;
	    if (VestaSource::type == VestaSource::evaluatorROEDirectory) {
		result->longid = LongId::fromShortId(ei->sid, &ei->fptag);
		result->pseudoInode = ei->sid;
		result->ac.mode = 0444;
	    } else {
		result->longid = result_longid;
		result->pseudoInode = indexToPseudoInode(ei->index);
	    }
	    break;
	  case VestaSource::deleted:
	    // Failed lookup
	    Repos::dprintf(DBG_VDIREVAL, 
			   "eval dir lookup %s in 0x%" FORMAT_LENGTH_INT_64 "x"
			   " -> notFound (cache)\n",
			   arc, edi->dirhandle);
	    result = NULL;
	    edi->cache_mu.unlock();
	    return VestaSource::notFound;
	  default:
	    assert(false);
	    break;
	}
	result->master = true;
	result->attribs = NULL;
	edi->cache_mu.unlock();
	return VestaSource::ok;
    }
    edi->cache_mu.unlock();
    assert(VMemPool::type(rep) == VMemPool::vDirEvaluator);

    // Do SRPC
    RECORD_TIME_POINT;
    MultiSRPC::ConnId id = -1;
    VestaSource::errorCode err;
    try {
	SRPC *srpc;
	id = multi->Start((Text) edi->hostname, (Text) edi->port, srpc);
	srpc->enable_read_timeout(readTimeout);
	
	srpc->start_call(ed_lookup, EVALUATOR_DIR_SRPC_VERSION);
	srpc->send_bytes((const char*) &edi->dirhandle, sizeof(Bit64));
	srpc->send_chars(arc);
	srpc->send_end();
	int edtype = srpc->recv_int();

	unsigned int rawIndex, index;
	Bit64 subdirHandle = 0;
	ShortId sid = NullShortId;
	FP::Tag fptag = nullFPTag;
	int len;
	EvalDirInfo* resEDI = NULL;
	LongId result_longid;
	
	switch (edtype) {
	  case ed_none:
	    srpc->recv_end();
	    Repos::dprintf(DBG_VDIREVAL,
			   "eval dir lookup %s in 0x%" FORMAT_LENGTH_INT_64 "x"
			   " -> notFound\n",
			   arc, edi->dirhandle);
	    err = VestaSource::notFound;
	    break;

	  case ed_directory:
	    rawIndex = (unsigned int) srpc->recv_int();
	    index = indexOffset + 2*(rawIndex + 1);
	    len = sizeof(subdirHandle);
	    srpc->recv_bytes_here((char*) &subdirHandle, len);
	    srpc->recv_end();
	    // Check for LongId overflow.
	    result_longid = longid.append(index);
	    if(result_longid == NullLongId)
	      {
		err = VestaSource::longIdOverflow;
	      }
	    else
	      {
		result = NEW_CONSTR(VDirEvaluator,
				    (this->type,
				     edi->hostname, edi->port,
				     subdirHandle, edi->alive, edi->timestamp));
		resEDI = ((VDirEvaluator*)result)->edi; // save for entry cache
		result->fptag = fptag;
		result->longid = result_longid;
		result->master = true;
		result->ac = this->ac;
		result->pseudoInode = indexToPseudoInode(index);
		result->attribs = NULL;
		Repos::dprintf(DBG_VDIREVAL,
			       "eval dir lookup %s in 0x%" FORMAT_LENGTH_INT_64 "x"
			       " -> dir 0x%" FORMAT_LENGTH_INT_64 "x\n",
			       arc, edi->dirhandle, subdirHandle);
		err = VestaSource::ok;
	      }
	    break;

	  case ed_file:
	    rawIndex = (unsigned int) srpc->recv_int();
	    index = indexOffset + 2*(rawIndex + 1);
	    sid = (ShortId) srpc->recv_int();
	    fptag.Recv(*srpc);
	    srpc->recv_end();
	    // Check for LongId overflow.  (We skip this if our type
	    // is evaluatorROEDirectory, as then the object will get a
	    // ShortId derived LongId.)
	    if((VestaSource::type != VestaSource::evaluatorROEDirectory) &&
	       ((result_longid = longid.append(index)) == NullLongId))
	      {
		err = VestaSource::longIdOverflow;
	      }
	    else
	      {
		result = NEW_CONSTR(VLeaf, (VestaSource::immutableFile, sid));
		result->fptag = fptag;
		result->ac = this->ac;
		if (VestaSource::type == VestaSource::evaluatorROEDirectory) {
		  result->longid = LongId::fromShortId(sid, &fptag);
		  result->pseudoInode = sid;
		  result->ac.mode = 0444;
		} else {
		  result->longid = result_longid;
		  result->pseudoInode = indexToPseudoInode(index);
		}
		result->master = true;
		result->attribs = NULL;
		Repos::dprintf(DBG_VDIREVAL, 
			       "eval dir lookup %s in 0x%" FORMAT_LENGTH_INT_64 "x"
			       " -> sid 0x%08x\n",
			       arc, edi->dirhandle, sid);
		err = VestaSource::ok;
	      }
	    break;

	  case ed_device:
	    rawIndex = (unsigned int) srpc->recv_int();
	    index = indexOffset + 2*(rawIndex + 1);
	    sid = (ShortId) srpc->recv_int(); // really the device number
	    srpc->recv_end();
	    // Check for LongId overflow.
	    result_longid = longid.append(index);
	    if(result_longid == NullLongId)
	      {
		err = VestaSource::longIdOverflow;
	      }
	    else
	      {
		result = NEW_CONSTR(VLeaf, (VestaSource::device, sid));
		result->fptag = fptag;
		result->longid = result_longid;
		result->master = true;
		result->ac = this->ac;
		result->pseudoInode = indexToPseudoInode(index);
		result->attribs = NULL;
		Repos::dprintf(DBG_VDIREVAL, 
			       "eval dir lookup %s in 0x%" FORMAT_LENGTH_INT_64 "x"
			       " -> device 0x%x\n",
			       arc, edi->dirhandle, sid);
		err = VestaSource::ok;
	      }
	    break;

	  default:
	    assert(false);
	    break;
	}
	assert(VMemPool::type(rep) == VMemPool::vDirEvaluator);
	if (err == VestaSource::ok) {
	    // Enter into cache
	    edi->cache_mu.lock();
	    EntryInfo* ei;
	    if (edi->itab.Delete(index, ei)) {
		// Already an entry?!  This should happen only if
		// another thread just added it since we took our
		// cache miss above.
		assert(result->type == ei->type);
		assert(strcmp(ei->name, arc) == 0);
		/*assert(ei->handle == subdirHandle);*/ /* not guaranteed */
		assert(ei->sid == sid);
		assert(ei->fptag == fptag);
		free(ei->name);
		delete ei;
	    }
	    ei = NEW(EntryInfo);
	    ei->type = result->type;
	    ei->name = strdup(arc);
	    ei->index = index;
	    ei->edi = resEDI;
	    ei->sid = sid;
	    ei->fptag = fptag;
	    edi->itab.Put(index, ei);
	    ArcKey ak(ei->name, strlen(ei->name));
	    edi->atab.Put(ak, ei);
	    if (resEDI) {
	      assert(VMemPool::type(resEDI->rep) == VMemPool::vDirEvaluator);
	    }
	    edi->cache_mu.unlock();
	} else if (err == VestaSource::notFound) {
	    // Enter failed lookup into cache
	    edi->cache_mu.lock();
	    EntryInfo* ei;
	    char* duparc = strdup(arc);
	    ArcKey ak(duparc, strlen(duparc));
	    if (edi->atab.Delete(ak, ei)) {
		// Already an entry?!  This should happen only if
		// another thread just added it since we took our
		// cache miss above.
		assert(ei->type == VestaSource::deleted);
		assert(strcmp(ei->name, arc) == 0);
		free(ei->name);
		delete ei;
	    }
	    ei = NEW(EntryInfo);
	    ei->type = VestaSource::deleted;
	    ei->name = duparc;
	    ei->index = 0;
	    ei->edi = NULL;
	    ei->sid = NullShortId;
	    ei->fptag = nullFPTag;
	    edi->atab.Put(ak, ei);
	    edi->cache_mu.unlock();
	}
	assert(VMemPool::type(rep) == VMemPool::vDirEvaluator);

    } catch (SRPC::failure f) {
	char buf[100];
	Repos::pr_nfs_fh(buf, (nfs_fh*) &longid.value);
	Repos::dprintf(DBG_ALWAYS,
		       "VDirEvaluator::lookup of %s in %s (%s:%s "
		       "0x%" FORMAT_LENGTH_INT_64 "x)"
		       " returns VestaSource::rpcFailure: %s (%d)\n",
		       arc, buf, edi->hostname, edi->port, edi->dirhandle, 
		       f.msg.cchars(), f.r);
	err = VestaSource::rpcFailure;
	*edi->alive = false;
    }
    multi->End(id);
    RECORD_TIME_POINT;
    return err;
}

VestaSource::errorCode
VDirEvaluator::lookupIndex(unsigned int index, VestaSource*& result,
			   char* arcbuf) throw ()
{
  // Start by initializing the result, in case we fail before setting
  // it to something useful.
  result = 0;

    assert(VMemPool::type(rep) == VMemPool::vDirEvaluator);
    // An evaluatorDirectory has only even indices (from our point of
    // view).  Also, 0 is not a valid index; it terminates a LongId.
    if (index & 1) {
	return VestaSource::notFound;
    }

    // Optimize out repeated SRPCs to dead evaluator
    if (!(*edi->alive)) return VestaSource::rpcFailure;

    // Look in cache first
    EntryInfo* ei;
    edi->cache_mu.lock();
    if (edi->itab.Get(index, ei)) {
	// Hit
      LongId result_longid;
	switch (ei->type) {
	  case VestaSource::evaluatorDirectory:
	  case VestaSource::evaluatorROEDirectory:
	    Repos::dprintf(DBG_VDIREVAL, 
			   "eval dir lookupIndex %u in 0x%" FORMAT_LENGTH_INT_64 "x"
			   " -> dir 0x%" FORMAT_LENGTH_INT_64 "x "
			   "(cache)\n", index, edi->dirhandle, ei->edi->dirhandle);
	    
	    // Check for LongId overflow.
	    result_longid = longid.append(index);
	    if(result_longid == NullLongId)
	      {
		edi->cache_mu.unlock();
		return VestaSource::longIdOverflow;
	      }

	    result = NEW_CONSTR(VDirEvaluator, (ei->type, ei->edi));
	    result->ac = this->ac;
	    result->longid = result_longid;
	    result->pseudoInode = indexToPseudoInode(index);
	    break;
	  case VestaSource::device:
	    Repos::dprintf(DBG_VDIREVAL, 
			   "eval dir lookupIndex %u in 0x%" FORMAT_LENGTH_INT_64 "x"
			   " -> device 0x%x "
			   "(cache)\n", index, edi->dirhandle, ei->sid);
	    
	    // Check for LongId overflow.
	    result_longid = longid.append(index);
	    if(result_longid == NullLongId)
	      {
		edi->cache_mu.unlock();
		return VestaSource::longIdOverflow;
	      }

	    result = NEW_CONSTR(VLeaf, (ei->type, ei->sid));
	    result->ac = this->ac;
	    result->longid = result_longid;
	    result->pseudoInode = indexToPseudoInode(index);
	    break;
	  case VestaSource::immutableFile:
	    Repos::dprintf(DBG_VDIREVAL, 
			   "eval dir lookupIndex %u in 0x%" FORMAT_LENGTH_INT_64 "x"
			   " -> sid 0x%08x "
			   "(cache)\n", index, edi->dirhandle, ei->sid);

	    // Check for LongId overflow.  (We skip this if our type
	    // is evaluatorROEDirectory, as then the object will get a
	    // ShortId derived LongId.)
	    if((VestaSource::type != VestaSource::evaluatorROEDirectory) &&
	       ((result_longid = longid.append(ei->index))  == NullLongId))
	      {
		edi->cache_mu.unlock();
		return VestaSource::longIdOverflow;
	      }

	    result = NEW_CONSTR(VLeaf, (ei->type, ei->sid));
	    result->ac = this->ac;
	    if (VestaSource::type == VestaSource::evaluatorROEDirectory) {
		result->longid = LongId::fromShortId(ei->sid, &ei->fptag);
		result->pseudoInode = ei->sid;
		result->ac.mode = 0444;
	    } else {
		result->longid = result_longid;
		result->pseudoInode = indexToPseudoInode(index);
	    }
	    break;
	  default:
	    assert(false);
	    break;
	}
	result->fptag = ei->fptag;
	result->master = true;
	result->attribs = NULL;
	if (arcbuf != NULL) strcpy(arcbuf, ei->name);
	edi->cache_mu.unlock();
	assert(VMemPool::type(rep) == VMemPool::vDirEvaluator);
	return VestaSource::ok;
    }
    edi->cache_mu.unlock();

    // Miss; do SRPC
    // Should never happen: we never discard anything from our cache
    assert(false);
    return VestaSource::notFound;
}


VestaSource::errorCode
VDirEvaluator::list(unsigned int firstIndex,
		    VestaSource::listCallback callback, void* closure,
		    AccessControl::Identity who,
		    bool deltaOnly, unsigned int indexOffset) throw ()
{
    assert(VMemPool::type(rep) == VMemPool::vDirEvaluator);
    int curChunkSize = 0;
    VestaSource::errorCode err = VestaSource::ok;
    // Convert 0 or even index > 0 to raw form
    assert(indexOffset == 0);
    assert((firstIndex & 1) == 0);
    int rawIndex = (firstIndex == 0) ? 0 : (firstIndex >> 1) - 1; 

    if (deltaOnly) return VestaSource::ok;

    // Optimize out repeated SRPCs to dead evaluator
    if (!(*edi->alive)) return VestaSource::rpcFailure;

    RECORD_TIME_POINT;
    MultiSRPC::ConnId id = -1;
    try {
	SRPC *srpc;
	id = multi->Start((Text) edi->hostname, (Text) edi->port, srpc);
	srpc->enable_read_timeout(readTimeout);

	// Do multiple calls, each fetching about chunkSize bytes worth of
	// results.  The idea is that our client will probably return
	// false ("stop") after it has received that many bytes, so we
	// don't want to get more than that in a stream from the server.
	bool stop_req = false, dir_end = false;
	// Note that we buffer the results of each call *before* calling
	// the callback, as we don't want to hold up the server during the
	// callback's execution.
	ListResultSeq list_result;
	do {
	    srpc->start_call(ed_list, EVALUATOR_DIR_SRPC_VERSION);
	    srpc->send_bytes((const char*) &edi->dirhandle, sizeof(Bit64));
	    srpc->send_int(rawIndex); 
	    srpc->send_int(listChunkSize);
	    srpc->send_int(listPerEntryOverhead);
	    srpc->send_end();
	    
	    srpc->recv_seq_start();
	    while(!dir_end) {
	        ListResultItem list_item;
		int arclen = sizeof(list_item.arc);
		bool got_end;
		srpc->recv_chars_here(list_item.arc, arclen, &got_end);
		if (got_end) break;
		ed_entry_type edtype = (ed_entry_type) srpc->recv_int();
		list_item.filesid = (ShortId) srpc->recv_int();
		list_item.index = (rawIndex + 1) * 2;
		rawIndex++;
		list_item.pseudoInode = indexToPseudoInode(list_item.index);
		switch (edtype) {
		  case ed_none:
		    dir_end = true;
		  case ed_directory:
		    list_item.type = this->type;
		    break;
		  case ed_file:
		    list_item.type = VestaSource::immutableFile;
		    if (this->type == VestaSource::evaluatorROEDirectory) {
		        list_item.pseudoInode = list_item.filesid;
		    }
		    break;
		  case ed_device:
		    list_item.type = VestaSource::device;
		    break;
		}
		// Evaluator directory entries are always master.
		list_item.master = true;

		// Add this item to the result sequence unless this
		// was the terminating entry.
		if(!dir_end)
		  list_result.addhi(list_item);
	    }
	    srpc->recv_seq_end();
	    srpc->recv_end();

	    // Call the callback once per entry returned by the server
	    // until the callback tells us to stop or we run out.
	    while((list_result.size() > 0) && !stop_req)
	      {
		ListResultItem list_item = list_result.remlo();
	    
		curChunkSize += strlen(list_item.arc) + listPerEntryOverhead;

		stop_req = !callback(closure, list_item.type, list_item.arc,
				     list_item.index, list_item.pseudoInode,
				     list_item.filesid, list_item.master);
	      }

	    if(stop_req && listChunkSize > curChunkSize + 100)
	      {
		// Seems we guessed wrong; use smaller size now.
		listChunkSize = curChunkSize + 100;
	      }

	} while (!stop_req && !dir_end);

    } catch (SRPC::failure f) {
	char buf[100];
	Repos::pr_nfs_fh(buf, (nfs_fh*) &longid.value);
	Repos::dprintf(DBG_ALWAYS,
		       "VDirEvaluator::list of %s (%s:%s "
		       "0x%" FORMAT_LENGTH_INT_64 "x)"
		       " returns VestaSource::rpcFailure: %s (%d)\n", 
		       buf, edi->hostname, edi->port, edi->dirhandle, 
		       f.msg.cchars(), f.r);
	err = VestaSource::rpcFailure;
	*edi->alive = false;
    }
    multi->End(id);
    assert(VMemPool::type(rep) == VMemPool::vDirEvaluator);
    RECORD_TIME_POINT;
    return err;
}

void
VDirEvaluator::mark(bool byName, ArcTable* hidden) throw ()
{
  assert(VMemPool::type(rep) == VMemPool::vDirEvaluator);
  setVisited(true);
  // Mark cached children
  IndexToEntryIter iter(&edi->itab);
  IndexKey ik;
  EntryInfo* ei;
  while (iter.Next(ik, ei)) {
    if (ei->type == VestaSource::evaluatorDirectory ||
	ei->type == VestaSource::evaluatorROEDirectory) {
      VDirEvaluator child(VestaSource::evaluatorDirectory, ei->edi);
      child.mark();
    }
  }
}

void
VDirEvaluator::markCallback(void* closure, VMemPool::typeCode type) throw ()
{
    // Nothing to do
}

bool
VDirEvaluator::sweepCallback(void* closure, VMemPool::typeCode type,
			     void* addr, Bit32& size) throw ()
{
    VDirEvaluator vs(VestaSource::evaluatorDirectory, (Bit8*) addr);
    bool retain = vs.visited();
    vs.setHasName(false);
    vs.setVisited(false);
    size = VDIREV_SIZE;
    if (!retain) {
      ArcToEntryIter iter(&vs.edi->atab);
      ArcKey ak;
      EntryInfo* ei;
      while (iter.Next(ak, ei)) {
	free(ei->name);
	delete ei;
      }
      free(vs.edi->hostname);
      free(vs.edi->port);
      delete vs.edi;
    }
    return retain;
}

void
VDirEvaluator::rebuildCallback(void* closure, VMemPool::typeCode type,
			       void* addr, Bit32& size) throw ()
{
  // Compute size
  VDirEvaluator vs(VestaSource::evaluatorDirectory, (Bit8*) addr);
  size = VDIREV_SIZE;

  // Update cached pointer to this rep.  This is really needed only
  // after reading a checkpoint, but also (harmlessly) gets executed
  // after a mark and sweep.
  vs.edi->rep = vs.rep;
}

Bit32
VDirEvaluator::checkpoint(Bit32& nextSP, std::fstream& ckpt) throw ()
{
  if (visited()) return (Bit32) (PointerInt) repEDI();  // reused field
  assert(VMemPool::type(rep) == VMemPool::vDirEvaluator);

  // Checkpoint cached children
  IndexToEntryIter iter(&edi->itab);
  IndexKey ik;
  EntryInfo* ei;
  while (iter.Next(ik, ei)) {
    if (ei->type == VestaSource::evaluatorDirectory ||
	ei->type == VestaSource::evaluatorROEDirectory) {
      VDirEvaluator child(VestaSource::evaluatorDirectory, ei->edi);
      child.checkpoint(nextSP, ckpt);
    }
  }

  // Checkpoint this
  Bit32 newSP = nextSP;
  ckpt.write((char *) rep, VDIREV_SIZE);
  int pad = (-VDIREV_SIZE) & VMemPool::alignmentMask;
  nextSP += VDIREV_SIZE + pad;
  while (pad--) {
    ckpt.put(VMemPool::freeByte);
  }
  // Crash if writing to the checkpoint file is failing
  if (!ckpt.good()) {
    int errno_save = errno;
    Text err = Basics::errno_Text(errno_save);
    Repos::dprintf(DBG_ALWAYS, "write to checkpoint file failed: %s"
		   " (errno = %d)\n",
		   err.cchars(), errno_save);
    assert(ckpt.good());  // crash
  }
  setVisited(true);
  setRepEDI(reinterpret_cast<EvalDirInfo*>(newSP));  //reuse (smash) this field

  return newSP;
}

// Requires a write lock on the volatile tree this directory is in.
void
VDirEvaluator::freeTree() throw ()
{
    assert(VMemPool::type(rep) == VMemPool::vDirEvaluator);
    ArcToEntryIter iter(&edi->atab);
    ArcKey ak;
    EntryInfo* ei;
    while (iter.Next(ak, ei)) {
	if (ei->type == VestaSource::evaluatorDirectory ||
	    ei->type == VestaSource::evaluatorROEDirectory) {
	  VDirEvaluator vde(VestaSource::evaluatorDirectory, ei->edi);
	  vde.freeTree();
	}
	free(ei->name);
	delete ei;
    }
    VMemPool::free(rep, VDIREV_SIZE, VMemPool::vDirEvaluator);
    free(edi->hostname);
    free(edi->port);
    delete edi;
}

bool
VDirEvaluator::alive()
{
  if (!(*edi->alive)) return false;
  MultiSRPC::ConnId id = -1;
  try {
    SRPC *srpc;
    id = multi->Start((Text) edi->hostname, (Text) edi->port, srpc);
    if (!srpc->alive()) {
      multi->Discard(id);
      multi->Purge(edi->hostname, edi->port);
      *edi->alive = false;      
    }
  } catch (SRPC::failure f) {
    *edi->alive = false;
  }
  multi->End(id);
  return *edi->alive;
}

// Purge cached connections for talking to this evaluator
void
VDirEvaluator::purge()
{
  try
    {
      multi->Purge(edi->hostname, edi->port);
    }
  catch(const SRPC::failure &f)
    {
      Repos::dprintf(DBG_ALWAYS,
		     "VDirEvaluator::purge (client %s:%s) "
		     "caught SRPC::failure: %s (%d)\n", 
		     edi->hostname, edi->port, 
		     f.msg.cchars(), f.r);
    }
}

time_t
VDirEvaluator::timestamp() throw ()
{
  return edi->timestamp;
}

void VDirEvaluator::getClientHostPort(/*OUT*/ Text &hostname,
				      /*OUT*/ Text &port)
  const throw()
{
  hostname = edi->hostname;
  port = edi->port;
}
