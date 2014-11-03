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
// VestaSource.H
//
// Primary internal interface for the Vesta repository directory
// structure.  

#ifndef _VSOURCE
#define _VSOURCE 1

#include "SourceOrDerived.H"
#include "ArcTable.H"
#include "AccessControl.H"
#include "ReadersWritersLock.H"
#include "FP.H"
#include "VRErrorCode.H"
#include "VestaAttribs.H"
#include <iostream>

typedef const char* Arc;        // must not contain /, \, or \000
#define MAX_ARC_LEN 255

class VestaSource; // forward

// A LongId is an identifier for a Vesta source that is suitable for
// use with NFS.  Every source in the /vesta directory tree at a site
// has a LongId, as does every source in a Vesta mutable (working)
// directory tree.
//
class LongId {
  public:
    Byte32 value;

    // Form the LongId of the indexth child of this source.
    // (1-origin) Return NullLongId if there is no room for more
    // children.  Crash if invoked on NullLongId.
    LongId append(unsigned int index) const throw ();

    // Form the LongId of the parent directory of this source.  If
    // index is not NULL, also set *index to the index of this source
    // within its parent.  Return NullLongId if this source has no
    // parent.
    LongId getParent(unsigned int* index =NULL) const throw ();

    // Return true if this LongId is an ancestor of the given child.
    // A LongId is considered to be its own ancestor.
    bool isAncestorOf(const LongId& child) const throw ();

    // Compare two LongIds
    bool operator==(const LongId& child) const throw () {
	return memcmp(this->value.byte, child.value.byte, sizeof(Byte32)) == 0;
    };
    bool operator!=(const LongId& child) const throw () {
      return !(*this == child);
    }

    // Construct a LongId from a ShortId.  If the ShortId belongs to
    // a file, the fingerprint must be supplied.  If the ShortId
    // belongs to an immutable directory, the fingerprint is ignored;
    // the repository looks it up internally when the LongId is used.
    static LongId fromShortId(ShortId sid, const FP::Tag* fptag =NULL)
        throw ();

    // Find the length of a LongId.  When pickling and unpickling
    // LongIds, only the first "length" bytes need be pickled; the
    // rest are zeros (and must be filled with zeros upon unpickling).
    int length() const throw ();

    // Look up LongId to get a VestaSource.  Return NULL if this
    // LongId does not correspond to any extant source.  The caller is
    // responsible for freeing the object returned.
    //
    // The following feature is available only inside the repository
    // address space: If lockKind == readLock, readLockV, writeLock,
    // or writeLockV, acquire the repository global read or write lock
    // on the directory tree that the LongId is in (either the stable
    // tree lock or a volatile tree lock), and return a pointer to the
    // correct lock.  If lockKind == readLockV or writeLockV, the
    // caller already has the volatile root read lock, so it is not
    // acquired again during the call.  If lockKind == checkLock,
    // check that the supplied lock pointer is the correct one for the
    // tree the LongId is in, and return NULL if it is not.  If the
    // LongId represents a file ShortId, *lock is set to NULL, as no
    // tree lock is needed.
    //
    // The method can throw SRPC::failure only if used outside the
    // repository address space.
    //
    enum lockKindTag { readLock, writeLock, noLock, checkLock,
		       readLockV, writeLockV };
    VestaSource* lookup(lockKindTag lockKind =noLock,
			ReadersWritersLock** lock =NULL)
      throw (SRPC::failure);

    // Check whether a LongId is valid.  Returns false if lookup would
    // return NULL; true if it would return non-NULL .
    bool valid() throw (SRPC::failure);
};

extern const LongId RootLongId;        // LongId of /vesta
extern const LongId MutableRootLongId;
extern const LongId VolatileRootLongId;
extern const LongId DirShortIdRootLongId;
extern const LongId FileShortIdRootLongId;
extern const LongId NullLongId;        // A known invalid LongId

// VestaSource is the base class for all types of Vesta source.

// The logical type of a VestaSource is given by a typeTag (see
// below).  Most of these types are discussed in the repository design
// notes.

