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
// VRWeed.H
// Last modified on Mon Nov 12 18:37:56 EST 2001 by ken@xorian.net
//      modified on Thu Jul 20 14:30:53 PDT 2000 by mann
//
// Miscellaneous repository weeding code.
//

#include "Basics.H"
#include "SourceOrDerived.H"

// Called from VestaSource::init()
void InitVRWeed();

// Implementation for RPC weeder interface

// Returns 0 if successful, otherwise an error code from errno.h
extern int KeepDerived(ShortIdsFile ds, time_t dt, bool force);

extern void Checkpoint() throw();

// This routine must be called during initialization by a thread with
// a large stack, preferably the main thread.  Does not return.
extern void CheckpointServer() throw();

extern void GetWeedingState(ShortIdsFile& ds, time_t& dt,
			    ShortIdsFile& ss, time_t& st,
			    bool& sourceWeedInProgress,
			    bool& deletionsInProgress,
			    bool& deletionsDone,
			    bool& checkpointInProgress) throw();


// Exposed for use by recovery.  Both are no-ops if not needed.

// Returns 0 if successful, otherwise an error code from errno.h
extern int SourceWeed() throw();

// Returns 0 if successful, otherwise an error code from errno.h
extern int DoDeletions() throw();

