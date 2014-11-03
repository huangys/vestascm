// Copyright (C) 2005, Vesta free software project
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

// MutableSidref.C - suopport functions for mutable shortid reference
// counting in the mutable root

#include "MutableSidref.H"
#include "VDirChangeable.H"
#include "logging.H"

// Should we skip the check altogether?
static bool skip_check = true;

// Should we automatically correct any inconsistency detected?
static bool auto_correct = true;

// Should we consistency check across log recovery?
static bool recovery_check = false;

// Single-time initialization code
static pthread_once_t MutableSidrefCheck_once = PTHREAD_ONCE_INIT;
extern "C"
{
  static void MutableSidrefCheck_init()
  {
    Text value;
    if(VestaConfig::get("Repository", "mutable_sid_refcount_paranoia", /*OUT*/ value))
      {
	// Development mode: abort if an inconsistency is detected
	if(value == "abort")
	  {
	    skip_check = false;
	    auto_correct = false;

	    // Do extra checking that recovering log entries updates
	    // sidref correctly.
	    recovery_check = true;
	  }
	// Early deployment mode: auto-correct if an inconsistency is
	// detected
	else if(value == "correct")
	  {
	    skip_check = false;
	    auto_correct = true;
	  }
	// Default to skipping the check
	else
	  {
	    skip_check = true;
	  }
      }
    else
      {
	skip_check = true;
      }
  }
}

void MutableSidrefInit()
{
  Repos::dprintf(DBG_ALWAYS, 
		 "Rebuilding mutable shortid reference counts\n");
  ((VDirChangeable *) VestaSource::mutableRoot())->buildMutableSidref();
  Repos::dprintf(DBG_ALWAYS, 
		 "Done rebuilding mutable shortid reference counts\n");
}

void MutableSidrefCheck(VestaSource *vs, ReadersWritersLock *lock)
{
  if((vs != 0) && (lock != 0))
    MutableSidrefCheck(vs->longid, lock);
}

void MutableSidrefCheck(const LongId &longid, ReadersWritersLock *lock)
{
  if(MutableRootLongId.isAncestorOf(longid))
    {
      MutableSidrefCheck(lock, LongId::checkLock);
    }
}

void MutableSidrefCheck(ReadersWritersLock *lock,
			LongId::lockKindTag lockKind)
{
  // Ensure that we've initialized our settings
  pthread_once(&MutableSidrefCheck_once, MutableSidrefCheck_init);

  // Do nothing if we're supposed to skip the check
  if(skip_check) return;

  // Acquire or check the lock
  bool releaseLock = false;
  VestaSource *mr = 0;
  if(lock != 0)
    {
      mr = VestaSource::mutableRoot(LongId::checkLock, &lock);
    }
  else
    {
      mr = VestaSource::mutableRoot(lockKind, &lock);
      releaseLock = (lock != 0);
    }
  assert(mr != 0);

  // Perform the consistency check
  ((VDirChangeable *) mr)->checkMutableSidref();

  // Release the lock if needed
  if(releaseLock) lock->releaseWrite();
}

bool MutableSidrefRecoveryCheck()
{
  // Ensure that we've initialized our settings
  pthread_once(&MutableSidrefCheck_once, MutableSidrefCheck_init);

  return recovery_check; 
}
