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
// DirShortId.H
// Last modified on Wed Jul 21 00:39:21 EDT 2004 by ken@xorian.net
//      modified on Tue Dec 10 13:41:01 PST 1996 by mann
//
// ShortIds for immutable directories.  Server side only.
// 

#ifndef _DSID_H
#define _DSID_H

#include "SourceOrDerived.H"
#include "ShortIdBlock.H"
#include "FP.H"
#include <fstream>

// Call once before calling any other procedures here.
void InitDirShortId() throw();

// Allocate a new directory sid (i.e., one not currently registered),
// and register it as corresponding to a directory at short address
// sp.  The fingerprint is passed as an argument to help in making
// ShortId assignment deterministic.
ShortId NewDirShortId(const FP::Tag& fptag, Bit32 sp) throw();

// Lookup a sid in the table.  Return 0 if not found.
Bit32 GetDirShortId(ShortId sid) throw();

// Register or reregister an existing directory sid as being at short
// address sp.
void SetDirShortId(ShortId sid, Bit32 sp) throw();

// Log all the ShortIds that are currently in the table.
// Assumes the correct locks for log access are held.
void LogAllDirShortIds(char* tag);

// Checkpoint the ShortIds that are currently in the table and not
// checkpointed already.
void CheckpointAllDirShortIds(Bit32& nextSP, std::fstream& ckpt);

// Delete one ShortId registration.
void DeleteDirShortId(ShortId sid) throw();

// Delete all ShortId registrations, in preparation for rebuilding
// the registration table.
void DeleteAllDirShortIds() throw();

#endif // _DSID_H
