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
// VDirChangeable.H
//
// Derived class for in-memory representation of Vesta directories
// that can change, including appendable and mutable.  The
// representation is also adequate for immutable directories, though
// it wastes a little space in that case.
//
// The purpose of this class is to provide methods that operate on the
// special packed representation.  Most of the directories that exist
// at any moment will not have objects of this class corresponding to
// them; they will exist only in the packed representation.
//

#ifndef _VDIRCH
#define _VDIRCH 1

#include "VestaSource.H"
#include "VDirVolatileRoot.H"
#include "VMemPool.H"
#include "AccessControl.H"
#include "ShortIdRefCount.H"
#include <assert.h>

// Below we describe the packed representation of a VDirChangeable
// using a pseudo-C++ notation.  Differences from real C++ are:
// (1) The keyword "packed" indicates the struct contains no compiler-
// inserted padding; its fields may be arbitrarily aligned.  (2)
// "@" in place of "*" indicates a 32-bit pointer.  On the Alpha this
// is an offset within a 2^32 byte range of the address space within
// which directories are constrained to lie.  (3) An array size can be
// a field of the struct or empty, rather than a constant.  (4) Bit
// fields are always packed into bytes in little-endian order.
//
// packed struct VDirChangeableRep {
//     Bit8 hasName: 1;        // bool, used by GC
//     Bit8 visited: 1;        // bool, used by GC
//     Bit8 isMoreOrBase: 2;
//     Bit8 typecode: 4;       // If immutable,  VMemPool::vDirImmutable;
//                             // if appendable, VMemPool::vDirAppendable;
//                             // else           VMemPool::vDirChangeable.
//     union {
//         Bit32 null = 0;              // if isMoreOrBase = isNeither
//         VDirChangeableRep@ more;     // if isMoreOrBase = isMore
//         VDirChangeableRep@ base;     // if isMoreOrBase = isBase 
//     } moreOrBase;
//     Bit32 timestamp;                 // modified time (32-bit time_t)
//     union {
//         Bit32 pseudoInode;           // mutable or volatile directory
//         ShortId sid;                 // immutable directory
//         Bit32 null = 0;              // others
//     };
//     union {
//         VDirChangeableRep@ snapshot; // mutable directory (padded)
//         FP::Tag fptag;               // immutable directory
//         FP::Tag unusedTag;           // others
//     };
//     packed struct {
//         Bit8 master: 1;     // bool
//         Bit8 inUse: 1;      // bool, used by GC
//         Bit8 hasEFPTag: 1;  // efptag field present
//         Bit8 sameAsBase: 1; // bool: map the LongId of this name in
//			       //  the base to this entry.
//         Bit8 type: 4;       // VestaSource::typeTag
//         union {
//             Bit32 null = 0;            // ghost, stub
//             ShortId sid;               // immutableFile, mutableFile
//             VForward@ fwd;             // deleted
//             VDirChangeableRep@ cdir;   // directory
//             Bit32 gapsize;             // gap (=number of unused indices)
//         } value;
//         AttributesRep@ attrib;
//         packed union {
//             FP::Tag efptag; // if hasEFPTag == 1
//             /*empty*/;      // if hasEFPTag == 0 (occupies no space)
//         };
//         Bit8 arcLen;
//         char arc[arcLen];   // NUL-padded but not terminated
//     } entries[];
//     Bit8 endMark;  // Must be 0xff; identifies end of entries.
//                    //  This bit pattern can't begin an entry because
//                    //  0xf is not a valid type tag.
//     Bit32 freeLen;
//     Bit8 free[freeLen];  // free space for expansion
// };

