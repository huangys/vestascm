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
// VDirSurrogate.H
//
// Remote surrogate for a VestaSource of any type.
//

#include "VestaSource.H"
#include "SRPC.H"

extern "C" void VDirSurrogate_init();
extern "C" void VDirSurrogate_init2();

class VDirSurrogate : public VestaSource {
    // Warning!  The following fields of the VestaSource parent class are
    // not set in a VDirSurrogate:  ac, rep, nextPseudoInode.
  private:
    time_t timestampCache;
    ShortId sidCache;
    int executableCache;
    Basics::uint64 sizeCache;
    // Which repository to use
    /*const*/ Text host_, port_;
  public:
    static Text defaultHost() throw();
    static Text defaultPort() throw();
    Text host() throw() { return host_; };
    Text port() throw() { return port_; };

    VDirSurrogate(time_t timestamp, ShortId sid,
		  const Text host_ = "", const Text port_ = "",
		  int executable = -1,
		  Basics::uint64 size = (Basics::uint64) -1)
      throw (SRPC::failure);

    // Create a volatile directory based on a Vesta language binding.
    // The new directory is volatile in the sense that it does not
    // survive a crash and restart of the repository server.  It is
    // "based" on the binding in the sense that its initial contents
    // are the contents of the binding, interpreted as a directory.
    // The new directory can be mutated, and a list of changes to it
    // can be obtained by calling its list method with deltaOnly=true.
    //
    // If readOnlyExisting is true, existing files in the directory
    // and its subdirectories are read-only; if false, they can be
    // mutated (via copy-on-write).  When readOnlyExisting is true, an
    // existing file's longid is based on its shortid and fingerprint;
    // when false, a new unique longid is chosen for each file.  The
    // true case gives a much better file block cache hit rate on the
    // machines where the tools run.  It should be used whenever
    // possible. 
    //
    // The base binding is specified by (host, handle), where
    // (host, port) is an SRPC socket on which the evaluator
    // holding the binding can be contacted, and handle identifies the
    // binding to the evaluator.  The timestamp parameter gives the
    // initial timestamp (modified time) that should be attributed to
    // the directory; if it is defaulted, the current time is used.
    // The caller is responsible for freeing the object returned.
    //
    // The repository to use is specified by (reposHost, reposPort).
    // If these arguments are omitted, the default values are taken from
    // VestaConfig entries [Repository]VestaSourceSRPC_host and
    // [Repository]VestaSourceSRPC_port.
    //
    static VestaSource::errorCode
      createVolatileDirectory(const char* host, const char* port,
			      Bit64 handle,  VestaSource*& result,
			      time_t timestamp =0,
			      bool readOnlyExisting =true,
			      Text reposHost ="",
			      Text reposPort ="")
	throw (SRPC::failure);

    // Backward compatibility: get the port from vesta.cfg.
    static VestaSource::errorCode
      createVolatileDirectory(const char* host, Bit64 handle,
			      VestaSource*& result, time_t timestamp =0,
			      bool readOnlyExisting =true,
			      Text reposHost ="",
			      Text reposPort ="")
	throw (SRPC::failure);    

    // Delete a volatile directory, freeing the repository server
    // resources that were required to store it.  Does not free the
    // local pointer dir.
    static VestaSource::errorCode
      deleteVolatileDirectory(VestaSource* dir)
	throw (SRPC::failure);

    // Obtain the UDP socket of the default repository's NFS server,
    // in the SRPC-style hostName:portNumber format.  Also returns the
    // LongIds (NFS file handles) for the repository root directory
    // and mutable root directory.  (Note: All this information is
    // either constant or obtainable from the Vesta config file, so
    // this method is not strictly necessary.)  The caller is
    // responsible for freeing the char* socket returned.
    //
    static void
      getNFSInfo(char*& socket, LongId& root, LongId& muRoot)
	throw (SRPC::failure);

    // ShortId lookup from fingerprint for immutable files.
    static ShortId fpToShortId(const FP::Tag& fptag, Text host, Text port)
      throw (SRPC::failure);

