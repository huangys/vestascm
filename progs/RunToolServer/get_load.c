/*
** Copyright (C) 2003, Scott Venier
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
  The majority of the code in this file was lifted from the xload
  sources.  Some modifications were made in October 2003 by Scott
  Venier to adapt it for use in Vesta's RunToolServer.  The above
  copyright covers those changes.  As the original license
  specifically allows sub-licensing, this version is distributed under
  the LGPL, just like the rest of Vesta.
*/

/* $XdotOrg: xc/programs/xload/get_load.c,v 1.2 2004/04/23 19:54:57 eich Exp $ */
/* $XConsortium: get_load.c /main/37 1996/03/09 09:38:04 kaleb $ */
/* $XFree86: xc/programs/xload/get_load.c,v 1.21tsi Exp $ */
/*

Copyright (c) 1989  X Consortium

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE X CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of the X Consortium shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from the X Consortium.

*/

/*
 * get_load - get system load
 *
 * Authors:  Many and varied...
 *
 * Call InitLoadPoint() to initialize.
 * GetLoadPoint() is a callback for the StripChart widget.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if defined(__SVR4) && !defined(SVR4)
#define SVR4
#endif

#if defined(__CYGWIN__)
#include <windows.h>
typedef struct {
  DWORD stat;
  union {
    LONG vLong;
    double vDouble;
    LONGLONG vLongLong;
    void *string;
  } u;
} COUNTER;
static HANDLE query;
static HANDLE counter;
static HINSTANCE hdll;
static long (__stdcall *pdhopen)(LPCSTR, DWORD, HANDLE);
static long (__stdcall *pdhaddcounter)(HANDLE, LPCSTR, DWORD, HANDLE*);
static long (__stdcall *pdhcollectquerydata)(HANDLE);
static long (__stdcall *pdhgetformattedcountervalue)(HANDLE, DWORD, LPDWORD, COUNTER*);
#define CYGWIN_PERF 
void InitLoadPoint()
{
  long ret;
  hdll=LoadLibrary("pdh.dll");
  if (!hdll) exit(-1);
  pdhopen=(void*)GetProcAddress(hdll, "PdhOpenQueryA");
  if (!pdhopen) exit(-1);
  pdhaddcounter=(void*)GetProcAddress(hdll, "PdhAddCounterA");
  if (!pdhaddcounter) exit(-1);
  pdhcollectquerydata=(void*)GetProcAddress(hdll, "PdhCollectQueryData");
  if (!pdhcollectquerydata) exit(-1);
  pdhgetformattedcountervalue=(void*)GetProcAddress(hdll, "PdhGetFormattedCounterValue");
  if (!pdhgetformattedcountervalue) exit(-1);
  ret = pdhopen( NULL , 0, &query );
  if (ret!=0) exit(-1);
  ret = pdhaddcounter(query, "\\Processor(_Total)\\% Processor Time", 0, &counter);
  if (ret!=0) exit(-1);  
}
void GetLoadPoint( w, closure, call_data )      /* SYSV386 version */
     Widget  w;              /* unused */
     XtPointer       closure;        /* unused */
     XtPointer       call_data;      /* pointer to (double) return value */
{
  double *loadavg = (double *)call_data;
  COUNTER fmtvalue;
  long ret;
  *loadavg = 0.0;
  ret = pdhcollectquerydata(query);
  if (ret!=0) return;
  ret = pdhgetformattedcountervalue(counter, 0x200, NULL, &fmtvalue);
  if (ret!=0) return;
  *loadavg = (fmtvalue.u.vDouble-0.01)/100.0;
}
#else


#if !defined(DGUX)
#if defined(att) || defined(QNX4)
#define LOADSTUB
#endif

#ifndef macII
#ifndef apollo
#ifndef LOADSTUB
#if !defined(linux) && !defined(__UNIXOS2__) && !defined(__GLIBC__)
#include <nlist.h>
#endif /* !linux && ... */
#endif /* LOADSTUB */
#endif /* apollo */
#endif /* macII */

#if defined(MOTOROLA) && defined(SYSV)
#include <sys/sysinfo.h>
#endif

