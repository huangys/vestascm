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


// CacheEntry.H -- defines the cache entry class "CE::T" (and lists thereof)

#ifndef _CACHE_ENTRY_H
#define _CACHE_ENTRY_H

#include <Basics.H>
#include <FS.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <FP.H>
#include <BitVector.H>
#include <Model.H>
#include <CacheIndex.H>
#include <VestaVal.H>

#include "IntIntTblLR.H"
#include "Combine.H"

namespace CE
{
  // CE::T -- A cache entry

  /* The methods of a "CE::T" are unsynchronized. However, each entry
     "ce" is naturally associated with a "VPKFile". We denote this by
     "VPKFile(ce)". The methods of a "CE::T" that read or write
     potentially changing parts of the entry state require that the lock
     "VPKFile(SELF).mu" be held. */

  class T {
  public:
    // constructors/destructor
    T(CacheEntry::Index ci, BitVector *uncommonNames, FP::List *fps,
      IntIntTblLR *imap, VestaVal::T *value, CacheEntry::Indices *kids,
      Model::T model) throw ();
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* Initialize the new cache entry, where "ci" is the index of the
       entry and "uncommonNames" is the set of uncommon free variables
       for this entry (relative to the names stored in "VPKFile(SELF)").
       "fps[imap(i)]" is the fingerprint of the "i"th name in the list of
       names "VPKFile(SELF).allNames", "value" is the entry's value,
       "kids" are the indices of this entry's children, and "model" is the
       model for this entry. All pointer arguments are required to be
       non-NULL.

       As opposed to the "uncommonNames" bit vector, the "fps" array is
       not sparse. It contains exactly as many values as there are free
       variables for this entry.

       The responsibility for deleting the space for "uncommonNames",
       "fps", "imap", "value", and "kids" is transferred from caller to
       callee. */

    T(const T *ce) throw ();
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* Make a copy of the entry "ce". This copies the mutable fields and
       uses references for the immutable fields where possible. It is like
       a copy constructor, but requires an explicit pointer to an existing
       entry as an argument, so it is not likely to be used inadvertently.
       The regular copy constructor is still private. */

    T(RecoveryReader &rd) throw (VestaLog::Error, VestaLog::Eof)
      : value(0), kids(0), fps(0),
	value_immut(0), kids_immut(0)
    { Recover(rd); }
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* Recover the cache entry from "rd" including the ``extra''
       fields "imap" and "fps". */

    T(std::ifstream &ifs, bool old_format = false,
      bool readImmutable = true)
      throw (FS::EndOfFile, FS::Failure)
      : value(0), kids(0), fps(0),
	value_immut(0), kids_immut(0)
    { Read(ifs, old_format, readImmutable); }
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* Read the cache entry from "ifs". This method does not read
       the ``extra'' fields "imap" and "fps"; use the "ReadExtras"
       routine below to read them. See the "Read" method below for
       the meaning of the "readImmutable" argument. */

    // computation functions
    FP::Tag *CombineFP(const BitVector& bv) const throw ()
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* Returns a new fingerprint that is the result of combining the
       fingerprints "fps[imap(i)]" for all "i" such that "bv.Read(i)".
       It is an unchecked runtime error for "bv" to have a bit set
       whose index is not in the domain of "imap". */
    { return NEW_PTRFREE_CONSTR(Combine::FPTag, (*fps, bv, imap)); }

    bool FPMatch(const FP::List& pkFPs) throw ();
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* Return "true" iff the fingerprints "pkFPs" constitute a match
       against the uncommon FP's stored in this entry. The "uncommonNames"
       bit vector for this entry is used to compute the combined uncommon
       fingerprint for the fingerprints "pkFPs", and the result is
       compared against the entry's combined uncommon fingerprint. */

