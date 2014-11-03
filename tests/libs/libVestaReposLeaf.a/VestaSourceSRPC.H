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
// VestaSourceSRPC.H
//
// Shared definitions between SRPC client and server for VestaSource
// interface, etc.
//

#ifndef _VSSRPC
#define _VSSRPC 1

#include <MultiSRPC.H>
#include "AccessControl.H"

// The type "identity" used below is an AccessControl::Identity.  It
// is marshalled as follows: 
//
// - First, the flavor is marshalled as an int.
//
// - Second:
//
//   - For the unix flavor, the fields of the underlying
//   authunix_parms are marshalled in the order they appear, with the
//   length of the list of groups marshalled just before the list.
//
//   - For the global flavor, the character string user_ is
//   marshalled.
//
//   - For the gssapi flavor, the character string user_ is
//   marshalled, followed by the appropriate gssapi token.
//   !! May need to do more here.
//
// !!We might also need a way to say "same as last identity" to save
// the time and effort of repeatedly marshalling and verifying the same
// identity after a connection is opened.  This is tolerable now but
// maybe not once GSS-API identities are supported.  However, it's hard
// to see how to do this on top of MultiSRPC...

namespace VestaSourceSRPC {
    enum {
      // current version
      version = 11
    };
    enum {
	Lookup, // see VestaSource::lookup
	// Arguments:
	//   longid      (bytes)  32-byte LongId of directory.
	//   arc         (chars)  Name to look up.
	//                        At most MAX_ARC_LEN plus \0 terminator.
	//   who         (identity)
	// Results:
	//   err         (int)    VestaSource::errorCode.  If err !=
	//                        VestaSource::ok, other results are omitted.
	//   type        (int)    VestaSource::typeTag for result.
	//   rlongid     (bytes)  32-byte LongId of result.
	//   master      (int)    bool; master attribute of result.
	//   pseudoInode (int)    pseudo-inode number for result.
	//   sid         (int)    ShortId of result, 0 if none
	//   timestamp   (int)    modified time of result (as a time_t)
	//   hasAttribs  (int)    bool; does source have mutable attribs?
	//   fptag       (bytes)  8-byte FP::Tag of result

	CreateVolatileDirectory, // see VDirSurrogate::createVolatileDirectory
	// Arguments:
	//   hostname    (chars)  TCP hostname for SRPC to evaluator.
	//   port        (chars)  TCP port for SRPC to evaluator.
	//   handle      (bytes)  8-byte handle of base evalutor directory
	//   timestamp   (int)    time_t
	//   roExisting  (int)    bool
	// Results:
	//   Same as Lookup.

	DeleteVolatileDirectory, // see VDirSurrogate::deleteVolatileDirectory
	// Arguments:
	//   longid      (bytes)  32-byte LongId of directory
	// Results:
	//   err         (int)    VestaSource::errorCode

	List, // see VestaSource::list
	// Arguments:
	//   longid      (bytes)  32-byte LongId of directory
	//   firstIndex  (int)    unsigned int
	//   who         (identity)
	//   deltaOnly   (int)    bool
	//   limit       (int)    Maximum amount to return; see below
	//   overhead    (int)    Used in limit calculation; see below
	// Results:
	//  Returns an SRPC general sequence, using send_seq_*
	//  No length information is passed with send_seq_start.
	//  Each sequence element other than the last is:
	//   arc         (chars)  Name for this entry.
	//                        At most MAX_ARC_LEN plus \0 terminator.
	//   type        (int)    VestaSource::typeTag for this entry
	//   index       (int)    Index for this entry.
	//   pseudoInode (int)    pseudo-inode number for this entry.
	//   filesid     (int)    shortid for this entry or NullShortId.
	//   master      (int)    master flag for this entry.
	//  The last element is:
	//   arc         (chars)  Always ""
	//   type        (int)    VestaSource::unused
	//   err         (int)    VestaSource::errorCode
	//
	//   The sequence terminates when either:
	//     (a) extending the sequence by one would cause the cumulative
	//         cost of the sequence to exceed 'limit', where the cost
	//         of an element of the sequence is strlen(name)+overhead, or
	//     (b) the end of the directory is reached, in which case a
	//         special terminating entry is appended to the sequence
	//         whose 'type' is VestaSource::unused.  This entry is
	//         not included in the cost calculation.

