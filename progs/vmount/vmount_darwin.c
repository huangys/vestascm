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
 *  vmount [-p:rxsdyfugSW:R:T:E:H:ICAN:X:D:Y:OPU] host handle /directory
 * 
 *  -p port   UDP port number for NFS server, default 2049
 * 
 *  -r        M_RDONLY  The file system should be treated as read only; no
 *                      writing is allowed (even by a process with appropriate
 *                      privilege).  Physically write-protected and magnetic
 *                      tape file systems must be mounted read only or errors
 *                      will occur when access times are updated, whether or
 *                      not any explicit write is attempted.
 *
 *  -x        M_NOEXEC  Do not allow files to be executed from the file system.
 *
 *  -s        M_NOSUID  Do not honor setuid or setgid bits on files when exe-
 *                      cuting them.
 *
 *  -d        M_NODEV   Do not interpret special files on the file system.
 *
 *  -y        M_SYNCHRONOUS
 *                      All I/O to the file system should be done synchro-
 *                      nously.
 *
 *  -u        M_UPDATE  The mount command is being applied to an already
 *                      mounted file system.  This allows the mount flags to be
 *                      changed without requiring that the file system be
 *                      unmounted and remounted.
 *
 *  -S        NFSMNT_SOFT     soft mount (hard is default)
 *  -W size   NFSMNT_WSIZE    set write size (default 8192)
 *  -R size   NFSMNT_RSIZE    set read size (default 8192)
 *  -T tnths  NFSMNT_TIMEO    set initial timeout (in 1/10 secs; default 11)
 *  -E count  NFSMNT_RETRANS  set number of request retries (default 4)
 *  -I        NFSMNT_INT      allow interrupts on hard mount
 *  -C        NFSMNT_NOCONN   no connect on mount - any responder
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <stdlib.h>
#define _SOCKADDR_LEN
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* getopt */
#include <unistd.h>

/* nfs_args */
#include <nfs/rpcv2.h>
#include <nfs/nfsproto.h>
#include <nfs/nfs.h>

#define DEFAULT_PORT 2049

#define ARGS "p:rxsdyuSW:R:T:E:IC"

int
main(int argc, char *argv[])
{
    char *hostname, *handlestr, *dirname;
    int flags = 0;
    int i, c, errflg = 0;
    struct nfs_args nfsargs;
    struct sockaddr_in hostaddr;
    struct hostent *hp;
    unsigned char handle[NFSX_V2FH];
    unsigned short uport = DEFAULT_PORT;
    char errname[1024];

    memset((char *) &nfsargs, (int) 0, sizeof(struct nfs_args));

    nfsargs.version = NFS_ARGSVERSION;
    nfsargs.sotype = SOCK_DGRAM;

    optarg = NULL;
    while (!errflg && (c = getopt(argc, argv, ARGS)) != -1) {
	switch (c) {
	  case 'p':
	    uport = (unsigned short) atoi(optarg);
	    break;
	  case 'r':
	    flags |= MNT_RDONLY;
	    break;
	  case 'x':
	    flags |= MNT_NOEXEC;
	    break;
	  case 's':
	    flags |= MNT_NOSUID;
	    break;
	  case 'd':
	    flags |= MNT_NODEV;
	    break;
	  case 'y':
	    flags |= MNT_SYNCHRONOUS;
	    break;
	  case 'u':
	    flags |= MNT_UPDATE;
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
	  case 'I':
	    nfsargs.flags |= NFSMNT_INT;
	    break;
	  case 'C':
	    nfsargs.flags |= NFSMNT_NOCONN;
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
    for (i = 0; i < NFSX_V2FH; i++) {
	int byte;
	if (sscanf(&handlestr[i*2], "%2x", &byte) != 1) break;
	handle[i] = byte;
	if (handlestr[i*2 + 1] == '\0') {
	    handle[i] <<= 4;
	    break;
	}
    }
    for (; i < NFSX_V2FH; i++) {
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
    nfsargs.addr = (struct sockaddr *) &hostaddr;
    nfsargs.addrlen = sizeof(hostaddr);
    nfsargs.fh = (caddr_t) handle;
    nfsargs.fhsize = NFSX_V2FH;

    if (uport != DEFAULT_PORT) {
      sprintf(errname, "%s:%hu/%s", hostname, uport, handlestr);
    } else {
      sprintf(errname, "%s/%s", hostname, handlestr);
    }
    nfsargs.hostname = errname;

    if (mount("nfs", dirname, flags, (caddr_t) &nfsargs) == -1) {
	fprintf(stderr, "%s %s: %s\n", argv[0], dirname, strerror(errno));
	exit(2);
    }
    exit(0);
}

