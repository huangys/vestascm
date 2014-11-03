/*
** Copyright (C) 2001, Compaq Computer Corporation
** 
** This file is part of Vesta.
** 
** Vesta is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License as published by the Free Software Foundation; either
** version 2.1 of the License, or (at your option) any later version.
** 
** Vesta is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Lesser General Public License for more details.
** 
** You should have received a copy of the GNU Lesser General Public
** License along with Vesta; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
// svc_udp.h
// Last modified on Mon Nov 12 18:43:16 EST 2001 by ken@xorian.net
//      modified on Fri Jun 28 15:23:52 PDT 1996 by mann
//
// Private interface to internals of svc_udp.c, for use by dupe.C
*/

/*
 * rpc buffer pointer is kept in xprt->xp_p1
 */
#define rpc_buffer(xprt) ((xprt)->xp_p1)

/*
 * additional data about the call is kept in xprt->xp_p2
 */
struct svcudp_data {
	u_int   su_iosz;	/* byte size of send.recv buffer */
	u_int	su_xid;		/* transaction id */
	XDR	su_xdrs;	/* XDR handle */
	char	su_verfbody[MAX_AUTH_BYTES];	/* verifier body */
};
#define	su_data(xprt)	((struct svcudp_data *)(xprt->xp_p2))
