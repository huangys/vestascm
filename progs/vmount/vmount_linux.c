/*
 * vmount.c
 * Last modified on Tue Dec 12 18:45:11 PST 2000 by mann
 *
 * Mount an NFS file system by handle, without using mountd.
 *
 * Usage: 
 *  vmount [-p:rxsdyfugSW:R:T:E:H:ICAN:X:D:Y:OPUZ] host handle /directory
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
 *  -f        M_FMOUNT  Forcibly mount, even if the file system is unclean.
 *                      ** Not implemented on Linux **
 *
 *  -u        M_UPDATE  The mount command is being applied to an already
 *                      mounted file system.  This allows the mount flags to be
 *                      changed without requiring that the file system be
 *                      unmounted and remounted.
 *
 *  -g        M_GRPID   All new files and directories will inherit the group ID
 *                      of the parent.  When this is not specified, SVID III
 *                      semantics apply, for example, if the parent directory's
 *                      mode bits include the parent's group ID.  If IS_GID is
 *                      off, then it inherits the processes group ID.
 *                      ** Not implemented on Linux **
 *
 *  -S        NFSMNT_SOFT     soft mount (hard is default)
 *  -W size   NFSMNT_WSIZE    set write size (default determined by kernel)
 *  -R size   NFSMNT_RSIZE    set read size (default determined by kernel)
 *  -T tnths  NFSMNT_TIMEO    set initial timeout (in 1/10 secs; default 7)
 *  -E count  NFSMNT_RETRANS  set number of request retries (default 3)
 *  -H name   NFSMNT_HOSTNAME set hostname for error printf (default host:hdl)
 *  -I        NFSMNT_INT      allow interrupts on hard mount
 *  -C        NFSMNT_NOCONN   no connect on mount - any responder
 *  -A        NFSMNT_NOAC     don't cache attributes
 *  -N sec    NFSMNT_ACREGMIN set min seconds for file attr cache (def 3)
 *  -X sec    NFSMNT_ACREGMAX set max seconds for file attr cache (def 60)
 *  -D sec    NFSMNT_ACDIRMIN set min seconds for dir attr cache (def 30)
 *  -Y sec    NFSMNT_ACDIRMAX set max seconds for dir attr cache (def 60)
 *  -O        NFSMNT_NOCTO    don't freshen attributes on open
 *  -P        NFSMNT_POSIX    static pathconf kludge info (not supported)
 *  -U        NFSMNT_AUTO     automount file system
 *  -Z        NFS_MOUNT_SECURE  linux flag, meaning unknown
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mount.h>

/* ----------------------------------------------------------------------

 * The Linux kernel headers defining the NFS mount data structure
 * conflict with certain versions of the glibc includes.  We avoid
 * trouble by just defining it ourselves.

 * This also makes it possible to include new fields introduced in
 * later kernels.  This allows us to compile vmount on old kernels in
 * such a way that the binary will work well on more recent kernels.

 * ---------------------------------------------------------------------- */

#define NFS_MOUNT_VERSION	4

struct nfs2_fh {
        char                    data[32];
};
struct nfs3_fh {
        unsigned short          size;
        unsigned char           data[64];
};

struct nfs_mount_data {
	int		version;		/* 1 */
	int		fd;			/* 1 */
	struct nfs2_fh	old_root;		/* 1 */
	int		flags;			/* 1 */
	int		rsize;			/* 1 */
	int		wsize;			/* 1 */
	int		timeo;			/* 1 */
	int		retrans;		/* 1 */
	int		acregmin;		/* 1 */
	int		acregmax;		/* 1 */
	int		acdirmin;		/* 1 */
	int		acdirmax;		/* 1 */
	struct sockaddr_in addr;		/* 1 */
	char		hostname[256];		/* 1 */
	int		namlen;			/* 2 */
	unsigned int	bsize;			/* 3 */
	struct nfs3_fh	root;			/* 4 */
};

/* bits in the flags field */

#define NFS_MOUNT_SOFT		0x0001	/* 1 */
#define NFS_MOUNT_INTR		0x0002	/* 1 */
#define NFS_MOUNT_SECURE	0x0004	/* 1 */
#define NFS_MOUNT_POSIX		0x0008	/* 1 */
#define NFS_MOUNT_NOCTO		0x0010	/* 1 */
#define NFS_MOUNT_NOAC		0x0020	/* 1 */
#define NFS_MOUNT_TCP		0x0040	/* 2 */
#define NFS_MOUNT_VER3		0x0080	/* 3 */
#define NFS_MOUNT_KERBEROS	0x0100	/* 3 */
#define NFS_MOUNT_NONLM		0x0200	/* 3 */

#define NFS_MOUNT_LOCAL_FLOCK	0x100000
#define NFS_MOUNT_LOCAL_FCNTL	0x200000

#define NFS_FHSIZE 32

#define DEFAULT_PORT 2049