// The representation type (subclass) used for a VestaSource is
// generally determined by its logical type and whether the source is
// local or remote.  The representation types include VLeaf,
// VDirUnchangeable, VDirChangeable, VDirEvaluator, and VDirSurrogate.
// Do not create an object of the base class.

// All these representations are in-memory data structures.  The
// on-disk representation of a directory is the repository log and
// checkpoint.  The on-disk representation of a ghost or stub is in
// its parent directory.  The on-disk representation of a file is
// partly in its parent directory and partly in the file corresponding
// to its ShortId.

// Here is how the representations are used.  Note that some logical
// types have multiple representations possible.

// Logical type           Representation        Notes
// ------------           --------------        -----
// any source             VDirSurrogate         1
// immutableFile          VLeaf                 
// mutableFile            VLeaf                 
// immutableDirectory     VDirChangeable        2
// appendableDirectory    VDirChangeable        
// mutableDirectory       VDirChangeable        
// ghost                  VLeaf                 
// stub                   VLeaf                 
// deleted                none                  3
// outdated               none                  3
// volatileDirectory      VDirChangeable
// volatileROEDirectory   VDirChangeable
// evaluatorDirectory     VDirEvaluator         4
// evaluatorROEDirectory  VDirEvaluator         4
// device                 none                  5
// gap                    none                  3
// unused                 none                  3

// 1) Used on the client side of the SRPC interface to the repository
// server, for all sources.  The "Dir" in the name is historical; the
// type is no longer used only for directories.
// 2) It was once intended to have a separate type VDirUnchangeable
// for immutable directories, but that idea has been abandoned.
// 3) These values are used inside the directory implementation, and
// "deleted" may be returned by the listCallback if the caller has
// asked to see a delta instead of a full directory.
// 4) This is a kind of surrogate inside the repository server for a
// "directory" that is really a binding in the evaluator value space.
// 5) /dev/null implementation inside the repository NFS server.

// Note: Most methods have SRPC::failure in their throw () clauses, but
// they can really throw this exception only in the subclass
// VDirSurrogate.

class VestaSource : public VestaAttribs, public VRErrorCode {
  public:
    enum dupeCheck { dontReplace = 0,  // Report error if name is in use.
		     replaceDiff,      // Replace existing object, if
				       //  any, with a different object
				       //  having the same name.
		     replaceNonMaster  // Replace existing object, if any,
			               //  with a different object
			               //  having the same name, unless
                                       //  the existing object is master.
		 };
    enum typeTag { immutableFile=0, mutableFile, immutableDirectory,
                   appendableDirectory, mutableDirectory, ghost, stub,
                   deleted, outdated, volatileDirectory,
		   evaluatorDirectory, device, volatileROEDirectory,
		   evaluatorROEDirectory, gap, unused=15 };

    // The following two functions return pointers to static storage
    static const char* errorCodeString(errorCode err) throw (); 
    static const char* typeTagString(typeTag type) throw ();
    static char typeTagChar(typeTag type) throw ();
      // immutable(f)ile, m(u)tableFile, (i)mmutableDirectory,
      // (a)ppendableDirectory, (m)utableDirectory, (g)host, (s)tub,
      // (d)eleted, (o)utdated, (v)olatileDirectory, (e)valuatorDirectory,
      // dev(n)ull, volatile(r)OEDirectory, eva(l)uatorROEDirectory,
      // ga(p), unused(x)

    // Find roots.  Outside the repository address space, the
    // parameters must be defaulted.
    static VestaSource*
      repositoryRoot(LongId::lockKindTag lockKind =LongId::noLock,
		     ReadersWritersLock** lock =NULL) throw (SRPC::failure);
    static VestaSource*
      mutableRoot(LongId::lockKindTag lockKind =LongId::noLock,
		  ReadersWritersLock** lock =NULL) throw (SRPC::failure);
    static VestaSource*
      volatileRoot(LongId::lockKindTag lockKind =LongId::noLock,
		   ReadersWritersLock** lock =NULL) throw (SRPC::failure);

    // The following fields should not be modified after initialization
    typeTag type;
    LongId longid;
    bool master;
    Bit32 pseudoInode;  // See below
    FP::Tag fptag;  // See below

