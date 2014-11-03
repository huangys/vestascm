AC_INIT(vesta, 1.0)
AC_CONFIG_SRCDIR(docs/readme/README.upgrading)
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
dnl Configure sub-directories
AC_CONFIG_SUBDIRS([progs/libs/libVestaReposLeaf.a progs/libs/libVestaSRPC.a progs/libs/libBasics.a progs/libs/libpcre progs/libs/libBasicsGC.a progs/libs/libgcthrd progs/RunToolServer progs/vmount tests/libs/libVestaReposLeaf.a tests/libs/libVestaSRPC.a tests/libs/libBasics.a tests/libs/libpcre tests/libs/libBasicsGC.a tests/libs/libgcthrd])

dnl Configure sub-directories that lack autoconf inputs or that have already been processed with autoconf
( cd progs/libs/libz.a; ./configure  )
( cd tests/libs/libz.a; ./configure  )

dnl Generate Makefiles
AC_OUTPUT([progs/libs/libParseImports.a/Makefile progs/libs/libVestaRunTool.a/Makefile progs/libs/libVestaCacheClient.a/Makefile progs/libs/libVestaCacheCommon.a/Makefile progs/libs/libVestaReplicator.a/Makefile progs/libs/libVestaReposUI.a/Makefile progs/libs/libVestaReposLeaf.a/Makefile progs/libs/libVestaFP.a/Makefile progs/libs/libVestaLog.a/Makefile progs/libs/libVestaSRPC.a/Makefile progs/libs/libVestaConfig.a/Makefile progs/libs/libOS.a/Makefile progs/libs/libGenerics.a/Makefile progs/libs/libBasics.a/Makefile progs/libs/libBasicsGC.a/Makefile progs/libs/libVestaCacheServer.a/Makefile progs/vimports/Makefile progs/vupdate/Makefile progs/RunToolServer/Makefile progs/tool_launcher/Makefile progs/RunToolProbe/Makefile progs/VCacheMonitor/Makefile progs/ChkptCache/Makefile progs/FlushCache/Makefile progs/VCacheStats/Makefile progs/PrintCacheVal/Makefile progs/PrintMPKFile/Makefile progs/PrintCacheLog/Makefile progs/PrintGraphLog/Makefile progs/PrintCacheVars/Makefile progs/PrintUsedCIs/Makefile progs/PrintFVDiff/Makefile progs/PrintCallGraph/Makefile progs/VCache/Makefile progs/vrepl/Makefile progs/vmaster/Makefile progs/vglob/Makefile progs/vcheckagreement/Makefile progs/vadvance/Makefile progs/vattrib/Makefile progs/vbranch/Makefile progs/vcheckin/Makefile progs/vcheckout/Makefile progs/vcreate/Makefile progs/vmkdir/Makefile progs/vrm/Makefile progs/vwhohas/Makefile progs/vlatest/Makefile progs/vid/Makefile progs/vaccessrefresh/Makefile progs/vfindmaster/Makefile progs/vfindany/Makefile progs/vmeasure/Makefile progs/repository/Makefile progs/vreposmonitor/Makefile progs/vcollapsebase/Makefile progs/vappendlog/Makefile progs/vdumplog/Makefile progs/vdebuglog/Makefile progs/vundebuglog/Makefile progs/vgetconfig/Makefile progs/vestaeval/Makefile progs/PickleStats/Makefile progs/CountShortIds/Makefile progs/ShortIdSetDiff/Makefile progs/VestaWeed/Makefile progs/QuickWeed/Makefile progs/PrintWeederVars/Makefile progs/vmount/Makefile tests/libs/libParseImports.a/Makefile tests/libs/libVestaRunTool.a/Makefile tests/libs/libVestaCacheClient.a/Makefile tests/libs/libVestaCacheCommon.a/Makefile tests/libs/libVestaReplicator.a/Makefile tests/libs/libVestaReposUI.a/Makefile tests/libs/libVestaReposLeaf.a/Makefile tests/libs/libVestaFP.a/Makefile tests/libs/libVestaLog.a/Makefile tests/libs/libVestaSRPC.a/Makefile tests/libs/libVestaConfig.a/Makefile tests/libs/libOS.a/Makefile tests/libs/libGenerics.a/Makefile tests/libs/libBasics.a/Makefile tests/libs/libBasicsGC.a/Makefile tests/libs/libVestaCacheServer.a/Makefile tests/TestParseImports/Makefile tests/TestCacheSRPC/Makefile tests/TestBitVector/Makefile tests/TestCompactFV/Makefile tests/TestPKPrefix/Makefile tests/TestPrefixTbl/Makefile tests/TestTimer/Makefile tests/TestCache/Makefile tests/TestCacheRandom/Makefile tests/TestFlushQueue/Makefile tests/TestIntIntTblLR/Makefile tests/prev_ver/Makefile tests/TestReposUIPath/Makefile tests/TestShortId/Makefile tests/TestLongId/Makefile tests/TestVDirSurrogate/Makefile tests/TestVSStream/Makefile tests/TestVSStreamPerf/Makefile tests/TestFPFile/Makefile tests/TestFP/Makefile tests/TestFPPerf/Makefile tests/TestUniqueId/Makefile tests/TestFPTable/Makefile tests/TestFPStream/Makefile tests/TestLog/Makefile tests/TestBigLog/Makefile tests/TestNullSRPC/Makefile tests/TestClient/Makefile tests/TestServer/Makefile tests/TestCharsSeq/Makefile tests/TestBigXfer/Makefile tests/TestCharsSeqBug/Makefile tests/TestLimService/Makefile tests/TestLimServiceGC/Makefile tests/TestConfig/Makefile tests/TestAF/Makefile tests/TestFullDisk/Makefile tests/TestOS/Makefile tests/TestFdStreams/Makefile tests/TestFSMethods/Makefile tests/TestThreadIO/Makefile tests/TestRealpath/Makefile tests/TestAtom/Makefile tests/TestIntSTbl/Makefile tests/TestIntSeq/Makefile tests/TestTextSeq/Makefile tests/TestIntTbl/Makefile tests/TestText/Makefile tests/TestTextGC/Makefile tests/TestAllocator/Makefile tests/TestAllocatorGC/Makefile tests/TestThread/Makefile tests/TestSizes/Makefile tests/TestRegExp/Makefile tests/TestBufStream/Makefile Makefile])

