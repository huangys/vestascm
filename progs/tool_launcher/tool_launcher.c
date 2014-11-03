/* Copyright (C) 2001, Compaq Computer Corporation
 * 
 * This file is part of Vesta.
 * 
 * Vesta is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * Vesta is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with Vesta; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


 *  ****************
 *  *  Launcher.C  *
 *  ****************
 *
 *  For security reaons, I changed this file from C++ to just C.
 *  I also no longer link in all the vesta libraries.
 *  This keeps the privlidged code down to just under 300 lines.
 *   --scott
 */

#define NO_FD -1
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>

#define SYSERROR -1
#define EXIT_SUCCESS 0

#define false 0
#define true 1
typedef int bool;

#include "Launcher.h"

static void giveup(int code, int errn);

/* class Args */
typedef struct Args_s {
  int i;
  int c;
  char **v;
} Args;

static Args a; /* initialization in parse() */

/* class Args member functions */
init_a(int i, int c, char **v) {
  a.i = i;
  a.c = c;
  a.v = v;
}
  
char *next_arg() {
  if (a.i >= a.c) giveup(missing_command, 0);
  assert(a.v[a.i]);
  return a.v[a.i++];
}

/* This is allowed to return NULL */
char **pnext_arg() {
  return &(a.v[a.i]);
}

char **pcurrent_arg() {
  assert(a.i > 0);
  return &(a.v[a.i-1]);
}

/* Global variables */

static bool verbose = false;            /* true => debugging output */
static bool testing = false;            /* true => no chroot */

static char *root = NULL;               /* directory for chroot */
static char **command = NULL;           /* command line to be executed */
static char **envvars = NULL;           /* environment variables */

static int errfd = NO_FD;               /* file descriptor from "-err fd" */
static FILE *errs = NULL;           /* stream for errfd */
static int dbgfd = NO_FD;               /* file descriptor from "-dbg fd" */
static FILE *dbgs = NULL;           /* stream for dbgfd, if verbose is true */

/* our environment */
extern char **environ;

/* Error messages */

static char *self;

char *messages[] = {
  "Missing command line switch",
  "Illegal file descriptor",
  "Incorrect root directory",
  "Missing command line for tool",
  "Can't set effective userid to real userid",
  "Can't set close-on-exec flag for error file descriptor",
  "Unrecognized command line argument",
  "Chroot to specified root directory failed",
  "Execve failure",
  "Can't get working directory",
  "Working directory outside root",
  "Couldn't chdir to working directory after chroot",
  "Couldn't mount procfs on the /proc"
};

static void giveup(int code, int errn)
{
  char *error;
  assert(missing_switch <= code && code < launcher_failure_enum_end);
  fprintf(errs, "  %s: ", self);
  fprintf(errs, "%s", messages[code-1]);
  if (errn != 0) {
    error = strerror(errn); /*make sure this didn't return NULL*/
    fprintf(errs, ", %s (errno = %d)", error?error:"", errn);
  }
  switch (code) {
  case unrecognized_switch:
    fprintf(errs, ": '%s'", *(pcurrent_arg()));
    break;
  case chroot_failure:
    fprintf(errs, "\n  Possible cause: perhaps tool_launcher isn't setuid root?");
    break;
  case execve_failure:
    fprintf(errs, "\n  Possible cause: perhaps tool pathname is invalid");
    fprintf(errs, " or file system is incomplete?");
    break;
  case getcwd_failure:
    fprintf(errs, "\n  Possible cause: WD not readable by user running");
    fprintf(errs, " the RunToolServer?");
    break;
  case wd_not_in_root:
    fprintf(errs, "\n  Possible cause: Symlink on path to volatile root");
    fprintf(errs, " mount point?");
    break;
  default:
    fprintf(errs, "\n  Possible cause: probably an implementation problem");
    break;
  }
  fflush(errs);          /* flush error messages */
  fclose(errs);           /* not really necessary; exit does this */
  exit(code);
}

static void get_fd(int *fd, /*OUT*/ FILE** f) {
  errno = 0;
  *fd = strtoul(next_arg(), (char**)NULL, 10); /*atoi() doesn't catch errors */
  if (errno != 0) giveup(bogus_fd, errno);
  *f = fdopen(*fd, "w");
  if (*f == NULL) giveup(bogus_fd, errno);
}