    // Server-side initialization.  These methods are not
    // needed (or supplied) in the client-side implementation.
    // init() must be called before the first use of anything else in
    // this interface.  recoveryDone() must be called after recovery
    // from the repository log is complete, to turn on logging of new
    // updates; it sets doLogging to true.
    static void init() throw ();
    static void recoveryDone() throw ();
    static bool doLogging;

    // Someone else may have modified the representation that this
    // object points to, so discard any state cached in the object.
    virtual void resync(AccessControl::Identity who =NULL)
      throw (SRPC::failure);

    // Get a VestaSource for the parent directory.  Returns 0 if this
    // object has no parent directory.  (Note: not implemented inside
    // the repository server.)
    VestaSource* getParent() throw (SRPC::failure);

    // The following methods work only on files.  If you call one on
    // a non-file, it will return an error, except as noted below.
    // These methods are meant only for remote, "arm's length" use by
    // the replicator: local processes with sufficient privilege can
    // obtain a file's ShortId and access it more efficiently through
    // the SourceOrDerived interface.  These methods modify the
    // underlying ShortId file directly and immediately; they are not
    // part of the current failure-atomic transaction.

    // Immutable or mutable files
    virtual errorCode
      read(void* buffer, /*INOUT*/int* nbytes, Basics::uint64 offset,
	   AccessControl::Identity who =NULL) throw (SRPC::failure);
    // Read the entire file at once.  Write its contents to the
    // specified ostream.  (Note: not implemented inside the
    // repository server.)
    virtual errorCode
      readWhole(std::ostream &out, AccessControl::Identity who =NULL)
      throw (SRPC::failure);
    // Returns false for non-files:
    virtual bool executable() throw (SRPC::failure);
    // Returns 0 for non-files:
    virtual Basics::uint64 size() throw (SRPC::failure); 

    // Mutable files only
    virtual errorCode
      write(const void* buffer, /*INOUT*/int* nbytes, Basics::uint64 offset,
	    AccessControl::Identity who =NULL) throw (SRPC::failure);
    virtual errorCode
      setExecutable(bool x, AccessControl::Identity who =NULL)
      throw (SRPC::failure);
    virtual errorCode
      setSize(Basics::uint64 s, AccessControl::Identity who =NULL)
      throw (SRPC::failure);

    // The following methods work on both files and directories.

    // Get the modified time.  Returns "2" on ghosts and stubs.
    virtual time_t timestamp() throw (SRPC::failure);

    // Set the modified time.  No-op on ghosts and stubs.  When
    // applied to a file, this method modifies the underlying ShortId
    // file directly and immediately; it is not part of the current
    // failure-atomic transaction.
    virtual errorCode
      setTimestamp(time_t ts, AccessControl::Identity who =NULL)
      throw (SRPC::failure);

    // The following method works only on a file or immutable
    // directory.  If you call it on another type of source, it will
    // return NullShortId.
    virtual ShortId shortId() throw (SRPC::failure);

    // The following methods work only on directories.  If you
    // call one on a non-directory, it will return notADirectory,
    // unless the specific comments state otherwise.  In the methods
    // that take a timestamp argument, this value is used as the
    // modified time if supplied; otherwise the current time is used.

    // Look up an Arc relative to this directory.  Note that ".",
    // "..", and "" arcs are not understood at this layer.  The
    // indexOffset argument to lookup() is for internal use only;
    // clients must always default it.  The caller is responsible for
    // freeing the object returned in result.
    virtual errorCode
      lookup(Arc arc, VestaSource*& result, AccessControl::Identity who =NULL,
	     unsigned int indexOffset =0) throw (SRPC::failure);

    // Look up a pathname, consisting of arcs separated by
    // pathnameSep characters, relative to this directory.  Again,
    // ".", "..", and "" arcs are not understood.  Also, an initial
    // pathnameSep character does not specify the root.  The caller is
    // responsible for freeing the object returned in result.
    virtual errorCode
      lookupPathname(const char* pathname, VestaSource*& result,
		     AccessControl::Identity who =NULL,
		     char pathnameSep =PathnameSep) throw (SRPC::failure);