#ifdef sun
#    include <sys/param.h>
#    if defined(SVR4) || defined(__SVR4)
# 	define HAVE_LIBKSTAT 1
#    endif
#    ifdef HAVE_LIBKSTAT
#	include <kstat.h>
#	include <errno.h>
#    elif defined(i386) && !defined(SVR4)
#        include <kvm.h>
#        define	KVM_ROUTINES
#    endif /* i386 */
#endif

#ifdef CSRG_BASED
#include <sys/param.h>
#endif

#if defined(umips) || (defined(ultrix) && defined(mips))
#include <sys/fixpoint.h>
#endif

#if  defined(CRAY) || defined(AIXV3)
#include <sys/param.h>
#define word word_t
#include <sys/sysinfo.h>
#undef word
#undef n_type
#define n_type n_value
#endif	/* CRAY */

#ifdef sequent
#include <sys/vm.h>
#endif /* sequent */

#ifdef macII
#include <a.out.h>
#include <sys/var.h>
#define X_AVENRUN 0
#define fxtod(i) (vec[i].high+(vec[i].low/65536.0))
struct lavnum {
    unsigned short high;
    unsigned short low;
};
#endif /* macII */

#ifdef hcx
#include <sys/param.h>
#endif /* hcx */

#if defined(UTEK) || defined(alliant) || (defined(MOTOROLA) && defined(SVR4))
#define FSCALE	100.0
#endif

#ifdef sequent
#define FSCALE	1000.0
#endif

#ifdef sgi
#define FSCALE	1024.0
#endif

#if defined(sony) && OSMAJORVERSION == 4
#ifdef mips
#include <sys/fixpoint.h>
#else
#include <sys/param.h>
#endif
#endif

#ifdef __osf__
/*
 * Use the table(2) interface; it doesn't require setuid root.
 *
 * Select 0, 1, or 2 for 5, 30, or 60 second load averages.
 */
#ifndef WHICH_AVG
#define WHICH_AVG 1
#endif
#include <sys/table.h>
#endif

#ifdef SVR4
#ifndef FSCALE
#define FSCALE	(1 << 8)
#endif
#endif

#ifdef X_NOT_POSIX
extern long lseek();
#endif

#ifdef apollo
#include <apollo/base.h>
#include <apollo/time.h>
typedef struct {
	short		state;		/* ready, waiting, etc. */
	pinteger	usr;		/* user sr */
	linteger	upc;		/* user pc */
	linteger	usp;		/* user stack pointer */
	linteger	usb;		/* user sb ptr (A6) */
	time_$clock_t	cpu_total;	/* cumulative cpu used by process */
	unsigned short	priority;	/* process priority */
    } proc1_$info_t;

void proc1_$get_cput(
	time_$clock_t	*cput
);

void proc1_$get_info(
	short		&pid,
	proc1_$info_t	*info,
	status_$t	*sts
);

static int     lastNullCpu;
static int     lastClock;

void InitLoadPoint(void)				/* Apollo version */
{
     time_$clock_t  timeNow;
     proc1_$info_t  info;
     status_$t      st;

     proc1_$get_info( (short) 2, &info, &st );
     time_$clock( &timeNow );

     lastClock = timeNow.low32;
     lastNullCpu = info.cpu_total.low32;
}

/* ARGSUSED */
double GetLoadPoint(void) 	/* Apollo version */
{
     time_$clock_t  timeNow;
     double         temp;
     proc1_$info_t  info;
     status_$t      st;
     double	    load_average;

     proc1_$get_info( (short) 2, &info, &st );
     time_$clock( &timeNow );

     temp = info.cpu_total.low32 - lastNullCpu;
     load_average = 1.0 - temp / (timeNow.low32 - lastClock);

     lastClock = timeNow.low32;
     lastNullCpu = info.cpu_total.low32;
     return load_average;
}
#else /* not apollo */
#if defined(SYSV) && defined(i386)
/*
 * inspired by 'avgload' by John F. Haugh II
 */
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/immu.h>
#include <sys/region.h>
#include <sys/var.h>
#include <sys/proc.h>
#define KERNEL_FILE "/unix"
#define KMEM_FILE "/dev/kmem"
#define VAR_NAME "v"
#define PROC_NAME "proc"
#define BUF_NAME "buf"
#define DECAY 0.8
struct nlist namelist[] = {
  {VAR_NAME},
  {PROC_NAME},
  {BUF_NAME},
  {0},
};

