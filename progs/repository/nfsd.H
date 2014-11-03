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

// Last modified on Wed Dec  8 01:35:23 EST 2004 by ken@xorian.net

/*
 * nfsd.H	This program implements a user-space NFS server.
 *
 * Authors:	Mark A. Shand, May 1988
 *		Rick Sladkey, <jrs@world.std.com>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		Copyright 1988 Mark A. Shand
 *		This software maybe be used for any purpose provided
 *		the above copyright notice is retained.  It is supplied
 *		as is, with no warranty expressed or implied.
 */

#ifndef _NFSD_H
#define _NFSD_H

#include "system.h"
extern "C" {
#include "nfs_prot.h"
}
#include "AccessControl.H"

#if defined(__STDC__) || defined(__cplusplus)
#   define _PRO(f, a)	f a
#else
#   define _PRO(f, a)	f ()
#endif

union argument_types {
	nfs_fh			nfsproc_getattr_2_arg;
	sattrargs		nfsproc_setattr_2_arg;
	diropargs		nfsproc_lookup_2_arg;
	nfs_fh			nfsproc_readlink_2_arg;
	readargs		nfsproc_read_2_arg;
	writeargs		nfsproc_write_2_arg;
	createargs		nfsproc_create_2_arg;
	diropargs		nfsproc_remove_2_arg;
	renameargs		nfsproc_rename_2_arg;
	linkargs		nfsproc_link_2_arg;
	symlinkargs		nfsproc_symlink_2_arg;
	createargs		nfsproc_mkdir_2_arg;
	diropargs		nfsproc_rmdir_2_arg;
	readdirargs		nfsproc_readdir_2_arg;
	nfs_fh			nfsproc_statfs_2_arg;
};

union result_types {
	attrstat		r_attrstat;
	diropres		r_diropres;
	readlinkres		r_readlinkres;
	readres			r_readres;
	nfsstat			r_nfsstat;
	readdirres		r_readdirres;
	statfsres		r_statfsres;
};

typedef int (*funct_proc)(union argument_types*, union result_types*,
			  AccessControl::Identity);
typedef void (*print_proc)(char* buf, union argument_types *argp);
typedef void (*print_res_proc)(char* buf, union result_types *argp);

struct dispatch_entry {
        int authenticate;	/* 1 if op requires access checking */
	int read_only;		/* 1 if op forbidden on RO FS */
	int credentials;	/* 1 if credentials needed */
	int res_size, arg_size;	/* sizeof the res/arg structs */
	xdrproc_t xdr_result;
	xdrproc_t xdr_argument;
	funct_proc funct;
	char *name;		/* name of function */
	print_proc log_print;
	print_res_proc log_print_res;
};

extern struct dispatch_entry dtable[];

/* Imported global variables. */
extern uid_t fake_uid;
extern gid_t fake_gid;
extern int nfs_bufreqs; /* # of NFS requests to fit in socket buffer */

/* Global Function prototypes. */
extern _PRO( void mallocfailed, (void) );
extern _PRO( nfsstat getattr, (nfs_fh *fh, fattr *attr,
			       struct stat *stat_optimize) );
extern _PRO( void nfsd_init, (int) );

#define DEVICE_FAKE_FD -2

extern int nfsd_nfsproc_null_2(void* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_getattr_2(nfs_fh* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_setattr_2(sattrargs* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_root_2(void* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_lookup_2(diropargs* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_readlink_2(nfs_fh* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_read_2(readargs* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_writecache_2(void* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_write_2(writeargs* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_create_2(createargs* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_remove_2(diropargs* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_rename_2(renameargs* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_link_2(linkargs* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_symlink_2(symlinkargs* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_mkdir_2(createargs* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_rmdir_2(diropargs* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_readdir_2(readdirargs* argp, union result_types* resp, 
    AccessControl::Identity cred);
extern int nfsd_nfsproc_statfs_2(nfs_fh* argp, union result_types* resp, 
    AccessControl::Identity cred);

/* End of nfsd.h. */

#endif
