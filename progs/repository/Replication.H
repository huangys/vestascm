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
// Replication.H
// Last modified on Mon Nov 12 18:37:56 EST 2001 by ken@xorian.net
//      modified on Fri May 18 17:08:50 PDT 2001 by mann
//

#include "UniqueId.H"
#include "FP.H"
#include "VestaSource.H"

// Implements VDirSurrogate::replicate
VestaSource::errorCode Replicate(const char* pathname,
    bool asStub, bool asGhost,
    const char* srcHost, const char* srcPort, char pathnameSep,
    AccessControl::Identity dwho, AccessControl::Identity swho) throw ();

// Implements VDirSurrogate::replicateAttribs
VestaSource::errorCode ReplicateAttribs(const char* pathname,
    bool includeAccess,
    const char* srcHost, const char* srcPort, char pathnameSep,
    AccessControl::Identity dwho, AccessControl::Identity swho) throw ();

// Called during repository initialization, after recovery but before
// VestaSourceRPC interface is exported.  Deletes any mutable
// directories left over from unfinished Replicate calls.
void ReplicationCleanup() throw ();