    // Find the indexth child of this directory.  If arcbuf is not
    // NULL, the arc corresponding to the child found is returned in
    // the supplied buffer, which must be at least MAX_ARC_LEN+1 bytes
    // long.  This method may indirect through a forwarding pointer
    // (if the indexth entry was renamed), so the LongId of the result may
    // be different from longid.append(index).  The caller is
    // responsible for freeing the object returned in result.
    virtual errorCode lookupIndex(unsigned int index, VestaSource*& result,
				  char* arcbuf =NULL) throw (SRPC::failure);

    // Really delete this entry; do not leave a ghost.  WARNING: do not
    // use on a master appendable directory, or the repository's guarantee
    // not to reuse a name with a different meaning can be violated.
    // If existCheck is false, the routine succeeds even if no entry by the 
    // given name exists. 
    virtual errorCode reallyDelete(Arc arc, AccessControl::Identity who =NULL,
				   bool existCheck =true,
				   time_t timestamp =0) throw (SRPC::failure);

    // Insert an entry into the directory.  The behavior if this name
    // is already bound depends on the dupeCheck parameter; see above.
    // If the newvs parameter is not NULL, a VestaSource object
    // referring to the new entry is returned there; the caller is
    // responsible for freeing this object.

    // Link to an existing file by ShortId.  Parent may be mutable,
    // appendable, or volatile (including volatileROE).  Normally
    // fptag is NULL, but you can choose the fingerprint by setting it.
    virtual errorCode
      insertFile(Arc arc, ShortId sid, bool master, 
		 AccessControl::Identity who =NULL,
		 dupeCheck chk=dontReplace, VestaSource** newvs =NULL,
		 time_t timestamp =0, FP::Tag* fptag =NULL)
      throw (SRPC::failure);
    // Mutable link to an existing or new file by ShortId.  Parent must be
    // mutable or volatile (including volatileROE).  If sid is NullShortId,
    // a new shortid is assigned and an empty file is created.
    virtual errorCode
      insertMutableFile(Arc arc, ShortId sid, bool master,
		 AccessControl::Identity who =NULL,
		 dupeCheck chk=dontReplace, VestaSource** newvs =NULL,
		 time_t timestamp =0) throw (SRPC::failure);
    // If dir is immutable, bind arc to it.  If dir is mutable, bind
    // arc to a new immutableDirectory that is a deep copy of dir.  If
    // dir is NULL, bind arc to an empty immutableDirectory.  Parent
    // must be appendable or mutable.  CheckIn and Advance use this method.
    // Normally fptag is NULL, but if dir is mutable or NULL, you can
    // choose the new directory's fingerprint by setting fptag.
    virtual errorCode
      insertImmutableDirectory(Arc arc, 
                 VestaSource* dir, bool master, 
		 AccessControl::Identity who =NULL,
		 dupeCheck chk=dontReplace, VestaSource** newvs =NULL,
		 time_t timestamp =0, FP::Tag* fptag =NULL)
       throw (SRPC::failure);
    // Create new, empty appendableDirectory.  Parent must be appendable.
    virtual errorCode
      insertAppendableDirectory(Arc arc, bool master,
		 AccessControl::Identity who =NULL,
		 dupeCheck chk=dontReplace, VestaSource** newvs =NULL,
		 time_t timestamp =0) throw (SRPC::failure);
    // Create new mutable (or volatile, or volatileROE) directory.
    // Parent must be mutable (or volatile, or volatileROE,
    // respectively).  If dir is NULL, the new directory is empty;
    // otherwise, dir must be immutable, and the new directory is
    // initially a shallow copy of it.  CheckOut uses this method.
    virtual errorCode
      insertMutableDirectory(Arc arc, VestaSource* dir, bool master, 
		 AccessControl::Identity who =NULL,
		 dupeCheck chk=dontReplace, VestaSource** newvs =NULL,
		 time_t timestamp =0) throw (SRPC::failure);
    // Create new ghost.  Parent must be appendable.
    virtual errorCode
      insertGhost(Arc arc, bool master,
		 AccessControl::Identity who =NULL,
		 dupeCheck chk=dontReplace, VestaSource** newvs =NULL,
		 time_t timestamp =0) throw (SRPC::failure);
    // Create new stub.  Parent must be appendable.
    virtual errorCode
      insertStub(Arc arc, bool master,
		 AccessControl::Identity who =NULL,
		 dupeCheck chk=dontReplace, VestaSource** newvs =NULL,
		 time_t timestamp =0) throw (SRPC::failure);

