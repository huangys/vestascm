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

#include <sys/stat.h>         // for mkdir(2)
#if defined(__digital__)
#include <sys/mode.h>         // permissions for newly-created directory modes
#endif

// vesta/basics
#include <Basics.H>
#include <FS.H>
#include <Table.H>
#include <Sequence.H>
#include <VestaLog.H>
#include <AtomicFile.H>

// vesta/fp
#include <FP.H>

// vesta/cache-common
#include <BitVector.H>
#include <CacheConfig.H>
#include <CacheState.H>
#include <Debug.H>
#include <PKPrefix.H>

// vesta/cache-server
#include "CacheConfigServer.H"
#include "EmptyPKLog.H"
#include "SPKFileRep.H"       // for SPKFileRep::*Version
#include "CacheEntry.H"       // for CE::List*
#include "SPKFile.H"          // for SPKFile::CFPEntryMap
#include "SMultiPKFileRep.H"
#include "VPKFile.H"
#include "SMultiPKFile.H"

using std::ios;
using std::streampos;
using std::ifstream;
using std::cerr;
using std::endl;
using std::ostream;
using OS::cio;

// KCS, 2003-01-29:

// I have reason to suspect that there's a bug which, when rewriting a
// MultiPKfile but only modifying a subset of its PKFiles, can copy
// either too many bytes or too few bytes for the unmodified PKFiles.
// This macro changes SMultiPKFile::Rewrite to check for the condition
// that would cause this and to always rewrite all PKFiles from memory
// rather than copying unmodified ones as sequences of bytes.
#define MISTRUST_PKLEN

// Number of bits used for each arc of the MultiPKFile pathname. The code
// requires that ArcBits <= 8*sizeof(Word) + 1 (that is, that a single arc
// cannot span more than one word boundary).
//
// static const int ArcBits = 12; // at most 4K elements per directory
static const int ArcBits = 8; // at most 256 elements per directory

// Lookup Methods -------------------------------------------------------------

void SMultiPKFile::OpenRead(const PKPrefix::T &pfx, /*OUT*/ ifstream &ifs)
  throw (SMultiPKFileRep::NotFound, FS::Failure)
{
    try {
	// Try to open the file for reading
	Text fpath(FullName(pfx));
	FS::OpenReadOnly(fpath, /*OUT*/ ifs);
    }
    catch (FS::DoesNotExist) {
	throw SMultiPKFileRep::NotFound();
    }
}

streampos SMultiPKFile::SeekToPKFile(ifstream &ifs, const FP::Tag &pk,
  /*OUT*/ int &version) throw (SMultiPKFileRep::NotFound, FS::Failure)
{
    try {
	// read version and header type
	SMultiPKFileRep::Header hdr(ifs);

	// search and seek to result
	streampos seek;
	switch (hdr.type) {
	  case SMultiPKFileRep::HT_List:
	    if (hdr.version <= SPKFileRep::LargeFVsVersion) {
		seek = SeekInListV1(ifs, hdr.num, pk);
	    } else {
		// no support for other versions
		assert(false);
	    }
	    break;
          case SMultiPKFileRep::HT_SortedList:
	    if (hdr.version <= SPKFileRep::LargeFVsVersion) {
		seek = SeekInSortedListV1(ifs, hdr.num, pk);
	    } else {
		// no support for other versions
		assert(false);
	    }
	    break;
	  case SMultiPKFileRep::HT_HashTable:
            /*** NYI ***/
	    assert(false);
	  default:
	    assert(false);
	}
	FS::Seek(ifs, seek);
	version = hdr.version;
	return seek;
    }
    catch (SMultiPKFileRep::BadMPKFile) {
	// opening non-MultiPKFile indicates programming error
	cerr << "Fatal error: tried reading bad MultiPKFile "
	     << "in SMultiPKFile::SeekToPKFile; exiting..." << endl;
	assert(false);
    }
    catch (FS::EndOfFile) {
	// premature end-of-file indicates programming error
	cerr << "Fatal error: premature EOF in "
             << "SMultiPKFile::SeekToPKFile; exiting..." << endl;
	assert(false);
    }
    return 0; // not reached
}