static int kmem;
static struct var v;
static struct proc *p;
static XtPointer first_buf, last_buf;
static int error;

void InitLoadPoint(void)				/* SYSV386 version */
{
    int i;

    nlist( KERNEL_FILE, namelist);

    for (i=0; namelist[i].n_name; i++) 
	if (namelist[i].n_value == 0) {
	    error = 1;
	    return;
	}

    if ((kmem = open(KMEM_FILE, O_RDONLY)) < 0) {
	error = 1;
	return;
    }

    if (lseek(kmem, namelist[0].n_value, 0) == -1) {
	error = 1;
	return;
    }

    if (read(kmem, &v, sizeof(v)) != sizeof(v)) {
	error = 1;
	return;
    }

    if ((p=(struct proc *)malloc(v.v_proc*sizeof(*p))) == NULL) {
	error = 1;
	return;
    }
	  
    first_buf = (XtPointer) namelist[2].n_value;
    last_buf  = (char *)first_buf + v.v_buf * sizeof(struct buf);
    error = 0;
}
	
/* ARGSUSED */
double GetLoadPoint(void)	/* SYSV386 version */
{
    static double avenrun = 0.0;
    int i, nproc, size;
	
    if(error)
	return -1.0;
    (void) lseek(kmem, namelist[0].n_value, 0);
    (void) read(kmem, &v, sizeof(v));

    size = (struct proc *)v.ve_proc - (struct proc *)namelist[1].n_value;

    (void) lseek(kmem, namelist[1].n_value, 0);
    (void) read(kmem, p, size * sizeof(struct proc));

    for (nproc = 0, i=0; i<size; i++) 
	  if ((p[i].p_stat == SRUN) ||
	      (p[i].p_stat == SIDL) ||
	      (p[i].p_stat == SXBRK) ||
	      (p[i].p_stat == SSLEEP && (p[i].p_pri < PZERO) &&
	       (p[i].p_wchan >= (char *)first_buf) && (p[i].p_wchan < (char *)last_buf)))
	    nproc++;

    /* update the load average using a decay filter */
    avenrun = DECAY * avenrun + nproc * (1.0 - DECAY);

    return avenrun;
}
#else /* not (SYSV && i386) */
#ifdef KVM_ROUTINES
/*
 *	Sun 386i Code - abstracted to see the wood for the trees
 */

static struct nlist nl[2];
static kvm_t *kd;
static int error;

void
InitLoadPoint(void)					/* Sun 386i version */
{
    kd = kvm_open("/vmunix", NULL, NULL, O_RDONLY, "Load Widget");
    if (kd == (kvm_t *)0) {
	error = 1;
	return;
    }
	
    nl[0].n_name = "avenrun";
    nl[1].n_name = NULL;
	
    if (kvm_nlist(kd, nl) != 0) {
	error = 1;
	return;
    }
    
    if (nl[0].n_value == 0) {
	error = 1;
	return;
    }

    error = 0;
}

/* ARGSUSED */
double
GetLoadPoint(void) 		/* Sun 386i version */
{
    long	temp;

    if(error)
	return -1.0;
    if (kvm_read(kd, nl[0].n_value, (char *)&temp, sizeof (temp)) != 
	sizeof (temp)) {
      error = 1;
    }
    if(error)
      return -1.0;
    return (double)temp/FSCALE;
}
#else /* not KVM_ROUTINES */

#if defined(linux) || (defined(__FreeBSD_kernel__) && defined(__GLIBC__))

void InitLoadPoint(void)
{
      return;
}

double GetLoadPoint(void)
{
      static int fd = -1;
      double load_average;
      int n;
      char buf[10] = {0, };

      if (fd < 0)
      {
              if (fd == -2 ||
                  (fd = open("/proc/loadavg", O_RDONLY)) < 0)
              {
                      fd = -2;
                      return -1.0;
              }
      }
      else
              lseek(fd, 0, 0);

      if ((n = read(fd, buf, sizeof(buf)-1)) > 0) {
	  if (sscanf(buf, "%lf", &load_average) == 1)
	      return load_average;
      }

      return -1.0; /* temporary hiccup */
}

#else /* linux */

#ifdef __GNU__

#include <mach.h>

static processor_set_t default_set;
static int error;