    // Rename existing file or directory.  Should be used only on
    // mutable or volatile/volatileROE directories.  Currently also
    // works on appendable directories for backward compatibility
    // only; this functionality may be removed in the future.
    virtual errorCode
      renameTo(Arc arc, VestaSource* fromDir, Arc fromArc,
	       AccessControl::Identity who =NULL,
	       dupeCheck chk=dontReplace, time_t timestamp =0)
      throw (SRPC::failure);

    // Directory listing.  For each directory entry, beginning with
    // firstIndex, the callback is invoked with the corresponding
    // object's parameters.  The shortid is provided only for files,
    // not directories, and is also omitted for files in an evaluator
    // directory (including the base of a volatile directory).  If the
    // callback returns true, the listing is continued; if not, it is
    // stopped.  Note that "." and ".." arcs are not understood at
    // this layer.  You can get the LongId for ".." with
    // longid.getParent().  To list starting at the next entry after
    // one returned with index=i, begin with firstIndex=i+2.  To list
    // starting at the first entry, begin with firstIndex=0.
    //
    // If deltaOnly is true for a mutable, volatile, or volatileROE
    // directory, only the changes with respect to the directory base
    // are shown; deletions are shown as arcs of type deleted.  (In
    // a deleted arc may sometimes appear for a name that transiently
    // existed in the directory but never existed in the base.)
    //
    // The indexOffset argument is for internal use only; clients
    // must always default it.

    typedef bool (*listCallback)(void* closure, typeTag type, Arc arc,
				 unsigned int index, Bit32 pseudoInode,
				 ShortId fileSid, bool master);
    virtual errorCode
      list(unsigned int firstIndex, listCallback callback,
	   void* closure, AccessControl::Identity who =NULL,
	   bool deltaOnly =false, unsigned int indexOffset =0)
      /*throw (SRPC::failure or anything thrown by the callback)*/;

    // Acquire base of directory and return a new VestaSource object
    // for the base. The caller is responsible for freeing the 
    // object returned.
    virtual errorCode
      getBase(VestaSource*& result, AccessControl::Identity who =NULL)
      throw (SRPC::failure);

    // Replace an immutableFile or immutableDirectory with a mutable
    // (or volatile) one, and return a new VestaSource object for the
    // new source.  The new source has the same name and (unlike
    // sources inserted with other methods) the same LongId as the old
    // source had.  The source must be a descendent of MutableRoot (or
    // VolatileRoot).
    // 
    // If the source parent is an immutable (or evaluator) directory,
    // the parent is made mutable (resp. volatile) recursively.  If
    // the old source was a directory, the new source is a shallow
    // copy of the old one, as with insertMutableDirectory above.
    //
    // If the old source was a file, the caller may either supply the
    // ShortId of a new source, or supply NullShortId.  In the latter
    // case, the repository assigns a new shortid and copies the first
    // copyMax bytes of the old data to it.
    //
    // It is an error to apply this method to a file whose LongId is
    // based on its ShortId; such LongIds are assigned explicitly by
    // LongId::fromShortId, or implicitly to files in directory trees
    // created by createVolatileDirectory with the readOnlyExisting
    // flag.
    //
    // On return, the old VestaSource object is invalid (but has not
    // been freed).  The caller is responsible for freeing the object
    // returned.
    virtual errorCode
      makeMutable(VestaSource*& result, ShortId sid =NullShortId,
		  Basics::uint64 copyMax = (Basics::uint64) -1,
		  AccessControl::Identity who =NULL)
       throw (SRPC::failure);