streampos SMultiPKFile::SeekInListV1(ifstream &ifs,
  SMultiPKFileRep::UShort numEntries, const FP::Tag &pk)
  throw (SMultiPKFileRep::NotFound, FS::EndOfFile, FS::Failure)
{
    while (numEntries > 0) {
	SMultiPKFileRep::HeaderEntry ent(ifs);
	if (ent.pk == pk) return (streampos)(ent.offset);
	numEntries--;
    }
    throw SMultiPKFileRep::NotFound();
}

streampos SMultiPKFile::SeekInSortedListV1(ifstream &ifs,
  SMultiPKFileRep::UShort numEntries, const FP::Tag &pk)
  throw (SMultiPKFileRep::NotFound, FS::EndOfFile, FS::Failure)
{
    // lookup the entry using binary search
    int lo = 0, hi = numEntries;
    streampos base = FS::Posn(ifs); // record start of entries on disk
    while (lo < hi) {
	// Loop invariant: Matching entry (if any) has index in [lo, hi).
        // On each iteration, the difference "hi - lo" is guaranteed to
        // decrease, so the loop is guaranteed to terminate.

	// read middle entry
	int mid = (lo + hi) / 2;
	assert(lo <= mid && mid < hi); // mid is in [lo, hi)
	FS::Seek(ifs, base +
		 (streampos) (mid * SMultiPKFileRep::HeaderEntry::Size()));
	SMultiPKFileRep::HeaderEntry midEntry(ifs);

	// compare middle CFP to "pk"
	int cmp = Compare(pk, midEntry.pk);
	if (cmp < 0) {
	    // matching entry in [lo, mid) => make "hi" smaller
	    hi = mid;
	} else if (cmp > 0) {
	    // matching entry in [mid+1, hi) => make "lo" larger
	    lo = mid + 1;
	} else {
	    return (streampos)(midEntry.offset);
	}
    }
    throw SMultiPKFileRep::NotFound();
}

// Rewrite methods ------------------------------------------------------------

bool SMultiPKFile::ChkptForRewrite(VPKFileMap& vpkFiles,
				   const BitVector *toDelete,
				   /*OUT*/ ChkPtTbl &vpkChkptTbl)
     throw()
/* REQUIRES (forall vf: VPKFile :: Sup(LL) < vf.mu) */

/* First, if toDelete is NULL, check each VPKFile in "vpkFiles" to see
   if it has any new entries.  If there are no new entries in any of
   these, return false to indicate that no rw-write is needed.

   If we have not decided that a re-write is unneccessary, Each
   VPKFile in "vpkFiles" is checkpointed (see the "VPKFileChkPt"
   interface for a description of how a checkpoint is represented)
   with the checkpoints placed in "vpkChkptTbl". Only the entries
   stored in the checkpoint are considered during the process of
   re-writing a MultiPKFile (with SMultiPKFile::Rewrite, below).

   The return value indicates whether a re-write is neccessary, and
   consequently whether the VPKFiles have been checkpointed into
   "vpkChkptTbl". */
{
    FP::Tag pk;
    VPKFile *vpkfile;
    VPKFileIter it(&vpkFiles);

    /* If no deletions are pending and none of the VPKFiles has any
       new entries to flush, then this MultiPKFile will be unchanged,
       so return false to indicate that no re-wrtoe will be
       performed. */
    if ((toDelete == (BitVector *)NULL)) {
	bool anyNewEntries = false;
	while (!anyNewEntries && it.Next(/*OUT*/ pk, /*OUT*/ vpkfile)) {
	    vpkfile->mu.lock();
	    if (vpkfile->HasNewEntries()) { anyNewEntries = true; }
	    vpkfile->mu.unlock();
	}
	if (!anyNewEntries) return false;
    }

    // checkpoint entries in "vpkFiles"; store them in "chkptTbl"
    for (it.Reset(); it.Next(/*OUT*/ pk, /*OUT*/ vpkfile); /*SKIP*/) {
        VPKFileChkPt *chkpt = vpkfile->CheckPoint();
	bool inTbl = vpkChkptTbl.Put(pk, chkpt, /*resize=*/ false);
	assert(!inTbl);
    }
    vpkChkptTbl.Resize();

    // If we made it this far, we've decided that there may be some
    // re-writing to do, so return true to indicate that.
    return true;
}