void InitLoadPoint(void)
{
  if (processor_set_default (mach_host_self (), &default_set) != KERN_SUCCESS)
    error = 1;
}

/* ARGSUSED */
double GetLoadPoint(void)
{
  host_t host;
  struct processor_set_basic_info info;
  unsigned info_count;

  if(error)
      return -1.0;
  info_count = PROCESSOR_SET_BASIC_INFO_COUNT;
  if (processor_set_info (default_set, PROCESSOR_SET_BASIC_INFO, &host,
			  (processor_set_info_t) &info, &info_count)
      != KERN_SUCCESS)
    {
      InitLoadPoint(void);
      info.load_average = 0;
    }

  return info.load_average * 1000 / LOAD_SCALE;
}

#else /* __GNU__ */

#if defined(__DARWIN__) || (defined(__MACH__) && defined(__APPLE__))

#include <mach/mach.h>

static mach_port_t host_priv_port;
static int error;

void InitLoadPoint(void)
{
    host_priv_port = mach_host_self();
}

double GetLoadPoint(void)
{
    struct host_load_info load_data;
    int host_count;
    kern_return_t kr;

    host_count = sizeof(load_data)/sizeof(integer_t);
    kr = host_statistics(host_priv_port, HOST_LOAD_INFO,
                        (host_info_t)&load_data, &host_count);
    if (kr != KERN_SUCCESS)
      error = 1;
    if(error)
      return -1.0;

    return (double)load_data.avenrun[0]/LOAD_SCALE;
}

#else /* __DARWIN__ */

#ifdef LOADSTUB

void InitLoadPoint(void)
{
}

/* ARGSUSED */
double GetLoadPoint(void)
{
	return -1.0;
}

#else /* not LOADSTUB */

#ifdef __osf__

void InitLoadPoint(void)
{
}

/*ARGSUSED*/
double GetLoadPoint(void)
{
    struct tbl_loadavg load_data;

    if (table(TBL_LOADAVG, 0, (char *)&load_data, 1, sizeof(load_data)) < 0)
	return -1.0;
    return (load_data.tl_lscale == 0) ?
	load_data.tl_avenrun.d[WHICH_AVG] :
	load_data.tl_avenrun.l[WHICH_AVG] / (double)load_data.tl_lscale;
}

#else /* not __osf__ */

#ifdef __QNXNTO__
#include <time.h>
#include <sys/neutrino.h>
static _Uint64t          nto_idle = 0, nto_idle_last = 0;
static  int       nto_idle_id;
static  struct timespec nto_now, nto_last;

void
InitLoadPoint(void)
{
  nto_idle_id = ClockId(1, 1); /* Idle thread */
  ClockTime(nto_idle_id, NULL, &nto_idle_last);
  clock_gettime( CLOCK_REALTIME, &nto_last);
}

/* ARGSUSED */
double
GetLoadPoint(void)           /* QNX NTO version */
{
    double timediff;
    double temp = 0.0;

    ClockTime(nto_idle_id, NULL, &nto_idle);
    clock_gettime( CLOCK_REALTIME, &nto_now);
    timediff = 1000000000.0 * (nto_now.tv_sec - nto_last.tv_sec)
               + (nto_now.tv_nsec - nto_last.tv_nsec);
    temp = 1.0 - (nto_idle-nto_idle_last)/timediff;
    nto_idle_last = nto_idle;
    nto_last = nto_now;
    return temp >= 0 ? temp : 0;
}
#else /* not __QNXNTO__ */

#ifdef __bsdi__
#include <kvm.h>

static struct nlist nl[] = {
  { "_averunnable" },
#define X_AVERUNNABLE 0
  { "_fscale" },
#define X_FSCALE      1
  { "" },
};
static kvm_t *kd;
static int fscale;
static int error;

void InitLoadPoint(void)
{
  fixpt_t averunnable[3];  /* unused really */

  if ((kd = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, NULL)) == NULL) {
    error = 1;
    return;
  }

  if (kvm_nlist(kd, nl) != 0) {
    error = 1;
    return;
  }

  if (kvm_read(kd, (off_t)nl[X_AVERUNNABLE].n_value, (char *)averunnable,
	       sizeof(averunnable)) != sizeof(averunnable)) {
    error = 1;
    return;
  }

  if (kvm_read(kd, (off_t)nl[X_FSCALE].n_value, (char *)&fscale,
	       sizeof(fscale)) != sizeof(fscale)) {
    error = 1;
    return;
  }

  error = 0;
  return;
}