static void parse(int argc, char *argv[])
{
  char *arg;
  int i;
  init_a(1, argc, argv);

  arg = next_arg();

  self = argv[0];

  if (strcmp(arg, "-dbg") == 0) {
    get_fd(&dbgfd, /*OUT*/ &dbgs);
    arg = next_arg();
    verbose = true;

    /* report command line */
    fprintf(dbgs, "Tool launcher args: ");
    for (i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-cmd") == 0) break;
      fprintf(dbgs, "%s ", argv[i]);
    }
    fprintf(dbgs, "\n");
    if (i < argc) {
      fprintf(dbgs, "Command line: ");
      for (i++; i < argc; i++) {
	fprintf(dbgs, "%s ", argv[i]);
      }
      fprintf(dbgs, "\n");
    }
    fflush(dbgs);
    fclose(dbgs);
  }
  if (strcmp(arg, "-t") == 0) { testing = true; arg = next_arg(); }

  /* -err fd (required) */
  if (strcmp(arg, "-err") != 0) giveup(missing_switch, 0);
  get_fd(&errfd, &errs);
  arg = next_arg();

  /* -root root  (required) */
  if (strcmp(arg, "-root") != 0)  giveup(bogus_root_directory, 0);
  root = next_arg();
  arg = next_arg();

  /* -env envvars (optional) */
  if (strcmp(arg, "-env") == 0) {
    envvars = pnext_arg();
    arg = next_arg();
    while (strcmp(arg, "-cmd") != 0) arg = next_arg();
    /* we've found -cmd; was envvar list empty? */
    if (envvars == pcurrent_arg()) envvars = NULL;
  }
  if (strcmp(arg, "-cmd") == 0) {
    *pcurrent_arg() = NULL;    /* terminate envvars list for execve */
    command = pnext_arg();     /* might be NULL */
    return;
  }
  giveup(unrecognized_switch, 0);
}

char *safe_getcwd(char *buf, unsigned int size)
{
  char *bigger_buf = NULL, *getcwd_result = NULL;

  getcwd_result = getcwd(buf, size);

  /* If the cwd is too big to fit in the supplied buffer, allocate a
     bigger one. */
  while((getcwd_result == NULL) && (errno = ERANGE))
    {
      /* If we already allocated one that was also too small, free it. */
      if(bigger_buf != NULL) free(bigger_buf);
      /* Allocate a new, biger one. */
      size *= 2;
      bigger_buf = (char *) malloc(size);
      /* Try it again. */
      getcwd_result = getcwd(bigger_buf, size);
    }

  return getcwd_result;
}

void set_close_on_exec()
{
  int fd, max_fd;
  max_fd = getdtablesize();

  /* We skip file descriptors 0, 1, and 2 (standard input, standard
   * output, and standard error) as we want those to stay open. */
  for(fd = 3; fd < max_fd; fd++)
    {
      if((fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) &&
	 /* Ignore bad file descriptors */
	 (errno != EBADF))
	{
	  giveup(fcntl_failure, errno);
	}
    }
}

int main(int argc, char *argv[])
{
  char *cwd, cwd_buff[1024];
  char *newcwd;
  int cwd_is_root=0;
  struct stat stat_buf;
  parse(argc, argv);

  /* check that a command was specified */
  if (command == NULL || *command == NULL) {
    giveup(missing_command, 0);
  }

  /* strip off a trailing / from the new root */
  if(root[strlen(root)-1] == '/') root[strlen(root)-1] = '\0';

  /* make sure the cwd is inside the new root */
  if((cwd = safe_getcwd(cwd_buff, 1024)) == NULL) {
    giveup(getcwd_failure, errno);
  }
  if(cwd != strstr(cwd, root)) {
    giveup(wd_not_in_root, 0);
  }
  if(cwd[strlen(root)] != '/' && strlen(root) != strlen(cwd)) {
    giveup(wd_not_in_root, 0);
  }

  /* execute chroot(2) */
  if (!testing && chroot(root) == SYSERROR) {
    giveup(chroot_failure, errno);
  }

  /* chdir to make sure the cwd gets jailed
   * this probably isn't required, but I'm paranoid and it's tradition. */
  /*first, compute the new name for the cwd by chopping off the new root*/
  newcwd = cwd+strlen(root);
  /*next, if the new root is the same length as the cwd, we 'cd /' */
  cwd_is_root = strlen(root) == strlen(cwd);
  if(!testing && chdir(cwd_is_root?"/":newcwd) == SYSERROR) {
    giveup(chdir_after_chroot_failure, errno);
  }

  /* 如果 / 下有个 proc 的目录, 则将 proc 伪文件系统 mount 上.
   * 有些应用程序需要 proc 获得系统和进程信息, 如 quartus, vcs 等. */
  struct stat sb;
  if ((stat("/proc", &sb) == 0) && (S_IFDIR & sb.st_mode)) {
    if (mount("none", "/proc", "proc", MS_RDONLY, NULL) == -1) {
      giveup(mount_proc_failure, errno);
    }
  }

  /* Change effective user id back to real user id. */
  if (setuid(getuid()) == SYSERROR) {
    giveup(seteuid_failure, errno);
  }

  /* Make sure we don't leak any file descriptors from the
     RunToolServer.  Everything but the standard fds 0, 1, and 2 will
     be closed on exec. */
  set_close_on_exec();

  /* Setup the environment */
  environ = envvars;

  if (stat(command[0], &stat_buf)) {
	  /* Couln't stat(2) the file, launch it using $PATH */
	  execvp(command[0], command);     /* never returns */
  } else {
	  /* command[0] names the file directly,
	   * use the old style and just launch it */
	  execv(command[0], command);      /* never returns */
  }

  /* If we reached here, one of the exec(3) calls must have failed. */
  giveup(execve_failure, errno);

  /* keep compiler happy */
  return EXIT_SUCCESS;
}
