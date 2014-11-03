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
// vnfs/svc_udp.c
//
// Server side for UDP/IP based RPC.  Sun duplicate suppression (caching)
// totally removed and replaced by my own implementation (in the dupe.C
// module).
*/

/* @(#)svc_udp.c	2.2 88/07/29 4.0 RPCSRC */
/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 * 
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 * 
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 * 
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 * 
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 * 
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */
#if !defined(lint) && defined(SCCSIDS)
static char sccsid[] = "@(#)svc_udp.c 1.24 87/08/11 Copyr 1984 Sun Micro";
#endif

/*
 * svc_udp.c,
 * Server side for UDP/IP based RPC.  (Does some caching in the hopes of
 * achieving execute-at-most-once semantics.)
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#include <pthread.h>
#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>

#include "system.h"
#include "svc_udp.h"

#include "timing.H"
#include <assert.h>

#include "ReposConfig.h"

#if !defined(MAX)
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif

static bool_t		svcudp_recv();
static bool_t		svcudp_reply();
static enum xprt_stat	svcudp_stat();
static bool_t		svcudp_getargs();
static bool_t		svcudp_freeargs();
static void		svcudp_destroy();

static struct xp_ops svcudp_op = {
	svcudp_recv,
	svcudp_stat,
	svcudp_getargs,
	svcudp_reply,
	svcudp_freeargs,
	svcudp_destroy
};

/*
 * Usage:
 *	xprt = svcudp_create(sock);
 *
 * If sock<0 then a socket is created, else sock is used.
 * If the socket, sock is not bound to a port then svcudp_create
 * binds it to an arbitrary port.  In any (successful) case,
 * xprt->xp_sock is the registered socket number and xprt->xp_port is the
 * associated port number.
 * Once *xprt is initialized, it is registered as a transporter;
 * see (svc.h, xprt_register).
 * The routines returns NULL if a problem occurred.
 */
SVCXPRT *
svcudp_bufcreate(sock, sendsz, recvsz)
     register int sock;
     u_int sendsz, recvsz;
{
    bool_t madesock = FALSE;
    register SVCXPRT *xprt;
    register struct svcudp_data *su;
    struct sockaddr_in addr;
    int len = sizeof(struct sockaddr_in);

    if (sock == RPC_ANYSOCK) {
	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
#if 0 /*!!*/
	    dprintf(DBG_ALWAYS,
		    "svcudp_create - socket creation problem: %s\n",
		    strerror(errno));
#endif
	    return ((SVCXPRT *)NULL);
	}
	madesock = TRUE;
    }
    bzero((char *)&addr, sizeof (addr));
    addr.sin_family = AF_INET;
    if (bindresvport(sock, &addr)) {
	addr.sin_port = 0;
	(void)bind(sock, (struct sockaddr *)&addr, len);
    }
    if (getsockname(sock, (struct sockaddr *)&addr, &len) != 0) {
#if 0 /*!!*/
	dprintf(DBG_ALWAYS, "svcudp_create - cannot getsockname: %s");
#endif
	if (madesock)
	  (void)close(sock);
	return ((SVCXPRT *)NULL);
    }
    xprt = (SVCXPRT *)mem_alloc(sizeof(SVCXPRT));
    if (xprt == NULL) {
	(void)fprintf(stderr, "svcudp_create: out of memory\n");
	if (madesock)
	  (void)close(sock);
	return (NULL);
    }
    su = (struct svcudp_data *)mem_alloc(sizeof(*su));
    if (su == NULL) {
	(void)fprintf(stderr, "svcudp_create: out of memory\n");
	mem_free((char *) xprt, sizeof(SVCXPRT));
	if (madesock)
	  (void)close(sock);
	return (NULL);
    }
    su->su_iosz = ((MAX(sendsz, recvsz) + 3) / 4) * 4;
    if ((rpc_buffer(xprt) = mem_alloc(su->su_iosz)) == NULL) {
	(void)fprintf(stderr, "svcudp_create: out of memory\n");
	mem_free((char *) su, sizeof(*su));
	mem_free((char *) xprt, sizeof(SVCXPRT));
	if (madesock)
	  (void)close(sock);
	return (NULL);
    }
    xdrmem_create(
		  &(su->su_xdrs), rpc_buffer(xprt), su->su_iosz, XDR_DECODE);
    xprt->xp_p2 = (caddr_t)su;
    xprt->xp_verf.oa_base = su->su_verfbody;
    xprt->xp_ops = &svcudp_op;
    xprt->xp_port = ntohs(addr.sin_port);
#if defined(SVCXPRT_XP_SOCK)
    xprt->xp_sock = sock;
#elif defined(SVCXPRT_XP_FD)
    xprt->xp_fd = sock;
