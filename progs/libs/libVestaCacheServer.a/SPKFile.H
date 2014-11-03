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

// SPKFile.H -- An interface for reading/writing stable PKFile's within
// MultiPKFile's on disk. This also defines the type "SPKFile", of which
// "VPKFile" is a subtype.

#ifndef _SPK_FILE_H
#define _SPK_FILE_H

#include <Basics.H>
#include <FS.H>
#include <Table.H>
#include <FP.H>
#include <CacheIntf.H>
#include <BitVector.H>
#include <FV.H>

#include "SPKFileRep.H"
#include "IntIntTblLR.H"
#include "CacheEntry.H"
#include "VPKFileChkPt.H"

class SPKFile {
public:
  // constructors
  SPKFile(const FP::Tag& pk) throw ()
    : pk(pk), pkEpoch(0), namesEpoch(0), sourceFunc(NULL) { /*SKIP*/ }
  /* Create a new, empty PKFile for primary key "pk". This will be used as
     the initial PKFile when none exists in the stable cache. */

  SPKFile(const FP::Tag& pk, std::ifstream &ifs, std::streampos origin,
	  int version, /*OUT*/ SPKFileRep::Header* &hdr,
	  bool readEntries = true, bool readImmutable = true)
    throw (FS::EndOfFile, FS::Failure)
    : pk(pk)
  { Read(ifs, origin, version, /*OUT*/ hdr, readEntries, readImmutable);}
  /* REQUIRES FS::Posn(ifs) == ``start of <PKFile>'' */
  /* Initialize a new "SPKFile" object from the disk. See "Read" below. */

  // update
  bool Update(const VPKFileChkPt *chkpt, const BitVector *toDelete,
	      /*OUT*/ BitVector* &exCommonNames,
	      /*OUT*/ BitVector* &exUncommonNames,
	      /*OUT*/ BitVector* &packMask, /*OUT*/ IntIntTblLR* &reMap,
	      /*OUT*/ bool &isEmpty) throw ();
  /* REQUIRES ((chkpt!=NULL) && chkpt->hasNewEntries) || (toDelete!=NULL) */
  /* ENSURES (packMask == NULL) == (reMap == NULL) */
  /* Update this SPKFile. If "chkpt != NULL", incorporate the new names
     and entries in "chkpt". If "toDelete" is non-NULL, then any entries
     in this SPKFile whose indices correspond to set bits in "*toDelete"
     should be deleted. The method returns "true" iff the "SPKFile" needs
     to be modified on disk in any way. This will be true if there are
     new entries to add from "chkpt", if some existing entries on the
     disk must be deleted, or if some entries in "chkpt" must be deleted.
     In the latter case, the contents of the SPKFile may not change, but
     its "pkEpoch" must be increased.

     If any entries are added or removed, the set of common names for this
     SPKFile may change. In particular, if any entries are added, some names
     that used to be common may become uncommon. In this case, the "OUT"
     parameter "exCommonNames" is set to point to a new bit vector of the
     names that used to be common; it is set to "NULL" if there are no such
     names. If any entries are deleted, then names that used to be uncommon
     may become common. In this case, the "OUT" parameter "exUncommonNames"
     is set to point to a new bit vector of the names that used to be
     uncommon; it is set to "NULL" if there are no such names. The set bits
     of both the "exCommonNames" and "exUncommonNames" are interpreted
     relative to the "allNames" array *before* this method was called; they
     are *not* packed according to "packMask".

     Finally, if entries are deleted, the union of all the names appearing
     in this SPKFile (its "allNames" field) may change. In this case,
     "packMask" is set to a new bit vector whose set bits correspond to
     names in the original "allNames" array that still appear in some entry
     in the stable PKFile. This bit vector may be used to pack the
     "commonNames" and "uncommonNames" bit vectors in the volatile cache.
     Furthermore, "reMap" is set to point to a new table mapping integers to
     integers. For each name that remains after the SPKFile has been
     updated, this table maps the index of each name in the original
     "allNames" array to its index in the new version of that array.
     Because the spec requires that the sequence of the names is preserved,
     a name's new index will always be at most its old index. If no name
     indices change, then "packMask" and "reMap" are both set to "NULL".
     In the event that "packMask" and "reMap" are non-NULL, the
     "this->allNames" array and the "this->commonNames" bit vector are
     packed according to "packMask". 

     Even in the event that "packMask" and "reMap" are non-NULL, the bit
     vectors "exCommonNames" and "exUncommonNames" (if non-NULL) are *not*
     packed. That is, they should be interpreted relative to the original
     "allNames" array in place on entry to this method.

     If the PKFile has no entries after being flushed, then "isEmpty" is set
     to true. It is set to false otherwise.

     If the PKFile changes (i.e., if this method returns "true"), the
     "namesEpoch" value is updated from "chkpt".  If any name indices change
     (i.e., if "packMask" and "reMap" are set non-NIL), then the SPKFile's
     "namesEpoch" is incremented.

     One special case worth considering is the one in which this "SPKFile"
     is brand new, and hence, empty. In this case, there are no existing
     common names, so "exCommonNames" will be set to "NULL" and
     "exUncommonNames" will be set to the *new* set of common names.  Also
     in this case, this->pkEpoch and chkpt->pkEpoch will both be zero on
     entry, so this->pkEpoch must be updated to 1; in all other cases,
     this->pkEpoch will already have been updated to chkpt->pkEpoch + 1 by
     VPKFile::CheckPoint.

     Another case worth considering is the one in which all the entries from
     this SPKFile have been deleted (i.e., "isEmpty" is set to true). In
     this case, "this->allNames" is set to the empty list, all entries are
     deleted from "this->oldEntries", "exCommonNames" is set to a new bit
     vector containing the previous set of common names, "exUncommonNames"
     is set to "NULL", "packMask" is set to a new, empty bit vector, and
     "reMap is set to a new, empty table. */

