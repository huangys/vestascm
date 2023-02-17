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
 * vnfsd.C     	This code handles RPC "NFS" data requests
 *              to the Vesta repository.
 *
 * Adapted for Vesta:
 *              Tim Mann, <mann@pa.dec.com>
 *
 * Authors of original user-space NFS server:
 *	        Mark A. Shand, May 1988
 *		Donald J. Becker, <becker@super.org>
 *		Rick Sladkey, <jrs@world.std.com>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Eric Kasten, <tigger@tigger.cl.msu.edu>
 *
 *		Copyright 1988 Mark A. Shand
 *		This software may be be used for any purpose provided
 *		the above copyright notice is retained.  It is supplied
 *		as is, with no warranty expressed or implied.
 */

#ifdef __linux__
// To get pthread_attr_setguardsize:
#define _XOPEN_SOURCE 500
// To keep _XOPEN_SOURCE from messing up Linux system headers:
#define _BSD_SOURCE
#endif

#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <pthread.h>
#include <Basics.H>
#include <assert.h>
#include "nfsd.H"
#include "logging.H"
#include "AccessControl.H"
#include "glue.H"

#include "timing.H"

#include "nfsStats.H"

#include "ReposConfig.h"

extern _PRO(void nfs_dispatch, (struct svc_req * rqstp, SVCXPRT * transp));
static _PRO(int makesock, (int port, int proto, int socksz));

extern void xdr_free();		/* fill this in later */

void
mallocfailed()
{
    Repos::dprintf(DBG_ALWAYS, "malloc failed -- exiting\n");
    exit(1);
}

/*
 * The "wrappers" of the following functions came from `rpcgen -l nfs_prot.x`.
 * This normally generates the client routines, but it provides nice
 * prototypes for the server routines also.
 */

int
nfsd_nfsproc_null_2(void *argp, union result_types *resp,
		    AccessControl::Identity cred)
{
    return 0;
}

int
nfsd_nfsproc_getattr_2(nfs_fh *argp, union result_types *resp,
		       AccessControl::Identity cred)
{
    return do_getattr(argp, &resp->r_attrstat.attrstat_u.attributes, cred);
}

int
nfsd_nfsproc_setattr_2(sattrargs *argp, union result_types *resp,
		       AccessControl::Identity cred)
{
    return do_setattr(argp, &resp->r_attrstat.attrstat_u.attributes, cred);
}

int
nfsd_nfsproc_root_2(void *argp, union result_types *resp,
		       AccessControl::Identity cred)
{
    return 0;
}

int
nfsd_nfsproc_lookup_2(diropargs *argp, union result_types *resp,
		      AccessControl::Identity cred)
{
    nfsstat status;
    diropokres *dp = &resp->r_diropres.diropres_u.diropres;
    status = do_lookup(argp, dp, cred);
    resp->r_diropres.status = status;
    return status;
}

int
nfsd_nfsproc_readlink_2(nfs_fh *argp, union result_types *resp,
			AccessControl::Identity cred)
{
    nfsstat status;
    status = do_readlink(argp, resp->r_readlinkres.readlinkres_u.data, cred);
    resp->r_readlinkres.status = status;
    return status;
}

int
nfsd_nfsproc_read_2(readargs *argp, union result_types *resp,
		    AccessControl::Identity cred)
{
    nfsstat status;
    int fd;
    VestaSource* vs;
    int ofl; /* really a FdCache::OFlag */

    RECORD_TIME_POINT;
    if ((fd = fh_fd(&(argp->file), &status, O_RDONLY, &vs, &ofl, cred))== -1) {
	return ((int) status);
    }
    if (fd == DEVICE_FAKE_FD) {
	resp->r_readres.readres_u.reply.data.data_len = 0;
	status = NFS_OK;
    } else {
      RECORD_TIME_POINT;
      errno = 0;
      resp->r_readres.readres_u.reply.data.data_len =
	pread(fd, resp->r_readres.readres_u.reply.data.data_val,
	      argp->count, (long) argp->offset);
      status = xlate_errno(errno);
    }
    if (status == NFS_OK) {
      RECORD_TIME_POINT;
	status = any_fattr(&resp->r_readres.readres_u.reply.attributes,
			   vs, fd);
    }
    RECORD_TIME_POINT;
    fd_inactive(vs, fd, ofl);
    RECORD_TIME_POINT;
    return status;
}

