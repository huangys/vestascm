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

// Last modified on Thu Aug  5 00:19:51 EDT 2004 by ken@xorian.net
//      modified on Tue May  4 11:16:46 PDT 1999 by heydon

// SMultiPKFile -- operations for reading/writing MultiPKFile's on disk

/* The cache entries for a single primary key (PK) are grouped together on the
   disk in what is called a PKFile. It would be too expensive to use a single
   disk file for each PKFile, so PKFiles are grouped together into so-called
   MultiPKFiles on the disk.

   An SMultiPKFile exports operations for reading/writing MultiPKFiles on
   disk in the stable cache.

   The start of a MultiPKFile has an index for locating the PKFile within the
   MultiPKFile for a particular PK. The methods of this interface are used for
   opening MultiPKFiles, and for reading and writing their indices. See the
   PKFile interface for reading and writing individual PKFile's.

   The mapping from a PK to its MultiPKFile is implemented by the file system.
   The MultiPKFile for a given PK is formed from a prefix of the PK. The size
   used for the prefix determines the {\def granularity} of the MultiPKFile's,
   and is defined by the "PKPrefix" interface. The implementation is allowed
   to change the PKPrefix granularity, but it must of course support the
   ability to find MultiPKFile's of older granularity, and to write files of
   the latest granularity from older ones when the granularity changes. The
   ability to support MultiPKFiles of differing granularity is currently not
   supported. */

#ifndef _S_MULTI_PK_FILE_H
#define _S_MULTI_PK_FILE_H

#include <Basics.H>
#include <FS.H>
#include <Table.H>
#include <VestaLog.H>
#include <AtomicFile.H>
#include <FP.H>
#include <BitVector.H>
#include <CacheState.H>
#include <PKPrefix.H>

#include "EmptyPKLog.H"
#include "SMultiPKFileRep.H"
#include "VPKFile.H"

class SMultiPKFile {
public:
  // dictionary types
  typedef Table<FP::Tag,VPKFile*>::Default VPKFileMap;
  typedef Table<FP::Tag,VPKFile*>::Iterator VPKFileIter;

  // ChkPtTbl: FP::Tag -> (VPKFileChkPt *)
  typedef Table<FP::Tag,VPKFileChkPt*>::Default ChkPtTbl;
  typedef Table<FP::Tag,VPKFileChkPt*>::Iterator ChkPtIter;

  static void OpenRead(const PKPrefix::T &pfx, /*OUT*/ std::ifstream &ifs)
    throw (SMultiPKFileRep::NotFound, FS::Failure);
  /* Open the MultiPKFile with prefix "pfx" for reading, and set "ifs" to
     an input file stream on it. If there is no such MultiPKFile, raise
     "SMultiPKFileRep::NotFound".

     The name of the MultiPKFile for "pfx" is determined from the values of
     the "[CacheServer]/MetaDataRoot", "[CacheServer]/MetaDataDir", and
     "[CacheServer]/SCacheDir" configuration variables, from the "pfx"
     itself, and from the "granularity" at which the latest MultiPKFile
     for "pfx" was written. */ 

  static std::streampos SeekToPKFile(std::ifstream &ifs, const FP::Tag &pk,
				     /*OUT*/ int &version)
    throw (SMultiPKFileRep::NotFound, FS::Failure);
  /* REQUIRES ifs.tellg() == ``start of <MultiPKFile>'' */
  /* Assuming "ifs" is positioned at the start of the MultiPKFile "ifs",
     seek to the position in that file where the PKFile for "pk" begins
     if such a PKFile exists, set "version" to the version number found
     in the MultiPKFile <Header>, and return that position; raise
     "SMultiPKFileRep::NotFound" otherwise. */

  static bool ChkptForRewrite(VPKFileMap& vpkFiles,
			      const BitVector *toDelete,
			      /*OUT*/ ChkPtTbl &vpkChkptTbl) throw();
  /* REQUIRES (forall vf: VPKFile :: Sup(LL) < vf.mu) */
  /* In preparation for rewriting a MultiPKFile, checkpoint in
     memory all of its VPKFiles to capture what is to be written.
     This is neceessary because new entries may be added in parallel
     with re-writng. "vpkFiles" is a table of "VPKFiles" stored in
     the cache; this table must be a *superset* of the "SPKFiles"
     stored in the corresponding MultiPKFile on disk. If "toDelete"
     is non-NULL, it points to a bit vector whose set bits
     correspond to the indices of cache entries to delete from the
     cache.

     "vpkChkptTbl" is filled with VPKFileChkPt objects, one per
     VPKFile in "vpkFiles".  The return value indicates whether a
     re-write is neccessary.  If toDelete is NULL and there are no
     new entries in any of the VPKFiles, the result will be false
     (indicating that there is nothing that needs to be written to
     disk). */

  static bool PrepareForRewrite(const PKPrefix::T &pfx,
				std::ifstream &ifs,
				/*OUT*/ SMultiPKFileRep::Header *&hdr)
    throw (FS::Failure, FS::EndOfFile);
  /* Perform the initial steps of re-writing the MultiPKFile with
     prefix "pfx".

     The return value indicates if the stable MultiPKFile already
     exists on disk.  If it does, it will be opened with "ifs" and
     its header and header entries will be read into "hdr".  If it
     does not, "hdr" will be set to a new empty
     SMultiPKFileRep::Header.

     This step is separated from Rewrite (below) to allow the cache
     to create VPKFiles for all PKFiles in the stable copy of the
     MultiPKFile.  Doing so avoids a race between rewriting and
     client requests. */

