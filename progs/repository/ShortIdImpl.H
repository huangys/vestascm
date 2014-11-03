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
// ShortIdImpl.H
//
// Server implementations of remote methods in ShortIdBlock.H
// 

#ifndef _SIS
#define _SIS

#include "ShortIdBlock.H"
#include <fstream>

extern void AcquireShortIdBlock(ShortIdBlock& bk,
				bool leafflag, bool local =false) throw();
extern bool RenewShortIdBlock(ShortIdBlock& block) throw();
extern void ReleaseShortIdBlock(ShortIdBlock& block, bool local =false)
     throw();

// Do actual deletions.  sidfile must be sorted.  Any file with a
// modified time greater than or equal to lease is not deleted.  For
// each file deleted, a line will be written to deleted_file with its
// shortid, deleted_count will be incremented, and deleted_space will
// be incremented by the approximate amount of disk space reclaimed
// (in bytes).  Returns 0 if successful, an errno value if not
extern int DeleteAllShortIdsBut(ShortIdsFile sidfile, time_t lease,
				ShortIdsFile deleted_file,
				/*OUT*/ Basics::uint32 &deleted_count,
				/*OUT*/ Basics::uint64 &deleted_space) throw();

// Checkpoint support.  Called with VRLock write lock already held.
extern void ShortIdBlockCheckpoint(std::fstream& ckpt) throw();

// If debug messages for shortid blocks are turned on, dump the
// complete list of all leases.  Incorporate the text "message" into
// what gets printed.
extern void DebugShortIdBlocks(const char *message) throw();

// Local initialization
extern void ShortIdServerInit();                      // call before Recover
extern void ShortIdServerInit2();                     // call after Recover
extern void ShortIdServerExport(char *serverName =0); // call after Recover

#endif // _SIS
