dnl Required initialization.

AC_INIT(libBasics, 1.0)
AC_CONFIG_SRCDIR(Basics.C)
AM_INIT_AUTOMAKE

dnl Tell libtool not to bother with shared libraries for now
AC_DISABLE_SHARED

dnl Checks for programs we need
AC_PROG_CXX
AC_PROG_LIBTOOL
AC_CONFIG_HEADER(BasicsConfig.h)
AC_CHECK_HEADERS([stdint.h machine/endian.h getopt.h])
AC_CHECK_SIZEOF(unsigned short)
AC_CHECK_SIZEOF(unsigned int)
AC_CHECK_SIZEOF(unsigned long)
AC_CHECK_SIZEOF(unsigned long long)
AC_CHECK_SIZEOF(void *)
AC_CHECK_FUNCS(strerror_r strerror vasprintf vsnprintf)
AC_TRY_RUN([#include <stdio.h>
#include <string.h>
int main()
{
#if SIZEOF_UNSIGNED_INT == 8
  unsigned int x = 0x0123456789abcdefU;
#elif SIZEOF_UNSIGNED_LONG == 8
  unsigned long x = 0x0123456789abcdefUL;
#elif SIZEOF_UNSIGNED_LONG_LONG == 8
  unsigned long long x = 0x0123456789abcdefULL;
#else
#error No 64-bit integer type
#endif

  char buf[17];
  sprintf(buf, "%016llx", x);
  return strcmp(buf, "0123456789abcdef");
}
], [AC_DEFINE(FORMAT_LENGTH_INT_64, "ll", [printf-style format length specified for 64-bit integers])], [AC_TRY_RUN([#include <stdio.h>
#include <string.h>
int main()
{
#if SIZEOF_UNSIGNED_INT == 8
  unsigned int x = 0x0123456789abcdefU;
#elif SIZEOF_UNSIGNED_LONG == 8
  unsigned long x = 0x0123456789abcdefUL;
#elif SIZEOF_UNSIGNED_LONG_LONG == 8
  unsigned long long x = 0x0123456789abcdefULL;
#else
#error No 64-bit integer type
#endif

  char buf[17];
  sprintf(buf, "%016lx", x);
  return strcmp(buf, "0123456789abcdef");
}
], [AC_DEFINE(FORMAT_LENGTH_INT_64, "l", [printf-style format length specified for 64-bit integers])])
])
AC_LANG_PUSH(C++)
AC_TRY_RUN([#include <string.h>
#include <errno.h>
int main() {
  char buf[2];
  int res = strerror_r(ERANGE, buf, sizeof(buf));
  if ((res == -1) && (errno == ERANGE)) return 0;
  return 1;
}
], [AC_DEFINE(STRERROR_R_SUSV3, 1, [strerror_r behaves like SUSv3 says])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_RUN([#include <string.h>
#include <errno.h>
int main() {
  char buf[2];
  char *res = strerror_r(ENOENT, buf, sizeof(buf));
  return strcmp(res, "No such file or directory");
}
], [AC_DEFINE(STRERROR_R_GLIBC, 1, [strerror_r behaves like GLIBC extension])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_RUN([#include <string.h>
#include <errno.h>
int main() {
  char buf[2];
  int res = strerror_r(ERANGE, buf, sizeof(buf));
  if (res == ERANGE) return 0;
  return 1;
}
], [AC_DEFINE(STRERROR_R_RETURNS_ERANGE, 1, [strerror_r returns ERANGE if the buffer is too small])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#include <iosfwd>
#include <iostream>
], [typedef streampos pos_type;
typedef streamoff off_type;
typedef ios::seek_dir seekdir;
typedef int openmode;
], [AC_DEFINE(IOSTREAM_TYPES_PREISO, 1, [The old pre-ISO/gcc2 iostream types exist])])
AC_LANG_POP(C++)
AC_LANG_PUSH(C++)
AC_TRY_COMPILE([#include <iosfwd>
#include <iostream>
], [typedef std::streambuf::pos_type pos_type;
typedef std::streambuf::off_type off_type;
typedef std::ios_base::seekdir seekdir;
typedef std::ios_base::openmode openmode;
], [AC_DEFINE(IOSTREAM_TYPES_ISO, 1, [The ANSI/ISO/gcc3 iostream types exist])])
AC_LANG_POP(C++)
prevlibs="$LIBS"
LIBS="-lpthread -lc"
AC_TRY_RUN([#include <pthread.h>

void * thread_body(void *p)
{
  sleep(0);
  return p;
}

int main()
{
  pthread_t t;

  pthread_attr_t attr;
  void *result;
  int code;

  code = pthread_attr_init(&attr);
  if (code != 0) { return 1; }
  code = pthread_create(&t, &attr, thread_body, 0);
  if (code != 0) { return 1; }
  code = pthread_join(t, &result);
  if (code != 0) { return 1; }

  return 0;
}
], [AC_DEFINE(PTHREAD_CREATE_OK, 1, [pthreads seem functional])])
LIBS="$prevlibs"
prevlibs="$LIBS"
LIBS="-lpthread -lc"
AC_TRY_RUN([#include <pthread.h>
#include <sched.h>

void * thread_body(void *p)
{
  sleep(0);
  return p;
}

int main()
{
  pthread_t t = pthread_self();
  int old_policy;
  struct sched_param my_sched_param;
  pthread_attr_t attr;
  int code;
  void *result;

  code = pthread_getschedparam(t, &old_policy, &my_sched_param);
  if (code != 0) return 1;
  my_sched_param.sched_priority = sched_get_priority_min(SCHED_RR);
  code = pthread_setschedparam(t, SCHED_RR, &my_sched_param);
  if (code != 0) return 1;
  code = pthread_attr_init(&attr);
  if (code != 0) return 1;
  code = pthread_attr_setschedpolicy(&attr, SCHED_RR);
  if (code != 0) return 1;
  code = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  if (code != 0) return 1;
  code = pthread_attr_getschedparam(&attr, &my_sched_param);
  if (code != 0) return 1;
  my_sched_param.sched_priority = sched_get_priority_min(SCHED_RR);
  code = pthread_attr_getschedparam(&attr, &my_sched_param);
  if (code != 0) return 1;
  code = pthread_create(&t, &attr, thread_body, 0);
  if (code != 0) return 1;
  code = pthread_join(t, &result);
  if (code != 0) return 1;

  return 0;
}
], [AC_DEFINE(PTHREAD_SCHED_RR_OK, 1, [SCHED_RR usable with pthreads])])
LIBS="$prevlibs"
dnl Generate a Makefile
AC_OUTPUT(Makefile)
