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
// glue.H
// Last modified on Mon Nov 12 18:37:56 EST 2001 by ken@xorian.net
//      modified on Tue Sep 12 12:13:30 PDT 2000 by mann
//
// Glue code between nfsd.C and repository implementation
//

#include "VestaSource.H"

extern nfsstat do_getattr(nfs_fh* fh, fattr* attr,
			  AccessControl::Identity cred);
extern nfsstat do_setattr(sattrargs* argp, fattr* attr,
			  AccessControl::Identity cred);
extern nfsstat do_lookup(diropargs *dopa, diropokres* dp,
			 AccessControl::Identity cred);
extern nfsstat do_readlink(nfs_fh *fh, nfspath np,
			   AccessControl::Identity cred);
extern int fh_fd(nfs_fh* fh, nfsstat* status, int omode, VestaSource** vsout,
		 int* oflout, AccessControl::Identity cred);
extern nfsstat xlate_errno(int errno_val);
extern nfsstat any_fattr(fattr* attr, VestaSource* vs, int fd);
extern void fd_inactive(void* vsin, int fd, int ofl);
extern nfsstat do_create(createargs* argp, diropokres* dp,
			 AccessControl::Identity cred);
extern nfsstat do_remove(diropargs* argp, AccessControl::Identity cred);
extern nfsstat do_rename(renameargs* argp, AccessControl::Identity cred);
extern nfsstat do_hardlink(linkargs* argp, AccessControl::Identity cred);
extern nfsstat do_symlink(symlinkargs* argp, AccessControl::Identity cred);
extern nfsstat do_mkdir(createargs* argp, diropokres* dp,
			AccessControl::Identity cred);
extern nfsstat do_readdir(readdirargs* argp, result_types* resp,
			  AccessControl::Identity cred);
extern nfsstat do_statfs(nfs_fh* argp, result_types* resp,
			 AccessControl::Identity cred);


