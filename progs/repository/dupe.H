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

/*
// dupe.H
// Last modified on Wed Apr  7 14:15:45 EDT 2004 by ken@xorian.net
//      modified on Tue Sep 12 12:18:00 PDT 2000 by mann
//
// ONC RPC duplicate suppression:
// (1) Detects when an RPC we are still processing is retransmitted
//     and drops the retransmission on the floor.
// (2) Detects when an RPC we recently replied to is retransmitted
//     and retransmits the reply from its cache.
*/

#ifndef _DUPE_H
#define _DUPE_H

#include "nfsd.H"

/* If the RPC is a duplicate, reply to it and return false.  If not,
// note that the RPC is in progress and return true.  In the latter
// case the caller must invoke completed_rpc after replying to the
// call.
*/
extern
#if __cplusplus
"C" 
#endif
int new_rpc(SVCXPRT* xprt, struct rpc_msg* msg);

/* Note that this RPC is completed and cache its result, so that
// is_dupe will retransmit the result it if another thread receives a
// retransmitted call.
*/
extern 
#if __cplusplus
"C"
#endif
void completed_rpc(SVCXPRT* xprt, struct rpc_msg* msg, int replylen);

#if __cplusplus

#include "ReposStats.H"

// Get the current duplicate suppression statistics
ReposStats::DupeStats get_dupe_stats() throw();
#endif

#endif /* _DUPE_H */
