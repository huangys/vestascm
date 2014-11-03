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
// VRErrorCode.H
// Last modified on Mon Sep 22 13:35:57 EDT 2003 by ken@xorian.net
//      modified on Thu Aug 24 13:43:12 PDT 2000 by mann
//

#ifndef _VREC
#define _VREC 1

class VRErrorCode {
 public:
  enum errorCode { ok = 0,           // No error.
		   notFound,         // Name not bound.
		   noPermission,     // Access control violation.
		   nameInUse,        // Name was already bound.
		   inappropriateOp,  // E.g., insert in immutableDirectory.
		   nameTooLong,
		   rpcFailure,       // Callout from repository (to evaluator
		                     //   or to another repository) failed.
		   notADirectory,    // Directory op on nondirectory.
		   isADirectory,     // Nondirectory op on directory.
		   invalidArgs,      // Catchall; e.g., rename would loop
		   outOfSpace,
		   notMaster,

		   longIdOverflow    // Attempt to create something
				     // that would have a LongId > 32
				     // bytes

  };
};

#endif // _VREC
