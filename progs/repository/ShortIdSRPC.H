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
// ShortIdSRPC.H
// Last modified on Mon Nov 12 18:37:56 EST 2001 by ken@xorian.net
//      modified on Thu Jul 20 14:27:16 PDT 2000 by mann
//
// Shared definitions between SRPC client and server for ShortId interface
//

#ifndef _SIDSRPC
#define _SIDSRPC 1

class ShortIdSRPC {
  public:
    enum { version = 4 };
    enum { 
	AcquireShortIdBlock,
	// Arguments:
	//   leafflag         (int)    bool
	// Results:
	//   start            (int)    ShortId
	//   leaseExpires     (int)    time_t
	//   bits             (bytes)  bitmap of already used ShortIds
	
	RenewShortIdBlock,
	// Arguments:
	//   start            (int)    ShortId
	// Results:
	//   leaseExpires     (int)    time_t
	//   res              (int)    bool, return value

	ReleaseShortIdBlock,
	// Arguments:
	//   start            (int)    ShortId
	// Results:
	//   None.

	OldKeepDerived,
	// "force" is always true
	// Arguments:
	//   ds               (int)    ShortId
	//   dt               (int)    time_t
	// Results:
	//   res              (int)    errno value

	Checkpoint,
	// Arguments:
	//   None.
	// Results:
	//   None.

	GetWeedingState,
	// Arguments:
	//   None.
	// Results:
	//   ds                   (int)    ShortId
	//   dt                   (int)    time_t
	//   ss                   (int)    ShortId
	//   st                   (int)    time_t
	//   sourceWeedInProgress (int)    bool
	//   deletionsInProgress  (int)    bool
	//   deletionsDone        (int)    bool
	//   checkpointInProgress (int)    bool

	KeepDerived
	// Arguments:
	//   ds               (int)    ShortId
	//   dt               (int)    time_t
	//   force            (int)    bool
	// Results:
	//   res              (int)    errno value
    };
};

#endif //_SIDSRPC
