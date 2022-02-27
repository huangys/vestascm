/* lim.c */

#include <stdio.h>
#include <stdlib.h>
#include "atom.h"
#include "parsetree.h"
#include "scope.h"
#include "limrd.h"
#include "limwr.h"
#include "state.h"
#include "interpret.h"
#include "limlex.h"

extern FILE *yyin;

char *limprogname;

node *progtree;
node *progtreetail;

int debug = 0;

void main(int argc, char *argv[]) 
{        
        int argn = 1;
        static char dbgflg[] = "-debug=";

        if (argn < argc && strncmp (argv[argn], dbgflg, strlen (dbgflg)) == 0) {
        	debug = atoi (&argv[argn][strlen (dbgflg)]);
        	argn++;
        }

        if (argn+1 != argc) {
                fprintf(stderr, "usage: lim progfile\n"); 
		exit(2);
        }
	limprogname = argv[argn];
        yyin = fopen(limprogname, "r");
        if (yyin == NULL) {
                perror(limprogname); 
		exit(2);
        }
        progtree = (node*)0;
        progtreetail = (node*)0;
        if (yyparse()) {
                exit(2);
        }
        if (ferror(yyin)) {
                perror(limprogname); 
		exit(2);
        }
        (void) fclose(yyin);
        {
                int nglobals;
                int nmutable;
                int res;
                node *m;
                int mainframesize;
                state *s;
                if (debug & 0x2) {
                        printtree(progtree);
                        fprintf(stderr, "\n");
                }
                m = annotate(progtree, &nglobals, &nmutable, &mainframesize);
                if (debug & 0x4) {
                        printtree(progtree);
                        fprintf(stderr, "\n");
                }
                if (m == (node*)0) {
                        exit(2);
                }
                mark(progtree);
                if (debug & 0x8) {
                        printtree(progtree);
                        fprintf(stderr, "\n");
                }
                s = newstate(nglobals, nmutable, stdin, stdout);
                res = initialize(progtree, s);
                if (res) {
                        varlist novars;
                        vallist novals;
                        novars.len = 0;
                        novals.len = 0;
                        newframe(s, 0, novars, novals, mainframesize);
                        res = cp(m, s);
                }
                if (!res) {
                        limerr("guard failure", "", 0);
                        fprintf(stderr, "number of chars read = %d.\n",
                                limrd_maxread(s->input));
			limwr_seek(s->output, 0);
                }
                freestate(s);
                freetree(progtree);
                exit(!res);
        }
}

void limerr (char *msg0, char *msg1, int line)
{
	fprintf (stderr, "%s: %s%s", limprogname, msg0, msg1);
	if (line != 0) {
		fprintf (stderr, " near line %d", line);
	}
	fprintf (stderr, "\n");
}