bool SMultiPKFile::PrepareForRewrite(const PKPrefix::T &pfx,
				     ifstream &ifs,
				     /*OUT*/ SMultiPKFileRep::Header *&hdr)
  throw (FS::Failure, FS::EndOfFile)
{
  bool mpkFileExists; // true iff "ifs" is an open file

  try
    {
      OpenRead(pfx, /*OUT*/ ifs);
      mpkFileExists = true;
    }
  catch (SMultiPKFileRep::NotFound)
    {
      mpkFileExists = false;
    }

  // Read existing MultiPKFile header and header entries
  if (mpkFileExists)
    {
      try
	{
	  hdr = SMultiPKFile::ReadHeader(pfx, /*readEntries=*/ true, ifs);
	}
      catch (...)
	{
	  // Log something about which file we encountered a problem
	  // with.
	  cerr << "Exception while reeading MPKFile header "
	       << FullName(pfx)
	       << endl;
	  // Make sure we close the input stream if we opened it.
	  if (mpkFileExists) FS::Close(ifs);
	  // Re-throw the exception.
	  throw;
	}
    }
  else
    {
      // allocate an empty one
      hdr = NEW(SMultiPKFileRep::Header);
    }

  return mpkFileExists;
}

void SMultiPKFile::Rewrite(const PKPrefix::T &pfx,

			   // From PrepareForRewrite above
			   bool mpkFileExists, ifstream &ifs,
			   SMultiPKFileRep::Header *hdr,

			   VPKFileMap& vpkFiles,
			   ChkPtTbl &vpkChkptTbl,
			   const BitVector *toDelete,
			   EmptyPKLog *emptyPKLog,
			   /*INOUT*/ EntryState &state)
  throw (FS::Failure, FS::EndOfFile, VestaLog::Error)
