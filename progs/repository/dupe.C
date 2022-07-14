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
// dupe.C
//
// ONC RPC duplicate suppression:
// (1) Detects when an RPC we are still processing is retransmitted
//     and drops the retransmission on the floor.
// (2) Detects when an RPC we recently replied to is retransmitted
//     and retransmits the reply from its cache.
//

#if __linux__
#include <stdint.h>
#endif
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>

#include <Basics.H>
#include <VestaConfig.H>

#include "dupe.H"
extern "C" {
#include "svc_udp.h"
}
#include "logging.H"

#include "ReposConfig.h"

//
// Module types
//
class RPCKey {
  public:
    unsigned int xid;
    struct sockaddr_in caller;
    Word Hash() const throw() {
	return ((Word) xid) ^ ((Word) caller.sin_port)
	  ^ ((Word) caller.sin_addr.s_addr);
    };
    inline RPCKey() { };
    inline RPCKey(unsigned int xid_, struct sockaddr_in& caller_) {
	xid = xid_; caller = caller_;
    };
    friend int operator==(RPCKey k1, RPCKey k2) {
	return k1.xid == k2.xid && k1.caller.sin_port == k2.caller.sin_port &&
	    k1.caller.sin_addr.s_addr == k2.caller.sin_addr.s_addr;
    };
};
 
class RPCValue {
  public:
    RPCKey key;
    char* reply; // if NULL, this request is still being processed
    int replylen;
    RPCValue* older;
    RPCValue* newer;
    inline RPCValue() { };
    inline RPCValue(unsigned int xid_, struct sockaddr_in& caller_,
		    char* reply_, int replylen_)
      : key(xid_, caller_)
    {
	reply = reply_; replylen = replylen_;
	older = NULL; newer = NULL;
    };
};

typedef Table<RPCKey, RPCValue*>::Default RPCTable;

class DupeTable
{
private:
  Basics::mutex mu;
  RPCTable dupeTable;
  int dupeTableCount;
  RPCValue* oldestDupe;
  RPCValue* newestDupe;
  ReposStats::DupeStats dupeStats;

public:
  DupeTable()
    : dupeTableCount(0), oldestDupe(NULL), newestDupe(NULL)
  {
  }

  bool new_rpc(SVCXPRT* xprt, struct rpc_msg* msg);

  void completed_rpc(SVCXPRT* xprt, struct rpc_msg* msg, int replylen);

  inline ReposStats::DupeStats get_stats()
  {
    ReposStats::DupeStats result;
    this->mu.lock();
    result = this->dupeStats;
    this->mu.unlock();
    return result;
  }

private:
  // Hide copy constructor, which should never be used.
  DupeTable(const DupeTable &);
      
};

//
// Module globals
//
static pthread_once_t once = PTHREAD_ONCE_INIT;

// Maximum number of past responses to keep.
static unsigned int dupeTableMax = 20;

// Number of bits from the client IP address and RPC XID so use in
// chosing the DupeTable to use.  There will be
// 2^(dupe_hash_ip_bits+dupe_hash_xid_bits) tables.
static unsigned int dupe_hash_ip_bits = 0, dupe_hash_xid_bits = 2;
static unsigned char dupe_hash_ip_mask, dupe_hash_xid_mask;

static unsigned int dupeTableCount;
DupeTable *dupeTables = NULL;

