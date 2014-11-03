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
 * vmount.c
 *
 * Mount an NFS file system by handle, without using mountd.
 *
 * Usage: 
 *  vmount [-p:rsSW:R:T:E:H:IAN:X:D:Y:OP] host handle /directory
 * 
 *  -p port   UDP port number for NFS server, default 2049
 * 
 *  -r        MS_RDONLY  The file system should be treated as read only; no
 *                       writing is allowed (even by a process with appropriate
 *                       privilege).  Physically write-protected and magnetic
 *                       tape file systems must be mounted read only or errors
 *                       will occur when access times are updated, whether or
 *                       not any explicit write is attempted.
 *
 *  -s        MS_NOSUID  Do not honor setuid or setgid bits on files when exe-
 *                       cuting them.
 *
 *  -S        NFSMNT_SOFT     soft mount (hard is default)
 *  -W size   NFSMNT_WSIZE    set write size (default 8192)
 *  -R size   NFSMNT_RSIZE    set read size (default 8192)
 *  -T tnths  NFSMNT_TIMEO    set initial timeout (in 1/10 secs; default 11)
 *  -E count  NFSMNT_RETRANS  set number of request retries (default 4)
 *  -H name   NFSMNT_HOSTNAME set hostname for error printf (default host:hdl)
 *  -I        NFSMNT_INT      allow interrupts on hard mount
 *  -A        NFSMNT_NOAC     don't cache attributes
 *  -N sec    NFSMNT_ACREGMIN set min seconds for file attr cache (def 3600)
 *  -X sec    NFSMNT_ACREGMAX set max seconds for file attr cache (def 36000)
 *  -D sec    NFSMNT_ACDIRMIN set min seconds for dir attr cache (def 3600)
 *  -Y sec    NFSMNT_ACDIRMAX set max seconds for dir attr cache (def 36000)
 *  -O        NFSMNT_NOCTO    don't freshen attributes on open
 */

#include <sys/mount.h>
#include <stdlib.h>
#define _SOCKADDR_LEN
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
/* Added for Solaris */
#include <nfs/nfs.h>
#include <nfs/mount.h>
#include <sys/mntent.h>

#define DEFAULT_PORT 2049

#define ARGS "p:rsgSW:R:T:E:H:IAN:X:D:Y:O"

