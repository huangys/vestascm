dnl Required initialization.

AC_INIT(libVestaReposLeaf, 1.0)
AC_CONFIG_SRCDIR(VestaSourceCommon.C)
AM_INIT_AUTOMAKE

dnl Tell libtool not to bother with shared libraries for now
AC_DISABLE_SHARED

dnl Checks for programs we need
AC_PROG_CC
AC_PROG_CXX
AC_PROG_LIBTOOL

dnl Extra libraries which may be present and we need on some platforms (Solaris)
AC_SEARCH_LIBS([gethostbyname], [nsl])
AC_SEARCH_LIBS([socket], [socket], [], [AC_CHECK_LIB([socket], [socket], [LIBS="-lsocket -lnsl $LIBS"], [], [-lnsl])])

AC_CONFIG_HEADER(ReposConfig.h)
AC_CHECK_FUNCS(initstate_r random_r rand_r srandom random _authenticate __authenticate statfs statvfs)
AC_CHECK_HEADERS([sys/statvfs.h sys/statfs.h sys/param.h sys/mount.h])
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#define _REENTRANT 1
#include <stdlib.h>
], [struct random_data rd; int ri; (void) random_r(&rd, &ri);], [AC_DEFINE(RANDOM_R_ARGS_RANDOM_DATA_INT, 1, [random_r takes arguments (struct random_data *, int *)])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#define _REENTRANT 1
#include <stdlib.h>
], [struct random_data rd; int ri; (void) random_r(&ri, &rd);], [AC_DEFINE(RANDOM_R_ARGS_INT_RANDOM_DATA, 1, [random_r takes arguments (int *, struct random_data *)])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#define _REENTRANT 1
#include <stdlib.h>
], [unsigned seed; char state_buf[128]; struct random_data rd; (void) initstate_r(seed, state_buf, sizeof(state_buf), &rd);], [AC_DEFINE(INITSTATE_R_ARGS_SEED_STATE_SIZE_RD, 1, [initstate_r takes arguments (unsigned int, char *, size_t, struct random_data *)])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#define _REENTRANT 1
#include <stdlib.h>
], [unsigned seed; char state_buf[128]; char *dummy; struct random_data rd; (void) initstate_r(seed, state_buf, sizeof(state_buf), &dummy, &rd);], [AC_DEFINE(INITSTATE_R_ARGS_SEED_STATE_SIZE_CHARPP_RD, 1, [initstate_r takes arguments (unsigned int, char *, size_t, char **, struct random_data *)])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#include <rpc/rpc.h>
#include <rpc/auth_unix.h>
], [authunix_parms aup; gid_t gid; aup.aup_gids = &gid;], [AC_DEFINE(AUTHUNIX_PARMS_GID_T_GID, 1, [authunix_parms.gid is type int])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#include <rpc/rpc.h>
#include <rpc/auth_unix.h>
], [authunix_parms aup; int gid; aup.aup_gids = &gid;], [AC_DEFINE(AUTHUNIX_PARMS_INT_GID, 1, [authunix_parms.gid is type int])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#include <rpc/rpc.h>
], [SVCXPRT xprt; xprt.xp_sock = 1;], [AC_DEFINE(SVCXPRT_XP_SOCK, 1, [SVCXPRT has the xp_sock field])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#include <rpc/rpc.h>
], [SVCXPRT xprt; xprt.xp_fd = 1;], [AC_DEFINE(SVCXPRT_XP_FD, 1, [SVCXPRT has the xp_fd field])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#include <rpc/rpc.h>
], [SVCXPRT *xprt; xdrproc_t xdr_arg; char arg[1024]; int res;
res = svc_getargs(xprt, xdr_arg, arg);], [AC_DEFINE(SVC_GETARGS_WORKS_AS_IS, 1, [svc_getargs works in C++ as-is])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#include <rpc/rpc.h>
], [SVCXPRT *xprt; xdrproc_t xdr_arg; char arg[1024]; int res;
typedef bool_t (*__xp_getargs_t)(SVCXPRT *, xdrproc_t, void *);
#define svc_getargs(xprt, xargs, argsp) (*((__xp_getargs_t) (xprt)->xp_ops->xp_getargs))((xprt), (xargs), (argsp))
res = svc_getargs(xprt, xdr_arg, arg);], [AC_DEFINE(SVC_GETARGS_WORKS_WITH_TYPE_FIX, 1, [svc_getargs works in C++ with xp_getargs type correction])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#include <rpc/rpc.h>
], [SVCXPRT *xprt; xdrproc_t xdr_arg; char arg[1024]; int res;
res = svc_freeargs(xprt, xdr_arg, arg);], [AC_DEFINE(SVC_FREEARGS_WORKS_AS_IS, 1, [svc_freeargs works in C++ as-is])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#include <rpc/rpc.h>
], [SVCXPRT *xprt; xdrproc_t xdr_arg; char arg[1024]; int res;
typedef bool_t (*__xp_freeargs_t)(SVCXPRT *, xdrproc_t, void *);
#define svc_freeargs(xprt, xargs, argsp) (*((__xp_freeargs_t) (xprt)->xp_ops->xp_freeargs))((xprt), (xargs), (argsp))
res = svc_freeargs(xprt, xdr_arg, arg);], [AC_DEFINE(SVC_FREEARGS_WORKS_WITH_TYPE_FIX, 1, [svc_freeargs works in C++ with xp_freeargs type correction])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#include <rpc/rpc.h>
], [SVCXPRT xprt; struct rpc_msg msg; int res;
res = SVC_RECV(&xprt, &msg);], [AC_DEFINE(SVC_RECV_WORKS_AS_IS, 1, [svc_recv works in C++ as-is])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#include <rpc/rpc.h>
], [SVCXPRT xprt; struct rpc_msg msg; int res;
typedef bool_t (*__xp_recv_t)(SVCXPRT *, struct rpc_msg *);
#define SVC_RECV(xprt, msg) (*((__xp_recv_t) (xprt)->xp_ops->xp_recv))((xprt), (msg))
res = SVC_RECV(&xprt, &msg);], [AC_DEFINE(SVC_RECV_WORKS_WITH_TYPE_FIX, 1, [svc_recv works in C++ with xp_recv type correction])])
AC_LANG_POP(C++)
dnl Generate a Makefile
AC_OUTPUT(Makefile)