// Some numbers derived from above
#define VDIRCH_FLAGS 0
#define VDIRCH_MOREORBASE 1
#define VDIRCH_TIMESTAMP 5
#define VDIRCH_ID 9
#define VDIRCH_FPTAG 13
#define VDIRCH_ENTRIES 29
#define VDIRCH_ENDMARK 29 // if no entries
#define VDIRCH_FREELEN 30 // if no entries
#define VDIRCH_MINSIZE 34 // if no entries
#define VDIRCH_EFLAGS 0
#define VDIRCH_EVALUE 1
#define VDIRCH_EATTRIB 5
// if efptag omitted:
#define VDIRCH_E1ARCLEN 9
#define VDIRCH_E1ARC 10
#define VDIRCH_E1MINSIZE 10 // if arcLen=0
// if efptag present:
#define VDIRCH_EFPTAG 9
#define VDIRCH_E2ARCLEN 25
#define VDIRCH_E2ARC 26
#define VDIRCH_E2MINSIZE 26 // if arcLen=0

class VDirChangeable : public VestaSource {
  public:
    // Our versions of virtual methods from base class.
    void resync(AccessControl::Identity who =NULL) throw ();
    ShortId shortId() throw ();
    VestaSource::errorCode
      setTimestamp(time_t ts, AccessControl::Identity who =NULL) throw ();
    VestaSource::errorCode lookup(Arc arc, VestaSource*& result,
		 AccessControl::Identity who =NULL,
		 unsigned int indexOffset =0) throw();
    VestaSource::errorCode 
      reallyDelete(Arc arc, AccessControl::Identity who =NULL,
		 bool existCheck =true,	time_t timestamp =0) throw();
    VestaSource::errorCode
      insertFile(Arc arc, ShortId sid, bool mast,
		 AccessControl::Identity who =NULL,
		 VestaSource::dupeCheck chk =VestaSource::dontReplace,
		 VestaSource** newvs =NULL, time_t timestamp =0,
		 FP::Tag* fptag =NULL) throw();
    VestaSource::errorCode
      insertMutableFile(Arc arc, ShortId sid, bool mast,
		 AccessControl::Identity who =NULL,
		 VestaSource::dupeCheck chk =VestaSource::dontReplace,
		 VestaSource** newvs =NULL, time_t timestamp =0) throw();
    VestaSource::errorCode
      insertImmutableDirectory(Arc arc, VestaSource* dir, bool mast,
		 AccessControl::Identity who =NULL,
		 VestaSource::dupeCheck chk =VestaSource::dontReplace,
		 VestaSource** newvs =NULL, time_t timestamp =0,
		 FP::Tag* fptag =NULL) throw();
    VestaSource::errorCode
      insertAppendableDirectory(Arc arc, bool mast, 
		 AccessControl::Identity who =NULL,
		 VestaSource::dupeCheck chk =VestaSource::dontReplace,
		 VestaSource** newvs =NULL, time_t timestamp =0) throw();
    VestaSource::errorCode
      insertMutableDirectory(Arc arc, VestaSource* dir, bool mast, 
		 AccessControl::Identity who =NULL,
		 VestaSource::dupeCheck chk =VestaSource::dontReplace,
		 VestaSource** newvs =NULL, time_t timestamp =0) throw();
    VestaSource::errorCode
      insertGhost(Arc arc, bool mast, 
		 AccessControl::Identity who =NULL,
		 VestaSource::dupeCheck chk =VestaSource::dontReplace,
		 VestaSource** newvs =NULL, time_t timestamp =0) throw();
    VestaSource::errorCode
      insertStub(Arc arc, bool mast, 
		 AccessControl::Identity who =NULL,
		 VestaSource::dupeCheck chk =VestaSource::dontReplace,
		 VestaSource** newvs =NULL, time_t timestamp =0) throw();
    VestaSource::errorCode
      renameTo(Arc arc, VestaSource* fromDir, Arc fromArc,
		 AccessControl::Identity who =NULL,
	         VestaSource::dupeCheck chk =VestaSource::dontReplace,
		 time_t timestamp =0) throw();
    VestaSource::errorCode
      getBase(VestaSource*& result, AccessControl::Identity who =NULL) throw();
    VestaSource::errorCode
      list(unsigned int firstIndex,
     	         VestaSource::listCallback callback, void* closure,
		 AccessControl::Identity who =NULL,
	         bool deltaOnly =false, unsigned int indexOffset =0) throw();