int
nfsd_nfsproc_writecache_2(void *argp, union result_types *resp,
			  AccessControl::Identity cred)
{
    return 0;
}

int
nfsd_nfsproc_write_2(writeargs *argp, union result_types *resp,
		       AccessControl::Identity cred)
{
    nfsstat status;
    int fd;
    VestaSource* vs;
    int ofl; /* really a FdCache::OFlag */

    RECORD_TIME_POINT;
    if ((fd = fh_fd(&(argp->file), &status, O_WRONLY, &vs, &ofl, cred))== -1) {
	return ((int) status);
    }
    if (fd == DEVICE_FAKE_FD) {
	status = NFS_OK;
    } else {
      RECORD_TIME_POINT;
      errno = 0;
      int errno_save = 0;
      if (pwrite(fd, argp->data.data_val, argp->data.data_len, (long) argp->offset) !=
	  argp->data.data_len) {
	errno_save = errno;
	Text emsg = Basics::errno_Text(errno_save);
	Repos::dprintf(DBG_ALWAYS, "NFS write failure to sid 0x%08x:"
			" %s (errno = %d)\n", vs->shortId(),
		       emsg.cchars(), errno_save);
      }
      status = xlate_errno(errno_save);
    }
    if (status == NFS_OK) {
      RECORD_TIME_POINT;
	status = any_fattr(&resp->r_attrstat.attrstat_u.attributes,
			   vs, fd);
    }
    RECORD_TIME_POINT;
    fd_inactive(vs, fd, ofl);
    RECORD_TIME_POINT;
    return status;
}

int
nfsd_nfsproc_create_2(createargs *argp, union result_types *resp,
		       AccessControl::Identity cred)
{
    nfsstat status;
    status = do_create(argp, &resp->r_diropres.diropres_u.diropres, cred);
    resp->r_diropres.status = status;
    return status;
}

int
nfsd_nfsproc_remove_2(diropargs *argp, union result_types *resp,
		       AccessControl::Identity cred)
{
    nfsstat status;
    status = do_remove(argp, cred);
    resp->r_nfsstat = status;
    return status;
}

int
nfsd_nfsproc_rename_2(renameargs *argp, union result_types *resp,
		       AccessControl::Identity cred)
{
    nfsstat status;
    status = do_rename(argp, cred);
    resp->r_nfsstat = status;
    return status;
}

int
nfsd_nfsproc_link_2(linkargs *argp, union result_types *resp,
		       AccessControl::Identity cred)
{
    nfsstat status;
    status = do_hardlink(argp, cred);
    resp->r_nfsstat = status;
    return status;
}

int
nfsd_nfsproc_symlink_2(symlinkargs *argp, union result_types *resp,
		       AccessControl::Identity cred)
{
    nfsstat status;
    status = do_symlink(argp, cred);
    resp->r_nfsstat = status;
    return status;
}

int
nfsd_nfsproc_mkdir_2(createargs *argp, union result_types *resp,
		       AccessControl::Identity cred)
{
    nfsstat status;
    status = do_mkdir(argp, &resp->r_diropres.diropres_u.diropres, cred);
    resp->r_diropres.status = status;
    return status;
}

int
nfsd_nfsproc_rmdir_2(diropargs *argp, union result_types *resp,
		       AccessControl::Identity cred)
{
    nfsstat status;
    status = do_remove(argp, cred);
    resp->r_nfsstat = status;
    return status;
}

