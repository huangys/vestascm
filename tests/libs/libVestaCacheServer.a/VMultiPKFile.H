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

// Last modified on Mon May 23 22:52:04 EDT 2005 by ken@xorian.net
//      modified on Fri Sep 18 16:30:19 PDT 1998 by heydon

// VMultiPKFile -- a set of VPKFile's stored in memory

/* A "VMultiPKFile" holds all "VPKFile"'s in the cache with a common prefix.
   The size of the prefix is determined by the current MultiPKFile
   granularity; see "SMultiPKFile.H". When one "VPKFile" is flushed to the
   stable cache, the "VMultiPKFile" containing is used to see which other
   "VPKFile"'s in the same MultiPKFile on disk can also be rewritten.

   The methods exported by a "VMultiPKFile" object have been designed to allow
   a high degree of concurrency within the cache server. The only restriction
   they introduce is that a MultiPKFile cannot be rewritten by more than one
   thread at a time. See the locking level specifications for each method. */

#ifndef _V_MULTI_PK_FILE_H
#define _V_MULTI_PK_FILE_H

#include <Basics.H>
#include <FS.H>
#include <VestaLog.H>
#include <FP.H>
#include <PKPrefix.H>
#include <BitVector.H>
#include <CacheState.H>
#include "EmptyPKLog.H"
#include "VPKFile.H"
#include "SMultiPKFile.H"

class VMultiPKFile {
  public:
    VMultiPKFile(const PKPrefix::T &pfx) throw ()
    : prefix(pfx), freeMPKFileEpoch(-1), numWaiting(0), numRunning(0),
      numNewEntries(0), autoFlushPending(false) { /*SKIP*/ }
    /* Return a new, empty "VMultiPKFile", all of whose "VPKFile"'s
       will share the same prefix "pfx". */

    bool Get(const FP::Tag &pk, /*OUT*/ VPKFile* &vpk) throw ()
      { return tbl.Get(pk, /*OUT*/ vpk); }
    /* REQUIRES Sup(LL) = CacheS.mu */
    /* If there is an entry in this "VMultiPKFile" under "pk", set "vpk" to
       the associated value and return "true"; otherwise, return false. */

    bool Put(const FP::Tag &pk, VPKFile *vpk) throw ()
      { return tbl.Put(pk, vpk); }
    /* REQUIRES Sup(LL) = CacheS.mu */
    /* Add "vpk" to this "VMultiPKFile" under the key "pk". Return "true" iff
       there was already an entry in this "VMultiPKFile" under key "pk". */

    bool Delete(const FP::Tag &pk, VPKFile *&vpk) throw ()
      { return tbl.Delete(pk, vpk); }
    /* REQUIRES Sup(LL) = CacheS.mu */
    /* Remove the key "pk" from this VMultiPKFile under the key
       "pk". Return "true" iff there was an entry in this VMultiPKFile
       under key "pk".  If there was an entry with key "pk", set "vpk"
       to the associated value. */

    int NumVPKFiles() const throw ()
      { return tbl.Size(); }
    /* REQUIRES Sup(LL) = CacheS.mu */
    /* Returns the number of VPKFiles contained by this
       VMultiPKFile. */

    const SMultiPKFile::VPKFileMap & VPKFileTbl() const throw()
    /* REQUIRES Sup(LL) = CacheS.mu */
    /* Returns the table of VPKFiles contained by this VMultiPKFile.
       Note that it is unsafe to access this table without holding
       CacheS.mu. */
      { return tbl; }

    void IncEntries() throw () { this->numNewEntries++; }
    /* REQUIRES Sup(LL) = CacheS.mu */
    /* Increment the count of total entries in this MultiPKFile. The count is
       used by "IsFull" to decide if the MPKFile should be flushed to the
       stable cache. */