    // Similar to makeMutable, but doesn't make the object itself
    // mutable.  This allows for adding attributes to an object in a
    // mutable directory without actually creating a mutable copy.  As
    // with makeMutable, the new source has the same name and LongId
    // as the old source.  The source must be a descendent of
    // MutableRoot.
    //
    // If the source parent is an immutable directory, the parent is
    // made mutable recursively.
    //
    // On return, the old VestaSource object is invalid (but has not
    // been freed).  The caller is responsible for freeing the object
    // returned.
    virtual errorCode
      copyToMutable(VestaSource*& result,
		    AccessControl::Identity who =NULL)
       throw (SRPC::failure);

    // Make files in a directory tree immutable and optionally
    // cause their fingerprints to be based on content.
    // The object must be a mutable, volatile, or volatileROE
    // directory, or a mutable file.  Walks the directory tree rooted
    // at the object and examines each mutable file.  If a mutable
    // file is less than threshold bytes long, it is made immutable
    // with a fingerprint computed based on its content.  Otherwise,
    // if the directory is volatile[ROE], the file is made immutable
    // with a fingerprint based on its preassigned unique id.
    // Otherwise (i.e., if the file is threshold or more bytes long
    // and the directory is mutable), the file remains mutable and a
    // fingerprint is not assigned yet.  A threshold of (unsigned int)
    // -1 is "infinity".
    //
    // This method must be used at the end of every tool invocation,
    // to ensure that the files created will really be read-only in
    // volatileROE directories created later.  It is also used by
    // vadvance to implement the -F flag.
    //
    virtual errorCode
      makeFilesImmutable(unsigned int threshold,
			 AccessControl::Identity who =NULL)
        throw (SRPC::failure);

    // Turn the master flag on or off.  Use with care to preserve the
    // replication invariants!  setMaster is a convenience method
    // implemented on top of setIndexMaster.  Subclasses need only 
    // provide the latter.
    virtual errorCode setMaster(bool state,
		  	        AccessControl::Identity who =NULL)
      throw (SRPC::failure);
    virtual errorCode setIndexMaster(unsigned int index, bool state,
				     AccessControl::Identity who =NULL)
       throw (SRPC::failure);

    // Find out which repository this VestaSource is in
    virtual Text host() throw ();
    virtual Text port() throw ();

    // The following method should be called only from inside the
    // repository that is acquiring mastership, not from ordinary clients.
    // See Mastership.C.  Caller is responsible for freeing *grantidOut.
    virtual errorCode
      cedeMastership(const char* requestid, const char** grantidOut,
		     AccessControl::Identity who =NULL) throw (SRPC::failure);

    // Access control
    AccessControl ac;

    // The following methods give an assertion failure if applied to
    // a nondirectory.

    // Methods for use in source weeding (garbage collection).
    // Cannot be used outside the repository address space.
    virtual bool hasName() throw ();
    virtual void setHasName(bool val) throw ();
    virtual bool visited() throw ();
    virtual void setVisited(bool val) throw ();
    virtual void mark(bool byName =true, ArcTable* hidden =NULL) throw();

    // Method for explicit free of directory tree.  The object must
    // be a volatile or evaluator directory.  Used to reclaim memory
    // allocated to volatile directory trees eagerly instead of
    // waiting for the next source weed.  The caller guarantees that
    // there are no references to this rep or any of its substructure
    // from outside the tree.  Cannot be used outside the repository
    // address space.
    virtual void freeTree() throw ();

    // Method for checkpointing.  nextSP is the new short pointer
    // that the next item written to the checkpoint file will receive.
    // The method increments nextSP by the number of bytes it writes
    // into the checkpoint.  The new short pointer for this object is
    // returned.  Cannot be used outside the repository address space.
    virtual Bit32 checkpoint(Bit32& nextSP, std::fstream& ckpt) throw ();

    // Utility method for generating pseudo-inode numbers.
    // Should not be used outside the repository address space.
    inline Bit32 indexToPseudoInode(unsigned int index) throw (){
      return 0x7fffffff & (((pseudoInode << 8) | (pseudoInode >> 23)) + index);
    }

    //
    // Convenience wrappers around writeAttrib
    //

    // opSet: Bind name to the singleton set {value}.  Equivalent to clear
    // followed by add, but atomic even in the presence of other
    // changes with the same timestamp.
    inline VRErrorCode::errorCode
      setAttrib(const char* name, const char* value,
		AccessControl::Identity who =NULL,
		time_t timestamp =0) throw (SRPC::failure)
      { return writeAttrib(opSet, name, value, who, timestamp); };