//
// One-time initialization
//
extern "C"
{
  static void
  dupe_init()
  {
    Text value;
    if (VestaConfig::get("Repository", "dupe_table_max", value)) {
	dupeTableMax = atoi(value.cchars());
    }
    if (VestaConfig::get("Repository", "dupe_hash_ip_bits", value)) {
	dupe_hash_ip_bits = atoi(value.cchars());
	assert(dupe_hash_ip_bits <= 8);
    }
    if (VestaConfig::get("Repository", "dupe_hash_xid_bits", value)) {
	dupe_hash_xid_bits = atoi(value.cchars());
	assert(dupe_hash_xid_bits <= 8);
    }

    // Generate masks for the requested number of bits of IP and XID.
    dupe_hash_ip_mask = 0;
    for(unsigned int i = 0; i < dupe_hash_ip_bits; i++)
      {
	dupe_hash_ip_mask |= (1 << i);
      }
    dupe_hash_xid_mask = 0;
    for(unsigned int i = 0; i < dupe_hash_xid_bits; i++)
      {
	dupe_hash_xid_mask |= (1 << i);
      }

    // Compute the number of duplicate suppression tables we will
    // have.
    dupeTableCount = (1 << (dupe_hash_ip_bits+dupe_hash_xid_bits));
    assert(dupeTableCount > 0);
    assert((dupe_hash_ip_mask | (dupe_hash_xid_mask << dupe_hash_ip_bits)) <
	   dupeTableCount);

    dupeTables = NEW_ARRAY(DupeTable, dupeTableCount);
  }
}

static unsigned int dupeTableIndex(unsigned int xid,
				   const struct sockaddr_in &caller)
{
  // Which bits of the XID change more rapidly may depend on the byte
  // order of the sender.  To try to get a small number of
  // rapidly-changing bits from the XID, XOR the byte of the XID
  // together.
  unsigned char xid_byte = xid;
  xid_byte ^= (xid >> 8);
  xid_byte ^= (xid >> 16);
  xid_byte ^= (xid >> 24);
  assert(sizeof(xid) == 4);

  // We use the low byte of the IP address, as those bits are more
  // likely to be different between clients.  (Note: assumes IPv4!)
  unsigned char ip_byte = ntohl(caller.sin_addr.s_addr);

  // Mask off unwanted bits.
  xid_byte &= dupe_hash_xid_mask;
  ip_byte &= dupe_hash_ip_mask;

  // Concatenate together the bits from the XID and the IP address.
  unsigned int result = ((((unsigned int) xid_byte) << dupe_hash_ip_bits) |
			 ((unsigned int) ip_byte));

  assert(result < dupeTableCount);
  return result;
}

// On some platforms the type od the address is "struct sockaddr_in",
// on others it's "char [16]".  This overloaded function helps us
// convert automatically in the latter case.

static inline struct sockaddr_in &as_sockaddr_in(struct sockaddr_in &addr)
{
  return addr;
}
static inline struct sockaddr_in &as_sockaddr_in(char *addr)
{
  return *((struct sockaddr_in *) addr);
}

//
// If the RPC is a duplicate, reply to it and return false.  If not,
// note that the RPC is in progress and return true.  In the latter
// case the caller must invoke completed_rpc after replying to the
// call.
//
extern "C" int
new_rpc(SVCXPRT* xprt, struct rpc_msg* msg)
{
  pthread_once(&once, dupe_init);

  unsigned int table_index =
    dupeTableIndex(msg->rm_xid, as_sockaddr_in((struct sockaddr_in&)(xprt->xp_raddr)));
  assert(dupeTables != NULL);
  return dupeTables[table_index].new_rpc(xprt, msg);
}