  // read/write
  void Read(std::ifstream &ifs, std::streampos origin, int version,
	    /*OUT*/ SPKFileRep::Header* &hdr, bool readEntries = true,
	    bool readImmutable = true) throw (FS::EndOfFile, FS::Failure);
  /* REQUIRES FS::Posn(ifs) == ``start of <PKFile>'' */
  /* Fill in the fields of this "SPKFile" from "ifs" (namely, the
     <PKFHeaderTail>).

     If "readEntries" is true, the "hdr" is set to a newly-allocated record
     containing the <CFPHeader> information, and all of the PKFile's
     <PKEntries> records are stored in this SPKFile's "oldEntries" table.
     Otherwise, "hdr" is set to NULL, and "ifs" is left pointing just after
     the <PKFHeader>.

     If "readEntries" is true, then the "readImmutable" parameter is
     significant; otherwise, it is ignored. If "readImmutable" is true,
     then the mutable and immutable fields of all cache entries are read
     from disk into memory. If "readImmutable" is false, then the cache
     indices of the cache entries are negated, and the immutable "value",
     "kids", and "fps" fields are not read from disk. Instead, those
     pointers are made to point to records containing the length and
     position of the pickled representations of those values in the SPKFile.
     "readImmutable" should only be passed as "false" when the cache entries
     are intended to be written out again. */

  void Write(std::ifstream &ifs, std::ostream &ofs, std::streampos origin,
	     /*OUT*/ SPKFileRep::Header* &hdr) const throw (FS::Failure);
  /* REQUIRES VPKFile(SELF).mu IN LL */
  /* REQUIRES FS::Posn(ofs) == ``start of <PKFile>'' && hdr == NULL */
  /* Write this "SPKFile" to "ofs", and fill in "hdr" to correspond to the
     newly-allocated header information. The "hdr" is set to the written
     file's <PKFHeader> so it's relative offsets can be later back-patched.
     "origin" should be the location of the start of this <PKFile>, namely
     "FS::Posn(ofs)" at the start of the call. "ifs" is the seekable input
     file from which the data for the immutable cache fields is read. */