    // Special constructor methods to enable dealing with more than
    // one repository in the same process.  The corresponding methods
    // in VestaSource and LongId always access the default repository
    // (host, port) obtained from VestaConfig; these methods let the
    // caller specify the host and port.  These methods (unlike those
    // in VestaSource) return dynamic storage that the caller is
    // responsible for freeing.
    //
    static VestaSource* repositoryRoot(Text host, Text port,
				       AccessControl::Identity who =NULL)
      throw (SRPC::failure);
    static VestaSource* mutableRoot(Text host, Text port,
				    AccessControl::Identity who =NULL)
      throw (SRPC::failure);
    static VestaSource* volatileRoot(Text host, Text port,
				     AccessControl::Identity who =NULL)
      throw (SRPC::failure);
    static VestaSource* LongIdLookup(LongId longid, Text host, Text port,
				     AccessControl::Identity who =NULL)
      throw (SRPC::failure);
    static bool LongIdValid(LongId longid, Text host, Text port,
			    AccessControl::Identity who =NULL)
      throw (SRPC::failure);

    // Request the specified dst repository (by default, the local one)
    // to acquire mastership of the specified object from the specified
    // src repository.  Defaulting the src repository is no longer supported.
    //
    // The given pathname must be relative to "/vesta"; it must not
    // start with "/vesta" or "/".
    //
    // The src repository must grant ownership permission to the user
    // swho and be willing to cede mastership to the dst repository.
    // The dst repository must grant ownership permission to the user
    // dwho and be willing to accept mastership from the src
    // repository.  A repository will agree to cede (or accept)
    // mastership on a pathname from another repository if, when
    // searching upward in its copy of the namespace, the first object
    // it finds that has a #mastership-to (or respectively
    // #mastership-from) attribute includes the other repository's
    // host:port name (or "*") in its value.  If no objects with the
    // appropriate attribute are found, permission is denied.
    //
    // Possible errors:
    //  ok              Transfer succeeded, or dst was already master.
    //  notFound        Name not found on dst.  (Name not found on src
    //                   is reported as notMaster.)
    //  noPermission    Permission to change either dst or src denied.
    //  nameInUse       Impossible.
    //  inappropriateOp Type error: transfer to stub from non-stub, to
    //                   appendable directory from ghost, to ghost from non-
    //                   ghost (technically legal but disallowed as probably
    //                   a mistake), or between types that should not occur
    //                   together because they violate the replica agreement
    //                   invariant (e.g., to file from immutable directory).
    //  nameTooLong     Overly long arc in pathname.
    //  rpcFailure      Call from dst to src repository failed.  If the first
    //                   call to src fails, the object is unchanged.  If a
    //                   later call fails, the transfer protocol has been
    //                   partially carried out, the object is left
    //                   with a "#master-request" attribute set, and
    //                   dst forks a recovery thread to try to complete
    //                   or abort the transfer.  This thread runs
    //                   periodically, persisting even after reboots,
    //                   until src can be contacted.  If necessary,
    //                   you can force the destination to give up
    //                   by manually removing its "#master-request"
    //                   attribute; however, the source may have
    //                   already ceded mastership, which would leave
    //                   the object with no master.
    //  notADirectory   Pathname has arcs beyond a leaf.
    //  isADirectory    Impossible.
    //  invalidArgs     Returned for implementation errors where
    //                   dst supplies invalid data to src or vice
    //                   versa; these should never occur.
    //  outOfSpace      Impossible.
    //  notMaster       src is not the master for this object.
    //
    // If the method throws SRPC::failure, the call to dst failed.  As
    // with all other methods in this interface, the operation
    // probably was not started, but possibly could have succeeded or
    // have failed with any of the possible error codes.
    //
    // Currently, separate identities can be supplied to use at the
    // destination and source.  The source identity defaults to be the
    // same as the destination identity, and the ability to specify it
    // separately may be removed in the future.
    //
    static VestaSource::errorCode
      acquireMastership(const char* pathname,
			Text dstHost ="", Text dstPort ="",
			Text srcHost ="", Text srcPort ="",
			char pathnameSep =PathnameSep,
			AccessControl::Identity dwho =NULL,
			AccessControl::Identity swho =NULL)
      throw (SRPC::failure);

