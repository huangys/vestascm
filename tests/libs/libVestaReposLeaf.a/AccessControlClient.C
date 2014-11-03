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
// AccessControlClient.C
//
// AccessControl implementation required on the client side
//

#include "AccessControl.H"
#include <pwd.h>
#include "ReposConfig.h"

using std::cerr;
using std::endl;

const char* AccessControl::realm = 0;  // [Repository]realm
unsigned int AccessControl::realmlen = 0;
AccessControl::IdentityRep::Flavor AccessControl::defaultFlavor
  = AccessControl::IdentityRep::unspecified;
bool AccessControl::restrictDelete;
uid_t AccessControl::vforeignUser;
gid_t AccessControl::vforeignGroup;
const char* AccessControl::vadminUser;
const char* AccessControl::vadminGroup;
const char* AccessControl::rootUser;
const char* AccessControl::runtoolUser;
const char* AccessControl::vwizardUser;
AccessControl::Identity AccessControl::self_ = 0;

static bool clientDone, commonDone;

static void ensure_realm(Text &val /*IN/OUT*/, const Text &default_realm)
{
  if(val.FindChar('@') == -1)
    {
      // Add the local realm
      val += "@";
      val += default_realm;
    }
}

extern "C"
{
  void AccessControl_commonInit_inner() throw()
  {
    try
      {
	// ------------------------------------------------------------
	// Client initialization
	// ------------------------------------------------------------
	Text t, flavor_var = "[UserInterface]default_flavor";

	// If realm is unitialized, initialize it.
	if(AccessControl::realm == 0)
	  {
	    // Use [UserInterface]realm if set, otherwise use [Repository]realm
	    if(!VestaConfig::get("UserInterface", "realm", t))
	      {
		t = VestaConfig::get_Text("Repository", "realm");
	      }
	    AccessControl::realm = strdup(t.cchars());
	    AccessControl::realmlen = strlen(AccessControl::realm);
	  }

	// If defaultFlavor is unitialized, initialize it.
	if(AccessControl::defaultFlavor == AccessControl::IdentityRep::unspecified)
	  {
	    // Use [UserInterface]default_flavor if set, otherwise use
	    // [Repository]default_flavor
	    if(!VestaConfig::get("UserInterface", "default_flavor", t))
	      {
		t = VestaConfig::get_Text("Repository", "default_flavor");
		flavor_var = "[Repository]default_flavor";
	      }
	    if (strcasecmp(t.cchars(), "unix") == 0) {
	      AccessControl::defaultFlavor = AccessControl::IdentityRep::unix_flavor;
	    } else if (strcasecmp(t.cchars(), "global") == 0) {
	      AccessControl::defaultFlavor = AccessControl::IdentityRep::global;
	    } else if (strcasecmp(t.cchars(), "gssapi") == 0) {
	      AccessControl::defaultFlavor = AccessControl::IdentityRep::gssapi;
	    } else {
	      throw(VestaConfig::failure("bad value for "+flavor_var));
	    }
	  }

	// ------------------------------------------------------------
	// Common initialization
	// ------------------------------------------------------------
	Text rrealm;

	// We need to get [Repository]realm here.  AccessControl::realm
	// could be set to [UserInterface]realm, which could be different
	// from [Repository]realm.  However, when defaulting the realm for
	// the special users/groups, we need to use [Repository]realm, so we
	// ignore AccessControl::realm here.
	rrealm = VestaConfig::get_Text("Repository", "realm");

	t = VestaConfig::get_Text("Repository", "root_user");
	ensure_realm(t, rrealm);
	AccessControl::rootUser = strdup(t.cchars());

	t = VestaConfig::get_Text("Repository", "vadmin_user");
	ensure_realm(t, rrealm);
	AccessControl::vadminUser = strdup(t.cchars());

	t = VestaConfig::get_Text("Repository", "vadmin_group");
	ensure_realm(t, rrealm);
	// Make sure vadminGroup starts with '^' (the group specifier
	// character)
	if(t[0] != '^')
	  {
	    t = "^" + t;
	  }
	AccessControl::vadminGroup = strdup(t.cchars());

	t = VestaConfig::get_Text("Repository", "runtool_user");
	ensure_realm(t, rrealm);
	AccessControl::runtoolUser = strdup(t.cchars());

	try {
	  t = VestaConfig::get_Text("Repository", "vwizard_user");
	  ensure_realm(t, rrealm);
	  AccessControl::vwizardUser = strdup(t.cchars());
	} catch (VestaConfig::failure) {
	  AccessControl::vwizardUser = "";
	}

	AccessControl::vforeignUser =
	  VestaConfig::get_int("Repository", "vforeign_uid");

	AccessControl::vforeignGroup =
	  VestaConfig::get_int("Repository", "vforeign_gid");

	AccessControl::restrictDelete =
	  (bool) VestaConfig::get_int("Repository", "restrict_delete");

	//!! gssapi initialization may be needed here
      }
    catch(const VestaConfig::failure &f)
      {
	cerr << "Configuration error in AccessControl::commonInit:" << endl
	     << f.msg << endl;
	// Fatal error
	abort();
      }
    /* catch(gssapi::failure) ? */
  }
}

