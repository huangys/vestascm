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

// CacheC -- the evaluator client interface to the Vesta-2 cache server

// The class "CacheC" provides a client connection to one or more running
// instances of the Vesta-2 cache server. It provides operations required by
// the Vesta-2 evaluator.

// Once a new "CacheC" object is initialized, its methods are thread-safe.
// That is, they can safely be called by multiple threads at once.

#ifndef _CACHE_C_H
#define _CACHE_C_H

// from vesta/srpc
#include <SRPC.H>
#include <MultiSRPC.H>

// from vesta/fp
#include <FP.H>

// from vesta/cache-common
#include <CacheIntf.H>
#include <BitVector.H>
#include <FV.H>
#include <CacheIndex.H>
#include <Model.H>
#include <VestaVal.H>
#include <FV2.H>
#include <PKPrefix.H>
#include <CompactFV.H>

class CacheC {
  public:
    // Constructor: initialize the client
    CacheC(CacheIntf::DebugLevel debug = CacheIntf::None)
      throw (SRPC::failure);

    // Destructor: close and free any open connections to servers
    ~CacheC() throw ();

    FV::Epoch FreeVariables(const FP::Tag& pk, /*OUT*/ CompactFV::List& names,
      /*OUT*/ bool &isEmpty) const throw (SRPC::failure);
    /* Set "names" to the list of the union of free variables associated with
       cache entries under the primary key "pk". Return the epoch of this set
       of names, which should be passed to the "Lookup" method below. If the
       cache is guaranteed not to contain any entries for "pk", then "isEmpty"
       is set to "true", in which case "names" will be empty and a subsequent
       "Lookup" would be pointless. "isEmpty" is set to "false" otherwise. */

    CacheIntf::LookupRes Lookup(
      const FP::Tag& pk, const FV::Epoch id, const FP::List& fps,
      /*OUT*/ CacheEntry::Index& ci, /*OUT*/ VestaVal::T& value)
      const throw (SRPC::failure);
    /* Look up the cache entry with primary key "pk", free variables set with
       epoch "id", and corresponding value fingerprints "fps". 

       In the event of a hit, "Hit" is returned, a lease is taken out (or
       renewed) on the matching cache entry, "ci" is set to the index of that
       entry, and "value" is set to its result value. For all other return
       values, "ci" and "value" are unchanged. 

       In the event of a cache miss, "Miss" is returned. In the event that
       "id" is an old epoch for "pk", "FVMismatch" is returned. In this case,
       the caller should call the "FreeVariables" method again to get the
       latest epoch and names associated with "pk", and then try "Lookup"
       again.

       If "id" corresponds to the most recent epoch, but the length of "fss"
       does not agree with with length of the "names" list returned by the
       most recent call to "FreeVariables", then "CacheIntf::BadLookupArgs"
       is returned. This return result indicates a programming error on the
       part of the client. */

    CacheIntf::AddEntryRes AddEntry(
      const FP::Tag& pk, const char *types, const FV2::List& names,
      const FP::List& fps, const VestaVal::T& value, const Model::T model,
      const CacheEntry::Indices& kids, const Text& sourceFunc,
      /*OUT*/ CacheEntry::Index& ci) throw (SRPC::failure);
    /* Add a new entry to the cache under the primary key "pk" corresponding
       to the evaluation of some function; "types" and "names" together
       specify the set of names that are free during the evaluation of the
       function, and "fps" are the fingerprints of the values associated with
       those names. "types" must be non-NULL.

       The "i"th free name is formed by prepending the character "types[i]"
       (treated as a text) to the names in the sequence "names[i]". It is an
       unchecked run-time error if the "types" array has size less than
       "names.len". If "names.len != fps.len", or if "names" contains any
       duplicate names, "CacheIntf::BadAddEntryArgs" is immediately returned;
       this return result indicates a programming error on the part of the
       client.

       "value" is the value produced by the evaluation of the function,
       "model" is the model in which the function is defined, and "kids" are
       the indices of the cache entries corresponding to function evaluations
       performed directly on behalf of this evaluation. Finally, "sourceFunc"
       is a text denoting the source location of the function definition
       for the new cache entry.

       In the event of success, "CacheIntf::EntryAdded" is returned, a lease
       is taken out on the new cache entry, and "ci" is set to the index of
       the new cache entry. 

       If any of the "kids" lacks a lease, then "CacheIntf::NoLease" is
       returned, and the value of "ci" is unchanged. */

    void Checkpoint(const FP::Tag &pkgVersion, Model::T model,
      const CacheEntry::Indices& cis, bool done) throw (SRPC::failure);
    /* Make stable all cache entries and deriveds reachable from the entries
       corresponding to "cis". If the cache entries corresponding to "cis" all
       have leases, then also write a "Root" entry to the graph log that
       protects them from weeding. The arguments "pkgVersion" and "model"
       identify the top-level model on which the evaluator was invoked:
       "pkgVersion" is the fingerprint of the immutable directory in which the
       model resides, and "model" is the ShortId of the model file itself. The
       "done" argument should be true if the checkpoint is for a completed
       model evaluation, false if it represents an intermediate checkpoint.
       The checkpoint is done synchronously iff "done" is true. */

    bool RenewLeases(const CacheEntry::Indices& cis)
      throw (SRPC::failure);
    /* Renew the leases on the cache entries with indexes "cis". Returns true
       if all the cache entries named by "cis" exist and all currently have
       valid leases; false otherwise. Even if false is returned, the leases of
       all existing cache entries with valid leases named in "cis" will have
       had their leases renewed. */

  protected:
    // The client maintains a cache of connections in a "Connections" object
    MultiSRPC *conns;
    // The port we talk to the server on
    Text serverPort;
    
    // Read-only after initialization
    CacheIntf::DebugLevel debug;
    
    // The cache server instance identifier.  Read-only after
    // initialization
    FP::Tag server_instance;

  private:
    // hide copy constructor from clients
    CacheC(const CacheC&);

    // Intialize the server instance fingerprint.  Should only called
    // by constructors.
    void init_server_instance() throw(SRPC::failure);

    // Recieve the server instance check response for an SRPC call
    // guarded by such a check.  Either return to indicate that the
    // call should continue, or throw SRPC::failure to indicate that a
    // server mismatch.
    void recv_server_instance_check(SRPC *srpc, const char *func_name)
      const throw(SRPC::failure);
};

#endif // _CACHE_C_H