    VestaSource::errorCode
      lookupIndex(unsigned int index, VestaSource*& result,
		  char* arcbuf =NULL) throw();

    // This method provides support for makeMutable.  The directory
    // must already be mutable (or volatile).  Kludge warning: The
    // method assumes that this->longid and index have been obtained
    // by splitting the longid obtained as the result of a lookup done
    // atomically with the current call under VRLock.  (In the
    // implementation, the method fails if index names a forwarding
    // pointer).
    VestaSource::errorCode
      makeIndexMutable(unsigned int index, VestaSource*& result,
		       ShortId sid =NullShortId,
		       Basics::uint64 copyMax = (Basics::uint64) -1,
		       AccessControl::Identity who =NULL) throw();

    // This method provides support for makeFilesImmutable.  The index
    // must belong directly to a mutable file (not to a forwarding
    // pointer, not to an immutable file that has been copied to a
    // mutable directory, etc.).  The caller provides a size threshold
    // for fingerprinting by content.  The fingerprint and correct
    // shortid is determined, possibly copying or unlinking the
    // current mutable shortid (depending on whether there are other
    // references and whether the contents are the same as an existing
    // shortid).  If the same shortid is kept, it will have all
    // pending writes flushed to disk with fsync(2) and be chmod-ed
    // read-only.  (See also makeEntryImmutable.)
    void makeIndexImmutable(unsigned int index,
			    unsigned int fpThreshold) throw();

    // This method provides support for log replay.  The index must
    // belong directly to a mutable file (not to a forwarding pointer,
    // not to an immutable file that has been copied to a mutable
    // directory, etc.).  The file type is changed to immutable, the
    // fingerprint is changed if fptag is non-NULL, and the shortid is
    // changed if newsid is not NullShortId.  (See also
    // makeEntryImmutable.)
    void makeIndexImmutable(unsigned int index,
			    const FP::Tag* fptag =NULL,
			    ShortId newsid =NullShortId) throw();

    // This method provides support for copyToMutable.  The directory
    // must already be mutable.  Essentially a limited version of
    // makeIndexMutable that copies the directory entry to a
    // sameAsBase entry in the mutable directory without actually
    // making it mutable.  (As with makeIndexMutable, it's assumed
    // that this->longid and index have been obtained by splitting the
    // longid obtained as the result of a lookup done atomically with
    // the current call while holding a lock on the directory
    // structure.)
    VestaSource::errorCode
      copyIndexToMutable(unsigned int index, VestaSource*& result,
			 AccessControl::Identity who =NULL) throw();

    // Turn the master flag on or off.  Use with care to preserve the
    // replication invariants!
    errorCode setIndexMaster(unsigned int index, bool state,
			     AccessControl::Identity who =NULL) throw();

    // Garbage collection support.  The closure argument to the
    // sweepCallback should be an ostream* representing an open
    // ShortIdsFile.  All reachable ShortIds found in the sweep are
    // appended to this file.
    void mark(bool byName =true, ArcTable* hidden =NULL) throw();
    static void markCallback(void* closure, VMemPool::typeCode type) throw();
    static bool sweepCallback(void* closure, VMemPool::typeCode type,
			      void* addr, Bit32& size) throw();
    static void rebuildCallback(void* closure, VMemPool::typeCode type,
				void* addr, Bit32& size) throw();
    
    // Checkpointing
    Bit32 checkpoint(Bit32& nextSP, std::fstream& ckpt) throw();

    // Explicit free support
    void freeTree() throw();

    errorCode
      measureDirectory(/*OUT*/directoryStats &result,
		       AccessControl::Identity who =NULL)
      throw (SRPC::failure);

