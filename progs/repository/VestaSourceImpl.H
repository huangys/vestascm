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
// VestaSourceImpl.H
// Last modified on Sun Nov 28 18:22:26 EST 2004 by ken@xorian.net
//      modified on Fri Sep 15 19:41:11 PDT 2000 by mann
//
// Grungy stuff shared between various modules within the repository

#ifndef _VSI_H
#define _VSI_H 1

#include "SRPC.H"
#include "AccessControl.H"

// VestaSourceServer.C and VSAServer.C
AccessControl::Identity srpc_recv_identity(SRPC* srpc, int intf_ver,
					   bool access_needed = true);
void VSAtomic(SRPC* srpc, int intf_ver);

#endif