/* REQUIRES (forall vf: VPKFile :: Sup(LL) < vf.mu) */
/* This is the routine for rewriting a MultiPKFile. It is roughly equivalent
   to the "VToSCache" function of the "SVCache" specification, except
   that it is working with a *group* of PKFiles stored in a single
   MultiPKFile.

   For each PKFile contained in the MultiPKFile with prefix "pfx" (i.e. those
   appearing in the range of "vpkfile"), it reads the PKFile (if any) from
   disk, and then writes out a new version of the PKFile that includes any new
   entries contained in the "VPKFile"'s stored in "vpkFiles", removing any
   entries named in "toDelete" (if it is non-NULL).

   "vpkChkptTbl" must contain checkpoints of each VPKFile in
   "vpkFiles". (These checkpoints are normally created by
   SMultiPKFile::ChkptForRewrite, above.)  Only the entries stored in
   these checkpoints are considered in the rest of the procedure. The
   checkpoints are local, so fields within them that don't point back
   to the original VPKFile can be accessed without a lock; those that
   point back to the VPKFile must be accessed with a lock held.

   The MultiPKFile is read from disk into "SMultPKFileRep::Header" and
   "SMultiPKFileRep::HeaderEntry" structures in memory. The latter in turn
   contain "SPKFile" structures that contain the contents of individual
   PKFiles within the MultiPKFile. All of the updates are performed on these
   in-memory structures, and then the result is written out to disk.

   The new MultiPKFile is written to disk using a "AtomicFile" object.
   This object causes a temporary file object to be written. The pointer for
   the actual file is swung when the "AtomicFile" object is closed.
   Hence, the entire file can be written without a lock; the lock is only
   required when the file is closed.

   However, before the file pointer can be swung, each of the "VPKFile"'s for
   "pfx" must be modified. These modifications must occur with the appropriate
   "VPKFile" locks held. Since it would be unsafe for a thread to have access
   to an updated "VPKFile" before it had access to the updated MultiPKFile on
   disk, all of the "VPKFile" locks for the MultiPKFile are held until after
   the pointer has been swung. */
{
    FP::Tag pk;
    VPKFile *vpkfile;
    VPKFileIter it(&vpkFiles);

    AtomicFile ofs;
    try {
      /* Add new empty VPKFiles to "hdr" for those with pending entries to
	 flush that are not yet in this MultiPKFile. Remove from "vpkFiles"
	 those that have no pending entries and no PKFile on disk. */
      for (it.Reset(); it.Next(/*OUT*/ pk, /*OUT*/ vpkfile); /*SKIP*/) {
	VPKFileChkPt *chkpt;
	SMultiPKFileRep::HeaderEntry *dummyHE;
	bool inTbl = vpkChkptTbl.Get(pk, /*OUT*/ chkpt); assert(inTbl);
	if (chkpt->hasNewEntries) {
	  (void)(hdr->AppendNewHeaderEntry(pk));
	} else if (!hdr->pkTbl.Get(pk, /*OUT*/ dummyHE)) {
	  // if not in "hdr" already, remove "pk" from "vpkFiles"
	  inTbl = vpkFiles.Delete(pk, /*OUT*/ vpkfile, /*resize=*/ false);
	  assert(inTbl);
	}
      }
      vpkFiles.Resize();

      // Determine whether the MultiPKFile is written in an old format.
      // When this is the case, we will always re-write every PKFile in
      // it, even ones that have no changes (no new entries and no
      // deletions).  Ordinarilly, we copy such PKFiles from the old
      // stable MultiPKFile to the new one, but if the file format has
      // changed, this can create a MultiPKFile to contain PKFiles with
      // different formats.  Since the file version is only stored at
      // the MultiPKFile granularity, we can't handle PKFiles with
      // different formats in the same MultiPKFile.
      bool l_fileFormatChange = mpkFileExists &&
	(hdr->version < SPKFileRep::LastVersion);

      // update the individual PKFiles (in memory only)
      int i;
      for (i = 0; i < hdr->num; i++) {
	FP::Tag *pki = hdr->pkSeq[i];    // "pki" == an alias for the "i"th PK
	SMultiPKFileRep::HeaderEntry *he;
	bool inTbl = hdr->pkTbl.Get(*pki, /*OUT*/ he); assert(inTbl);
	VPKFileChkPt *chkpt = (VPKFileChkPt *)NULL;
	(void)(vpkChkptTbl.Get(he->pk, /*OUT*/ chkpt));

	// There must be a volatile PKFile for every stable PKFile.
	inTbl = vpkFiles.Get(he->pk, /*OUT*/ vpkfile); assert(inTbl);

	// only read and update the SPKFile if there are deletions pending
	// or if this PKFile has new entries pending
	if ((toDelete != (BitVector *)NULL) ||
            ((chkpt != (VPKFileChkPt *)NULL) && (chkpt->hasNewEntries))) {

	  // read/create the SPKFile
	  if (he->SPKFileExists()) {
	    // read existing SPKFile from disk
	    assert(mpkFileExists);
	    FS::Seek(ifs, he->offset);
	    he->pkfile = NEW_CONSTR(SPKFile,
				    (he->pk, ifs, he->offset,
				     hdr->version, /*OUT*/ he->pkhdr,
				     /*readEntries=*/ true,
				     /*readImmutable=*/ Config_ReadImmutable));

#ifdef MISTRUST_PKLEN
	    // Check for incorrect pkLen
	    if(FS::Posn(ifs) != (streampos) (he->offset + he->pkLen))
	      {
		ostream& err_stream = cio().start_err();
		err_stream << Debug::Timestamp()
			   << " -- Detected incorrect pkLen" << endl
			   << "  pkfile             = " << i << endl
			   << "  expoected position = "
			   << (he->offset + he->pkLen) << endl
			   << "  actual position    = " << FS::Posn(ifs) << endl;
		hdr->Debug(err_stream, true);
		cio().end_err();
	      }
#endif
	  } else {
	    // create a new, empty SPKFile
	    he->pkfile = NEW_CONSTR(SPKFile, (he->pk));
	  }

	  // Before updating either, the volatile PKFile's common
	  // names set must match that of the stable PKFile.
	  assert(*(he->pkfile->CommonNames()) == *(vpkfile->CommonNames()));

	  // update it (this is where all the merging work is done)
	  he->pkfileModified = he->pkfile->Update(chkpt, toDelete,
						  /*OUT*/ he->exCommonNames,
						  /*OUT*/ he->exUncommonNames,
						  /*OUT*/ he->packMask,
						  /*OUT*/ he->reMap,
						  /*OUT*/ he->becameEmpty);

	  // delete this PKFile from the MultiPKFile if it is now empty
	  if (he->becameEmpty) {
	    // remove it from "hdr"
	    hdr->RemoveHeaderEntry(*pki);
	    i--; // don't advance index in "pkSeq" array

	    // add it to the "EmptyPKLog"
	    emptyPKLog->Write(*pki, he->pkfile->PKEpoch());
	  }
	} else
	  // If we're rewriting a MultiPKFile that's written in an old
	  // format, load even PKFiles with no changes pending.
	  if(l_fileFormatChange
#ifdef MISTRUST_PKLEN
	     // If we suspect that pkLens may be wrong, always load
	     // all PKFiles in a MultiPKFile being rewritten.
	     || true
#endif
	     ) {
	    // Note that we should never get here for a PKFile that
	    // doesn't already exist on disk.  A loop above removes
	    // all references to VPKFiles that are not on disk and
	    // don't have changes pending, and the previous if clause
	    // handles all PKFiles with changes pending.
	    assert(he->SPKFileExists());

	    // read existing SPKFile from disk
	    assert(mpkFileExists);
	    FS::Seek(ifs, he->offset);
	    he->pkfile = NEW_CONSTR(SPKFile,
				    (he->pk, ifs, he->offset,
				     hdr->version, /*OUT*/ he->pkhdr,
				     /*readEntries=*/ true,
				     /*readImmutable=*/ Config_ReadImmutable));

	  // Even if we're not updating, the volatile PKFile's common
	  // names set must match that of the stable PKFile.
	  assert(*(he->pkfile->CommonNames()) == *(vpkfile->CommonNames()));

#ifdef MISTRUST_PKLEN
	  // Check for incorrect pkLen
	  if(FS::Posn(ifs) != (streampos) (he->offset + he->pkLen))
	    {
	      ostream& err_stream = cio().start_err();
	      err_stream << Debug::Timestamp()
			 << " -- Detected incorrect pkLen:" << endl
			 << "  pkfile             = " << i << endl
			 << "  expoected position = "
			 << (he->offset + he->pkLen) << endl
			 << "  actual position    = " << FS::Posn(ifs) << endl;
	      hdr->Debug(err_stream, true);
	      cio().end_err();
	    }
#endif

	  } else {
	    he->pkfileModified = false;
	  }
      }

      // write the SMultiPKFile to disk if it is non-empty
      if (hdr->num > 0) {
	// create a new file to write result to
	OpenAtomicWrite(pfx, /*OUT*/ ofs);

	// write "hdr" and its header entries to disk
	hdr->Write(ofs);
	hdr->WriteEntries(ofs);

	// write PKFiles to disk
	for (i = 0; i < hdr->num; i++) {
	  FP::Tag *pki = hdr->pkSeq[i]; // an alias for the "i"th PK
	  SMultiPKFileRep::HeaderEntry *he;
	  bool inTbl = hdr->pkTbl.Get(*pki, /*OUT*/ he); assert(inTbl);
	  if (he->pkfileModified || l_fileFormatChange
#ifdef MISTRUST_PKLEN
	      // If we suspect that pkLens may be wrong, always
	      // rewrite all PKFiles.
	      || true
#endif
	      ) {
	    // write new version of PKFile to disk (either because
	    // the contents of the PKFile have changed, or because
	    // the file format has changed).
	    he->offset = FS::Posn(ofs);
	    assert(he->pkfile != NULL);
	    he->pkfile->Write(ifs, ofs, he->offset, /*OUT*/ he->pkhdr);
	  } else {
	    // copy PKFile on disk from "ifs" to "ofs"
	    FS::Seek(ifs, he->offset);
	    he->offset = FS::Posn(ofs);
	    FS::Copy(ifs, ofs, he->pkLen);
	  }
	}

	// back-patch the SMultiPKFile and relevant SPKFiles
	hdr->BackPatch(ofs);
	for (i = 0; i < hdr->num; i++) {
	  FP::Tag *pki = hdr->pkSeq[i]; // an alias for the "i"th PK
	  SMultiPKFileRep::HeaderEntry *he;
	  bool inTbl = hdr->pkTbl.Get(*pki, /*OUT*/ he); assert(inTbl);
	  /* We only have to backpatch the PKFiles that were
	     changed, since the offsets stored in a PKFile header
	     are all relative to the start of the PKFile within the
	     MultiPKFile.  (If we're writing out a different file
	     format, all PKFiles have probably changed, so we should
	     backpatch the offsets for them all.) */
	  if (he->pkfileModified || l_fileFormatChange
#ifdef MISTRUST_PKLEN
	      // If we suspect that pkLens may be wrong, we rewrite
	      // the PKfile, so we backpatch it.  Shouldn't be
	      // strictly neccessary, but seems like a good idea.
	      || true
#endif
	      ) {
	    assert(he->pkfile != NULL && he->pkhdr != NULL);
	    he->pkhdr->BackPatch(ofs, he->offset);
	  }
	}
      }
    }
    catch (...)
      {
	// Log something about which file we encountered a problem
	// with.
	cerr << "Exception while re-writing MPKFile "
	     << FullName(pfx)
	     << endl;
	// Make sure we close the input stream if we opened it.
	if (mpkFileExists) FS::Close(ifs);
	// Re-throw the exception.
	throw;
      }
    if (mpkFileExists) FS::Close(ifs);

    // pause if necessary (for debugging)
    if (Config_WeedPauseDur > 0) PauseDeletionThread();

    // atomically replace VPKFiles and swing SMultiPKFile pointer.
    for (it.Reset(); it.Next(/*OUT*/ pk, /*OUT*/ vpkfile); /*SKIP*/) {
	// find HeaderEntry and corresponding checkpoint
	bool inTbl;
	SMultiPKFileRep::HeaderEntry *he;
	VPKFileChkPt *chkpt;
	inTbl = hdr->pkTbl.Get(pk, /*OUT*/ he); assert(inTbl);
	inTbl = vpkChkptTbl.Get(pk, /*OUT*/ chkpt); assert(inTbl);

	// acquire lock on "vpkfile"
	vpkfile->mu.lock();

	// VPKfiles in use by a flush to the stable cache cannot be
	// evicted.  This should be guaranteed, but we check it just
	// to be sure.
	if(vpkfile->Evicted())
	  {
	    cio().start_err() << Debug::Timestamp()
			      << "INTERNAL ERROR: "
			      << "evicted VPKFile in SMultiPKFile::Rewrite:" << endl
			      << "  pk           = " << pk << endl
			      << " (Please report this as a bug.)" << endl;
	    cio().end_err(/*aborting*/true);
	    // Crash and dump core, as we can't continue correctly.
	    abort();
	  }

	/* It is safe to decrement the "pkEpoch" of the VPKFile if the
	   SPKFile was unmodified and if no new entries have arrived
           since the VPKFile was checkpointed. */
	if (!he->pkfileModified && !vpkfile->HasNewEntries()) {
	    vpkfile->DecrPKEpoch();
	}

	// update the VPKFile
        /* This is only necessary if the SPKFile was modified on disk
           or if the VPKFile has new entries pending. The latter implies the
           former, even if there were new entries in the checkpoint but they
	   were all deleted. */
	assert(!chkpt->hasNewEntries || he->pkfileModified);
	if (he->pkfileModified) {
	    assert(he->pkfile != (SPKFile *)NULL);
	    // The following assertion assumes that no more than one flush
	    // can be in progress at a time.  --mann
	    assert((vpkfile->PKEpoch() == he->pkfile->PKEpoch()) &&
                   (vpkfile->PKEpoch() == (chkpt->pkEpoch + 1)));
	    vpkfile->Update(*(he->pkfile), *chkpt,
              toDelete, he->exCommonNames, he->exUncommonNames,
              he->packMask, he->reMap, he->becameEmpty, /*INOUT*/ state);
	}

	// After updating the volatile PKFile if necessary, its set of
	// common names should match the new set of common names in
	// the stable PKFile.
	if(he->pkfile != NULL)
	  {
	    assert(*(he->pkfile->CommonNames()) == *(vpkfile->CommonNames()));
	  }
    }

    // delete file or swing file pointer atomically
    if (hdr->num == 0) {
	// delete the MultiPKFile if it exists
        /* We test if the file exists by whether the "totalLen"
	   field of the SMultiPKFileRep::Header object is non-zero. */
	if (hdr->totalLen > 0) DeleteFile(pfx);
    } else {
	FS::Close(ofs); // swing file pointer atomically
    }

    // unlock all the VPKFile locks
    for (it.Reset(); it.Next(/*OUT*/ pk, /*OUT*/ vpkfile); /*SKIP*/) {
	vpkfile->mu.unlock();
    }
} // SMultiPKFile::Rewrite

