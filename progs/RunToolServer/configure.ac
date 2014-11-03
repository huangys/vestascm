dnl Required initialization.

AC_INIT(RunToolServer, 1.0)
AC_CONFIG_SRCDIR(RunToolDaemon.C)
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

AC_CHECK_LIB(elf, elf_begin, LIBS="-lelf $LIBS")
AC_CHECK_LIB(kvm, kvm_open, LIBS="-lkvm $LIBS")
AC_CHECK_LIB(kstat, kstat_open, LIBS="-lkstat $LIBS")
AC_CONFIG_HEADER(RunToolDaemonConfig.h)
AC_CHECK_HEADERS([stdint.h sys/sysinfo.h machine/hal_sysinfo.h sys/sysctl.h sys/processor.h])
AC_TRY_RUN([#include <stdio.h>
#include <sys/sysinfo.h>
#include <machine/hal_sysinfo.h>

int main()
{
  unsigned int cpus, cpuMHz;
  struct cpu_info cpuinfo;
  int start = 0, res = 0;

  cpuinfo.cpus_in_box = 0;
  cpuinfo.mhz = 0;
  res = getsysinfo(GSI_CPU_INFO, (caddr_t) &cpuinfo, sizeof(cpuinfo),
		   &start, NULL, NULL);
  if(res == -1)
    {
      perror("getsysinfo");
      return 1;
    }
  else if(res < 1)
    {
      printf("res = %d\n", res);
      return 1;
    }

  cpus = cpuinfo.cpus_in_box;
  cpuMHz = cpuinfo.mhz;

  if(cpus < 1)
    {
      printf("0 CPUs!\n");
      return 1;
    }

  if(cpuMHz < 1)
    {
      printf("0 CPU speed!\n");
      return 1;
    }

  printf("cpus : %d\ncpuMHz : %d\n", cpus, cpuMHz);
  return 0;
}
], [AC_DEFINE(USE_GETSYSINFO_CPU_INFO, 1, [Use getsysinfo with GSI_CPU_INFO to get CPU count and speed])])
AC_TRY_RUN([#include <stdio.h>
#include <sys/sysinfo.h>
#include <machine/hal_sysinfo.h>

int main()
{
  unsigned int memKB = 0;
  int start = 0, res = 0;

  res = getsysinfo(GSI_PHYSMEM, (caddr_t) &memKB, sizeof(memKB),
		   &start, NULL, NULL);
  if(res == -1)
    {
      perror("getsysinfo");
      return 1;
    }
  else if(res < 1)
    {
      printf("res = %d\n", res);
      return 1;
    }

  if(memKB < 1)
    {
      printf("0 memory!\n");
      return 1;
    }

  printf("memKB : %d\n", memKB);
  return 0;
}
], [AC_DEFINE(USE_GETSYSINFO_PHYSMEM, 1, [Use getsysinfo with GSI_PHYSMEM to get memory size])])
AC_TRY_RUN([#include <sys/types.h>
#include <sys/processor.h>
#include <stdio.h>

int main()
{
  unsigned int cpuMHz;
  processor_info_t proc_info;
  int res;

  proc_info.pi_clock = 0;
  res = processor_info(0, &proc_info);
  if(res != 0)
    {
      printf("processor_info failed!\n");
      return 1;
    }
  cpuMHz = proc_info.pi_clock;

  if(cpuMHz < 1)
    {
      printf("0 CPU speed!\n");
      return 1;
    }

  printf("cpuMHz : %d\n", cpuMHz);
  return 0;
}
], [AC_DEFINE(USE_PROCESSOR_INFO, 1, [Use processor_info to get CPU speed])])
AC_TRY_RUN([#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

int main()
{
  unsigned int cpus;

  cpus = sysconf(_SC_NPROCESSORS_ONLN);

  if(cpus < 1)
    {
      printf("0 CPUs!\n");
      return 1;
    }

  printf("cpus : %d\n", cpus);

  return 0;
}
], [AC_DEFINE(USE_SYSCONF_NPROCESSORS, 1, [Use sysconf with _SC_NPROCESSORS_ONLN to get CPU count])])
AC_TRY_RUN([#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

int main()
{
  unsigned long page_size;
  unsigned long page_count;
  unsigned long memKB;

#if defined(_SC_PAGESIZE)
  page_size = sysconf(_SC_PAGESIZE);
#elif defined(_SC_PAGE_SIZE)
  page_size = sysconf(_SC_PAGE_SIZE);
#else
#error Need page size
#endif

  page_count = sysconf(_SC_PHYS_PAGES);

  memKB = page_count * page_size / 1024;

  printf("memKB : %ld\n", memKB);

  return 0;
}
], [AC_DEFINE(USE_SYSCONF_PHYSPAGES, 1, [Use sysconf with _SC_PAGESIZE and _SC_PHYS_PAGES to get memory size])])
AC_TRY_RUN([#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdio.h>

int main()
{
  unsigned int ncpu;
  size_t ncpu_len = sizeof(ncpu);
  int res;

  res = sysctlbyname("hw.ncpu", &ncpu, &ncpu_len, (void *) 0, 0);

  if(res != 0)
    {
      perror("sysctl");
      return 1;
    }

  printf("cpus : %d\n", ncpu);

  return 0;
}
], [AC_DEFINE(USE_SYSCTL_NCPU, 1, [Use sysctl with hw.ncpu to get CPU count])])
AC_TRY_RUN([#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdio.h>

int main()
{
  unsigned long physmem;
  size_t physmem_len = sizeof(physmem);
  int res;

  res = sysctlbyname("hw.physmem", &physmem, &physmem_len, (void *) 0, 0);

  if(res != 0)
    {
      perror("sysctl");
      return 1;
    }
  else
    {
      physmem = (physmem >> 10);
    }

  printf("memKb : %d\n", (unsigned int) physmem);

  return 0;
}
], [AC_DEFINE(USE_SYSCTL_PHYSMEM, 1, [Use sysctl with hw.physmem to get memory size])])
AC_TRY_RUN([#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdio.h>

int main()
{
  unsigned int cpumhz = 0;
  unsigned long cpumhz_l = 0;
  size_t cpumhz_len = sizeof(cpumhz);
  char *name = "hw.cpufrequency";
  int res;

  res = sysctlbyname(name, &cpumhz, &cpumhz_len, (void *) 0, 0);

  if(res != 0)
    {
      cpumhz_len = sizeof(cpumhz_l);
      res = sysctlbyname(name, &cpumhz_l, &cpumhz_len, (void *) 0, 0);
      if(res != 0)
	{
	  perror("sysctl");
	  return 1;
	}
      else
	{
	  cpumhz = (cpumhz_l / 1000);
	}
    }
  else
    {
      cpumhz /= 1000;
    }

  printf("cpuMHz : %d\n", cpumhz);

  return 0;
}
], [AC_DEFINE(USE_SYSCTL_CPUFREQ, 1, [Use sysctl with hw.cpufrequency to get CPU speed])])
AC_TRY_RUN([#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdio.h>

int main()
{
  unsigned int cpumhz = 0;
  u_int64_t cpumhz_64 = 0;
  size_t cpumhz_len = sizeof(cpumhz_64);
  char *name = "hw.cpufrequency";
  int res;

  res = sysctlbyname(name, &cpumhz_64, &cpumhz_len, (void *) 0, 0);

  if(res != 0)
    {
      perror("sysctl");
      return 1;
    }
  else if(cpumhz_64 < 1)
    {
      printf("0 CPU speed!\n");
      return 1;
    }

  cpumhz = cpumhz_64 / 1000;

  printf("cpuMHz : %d\n", cpumhz);

  return 0;
}
], [AC_DEFINE(USE_SYSCTL_CPUFREQ_INT64, 1, [Use sysctl with hw.cpufrequency to get CPU speed as a 64-bit integer])])
dnl Generate a Makefile
AC_OUTPUT(Makefile)