    errorCode
      collapseBase(AccessControl::Identity who =NULL)
      throw (SRPC::failure);

    // Constructors
    enum { defaultRepSize = 512 }; // must hold >=1 max sized entry
    VDirChangeable(VestaSource::typeTag type,
		   int size =defaultRepSize, // size= initial size in bytes
		   ShortIdRefCount* sidref =NULL)
      throw();

    VDirChangeable(VestaSource::typeTag type, Bit8* existingRep,
		   ShortIdRefCount* sidref =NULL) throw();

    // Build the ShortIdRefCount for this mtuable directory.  Only
    // valid on the mutable root.
    void buildMutableSidref() throw();

    // Re-build the ShortIdRefCount for this mtuable directory and see
    // if thr counts match.  Only valid on the mutable root.  If
    // "correct" is true, then any inconsistency will simply result in
    // a message and the reference count will be corrected.  If
    // "correct" is false, exit with abort.
    void checkMutableSidref(bool correct = false) throw();
    
    // Print memory usage stats using dprintf
    static void printStats() throw ();

    virtual VestaSource *copy() throw();

    virtual void debugDump(std::ostream &dump) throw ();

  private:
    // Cached values are valid iff repEndCache != NULL
    Bit8* repEndCache;              // Cached pointer to endMark of rep
    unsigned int nextRawIndexCache; // Number of entries + 1
    Bit8* baseCache;                // Pointer to rep of base, if any
    Bit8* lastRepBlockCache;        // Last block of rep
    int totalRepSizeCache;          // Total non-free bytes of rep,
                                    // not counting outdated entries.

    // Accessors for the packed representation.  Those for entries
    // take a "Bit8* entry" argument, which is simply a pointer within
    // the representation.  Those that may access another block of
    // representation at the end of a more pointer take the (current
    // block of) rep as an argument.
  public:
    inline bool hasName() throw() { return (bool) (rep[VDIRCH_FLAGS] & 1); };
    inline void setHasName(bool newval) throw()
      { rep[VDIRCH_FLAGS] = (rep[VDIRCH_FLAGS] & 0xfe) | (int) newval; };
    inline bool visited() throw()
      { return (bool) ((rep[VDIRCH_FLAGS] & 2) != 0); };
    inline void setVisited(bool newval) throw()
      { rep[VDIRCH_FLAGS] =
	  (rep[VDIRCH_FLAGS] & 0xfd) | ((int)newval << 1); };
  private:
    inline bool hasName(Bit8* repb) throw()
      {	return (bool) (repb[VDIRCH_FLAGS] & 1); };
    inline void setHasName(Bit8* repb, bool newval) throw()
      { repb[VDIRCH_FLAGS] = (repb[VDIRCH_FLAGS] & 0xfe) | (int) newval; };
    inline bool visited(Bit8* repb) throw()
      { return (bool) ((repb[VDIRCH_FLAGS] & 2) != 0); };
    inline void setVisited(Bit8* repb, bool newval) throw()
      { repb[VDIRCH_FLAGS] =
	  (repb[VDIRCH_FLAGS] & 0xfd) | ((int)newval << 1); };

    enum { isNeither = 0, isMore = 1, isBase = 2 };
    static inline int isMoreOrBase(Bit8 *repb) throw()
      { return (repb[VDIRCH_FLAGS] >> 2) & 3; };
    static inline void setIsMoreOrBase(Bit8* repb, int newval) throw()
      { repb[VDIRCH_FLAGS] = (repb[VDIRCH_FLAGS] & 0xf3) | (newval << 2); };
    static inline Bit32 moreOrBase(Bit8 *repb) throw()
      { Bit32 val; memcpy(&val, &repb[VDIRCH_MOREORBASE], 4); return val; };
    static inline void setMoreOrBase(Bit8 *repb, Bit32 newval) throw()
      { memcpy(&repb[VDIRCH_MOREORBASE], &newval, 4); };
  public:
    inline time_t timestamp() throw()
      { Bit32 val; memcpy(&val, &rep[VDIRCH_TIMESTAMP], 4);
	return (time_t) val; };
  private:
    inline void setTimestampField(Bit32 newval) throw()
      { memcpy(&rep[VDIRCH_TIMESTAMP], &newval, 4); };
    // Get/set pseudoInode or sid, depending on type
    inline Bit32 getID() throw()
      { Bit32 val; memcpy(&val, &rep[VDIRCH_ID], 4); return val; };
    inline void setID(Bit32 newval) throw()
      { memcpy(&rep[VDIRCH_ID], &newval, 4); };

