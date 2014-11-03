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
 * logging.C	This module handles error and debug info logging.
 *
 * Rewritten for Vesta; not much is left of the original, and those
 *  are the bad parts.  By the way, what copyright notice?  Listing
 *  authors is not a copyright notice. --Tim Mann <mann@pa.dec.com>
 * 
 * Original Authors:	Donald J. Becker, <becker@super.org>
 *		Rick Sladkey, <jrs@world.std.com>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This software may be used for any purpose provided
 *		the above copyright notice is retained.  It is supplied
 *		as is, with no warranty expressed or implied.
 */

#include <Basics.H>
#include <pthread.h>
#include "nfsd.H"
#include "logging.H"

#define LOG_FILE  "%s.log"

pthread_mutex_t dp_mutex;
static pthread_once_t dp_once = PTHREAD_ONCE_INIT;

static char printbuf[1024];		/* local print buffer		*/
int debugLevel = 0;		        /* enable/disable DEBUG logs	*/
static char log_name[32];		/* name of this program		*/
static FILE *log_fp = (FILE *)NULL;	/* fp for the log file		*/

extern "C"
{
  static void
  dp_init()
  {
    pthread_mutex_init(&dp_mutex, (pthread_mutexattr_t *)NULL);
  }
}

void Repos::setDebugLevel(int level)
{
    debugLevel = level;
}

int Repos::isDebugLevel(int level)
{
    return level == 0 || ((debugLevel & level) != 0);
}

