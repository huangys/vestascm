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
// AccessControl.H
//

#ifndef _AccessControl
#define _AccessControl 1

#include "Basics.H"
#include "VestaAttribs.H"
#include "VestaConfig.H"
#include "CharsSeq.H"
#include <PwGrp.H>
#include <chars_seq.H>
#include <sys/types.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/auth_unix.h>

// Forward declaration of functions uder with pthread_once (which must
// have C linkage).
extern "C"
{
  void AccessControl_commonInit_inner() throw();
  void AccessControl_selfInit_inner() throw();
}

//
// Access control list for a Vesta source
//
class AccessControl {
 public:
  //
  // Nested class for the identity of a principal
  //
  class IdentityRep {
  protected:
    CharsSeq *users_cache, *groups_cache;
    virtual void fill_caches()  throw() = 0;

    IdentityRep() : users_cache(0), groups_cache(0) { }
  public:
    // Flavor of credentials used.  "unspecified" is not a valid
    // flavor; it is used as a special flag in various places.

    // (Just "unix" is often a pre-defined macro, so we use
    // "unix_flavor instead.)
    enum Flavor { unix_flavor, global, gssapi,
		  nflavors=3, unspecified=255 };
    Flavor flavor;
    
    // Host of origin
    sockaddr_in origin;

    // Global user names.  Primary name is 0th.
    // Returns NULL after users are exhausted.
    virtual const char* user(int n = 0) throw (); // shared, do not free

    // Global group names.  Primary name is 0th.
    // Returns NULL after groups are exhausted.
    virtual const char* group(int n = 0) throw (); // shared, do not free

    // Forbid write-like operations to this user
    bool readOnly;

    // For use by NFS glue
    virtual uid_t toUnixUser() throw () = 0;
    virtual gid_t toUnixGroup() throw () = 0;

    //
    // Convenience methods for use by check()
    //

    // Would user(i) for some i return name?
    bool userMatch(const char* name) throw ();

    // Would group(i) for some i return name?
    bool groupMatch(const char* name) throw ();

    // Would userMatch(v) return true for some value v of the aname attribute?
    bool userMatch(const char* aname, VestaAttribs attribs) throw ();

    // Would groupMatch(v) return true for some value v of the aname attribute?
    bool groupMatch(const char* aname, VestaAttribs attribs) throw ();

    virtual bool operator==(const IdentityRep &other) const throw() = 0;
    bool operator !=(const IdentityRep &other) const throw()
    {
      return !(*this == other);
    }

    virtual void send(SRPC *srpc) const throw(SRPC::failure) = 0;
    virtual ~IdentityRep();
  };
  typedef IdentityRep* Identity;

  class GlobalIdentityRep : public IdentityRep {
  protected:
    const char* user_;
    void fill_caches()  throw();
  public:
    // Note: the constructor retains the supplied char*.  If u is
    // NULL, the effective uid of the current process is translated to
    // a global name and used.
    GlobalIdentityRep(const char* u =NULL, const sockaddr_in* o =NULL) throw();
    const char* user(int n = 0) throw (); // shared, do not free
    const char* group(int n = 0) throw (); // shared, do not free
    uid_t toUnixUser() throw () { return globalToUnixUser(user(0)); };
    gid_t toUnixGroup() throw () { return globalToUnixGroup(group(0)); };

    bool operator==(const IdentityRep &other) const throw();
    void send(SRPC *srpc) const throw(SRPC::failure);
    ~GlobalIdentityRep();
  };

#if 0
  // !!Unfinished
  class GssapiIdentityRep : public GlobalIdentityRep {
  private:
    gss_cred_id_t* creds_;  // NULL if not owned by this process
  public:
    // The first form imports a plain character string name.
    // If u is NULL, the default principal for the current process is
    // used, i.e., whoever the user logged in to Kerberos as with kinit.
    GssapiIdentityRep(const char* u =NULL, const sockaddr_in* o =NULL)
      throw(failure);
    GssapiIdentityRep(const gss_name_t u, const sockaddr_in* o =NULL)
      throw(failure);
    GssapiIdentityRep(const gss_cred_id_t c, const sockaddr_in* o =NULL)
      throw(failure);

    ~GssapiIdentityRep();  // may need base type to have a virtual destructor

    class failure {
    public:
      Bit32 generic, minor;
      Text where;
      failure(Bit32 g, Bit32 m, Text w) : generic(g), minor(m), where(w) {};
    };
  };
#endif

  class UnixIdentityRep : public IdentityRep {
  protected:
    virtual void fill_caches()  throw();
  private:
    authunix_parms* aup_;
    bool free_aup_;  // Should we free aup_ when this object is destroyed?
  public:
    // Note: the constructor retains the supplied aup pointer
    UnixIdentityRep(authunix_parms* aup =NULL, const sockaddr_in* o =NULL, bool own_aup=true)
      throw ();
    const char* user(int n = 0) throw (); // shared, do not free
    const char* group(int n = 0) throw (); // shared, do not free
    uid_t toUnixUser() throw () { return aup_->aup_uid; };
    gid_t toUnixGroup() throw () { return aup_->aup_gid; };