SMultiPKFileRep::Header* SMultiPKFile::ReadHeader(const PKPrefix::T &pfx,
  bool readEntries, ifstream &ifs) throw (FS::Failure, FS::EndOfFile)
{
    SMultiPKFileRep::Header *res;
    try {
      // Read MultiPKFile <Header> and all its entries
      res = NEW_CONSTR(SMultiPKFileRep::Header, (ifs));
      if (readEntries) res->ReadEntries(ifs);
    }
    catch (SMultiPKFileRep::BadMPKFile) {
	cerr << "Fatal error: the MultiPKFile for prefix " << pfx
             << " is bad; crashing..." << endl;
	assert(false);
    }
    return res;
}

Text SMultiPKFile::Name(const PKPrefix::T &pfx, int granBits) throw ()
{
    char buff[10];
    int printLen = sprintf(buff, "gran-%02d/", granBits);
    assert(printLen == 8);
    Text res(buff);
    res += pfx.Pathname(granBits, ArcBits);
    return res;
}

Text SMultiPKFile::FullName(const PKPrefix::T &pfx) throw ()
{
    Text nm(Name(pfx, PKPrefix::Granularity()));
    Text res(Config_SCachePath + "/" + nm);
    return res;
}

void SMultiPKFile::OpenAtomicWrite(const PKPrefix::T &pfx,
  /*OUT*/ AtomicFile &ofs) throw (FS::Failure)
{
    // compute filename
    Text fpath(FullName(pfx));

    // open file
    ofs.open(fpath.chars(), ios::out);
    if (ofs.fail() && errno == ENOENT) {
	// a directory is missing, so create any necessary ones
	MakeDirs(fpath);

	// Clear the stream state before we try again, as open may not
	// reset it to good for us.
        ofs.clear(ios::goodbit);

	// try opening it again
	ofs.open(fpath.chars(), ios::out);
    }

    // check if there is still an error
    if (ofs.fail()) {
	throw(FS::Failure(Text("OpenAtomicWrite"), fpath));
    }
}