  // other methods
  void Debug(std::ostream &os, const SPKFileRep::Header &hdr,
	     std::streampos origin, bool verbose = false) const throw ();
  /* REQUIRES VPKFile(SELF).mu IN LL */
  /* Write an ASCII represenation of this PKFile to "os", where "hdr" is the
     corresponding header data, and "origin" is the location of the start of
     the PKFile within the MultiPKFile.

     If "verbose" is false, write a concise representation that only lists
     the primary key, the number of distinct secondary keys, the secondary
     keys themselves, and the number of entries stored under each secondary
     key. Otherwise, write a complete representation that includes the
     complete header information and contents of the entries as well. */  

  // dictionary types
  typedef Table<FP::Tag,CE::List*>::Default CFPEntryMap;
  typedef Table<FP::Tag,CE::List*>::Iterator CFPEntryIter;

  // accessors
  const FP::Tag PK() const throw ()
  { return this->pk; }
  PKFile::Epoch PKEpoch() const throw ()
  { return this->pkEpoch; }
  FV::Epoch NamesEpoch() const throw ()
  { return this->namesEpoch; }
  const FV::ListApp *AllNames() const throw ()
  { return &(this->allNames); }
  const BitVector *CommonNames() const throw ()
  { return &(this->commonNames); }
  const CFPEntryMap *OldEntries() const throw ()
  { return &(this->oldEntries); }
  const Text *SourceFunc() const throw ()
  { return this->sourceFunc; }

protected:
  // data fields
  FP::Tag pk;			// the PK for this PKFile
  PKFile::Epoch pkEpoch;      // epoch for this PKFile
  FV::Epoch namesEpoch;	// epoch for "allNames"
  FV::ListApp allNames;	// list of union of names in all entries
  BitVector commonNames;	// set of common names for all entries
  CFPEntryMap oldEntries;	// this PKFile's cache entries: CFP -> CE::List
  const Text *sourceFunc;     // definition site of corresponding function

  /* The "pkEpoch" records the epoch of this PKFile. It is
     incremented each time the PKFile is flushed to the stable cache.
     Flushing is not atomic, and the field is incremented before the
     disk write is done, so at times it will be ahead of the value on disk.

     The "namesEpoch" records the epoch of the "allNames" list. It is
     incremented whenever that list changes, which can occur either when a
     new entry is added, or when deletions are pending and the PKFile is
     flushed to disk.

     The "commonNames" bit vector indicates which names are common to all
     entries in this PKFile. It is interpreted relative to the "allNames"
     sequence.

     The "oldEntries" table maps common fingerprints to lists of cache
     entries that share the same common fingerprint. Hence, it stores all
     of this PKFile's entries. (In the VPKFile sub-type, it stores the
     ``warm'' entries that have been ``paged in'' from the stable cache.)

     The "sourceFunc" field is a string generated by the evaluator for
     debugging purposes only. It is meant to denote the site where the
     function corresponding to the entries in this PKFile is defined.
     A value of NULL means the field is uninitialized; it gets initialized
     when entries are added to this PKFile, but may remain uninitialized
     if all entries in the PKFile were created before this feature was
     added.
  */

  void AddEntryToTable(/*INOUT*/ CFPEntryMap &tbl,
		       const FP::Tag &cfp, CE::T *ce) const throw ();
  /* Add the cache entry "ce" to "tbl" under the common fingerprint
     "cfp". */ 

  virtual VPKFileChkPt *CheckPoint() /*!!const*/ throw ();
  /* Return a newly-allocated checkpoint of this "SPKFile". This does not
     set the "newUncommon" and "newCommon" fields of the checkpoint. */