    void Pack(const BitVector *packMask, const IntIntTblLR *reMap)
      throw ();
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* REQUIRES (packMask == NULL) == (reMap == NULL) */
    /* Pack this enty's "uncommonNames" and "imap" according to "packMask"
       and "reMap". If both "packMask" and "reMap" are NULL, this is a
       no-op. Otherwise, the entry's fields are updated to reflect the
       fact that names corresponding to unset bits in "packMask" have been
       deleted from this entry's PKFile's "allNames" list.

       In particular, the entry's "uncommonNames" are packed according
       to "packMask": all bits in "uncommonNames" corresponding to unset
       bits in "packMask" are removed (these bits are all required to be
       unset in "uncommonNames" as well), and the remaining bits are
       packed toward the LSB. "imap" is updated according to "reMap":
       each entry "k -> v" is replaced by the entry "k' -> v", where
       "k -> k'" in "reMap". */

    void CycleNames(const BitVector &delBV,
		    const IntIntTblLR &delMap) throw ();
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* REQUIRES !delBV.IsEmpty() */
    /* Update this cache entry's "uncommonNames" and "imap" fields to
       reflect the fact that names with indices corresponding to set bits
       in "delBV" are being assigned new indices according to
       "delMap". 

       In particular, the domain of "delMap" is precisely the set of
       set bits in "delBV"; the entry "old -> new" in "delMap" means
       that names with index "old" should now have index "new". The
       corresponding ``old'' bits in "uncommonNames" are required to all
       be set; they are reset and the corresponding ``new'' bits (which
       are required to all be initially reset) are set. Similarly, for
       each entry "old -> new" in "delMap", the enty "old -> ix" is
       replaced by the enty "new ->ix" in "imap". "imap" is required to
       have all of the ``old'' values in its initial domain, but none of
       the ``new'' values. */ 

    void Update(const BitVector *exCommonNames,
		const BitVector *exUncommonNames, const BitVector *packMask,
		const IntIntTblLR *reMap) throw ();
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* REQUIRES (packMask == NULL) == (reMap == NULL) */
    /* Update this entry's "uncommonNames" (and corresponding uncommon
       fingerprint) according to the arguments. Bits in "exCommonNames"
       (if it is non-NULL) are set in "uncommonNames", and bits in
       "exUncommonNames" (if it is non-NULL) are reset in "uncommonNames".
       If "uncommonNames" is changed at all, the uncommon fingerprint is
       recomputed. Finally, "uncommonNames" and "imap" are ``packed''
       according to "packMask" and "reMap" as described by the "Pack"
       method above. */

    void XorUncommonNames(const BitVector &mask) throw ();
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* Exclusive-OR the "mask" bits with this entry's "uncommonNames",
       and recompute the entry's "uncommonTag" since the set of uncommon
       names has changed. */

    void UnlazyUncommonFP() throw ()
    { this->uncommonTag.UnlazyFPVal(*fps, *uncommonNames, imap); }
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* Unlazy the tag of the uncommon names. */

    bool UncommonFPIsUnlazied() throw ()
    { return this->uncommonNames->Size() == 0
	|| this->uncommonTag.FPValIsUnlazied(); }

    // Consistency checking
    static bool CheckUsedNames(const BitVector *commonNames,
			       const BitVector *uncommonNames,
			       const IntIntTblLR* imap,
			       const FP::List *fps,
			       /*OUT*/ unsigned int &missing) throw ();
    bool CheckUsedNames(const BitVector *commonNames,
			/*OUT*/ unsigned int &missing)
      const throw ()
    {
      return CE::T::CheckUsedNames(commonNames, this->uncommonNames,
				   this->imap, this->fps,
				   missing);
    }
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* Check that all names used by this entry (determined from
       imap/fps) are either in this entry's uncommonNames or the
       passed commonNames.  (Note that commonNames can be NULL if
       this is a new uncommon entry, in which case its own
       uncommonNames should include all the names the entry uses.)
       The result indicates whether there are any names missing
       from the union of uncommonNames and commonNames.  If it
       returns true (indicating a problem), "missing" is set to
       the index of the first name found not marked as used.  The
       class member (static version) is provided to make it
       possible to perform this consistency check prior to
       creating a CE::T object. */