/* Log debugging information to stderr.  It will be printed if 
   level == 0 or if (level & debugLevel) != 0, where debugLevel
   is the argument to -d on the repository command line.
*/
void Repos::dprintf(int level, const char *fmt, ...)
{
    va_list args;
    time_t now;
    struct tm *tm;

    if (level == 0 || (level & debugLevel)) {
	pthread_once(&dp_once, dp_init);
	pthread_mutex_lock(&dp_mutex);

	(void) time(&now);
	tm = localtime(&now);
	fprintf(stderr, "%s %02d/%02d/%04d %02d:%02d:%02d ",
		log_name, tm->tm_mon + 1, tm->tm_mday, tm->tm_year+1900,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	pthread_mutex_unlock(&dp_mutex);
    }	    
}

/* Log an incoming call */
void
Repos::log_call(struct svc_req *rqstp, struct dispatch_entry* dent,
		union argument_types* argument)
{
    char buff[2048];
    register char *sp;
    int i;

    if (!isDebugLevel(DBG_NFS)) return;

    sp = buff;
    sprintf(sp, "%s\n  [%d ", dent->name, rqstp->rq_cred.oa_flavor);
    sp += strlen(sp);
    if (rqstp->rq_cred.oa_flavor == AUTH_UNIX) {
	struct authunix_parms *unix_cred;
	struct tm stm;

	unix_cred = (struct authunix_parms *) rqstp->rq_clntcred;
#if defined(__digital__) && defined(__DECCXX_VER)
# if __DECCXX_VER < 60290033
	// Workaround for broken macro for localtime_r in the classic
	// Tru64 std_env.
#  undef localtime_r
# endif
#endif
	localtime_r((time_t*) &unix_cred->aup_time, &stm);
	sprintf(sp, "%d/%d/%d %02d:%02d:%02d %s %d.%d",
		stm.tm_year+1900, stm.tm_mon + 1, stm.tm_mday,
		stm.tm_hour, stm.tm_min, stm.tm_sec,
		unix_cred->aup_machname,
		unix_cred->aup_uid,
		unix_cred->aup_gid);
	sp += strlen(sp);
	if ((int) unix_cred->aup_len > 0) {
	    sprintf(sp, "+%d", unix_cred->aup_gids[0]);
	    sp += strlen(sp);
	    for (i = 1; i < unix_cred->aup_len; i++) {
		sprintf(sp, ",%d", unix_cred->aup_gids[i]);
		sp += strlen(sp);
	    }
	}
    }
    strcpy(sp, "]\n  ");
    sp += strlen(sp);
    dent->log_print(sp, argument);
    dprintf(DBG_NFS, "%s\n", buff);
}

/* Log a call's result */
void Repos::log_result(struct svc_req *rqstp,
		       struct dispatch_entry* dent,
		       union result_types* result)
{
    char buf[2048];
    if (!isDebugLevel(DBG_NFS)) return;
    dent->log_print_res(buf, result);
    dprintf(DBG_NFS, "%s\n", buf);
}

void Repos::pr_void(char* buf)
{
    buf[0] = '\000';
}

void Repos::pr_sattrargs(char* buf, sattrargs *argp)
{
    pr_nfs_fh(buf, &argp->file);
    buf += strlen(buf);
    sprintf(buf, " m:%0o u/g:%d/%d size:%d atime:%#x.%#x mtime:%#x.%#x",
	    argp->attributes.mode,
	    argp->attributes.uid, argp->attributes.gid,
	    argp->attributes.size,
	    argp->attributes.atime.seconds, argp->attributes.atime.useconds,
	    argp->attributes.mtime.seconds, argp->attributes.mtime.useconds);
}

void Repos::pr_diropargs(char* buf, diropargs *argp)
{
    pr_nfs_fh(buf, &(argp->dir));
    buf += strlen(buf);
    sprintf(buf, " n:%s", argp->name);
}

void Repos::pr_readargs(char* buf, readargs *argp)
{
    pr_nfs_fh(buf, &(argp->file));
    buf += strlen(buf);
    sprintf(buf, ": %d bytes at %d", argp->count, argp->offset);
}

void Repos::pr_writeargs(char* buf, writeargs *argp)
{
    pr_nfs_fh(buf, &(argp->file));
    buf += strlen(buf);
    sprintf(buf, ": %d bytes at %d", argp->data.data_len, argp->offset);
}

void Repos::pr_createargs(char* buf, createargs *argp)
{
    pr_nfs_fh(buf, &(argp->where.dir));
    buf += strlen(buf);
    sprintf(buf, " n:%s m:%0o u/g:%d/%d size:%d atime:%#x.%#x mtime:%#x.%#x",
	    argp->where.name,
	    argp->attributes.mode, argp->attributes.uid,
	    argp->attributes.gid, argp->attributes.size,
	    argp->attributes.atime.seconds, argp->attributes.atime.useconds,
	    argp->attributes.mtime.seconds, argp->attributes.mtime.useconds);
}

void Repos::pr_renameargs(char* buf, renameargs *argp)
{
    strcpy(buf, "from:");
    buf += strlen(buf);
    pr_nfs_fh(buf, &(argp->from.dir));
    strcat(buf, " n:");
    strcat(buf, argp->from.name);
    strcat(buf, " to:");
    buf += strlen(buf);
    pr_nfs_fh(buf, &(argp->to.dir));
    strcat(buf, " n:");
    strcat(buf, argp->to.name);
}

void Repos::pr_linkargs(char *buf, linkargs *argp)
{
    strcpy(buf, "unimplemented");
}

void Repos::pr_symlinkargs(char *buf, symlinkargs *argp)
{
    strcpy(buf, "unimplemented");
}

void Repos::pr_readdirargs(char* buf, readdirargs *argp)
{
    char digits[64];
    int i;
    pr_nfs_fh(buf, &argp->dir);
    strcat(buf, " cookie:");
    for (i=0; i<NFS_COOKIESIZE; i++) {
	sprintf(digits, "%02x", argp->cookie[i]);
	strcat(buf, digits);
    }
    sprintf(digits, " count:%u", argp->count);
    strcat(buf, digits);
}

static void pr_fattr(char* buf, fattr *argp)
{
    sprintf(buf,
	    "t:%d m:%0o n:%d u/g:%d/%d\n"
	    "  sz:%d bsz:%d bks:%d rdev:%d fsid:%d fid:%#x\n"
	    "  atime:%#x mtime:%#x ctime:%#x",
	    argp->type,
	    argp->mode,
	    argp->nlink,
	    argp->uid,
	    argp->gid,
	    argp->size,
	    argp->blocksize,
	    argp->blocks,
	    argp->rdev,
	    argp->fsid,
	    argp->fileid,
	    argp->atime,
	    argp->mtime,
	    argp->ctime);
}

void Repos::pr_attrstat(char* buf, attrstat *argp)
{
    if (argp->status != NFS_OK) {
	sprintf(buf, "error:%d", argp->status);
    } else {
	pr_fattr(buf, &argp->attrstat_u.attributes);
    }
}

void Repos::pr_diropres(char* buf, diropres *argp)
{
    if (argp->status != NFS_OK) {
	sprintf(buf, "error:%d", argp->status);
    } else {
	strcpy(buf, "fh:");
	buf += strlen(buf);
	pr_nfs_fh(buf, &argp->diropres_u.diropres.file);
	strcat(buf, " ");
	buf += strlen(buf);
	pr_fattr(buf, &argp->diropres_u.diropres.attributes);
    }
}

void Repos::pr_readlinkres(char* buf, readlinkres *argp)
{
    if (argp->status != NFS_OK) {
	sprintf(buf, "error:%d", argp->status);
    } else {
	sprintf(buf, "ok");
    }
}

void Repos::pr_readres(char* buf, readres *argp)
{
    if (argp->status != NFS_OK) {
	sprintf(buf, "error:%d", argp->status);
    } else {
	sprintf(buf, "len:%d ",
                argp->readres_u.reply.data.data_len);
	buf += strlen(buf);
	pr_fattr(buf, &argp->readres_u.reply.attributes);
    }
}

void Repos::pr_nfsstat(char* buf, nfsstat *argp)
{
    if (*argp != NFS_OK) {
	sprintf(printbuf, "error:%d", *argp);
    } else {
	sprintf(buf, "ok");
    }
}

void Repos::pr_readdirres(char* buf, readdirres *argp)
{
    if (argp->status != NFS_OK) {
	sprintf(buf, "error:%d", argp->status);
    } else {
	sprintf(buf, "ok");
    }
}

void Repos::pr_statfsres(char* buf, statfsres *argp)
{
    if (argp->status != NFS_OK) {
	sprintf(buf, "error:%d", argp->status);
    } else {
	sprintf(buf, "ok");
    }
}

void Repos::pr_nfs_fh(char* buf, nfs_fh *fh)
{
    unsigned char *p = (unsigned char *) fh;
    int i, len;

    if (p[0] == 0 && p[1] == 4) {
	len = 24;
    } else {
	len = strlen((char*)&p[1]) + 1;
    }
    for (i = 0; i < len; i++) {
	sprintf(&buf[i*2], "%02x", p[i]);
    }
}

VRErrorCode::errorCode
Repos::errno_to_errorCode(int err)
{
    switch (err) {
      case 0:
	return VRErrorCode::ok;
      case EACCES:
      case EPERM:
      case EROFS:
	return VRErrorCode::noPermission;
      case EDQUOT:
      case EFBIG:
      case ENOSPC:
	return VRErrorCode::outOfSpace;
      case EISDIR:
	return VRErrorCode::isADirectory;
      case ENOTDIR:
	return VRErrorCode::notADirectory;
      case ENAMETOOLONG:
	return VRErrorCode::nameTooLong;
      case ENOENT:
	return VRErrorCode::notFound;
      case EINVAL:
	return VRErrorCode::invalidArgs;
      default:
	Text msg = Basics::errno_Text(err);
	dprintf(DBG_ALWAYS,
		"errno_to_errorCode: unexpected errno %d: %s\n",
		err, msg.cchars());
	return VRErrorCode::invalidArgs;
    }
}