	GetNFSInfo, // see VDirSurrogate::getNFSInfo
	// Arguments:
	//   None.
	// Results:
	//   socket      (chars)  UDP socket of repository NFS server,
	//                        in SRPC-style hostName:portNumber format.
	//   root        (bytes)  32-byte LongId (fhandle) of repository root
	//   muRoot      (bytes)  32-byte LongId (fhandle) of mutable root

	ReallyDelete, // see VestaSource::reallyDelete
	// Arguments:
	//   longid      (bytes)  32-byte LongId of directory
	//   arc         (chars)  At most MAX_ARC_LEN plus \0 terminator.
	//   who         (identity)
	//   existCheck  (int)    bool
	//   timestamp   (int)    time_t
	// Results:
	//   err         (int)    VestaSource::errorCode

	InsertFile, // see VestaSource::insertFile
	// Arguments:
	//   longid      (bytes)  32-byte LongId of directory
	//   arc         (chars)  At most MAX_ARC_LEN plus \0 terminator.
	//   sid         (int)    ShortId
	//   master      (int)    bool
	//   who         (identity)
	//   chk         (int)    VestaSource::dupeCheck
	//   timestamp   (int)    time_t
	//   fptag       (bytes)  8-byte FP::Tag to assign, or 0 bytes if NULL
	// Results:
	//   err         (int)    VestaSource::errorCode.  If err !=
	//                        VestaSource::ok, other results are omitted.
	//   rlongid     (bytes)  32-byte LongId of result.
	//   pseudoInode (int)    pseudo-inode number of result 
	//   fptag       (bytes)  8-byte FP::Tag of result
	//   sid         (int)    ShortId of result

	InsertMutableFile, // see VestaSource::insertMutableFile
	// Arguments and results:
	//   Same as InsertFile

	InsertImmutableDirectory, // see VestaSource::insertImmutableDirectory
	// Arguments:
	//   longid      (bytes)  32-byte LongId of this directory
	//   arc         (chars)  At most MAX_ARC_LEN plus \0 terminator.
	//   dir         (bytes)  32-byte LongId of prototype child dir,
	//                        or NullLongId to represent NULL.
	//   master      (int)    bool
	//   who         (identity)
	//   chk         (int)    VestaSource::dupeCheck
	//   timestamp   (int)    time_t
	//   fptag       (bytes)  8-byte FP::Tag to assign, or 0 bytes if NULL
	// Results:
	//   err         (int)    VestaSource::errorCode.  If err !=
	//                        VestaSource::ok, other results are omitted.
	//   rlongid     (bytes)  32-byte LongId of result.
	//   pseudoInode (int)    pseudo-inode number of result.
	//   fptag       (bytes)  8-byte FP::Tag of result
	//   sid         (int)    ShortId of result

	InsertAppendableDirectory,// see VestaSource::insertAppendableDirectory
	// Arguments:
	//   longid      (bytes)  32-byte LongId of this directory
	//   arc         (chars)  At most MAX_ARC_LEN plus \0 terminator.
	//   master      (int)    bool
	//   who         (identity)
	//   chk         (int)    VestaSource::dupeCheck
	//   timestamp   (int)    time_t
	// Results:
	//   err         (int)    VestaSource::errorCode.  If err !=
	//                        VestaSource::ok, other results are omitted.
	//   rlongid     (bytes)  32-byte LongId of result.
	//   pseudoInode (int)    pseudo-inode number of result.
	//   fptag       (bytes)  8-byte FP::Tag of result

	InsertMutableDirectory, // see VestaSource::insertMutableDirectory
	// Arguments:
	//   longid      (bytes)  32-byte LongId of this directory
	//   arc         (chars)  At most MAX_ARC_LEN plus \0 terminator.
	//   dir         (bytes)  32-byte LongId of prototype child dir,
	//                        or NullLongId to represent NULL.
	//   master      (int)    bool
	//   who         (identity)
	//   chk         (int)    VestaSource::dupeCheck
	//   timestamp   (int)    time_t
	// Results:
	//   err         (int)    VestaSource::errorCode.  If err !=
	//                        VestaSource::ok, other results are omitted.
	//   rlongid     (bytes)  32-byte LongId of result.
	//   pseudoInode (int)    pseudo-inode number of result.
	//   fptag       (bytes)  8-byte FP::Tag of result
	//   sid         (int)    ShortId of result (should be 0)