    // Get/set fptag or snapshot, depending on type
    inline FP::Tag fptag() throw()
      { FP::Tag ret;
	memcpy(ret.Words(), &rep[VDIRCH_FPTAG], FP::ByteCnt); return ret; };
    inline void setFPTag(const FP::Tag& newfptag) throw()
      { memcpy(&rep[VDIRCH_FPTAG], ((FP::Tag&)newfptag).Words(),
	       FP::ByteCnt); };
    inline Bit32 snapshot() throw()
      { Bit32 val; memcpy(&val, &rep[VDIRCH_FPTAG], 4); return val; };
    inline void setSnapshot(Bit32 newval) throw()
      { memcpy(&rep[VDIRCH_FPTAG], &newval, 4); };

    inline Bit8* firstEntry(Bit8 *repb) throw()
      { return &repb[VDIRCH_ENTRIES]; };
    static inline Bit8* nextEntry(Bit8* entry) throw() {
	if (hasEFPTag(entry))
	  return entry + entry[VDIRCH_E2ARCLEN] + VDIRCH_E2MINSIZE;
        else
	  return entry + entry[VDIRCH_E1ARCLEN] + VDIRCH_E1MINSIZE;
    };

    static inline bool isEndMark(Bit8* entry) throw()
      { return (bool) (entry[VDIRCH_EFLAGS] == 0xff); };
    static inline bool masterFlag(Bit8* entry) throw()
      { return (bool) (entry[VDIRCH_EFLAGS] & 1); };
    static inline void setMasterFlag(Bit8* entry, bool newval) throw()
      { entry[VDIRCH_EFLAGS] = (entry[VDIRCH_EFLAGS] & 0xfe) | (int) newval; };
    static inline bool inUse(Bit8* entry) throw()
      { return (bool) ((entry[VDIRCH_EFLAGS] & 2) != 0); };
    static inline void setInUse(Bit8* entry, bool newval) throw()
      { entry[VDIRCH_EFLAGS] =
	  (entry[VDIRCH_EFLAGS] & 0xfd) | ((int) newval << 1); };
    static inline bool hasEFPTag(Bit8* entry) throw()
      { return (bool) ((entry[VDIRCH_EFLAGS] & 4) != 0); };
    static inline void setHasEFPTag(Bit8* entry, bool newval) throw()
      { entry[VDIRCH_EFLAGS] =
	  (entry[VDIRCH_EFLAGS] & 0xfb) | ((int) newval << 2); };
    static inline bool sameAsBase(Bit8* entry) throw()
      { return (bool) ((entry[VDIRCH_EFLAGS] & 8) != 0); };
    static inline void setSameAsBase(Bit8* entry, bool newval) throw()
      { entry[VDIRCH_EFLAGS] =
	  (entry[VDIRCH_EFLAGS] & 0xf7) | ((int) newval << 3); };
    static inline VestaSource::typeTag type(Bit8* entry) throw()
      { return (VestaSource::typeTag) ((entry[VDIRCH_EFLAGS] >> 4) & 0xf); };
    static inline void setType(Bit8* entry, VestaSource::typeTag type) throw()
      { entry[VDIRCH_EFLAGS] =
	  (entry[VDIRCH_EFLAGS] & 0x0f) | (((int) type) << 4); };
    static inline Bit32 value(Bit8* entry) throw()
      { Bit32 val; memcpy(&val, &entry[VDIRCH_EVALUE], 4); return val; };
    static inline void setValue(Bit8* entry, Bit32 value) throw()
      { memcpy(&entry[VDIRCH_EVALUE], &value, 4); };
    static inline Bit32 attrib(Bit8* entry) throw()
      { Bit32 val; memcpy(&val, &entry[VDIRCH_EATTRIB], 4); return val; };
    static inline void setAttrib(Bit8* entry, Bit32 attrib) throw()
      { memcpy(&entry[VDIRCH_EATTRIB], &attrib, 4); };
    inline Bit8* attribAddr(Bit8* entry) throw()
      { if (VestaSource::type == VestaSource::appendableDirectory ||
	    VestaSource::type == VestaSource::mutableDirectory)
	  return entry + VDIRCH_EATTRIB; else return NULL; };
    static inline FP::Tag efptag(Bit8* entry) throw()
      { FP::Tag ret; assert(hasEFPTag(entry));
	memcpy(ret.Words(), &entry[VDIRCH_EFPTAG], FP::ByteCnt); return ret; };
    static inline void setEFPTag(Bit8* entry, const FP::Tag& newfptag) throw()
      { assert(hasEFPTag(entry));
	memcpy(&entry[VDIRCH_EFPTAG], ((FP::Tag&)newfptag).Words(),
	       FP::ByteCnt); };
    static inline int arcLen(Bit8* entry) throw()
      { return (int) entry[hasEFPTag(entry) ?
			   VDIRCH_E2ARCLEN : VDIRCH_E1ARCLEN]; };
    static inline char* arc(Bit8* entry) throw()
      { return (char*) &entry[hasEFPTag(entry) ?
			      VDIRCH_E2ARC : VDIRCH_E1ARC]; };