    // Server-side only call to make sure that the unig user/group ids
    // have mappings to global user/group names, possibly updating the
    // repository's internal tables.
    void validate() throw();

    bool operator==(const IdentityRep &other) const throw();
    void send(SRPC *srpc) const throw(SRPC::failure);
    ~UnixIdentityRep();
  };

  // Used to represent a user's information for the "whoami" call.
  class IdInfo {
  public:
    chars_seq names;  // Global usersnames (includes aliases)
    chars_seq groups; // Global group names

    uid_t unix_uid;   // The Unix UID of this user (used by the
		      // repository's NFS interface).
    uid_t unix_gid;   // The user's primary Unix GID (used by the
		      // repository's NFS interface).

    bool is_root;     // Does this user have root access?
    bool is_admin;    // Does this user have admin access?
    bool is_wizard;   // Does this user have wizard access?
    bool is_runtool;  // Is this user the runtool user (or aliased to
		      // it)?

    IdInfo() : names(), groups(), unix_uid(0), unix_gid(0),
	       is_root(false), is_admin(false),
	       is_wizard(false), is_runtool(false)
    { }
  };

  class ParseError {
  public:
    ParseError(Text m) : message(m) { };
    Text message;

    // The file in which the parsing error occurred.
    Text fname;

    // The type of file in which the parsing error occurred.
    Text fkind;
  };

  //
  // Conversions between local Unix uid/gid and global name
  // Fields are constant after initialization
  //
  static const char* realm;     // [Repository]realm (or 
  static unsigned int realmlen; // length of the realm string
  static IdentityRep::Flavor defaultFlavor;
  static uid_t globalToUnixUser(const char* user) throw ();
  static gid_t globalToUnixGroup(const char* group) throw ();
  static const char* unixToGlobalUser(uid_t uid) throw ();  // do not free
  static const char* unixToGlobalGroup(gid_t gid) throw (); // do not free
  static void refreshAccessTables() throw(ParseError); // server-side only

  uid_t toUnixUser() throw ();
  gid_t toUnixGroup() throw ();

  //
  // Parameters and methods for access checking
  //

  // AccessControl::del access is the same as administrative access
  // if restrictDelete is true; the same as write access otherwise.
  // That is, only an administrator can delete from an appendable
  // directory (creating a ghost) if restrictDelete is true;
  // otherwise anyone with write access to the directory can do so.
  // Available only within the repository address space.
  static bool restrictDelete;

  //
  // Special local and global users
  //
  static uid_t vforeignUser;       // globalToUnixUser if user is not local
  static gid_t vforeignGroup;      // globalToUnixGroup if group is not local
  static const char* vadminUser;   // privileged user
  static const char* vadminGroup;  // default group for /vesta
  static const char* rootUser;     // additional privileged user (root@realm)
  static const char* runtoolUser;  // owner of the volatile tree
  static const char* vwizardUser;  // can fix (or break) replication invariant

  // Operation types.  Note: VestaSource does not keep execute
  // permission bits for files, and AccessControl does not support
  // execute permission checking.  The NFS glue layer handles execute
  // permission by clearing the execute bits for a file if the
  // underlying shortid file is not executable or setting them to be
  // the same as the read bits if it is.
  //
  enum Class { unrestricted, administrative, ownership, read, write,
	       search, del, setuid, setgid, agreement };

  // Check whether someone with the given identity is permitted to
  // make an access of the given class under this access control.
  // If who is NULL, the identity is that of the caller: the Vesta
  // administrator if the call is from the Vesta server address
  // space, the current process's effective user and groups
  // otherwise.  Assumes that admit() is true.  The value argument
  // is used with the setuid and setgid classes, to say what the
  // object is being made setuid or setgid to.
  //
  bool check(Identity who, Class cls, const char* value =NULL) throw ();

  // Check whether the given identity is allowed any access at
  // all to the local repository.
  static bool admit(Identity who) throw ();

  //
  // Get this process's default identity.
  // Uses a cached value unless flush is true, in which case
  // the cache is updated.
  //
  static Identity self()
    throw (/*GssapiIdentityRep::failure*/);

  //
  // Specific fields and methods of an AccessControl object
  //
  typedef Bit32 ModeBits;
  static ModeBits parseModeBits(const char* char_mode) throw ();
  static const char* formatModeBits(ModeBits mode) throw (); // caller frees

  ModeBits mode;
  VestaAttribs owner; // inherit nonempty #owner attrib from here
  VestaAttribs group; // inherit nonempty #group attrib from here

  //
  // One-time module initialization.
  //
 public:
  // server side of VestaSourceSRPC
  static void serverInit()
    throw (VestaConfig::failure /*, gssapi::failure*/);
  // client side of VestaSourceSRPC and common bits used by both
  // client and server.
  static void commonInit() throw ();
 private:

  friend void AccessControl_commonInit_inner() throw();

  static void selfInit() throw ();
  friend void AccessControl_selfInit_inner() throw();

  static Identity self_;
};

#endif // _AccessControl