	InsertGhost, // see VestaSource::insertGhost
	// Arguments and results:
	//   Same as InsertAppendableDirectory

	InsertStub, // see VestaSource::insertStub
	// Arguments and results:
	//   Same as InsertAppendableDirectory

	RenameTo, // see VestaSource::renameTo
	// Arguments:
	//   longid      (bytes)  32-byte LongId of this directory
	//   arc         (chars)  At most MAX_ARC_LEN plus \0 terminator.
	//   fromDir     (bytes)  32-byte LongId of fromDir
	//   fromArc     (chars)  At most MAX_ARC_LEN plus \0 terminator.
	//   who         (identity)
	//   chk         (int)    VestaSource::dupeCheck
	//   timestamp   (int)    time_t
	// Results:
	//   err         (int)    VestaSource::errorCode.

	MakeMutable, // see VestaSource::makeMutable
	// Arguments:
	//   longid      (bytes)  32-byte LongId of this source
	//   sid         (int)    ShortId
	//   copyMaxLo   (int)    low-order 32 bits of copyMax
	//   copyMaxHi   (int)    high-order 32 bits of copyMax
	//   who         (identity)
	// Results:
	//   Same as Lookup.

	obsolete1, // formerly CheckOut
	obsolete2, // formerly Advance
	obsolete3, // formerly CheckIn

	InAttribs, // see VestaSource::inAttribs
	// Arguments:
	//   longid            (bytes)  32-byte LongId of this source
	//   name              (chars)
	//   value             (chars)
	// Results:
	//   retval            (int)    bool; value to return

	GetAttrib, // see VestaSource::getAttrib
	// Arguments:
	//   longid            (bytes)  32-byte LongId of this source
	//   name              (chars)
	// Results:
	//   null              (int)    bool; if true, return NULL
	//   value             (chars)  Otherwise, return this string

	GetAttrib2, // see VestaSource::getAttrib
	// Arguments:
	//   longid            (bytes)  32-byte LongId of this source
	//   name              (chars)
	// Results:
	//  Returns an SRPC general sequence, using send_seq_*
	//  No length information is passed with send_seq_start.
	//  Each sequence element is:
	//   value       (chars)

	ListAttribs, // see VestaSource::listAttribs
	// Arguments:
	//   longid            (bytes)  32-byte LongId of this source
	// Results:
	//  Returns an SRPC general sequence, using send_seq_*
	//  No length information is passed with send_seq_start.
	//  Each sequence element is:
	//   value       (chars)

	GetAttribHistory, // see VestaSource::getAttribHistory
	// Arguments:
	//   longid            (bytes)  32-byte LongId of this source
	// Results:
	//  Returns an SRPC general sequence, using send_seq_*
	//  No length information is passed with send_seq_start.
	//  Each sequence element is:
	//   op          (int)         VestaSource::attribOp
	//   name        (chars)
	//   value       (chars)
	//   timestamp   (int)         time_t

	WriteAttrib, // see VestaSource::writeAttrib
	// Arguments:
	//   longid            (bytes)  32-byte LongId of this source
	//   op                (int)    VestaSource::attribOp
	//   name              (chars)
	//   value             (chars)
	//   who               (identity)
	//   timestamp         (int)    time_t
	// Results:
	//   err               (int)    VestaSource::errorCode

	LookupPathname, // see VestaSource::lookupPathname
	// Arguments:
	//   longid      (bytes)  32-byte LongId of directory.
	//   pathname    (chars)  Name to look up; any length.
	//   who         (identity)
	//   pathnameSep (int)    char
	// Results:
	//   Same as Lookup.

	LookupIndex, // see VestaSource::lookupIndex
	// Arguments:
	//   longid      (bytes)  32-byte LongId of directory.
	//   index       (int)    unsigned int; index to look up
	//   sendarc     (int)    bool
	// Results:
	//   Same as Lookup, followed by...
	//   arc         (chars)  At most MAX_ARC_LEN plus \0 terminator;
	//                          omitted if sendarc is false.

	obsolete6, // was AddUserMapping
	obsolete7, // was RemoveUserMapping
	obsolete8, // was ListUserMappings
	obsolete9, // was AddGroupMapping
	obsolete10, // was RemoveGroupMapping
	obsolete11, // was ListGroupMappings
	obsolete12, // was RefreshImplicitMappings