    // Accessor functions
    const BitVector* UncommonNames() const throw ()
    { return this->uncommonNames; }
    /* REQUIRES VPKFile(SELF).mu IN LL */
    CacheEntry::Index CI() const throw () { return this->ci; }
    const VestaVal::T* Value() const throw () { return this->value; }
    const CacheEntry::Indices *Kids() const throw () { return this->kids; }
    const IntIntTblLR *IMap() const throw () { return this->imap; }
    /* REQUIRES VPKFile(SELF).mu IN LL */
    const FP::List *FPs() const throw () { return this->fps; }

    // log/recover
    void Log(VestaLog& log) const throw (VestaLog::Error);
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* Append this cache entry to "log", including the ``extra''
       fields "imap" and "fps". Requires "log" to be in the
       "logging" state. */
    void Recover(RecoveryReader &rd)
      throw (VestaLog::Error, VestaLog::Eof);
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* Recover the cache entry from "rd", including the ``extra''
       fields "imap" and "fps". */

    // write/read
    /* Note: neither "Write" nor "Read" writes/reads the ``extra'' fields
       "imap" and "fps"; use the "WriteExtras" and "ReadExtras" methods
       below to write/read them. */
    void Write(std::ostream &ofs, std::ifstream *ifs = NULL) const
      throw (FS::Failure);
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* If the immutable fields "value" and "kids" were not read into
       memory, but are instead represented by portions of a file, then
       "ifs" must be non-NULL; its bytes are written from there. */
    void Read(std::istream &ifs, bool old_format = false,
	      bool readImmutable = true)
      throw (FS::EndOfFile, FS::Failure);
    /* REQUIRES VPKFile(SELF).mu IN LL */
    /* The immutable fields "value" and "kids" are read into memory by
       "Read" iff "readImmutable" is "true". If "readImmutable" is false,
       those fields are instead set to pointers to "ImmutableVal" objects
       that record the starting position and length of the values in
       "ifs". */

    // write/read extras
    void WriteExtras(std::ostream &ofs, std::ifstream *ifs = NULL)
      const throw (FS::Failure);
    /* REQUIRES VPKFile(SELF).mu IN LL */
    void ReadExtras(std::istream &ifs, bool old_format = false,
		    bool readImmutable = true)
      throw (FS::EndOfFile, FS::Failure);
    /* REQUIRES VPKFile(SELF).mu IN LL */

    // print
    void Debug(std::ostream &os, int indent = 0) const throw ();
    /* REQUIRES VPKFile(SELF).mu IN LL */
    void DebugExtras(std::ostream &os, int indent = 0) const throw ();
    /* REQUIRES VPKFile(SELF).mu IN LL */

  private:
    // data fields
    CacheEntry::Index ci;          // readonly after init; see below
    Model::T model;                // readonly after init
    BitVector *uncommonNames;      // mutable
    Combine::XorFPTag uncommonTag; // mutable
    VestaVal::T *value;            // readonly after init; see below
    CacheEntry::Indices *kids;     // readonly after init; see below
    ImmutableVal *value_immut;
    ImmutableVal *kids_immut;

    // "extras"
    IntIntTblLR *imap;             // mutable
    FP::List *fps;	               // readonly after init

