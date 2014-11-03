// Copyright (c) 2000, Compaq Computer Corporation
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

// File: RunToolHost.H
// Last modified on Fri Nov  9 14:02:43 EST 2001 by ken@xorian.net
//      modified on Fri Apr  7 13:44:32 PDT 2000 by mann

#ifndef RunToolHost_H
#define RunToolHost_H

#include <Basics.H>
#include "Expr.H"

// Initialize module
void RunToolHostInit();

// Select a host of the given platform type to run a tool
Text RunToolHost(TextVC *platform, SrcLoc *loc, /*OUT*/void*& handle);

// Indicate we are done with the host returned by RunToolHost
void RunToolDone(void* handle);

#endif // RunToolHost_H