void SMultiPKFile::MakeDirs(const Text &name) throw (FS::Failure)
{
    // local constants
    const char PathnameSep = '/';
    const int  DirMode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

    // first make a copy of the string, since we will be mutating it
    Text nm(name.chars());

    // find last '/'; this separates last directory from filename
    const char *start = nm.cchars();
    char *q = ((char *)start) + nm.Length();

    // work backwards to find first directory that does not exist
    int stat_res;
    do {
	struct stat buffer;
	for (q--; q >= start && *q != PathnameSep; q--) /*SKIP*/;
	assert(q >= start);
	*q = '\0';
	stat_res = stat(start, /*OUT*/ &buffer);
	*q = PathnameSep;
    } while (stat_res != 0);

    // try creating each prefix of the path
    while (true) {
	q = strchr(q+1, PathnameSep);
	if (q == (char *)NULL) break;
	*q = '\0';
	int status = mkdir(start, DirMode);
	*q = PathnameSep;
	if (status < 0 && errno != EEXIST) {
	    throw (FS::Failure(Text("SMultiPKFile::MakeDirs"), name));
	}
    }
}

void SMultiPKFile::DeleteFile(const PKPrefix::T &pfx)
  throw (FS::Failure)
{
    // delete the file itself
    Text fpath(FullName(pfx));
    FS::Delete(fpath);

    // delete successive parent directories that are empty
    int i = fpath.Length(); bool deleted;
    do {
	i = fpath.FindCharR('/', i - 1); assert(i >= 0);
	Text dir(fpath.Sub(0, i));
	deleted = (dir != Config_SCachePath && FS::DeleteDir(dir));
    } while (deleted);
}

void SMultiPKFile::PauseDeletionThread() throw ()
{
  // pre debugging
  cio().start_out() << Debug::Timestamp() 
		    << "STARTING -- Pausing Deletion Thread" << endl
		    << "  Duration = " << Config_WeedPauseDur 
		    << " seconds" << endl << endl;
  cio().end_out();
  
  // pause
  Basics::thread::pause(Config_WeedPauseDur);
  
  // post debugging
  cio().start_out() << Debug::Timestamp() 
		    << "DONE -- Pausing Deletion Thread" << endl << endl;
  cio().end_out();
}
