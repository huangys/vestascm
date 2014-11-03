dnl Required initialization.

AC_INIT(libVestaSRPC, 1.0)
AC_CONFIG_SRCDIR(SRPC.C)
AM_INIT_AUTOMAKE

dnl Tell libtool not to bother with shared libraries for now
AC_DISABLE_SHARED

dnl Checks for programs we need
AC_PROG_CXX
AC_PROG_LIBTOOL

dnl Extra libraries which may be present and we need on some platforms (Solaris)
AC_SEARCH_LIBS([gethostbyname], [nsl])
AC_SEARCH_LIBS([socket], [socket], [], [AC_CHECK_LIB([socket], [socket], [LIBS="-lsocket -lnsl $LIBS"], [], [-lnsl])])

AC_CONFIG_HEADER(SRPCConfig.h)
AC_CHECK_HEADERS([sys/filio.h])
AC_TRY_COMPILE([#include <sys/types.h>
#include <sys/socket.h>
], [struct sockaddr sa; sa.sa_len = sizeof(sa);], [AC_DEFINE(SOCKADDR_HAS_SA_LEN, 1, [struct sockaddr has the sa_len field])])
AC_TRY_COMPILE([#include <sys/types.h>
#include <netinet/in.h>
], [struct sockaddr_in sa; sa.sin_len = sizeof(sa);], [AC_DEFINE(SOCKADDR_IN_HAS_SIN_LEN, 1, [struct sockaddr_in has the sin_len field])])
AC_TRY_RUN([#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <assert.h>

int main()
{
  struct addrinfo hints, *res = 0, *cur;
  int err, found = 0;

  /* Prepare the hints to ask for IPv4 results. */
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = PF_INET;

  /* Look up the address for hostname "localhost". */
  err = getaddrinfo("localhost", 0, &hints, &res);

  if(err != 0)
    {
      printf("getaddrinfo(localhost): %s\n", gai_strerror(err));
      return 1;
    }

  for(cur = res; (cur != 0) && !found; cur = cur->ai_next)
    {
      assert(cur->ai_addr != 0);
      if((cur->ai_addr->sa_family == AF_INET) &&
	 (((struct sockaddr_in *) cur->ai_addr)->sin_addr.s_addr == htonl(INADDR_LOOPBACK)))
	{
	  found = 1;
	}
    }
  freeaddrinfo(res);

  if(!found)
    {
      printf("loopback address not found in getaddrinfo result\n");
      return 1;
    }

  /* Look up the port for "smtp". */
  err = getaddrinfo(0, "smtp", &hints, &res);

  if(err != 0)
    {
      printf("getaddrinfo(smtp): %s\n", gai_strerror(err));
      return 1;
    }

  found = 0;
  for(cur = res; (cur != 0) && !found; cur = cur->ai_next)
    {
      assert(cur->ai_addr != 0);
      if((cur->ai_addr->sa_family == AF_INET) &&
	 (((struct sockaddr_in *) cur->ai_addr)->sin_port == htons(25)))
	{
	  found = 1;
	}
    }
  freeaddrinfo(res);

  if(!found)
    {
      printf("smtp port not found in getaddrinfo result\n");
      return 1;
    }

  /* If we make it all the way to the end after performing both
     lookups, all should be well. */
 return 0;
}
], [AC_DEFINE(USE_GETADDRINFO, 1, [getaddrinfo is present and seems to work])])
AC_TRY_RUN([#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>

int main()
{
  struct addrinfo hints, *res = 0, *cur;
  int err, found = 0;

  struct sockaddr_in addr;
  char hname_buf[1024];
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#if defined(SOCKADDR_IN_HAS_SIN_LEN)
  addr.sin_len = sizeof(addr);
#endif

  // We should really use this on any platform where it's
  // available.
  err = getnameinfo((struct sockaddr *) &addr, sizeof(addr),
		    hname_buf, sizeof(hname_buf),
		    (char *) 0, 0, NI_NAMEREQD);


  if (err != 0)
    {
      printf("getnameinfo(INADDR_LOOPBACK): %s\n", gai_strerror(err));
      return 1;
    }

  if(strncmp(hname_buf, "localhost", strlen("localhost")) != 0)
    {
      printf("getnameinfo(INADDR_LOOPBACK) = \"%s\", doesn't start with \"localhost\"\n",
	     hname_buf);
      return 1;
    }

  return 0;
}
], [AC_DEFINE(USE_GETNAMEINFO, 1, [getnameinfo is present and seems to work])])
dnl Generate a Makefile
AC_OUTPUT(Makefile)