    /* "ci" is the index of this cache entry.

       When updating a PKFile on disk, rather than reading the entire
       data of each entry into memory immutable portions of the entry
       can be copied from the old file to the new file.
       "ImmutableVal" objects that record the starting position and
       length of portions the existing file are used in that case.
       The ImmutableVal pointers in "value_immut" and "kids_immut" are
       used instead of "value" and "kids" when not reading the entry
       data into memory.

       "uncommonNames" is the bit vector of this entry's uncommon names;
       it is interpreted relative to all the names of this entry's PKFile
       (i.e., the PKFile's "allNames" field).

       The field "uncommonTag" is the combined fingerprint of the
       fingerprints for this entry's uncommon names. We do not store the
       combined fingerprint of the common names in the cache entry. For
       entries that don't have all common names it is unnecessary, and
       for entries that do have all common names, the entry will be
       stored in a table indexed by its common fingerprint.
 
       The fields "imap" and "fps" are ``extra'' fields that are not
       required to perform a lookup. However, they are required to
       update the entry when the PKFile's set of common names changes.
       The "imap" is a table mapping indices of names in this entry's
       PKFile to the indices of the corresponding fingerprint in the
       "fps" list. Hence, the *domain* of "imap" is potentially sparse,
       while the *range* of "imap" is the interval "[0, fps->len)".
 
       The fingerprints "fps" are read-only after initialization. They
       are the fingerprints for all of this entry's free variables,
       stored in the order in which they were passed to the AddEntry
       method when the entry was created.

       Hence, the free variables for an entry "ce" are those names in
       "VPKFile(ce).allNames" corresponding to set bits in the abstract
       bit vector:

       namesBV(ce) =  VPKFile(ce).commonNames + *(ce.uncommonNames).

       The fingerprint of name "i" (i.e., "VPKFile(ce).allNames[i]") is:

       ce.fps[ce.imap[i]].

       If we write "num(bv)" to represent the number of set bits
       in the bit vector "bv", and "domain(tbl)" and "range(tbl)" to
       represent the domain and range of the table "tbl", then we have the
       following invariants:

       I0. ce.fps->len = num(namesBV(ce))
       I1. domain(imap) = namesBV(ce)
       I2. range(imap) = { i : 0 <= i < ce.fps->len }

       The "uncommonTag" is computed from the "uncommonNames", "imap",
       and "fps" fields. Define:

       FPsXOR(bv, map) =
       { XOR i | bv.Read(i) | fps[map[i]].w }

       FPsCombine(bv, map) =
       { FP::T::Extend i | bv.Read(i) | fps[map[i]].w }

       That is, "FPsXOR(bv, map)" is the exclusive OR of the words in the
       entry's "fps" field for the set bits of "bv" (after adjusting the
       bit index via "map"). Similarly, "FPsCombine(bv, map)" is the
       combined fingerprint of those same words (in order of increasing
       bit index in "bv"). For a particular value of "ce.uncommonNames"
       and "ce.imap", the invariant for the "ce.uncommonTag" field is
       then given by: 

       I3. ce.uncommonTag.xor = FPsXOR(ce.uncommonNames, ce.imap)
       /\ ce.uncommonTag.fp = FPsCombine(ce.uncommonNames, ce.imap)

       Whenever "uncommonNames" or "imap" changes, "uncommonTag" must be
       re-computed to establish I3.

       On a lookup, the "imap" field is only necessary if either the "xor"
       or "fp" field of the "uncommonTag" must be computed. When a new
       entry is added, we always compute the "xor" field. But the "fp"
       field is computed lazily. Hence, we must keep the "imap" in memory
       for new entries. However, when a new entry is flushed to disk, the
       "fp" field is unlazied. Hence, we do not have to read the "imap"
       field into memory when we read an entry off the disk. */

    // hide copy constructor from clients
    T(const T&);
  };

  // A list of cache entries
  //
  // Since the "tail" field of a list cell is mutable, reads and
  // writes on it must be protected by a lock. This is most likely
  // the lock that protects the field that points to the head of
  // the linked list. It is denoted here by "ListLock(SELF)".
  class List {
  public:
    // constructor
    List(T *head, List *tail=(List *)NULL) throw ()
    /* Return a new list with formed by consing together the element
       "head" with the list "tail". */
    { this->head = head; this->tail = tail; }

    // length
    static unsigned Length(const List *list) throw ();

    // accessors
    T* Head() const throw () { return head; }
    List* Tail() const throw ()
    /* REQUIRES ListLock(SELF) IN LL */
    { return tail; }
    void SetTail(List *newTail) throw ()
    /* REQUIRES ListLock(SELF) IN LL */
    { tail = newTail; }
  private:
    T *head;     // readonly after init
    List *tail;  // mutable
  };
}

#endif // _CACHE_ENTRY_H