  static void Rewrite(const PKPrefix::T &pfx,

		      // From PrepareForRewrite above
		      bool mpkFileExists, std::ifstream &ifs,
		      SMultiPKFileRep::Header *hdr,

		      // In-memory representations to be updated
		      VPKFileMap &vpkFiles,

		      // Changes to be committed
		      ChkPtTbl &vpkChkptTbl,

		      const BitVector *toDelete, EmptyPKLog *emptyPKLog,
		      /*INOUT*/ EntryState & state)
    throw (FS::Failure, FS::EndOfFile, VestaLog::Error);
  /* REQUIRES (forall vf: VPKFile :: Sup(LL) < vf.mu) */
  /* Rewrite the MultiPKFile with prefix "pfx".

  "mpkFileExists" is the return value of PrepareForRewrite
  (above).  "ifs" and "hdr" are also prepared by the same
  function.

  "vpkFiles" is a table of "VPKFiles" stored in the cache under
  PK's with prefix "pfx"; this table must be a *superset* of the
  "SPKFiles" stored in the corresponding MultiPKFile on
  disk. "vpkChkptTbl" is a table of checkpoints of the VPKFiles
  in "vpkFiles", generated by calling
  SMultiPKFile::ChkptForRewrite (above). If "toDelete" is
  non-NULL, it points to a bit vector whose set bits correspond
  to the indices of cache entries to delete from the cache.

  This is the only method that reads and writes MultiPKFiles on disk.
  It is unmonitored; that is, its correctness requires that it is only
  called by one thread at a time. */

private:
  // lookup private methods -------------------------------------------------

  static std::streampos SeekInListV1(std::ifstream &ifs,
				     SMultiPKFileRep::UShort numEntries,
				     const FP::Tag &pk)
    throw (SMultiPKFileRep::NotFound, FS::EndOfFile, FS::Failure);
  /* REQUIRES ifs.tellg() == ``start of <SeqHeader>'' */
  /* Return the absolute offset for "pk" in the file "ifs", assumming "ifs"
     is positioned at the start of a <SeqHeader>, version 1. Throws
     "SMultiPKFileRep::NotFound" if there is no entry in the header matching
     "pk", "FS::EndOfFile" in the event of premature end-of-file, and
     "FS::Failure" in the event of any other file error. */

  static std::streampos SeekInSortedListV1(std::ifstream &ifs,
					   SMultiPKFileRep::UShort numEntries,
					   const FP::Tag &pk)
    throw (SMultiPKFileRep::NotFound, FS::EndOfFile, FS::Failure);
  /* REQUIRES ifs.tellg() == ``start of <SeqHeader>'' */
  /* Return the absolute offset for "pk" in the file "ifs", assumming "ifs"
     is positioned at the start of a sorted <SeqHeader>, version 1. Throws
     "SMultiPKFileRep::NotFound" if there is no entry in the header matching
     "pk", "FS::EndOfFile" in the event of premature end-of-file, and
     "FS::Failure" in the event of any other file error. */

  // rewrite private methods ------------------------------------------------

  static SMultiPKFileRep::Header* ReadHeader(const PKPrefix::T &pfx,
					     bool readEntries,
					     std::ifstream &ifs)
    throw (FS::Failure, FS::EndOfFile);
  /* Read the <Header> of the MultiPKFile for "pfx" from "ifs", and if
     "readEntries" is "true", fill in its "pkTbl" with the <HeaderEntry>'s.
     Even if "readEntries" is true, the PKFiles themselves are not read.
     Throw "FS::Failure" for any other I/O error. */

  static Text Name(const PKPrefix::T &pfx, int granBits) throw ();
  /* Return the pathname for the MultiPKFile of granularity "granBits"
     with prefix "p". */

  static Text FullName(const PKPrefix::T &pfx) throw ();
  /* Return the complete filename of the SMultiPKFile for prefix "pfx". */

  static void OpenAtomicWrite(const PKPrefix::T &pfx,
			      /*OUT*/ AtomicFile &ofs) throw (FS::Failure);
  /* Open the stream "ofs" for writing on the MultiPKFile for "pfx" at the
     current granularity. This opens the file under a different name; the
     "close" method on the "ofs" can then be used to atomically rename the
     file to its correct name. */

  static void DeleteFile(const PKPrefix::T &pfx) throw (FS::Failure);
  /* Delete the SMultiPKFile for prefix "pfx". */

  static void MakeDirs(const Text &name) throw (FS::Failure);
  /* Create any necessary directories along the path named by the file
     "name". If one of the directories cannot be created, throw
     "FS::Failure". */

  static void PauseDeletionThread() throw ();
  /* Print debugging information to standard output and pause the calling
     thread for the number of seconds specified in the global configuration
     variable "Config_WeedPauseDur". */
};

#endif // _S_MULTI_PK_FILE_H