    // opClear: Bind name to the empty set; i.e., unbind it.
    // Client should pass "" as value.
    inline VRErrorCode::errorCode
      clearAttrib(const char* name, AccessControl::Identity who =NULL,
		  time_t timestamp =0) throw (SRPC::failure)
      { return writeAttrib(opClear, name, "", who, timestamp); };

    // opAdd: Add value to the set that is bound to name.
    inline VRErrorCode::errorCode
      addAttrib(const char* name, const char* value,
		AccessControl::Identity who =NULL,
		time_t timestamp =0) throw (SRPC::failure)
      { return writeAttrib(opAdd, name, value, who, timestamp); };

    // opRemove: Remove value from the set that is bound to name, if present.
    inline VRErrorCode::errorCode
      removeAttrib(const char* name, const char* value,
		   AccessControl::Identity who =NULL,
		   time_t timestamp =0) throw (SRPC::failure)
      { return writeAttrib(opRemove, name, value, who, timestamp); };

    // Our own version of VestaAttribs::writeAttrib.  This one does
    // access checking and logging; the base class's method does not.
    // The method is a no-op if this source does not have attributes.
    virtual errorCode
      writeAttrib(attribOp op, const char* name, const char* value,
		  AccessControl::Identity who =NULL, time_t timestamp =0)
      throw (SRPC::failure);

    // Statistics primarily intended for VDirChangeable.  Other object
    // types return inapproprateOp.
    struct directoryStats
    {
      Basics::uint32
        // A mutable/immutable directory can be based on another, in
        // which case it represents a delta over the base.  (See
        // getBase above.)  This measurement tells you how long the
        // chain of base directories is.
        baseChainLength,
	// When one directory is based on another, only some of
	// directory entries along the base chain will be used to make
	// up the directory at the head.  However, the repository must
	// walk along all of these entries when listing the directory.
	// These measurements give the number and size in bytes of
	// directory entries used by the directory at the head and by
	// all directories along the base chain.
	usedEntryCount,
	usedEntrySize,
	totalEntryCount,
	totalEntrySize;
    };
    virtual errorCode
      measureDirectory(/*OUT*/directoryStats &result,
		       AccessControl::Identity who =NULL)
      throw (SRPC::failure);

    // Special method for immutable directories.  Causes the base
    // directory of this directory to be collapsed, eliminating some
    // amount of directory delta encoding, which may make some
    // operations (lookup, directory listing, changes) more efficient.
    virtual errorCode
      collapseBase(AccessControl::Identity who =NULL)
      throw (SRPC::failure);

    // Get the number of links to a file.  Returns 1 except for mutable
    // files in directories which support hard links.
    virtual unsigned int linkCount();

    // Create a duplicate of this instance of the appropriate subclass.
    virtual VestaSource *copy() throw();

    // Server-only method to dump out a debug representation of the
    // internal data structures.  Not implemented for clients, and
    // only implemented on certain classes.  Note that this is a
    // destructive dump and the server is required to exit after
    // performing such a dump.
    virtual void debugDump(std::ostream &dump) throw ();

    // client-only method to get a value of [Repository]master_hint setting.
    virtual Text getMasterHint() throw(SRPC::failure);

    virtual ~VestaSource() { }

  public:
    Bit8* rep;
};

// FILEHANDLE PERSISTENCE

// The longid of a source is used as its NFS filehandle.  NFS
// filehandles have one property that longids do not share: an object
// is supposed to keep the same filehandle despite renaming or
// copy-on-write.  We use two mechanisms to ensure this.

// 1) A VForward pointer keeps the old longid of a renamed object
// working as long as its old parent directory is not deleted.
// Opening the object under its new name gives a new longid.  This
// seems to work well enough in practice.

// 2) After copy-on-write, the new directory entry has a "sameAsBase"
// flag set, indicating that it should appear to have the longid of
// the newest object of the same name in the base rather than its own
// natural longid.  The object is accessible under its own longid as
// well, but this longid is never actually used.