    // Request the specified dst repository (by default, the local
    // one) to make a replica of the specified object from the
    // specified src repository.  Defaulting the src repository is not
    // supported.  The replication is not recursive except where it
    // must be: an appendable directory is replicated as an empty
    // directory, but an immutable directory tree is copied in full.
    // The caller should check that the destination object's parent
    // directory exists and that the object does not already exist
    // (other than as a ghost or nonmaster stub), as this routine will
    // make an unneeded mutable copy in such cases before returning an
    // error.
    //
    // The given pathname must be relative to "/vesta"; it must not
    // start with "/vesta" or "/".  The asStub flag creates a stub
    // instead of the complete object; the asGhost flag creates a
    // ghost.  
    //
    // The user swho must have read permission in the src repository,
    // the user dwho must have search permission in the dst
    // repository, and the src repository must have permission from
    // the dst repository to supply a replica.  The latter permission
    // is granted if, when searching upward from pathname in dst,
    // either the first object found that has a #replicate-from
    // attribute or the first object found that has a
    // #replicate-from-noac attribute includes srcHost:srcPort (or
    // "*") in its value.  If no objects with such attributes are
    // found, permission is denied.
    //
    // Possible errors:
    //  ok              Replication succeeded.
    //  notFound        Pathname not found in src repository, or its
    //                   parent not found in dst repository.
    //  noPermission    The src repository denied read permission to 
    //                   swho, the dst repository denied search
    //                   permission to dwho, or the dst repository
    //                   denied the src repository permission to
    //                   supply a replica (i.e., dst did not trust src to
    //                   provide a correct copy).
    //  nameInUse       If !asStub and !asGhost, this error means there
    //                   is already a non-ghost, non-stub object named
    //                   pathname in dst.  If asStub or asGhost, it
    //                   means there is already some object (not
    //                   necessarily of the type requested) named pathname
    //                   in dst.  In both cases the src repository has
    //                   probably not been examined yet.
    // inappropriateOp  Attempt to replicate between types that
    //                   should not occur together because they violate
    //                   the replica agreement invariant (e.g., to file
    //                   from immutable directory), or attempt to
    //                   do something that doesn't make sense (e.g.,
    //                   replicate a child into an immutable directory).
    //  nameTooLong     Overly long arc in pathname.
    //  rpcFailure      Call from dst to src repository failed.  The
    //                   pathname has not been created in dst.
    //  notADirectory   Pathname has arcs beyond a leaf.
    //  isADirectory    Impossible.
    //  invalidArgs     Bad syntax for a host or port, or both asStub
    //                   and asGhost are true.
    //  outOfSpace      dst does not enough disk space to hold the replica.
    //  notMaster       Impossible.
    //
    // If the method throws SRPC::failure, the call to dst failed.  As
    // with all other methods in this interface, the operation
    // probably was not started, but possibly could have succeeded or
    // have failed with any of the possible error codes.
    //
    // Currently, separate identities can be supplied to use at the
    // destination and source.  The source identity defaults to be the
    // same as the destination identity, and the ability to specify it
    // separately may be removed in the future.
    //
    static VestaSource::errorCode
      replicate(const char* pathname,
		bool asStub =false, bool asGhost =false,
		Text dstHost ="", Text dstPort ="",
		Text srcHost ="", Text srcPort ="",
		char pathnameSep =PathnameSep,
		AccessControl::Identity dwho =NULL,
		AccessControl::Identity swho =NULL)
      throw (SRPC::failure);