    bool IsFull() throw ();
    /* REQUIRES Sup(LL) = CacheS.mu */
    /* If the number of entries in this MPKFile exceeds some threshold and
       some thread is not already pending to flush it, return true and set
       state indicating that a thread is pending to flush the MPKFile to the
       stable cache. Otherwise, return false. */

    bool LockForWrite(Basics::mutex &mu, const BitVector *toDelete)
      throw();
    /* REQUIRES Sup(LL) = CacheS.mu */
    /* The first argument "mu" should be "CacheS.mu". It is the mutex
       used to protect the internal fields of a VMultiPKFile.

       The return value indicates whether to proceed with flushing
       this "MultiPKFile" to disk.  If a write should take place, this
       function will have acquired a lock which ensures that this
       thread is the exclusive writer of this MultiPKFile.  (If
       another thread is already re-writing the same MultiPKFile, this
       function will block.)  If the return value is true, the caller
       is responsible for calling either ChkptForRewrite followed by
       ToSCache (to complete the write) or ReleaseWriteLock (in case
       of an error prior to writing). */

    bool ChkptForWrite(Basics::mutex &mu, const BitVector *toDelete,
		       /*OUT*/ SMultiPKFile::VPKFileMap &toFlush,
		       /*OUT*/ SMultiPKFile::ChkPtTbl &vpkChkptTbl)
      throw();
    /* REQUIRES (FORALL vf: VPKFile :: Sup(LL) < vf.mu) */
    /* REQUIRES Sup(LL) = CacheS.mu */
    /* The first argument "mu" should be "CacheS.mu". It is the mutex
       used to protect the internal fields of a VMultiPKFile.

       Continue perparations to flush to the stable cache all
       VPKFile's added to this "MultiPKFile" since the last time it
       was flushed, and update the corresponding VPKFile's in the
       volatile cache.

       The return value indicates whether a write should take place.
       The caller must have previously called LockForWrite (above) and
       it must have also indicated that a write should take place.  If
       the return value is true, the caller is responsible for calling
       either ToSCache (to complete the write) or ReleaseWriteLock (in
       case of an error prior to writing).

       The argument "toFlush" is filled with a map of all the VPKFiles
       to be written to the stable MultiPKFile.  "vpkChkptTbl" is
       filled with in-memory checkpoints of the current contents of
       these VPKFiles.  These capture the exact set of entries which
       will be written to the stable cache.  This must be done
       *before* flushing the graph log, since new entries may be added
       concurrently with the re-writing process and new entries *must*
       be written to the graph log before being written to the stable
       cache. */

    void ReleaseWriteLock(Basics::mutex &mu) throw();
    /* Release the write lock acquired by PrepareForWrite (above).
       This should be called if an operation between PrepareForWrite
       and ToSCache (below) fails.  (For instance, if flushing the
       graph log fails.) */

    void ToSCache(Basics::mutex &mu,

		  // From SMultiPKFile::PrepareForRewrite
		  bool mpkFileExists, std::ifstream &ifs,
		  SMultiPKFileRep::Header *hdr,

		  // From ChkptForWrite above
		  SMultiPKFile::VPKFileMap &toFlush,
		  SMultiPKFile::ChkPtTbl &vpkChkptTbl,

		  const BitVector *toDelete,
		  EmptyPKLog *emptyPKLog, /*INOUT*/ EntryState &state)
      throw (FS::Failure, FS::EndOfFile, VestaLog::Error);
    /* REQUIRES (FORALL vf: VPKFile :: Sup(LL) < vf.mu) */
    /* The first argument "mu" should be "CacheS.mu". It is the mutex used to
       protect the internal fields of a VMultiPKFile.

       The second, third, and fourth arguments must be acquired by
       calling SMultiPKFile::PrepareForRewrite.  (These are passed
       directory on to SMultiPKFile::Rewrite.)

       The fifth and sixth arguments must be acquired by calling
       ChkptForWrite (above).

       Flush to the stable cache all VPKFile's added to this "MultiPKFile"
       since the last time it was flushed, and update the corresponding
       VPKFile's in the volatile cache. If "toDelete" is non-NULL, then any
       cache entries in this MultiPKFile with indices in "toDelete" are
       deleted.

       This is a potentially long-running operation. Since only one
       thread at a time can be re-writing a MultiPKFile, the caller
       must first call PrepareForWrite which will block if some other
       thread is already in the process of doing so. When the
       operation is complete, the state set by "IsFull" indicating
       that a thread is pending to flush the MultiPKFile is cleared. */

