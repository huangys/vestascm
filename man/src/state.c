/* state.c */
#include <stdio.h>
#include <stdlib.h>
#include "emalloc.h"
#include "limrd.h"
#include "limwr.h"
#include "atom.h"
#include "parsetree.h"
#include "state.h"
#include "stdlimrd.h"
#include "stdlimwr.h"

extern void bcopy(char *from, char *to, int len);
extern void bzero(char *p, int len);

#define MAX(a,b)    ((a) > (b)? (a) : (b))

typedef struct valuestack {
        int len;
        int sp;
        value *v; 
} valuestack;

typedef struct state_rep {
        struct state pub;
        value *globals;
        int nmutables;
        valuestack stack;
        valuestack history;
        int fp;
} *state_rep;

static void save_unsigned(valuestack *st, unsigned u);
static void save_int(valuestack *st, int n);
static void save_values(valuestack *st, value *v, int vlen);
static void restore_values(valuestack *st, value *v, int vlen);
static void discard_values(valuestack *st, int vlen);
static unsigned pop_unsigned(valuestack *st);
static unsigned pop_int(valuestack *st);

void initstack(valuestack *st)
{
        st->len = 100;
        st->sp = 0;
        st->v = (value *) emalloc(st->len * sizeof(value));
}

void extendstack(valuestack *st, int n)
{
        if (n > st->len) {
                st->len = MAX(2 * st->len, n);
                st->v = (value*) erealloc((char*) st->v, st->len * sizeof (value));
        }
}

state *newstate(int nglobals, int nmutables, FILE *in, FILE *out)
{
        state_rep res = (state_rep) emalloc(sizeof(struct state_rep));
        res->nmutables = nmutables;
        res->globals = (value*) emalloc(sizeof(value)*nglobals);
        initstack(&res->stack);
        initstack(&res->history);
        res->fp = 0;
        res->pub.input = rdfromfile(in);
        res->pub.output = wrfromfile(out);
        return (&res->pub);
}

void freestate(state *s) 
{
        state_rep srep = (state_rep) s;
        limwr_close(srep->pub.output);
        limrd_close(srep->pub.input);
        free((char *) srep->globals);
        free((char *) srep->stack.v);
        free((char *) srep->history.v);
        free((char *) s);
}

void save(state *s, int safe)
{
        state_rep srep = (state_rep) s;
        save_int(&srep->history, srep->fp);
        if (!(safe&SAFE_I)) {
                save_unsigned(&srep->history, limrd_tell(srep->pub.input));
	}
        if (!(safe&SAFE_O)) {
                save_unsigned(&srep->history, limwr_tell(srep->pub.output));
        }
        if (!(safe&SAFE_G)) {
                save_values(&srep->history, srep->globals, srep->nmutables);
        }
        if (!(safe&SAFE_L)) {
                save_values(&srep->history, 
                        &srep->stack.v[srep->fp], srep->stack.sp-srep->fp);
        }
}

void restore(state *s, int safe)
{
        state_rep srep = (state_rep) s;
        if (!(safe&SAFE_L)) {
                restore_values(&srep->history, 
                    &srep->stack.v[srep->fp], srep->stack.sp-srep->fp);
        }
        if (!(safe&SAFE_G)) {
                restore_values(&srep->history, srep->globals, srep->nmutables);
        }
        if (!(safe&SAFE_O)) {
                limwr_seek(srep->pub.output, pop_unsigned(&srep->history));
	}
        if (!(safe&SAFE_I)) {
                limrd_seek(srep->pub.input, pop_unsigned(&srep->history));
        }
        if (srep->fp != pop_int(&srep->history)) {
                abort();
        }
}