  void ScanCommonTable(const CFPEntryMap &cfpTbl, const BitVector *toDelete,
		       /*INOUT*/ BitVector &uncommonJoin,
		       /*INOUT*/ BitVector &uncommonMeet,
		       /*INOUT*/ int &totalEntries,
		       /*INOUT*/ bool &delOne) const throw ();
  /* REQUIRES VPKFile(SELF).mu IN LL */
  /* Over the set of entries in "cfpTbl" whose indices do not correspond to
     set bits in "toDelete", augment "uncommonJoin" to include the union of
     all their uncommon names, reduce "uncommonMeet" so as not to include
     any name not set in all of their uncommon names, and increment
     "totalEntries" by the number of (undeleted) entries considered.
     "delOne" is set to true if some entry in "cfpTbl" has an index
     corresponding to a set bit in "toDelete". */

  void ScanList(const CE::List *head, const BitVector *toDelete,
		/*INOUT*/ BitVector &uncommonJoin,
		/*INOUT*/ BitVector &uncommonMeet,
		/*INOUT*/ int &totalEntries,
		/*INOUT*/ bool &delOne) const throw ();
  /* REQUIRES VPKFile(SELF).mu, ListLock(*head) IN LL */
  /* Same as "ScanCommonTable" above, but only consider the entries in the
     list "head" whose indices do not correspond to set bits in "toDelete".
     "delOne" is set to true if some entry in the list "head" has an index
     corresponding to a set bit in "toDelete". */

  CE::List *ExtractEntries(/*INOUT*/ CFPEntryMap &cfpTbl) throw ();
  /* REQUIRES VPKFile(SELF).mu IN LL */
  /* Return a list of all entries in "cfpTbl", and clear the table. */

  void UpdateCommonTable(/*INOUT*/ CFPEntryMap &cfpTbl,
			 const BitVector *toDelete,
			 const BitVector *exCommonNames,
			 const BitVector *exUncommonNames,
			 const BitVector *packMask,
			 const IntIntTblLR *reMap, bool unlazyTag = true)
    throw ();
  /* REQUIRES cfpTbl.Size() > 0 */
  /* REQUIRES (packMask == NULL) == (reMap == NULL) */
  /* Recompute the new tags for all entries in "cfpTbl" whose indices do not
     correspond to set bits in "toDelete", and store them back under their
     new common fingerprints. Use "exCommonNames", "exUncommonNames",
     "packMask", and "reMap" to change the common names of all entries. If
     "unlazyTag" is true, then the fingerprints for the entries' uncommon
     tags are also computed. For each entry, compute the maximum timestamp
     of its common names.

     Note: The "commonNames" for this PKFile should *not* have been
     packed according to "packMask" before this method is called. */

  static CE::T* Lookup(std::ifstream &ifs, std::streampos origin, int version,
		       const FP::Tag &commonTag, const FP::List &fps)
    throw (FS::EndOfFile, FS::Failure);
  /* REQUIRES FS::Posn(ifs) == origin == ``start of <PKFile>'' */
  /* Assuming "ifs" is properly positioned at the start of the stable
     PKFile, lookup the cache entry with common fingerprint "commonTag" and
     with individual fingerprints for all free variables for this PKFiles
     specified by "fps". "origin" specifies the location of the start of the
     SPKFile within the SMultiPKFile, and "version" specifies the version
     number of the SMultiPKFile on disk (i.e., its format).

     In the event of a hit, return the resulting cache entry; otherwise,
     return NULL. */

private:
  void AddEntries(const VPKFileChkPt &chkpt, const BitVector *toDelete,
		  const BitVector *exCommonNames,
		  const BitVector *exUncommonNames,
		  const BitVector *packMask, const IntIntTblLR *reMap,
		  bool unlazyTag)
    throw ();
  /* REQUIRES (packMask == NULL) == (reMap == NULL) */
  /* Add the entries in "chkpt" to "this->entries", recomputing their common
     fingerprints and updating their tags if necessary. The uncommon tags
     are unlazied iff "unlazyTag" is true.

     Note: The "commonNames" for this PKFile should *not* have been
     packed according to "packMask" before this method is called. */