	MakeFilesImmutable, // see VestaSource::makeFilesImmutable
	// Arguments:
	//   longid      (bytes)  32-byte LongId of directory.
	//   threshold   (int)    unsigned int
	//   who         (identity)
	// Results:
	//   err         (int)    VestaSource::errorCode

	SetIndexMaster, // see VestaSource::setIndexMaster
	// Arguments:
	//   longid      (bytes)  32-byte LongId of directory.
	//   index       (int)    unsigned int
        //   state       (int)    bool
	//   who         (identity)
	// Results:
	//   err         (int)    VestaSource::errorCode

	Stat, // see VestaSource::(executable, size, timestamp)
	// Arguments:
	//   longid      (bytes)  32-byte LongId
	// Results:
	//   err         (int)    VestaSource::errorCode (not used by client)
	//   timestamp   (int)    modified time (as a time_t)
	//   executable  (int)    bool
	//   sizelo      (int)    low-order 32 bits of size
	//   sizehi      (int)    high-order 32 bits of size

	Read, // see VestaSource::read
	// Arguments:
	//   longid      (bytes)  32-byte LongId
	//   nbytes      (int)    number of bytes to read
	//   offsetlo    (int)    low-order 32 bits of offset
	//   offsethi    (int)    high-order 32 bits of offset
	//   who         (identity)
	// Results:
	//   err         (int)    VestaSource::errorCode.  If err !=
	//                        VestaSource::ok, other results are omitted.
	//   buffer      (bytes)  bytes read (<= nbytes)

	Write, // see VestaSource::write
	// Arguments:
	//   longid      (bytes)  32-byte LongId
	//   offsetlo    (int)    low-order 32 bits of offset
	//   offsethi    (int)    high-order 32 bits of offset
	//   buffer      (bytes)  nbytes bytes
	//   who         (identity)
	// Results:
	//   err         (int)    VestaSource::errorCode.  If err !=
	//                        VestaSource::ok, other results are omitted.
	//   rbytes      (int)    number of bytes written

	SetExecutable, // see VestaSource::setExecutable
	// Arguments:
	//   longid      (bytes)  32-byte LongId
	//   state       (int)    bool
	//   who         (identity)
	// Results:
	//   err         (int)    VestaSource::errorCode

	SetSize, // see VestaSource::setSize
	// Arguments:
	//   longid      (bytes)  32-byte LongId
	//   sizelo      (int)    low-order 32 bits of size
	//   sizehi      (int)    high-order 32 bits of size
	//   who         (identity)
	// Results:
	//   err         (int)    VestaSource::errorCode

	SetTimestamp, // see VestaSource::setTimestamp
	// Arguments:
	//   longid      (bytes)  32-byte LongId
	//   timestamp   (int)    time_t
	//   who         (identity)
	// Results:
	//   err         (int)    VestaSource::errorCode

	FPToShortId, // see VDirSurrogate::fpToShortId
	// Arguments:
	//   fptag       (bytes)  8-byte FP::Tag 
	// Results:
	//   sid         (int)    ShortId of result, 0 if none
	
	GetBase,  // see VestaSource::getBase
	// Arguments:
	//   longid      (bytes)  32-byte LongId of directory.
	//   who         (identity)
	// Results:
	//   Same as Lookup.

	obsolete4,  // Old mastership transfer opcodes
        obsolete5,

	// Start an atomic group; see VestaSourceAtomic.H
	Atomic,
	// Arguments:
	//   who         (identity)
	//   One or more of the following.  See VestaSourceAtomic.C
	//   for the real details.  Not all VestaSourceSRPC ops are
	//   supported; in particular, nesting is not allowed.
	//     opcode      (int)    VestaSourceSRPC enum element
	//     additional arguments depending on opcode
	// Results: see AtomicRun or AtomicCancel

	// Set target error codes for an atomic group
	AtomicTarget,
	// Arguments:
	//   target1     (int)    VestaSource::errorCode
	//   target2     (int)    VestaSource::errorCode
	//   okrepl      (int)    VestaSource::errorCode
	// Results: none, atomic group continues

	// Assign the next vsi to this longid
	AtomicDeclare,
	// Arguments:
	//   longid      (bytes)  32-byte LongId
	// Results: none, atomic group continues

	// Do a resync at the server end
	AtomicResync,
	// Arguments:
	//   vsi         (int)    VestaSourceAtomic::VSIndex
	// Results: none, atomic group continues