int
main(int argc, char *argv[])
{
    char *hostname, *handlestr, *dirname;
    int flags = MS_DATA;
    int i, c, errflg = 0;
    struct nfs_args nfsargs;
    struct sockaddr_in hostaddr;
    struct hostent *hp;
    unsigned char handle[NFS_FHSIZE];
    unsigned short uport = DEFAULT_PORT;
    char errname[1024];
    int errno_save;
    struct netbuf host_netbuf;
    struct knetconfig nfs_knetconfig;

    memset((char *) &nfsargs, (int) 0, sizeof(struct nfs_args));
    memset((char *) &nfs_knetconfig, (int) 0, sizeof(struct knetconfig));
    memset((char *) &host_netbuf, (int) 0, sizeof(struct netbuf));

    /* The repository has no lock manager, so we must use this
       flag. */
    nfsargs.flags = NFSMNT_LLOCK;

    optarg = NULL;
    while (!errflg && (c = getopt(argc, argv, ARGS)) != -1) {
	switch (c) {
	  case 'p':
	    uport = (unsigned short) atoi(optarg);
	    break;
	  case 'r':
	    flags |= MS_RDONLY;
	    break;
	  case 's':
	    flags |= MS_NOSUID;
	    break;
	  case 'S':
	    nfsargs.flags |= NFSMNT_SOFT;
	    break;
	  case 'W':
	    nfsargs.flags |= NFSMNT_WSIZE;
	    nfsargs.wsize = atoi(optarg);
	    break;
	  case 'R':
	    nfsargs.flags |= NFSMNT_RSIZE;
	    nfsargs.rsize = atoi(optarg);
	    break;
	  case 'T':
	    nfsargs.flags |= NFSMNT_TIMEO;
	    nfsargs.timeo = atoi(optarg);
	    break;
	  case 'E':
	    nfsargs.flags |= NFSMNT_RETRANS;
	    nfsargs.retrans = atoi(optarg);
	    break;
	  case 'H':
	    nfsargs.flags |= NFSMNT_HOSTNAME;
	    nfsargs.hostname = optarg;
	    break;
	  case 'I':
	    nfsargs.flags |= NFSMNT_INT;
	    break;
	  case 'A':
	    nfsargs.flags |= NFSMNT_NOAC;
	    break;
	  case 'N':
	    nfsargs.flags |= NFSMNT_ACREGMIN;
	    nfsargs.acregmin = atoi(optarg);
	    break;
	  case 'X':
	    nfsargs.flags |= NFSMNT_ACREGMAX;
	    nfsargs.acregmax = atoi(optarg);
	    break;
	  case 'D':
	    nfsargs.flags |= NFSMNT_ACDIRMIN;
	    nfsargs.acdirmin = atoi(optarg);
	    break;
	  case 'Y':
	    nfsargs.flags |= NFSMNT_ACDIRMAX;
	    nfsargs.acdirmax = atoi(optarg);
	    break;
	  case 'O':
	    nfsargs.flags |= NFSMNT_NOCTO;
	    break;
	  default:
	    errflg++;
	    break;
	}
    }

    if (errflg || argc < optind + 3) {
	fprintf(stderr,
		"Usage: %s [-%s] host handle /dir\n", argv[0], ARGS);
	exit(1);
    }
    
    hostname = argv[optind++];
    handlestr = argv[optind++];
    dirname = argv[optind];

    if (dirname[0] != '/') {
	fprintf(stderr,
		"%s: directory name must begin with '/'\n", argv[0]);
	exit(1);
    }

    /* Convert handlestr to handle */
    for (i = 0; i < NFS_FHSIZE; i++) {
	int byte;
	if (sscanf(&handlestr[i*2], "%2x", &byte) != 1) break;
	handle[i] = byte;
	if (handlestr[i*2 + 1] == '\0') {
	    handle[i] <<= 4;
	    break;
	}
    }
    for (; i < NFS_FHSIZE; i++) {
	handle[i] = 0;
    }

    /* Look up host name */
    memset((char *) &hostaddr, (int) 0, sizeof(struct sockaddr_in));
    if (!(hp = gethostbyname(hostname))) {
	int b0, b1, b2, b3;
	if (sscanf(hostname, "%d.%d.%d.%d", &b0, &b1, &b2, &b3) == 4) {
	    hp = (struct hostent *) calloc(1, sizeof(struct hostent));
	    hp->h_addrtype = AF_INET;
	    hp->h_length = 4;
	    hp->h_addr_list = (char **) calloc(2, sizeof(char *));
	    hp->h_addr_list[0] = (char *) malloc(4);
	    hp->h_addr_list[0][0] = b0;
	    hp->h_addr_list[0][1] = b1;
	    hp->h_addr_list[0][2] = b2;
	    hp->h_addr_list[0][3] = b3;
	} else {
	    fprintf(stderr, "%s error: no such host: %s\n", argv[0], hostname);
	    exit(1);
	}
    }
    hostaddr.sin_family = hp->h_addrtype;
    memcpy((char *) &hostaddr.sin_addr, hp->h_addr, hp->h_length);
    hostaddr.sin_port = htons(uport);
    
    /* Fill in remaining NFS mount args */
    host_netbuf.len = sizeof(hostaddr);
    host_netbuf.maxlen = sizeof(hostaddr);
    host_netbuf.buf = (char *) &hostaddr;
    nfsargs.addr = &host_netbuf;
    nfsargs.fh = (caddr_t) handle;

    /* KCS, 2005-01-15: I don't pretend to fully understand the
       contents of knconf.  I reverse-engineered it by eaxmining a
       core dump of /sbin/mount. */

    nfsargs.knconf = &nfs_knetconfig;
    nfsargs.flags |= NFSMNT_KNCONF;

    nfs_knetconfig.knc_semantics = 1;
    nfs_knetconfig.knc_protofmly = (caddr_t) "inet";
    nfs_knetconfig.knc_proto = (caddr_t) "udp";
    /* There's definitely some voodoo in this one.  All I can say is
       that /dev/udp is a character special device with major 41,
       minor 0, and 0xa4 is (41 << 2). */
    nfs_knetconfig.knc_rdev = 0x00a40000;
    
    if (uport != DEFAULT_PORT) {
      sprintf(errname, "%s:%hu/%s", hostname, uport, handlestr);
    } else {
      sprintf(errname, "%s/%s", hostname, handlestr);
    }

    if (!(flags & NFSMNT_HOSTNAME)) {
      nfsargs.flags |= NFSMNT_HOSTNAME;
      nfsargs.hostname = hostname;
    }

    if (mount(errname,
	      dirname, flags,
	      MNTTYPE_NFS, (caddr_t) &nfsargs, sizeof(nfsargs),
	      0, 0) == -1) {
      errno_save = errno;
      fprintf(stderr, "%s %s: %s (errno = %d)\n",
	      argv[0], dirname, strerror(errno_save),
	      errno_save);
      exit(2);
    }
    exit(0);
}

