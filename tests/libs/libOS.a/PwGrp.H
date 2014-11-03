// Copyright (C) 2005, Vesta Free Software Project
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

/*
  getpwent(), getgrent(), getpwuid(), getpwnam(), getgrgid(), getgrnam()
  library functions are not thread safe. 

  In order to make sure that only one thread calls them in a time we  
  defined the folowing wrappers:
  
  class PasswdIter: wrapper around iterative calls to getpwent() function
  class GroupIter: wrapper around iterative calls to getgrent() function
  getPwNam: wrapper around getpwnam() function
  getGrNam: wrapper around getgrnam() function
  getPwUid: wrapper around getpwuid() function
  getGrGid: wrapper around getgrgid() function

  Note that the constructors of PasswdIter and GroupIter acquire a
  mutex.  This lock is only released when the object is destroyed, so
  you should only ever keep these objects in existence long enough to
  loop over the passworg/group information and then destory them.

  The same mutex (pwGrp_mu) is acquired by the consturctors of
  PasswdIter and GroupIter, as well as getPwNam, getGrNam, getGrGid,
  getPwUid. In order to prevent a thread from inadvertenly causing a
  deadlock (by creating both iterators at the same time, or calling
  one of the functions while an iterator exists) the
  PTHREAD_MUTEX_RECURSIVE type of mutex is used.
*/

#ifndef _PwGrp_H
#define _PwGrp_H

#include <Basics.H>
#include <Generics.H>

namespace OS
{
  // Represents a single entry in the password database.  (Similar to
  // the standard struct passwd.)
  struct Passwd
  {
    Text name; // Username
    uid_t uid; // User ID.
    gid_t gid; // Group ID.
  };
  
  // Represents a single group in the group database.  (Similar to the
  // standard struct group.)
  class Group
  {
  public:
    Text name; // Group name.
    gid_t gid; // Group ID.
    TextSeq members; // Member list.
    
    Group() {};
    void clear();
  };

  // Iterator for looping over all the entries in the password
  // database.
  class PasswdIter
  {
  public:
    // Note: constructor acquires lock, destructor releases it.
    PasswdIter();
    ~PasswdIter();

    // Get the next password entry.  Modifies "passwd" in place.
    // Returns true if "passwd" was filled with a valid entry, false if
    // there are no more entries.
    bool Next(/*OUT*/Passwd& passwd);
  };
  
  // Iterator for looping over all the entries in the group database.
  class GroupIter
  {
  public:
    // Note: constructor acquires lock, destructor releases it.
    GroupIter();
    ~GroupIter();

    // Get the next group entry.  Modifies "group" in place.  Returns
    // true if "group" was filled with a valid entry, false if there
    // are no more entries.
    bool Next(/*OUT*/Group& group);
  };

  // Thread-safe wrappers for getpwnam(3), getgrnam(3), getpwuid(3),
  // getgrgid(3).  The second argument ("passwd" or "group") is
  // modified in place.  The return value indicates whether a matching
  // entry was found.

  bool getPwNam(const Text& user_name, /*OUT*/Passwd& passwd);
  bool getGrNam(const Text& user_name, /*OUT*/Group& group);
  bool getPwUid(uid_t uid, /*OUT*/Passwd& passwd);
  bool getGrGid(gid_t gid, /*OUT*/Group& group);

};

#endif //_PwGrp_H
