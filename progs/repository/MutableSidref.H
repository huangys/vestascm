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

// MutableSidref.H - suopport functions for mutable shortid reference
// counting in the mutable root

#include "VestaSource.H"

// Initialize the mutable shortid reference counter.  Note: doesn't
// acquire StableLock, assumes that the repository isn't yet "open for
// business".
void MutableSidrefInit();

// Optional paranoid consistency checks of the mutable shortid
// reference counter
void MutableSidrefCheck(VestaSource *vs, ReadersWritersLock *lock);
void MutableSidrefCheck(const LongId &longid, ReadersWritersLock *lock = 0);
void MutableSidrefCheck(ReadersWritersLock *lock = 0,
			LongId::lockKindTag lockKind = LongId::writeLock);

// Should we do a consistency check during log recovery?  This also
// determines whether we rebuild the mutable shortid reference counts
// after reading the checkpoint file or after finishing recovery of
// all transaction logs.
bool MutableSidrefRecoveryCheck();