double GetLoadPoint(void)
{
  fixpt_t t;

  if(error)
    return -1.0;
  if (kvm_read(kd, (off_t)nl[X_AVERUNNABLE].n_value, (char *)&t,
	       sizeof(t)) != sizeof(t))
    return -1.0

  return (double)t/fscale;
}

#else /* not __bsdi__ */
#if (defined(BSD) && (BSD >= 199306)) || defined(__FreeBSD__)
#include <stdlib.h>

void InitLoadPoint(void)
{
}

double GetLoadPoint(void)
{
  double loadavg;

  if (getloadavg(&loadavg, 1) < 0) 
    return -1.0;

  return loadavg;
}

#else /* not BSD >= 199306 */
#if defined(sun) && defined(HAVE_LIBKSTAT)

static kstat_t		*ksp;
static kstat_ctl_t	*kc;
static int error;

void
InitLoadPoint(void)
{
	if ((kc = kstat_open()) == NULL)
	  error = 1;

	if ((ksp = kstat_lookup(kc, "unix", 0, "system_misc")) == NULL)
	  error = 1;

	error = 0;
}

double
GetLoadPoint(void)
{
	kstat_named_t *vp;
	double loadavg;

	if(error)
	    return -1.0;

	if (kstat_read(kc, ksp, NULL) == -1)
	  error = 1;

	if ((vp = kstat_data_lookup(ksp, "avenrun_1min")) == NULL)
	  error = 1;

	if(error)
	    return -1.0;

	loadavg = (double)vp->value.l / FSCALE;
	return loadavg;
}
#else /* not Solaris */

#ifndef KMEM_FILE
#define KMEM_FILE "/dev/kmem"
#endif

#ifndef KERNEL_FILE

#ifdef alliant
#define KERNEL_FILE "/vmunix"
#endif /* alliant */

#ifdef CRAY
#define KERNEL_FILE "/unicos"
#endif /* CRAY */

#ifdef hpux
#define KERNEL_FILE "/hp-ux"
#endif /* hpux */

#ifdef macII
#define KERNEL_FILE "/unix"
#endif /* macII */

#ifdef umips
# ifdef SYSTYPE_SYSV
# define KERNEL_FILE "/unix"
# else
# define KERNEL_FILE "/vmunix"
# endif /* SYSTYPE_SYSV */
#endif /* umips */

#ifdef sequent
#define KERNEL_FILE "/dynix"
#endif /* sequent */

#ifdef hcx
#define KERNEL_FILE "/unix"
#endif /* hcx */

#ifdef MOTOROLA
#if defined(SYSV) && defined(m68k)
#define KERNEL_FILE "/sysV68"
#endif
#if defined(SYSV) && defined(m88k)
#define KERNEL_FILE "/unix"
#endif
#ifdef SVR4
#define KERNEL_FILE "/unix"
#endif
#endif /* MOTOROLA */

#if defined(sun) && defined(SVR4)
#define KERNEL_FILE "/kernel/unix"
#endif

#ifdef sgi
#if (OSMAJORVERSION > 4)
#define KERNEL_FILE "/unix"
#endif
#endif

/*
 * provide default for everyone else
 */
#ifndef KERNEL_FILE
#ifdef SVR4
#define KERNEL_FILE "/stand/unix"
#else
#ifdef SYSV
#define KERNEL_FILE "/unix"
#else
/* If a BSD system, check in <paths.h> */
#   ifdef BSD
#    include <paths.h>
#    ifdef _PATH_UNIX
#     define KERNEL_FILE _PATH_UNIX
#    else
#     ifdef _PATH_KERNEL
#      define KERNEL_FILE _PATH_KERNEL
#     else
#      define KERNEL_FILE "/vmunix"
#     endif
#    endif
#   else /* BSD */
#    define KERNEL_FILE "/vmunix"
#   endif /* BSD */
#endif /* SYSV */
#endif /* SVR4 */
#endif /* KERNEL_FILE */
#endif /* KERNEL_FILE */


#ifndef KERNEL_LOAD_VARIABLE
#    if defined(BSD) && (BSD >= 199103)
#        define KERNEL_LOAD_VARIABLE "_averunnable"
#    endif /* BSD >= 199103 */