void
AccessControl::commonInit() throw ()
{
  static pthread_once_t common_once = PTHREAD_ONCE_INIT;
  pthread_once(&common_once, AccessControl_commonInit_inner);
}

extern "C"
{
  void AccessControl_selfInit_inner() throw()
  {
    switch (AccessControl::defaultFlavor) {
    case AccessControl::IdentityRep::global:
      AccessControl::self_ = NEW(AccessControl::GlobalIdentityRep);
      break;
#if 0
    case AccessControl::IdentityRep::gssapi:
      AccessControl::self_ = NEW(AccessControl::GssapiIdentityRep);
      break;
#endif
    case AccessControl::IdentityRep::unix_flavor:
      AccessControl::self_ = NEW(AccessControl::UnixIdentityRep);
      break;
    default:
      assert("unsupported flavor" && false);
      break;
    }
  }
}

void
AccessControl::selfInit() throw ()
{
  // commonInit sets AccessControl::defaultFlavor, which is needed
  // first.
  commonInit();

  static pthread_once_t self_once = PTHREAD_ONCE_INIT;
  pthread_once(&self_once, AccessControl_selfInit_inner);
}

static void free_CharsSeq(CharsSeq *s)
{
  while(s->size() > 0)
    {
      const char *val = s->remhi();
      assert(val != NULL);
      delete [] val;
   }
  delete s;
}

AccessControl::IdentityRep::~IdentityRep()
{
  if(this->users_cache)
    {
      free_CharsSeq(this->users_cache);
      this->users_cache = 0;
    }
  if(this->groups_cache)
    {
      free_CharsSeq(this->groups_cache);
      this->groups_cache = 0;
    }
}

const char*
AccessControl::IdentityRep::user(int n) throw ()
{
  if(this->users_cache == 0) this->fill_caches();
  assert(this->users_cache != 0);

  if(n < this->users_cache->size())
    return this->users_cache->get(n);
  return NULL;
}

const char*
AccessControl::IdentityRep::group(int n) throw ()
{
  if(this->groups_cache == 0) this->fill_caches();
  assert(this->groups_cache != 0);

  if(n < this->groups_cache->size())
    return this->groups_cache->get(n);
  return NULL;
}

AccessControl::GlobalIdentityRep::GlobalIdentityRep
  (const char* u, const sockaddr_in* o) throw ()
{
  commonInit();
  flavor = global;
  if(o) {
    origin = *o;
  } 
  else {
    memset(&origin,0,sizeof(origin));
  }
  readOnly = false;
  if(u) {
    user_ = u;
  } 
  else {
    uid_t uid = geteuid();
    char *buf = 0;
    OS::Passwd passwd;
    if(OS::getPwUid(uid, passwd)) {
      buf = NEW_PTRFREE_ARRAY(char, (passwd.name.Length() + 1 +
				     AccessControl::realmlen + 1));
      sprintf(buf, "%s@%s", passwd.name.cchars(), AccessControl::realm);
    } 
    else {
      buf = NEW_PTRFREE_ARRAY(char, 22 + AccessControl::realmlen);
      sprintf(buf, "%u@%s", uid, AccessControl::realm);
    }
    user_ = buf;
  }
}

