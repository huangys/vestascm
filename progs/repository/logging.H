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
 * logging.H	Definitions for the logging functions.
 *
 * Authors:	Donald J. Becker, <becker@super.org>
 *		Rick Sladkey, <jrs@world.std.com>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This software maybe be used for any purpose provided
 *		the above copyright notice is retained.  It is supplied
 *		as is, with no warranty expressed or implied.
 */

#ifndef _LOGGING_H
#define _LOGGING_H

#include "nfsd.H"
#include "VRErrorCode.H"

/* Known debugLevel flags */
#define DBG_ALWAYS     0x00000  /* Always log this message */
#define DBG_SRPC       0x00001  /* SRPC - log when a client dies, etc. */
#define DBG_NFS        0x00002  /* NFS interface - log arguments and results. */
#define DBG_FDCACHE    0x00004  /* FdCache - log debugging messages. */
#define DBG_SIDDISP    0x00008  /* doDeletions - log disposition of each sid. */
#define DBG_SIDDEL     0x00010  /* doDeletions - log debugging messages. */
#define DBG_SIDNODEL   0x00020  /* doDeletions - suppress actual deletion. */
#define DBG_VDIREVAL   0x00040  /* VDirEvaluator - log debugging messages. */
#define DBG_NOWEEDREC  0x00080  /* do not try to recover an incomplete weed. */
#define DBG_VMEMPOOL   0x00100  /* VMemPool - log debugging messages. */
#define DBG_SAVECORE   0x00200  /* Startup - if a file named "core"
                                  exists in the current directory,
                                  rename it to core.DATE.TIME, where
                                  DATE.TIME are the current date and time. */
#define DBG_STALENFS   0x00400  /* NFS interface - log uses of stale handles. */
#define DBG_MASTERSHIP 0x00800  /* Mastership transfer - log progress, and
				  details of any errors */
#define DBG_COW        0x01000  /* File copy-on-write - log when it occurs. */
#define DBG_ACCESS     0x02000  /* Access control - log parsing of tables */
#define DBG_REPLICATION 0x04000 /* Details of any errors in replicate() */
#define DBG_GARDENER   0x08000  /* Details of the background thread
				  that deletes volatile directories of
				  defunct evaluators. */
#define DBG_SIDBLEASES 0x10000  /* shortid block leases (see
				   ShortIdImpl.C) - log blocks leased
				   and released. */

/* Global Function prototypes.  These are in a namespace because on
   some systems there's already a function named dprintf. */
namespace Repos
{
  /* Set debugging level flags.  The repository sets this from the -d flag
     on the command line, or from [Repository]debug_level.  See
     repository.8.mtex for debug levels currently in use.
  */
  extern void setDebugLevel(int level);

  /* True if level == 0 or (level & debugLevel) != 0 */
  extern int isDebugLevel(int level);

  extern "C"
  {
    /* Log debugging information to stderr if isDebugLevel(level) */
    extern void dprintf(int level, const char *fmt, ...);
  }

  extern void log_call(struct svc_req *rqstp,
		       struct dispatch_entry* dent,
		       union argument_types* argument);
  extern void log_result(struct svc_req *rqstp,
			 struct dispatch_entry* dent,
			 union result_types* result);
  extern void pr_void(char* buf);
  extern void pr_nfs_fh(char* buf, nfs_fh *argp);
  extern void pr_sattrargs(char* buf, sattrargs *argp);
  extern void pr_diropargs(char* buf, diropargs *argp);
  extern void pr_readargs(char* buf, readargs *argp);
  extern void pr_writeargs(char* buf, writeargs *argp);
  extern void pr_createargs(char* buf, createargs *argp);
  extern void pr_renameargs(char* buf, renameargs *argp);
  extern void pr_linkargs(char* buf, linkargs *argp);
  extern void pr_symlinkargs(char* buf, symlinkargs *argp);
  extern void pr_readdirargs(char* buf, readdirargs *argp);
  extern void pr_attrstat(char* buf, attrstat *argp);
  extern void pr_diropres(char* buf, diropres *argp);
  extern void pr_readlinkres(char* buf, readlinkres *argp);
  extern void pr_readres(char* buf, readres *argp);
  extern void pr_nfsstat(char* buf, nfsstat *argp);
  extern void pr_readdirres(char* buf, readdirres *argp);
  extern void pr_statfsres(char* buf, statfsres *argp);

  extern VRErrorCode::errorCode errno_to_errorCode(int err);
}

#endif