int
nfsd_nfsproc_readdir_2(readdirargs *argp, union result_types *resp,
		       AccessControl::Identity cred)
{
    return do_readdir(argp, resp, cred);
}

int
nfsd_nfsproc_statfs_2(nfs_fh *argp, union result_types *resp, 
		       AccessControl::Identity cred)
{
    return do_statfs(argp, resp, cred);
}

static int
makesock(int port, int proto, int socksz)
{
    struct sockaddr_in sin;
    int s;
    int sock_type;

    sock_type = (proto == IPPROTO_UDP) ? SOCK_DGRAM : SOCK_STREAM;
    s = socket(AF_INET, sock_type, proto);
    if (s < 0) {
      int saved_errno = errno;
      Text etxt = Basics::errno_Text(saved_errno);
      Repos::dprintf(DBG_ALWAYS, "Could not make a socket: %s\n", etxt.cchars());
      return (-1);
    }
    memset((char *) &sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

    {
	int val = 1;

	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0)
	  {
	    int saved_errno = errno;
	    Text etxt = Basics::errno_Text(saved_errno);
	    Repos::dprintf(DBG_ALWAYS, "setsockopt failed: %s\n", etxt.cchars());
	  }
    }

#ifdef SO_SNDBUF
    {
	int sblen, rblen;

	sblen = rblen = socksz;

	if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, &sblen, sizeof sblen) < 0)
	  {
	    int saved_errno = errno;
	    Text etxt = Basics::errno_Text(saved_errno);
	    Repos::dprintf(DBG_ALWAYS, "setsockopt(SO_SNDBUF, %d) failed: %s\n",
			   sblen, etxt.cchars());
	  }

	if(setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rblen, sizeof rblen) < 0)
	  {
	    int saved_errno = errno;
	    Text etxt = Basics::errno_Text(saved_errno);
	    Repos::dprintf(DBG_ALWAYS, "setsockopt(SO_RCVBUF, %d) failed: %s\n",
			   rblen, etxt.cchars());
	  }
    }
#endif /* SO_SNDBUF */

    if (bind(s, (struct sockaddr *) &sin, sizeof(sin)) == -1) {
      int saved_errno = errno;
      Text etxt = Basics::errno_Text(saved_errno);
      Repos::dprintf(DBG_ALWAYS,
		     "Could not bind name to socket: %s\n", etxt.cchars());
      return (-1);
    }
    return (s);
}

int nfs_port = NFS_PORT;
char *MyNFSSocket = "";

#define RQCRED_SIZE 480

#if defined(SVC_RECV_WORKS_WITH_TYPE_FIX) && !defined(SVC_RECV_WORKS_AS_IS)
typedef bool_t (*__xp_recv_t)(SVCXPRT *, struct rpc_msg *);
#define SVC_RECV(xprt, msg) (*((__xp_recv_t) (xprt)->xp_ops->xp_recv))((xprt), (msg))
#endif

void *
nfsd_thread(void *arg)
{
  int nfs_socket = (int)(long) arg;
  SVCXPRT *xprt;

  signal(SIGPIPE, SIG_IGN);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGSEGV, SIG_DFL);
  signal(SIGABRT, SIG_DFL);
  signal(SIGBUS, SIG_DFL);
  signal(SIGILL, SIG_DFL);

  xprt = svcudp_create(nfs_socket);
  assert(xprt != NULL);

  NFS_Call_Stats my_stats;

#if defined(REPOS_PERF_DEBUG)
  Timing_Recorder *my_recorder = NEW(Timing_Recorder);
