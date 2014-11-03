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
 * dispatch.C	This file contains the function dispatch table.
 *
 * Adapted for Vesta:
 *              Tim Mann, <mann@pa.dec.com>
 *
 * Authors of original user-space NFS server:
 *              Donald J. Becker, <becker@super.org>
 *		Rick Sladkey, <jrs@world.std.com>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This software maybe be used for any purpose provided
 *		the above copyright notice is retained.  It is supplied
 *		as is, with no warranty expressed or implied.
 */

#include <Basics.H>
#include <pthread.h>
#include "logging.H"
#include "nfsd.H"
#include "AccessControl.H"

#include "timing.H"

#include "ReposConfig.h"

/* Allow the following to be over-ridden at compile time. */

#ifndef ROOT_UID
#define ROOT_UID	0		/* Root user's ID. */
#endif

#ifndef NOBODY_UID
#define NOBODY_UID	((uid_t) 65534)	/* The unprivileged user. */
#endif

#ifndef NOBODY_GID
#define NOBODY_GID	((gid_t) 65534)	/* The unprivileged group. */
#endif

struct authunix_parms nobody_cred =
    { 0, "", NOBODY_UID, NOBODY_GID, 0, NULL };

/*
 * This is a dispatch table to simplify error checking,
 * and supply return attributes for NFS functions.
 */

#define CONCAT(a,b)	a##b
#define CONCAT3(a,b,c)	a##b##c
#define STRING(a)	#a

#define table_ent(auth, ro, cred, res_type, arg_type, funct) {	    \
	auth, ro, cred, sizeof(res_type), sizeof(arg_type),	    \
	(xdrproc_t)CONCAT(xdr_,res_type),                           \
        (xdrproc_t)CONCAT(xdr_,arg_type),		            \
	(funct_proc)CONCAT3(nfsd_nfsproc_,funct,_2), STRING(funct), \
	(print_proc)CONCAT(Repos::pr_,arg_type),                           \
        (print_res_proc)CONCAT(Repos::pr_,res_type)                        \
}

#define nil	char
#define xdr_nil xdr_void
#define pr_nil  pr_void
#define pr_char pr_void

struct dispatch_entry dtable[] = {
	table_ent(0,0,0,nil,nil,null),
	table_ent(1,0,1,attrstat,nfs_fh,getattr),
	table_ent(1,1,1,attrstat,sattrargs,setattr),
	table_ent(0,0,0,nil,nil,root),
	table_ent(1,0,1,diropres,diropargs,lookup),
	table_ent(1,0,1,readlinkres,nfs_fh,readlink),
	table_ent(1,0,1,readres,readargs,read),
	table_ent(0,0,0,nil,nil,writecache),
	table_ent(1,1,1,attrstat,writeargs,write),
	table_ent(1,1,1,diropres,createargs,create),
	table_ent(1,1,1,nfsstat,diropargs,remove),
	table_ent(1,1,1,nfsstat,renameargs,rename),
	table_ent(1,1,1,nfsstat,linkargs,link),
	table_ent(1,1,1,nfsstat,symlinkargs,symlink),
	table_ent(1,1,1,diropres,createargs,mkdir),
	table_ent(1,1,1,nfsstat,diropargs,rmdir),
	table_ent(1,0,1,readdirres,readdirargs,readdir),
	table_ent(1,0,0,statfsres,nfs_fh,statfs),
};

#if defined(SVC_GETARGS_WORKS_WITH_TYPE_FIX) && !defined(SVC_GETARGS_WORKS_AS_IS)
typedef bool_t (*__xp_getargs_t)(SVCXPRT *, xdrproc_t, void *);
#define svc_getargs(xprt, xargs, argsp) \
  (*((__xp_getargs_t) (xprt)->xp_ops->xp_getargs))((xprt), (xargs), (argsp))
#endif

#if defined(SVC_FREEARGS_WORKS_WITH_TYPE_FIX) && !defined(SVC_FREEARGS_WORKS_AS_IS)
typedef bool_t (*__xp_freeargs_t)(SVCXPRT *, xdrproc_t, void *);
#define svc_freeargs(xprt, xargs, argsp) \
  (*((__xp_freeargs_t) (xprt)->xp_ops->xp_freeargs))((xprt), (xargs), (argsp))
#endif

void nfs_dispatch(struct svc_req *rqstp, SVCXPRT *transp)
{
    union argument_types argument;
    union result_types result;
    char iobuf[NFS_MAXDATA]; 
    struct authunix_parms *unix_cred;
    /* assert(NFS_MAXDATA >= NFS_MAXPATHLEN + 1); */

    unsigned int proc_index = rqstp->rq_proc;
    struct dispatch_entry *dent;

    if (proc_index >= (sizeof(dtable) / sizeof(dtable[0]))) {
	svcerr_noproc(transp);
	return;
    }
    dent = &dtable[proc_index];

    memset(&argument, 0, dent->arg_size);
    RECORD_TIME_POINT;
    if (!svc_getargs(transp, (xdrproc_t)dent->xdr_argument,
		     (char*)&argument)) {
	svcerr_decode(transp);
	return;
    }
    RECORD_TIME_POINT;
    /* Clear the result structure. */
    memset(&result, 0, dent->res_size);

    /* Ugly special case code: Provide a stack-allocated buffer for
       routines that return large data. */ 
    if (proc_index == NFSPROC_READ) {
	result.r_readres.readres_u.reply.data.data_val = iobuf;
    } else if (proc_index == NFSPROC_READLINK) {
	result.r_readlinkres.readlinkres_u.data = iobuf;
    }

    /* Log the call. */
    Repos::log_call(rqstp, dent, &argument);

    RECORD_TIME_POINT;

    if (rqstp->rq_cred.oa_flavor == AUTH_UNIX) {
	unix_cred = (struct authunix_parms *) rqstp->rq_clntcred;
    } else {
        // !!Need code here to support GSSAPI credentials
	unix_cred = &nobody_cred;
    }
    AccessControl::UnixIdentityRep cred(unix_cred,
					(struct sockaddr_in*) &rqstp->rq_xprt->xp_raddr,
					false);

    RECORD_TIME_POINT;

    if (AccessControl::admit(&cred) || !dent->authenticate) {
      if(dent->authenticate)
	cred.validate();
      /* Do the function call itself. */
      result.r_nfsstat = (nfsstat) (*dent->funct)(&argument, &result, &cred);
    } else {
      // Kernel NFS servers return NFSERR_STALE for this case.  That
      // makes sense because their clients go through the mount daemon
      // first, which is responsible for denying permission if the
      // client is not authorized.  The repository doesn't use the
      // mount daemon, so it seems better to return a more
      // user-understandable error code here.

      Repos::dprintf(DBG_NFS, "unauthorized client, returning NFSERR_ACCES\n");
      result.r_nfsstat = NFSERR_ACCES;
    }

    /* Log the result. */
    Repos::log_result(rqstp, dent, &result);

    RECORD_TIME_POINT;

    if (!svc_sendreply(transp, (xdrproc_t)dent->xdr_result, (char*)&result)) {
	svcerr_systemerr(transp);
    }

    RECORD_TIME_POINT;

    svc_freeargs(transp, (xdrproc_t)dent->xdr_argument, (char*) &argument);

    RECORD_TIME_POINT;

    /* More ugly special case code: Free the reply only if needed */
    if (proc_index == NFSPROC_READDIR) {
	xdr_free((xdrproc_t)xdr_readdirres, (char*)&result.r_readdirres);
    }
}