#    ifdef alliant
#        define KERNEL_LOAD_VARIABLE "_Loadavg"
#    endif /* alliant */

#    ifdef CRAY
#        if defined(CRAY2) && OSMAJORVERSION == 4
#            define KERNEL_LOAD_VARIABLE "avenrun"
#        else
#            define KERNEL_LOAD_VARIABLE "sysinfo"
#            define SYSINFO
#        endif /* defined(CRAY2) && OSMAJORVERSION == 4 */
#    endif /* CRAY */

#    ifdef hpux
#        ifdef __hp9000s800
#            define KERNEL_LOAD_VARIABLE "avenrun"
#        endif /* hp9000s800 */
#    endif /* hpux */

#    ifdef umips
#        ifdef SYSTYPE_SYSV
#            define KERNEL_LOAD_VARIABLE "avenrun"
#        else
#            define KERNEL_LOAD_VARIABLE "_avenrun"
#        endif /* SYSTYPE_SYSV */
#    endif /* umips */

#    ifdef sgi
#	 define KERNEL_LOAD_VARIABLE "avenrun"
#    endif /* sgi */

#    ifdef AIXV3
#        define KERNEL_LOAD_VARIABLE "sysinfo"
#    endif /* AIXV3 */

#    ifdef MOTOROLA
#        if defined(SYSV) && defined(m68k)
#            define KERNEL_LOAD_VARIABLE "sysinfo"
#        endif
#        if defined(SYSV) && defined(m88k)
#            define KERNEL_LOAD_VARIABLE "_sysinfo"
#        endif
#        ifdef SVR4
#            define KERNEL_LOAD_VARIABLE "avenrun"
#        endif
#    endif /* MOTOROLA */

#endif /* KERNEL_LOAD_VARIABLE */

/*
 * provide default for everyone else
 */

#ifndef KERNEL_LOAD_VARIABLE
#    ifdef USG
#        define KERNEL_LOAD_VARIABLE "sysinfo"
#        define SYSINFO
#    else
#    ifdef SVR4
#        define KERNEL_LOAD_VARIABLE "avenrun"
#    else
#        define KERNEL_LOAD_VARIABLE "_avenrun"
#    endif
#    endif
#endif /* KERNEL_LOAD_VARIABLE */

#ifdef macII
static struct var v;
static int pad[2];	/* This padding is needed if xload compiled on */
			/* a/ux 1.1 is executed on a/ux 1.0, because */
			/* the var structure had too much padding in 1.0, */
			/* so the 1.0 kernel writes past the end of the 1.1 */
			/* var structure in the uvar() call. */
static struct nlist nl[2];
static struct lavnum vec[3];
#else /* not macII */
static struct nlist namelist[] = {	    /* namelist for vmunix grubbing */
#define LOADAV 0
    {KERNEL_LOAD_VARIABLE},
    {0}
};
#endif /* macII */

static int kmem;
static long loadavg_seek;
static int error;