	// Test vsi's master flag.
	AtomicTestMaster,
	// Arguments:
	//   vsi         (int)    VestaSourceAtomic::VSIndex
	//   master      (int)    bool
	//   err         (int)    VestaSource::errorCode
	// Results: none, atomic group continues

	// Set vsi's master flag
	AtomicSetMaster, // see VestaSource::setMaster
	// Arguments:
	//   vsi         (int)    VestaSourceAtomic::VSIndex
	//   master      (int)    bool
	// Results: none, atomic group continues

	// Invoke AccessControl::check on vsi's ac field
	AtomicAccessCheck,
	// Arguments:
	//   vsi         (int)    VestaSourceAtomic::VSIndex
	//   cls         (int)    AccessControl::Class
	//   expected    (bool)
	//   err         (int)    VestaSource::errorCode
	// Results: none, atomic group continues

	// Test vsi's type field
	AtomicTypeCheck,
	// Arguments:
	//   vsi         (int)    VestaSourceAtomic::VSIndex
	//   allowed     (int)    unsigned int
	//   err         (int)    VestaSource::errorCode
	// Results: none, atomic group continues

	// End an atomic group and run it
	AtomicRun,
	// Arguments: none
	// Results:
	//   ncommands   (int)    Number of commands successfully completed
	//   err         (int)    VestaSource::errorCode of last command tried
	//   okreplace   (int)    VestaSource::errorCode to use if err == ok 
        //                          but success == false
	//   success     (int)    bool, true if program successfully completed

	// End an atomic group and don't run it
	AtomicCancel,
	// Arguments: none
	// Results: none

	// See VestaSourceAtomic::mergeAttrib
	AtomicMergeAttrib,
	// Arguments:
	//   fromvsi     (int)    VestaSourceAtomic::VSIndex
	//   tovsi       (int)    VestaSourceAtomic::VSIndex
	//   name        (chars)
	//   timestamp   (int)    time_t
	// Results: none, atomic group continues

	AcquireMastership,
	// Arguments:
	//   pathname    (chars)  Pathname of object
	//   srcHost     (chars)  Current master repos host ("" if unknown)
	//   srcPort     (chars)  Current master repos port ("" if unknown)
	//   pathnameSep (int)    char
	//   dwho        (identity)  Identity to use at destination (callee)
	//   swho        (identity)  Identity to use at source
	// Results:
	//   err         (int)    VestaSource::errorCode

	CedeMastership,
	// Arguments:
	//   longid      (bytes)  32-byte LongId of object
	//   requestid   (chars)
	//   who         (identity)
	// Results:
	//   err         (int)    VestaSource::errorCode
	//   grantid     (chars)  Sent only if err == VestaSource::ok

	Replicate,
	// Arguments:
	//   pathname    (chars)  Pathname of object
	//   asStub      (int)    bool
	//   asGhost     (int)    bool
	//   srcHost     (chars)  Source repos host ("" if unknown)
	//   srcPort     (chars)  Source repos port ("" if unknown)
	//   pathnameSep (int)    char
	//   dwho        (identity)  Identity to use at destination (callee)
	//   swho        (identity)  Identity to use at source
	// Results:
	//   err         (int)    VestaSource::errorCode

	ReplicateAttribs,
	// Arguments:
	//   pathname    (chars)  Pathname of object
	//   inclAccess  (int)    bool
	//   srcHost     (chars)  Source repos host ("" if unknown)
	//   srcPort     (chars)  Source repos port ("" if unknown)
	//   pathnameSep (int)    char
	//   dwho        (identity)  Identity to use at destination (callee)
	//   swho        (identity)  Identity to use at source
	// Results:
	//   err         (int)    VestaSource::errorCode

	GetUserInfo,
	// Arguments:
	//   who         (identity)  User making the inquiry
	//   subject     (identity)  Identity to inquire about
	// Results:
	//   names       (chars_seq) Global names (incl aliases) of
	//                           this user
	//   groups      (chars_seq) Global groups this user is a
	//                           member of
	//   unix_uid    (int32)     UNIX user ID of this user
	//   unix_gid    (int32)     UNIX group ID of this user's
	//                           primary group
	//   specials    (int16)     0x1 = is_root
	//                           0x2 = is_admin
	//                           0x4 = is_wizard
	//                           0x8 = is_runtool

	RefreshAccessTables,
	// Arguments:
	//   who         (identity) User requesting the refresh
	// Results: none (in the event of failure, SRPC::send_failure
	//          is used to send the error message).

