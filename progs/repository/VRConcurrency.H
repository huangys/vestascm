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
// VRConcurrency.H
//
// Globals used for logging and concurrency control in the repository
// server.
//

#ifndef _VRCONCURRENCY_H_
#define _VRCONCURRENCY_H_

#include "ReadersWritersLock.H"
#include "VestaLog.H"

extern ReadersWritersLock VolatileRootLock;
extern ReadersWritersLock StableLock;

// StableLock.write must be held to modify any stable repository
// state (such as the appendable or mutable directory tree or the
// log).  StableLock.read must be held to read such state.
//
// VolatileRootLock.write allows modifying the volatile root;
// VolatileRootLock.read allows reading it.  The root of each subtree
// immediately below the volatile root has its own readers/writers
// lock, which allows reading or writing the subtree.
//
// A thread that is checkpointing or weeding must acquire
// VolatileRootLock.write, all volatile subtree write locks, and
// StableLock.write, in that order.

// Code throughout the repository makes use of the fact that, with the
// exception of the volatile root, if you hold the appropriate lock to
// manipulate a directory, then you hold the appropriate lock to
// manipulate its children and its base.

extern VestaLog VRLog;
extern int VRLogVersion;

// VRLog is the repository log.  Write methods on VestaSource objects
// internally call the start/commit methods on this log, but because
// start/commit pairs may be nested, clients can group such methods
// into larger atomic units by bracketing them with their own
// start/commit pairs.

// When the semantics of some logged operation change, VRLogVersion
// must change.  This change is logged and checkpointed, and if
// recovery begins with an older log version, operations must be
// carried out with the old semantics until a log record is processed
// that updates VRLogVersion to a newer version.

#endif