void discard(state *s, int safe)
{
        state_rep srep = (state_rep) s;
        if (!(safe&SAFE_L)) {
                discard_values(&srep->history, srep->stack.sp - srep->fp);
        }
        if (!(safe&SAFE_G)) {
                discard_values(&srep->history, srep->nmutables);
        }
        if (!(safe&SAFE_O)) {
                (void) pop_unsigned(&srep->history);
	}
        if (!(safe&SAFE_I)) {
                (void) pop_unsigned(&srep->history);
        }
        if (srep->fp != pop_int(&srep->history)) {
                abort();
        }
}

static void save_unsigned(valuestack *st, unsigned u)
{
        extendstack(st, st->sp+1);
        st->v[st->sp].intval = (int) u;
        st->sp++;
}

static void save_int(valuestack *st, int n)
{
        extendstack(st, st->sp+1);
        st->v[st->sp].intval = n;
        st->sp++;
}

static void save_values(valuestack *st, value *v, int vlen)
{
        extendstack(st, st->sp+vlen);
        bcopy((char*)v, (char*)&st->v[st->sp], vlen*sizeof(value));
        st->sp += vlen;
}

static void restore_values(valuestack *st, value *v, int vlen)
{
        st->sp -= vlen;
        bcopy((char*)&st->v[st->sp], (char*)v, vlen*sizeof(value));
}

static void discard_values(valuestack *st, int vlen)
{
        st->sp -= vlen;
}

static unsigned pop_unsigned(valuestack *st)
{
        st->sp--;
        return ((unsigned) st->v[st->sp].intval);
}

static unsigned pop_int(valuestack *st)
{
        st->sp--;
        return (st->v[st->sp].intval);
}

void newframe(state *s, int nouts, varlist inouts, vallist ins,
        int framesize)
{
        state_rep srep = (state_rep) s;
        int i;
        int newfp;
        int sp;
        extendstack(&srep->stack, srep->stack.sp+framesize+1);
        save_int(&srep->stack, srep->fp);
        sp = srep->stack.sp;
        newfp = sp;
        bzero ((char *)&srep->stack.v[sp], nouts * sizeof (value));
        sp += nouts;
        for (i = 0; i != inouts.len; i++) {
                srep->stack.v[sp+i] = lookup(s, inouts.list[i]);
        }
        sp += inouts.len;
        bcopy((char *)&ins.list[0], 
              (char *)&srep->stack.v[sp],
              sizeof(value) * ins.len);
        srep->stack.sp = newfp+framesize;
        srep->fp = newfp;
}

void oldframe(state *s, varlist outs, varlist inouts, int succeeded)
{
        state_rep srep = (state_rep) s;
        int oldfp = srep->fp;
        int i;
        if (oldfp==0) {
                abort();
        }
        srep->stack.sp = srep->fp;
        srep->fp = pop_int(&srep->stack);
	if (succeeded) {
		for (i = 0; i != outs.len; i++) {
			assign(s, outs.list[i], srep->stack.v[oldfp+i]);
		}
		for (i = 0; i != inouts.len; i++) {
			assign(s, inouts.list[i], 
			       srep->stack.v[oldfp+outs.len+i]);
		}
	}
}

void assign(state *s, var lhs, value rhs)
{
        state_rep srep = (state_rep) s;
        if (lhs.index >= 0) {
                srep->stack.v[srep->fp+lhs.index] = rhs;
        } else {
                srep->globals[-1 - lhs.index] = rhs;
        }
}

value lookup(state *s, var v) 
{
        state_rep srep = (state_rep) s;
        if (v.index >= 0) {
                return (srep->stack.v[srep->fp+v.index]);
        } else {
                return (srep->globals[-1 - v.index]);
        }
}

int lookupbyindex(state *s, int index) 
{
        state_rep srep = (state_rep) s;
        if (index >= 0) {
                return (srep->stack.v[srep->fp+index].intval);
        } else {
                return (srep->globals[-1 - index].intval);
        }
}

varlist outres()
{
        varlist res;
        res.len = 1;
        res.list = (var*) emalloc(sizeof(var));
        res.list[0].index = 0;
        res.list[0].name = (atom*)0;
        return (res);
}