    void UpdateEpoch(int currentEpoch) throw ()
    /* REQUIRES Sup(LL) = CacheS.mu */
      { this->freeMPKFileEpoch = currentEpoch; }

    bool IsStale(int latestEpoch) throw ();
    /* REQUIRES Sup(LL) = CacheS.mu */
    /* Return "true" iff this VMultiPKFile needs to be flushed by the
       background thread to free its memory because its last time of
       use was before "latestEpoch". */

    inline const PKPrefix::T &Prefix() const throw()
    /* REQUIRES Sup(LL) = CacheS.mu */
      { return prefix; }

    bool IsUnmodified() const throw ()
      { return this->freeMPKFileEpoch == -1; }
    /* REQUIRES Sup(LL) = CacheS.mu */
    /* Return "true" iff this VMultiPKFile has not had any entries
       added since it was last flushed (or created). */

    bool FlushRunning() const throw()
      { return (this->numRunning > 0); }
    /* REQUIRES Sup(LL) = CacheS.mu */
    /* Return "true" iff this VMultiPKFile is currently being flushed
       to the stable cache. */

    bool FlushPending() const throw()
      {	return ((this->numWaiting > 0) || autoFlushPending); }
    /* REQUIRES Sup(LL) = CacheS.mu */
    /* Return "true" if there are threads waiting to flush this
       VMultiPKFile to the stable cache, or a thread should start
       flushing it soon. */

  private:
    // read-only after initialization
    PKPrefix::T prefix;

    // data fields -- protected by CacheS.mu
    SMultiPKFile::VPKFileMap tbl; // set of VPKFiles
    int freeMPKFileEpoch;         // epoch of activity on this MultiPKFile
    int numWaiting;               // number of threads queued by ToSCache()
    int numRunning;		  // number of threads running "ToSCache"
    Basics::cond noneRunning;	  // !oneRunning
    int numNewEntries;            // number of new entries in all VPKFiles
    bool autoFlushPending;        // did "IsFull" recently return "true"?

    /* The "freeMPKFileEpoch" is the epoch at which an entry was last added
       to some VPKFile of this MultiPKFile. It is -1 if no entries have been
       added since the MultiPKFile was last flushed (or created).

       Since at most one thread can flush a MultiPKFile at a time, the value
       "numRunning" should only take on the values 0 and 1. Hence, it could be
       implemented as a Boolean value. However, we use an integer to represent
       it so we can check that the invariant is not violated on return from
       flushing the MultiPKFile.

       The "numNewEntries" field is an upper bound on the total number of new
       entries in all of this MultiPKFile's PKFiles that are not scheduled to
       be flushed. Before flushing the VMultiPKFile, the "ToSCache" method
       resets "numNewEntries" to zero. After that, new entries could be added,
       but those new entries might be flushed also. In that case, the total
       number of new entries after the flush completed would be 0, but
       "numNewEntries" would be strictly positive. The "numNewEntries" field
       is used heuristically by "ToSCache" to avoid flushing the MultiPKFile
       if possible.

       The set of VPKFiles is represented by a map "tbl: PK -> VPKFile*". If
       "(pk, vpkfile)" is an entry in the table, then "pk" has prefix
       "prefix", and "vpkfile" is a VPKFile for entries with PK "pk". */
};

#endif // _V_MULTI_PK_FILE_H