    // Request the specified dst repository (by default, the local
    // one) to merge the attribute history of the specified object in
    // the src repository into the corresponding object in the
    // destination repository.  Defaulting the src repository is not
    // supported.  The destination object must already exist, and must
    // be of the same type as the source object.
    //
    // The given pathname must be relative to "/vesta"; it must not
    // start with "/vesta" or "/".  If includeAccess is true, access
    // control attributes (that is, attributes whose names begin with
    // "#" are included; otherwise they are excluded).
    //
    // The user swho must have read permission in the src repository,
    // the user dwho must have search permission in the dst
    // repository, and the src repository must have permission from
    // the dst repository to supply a replica.  The latter permission
    // is granted if, when searching upward from pathname in dst, the
    // first object found that has a #replicate-from attribute, or
    // (provided that includeAccess is not set) the first object found
    // that has a #replicate-from-noac attribute, includes
    // srcHost:srcPort (or "*") in its value.  If no objects with such
    // attributes are found, permission is denied.
    //
    // Possible errors:
    //  ok              Replication succeeded.
    //  notFound        Pathname not found in src or dst repository.
    //  noPermission    The src repository denied search permission to 
    //                   swho, the dst repository denied search
    //                   permission to dwho, or the dst repository
    //                   denied the src repository permission to
    //                   supply a replica (i.e., dst did not trust src to
    //                   provide a correct copy).
    //  nameInUse       Impossible.
    //  inappropriateOp Attempt to replicate between types that
    //                   do not match.
    //  nameTooLong     Overly long arc in pathname.
    //  rpcFailure      Call from dst to src repository failed.  The
    //                   pathname has not been created in dst.
    //  notADirectory   Pathname has arcs beyond a leaf.
    //  isADirectory    Impossible.
    //  invalidArgs     Bad syntax for a host or port.
    //  outOfSpace      Impossible.
    //  notMaster       Impossible.
    //
    // If the method throws SRPC::failure, the call to dst failed.  As
    // with all other methods in this interface, the operation
    // probably was not started, but possibly could have succeeded or
    // have failed with any of the possible error codes.
    //
    // Currently, separate identities can be supplied to use at the
    // destination and source.  The source identity defaults to be the
    // same as the destination identity, and the ability to specify it
    // separately may be removed in the future.
    //
    static VestaSource::errorCode
      replicateAttribs(const char* pathname,
		       bool includeAccess = true,
		       Text dstHost ="", Text dstPort ="",
		       Text srcHost ="", Text srcPort ="",
		       char pathnameSep =PathnameSep,
		       AccessControl::Identity dwho =NULL,
		       AccessControl::Identity swho =NULL)
      throw (SRPC::failure);

    // Request information about a repository user.  The result is
    // returned into the AccessControl::IdInfo structure passed by
    // reference.  This is primarily intended as a diagnostic tool for
    // permissions problems.
    //
    // The user "who" is the one making the inquiry, and must be
    // allowed access to the repository (although any level of access
    // is enough).
    //
    // The user "subject" is the one about whom the inquiry is being
    // made.
    static void
      getUserInfo(AccessControl::IdInfo &result /*OUT*/,
		  AccessControl::Identity who =NULL,
		  AccessControl::Identity subject =NULL,
		  Text reposHost ="",
		  Text reposPort ="")
      throw (SRPC::failure);

    // Ask the respository to refresh its access control tables.  This
    // includes information derived from the host OS user and group
    // tables as well as that from the repository-specific alias,
    // group, and export files.  Note that this can fail if there are
    // errors parsing any of the repository-specific access control
    // files, in which case the access control tables will not be
    // changed by this call.
    //
    // The user "who" is the requestor, and must have administrative
    // access for the call to succeed.
    static void
      refreshAccessTables(AccessControl::Identity who =NULL,
			  Text reposHost ="",
			  Text reposPort ="")
      throw (SRPC::failure);

    // Our versions of virtual methods from base class.  These
    // methods can operate on virtual directories created with the
    // methods above, or on actual source directories in the
    // repository.  To do the latter, start at
    // VestaSource::repositoryRoot() or VestaSource::mutableRoot().
    // (VestaSource::volatileRoot() is not supported.)
    //
    // Atomicity and concurrency control for this subclass are
    // handled differently than for the subclasses of VestaSource that
    // are provided inside the repository server's address space.  The
    // latter classes use a readers/writers lock to control
    // concurrency, and they allow failure-atomic start/commit
    // brackets around any set of write operations.  In the
    // VDirSurrogate class, however, each method internally acquires
    // and releases the readers/writers lock as needed, and
    // failure-atomic brackets are not available.  Each write
    // operation is failure-atomic by itself, but they cannot be
    // grouped into larger units.  Therefore, additional methods are
    // provided for commonly needed atomic groups (checkout, advance,
    // and checkin).  The server implementation makes it
    // straightforward to add more such methods if needed.
    //
    // See VestaSource.H for detailed comments on each method below.
    // 

