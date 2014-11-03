dnl Required initialization.

AC_INIT(vmount, 1.0)
AC_CONFIG_SRCDIR(vmount_linux.c)
AM_INIT_AUTOMAKE

dnl Checks for programs we need
AC_PROG_CC
AC_PROG_LIBTOOL

dnl Extra libraries which may be present and we need on some platforms
dnl (Solaris)

AC_SEARCH_LIBS([gethostbyname], [nsl])
AC_SEARCH_LIBS([socket], [socket], [], [AC_CHECK_LIB([socket], [socket], [LIBS="-lsocket -lnsl $LIBS"], [], [-lnsl])])

dnl Get the host type

AC_CANONICAL_HOST

dnl Choose the right source file based on the platform

case $host in
  *-linux*)         VMOUNT_SRC=vmount_linux.c ;;
  *-dec-osf*)       VMOUNT_SRC=vmount_tru64.c ;;
  *-sun-solaris*)   VMOUNT_SRC=vmount_solaris.c ;;
  *-freebsd*)       VMOUNT_SRC=vmount_freebsd.c ;;
  *-darwin*)        VMOUNT_SRC=vmount_darwin.c ;;
esac

AC_CONFIG_LINKS([vmount.c:$VMOUNT_SRC])

dnl Generate a Makefile

AC_OUTPUT(Makefile)