  void UpdateList(/*INOUT*/ CFPEntryMap &cfpTbl,
		  const CE::List *ents, const BitVector *toDelete,
		  const BitVector *exCommonNames,
		  const BitVector *exUncommonNames,
		  const BitVector *packMask, const IntIntTblLR *reMap,
		  bool unlazyTag)
    throw ();
  /* REQUIRES VPKFile(SELF).mu, ListLock(*ents) IN LL */
  /* REQUIRES (packMask == NULL) == (reMap == NULL) */
  /* Add the entries in "ents" to "cfpTbl", recomputing their common
     fingerprints and updating their tags if necessary. If "exCommonNames"
     is non-NULL, then the "uncommonNames" of all entries in "ents" are
     augmented to include "exCommonNames". If "exUncommonNames" is
     non-NULL, then the "uncommonNames" of all entries in "ents" are
     decreased so as not to include "exUncommonNames". If "unlazyTag" is
     true, then the fingerprints of their uncommon tags will also be
     computed.

     Note: The "commonNames" for this PKFile should *not* have been
     packed according to "packMask" before this method is called. */

  CE::List *ReadEntries(std::ifstream &ifs, int version,
			bool readImmutable = true)
    throw (FS::EndOfFile, FS::Failure);
  /* REQUIRES FS::Posn(ifs) == ``start of <PKEntries>'' */
  /* Read a single <PKEntries> record from "ifs" and return the
     corresponding list of newly allocated entries (in order). The
     immutable fields of the cache entries are read into memory iff
     "readImmutable" is "true". */

  void WriteEntries(std::ifstream &ifs, std::ostream &ofs,
		    CE::List *entryList)
    const throw (FS::Failure);
  /* REQUIRES VPKFile(SELF).mu, ListLock(*entryList) IN LL */
  /* REQUIRES FS::Posn(ofs) == ``start of <PKEntries>'' */
  /* Write a single <PKEntries> record formed from the entries in
     "entryList" (in order) to "ofs". "ifs" is the input stream from
     which any unread immutable cache entry fields are read. */

  static std::streampos LookupCFP(std::ifstream &ifs, std::streampos origin,
				  int version, const FP::Tag &cfp,
				  const SPKFileRep::Header &hdr)
    throw (FS::EndOfFile, FS::Failure);
  /* REQUIRES FS::Posn(ifs) == ``start of <CFPTypedHeader>'' */
  /* Returns the index of the <PKEntries> for common fingerprint "cfp" in
     the stream "ifs" if any exist; -1 otherwise. */

  static std::streampos LookupCFPInListV1(std::ifstream &ifs,
					  std::streampos origin,
					  const FP::Tag &cfp, int num)
    throw (FS::EndOfFile, FS::Failure);
  /* REQUIRES FS::Posn(ifs) == ``start of <CFPSeqHeader>'' */
  /* Returns the index of the <PKEntries> for common fingerprint "cfp" in
     the stream "ifs" if any exist; -1 otherwise. This routine assumes
     the entries are an arbitrary list. */

  static std::streampos LookupCFPInSortedListV1(std::ifstream &ifs,
						std::streampos origin,
						const FP::Tag &cfp, int num)
    throw (FS::EndOfFile, FS::Failure);
  /* REQUIRES FS::Posn(ifs) == ``start of <CFPSeqHeader>'' */
  /* Returns the index of the <PKEntries> for common fingerprint "cfp" in
     the stream "ifs" if any exist; -1 otherwise. This routine requires
     the common fingerprints to be listed in sorted order. */

  static CE::T* LookupEntryV1(std::ifstream &ifs, int version,
			      const FP::List &fps)
    throw (FS::EndOfFile, FS::Failure);
  /* REQUIRES FS::Posn(ifs) == ``start of <PKEntries>'' */
};

#endif // _SPK_FILE_H