#define ARGS "p:rxsdyfugSW:R:T:E:H:ICAN:X:D:Y:OPU"

int
main(int argc, char *argv[])
{
    char *hostname, *handlestr, *dirname;
    unsigned long flags = MS_MGC_VAL;
    int i, c, errflg = 0;
    struct nfs_mount_data nfsargs;
    struct sockaddr_in hostaddr;
    struct hostent *hp;
    unsigned char handle[NFS_FHSIZE];
    unsigned short uport = DEFAULT_PORT;
    char errname[1024];

    memset((char *) &nfsargs, (int) 0, sizeof(nfsargs));
    /* Set defaults */
    nfsargs.version = NFS_MOUNT_VERSION;
    nfsargs.rsize = 0; 
    nfsargs.wsize = 0;
    nfsargs.timeo = 7;
    nfsargs.retrans = 3;
    nfsargs.acregmin = 3;
    nfsargs.acregmax = 60;
    nfsargs.acdirmin = 30;
    nfsargs.acdirmax = 60;
    /* Disable lock manager support by default */ 
    nfsargs.flags = NFS_MOUNT_NONLM | NFS_MOUNT_LOCAL_FLOCK | NFS_MOUNT_LOCAL_FCNTL;

    
    optarg = NULL;
    while (!errflg && (c = getopt(argc, argv, ARGS)) != -1) {
	switch (c) {
	  case 'p':
	    uport = (unsigned short) atoi(optarg);
	    break;
	  case 'r':
	    flags |= MS_RDONLY;
	    break;
	  case 'x':
	    flags |= MS_NOEXEC;
	    break;
	  case 's':
	    flags |= MS_NOSUID;
	    break;
	  case 'd':
	    flags |= MS_NODEV;
	    break;
	  case 'y':
	    flags |= MS_SYNCHRONOUS;
	    break;
	  case 'u':
	    flags |= MS_REMOUNT;
	    break;
	  case 'S':
	    nfsargs.flags |= NFS_MOUNT_SOFT;
	    break;
	  case 'W':
	    nfsargs.wsize = atoi(optarg);
	    break;
	  case 'R':
	    nfsargs.rsize = atoi(optarg);
	    break;
	  case 'T':
	    nfsargs.timeo = atoi(optarg);
	    break;
	  case 'E':
	    nfsargs.retrans = atoi(optarg);
	    break;
	  case 'H':
	    strcpy(nfsargs.hostname, optarg);
	    break;
	  case 'I':
	    nfsargs.flags |= NFS_MOUNT_INTR;
	    break;
	  case 'A':
	    nfsargs.flags |= NFS_MOUNT_NOAC;
	    break;
	  case 'N':
	    nfsargs.acregmin = atoi(optarg);
	    break;
	  case 'X':
	    nfsargs.acregmax = atoi(optarg);
	    break;
	  case 'D':
	    nfsargs.acdirmin = atoi(optarg);
	    break;
	  case 'Y':
	    nfsargs.acdirmax = atoi(optarg);
	    break;
	  case 'O':
	    nfsargs.flags |= NFS_MOUNT_NOCTO;
	    break;
	  case 'Z':
	    nfsargs.flags |= NFS_MOUNT_SECURE;
	    break;
	  default:
	    errflg++;
	    break;
	  case 'f':
	  case 'g':
	  case 'C':
	  case 'P':
	  case 'U':
	    fprintf(stderr, "%s: -%c flag not implemented\n", argv[0], c);
	    exit(1);
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
    
    /* Open a socket */
    nfsargs.fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (bindresvport(nfsargs.fd, 0) < 0) {
        fprintf(stderr, "%s warning: bindresvport: %s\n",
                argv[0], strerror(errno));
    }
    if (connect(nfsargs.fd, (const struct sockaddr*) &hostaddr,
		sizeof(hostaddr)) < 0) {
        fprintf(stderr, "%s connect: %s\n", argv[0], strerror(errno));
        exit(2);
    }

    /* Fill in remaining NFS mount args */
    nfsargs.addr = hostaddr;

    memcpy(nfsargs.root.data, (char *) handle, NFS_FHSIZE);
    nfsargs.root.size = NFS_FHSIZE;
    memcpy(nfsargs.old_root.data, (char *) handle, NFS_FHSIZE);

    if (uport != DEFAULT_PORT) {
      sprintf(errname, "%s.%hu:%s", hostname, uport, handlestr);
    } else {
      sprintf(errname, "%s:%s", hostname, handlestr);
    }

    if (nfsargs.hostname[0] == '\000') {
      strcpy(nfsargs.hostname, errname);
    }

    if (mount(errname, dirname, "nfs", flags,
	      (const void*) &nfsargs) == -1) {
	fprintf(stderr, "%s %s: %s\n", argv[0], dirname, strerror(errno));
	exit(2);
    }
    exit(0);
}