    void fillCaches() throw();  // fills all xxxCache members
    inline Bit8* repEnd() throw()
      { if (!repEndCache) fillCaches(); return repEndCache; };
    inline unsigned int nextRawIndex() throw()
      { if (!repEndCache) fillCaches(); return nextRawIndexCache; };
    inline Bit8* base() throw()
      { if (!repEndCache) fillCaches(); return baseCache; };
    inline Bit8* lastRepBlock() throw()
      { if (!repEndCache) fillCaches(); return lastRepBlockCache; };
    inline int totalRepSize() throw() // not counting outdated entries (?!!)
      { if (!repEndCache) fillCaches(); return totalRepSizeCache; };
    inline Bit32 freeLen() throw()
      { Bit32 val; memcpy(&val, repEnd() + 1, 4); return val; };
    inline void setFreeLen(Bit32 newval) throw()
      { memcpy(repEnd() + 1, &newval, 4); };

    // Locate an existing entry with the given arc name; return NULL
    // if there is none.  Also supply the raw index of the entry
    // found, that is, its actual 1-origin position within the rep.
    // Does not follow the base pointer.  A deleted entry is not
    // returned unless includeDeleted is true.  A deleted entry that
    // has been outdated by a later entry (by a replaceDiff insertion)
    // is not returned unless includeOutdated is true.
    Bit8* findArc(const char* arc, unsigned int& rawIndex,
		  bool includeDeleted =false,
		  bool includeOutdated =false) throw();

    // Locate an existing entry with the given raw index; NULL if none.
    // Does not follow the base pointer.  Also return a pointer to
    // the start of the rep block the index was found in.
    Bit8* findRawIndex(unsigned int rawIndex, Bit8*& repBlock) throw(); 

    // Append a new entry.  Don't check for duplicate names, etc.
    // arc need not be null-terminated.  arcLen must be <= MAX_ARC_LEN.
    // A pointer to the new entry is returned.  If fptag==NULL, no 
    // space is reserved in the entry for an fptag.
    Bit8* appendEntry(bool mast, bool sameAsBase,
		      VestaSource::typeTag type, Bit32 value,
		      Bit32 attrib, const FP::Tag* efptag,
		      const char* arc, int arcLen) throw();

