# Copyright 1993 Digital Equipment Corporation.
# Distributed only by permission.
# .../talctools/Makefile
# Last modified on Tue Jun  7 11:38:41 PDT 1994 by glassman
#      modified on Mon Nov  1 11:51:47 1993 by burrows
#****************************************************************

PACKAGE = lim

PROGRAM1=lim
MANPAGE1=lim.1
PROGRAM2=pplim
MANPAGE2=pp/pplim.1

MACHINES=mips alpha

# ***************************************************************
# Entry to bring the package up to date
#    The "make all" entry should be the first real entry
#    This section also includes the entries to rebuild everything
#    in each subdirectory.  These entries will normally be the same.

all:
	cd linux; make MACHINE=kk all

debug:
	mc=`machine`; cd $$mc-debug ; make MACHINE=$$mc all

testlim:
	cd test; ./runtest

clean:
	for x in linux; do (cd $$x; make clean); done

# ***************************************************************
# Standard entries to remove files from the directories
#    tidy    -- eliminate unwanted files
#    scratch -- delete derived files in preparation for rebuild

tidy: 
	rm -f ,* .,* .emacs_[0-9]* core a.out
	for x in ${MACHINES}; do (cd $$x; make tidy); done

scratch: clean tidy

# ***************************************************************
# Standard entries provided for all packages
#
#    install -- update all files; ship to /proj/packages; unlock
#    ship    -- transfer files to /proj/packages; leave locked
#    get     -- transfer files to local directory; lock package
#    setlock -- lock package (not ordinarily used explicitly)
#    unlock  -- unlock package (not ordinarily used explicitly)
#    create  -- create new /proj/packages component and lock it
#

install:
	make all && make ship && make unlock

ship:   tidy
	shippackage ${PACKAGE} \
	  `for x in ${MACHINES}; do echo -l /proj/$$x/bin ${PROGRAM1}=$$x/${PROGRAM1} ${PROGRAM2}=$$x/${PROGRAM2}; done` \
	  `for x in ${MACHINES}; do echo -l /proj/man/$$x/man1 ${MANPAGE1} ${MANPAGE2}; done`

get:
	getpackage ${PACKAGE}

setlock:
	setlockpackage ${PACKAGE}

unlock:
	unlockpackage ${PACKAGE}

create:
	createpackage ${PACKAGE}

# *****************************************************************