#else
#error Unknown SVCXPRT socket field
#endif
    xprt_register(xprt);
    return (xprt);
}

SVCXPRT *
svcudp_create(sock)
     int sock;
{
    return(svcudp_bufcreate(sock, UDPMSGSIZE, UDPMSGSIZE));
}

static enum xprt_stat
svcudp_stat(xprt)
     SVCXPRT *xprt;
{
    return (XPRT_IDLE); 
}

#define USECS_PER_SEC 1000000
#define DBG_ALWAYS     0x0000 
extern void dprintf(int level, const char *fmt, ...);

static bool_t
svcudp_recv(xprt, msg)
     register SVCXPRT *xprt;
     struct rpc_msg *msg;
{
    register struct svcudp_data *su = su_data(xprt);
    register XDR *xdrs = &(su->su_xdrs);
    register int rlen;

    bool_t new_call;

  again:
    /* If this is not the first time we've been here. */
    if(TIMING_IN_CALL)
      {
	/* Finish recording this call and get the elapsed time. */
	TIMING_END_CALL(NULL);
      }
    xprt->xp_addrlen = sizeof(struct sockaddr_in);
    rlen = recvfrom(
#if defined(SVCXPRT_XP_SOCK)
		    xprt->xp_sock
#elif defined(SVCXPRT_XP_FD)
		    xprt->xp_fd
#else
#error Unknown SVCXPRT socket field
#endif
		    , rpc_buffer(xprt), (int) su->su_iosz,
		    0, (struct sockaddr *)&(xprt->xp_raddr),
		    &(xprt->xp_addrlen));
    if (rlen == -1 && errno == EINTR)
      goto again;
    if (rlen < 4*sizeof(u_int))
      return FALSE;
    xdrs->x_op = XDR_DECODE;
    XDR_SETPOS(xdrs, 0);
    if (! xdr_callmsg(xdrs, msg))
      return FALSE;
    /* Start recording the timing of this call. */
    TIMING_START_CALL(msg->rm_call.cb_proc);
    su->su_xid = msg->rm_xid;
    new_call = new_rpc(xprt, msg);
    TIMING_DUPE_STATUS(new_call);
    return new_call;
}

static bool_t
svcudp_reply(xprt, msg)
     register SVCXPRT *xprt; 
     struct rpc_msg *msg; 
{
    register struct svcudp_data *su = su_data(xprt);
    register XDR *xdrs = &(su->su_xdrs);
    register int slen;
    register bool_t stat = FALSE;

    xdrs->x_op = XDR_ENCODE;
    XDR_SETPOS(xdrs, 0);
    msg->rm_xid = su->su_xid;
    if (xdr_replymsg(xdrs, msg)) {
	slen = (int)XDR_GETPOS(xdrs);
	RECORD_TIME_POINT;
	if (sendto(
#if defined(SVCXPRT_XP_SOCK)
		   xprt->xp_sock
#elif defined(SVCXPRT_XP_FD)
		   xprt->xp_fd
#else
#error Unknown SVCXPRT socket field
#endif
		   , rpc_buffer(xprt), slen, 0,
		   (struct sockaddr *)&(xprt->xp_raddr), xprt->xp_addrlen)
	    == slen) {
	    stat = TRUE;
	    RECORD_TIME_POINT;
	    completed_rpc(xprt, msg, slen);
	    RECORD_TIME_POINT;
	}
    }
    return (stat);
}

static bool_t
svcudp_getargs(xprt, xdr_args, args_ptr)
     SVCXPRT *xprt;
     xdrproc_t xdr_args;
     caddr_t args_ptr;
{
    return ((*xdr_args)(&(su_data(xprt)->su_xdrs), (caddr_t*)args_ptr));
}

static bool_t
svcudp_freeargs(xprt, xdr_args, args_ptr)
     SVCXPRT *xprt;
     xdrproc_t xdr_args;
     caddr_t args_ptr;
{
    register XDR *xdrs = &(su_data(xprt)->su_xdrs);

    xdrs->x_op = XDR_FREE;
    return ((*xdr_args)(xdrs, (caddr_t*)args_ptr));
}

static void
svcudp_destroy(xprt)
     register SVCXPRT *xprt;
{
    register struct svcudp_data *su = su_data(xprt);

    xprt_unregister(xprt);
#if defined(SVCXPRT_XP_SOCK)
    (void)close(xprt->xp_sock);
#elif defined(SVCXPRT_XP_FD)
    (void)close(xprt->xp_fd);
#else
#error Unknown SVCXPRT socket field
#endif
    XDR_DESTROY(&(su->su_xdrs));
    mem_free(rpc_buffer(xprt), su->su_iosz);
    mem_free((caddr_t)su, sizeof(struct svcudp_data));
    mem_free((caddr_t)xprt, sizeof(SVCXPRT));
}
