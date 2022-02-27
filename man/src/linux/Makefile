# Copyright 1993 Digital Equipment Corporation.
# Distributed only by permission.
# Makefile for lim package
# Last modified on Mon Nov  1 11:27:47 1993 by burrows
#****************************************************************

PROGRAM1=lim
PROGRAM2=pplim

# ***************************************************************
# Variables used to parameterize Makefile operations

CPPFLAGS=-I. -I..
CFLAGS= -O -g3 ${CPPFLAGS}
YFLAGS=-d
LINTFLAGS=${CPPFLAGS}
LDFLAGS= -g
CP=cp

# ***************************************************************
# Entry to bring the package up to date
#    The "make all" entry should be the first real entry
#    This section also includes the entries to rebuild everything
#    in each subdirectory.  These entries will normally be the same.

all:  ${PROGRAM1} ${PROGRAM2}

LIBS=
CSRCS= ../atom.c ../emalloc.c ../interpret.c ../lim.c ../parsetree.c \
	../scope.c ../state.c ../stdlimrd.c ../stdlimwr.c
YSRCS= ../ylim.y
LSRCS= ../llim.l
OBJS= atom.o emalloc.o interpret.o lim.o parsetree.o scope.o state.o stdlimrd.o stdlimwr.o ylim.o llim.o

lim: ylim.o ${OBJS} ${LIBS}
	${CC} ${CFLAGS} ${OBJS} -o lim ${LIBS}  -ll 

pplim: ../pp/pplim.sh
	${CP} ../pp/pplim.sh pplim; chmod 555 pplim

atom.o: ../atom.c; ${CC} -c ${CFLAGS} ../atom.c
emalloc.o: ../emalloc.c; ${CC} -c ${CFLAGS} ../emalloc.c
interpret.o: ../interpret.c; ${CC} -c ${CFLAGS} ../interpret.c
lim.o: ../lim.c; ${CC} -c ${CFLAGS} ../lim.c
parsetree.o: ../parsetree.c; ${CC} -c ${CFLAGS} ../parsetree.c
scope.o: ../scope.c; ${CC} -c ${CFLAGS} ../scope.c
state.o: ../state.c; ${CC} -c ${CFLAGS} ../state.c
stdlimrd.o: ../stdlimrd.c; ${CC} -c ${CFLAGS} ../stdlimrd.c
stdlimwr.o: ../stdlimwr.c; ${CC} -c ${CFLAGS} ../stdlimwr.c
ylim.o y.tab.h: ../ylim.y
	sh -c 'test ! -f y.tab.h || mv -f y.tab.h x.tab.h'
	${YACC} ${YFLAGS} ../ylim.y
	sh -c 'test -f x.tab.h && cmp -s y.tab.h x.tab.h && mv -f x.tab.h y.tab.h; exit 0'
	${CC} -c ${CFLAGS} y.tab.c
	mv y.tab.o ylim.o
llim.o: ../llim.l
	${LEX} ${LFLAGS} ../llim.l
	${CC} -c ${CFLAGS} lex.yy.c
	mv lex.yy.o llim.o

lim.lint: ${CSRCS}
	lint ${LINTFLAGS} ${CSRCS}  > lim.lint

lint:  lim.lint

clean:
	rm -f  lim pplim ${OBJS} [xy].tab.[ch] lex.yy.c lim.lint 

depend:
	sh -c 'test -f y.tab.h || touch y.tab.h'
	echo "includes -a ${CPPFLAGS}  ${CSRCS} ${YSRCS} ${LSRCS}" > \
	dependfile
	# sh -c 'test -s y.tab.h || rm y.tab.h'

# include dependfile

# ***************************************************************
# Standard entries to remove files from the directories
#    tidy    -- eliminate unwanted files
#    scratch -- delete derived files in preparation for rebuild

tidy: 
	rm -f ,* .,* .emacs_[0-9]* core a.out

scratch: clean tidy
