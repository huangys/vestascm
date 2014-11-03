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

// WeederC -- the weeder client interface to the Vesta-2 cache server

/* The class "WeederC" provides a client connection to one or more running
   instances of the Vesta-2 cache server. It provides operations required by
   the Vesta-2 weeder. */

#ifndef _WEEDER_C_H
#define _WEEDER_C_H

#include <Basics.H>
#include <Table.H>
#include <SRPC.H>
#include <MultiSRPC.H>
#include <CacheIntf.H>
#include <BitVector.H>
#include <PKPrefix.H>

// for use by "EndMark" method below
typedef Table<PKPrefix::T,char>::Default PKPrefixTbl;
typedef Table<PKPrefix::T,char>::Iterator PKPrefixIter;

class WeederC {
  public:
    // Constructor: initialize the client
    WeederC(CacheIntf::DebugLevel debug = CacheIntf::None) throw (SRPC::failure);

    // Destructor: close and free any open connections to servers
    ~WeederC() throw ();

    bool WeederRecovering(bool doneMarking) const throw (SRPC::failure);
    /* Indicate that the weeder is in the process of recovering, and set the
       cache server's state accordingly. "doneMarking" should be true iff the
       weeder's "weeded" set is non-empty, i.e., if the weeder had completed
       its mark phase but not the subsequent deletion phase. Returns true
       iff there is already a weeder running, in which case no action is
       taken and the weeder should immediately abort. */

    BitVector* StartMark(/*OUT*/ int &newLogVer) const throw (SRPC::failure);
    /* Indicate that the weeder is starting its mark phase. This blocks if the
       cache server is still in the middle of doing deletions from a previous
       weed. Then it disables lease expiration, and flushes the graphLog.
       It then checkpoints the graph log, and sets "newLogVer" to the version
       number of the new graphLog file. The weeder should read all versions up
       to (but not including) "newLogVer". Finally, this method returns the
       bit vector of cache indices in use at the time of the call. */

    void SetHitFilter(const BitVector &cis) const throw (SRPC::failure);
    /* Set the cache server's hit filter set to "cis". The hit filter is the
       set of cache entries that may soon be deleted, so hits on them by the
       cache server's "Lookup" method will be disabled. This method requires
       that the cache server is not currently doing deletions from a previous
       weed. */

    BitVector* GetLeases() const throw (SRPC::failure);
    /* Return a newly allocated bit vector corresponding to the cache entries
       that are currently leased. */

    void ResumeLeaseExp() const throw (SRPC::failure);
    /* Re-enable lease expiration disabled by the "StartMark" method above. */

    int EndMark(const BitVector &cis, const PKPrefixTbl &pfxs) const
      throw (SRPC::failure);
    /* Indicate that the weeder's mark phase has completed, and that "cis" are
       the indices of the cache entries to delete. The list "pfxs" contains
       the union of the prefixes of the PK's of all these entries.

       The cache server's hit filter is set to "cis", and the cache server
       will fork a background thread to delete these  entries. The number of
       the checkpoint file into which the weeder should write the ``clean''
       version of the graphLog is returned. */

    bool CommitChkpt(const Text &chkptFileName) const throw (SRPC::failure);
    /* Inform the cache server that the weeder has finished writing the
       graphLog checkpoint, and that the checkpoint should be committed.

       Pass the file to which we've written the graphLog checkpoint,
       which is the "real" checkpoint name plus a suffix to make it
       unique.  This file is renamed to take the place of the actual
       checkpoint.  This guards against a case where two weeders are
       both trying to checkpoint the graph log of the same cache
       server.

       The return value indicates whether the cache server accepted
       the checkpoint.  Unless it wasn't expecting one, it will. */

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
    void NullCall(CacheIntf::ProcIds id, char *name,
      CacheIntf::DebugLevel level) const throw (SRPC::failure);
    /* Make the remote call to procedure "id" with no arguments or return
       value. */

    // hide copy constructor from clients
    WeederC(const WeederC&);

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

#endif // _WEEDER_C_H
