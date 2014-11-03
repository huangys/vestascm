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

// Last modified on Mon May 23 22:30:48 EDT 2005 by ken@xorian.net  
//      modified on Wed Feb 23 17:09:29 PST 2000 by mann  
//      modified on Sat Aug 22 15:47:18 PDT 1998 by heydon

#ifndef _EMPTY_PK_LOG_H
#define _EMPTY_PK_LOG_H

#include <Basics.H>
#include <Table.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <FP.H>
#include <CacheIntf.H>
#include <PKEpoch.H>

/* Description:

   An "EmptyPKLog" is a log of "(pk, pkEpoch)" pairs for recording which
   PKFile's have become empty and their corresponding "pkEpoch" values.
   In the on-disk log, the successive "pkEpoch" values associated with any
   given PK appear in strictly monotonically increasing order.

   An "EmptyPKLog" also keeps an in-memory table corresponding to the on-disk
   pairs; when the log contains multiple pairs with the same "pk", the table
   stores the *maximum* "pkEpoch" associated with that "pk".

   Usage:

   The EmptyPKLog is read during recovery. Its purpose is to prevent cache
   entries in the cache log from being recovered that belonged to PKFiles that
   have since been deleted. If a cache entry's "pkEpoch" is less than the
   corresponding "pkEpoch" in the EmptyPKLog, or if it is less than the
   "pkEpoch" of the corresponding SPKFile in the stable cache, the cache entry
   can be ignored. The same technique is used to delete cache entries from the
   cache log when the cache log is cleaned.

   When a new VPKFile is created and no corresponding SPKFile is found for it,
   the empty PK table is consulted to see if the new VPKFile should be given a
   non-zero "pkEpoch" (this is necessary because there may still be entries in
   the cache log for "pk" that would be erroneously recovered if the VPKFile's
   epoch was initialized to 0).

   When all of the entries are deleted from a PKFile, a new "(pk, pkEpoch)"
   pair is written to the EmptyPKLog and added to the table for that PKFile.

   Note:
   
   A pair is written to the log whenever a PKFile becomes empty, but cache
   entries might subsequently be created for that PKFile. Hence, the PKFiles
   corresponding to the "pk" values in the EmptyPKLog are not guaranteed to be
   empty. Rather, the log contains the "pkEpoch" values corresponding to
   PKFiles that were once known to be empty. The stable cache must still be
   consulted to confirm that the PKFile is actually empty.
*/

class EmptyPKLog {
  public:
    // dictionary types
    typedef Table<FP::Tag,PKFile::Epoch>::Default PKEpochTbl;
    typedef Table<FP::Tag,PKFile::Epoch>::Default PKEpochIter;

    EmptyPKLog(Basics::mutex *mu, CacheIntf::DebugLevel, bool readonly = false)
      throw (VestaLog::Error);
    /* REQUIRES Sup(LL) = CacheS.mu */
    /* Allocate the private log and open it in the ``recovering'' state. The
       corresponding empty PK table is initially empty. The mutex "mu" should
       be the centralized lock "CacheS.mu". If "readonly" is true, the log
       is opened in read-only mode. */

    ~EmptyPKLog() throw () { this->log->close(); }
    /* Close the private log. */

    // Recovery methods -------------------------------------------------------

    /* These methods require "CacheS.mu" because the intention is for the
       client to acquire the mutex at the start of recovery and release it
       when recovery is complete. */

    bool EndOfFile() throw (VestaLog::Error) { return this->rd->eof(); }
    /* REQUIRES Sup(LL) = CacheS.mu && ``private log in recovering state'' */
    /* Return true iff the log is at end of file. */

    bool NextLog() throw (VestaLog::Error) { return this->log->nextLog(); }
    /* REQUIRES Sup(LL) = CacheS.mu && ``private log in recovering state'' */
    /* Return true iff there is another log to recover. */

    void Read(/*OUT*/ FP::Tag &pk, /*OUT*/ PKFile::Epoch &pkEpoch)
      throw (VestaLog::Error, VestaLog::Eof);
    /* REQUIRES Sup(LL) = CacheS.mu && ``private log in recovering state'' */
    /* Read the next entry in the log, setting "pk" and "pkEpoch"
       to the values read. The pair is also added to this EmptyPKLog's
       in-memory table. */

    void EndRecovery() throw (VestaLog::Error)
      { this->rd = (RecoveryReader *)NULL; this->log->loggingBegin(); }
    /* REQUIRES Sup(LL) = CacheS.mu && ``private log in recovering state'' */
    /* Switch the private log from the ``recovering'' state to the
       ``ready'' state. */

    // Checkpoint methods -----------------------------------------------------

    void CheckpointBegin()
      throw (FS::Failure, VestaLog::Error);
    /* REQUIRES Sup(LL) < CacheS.mu && ``private log in ready state'' */
    /* Write an empty checkpoint for the log, and move the corresponding
       in-memory table to be oldEmptyPKTable.  When the checkpoint is
       complete, the "CheckpointEnd" method below should be called to commit
       it, which will delete oldEmptyPKTable. In the interim, other threads
       can still append new entries to the log, and lookups will look both in
       emptyPKTable and oldEmptyPKTable. */

    void CheckpointEnd() throw (VestaLog::Error);
    /* REQUIRES Sup(LL) < CacheS.mu && ``private log in ready state'' */
    /* Atomically commit the current checkpoint. */

    // Writing methods --------------------------------------------------------

    void Write(const FP::Tag &pk, PKFile::Epoch pkEpoch)
      throw (VestaLog::Error);
    /* REQUIRES Sup(LL) < CacheS.mu && ``private log in ready state'' */
    /* Atomically append the pair "(pk, pkEpoch)" to the private log. The pair
       is also added to the empty PK table. The "pkEpoch" is required to be
       at least as large as any existing entry for this "pk"; if "pkEpoch"
       is equal to the existing entry, this method is a no-op. */ 

    // Empty PK Table methods -------------------------------------------------

    bool GetEpoch0(const FP::Tag &pk, /*OUT*/ PKFile::Epoch &pkEpoch) throw ();
    /* REQUIRES Sup(LL) = CacheS.mu */
    /* If the empty PK table includes an entry for "pk", set "pkEpoch" to the
       corresponding epoch and return "true". Otherwise, leave "pkEpoch"
       unchanged and return "false". */

    bool GetEpoch(const FP::Tag &pk, /*OUT*/ PKFile::Epoch &pkEpoch) throw ();
    /* REQUIRES Sup(LL) < CacheS.mu */
    /* Like "GetEpoch0" above, but with a different locking level. */

  private:
    // read-only after init
    CacheIntf::DebugLevel debug; // cache server's debugging level

    // data fields
    Basics::mutex *mu;       // actually points to "CacheS.mu"
    VestaLog *log;
    RecoveryReader *rd;      // only non-NULL during recovery
    PKEpochTbl *oldEmptyPKTbl;
    PKEpochTbl *emptyPKTbl;

    /* Invariant: "oldEmptyPKTbl + emptyPKTbl" (where + is overlay,
       as in the Vesta SDL) is the set of pairs "(pk, pkEpoch)" such
       that:

         (pk, pkEpoch) \in log /\
         pkEpoch = max( (pk, pkEpoch') \in log : pkEpoch' )

       A similar invariant is true for oldEmptyPKTbl and the portion
       of the log preceding the start of the most recent checkpoint.
    */
};

#endif // _EMPTY_PK_LOG_H
