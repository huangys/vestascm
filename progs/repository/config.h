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

/* config.h.  Generated automatically by configure.  */
/* config.h.in.  Generated automatically from configure.in by autoheader.  */

/* Define if on AIX 3.
   System headers sometimes define this.
   We just want to avoid a redefinition error message.  */
#ifndef _ALL_SOURCE
/* #undef _ALL_SOURCE */
#endif

/* Define if using alloca.c.  */
/* #undef C_ALLOCA */

/* Define to empty if the keyword does not work.  */
/* #undef const */

/* Define to one of _getb67, GETB67, getb67 for Cray-2 and Cray-YMP systems.
   This function is required for alloca.c support on those systems.  */
/* #undef CRAY_STACKSEG_END */

/* Define if you have dirent.h.  */
#define DIRENT 1

/* Define to the type of elements in the array set by `getgroups'.
   Usually this is either `int' or `gid_t'.  */
#define GETGROUPS_T gid_t

/* Define to `int' if <sys/types.h> doesn't define.  */
/* #undef gid_t */

/* Define if you have alloca.h and it should be used (not Ultrix).  */
#define HAVE_ALLOCA_H 1

/* Define if you don't have vprintf but do have _doprnt.  */
/* #undef HAVE_DOPRNT */

/* Define if your struct stat has st_blksize.  */
#define HAVE_ST_BLKSIZE 1

/* Define if your struct stat has st_blocks.  */
#define HAVE_ST_BLOCKS 1

/* Define if your struct stat has st_rdev.  */
#define HAVE_ST_RDEV 1

/* Define if utime(file, NULL) sets file's timestamp to the present.  */
#define HAVE_UTIME_NULL 1

/* Define if you have the vprintf function.  */
#define HAVE_VPRINTF 1

/* Define if major, minor, and makedev are declared in mkdev.h.  */
/* #undef MAJOR_IN_MKDEV */

/* Define if major, minor, and makedev are declared in sysmacros.h.  */
/* #undef MAJOR_IN_SYSMACROS */

/* Define if on MINIX.  */
/* #undef _MINIX */

/* Define to `int' if <sys/types.h> doesn't define.  */
/* #undef mode_t */

/* Define if you don't have dirent.h, but have ndir.h.  */
/* #undef NDIR */

/* Define if the system does not provide POSIX.1 features except
   with this defined.  */
/* #undef _POSIX_1_SOURCE */

/* Define if you need to in order for stat and other things to work.  */
/* #undef _POSIX_SOURCE */

/* Define as the return type of signal handlers (int or void).  */
#define RETSIGTYPE void

/* If using the C implementation of alloca, define if you know the
   direction of stack growth for your system; otherwise it will be
   automatically deduced at run-time.
	STACK_DIRECTION > 0 => grows toward higher addresses
	STACK_DIRECTION < 0 => grows toward lower addresses
	STACK_DIRECTION = 0 => direction of growth unknown
 */
/* #undef STACK_DIRECTION */

/* Define if the `S_IS*' macros in <sys/stat.h> do not work properly.  */
/* #undef STAT_MACROS_BROKEN */

/* Define if you have the ANSI C header files.  */
#define STDC_HEADERS 1

/* Define if you don't have dirent.h, but have sys/dir.h.  */
/* #undef SYSDIR */

/* Define if you don't have dirent.h, but have sys/ndir.h.  */
/* #undef SYSNDIR */

/* Define to `int' if <sys/types.h> doesn't define.  */
/* #undef uid_t */

/* Define if the closedir function returns void instead of int.  */
/* #undef VOID_CLOSEDIR */

/* XXX */
/* #undef CRAY_STACKSEG_END */

/* Define if your rpcgen has the -C option to generate ANSI C.  */
/* #undef HAVE_RPCGEN_C */

/* Define if your rpcgen has the -I option to generate servers
   that can be started from a port monitor like inetd or the listener.  */
#define HAVE_RPCGEN_I 1

/* Define this to the numerical user id of the anonymous NFS user
   (commonly nobody).  */
/* #undef NOBODY_UID */

/* Define this to the numerical group id of the anonymous NFS user
   (commonly nogroup).  */
/* #undef NOBODY_GID */

/* Define if your system uses the OSF/1 method of getting the mount list.  */
/* #undef MOUNTED_GETFSSTAT */

/* Define if your system uses the SysVr4 method of getting the mount list.  */
/* #undef MOUNTED_GETMNTENT2 */

/* Define if your system uses the AIX method of getting the mount list.  */
/* #undef MOUNTED_VMOUNT */

/* Define if your system uses the SysVr3 method of getting the mount list.  */
/* #undef MOUNTED_FREAD_FSTYP */

/* Define if your system uses the BSD4.3 method of getting the mount list.  */
/* #undef MOUNTED_GETMNTENT1 */

/* Define if your system uses the BSD4.4 method of getting the mount list.  */
#define MOUNTED_GETMNTINFO 1

/* Define if your system uses the Ultrix method of getting the mount list.  */
/* #undef MOUNTED_GETMNT */

/* Define if your system uses the SysVr2 method of getting the mount list.  */
/* #undef MOUNTED_FREAD */

/* Define if your system uses the OSF/1 method of getting fs usage.  */
#define STATFS_OSF1 1

/* Define if your system uses the SysVr2 method of getting fs usage.  */
/* #undef STAT_READ */

/* Define if your system uses the BSD4.3 method of getting fs usage.  */
/* #undef STAT_STATFS2_BSIZE */

/* Define if your system uses the BSD4.4 method of getting fs usage.  */
/* #undef STAT_STATFS2_FSIZE */

/* Define if your system uses the Ultrix method of getting fs usage.  */
/* #undef STAT_STATFS2_FS_DATA */

/* Define if your system uses the SysVr3 method of getting fs usage.  */
/* #undef STAT_STATFS4 */

/* Define if your system uses the SysVr4 method of getting fs usage.  */
/* #undef STAT_STATVFS */

/* Define if you have getcwd.  */
#define HAVE_GETCWD 1

/* Define if you have getdtablesize.  */
#define HAVE_GETDTABLESIZE 1

/* Define if you have lchown.  */
#define HAVE_LCHOWN 1

/* Define if you have seteuid.  */
#define HAVE_SETEUID 1

/* Define if you have setgroups.  */
#define HAVE_SETGROUPS 1

/* Define if you have setreuid.  */
#define HAVE_SETREUID 1

/* Define if you have setsid.  */
#define HAVE_SETSID 1

/* Define if you have the <fcntl.h> header file.  */
#define HAVE_FCNTL_H 1

/* Define if you have the <memory.h> header file.  */
#define HAVE_MEMORY_H 1

/* Define if you have the <string.h> header file.  */
#define HAVE_STRING_H 1

/* Define if you have the <sys/file.h> header file.  */
#define HAVE_SYS_FILE_H 1

/* Define if you have the <sys/time.h> header file.  */
#define HAVE_SYS_TIME_H 1

/* Define if you have the <syslog.h> header file.  */
#define HAVE_SYSLOG_H 1

/* Define if you have the <unistd.h> header file.  */
#define HAVE_UNISTD_H 1

/* Define if you have the <utime.h> header file.  */
#define HAVE_UTIME_H 1

/* Define if you have the nsl library (-lnsl).  */
/* #undef HAVE_LIBNSL */

/* Define if you have the socket library (-lsocket).  */
/* #undef HAVE_LIBSOCKET */