    // Read methods
    void resync(AccessControl::Identity who =NULL) throw (SRPC::failure);

    VestaSource::errorCode
      read(void* buffer, /*INOUT*/int* nbytes, Basics::uint64 offset,
	   AccessControl::Identity who =NULL) throw (SRPC::failure);
    VestaSource::errorCode
      readWhole(std::ostream &out, AccessControl::Identity who =NULL)
      throw (SRPC::failure);

    // Note that the timestamp, executable, or size cache may be out
    // of date unless lookup, resync, or the corresponding set method
    // was just called.
    time_t timestamp() throw(SRPC::failure);
    bool executable() throw (SRPC::failure);
    Basics::uint64 size() throw (SRPC::failure);

    ShortId shortId() throw (SRPC::failure) { return sidCache; };

    VestaSource::errorCode lookup(Arc arc, VestaSource*& result,
		 AccessControl::Identity who =NULL,
		 unsigned int indexOffset =0) throw (SRPC::failure);
    VestaSource::errorCode lookupIndex(unsigned int index,
		 VestaSource*& result,
		 char* arcbuf =NULL) throw(SRPC::failure);
    VestaSource::errorCode lookupPathname(const char* pathname,
		 VestaSource*& result,
		 AccessControl::Identity who =NULL,
		 char pathnameSep =PathnameSep) throw (SRPC::failure);

    VestaSource::errorCode
      getBase(VestaSource*& result, AccessControl::Identity who =NULL)
      throw (SRPC::failure);
    VestaSource::errorCode
      list(unsigned int firstIndex,
	   VestaSource::listCallback callback, void* closure,
	   AccessControl::Identity who =NULL,
	   bool deltaOnly =false, unsigned int indexOffset =0)
	/*throw (SRPC::failure or anything thrown by the callback)*/;