void InitLoadPoint(void)
{
#ifdef macII
    extern nlist();

    int i;

    strcpy(nl[0].n_name, "avenrun");
    nl[1].n_name[0] = '\0';

    kmem = open(KMEM_FILE, O_RDONLY);
    if (kmem < 0) {
	error = 1;
	return;
    }

    uvar(&v);

    if (nlist( KERNEL_FILE, nl) != 0) {
	error = 1;
	return;
    }
    for (i = 0; i < 2; i++) {
	nl[i].n_value = (int)nl[i].n_value - v.v_kvoffset;
    }
#else /* not macII */
#if !defined(SVR4) && !defined(__SVR4) && !defined(sgi) && !defined(MOTOROLA) && !defined(AIXV5) && !(BSD >= 199103) && !defined(__FreeBSD__)
    extern void nlist();
#endif

#ifdef AIXV3
    knlist( namelist, 1, sizeof(struct nlist));
#else	
    nlist( KERNEL_FILE, namelist);
#endif
    /*
     * Some systems appear to set only one of these to Zero if the entry could
     * not be found, I hope no_one returns Zero as a good value, or bad things
     * will happen to you.  (I have a hard time believing the value will
     * ever really be zero anyway).   CDP 5/17/89.
     */
#ifdef hcx
    if (namelist[LOADAV].n_type == 0 &&
#else
    if (namelist[LOADAV].n_type == 0 ||
#endif /* hcx */
	namelist[LOADAV].n_value == 0) {
	error = 1;
	return;
    }
    loadavg_seek = namelist[LOADAV].n_value;
#if defined(umips) && defined(SYSTYPE_SYSV)
    loadavg_seek &= 0x7fffffff;
#endif /* umips && SYSTYPE_SYSV */
#if (defined(CRAY) && defined(SYSINFO))
    loadavg_seek += ((char *) (((struct sysinfo *)NULL)->avenrun)) -
	((char *) NULL);
#endif /* CRAY && SYSINFO */
    kmem = open(KMEM_FILE, O_RDONLY);
    if (kmem < 0) error = 1;
#endif /* macII else */
}

/* ARGSUSED */
double GetLoadPoint(void)
{
	if(error)
	    return -1.0;
#ifdef macII
	lseek(kmem, (long)nl[X_AVENRUN].n_value, 0);
#else
	(void) lseek(kmem, loadavg_seek, 0);
#endif

#if defined(sun) || defined (UTEK) || defined(sequent) || defined(alliant) || defined(SVR4) || defined(sgi) || defined(hcx) || (BSD >= 199103)
	{
		long temp;
		(void) read(kmem, (char *)&temp, sizeof(long));
		return (double)temp/FSCALE;
	}
#else /* else not sun or UTEK or sequent or alliant or SVR4 or sgi or hcx */
# ifdef macII
        {
                read(kmem, vec, 3*sizeof(struct lavnum));
                return fxtod(0);
        }
# else /* else not macII */
#  if defined(umips) || (defined(ultrix) && defined(mips))
	{
		fix temp;
		(void) read(kmem, (char *)&temp, sizeof(fix));
		return FIX_TO_DBL(temp);
	}
#  else /* not umips or ultrix risc */
#    ifdef AIXV3
        {
	  double loadavg = -1.0;
          struct sysinfo sysinfo_now;
          struct sysinfo sysinfo_last;
          static firsttime = TRUE;
          static double runavg = 0.0, swpavg = 0.0;

          (void) lseek(kmem, loadavg_seek, 0);
          (void) read(kmem, (char *)&sysinfo_last, sizeof(struct sysinfo));
          if (firsttime)
            {
              loadavg = 0.0;
              firsttime = FALSE;
            }
          else
            {
              sleep(1);
              (void) lseek(kmem, loadavg_seek, 0);
              (void) read(kmem, (char *)&sysinfo_now, sizeof(struct sysinfo));
              runavg *= 0.8; swpavg *= 0.8;
              if (sysinfo_now.runocc != sysinfo_last.runocc)
                runavg += 0.2*((sysinfo_now.runque - sysinfo_last.runque - 1)
                          /(double)(sysinfo_now.runocc - sysinfo_last.runocc));
              if (sysinfo_now.swpocc != sysinfo_last.swpocc)
                swpavg += 0.2*((sysinfo_now.swpque - sysinfo_last.swpque)
                          /(double)(sysinfo_now.swpocc - sysinfo_last.swpocc));
              loadavg = runavg + swpavg;
              sysinfo_last = sysinfo_now;
            }
          /* otherwise we leave load alone. */
	  return loadavg;
        }
#    else /* not AIXV3 */
#      if defined(MOTOROLA) && defined(SYSV)
	{
        static int init = 0;
        static kmem;
        static long loadavg_seek;

#define CEXP    0.25            /* Constant used for load averaging */

        struct sysinfo sysinfod;
        static double oldloadavg;
        static double cexp = CEXP;
        static long sv_rq, sv_oc;   /* save old values */
        double rq, oc;              /* amount values have changed */

        if (!init)
        {
            if (nlist(KERNEL_FILE,namelist) == -1)
            {
		error = 1;
            }
            loadavg_seek = namelist[0].n_value;

            kmem = open(KMEM_FILE, O_RDONLY);
            if (kmem < 0)
            {
		error = 1;
            }
        }

        lseek(kmem, loadavg_seek, 0);
        if (read(kmem, &sysinfod, (int) sizeof (struct sysinfo)) == -1)
        {
             return -1.0;
        }

        if (!init)
        {
            init = 1;
            sv_rq = sysinfod.runque;
            sv_oc = sysinfod.runocc;
            oldloadavg = 0.0;
            return oldloadavg;
        }
        /*
         * calculate the amount the values have
         * changed since last update
         */
        rq = (double) sysinfod.runque - sv_rq;
        oc = (double) sysinfod.runocc - sv_oc;

        /*
         * save old values for next time
         */
        sv_rq = sysinfod.runque;
        sv_oc = sysinfod.runocc;

        if (oc == 0.0)          /* avoid divide by zero  */
        {
                oldloadavg = (1.0 - cexp) * oldloadavg;

        }
        else
        {
                oldloadavg = ((1.0 - cexp) * oldloadavg) + ((rq / oc) * cexp);
        }
        return oldloadavg;
	}
#      else /* not MOTOROLA */
#     if defined(sony) && OSMAJORVERSION == 4
#      ifdef mips
	{
		fix temp;
		(void) read(kmem, (char *)&temp, sizeof(fix));
		return FIX_TO_DBL(temp);
	}
#      else /* not mips */
	{
		long temp;
		(void) read(kmem, (char *)&temp, sizeof(long));
		return (double)temp/FSCALE;
	}
#      endif /* mips */
#     else /* not sony NEWSOS4 */
	(void) read(kmem, (char *)loadavg, sizeof(double));
#      endif /* sony NEWOS4 */
#     endif /* MOTOROLA else */
#    endif /* AIXV3 else */
#  endif /* umips else */
# endif /* macII else */
#endif /* sun or SVR4 or ... else */	
	return -1.0;
}
#endif /* sun else */
#endif /* BSD >= 199306 else */
#endif /* __bsdi__ else */
#endif /* __QNXNTO__ else */
#endif /* __osf__ else */
#endif /* LOADSTUB else */
#endif /* __DARWIN__ else */
#endif /* __GNU__ else */
#endif /* linux else */
#endif /* KVM_ROUTINES else */
#endif /* SYSV && i386 else */

#endif /* apollo else */

#else /* !DGUX */

/* INTEL DGUX Release 4.20MU04
 * Copyright 1999 Takis Psarogiannakopoulos
 * Cambridge, UK
 * <takis@dpmms.cam.ac.uk>
 */

#include <errno.h>
#include <nlist.h>
#include <sys/dg_sys_info.h>

static struct dg_sys_info_load_info load_info;  /* DG/ux */

#define KERNEL_FILE "/dgux"
#define LDAV_SYMBOL "_avenrun"

void InitLoadPoint(void)
{

}

double GetLoadPoint(void)
{
  double loadavg;
  if (getloadavg(&loadavg, 1) < 0)
    return -1.0;

  return loadavg;
}

#if !defined (LDAV_CVT) && defined (FSCALE)
#define LDAV_CVT(n) (((double) (n)) / FSCALE)
#endif
#if !defined(LDAV_CVT) && defined(LOAD_AVE_CVT)
#define LDAV_CVT(n) (LOAD_AVE_CVT (n) / 100.0)
#endif
#define LOAD_AVE_TYPE double
#ifndef LDAV_CVT
#define LDAV_CVT(n) ((double) (n))
#endif /* !LDAV_CVT */
static int channel;
static int getloadavg_initialized;
static long offset;
static struct nlist nl[2];


/* GETLOADAVG FUNCTION FOR DG/ux R4.20MU04 */

int
getloadavg (double loadavg[], int nelem)
{
  int elem = 0;                 /* Return value.  */
  int result =0 ;

  /* This call can return -1 for an error, but with good args
     it's not supposed to fail.  The first argument is for no
     apparent reason of type `long int *'.  */
  result = dg_sys_info ((long int *) &load_info,
		DG_SYS_INFO_LOAD_INFO_TYPE, DG_SYS_INFO_LOAD_VERSION_0);
  if ( result == -1)
  {
     return(-1);
  }
  if (nelem > 0)
    loadavg[elem++] = load_info.one_minute;
  if (nelem > 1)
    loadavg[elem++] = load_info.five_minute;
  if (nelem > 2)
    loadavg[elem++] = load_info.fifteen_minute;

  return elem;
}

#endif /* END OF DG/ux */


#endif /* END of __CYGWIN__ */