// PSEUDO-INODE NUMBERS

// The pseudo-inode number of a source is presented through the NFS
// interface as its inode number.

// Some NFS clients expect every file and directory to have a unique
// and persistent inode number, not just a 32-byte NFS file handle
// (longid).  Even though inode numbers cannot be used for lookup
// through the NFS interface, some clients use the inode number to
// index their cache and/or check it for consistency; in particular,
// the Linux 2.2 NFS client is quite fussy about inode numbers.

// These requirements are difficult to meet in Vesta for several
// reasons.  There is no truly unique 32-bit identifier across all
// types of object that can appear in Vesta directories.  One cannot
// simply use the 32-bit shortid, for several reasons: (1) Only files
// and immutable directories have shortids.  (2) A file changes its
// shortid when copy-on-write occurs.  (3) The same immutable
// directory can (and frequently does) appear in multiple places in
// the hierarchy, with either the same or different parent.  Each
// instance has a different filehandle (since its virtual ".." link is
// different), but the same shortid.

// Here are the measures taken to alleviate (but not fully solve)
// these problems.

// The pseudo-inode number of a file that is not subject to
// copy-on-write is its shortid.  This includes files in the appendable
// tree and files in volatileROE or evaluatorROE directories.

// The pseudo-inode number of a mutable, volatile, or volatileROE
// directory is stored in its representation and remains the same
// across renamings.  For a directory that is created by
// copy-on-write, the pseudo-inode number is initialized to that of
// the base directory; otherwise it is initialized as described in the
// next paragraph.

// The pseudo-inode number of any other source is a 31-bit hash of its
// longid, computed incrementally on the sequence of indices.  The
// high-order bit of the hash is set to 0 to prevent collisions with
// file shortids.

// With the scheme above, pseudo-inode number collisions should be
// infrequent, but they can occur.  The known problems that collisions
// can cause are:

// 1) If two directories with the same inode number appear in the same
// parent directory, some implementations of the Unix "pwd" program or
// "getwd" library routine will return the wrong answer when asked for
// the name of the second of them (or of one of the descendants of the
// latter).  The hash function used does an excellent job of
// preventing collisions within the same parent directory, however.

// 2) If two different objects with the same inode number are accessed
// by the same client fairly close together in time, and the client
// indexes its cache by inode instead of file handle, cached
// information about the first might be erroneously accepted as
// applying to the second.  (On Linux 2.2, this case seems to cause
// confusion only if both objects are directories.  On Tru64 Unix,
// it seems never to cause a problem.)  It would be nice to have a
// solution that makes this error impossible instead of just very
// unlikely.

// FINGERPRINTS

// The repository maintains fingerprints (type FP::Tag) of some
// sources for use by the evaluator.  

// The appendable tree rooted at repositoryRoot keeps a fingerprint
// for every immutableDirectory and immutableFile.  Repository
// operations return a source fingerprint in the VestaSource::fptag
// field.  Ghosts, stubs, and appendableDirectories do not have
// fingerprints; their fptag fields contain FP::Tag("").

// Two sources with different contents are guaranteed to have
// different fingerprints.  Two sources with the same content may or
// may not have the same fingerprint.  A source acquires a fingerprint
// based on the pathname under which it is first placed in the
// appendable part of the repository.  That fingerprint stays with it
// as the same source acquires new names via the checkin/advance/
// checkout cycle or by renaming in an appendable directory.  (But see
// below.)

// The mutable tree does not keep fingerprints for its sources.  The
// fptag field of a source looked up in the mutable tree is
// unspecified.

// Volatile directory trees keep a fingerprint for every file, but no
// fingerprints for directories; the fptag field of a volatile (or
// evaluator) directory is unspecified.

// Fingerprints for existing files in a volatile directory (that is,
// files in the base evaluator directory) are obtained from the
// evaluator.  When a new file is created, an arbitrary unique id
// number is generated for it and the fingerprint of that value is
// used.

// A mutable file's fingerprint can be recomputed to be based on its
// content by calling makeFilesImmutable with the proper arguments.
// This applies both to new files in mutable directories and new files
// in volatile directories.

#endif //_VSOURCE