    // Write methods
    VestaSource::errorCode
      write(const void* buffer, /*INOUT*/int* nbytes, Basics::uint64 offset,
	    AccessControl::Identity who =NULL) throw (SRPC::failure);
    VestaSource::errorCode
      setExecutable(bool x, AccessControl::Identity who =NULL)
	throw (SRPC::failure);
    VestaSource::errorCode
      setSize(Basics::uint64 s, AccessControl::Identity who =NULL)
	throw (SRPC::failure);
    VestaSource::errorCode
      setTimestamp(time_t ts, AccessControl::Identity who =NULL)
	throw (SRPC::failure);
    VestaSource::errorCode 
      reallyDelete(Arc arc, AccessControl::Identity who =NULL, 
		 bool existCheck =true,
                 time_t timestamp =0) throw (SRPC::failure);
    VestaSource::errorCode
      insertFile(Arc arc, ShortId sid, bool master,
		 AccessControl::Identity who =NULL,
		 VestaSource::dupeCheck chk =VestaSource::dontReplace,
		 VestaSource** newvs =NULL, time_t timestamp =0,
		 FP::Tag* fptag =NULL) 
       throw (SRPC::failure);
    VestaSource::errorCode
      insertMutableFile(Arc arc, ShortId sid, bool master,
		 AccessControl::Identity who =NULL,
		 VestaSource::dupeCheck chk =VestaSource::dontReplace,
		 VestaSource** newvs =NULL, time_t timestamp =0)
	throw (SRPC::failure);
    VestaSource::errorCode
      insertImmutableDirectory(Arc arc, VestaSource* dir, bool master,
		 AccessControl::Identity who =NULL,
		 VestaSource::dupeCheck chk =VestaSource::dontReplace,
		 VestaSource** newvs =NULL, time_t timestamp =0,
		 FP::Tag* fptag =NULL)
       throw (SRPC::failure);
    VestaSource::errorCode
      insertAppendableDirectory(Arc arc, bool master, 
		 AccessControl::Identity who =NULL,
		 VestaSource::dupeCheck chk =VestaSource::dontReplace,
		 VestaSource** newvs =NULL, time_t timestamp =0)
       throw (SRPC::failure);
    VestaSource::errorCode
      insertMutableDirectory(Arc arc, VestaSource* dir, bool master, 
		 AccessControl::Identity who =NULL,
		 VestaSource::dupeCheck chk =VestaSource::dontReplace,
		 VestaSource** newvs =NULL, time_t timestamp =0)
       throw (SRPC::failure);
    VestaSource::errorCode
      insertGhost(Arc arc, bool master, 
		 AccessControl::Identity who =NULL,
		 VestaSource::dupeCheck chk =VestaSource::dontReplace,
		 VestaSource** newvs =NULL, time_t timestamp =0)
       throw (SRPC::failure);
    VestaSource::errorCode
      insertStub(Arc arc, bool master, 
		 AccessControl::Identity who =NULL,
		 VestaSource::dupeCheck chk =VestaSource::dontReplace,
		 VestaSource** newvs =NULL, time_t timestamp =0)
       throw (SRPC::failure);
    VestaSource::errorCode
      renameTo(Arc arc, VestaSource* fromDir, Arc fromArc,
		 AccessControl::Identity who =NULL,
	         VestaSource::dupeCheck chk =VestaSource::dontReplace,
	         time_t timestamp =0)
        throw (SRPC::failure);
    VestaSource::errorCode
      makeMutable(VestaSource*& result, ShortId sid =NullShortId,
		  Basics::uint64 copyMax = (Basics::uint64) -1,		  
		  AccessControl::Identity who =NULL)
        throw (SRPC::failure);
    VestaSource::errorCode
      makeFilesImmutable(unsigned int threshold,
			 AccessControl::Identity who =NULL)
        throw (SRPC::failure);
    VestaSource::errorCode
      setIndexMaster(unsigned int index, bool state,
		     AccessControl::Identity who =NULL) throw (SRPC::failure);
    VestaSource::errorCode
      setMaster(bool state, AccessControl::Identity who =NULL)
        throw (SRPC::failure);
    VestaSource::errorCode
      cedeMastership(const char* requestid, const char** grantidOut,
		     AccessControl::Identity who =NULL) throw (SRPC::failure);

    // Attribute operations
    bool inAttribs(const char* name, const char* value) throw (SRPC::failure);
    char* getAttrib(const char* name) throw (SRPC::failure);
    void getAttrib(const char* name, VestaSource::valueCallback cb, void* cl)
      /*throw (SRPC::failure or anything thrown by the callback)*/;
    void listAttribs(VestaSource::valueCallback cb, void* cl)
      /*throw (SRPC::failure or anything thrown by the callback)*/;
    void getAttribHistory(VestaSource::historyCallback cb, void* cl)
      /*throw (SRPC::failure or anything thrown by the callback)*/;
    VestaSource::errorCode
      writeAttrib(VestaSource::attribOp op,
		  const char* name, const char* value,
		  AccessControl::Identity who =NULL, time_t timestamp =0)
	throw(SRPC::failure);

    VestaSource::errorCode
      measureDirectory(/*OUT*/VestaSource::directoryStats &result,
		       AccessControl::Identity who =NULL)
      throw (SRPC::failure);

    VestaSource::errorCode
      collapseBase(AccessControl::Identity who =NULL)
      throw (SRPC::failure);

    virtual VestaSource *copy() throw();

    // make SRPC call to repository in order to get 
    // a value of [Repository]master_hint setting.
    static Text getMasterHint(const Text &reposHost ="",
			      const Text &reposPort ="")
      throw(SRPC::failure);
    inline Text getMasterHint() throw(SRPC::failure)
    {
      return VDirSurrogate::getMasterHint(host_, port_);
    }

    friend void VDirSurrogate_init();
    friend void VDirSurrogate_init2();
};
