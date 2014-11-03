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

#if defined (__linux__)
// Needed to get PTHREAD_MUTEX_RECURSIVE on older platforms (RH7)
#define _XOPEN_SOURCE 600
#include <features.h>
#endif

#include <pwd.h>
#include <grp.h>
#include <sys/types.h>

#include "PwGrp.H"

// Define mutex for wrappers around following functions: getpwent(),
// getgrent(), getpwuid(), getpwnam(), getgrgid(), getgrnam()
static Basics::mutex pwGrp_mu(PTHREAD_MUTEX_RECURSIVE);

// Group class 
// functions implementations 
void OS::Group::clear()
{ // clear group members sequence
  int gr_mem_num = members.size();
  for(int i = 0; i < gr_mem_num; i++) {
    members.remhi();
  }  
}

// PasswdIter class
// functions implementations
OS::PasswdIter::PasswdIter()
{
  pwGrp_mu.lock();
  setpwent();
}

OS::PasswdIter::~PasswdIter()
{
  endpwent();
  pwGrp_mu.unlock();
}

bool OS::PasswdIter::Next(/*OUT*/OS::Passwd& passwd)
{
  struct passwd* pwent;
  pwent = getpwent();
  if(pwent != 0) {
    passwd.name = pwent->pw_name;
    passwd.uid = pwent->pw_uid;
    passwd.gid = pwent->pw_gid;
    return true;
  }
  return false;
}

// GroupIter class
// functions implementations
OS::GroupIter::GroupIter()
{
  pwGrp_mu.lock();
  setgrent();
}

OS::GroupIter::~GroupIter()
{
  endgrent();
  pwGrp_mu.unlock();
}

bool OS::GroupIter::Next(/*OUT*/OS::Group& group)
{
  struct group* grent;
  grent = getgrent();
  if(grent != 0) {
    group.clear();
    group.name = grent->gr_name;
    group.gid = grent->gr_gid;
    char** grmem;
    for(grmem = grent->gr_mem; *grmem != NULL; grmem++) {
      group.members.addhi(*grmem);
    }
    return true;
  }
  return false;
}

// getpwnam(3) wrapper
bool OS::getPwNam(const Text& user_name, /*OUT*/OS::Passwd& passwd)
{
  pwGrp_mu.lock();
  bool result = false;
  struct passwd* pwent;
  pwent = getpwnam(user_name.cchars());
  if(pwent != 0) {
    passwd.name = pwent->pw_name;
    passwd.uid = pwent->pw_uid;
    passwd.gid = pwent->pw_gid;
    result = true;
  }
  pwGrp_mu.unlock();
  return result;
}

// getpwuid(3) wrapper
bool OS::getPwUid(uid_t uid, /*OUT*/OS::Passwd& passwd)
{
  pwGrp_mu.lock();
  bool result = false;
  struct passwd* pwent;
  pwent = getpwuid(uid);
  if(pwent != 0) {
    passwd.name = pwent->pw_name;
    passwd.uid = pwent->pw_uid;
    passwd.gid = pwent->pw_gid;
    result = true;
  }
  pwGrp_mu.unlock();
  return result;
}

// getgrnam(3) wrapper
bool OS::getGrNam(const Text& user_name, /*OUT*/OS::Group& group)
{
  pwGrp_mu.lock();
  bool result = false;
  struct group* grent;
  grent = getgrnam(user_name.cchars());
  if(grent != 0) {
    group.clear();
    group.name = grent->gr_name;
    group.gid = grent->gr_gid;
    char** grmem;
    for(grmem = grent->gr_mem; *grmem != NULL; grmem++) {
      group.members.addhi(*grmem);
    }
    result = true;
  }
  pwGrp_mu.unlock();
  return result;
}

// getgrgid(3) wrapper
bool OS::getGrGid(gid_t gid, /*OUT*/OS::Group& group)
{
  pwGrp_mu.lock();
  bool result = false;
  struct group* grent;
  grent = getgrgid(gid);
  if(grent != 0) {
    group.clear();
    group.name = grent->gr_name;
    group.gid = grent->gr_gid;
    char** grmem;
    for(grmem = grent->gr_mem; *grmem != NULL; grmem++) {
      group.members.addhi(*grmem);
    }
    result = true;
  }
  pwGrp_mu.unlock();
  return result;
}