bool AccessControl::GlobalIdentityRep::operator==
(const AccessControl::IdentityRep &other)
  const throw()
{
  // If the other is the same flavor...
  if(other.flavor == global)
    {
      // Cast it to the right pointer type.
      AccessControl::GlobalIdentityRep *global_other =
	(AccessControl::GlobalIdentityRep *) (&other);

      // It's equivalent if the global username is the same.
      return (strcmp(global_other->user_, this->user_) == 0);
    }

  // If the other isn't of the same flavor, it's not equal.
  return false;
}

void AccessControl::GlobalIdentityRep::send(SRPC *srpc)
  const throw(SRPC::failure)
{
  srpc->send_int((int) this->flavor);
  srpc->send_chars(this->user_);
}

AccessControl::GlobalIdentityRep::~GlobalIdentityRep()
{
  if(this->user_) delete [] this->user_;
}

#if 0
// !!Unfinished
AccessControl::GssapiIdentityRep::GssapiIdentityRep(!!)
  throw (AccessControl::GssapiIdentityRep::failure)
{
  commonInit();
  !!
}
#endif

#ifndef NGRPS
// Some platforms don't define this macro.  An authunix_parms is
// supposed to max out at 16 supplementary gorups when sent over the
// network.
#define NGRPS 16
#endif

AccessControl::UnixIdentityRep::UnixIdentityRep
(authunix_parms* aup, const sockaddr_in* o, bool own_aup) throw ()
  : free_aup_(own_aup)
{
  commonInit();
  flavor = unix_flavor;
  if (o) {
    origin = *o;
  } else {
    memset(&origin,0,sizeof(origin));
  }
  readOnly = false;
  if (aup) {
    aup_ = aup;
  } else {
    aup_ = NEW(authunix_parms);
    aup_->aup_machname = NEW_PTRFREE_ARRAY(char, MAX_MACHINE_NAME+1);
#if defined(AUTHUNIX_PARMS_GID_T_GID)
    aup_->aup_gids = NEW_PTRFREE_ARRAY(gid_t, NGRPS);
#elif defined(AUTHUNIX_PARMS_INT_GID)
    aup_->aup_gids = NEW_PTRFREE_ARRAY(int, NGRPS);
#else
#error Unknown type for gid field of authunix_parms
#endif
    aup_->aup_time = time(NULL);
    gethostname(aup_->aup_machname, MAX_MACHINE_NAME+1);
    aup_->aup_uid = geteuid();
    aup_->aup_gid = getegid();
    aup_->aup_len = getgroups(NGRPS, (gid_t*) aup_->aup_gids);
  }
}

bool AccessControl::UnixIdentityRep::operator==
(const AccessControl::IdentityRep &other)
  const throw()
{
  // If the other is the same flavor...
  if(other.flavor == unix_flavor)
    {
      // Cast it to the right pointer type.
      AccessControl::UnixIdentityRep *unix_other =
	(AccessControl::UnixIdentityRep *) (&other);

      // It's equivalent if the user ID is the same
      return (unix_other->aup_->aup_uid == this->aup_->aup_uid);
    }

  // If the other isn't of the same flavor, it's not equal.
  return false;
}

void AccessControl::UnixIdentityRep::send(SRPC *srpc)
  const throw(SRPC::failure)
{
  srpc->send_int((int) this->flavor);
  authunix_parms* aup = this->aup_;
  srpc->send_int((int) aup->aup_time);
  srpc->send_chars(aup->aup_machname);
  srpc->send_int(aup->aup_uid);
  srpc->send_int(aup->aup_gid);
  srpc->send_seq_start((int)aup->aup_len);
  unsigned int i;
  for (i=0; i<aup->aup_len; i++) {
    srpc->send_int(aup->aup_gids[i]);
  }
  srpc->send_seq_end();
}

AccessControl::UnixIdentityRep::~UnixIdentityRep()
{
  if(this->free_aup_ && this->aup_)
    {
      if(this->aup_->aup_machname) delete [] this->aup_->aup_machname;
      if(this->aup_->aup_gids) delete [] this->aup_->aup_gids;
      delete this->aup_;
    }
}

AccessControl::Identity
AccessControl::self() throw (/*GssapiIdentityRep::failure*/)
{
  selfInit();
  return self_;
}