	GetStats,
	// Arguments:
	//   who         (identity)  User making the inquiry
        //   stats       (int16 array)
	//                           Requested statistics.  (See
	//                           ReposStats::StatKind enum.)
	// Results:
	//
	// [sequence of 0 or more of:]
	//
	//   stat_kind   (in16)  Following statistic kind
	//
	//   [if stat_kind == ReposStats::fdCache]
	//
	//     n_in_cache  (int32)
	//     hits        (int64)
	//     open_misses (int64)
	//     try_misses  (int64)
	//     evictions   (int64)
	//     expirations (int64)
	//
	//   [if stat_kind == ReposStats::dupeTotal]
	//
	//     new         (int64)
	//     inProcess   (int64)
	//     completed   (int64)
	//
	//   [if stat_kind == ReposStats::srpcTotal or ReposStats::nfsTotal]
	//
	//     call_count    (int64)
	//     elapsed_secs  (int64)
	//     elapsed_usecs (int64)

	MeasureDirectory, // see VestaSource::measureDirectory
	// Arguments:
	//   longid      (bytes)  32-byte LongId of the directory to
	//                        be measured
	//   who         (identity)
	// Results:
	//   err         (int)    VestaSource::errorCode.  If err !=
	//                        VestaSource::ok, other results are omitted.
	//   baseChainLength (int32)
	//   usedEntryCount  (int32)
	//   usedEntrySize   (int32)
	//   totalEntryCount (int32)
	//   totalEntrySize  (int32)

	CollapseBase,   // see VestaSource::collapseBase
	// Arguments:
	//   longid      (bytes)  32-byte LongId of the directory to
	//                        have its base collapsed
	//   who         (identity)
	// Results:
	//   err         (int)    VestaSource::errorCode.

	SetPerfDebug,
	// Arguments:
	//   who      (identity)
	//   settings (int64) bit mask of features to enable
	// Results:
	//   settings (int64) features now enabled (0 if this 
	//                    repository doesn't have performance
	//                    debugging code)

	GetServerInfo,
	// Arguments:
	//   who      (identity)
	// Results:
	//   version   (chars)  Server version string
	//   startTime (int64)  Server start time
	//   uptime    (int32)  Server uptime in seconds

	ReadWholeCompressed, // see VestaSource::readWhole
	// Arguments:
	//   longid      (bytes)  32-byte LongId
	//   who         (identity)
        //   methods     (int16 array)
	//                        Compression methods supported by the
	//                        client.
	//   max_bytes   (int32)  maximum number of bytes to receive
	//                        in a single chunk (i.e. size of the
	//                        buffer at the receiver)
	// Results:
	//   err         (int)    VestaSource::errorCode.  If err !=
	//                        VestaSource::ok, other results are omitted.
	//   method      (int16)  Compression method used
	//  Returns an SRPC general sequence, using send_seq_*
	//  No length information is passed with send_seq_start.
	//  Each sequence element is:
	//   buffer      (bytes)  compressed data in zlib format

	GetMasterHint
	// Arguments:    none
	// Results:
	//   master_hint  (text)

    };

  // Compression methods
  enum
    {
      // use zlib's deflate/inflate
      compress_zlib_deflate
    };

  // The default host an interface (i.e. that of the local
  // repository).
  Text defaultHost() throw();
  Text defaultInterface() throw();

  // Get a connection to the local repository.
  MultiSRPC::ConnId Start(SRPC *&srpc) throw (SRPC::failure);
  // Get a connection to a repository.  "host_interface" should be of
  // the form "hostname[:interface]".  If the interface is omitted,
  // the default will be used.
  MultiSRPC::ConnId Start(SRPC *&srpc,
			  const Text &host_interface)
    throw (SRPC::failure);
  // Get a connection to a repository.  If either "hostname" or
  // "interface" are the empty string, the defaults will be used.
  MultiSRPC::ConnId Start(SRPC *&srpc,
			  Text hostname, Text interface)
    throw (SRPC::failure);

  // Return a previously obtained connection, making available for
  // use.
  void End(MultiSRPC::ConnId id) throw();

  // Marshall and transmit an AccessControl::Identity.
  void send_identity(SRPC* srpc, AccessControl::Identity who)
    throw(SRPC::failure);
}

#endif //_VSSRPC