    // Change attributes and value of an existing entry.  If the
    // entry had no fptag field when first created, then fptag must be
    // NULL; otherwise it can either be NULL or have a value.
    static void setEntry(Bit8* entry, bool mast, bool sameAsBase,
			 VestaSource::typeTag type, Bit32 value,
			 Bit32 attrib, const FP::Tag* efptag) throw();

    // Common code for all insertXXX methods.  When replacing a stub
    // with anything or anything with a stub or ghost, the atttributes
    // of the old object are optionally returned in the "attribs"
    // parameter.
    VestaSource::errorCode insertCommon(Arc arc, bool mast,
		      VestaSource::typeTag newtype,
		      AccessControl::Identity who,
		      VestaSource::dupeCheck chk,
                      const char** setOwner, ShortId* delsid,
		      Bit32* attribs = NULL) throw();

    // Support code for insertImmutableDirectory.  WARNING: returned
    // value does not have a valid LongId field.
    VDirChangeable* copyMutableToImmutable(const FP::Tag& fptag) throw();

    VDirChangeable* collapse(unsigned int newSize = defaultRepSize) throw();

    // Support for reference counting the new shortids in a volatile
    // directory tree.  NULL in anything other than a volatile directory.
    ShortIdRefCount* sidref;

    // For getting the link count when looking up a file
    inline int getRefCount(ShortId sid, int default_count = 1)
      { return (sidref
		? sidref->GetCount(sid, default_count)
		: default_count); }

    // Rebuild a ShortIdRefCount for this directory.  Only valid on
    // the mutable root.
    ShortIdRefCount *rebuildMutableSidref() throw();

    // Make an entry immutable.  Index is needed for the log entry.
    // Caller is responsible for fingerprinting and determining the
    // correct immutable shortid.
    void makeEntryImmutable(Bit8 *entry, unsigned int index,
			    const FP::Tag* fptag =NULL,
			    ShortId newsid =NullShortId) throw();

    // Make an entry immutable.  Index is needed for the log entry.
    // The caller provides a size threshold for fingerprinting by
    // content and optionally a base fingerprint for files not
    // fingerprinted by content.  The fingerprint and correct shortid
    // is determined, possibly copying or unlinking the current
    // shortid.
    void makeEntryImmutable(Bit8 *entry, unsigned int index,
			    unsigned int fpThreshold,
			    const FP::Tag* snapshot_fptag = 0) throw();

    // Miscellaneous friends that use internal methods (ugh).
    friend bool filterListCallback(void* closure, typeTag type, 
				   Arc arc, unsigned int index,
				   unsigned int pseudoInode,
				   bool mast) throw();
    friend VestaSource::errorCode
      VDirVolatileRoot::createVolatileDirectory(char* hostname, char* port,
						Bit64 handle,
						VestaSource*& result,
						time_t timestamp,
						LongId::lockKindTag lockKind,
						ReadersWritersLock** lock,
						bool readOnlyExisting)
	throw();
    friend VestaSource::errorCode
      VDirVolatileRoot::lookupIndexAndLock(unsigned int index,
                                         VestaSource*& result,
                                         LongId::lockKindTag lockKind,
					   ReadersWritersLock** lock)
      throw();
    friend VestaSource*
      VDCLookupResult(VDirChangeable* dir, Bit8* entry, unsigned int index);

    friend VestaSource*
      LongId::lookup(LongId::lockKindTag lockKind, ReadersWritersLock** lock)
        throw (SRPC::failure);

    friend FP::Tag DirFP(Bit8* rep);
    friend ShortId DirId(Bit8* rep);
    friend void InitFPShortId();
};


#endif //_VDIRCH