bool DupeTable::new_rpc(SVCXPRT* xprt, struct rpc_msg* msg)
{
  RPCValue* oldv;
  RPCValue* newv = NEW_CONSTR(RPCValue,
			      (msg->rm_xid, as_sockaddr_in((struct sockaddr_in&)(xprt->xp_raddr)),
			       NULL, 0));
  this->mu.lock();
  bool isDupe = this->dupeTable.Get(newv->key, oldv);
  if (isDupe) {
    delete newv;
    if (oldv->reply == NULL) {
      // Currently in process; drop request on the floor
      this->dupeStats.inProcess++;
    } else {
      // Already done; retransmit reply
      this->dupeStats.completed++;
      (void) sendto(
#if defined(SVCXPRT_XP_SOCK)
		    xprt->xp_sock
#elif defined(SVCXPRT_XP_FD)
		    xprt->xp_fd
#else
#error Unknown SVCXPRT socket field
#endif
		    , oldv->reply, oldv->replylen,
		    0, (struct sockaddr*) &xprt->xp_raddr,
		    xprt->xp_addrlen);
      // Remove from LRU list
      if (oldv->older == NULL) {
	assert(this->oldestDupe == oldv);
	this->oldestDupe = oldv->newer;
      } else {
	oldv->older->newer = oldv->newer;
      }
      if (oldv->newer == NULL) {
	assert(this->newestDupe == oldv);
	this->newestDupe = oldv->older;
      } else {
	oldv->newer->older = oldv->older;
      }
      // Add to "newest" end
      oldv->newer = NULL;
      oldv->older = this->newestDupe;
      this->newestDupe = oldv;
      if (oldv->older == NULL) {
	this->oldestDupe = oldv;
      } else {
	assert(oldv->older->newer == NULL);
	oldv->older->newer = oldv;
      }
    }
  } else {
    // New request; remember it's in process
    this->dupeStats.non++;
    (void) this->dupeTable.Put(newv->key, newv);
  }
  this->mu.unlock();
  return !isDupe;
}

//
// Note that this RPC is completed and cache its result, so that
// new_rpc will retransmit the result if we receive a retransmitted
// call.
//
extern "C" void
completed_rpc(SVCXPRT* xprt, struct rpc_msg* msg, int replylen)
{
  pthread_once(&once, dupe_init);

  unsigned int table_index =
    dupeTableIndex(msg->rm_xid, as_sockaddr_in((struct sockaddr_in&)(xprt->xp_raddr)));
  assert(dupeTables != NULL);

  dupeTables[table_index].completed_rpc(xprt, msg, replylen);
}

void DupeTable::completed_rpc(SVCXPRT* xprt, struct rpc_msg* msg, int replylen)
{
  RPCKey k(msg->rm_xid, as_sockaddr_in((struct sockaddr_in&)(xprt->xp_raddr)));
  RPCValue* v;
  this->mu.lock();

  bool inProcess = this->dupeTable.Get(k, v);
  assert(inProcess);
  assert(v->reply == NULL);

  // If list is full, delete oldest element, but keep reply buffer
  char* savedbuf = NULL;
  if (this->dupeTableCount >= dupeTableMax) {
    assert(this->oldestDupe != NULL);  // we just added something!
    RPCValue* dying = this->oldestDupe;
    this->oldestDupe = this->oldestDupe->newer;
    if (this->oldestDupe == NULL) {
      this->newestDupe = NULL;
    } else {
      this->oldestDupe->older = NULL;
    }
    savedbuf = dying->reply;
    RPCValue* junk;
    this->dupeTable.Delete(dying->key, junk, false);
    assert(junk == dying);
    delete dying;
  } else {
    this->dupeTableCount++;
  }

  // Save this reply
  v->reply = (char *) rpc_buffer(xprt);
  v->replylen = replylen;
  if (savedbuf == NULL) {
    rpc_buffer(xprt) = (char*) malloc(su_data(xprt)->su_iosz);
  } else {
    rpc_buffer(xprt) = savedbuf;
  }
  xdrmem_create(&(su_data(xprt)->su_xdrs), (char *) rpc_buffer(xprt),
		su_data(xprt)->su_iosz, XDR_ENCODE);

  // Add to "newest" end of LRU list
  v->newer = NULL;
  v->older = this->newestDupe;
  this->newestDupe = v;
  if (v->older == NULL) {
    this->oldestDupe = v;
  } else {
    assert(v->older->newer == NULL);
    v->older->newer = v;
  }

  this->mu.unlock();
}

ReposStats::DupeStats get_dupe_stats() throw()
{
  ReposStats::DupeStats result;
  for(unsigned int index = 0; index < dupeTableCount; index++)
    {
      result += dupeTables[index].get_stats();
    }
  return result;
}