#endif

  for (;;) {
    enum xprt_stat stat;
    struct rpc_msg msg;
    struct svc_req r;
    char cred_area[2*MAX_AUTH_BYTES + RQCRED_SIZE];
    enum auth_stat why;

    msg.rm_call.cb_cred.oa_base = cred_area;
    msg.rm_call.cb_verf.oa_base = &(cred_area[MAX_AUTH_BYTES]);
    r.rq_clntcred = &(cred_area[2*MAX_AUTH_BYTES]);

    if (SVC_RECV(xprt, &msg)) {
      NFS_Call_Stats::Helper stat_recorder(my_stats);

      r.rq_xprt = xprt;
      r.rq_prog = msg.rm_call.cb_prog;
      r.rq_vers = msg.rm_call.cb_vers;
      r.rq_proc = msg.rm_call.cb_proc;
      r.rq_cred = msg.rm_call.cb_cred;

      /* Authenticate */
//       if ((why =
// #if defined(HAVE__AUTHENTICATE)
// 	   _authenticate(&r, &msg)
// #elif defined(HAVE___AUTHENTICATE)
// 	   __authenticate(&r, &msg)
// #else
// # error Need server-side RPC authentication function
// #endif
// 	   ) != AUTH_OK) {
// 	svcerr_auth(xprt, why);
// 	continue;
//       }
      RECORD_TIME_POINT;

      /* Check this is really an NFS call */
      if (r.rq_prog != NFS_PROGRAM) {
	svcerr_noprog(xprt);
	continue;
      }
      if (r.rq_vers != NFS_VERSION) {

#if defined(__osf__)
	svcerr_progvers(xprt);
#else
	/* extra two args are documented in svc.h
	   but not in man page */
	svcerr_progvers(xprt, NFS_VERSION, NFS_VERSION);
#endif
	continue;
      }

      /* Do it */
      nfs_dispatch(&r, xprt);
    }
  }
  //return NULL; /* not reached */
}

static Basics::thread *nfsd_threads = NULL;

void 
nfsd_init(int nfs_threads)
{
    int nfs_socket;
    struct sockaddr_in saddr;
    int addr_size, socksz;
    static char namebuf[MAXHOSTNAMELEN + 32];
    char *p;
    int i;
    Basics::thread_attr nfsda;

    /* Set MyNFSSocket for use in other modules */
    gethostname(namebuf, sizeof(namebuf));
    p = &namebuf[strlen(namebuf)];
    sprintf(p, ":%d", nfs_port);
    MyNFSSocket = namebuf;

    nfs_socket = 0;
    socksz = nfs_bufreqs * (NFS_MAXDATA + 1024);
    if ((nfs_socket = makesock(nfs_port, IPPROTO_UDP, socksz)) < 0) {
      int saved_errno = errno;
      Text etxt = Basics::errno_Text(saved_errno);
      fprintf(stderr,
	      "nfsd: could not make a UDP socket: %s\n",
	      etxt.cchars());
      exit(1);
    }

    if (nfs_threads == 0) nfs_threads = 1;

    nfsda.set_stacksize(256000L); /* guess */

#if defined (_POSIX_THREAD_PRIORITY_SCHEDULING) && defined(PTHREAD_SCHED_RR_OK) && !defined(__linux__)
    // Linux only allows the superuser to use SCHED_RR
    nfsda.set_schedpolicy(SCHED_RR);
    nfsda.set_inheritsched(PTHREAD_EXPLICIT_SCHED);
    nfsda.set_sched_priority(sched_get_priority_min(SCHED_RR));
#endif

    /* Attempt to work around bug in DEC threads that causes the
       nfsd threads to hang at times.  It didn't help, but I'm
       leaving it in anyway, since all these threads block in the
       kernel a lot. */
    // pthread_attr_setscope(&nfsda, PTHREAD_SCOPE_SYSTEM);

    nfsd_threads = NEW_PTRFREE_ARRAY(Basics::thread, nfs_threads);
    for (i=0; i<nfs_threads; i++) {
	/* There should be no reason to dup nfs_socket, but doing
	   so *does* seem to work around the bug in DEC threads that
	   otherwise makes all the nfsd threads hang at times.  (This
	   hang does not occur when we link with the Mike Burrows thread
	   library.)
	   */
      nfsd_threads[i].fork(nfsd_thread,
			   reinterpret_cast<void *>(dup(nfs_socket)),
			   nfsda);
    }
}

