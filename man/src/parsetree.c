#include <stdio.h>
#include <stdlib.h>
#include "emalloc.h"
#include "atom.h"
#include "parsetree.h"
#include "scope.h"
#include "limlex.h"
#include "y.tab.h"
#include "limrd.h"
#include "limwr.h"
#include "state.h"
#include "interpret.h"

struct annodata;

static void pushprocformals(scope *s, procdecl *pd, int *nlocals);
static int annotatevar(scope *s, var *v, int line, struct annodata *d, int ismutant);
static scope *builtin();
static int annotateproccall(scope *s, node *call, int *isbuiltin);
static int badsignature(node *p, int outs, int inouts, int ins, int gag);
static int annotatep(scope *s, node *p, struct annodata *d);
static int annotatepp(scope *s, node *p, int allows_str, struct annodata *d);
static void printvar(var v);
static void printgetslist(getslist g);
static void printproccall(proccall p);
static void printvarlist(varlist v);
static void printexprlist(exprlist e);
static void printstrconst(struct strconst s);
static printprocdecl(struct procdecl *p);
static void freegetslist(getslist g);
static void freeexprlist(exprlist e);
static void freestrconst(struct strconst s);
static freeprocdecl(struct procdecl p);
static struct nodemark markp(node *p);

node *newnode(int t, int line) 
{
        node *res;
        res = (node*) emalloc(sizeof(node));
        bzero((char*)res, sizeof(node));
        res->type = t;
        res->line = line;
        return (res);
}

/* The procedure annotate relies on two main subroutines: */

struct annodata {
        int nlocals;
        var *varp;
        int *mutant;
};

/* annotatep(s, p, d) annotates the command node p in
   the scope s, increments d->nlocals by the number of local variables
   in p, adds all var nodes within p to the linked list of var
   nodes d->varp, and sets d->mutant[i] for each i that is the
   index of a global variable that is directly modified in p. 
   (The outres global zero is never considered to be modified
   (see annotatevar)). */

/* annotatepp(s, p, allows_str, d) annotates the expr node p in
   the scope s, adds all var nodes within p to the linked list of var
   nodes d->varp, and sets d->mutant[i] for each i that is the
   index of a global variable that is directly modified in p. 
   (The outres global zero is never considered to be modified
   (see annotatevar)). */

node *annotate(node *prog, int *nglobals, int *nmutants, int *mainframesize)
{
       node *res = (node*)0;
       node *p = prog;
       atom *main_atm = fromname("Main");
       int error = 0;
       int i;
       scope *scp = newscope();
       struct annodata d;
       d.varp = (var*)0;
       *nglobals = 1; /* global 0 is for state.outres. */
       while (p != (node *)0) {
               if (p->type == VARDECL) {
                       getslist *g = &p->u.decl.u.globals;
                       for (i=0; i != g->len; i++) {
                               if ((entity*)0 != 
                                   lookupscope(scp, g->list[i].lhs.name)) {
                                   error = 1;
                                   limerr("Multiple definition of ",
                                       g->list[i].lhs.name->pname, p->line);
                               } else {
                                 entity e;
                                 e.type = GLOBAL;
                                 e.index = *nglobals;
                                 e.name = g->list[i].lhs.name;
                                 bindscope(scp, &e);
                                 (*nglobals)++;
                               }
                       }
               } else if (p->type == PROCDECL) {
                               procdecl *pd = & p->u.decl.u.p;
                               if ((entity*)0 != 
                                   lookupscope(scp, pd->name)) {
                                   error = 1;
                                   limerr("Multiple definition of ",
                                       pd->name->pname, p->line);
                               } else {
                                 entity e;
                                 e.type = PROCEDURE;
                                 e.pdecl = pd;
                                 e.name = pd->name; 
                                 bindscope(scp, &e);
                               }
               } else {
                   abort();
               }
               p = p->u.decl.link;
       }
       p = prog;
       d.mutant = (int*) emalloc(*nglobals * sizeof(int));
       bzero((char*) d.mutant, *nglobals * sizeof(int));
       d.varp = (var*)0;
       while (p != (node *)0) {
               if (p->type == VARDECL) {
                       getslist *g = &p->u.decl.u.globals;
                       for (i=0; i != g->len; i++) {
                               if (annotatevar(scp, &g->list[i].lhs, p->line, &d, 0)) {
                                       abort();
                               }
                               error = annotatepp(scp, g->list[i].rhs, 0, &d) || error;
                       }
               } else if (p->type == PROCDECL) {
                       procdecl *pd = & p->u.decl.u.p;
                       d.nlocals = 0;
                       savescope(scp);
                       pushprocformals(scp, pd, &d.nlocals);
                       error = annotatep(scp, pd->body, &d) || error;
                       pd->framesize = d.nlocals;
                       if (main_atm == pd->name) {
                               res = pd->body;
                               *mainframesize = pd->framesize;
                       }
                       restorescope(scp);
               } else {
                   abort();
               }
               p = p->u.decl.link;
       }
       {
               int nextmutantindex = 0;
               int nextimmutableindex = *nglobals-1;
               for (i = 0; i != *nglobals; i++) {
                       if (d.mutant[i]) {
                               d.mutant[i] = nextmutantindex;
                               nextmutantindex++;
                       } else {
                               d.mutant[i] = nextimmutableindex;
                               nextimmutableindex--;
                       }
               }
               *nmutants = nextmutantindex;
       }
       {
               var *varp = d.varp;
               while ((var*)0 != varp) {
                       varp->index = -1 - d.mutant[varp->index];
                       varp = varp->link;
               }
       }
       free((char*)d.mutant);
       freescope(scp);
       if (error) {
               return ((node*)0);
       } else {
               if ((node*)0 == res) {
                       limerr("No procedure Main", "", 0);
               }
               return (res);
       }
}

static void pushprocformals(scope *s, procdecl *pd, int *nlocals)
/* bind the out, inout, and in
   formals of pd to local entity indexes.  The first of
   these formals is given the index nlocals, and nlocals
   is incremented by the number of formals. */
{
        int i;
        entity e;
        e.type = LOCAL;
        for (i=0; i != pd->outs.len; i++) {
                e.index = *nlocals;
                (*nlocals)++;
                e.name = pd->outs.list[i].name;
                bindscope(s, &e);
        }
        for (i=0; i != pd->inouts.len; i++) {
                e.index = *nlocals;
                (*nlocals)++;
                e.name = pd->inouts.list[i].name;
                bindscope(s, &e);
        }
        for (i=0; i != pd->ins.len; i++) {
                e.index = *nlocals;
                (*nlocals)++;
                e.name = pd->ins.list[i].name;
                bindscope(s, &e);
        }
}
      
static int annotatevar(scope *s, var *v, int line, struct annodata *d, int ismutant) 
{
        entity *e;
        if ((atom*)0 == v->name) {
                v->link = d->varp;
                d->varp = v;
                return (0);
                /* ignore modifications to the special var in outres (see state.h) */
        }
        e = lookupscope(s, v->name);
        if (e == (entity*)0) {
                limerr("Undeclared id ", v->name->pname, line);
                return (1);
        } 
        v->index = e->index; 
          /* sic.  For globals, the sign is changed in pass 3 of annotate. */
        if (e->type == GLOBAL) {
                if (ismutant) {
                        d->mutant[e->index] = 1;
                }
                v->link = d->varp;
                d->varp = v;
        }
        return (0);
}

static scope *builtinscope = (scope*)0;

static atom *rdatm = (atom*)0;

/* builtinscope # NIL => rdatm is valid. */

/* builtin() returns a scope in which all the built-ins
   are bound; except that "Rd" is bound to the form
   which takes an in argument. */ 

static scope *builtin()
{
        entity e;
        if (builtinscope != (scope*)0) {
                return (builtinscope);
        }
        rdatm = fromname("Rd");
        builtinscope = newscope();
        e.type = BUILTIN;
        
        e.name = fromname("Rd");
        e.mark.total = 0;
        e.mark.pure = PURE_O | PURE_G | PURE_L;
        e.mark.safe = SAFE;
        e.mark.stable = STABLE;
        e.mark.inputvar = 0;
        IMASK_ALL (e.mark.inputmask);
        e.exec = exec_rdx;
        e.sig.outs = 0;
        e.sig.inouts = 0;
        e.sig.ins = 1;
        bindscope(builtinscope, &e);

        e.name = fromname("Wr");
        e.mark.total = TOTAL;
        e.mark.pure = PURE_I | PURE_G | PURE_L;
        e.mark.safe = SAFE;
        e.mark.stable = STABLE;
        e.mark.inputvar = IVAR_NONE;
        IMASK_ALL (e.mark.inputmask);
        e.exec = exec_wr;
        e.sig.outs = 0;
        e.sig.inouts = 0;
        e.sig.ins = 1;
        bindscope(builtinscope, &e);

        e.name = fromname("Err");
        e.mark.total = TOTAL;
        if (debug & 0x1) {
		e.mark.pure = PURE;
        } else {
		e.mark.pure = PURE_O | PURE_G | PURE_L;
		    /* not PURE_I so Err() doesn't get optimized away */
	}
        e.mark.safe = SAFE;
        e.mark.stable = STABLE;
        e.mark.inputvar = IVAR_NONE;
        IMASK_ALL (e.mark.inputmask);
        e.exec = exec_err;
        e.sig.outs = 0;
        e.sig.inouts = 0;
        e.sig.ins = 1;
        bindscope(builtinscope, &e);

        e.name = fromname("At");
        e.mark.total = 0;
        e.mark.pure = PURE;
        e.mark.safe = SAFE;
        e.mark.stable = STABLE;
        e.mark.inputvar = 0;
        IMASK_ALL (e.mark.inputmask);
        e.exec = exec_at;
        e.sig.outs = 0;
        e.sig.inouts = 0;
        e.sig.ins = 1;
        bindscope(builtinscope, &e);

        e.name = fromname("Eof");
        e.mark.total = 0;
        e.mark.pure = PURE;
        e.mark.safe = SAFE;
        e.mark.stable = STABLE;
        e.mark.inputvar = IVAR_NONE;
        IMASK_NONE (e.mark.inputmask);
        IMASK_ADD (e.mark.inputmask, -1);
        e.exec = exec_eof;
        e.sig.outs = 0;
        e.sig.inouts = 0;
        e.sig.ins = 0;
        bindscope(builtinscope, &e);

        return (builtinscope);
}
 
static int annotateproccall(scope *s, node *call, int *isbuiltin)
{
        int res = 0;
        entity *e = lookupscope(s, call->u.call.p.name);
        if (e != (entity*)0) {
                *isbuiltin = 0;
                call->u.call.p.pdecl = e->pdecl;
                res = badsignature(call, e->pdecl->outs.len,
                                e->pdecl->inouts.len,
                                e->pdecl->ins.len,
                                0);
        } else {
                e = lookupscope(builtin(), call->u.call.p.name);
                if (e == (entity*)0) {
                        limerr("Undefined procedure ", 
                                 call->u.call.p.name->pname, call->line);
                        res = 1;
                } else {
                        *isbuiltin = 1;
                        call->u.call.p.pdecl = (procdecl*)0;
                        call->u.call.p.exec = e->exec;
                        if (call->u.call.p.name == rdatm) {
                                res = badsignature(call, e->sig.outs, e->sig.inouts,
                                          e->sig.ins, 1);
                                if (res) {
                                        res = badsignature(call, 1, 0, 0, 0);
                                        call->u.call.p.exec = exec_rd;
                                }
                        } else {
                                res = badsignature(call, e->sig.outs, e->sig.inouts,
                                          e->sig.ins, 0);
                        }
                }
        }
        return (res);
}
        
static int badsignature(node *p, int outs, int inouts, int ins, int gag)
/* return whether the call p passes the wrong numbers of outs, inouts,
   or in parameters.  If not and if gag is zero, then print an
   appropriate error message. */
{
        int res = 0;
        if (p->u.call.outs.len != outs) {
                if (!gag) {
                        if (outs == 0) {
                                limerr(p->u.call.p.name->pname,
					" returns no value", p->line);
                        } else {
                                limerr("Wrong number of out parameters to ",
                                    p->u.call.p.name->pname, p->line);
                        }
                }
                res = 1;
        }
        if (p->u.call.inouts.len != inouts) {
                if (!gag) {
                        limerr("Wrong number of inout parameters to ",
                            p->u.call.p.name->pname, p->line);
                }
                res = 1;
        }
        if (p->u.call.ins.len != ins) {
                if (!gag) {
                        limerr("Wrong number of parameters to ",
                            p->u.call.p.name->pname, p->line);
                }
                res = 1;
        }
        return (res);
}

static int annotatep(scope *s, node *p, struct annodata *d) 
{
        int res = 0;
        switch (p->type)
        {
        case ';':
               res = annotatep(s, p->u.semi.left, d) || res;
               res = annotatep(s, p->u.semi.right, d) || res;
               break;
        case ARROW:
               res = annotatepp(s, p->u.arrow.left, 0, d) || res;
               res = annotatep(s, p->u.arrow.right, d) || res;
               break;
        case '|':
               res = annotatep(s, p->u.bar.left, d) || res;
               res = annotatep(s, p->u.bar.right, d) || res;
               break;
        case DO:
               res = annotatep(s, p->u.doloop.cmd, d) || res;
               break;
        case EVAL:
               res = annotatepp(s, p->u.eval.expr, 0, d) || res;
               break;
        case TIL:
               res = annotatep(s, p->u.til.left, d) || res;
               res = annotatep(s, p->u.til.right, d) || res;
               break;
        case VAR:
                {
                        int i;
                        entity e;
                        e.type = LOCAL;
                        for (i=0; i != p->u.var.locals.len; i++) {
                                e.index = d->nlocals;
                                d->nlocals++;
                                e.name = p->u.var.locals.list[i].lhs.name;
                                bindscope(s, &e);
                                if (annotatevar(s, &p->u.var.locals.list[i].lhs, p->line, d, 0)) {
                                        abort();
                                }
                                res = annotatepp(s, p->u.var.locals.list[i].rhs, 0, d) || res;
                        }
                        res = annotatep(s, p->u.var.cmd, d) || res;
                }
                break;
        case CALL:
                {
                        int i; 
                        int isbuiltin;
                        res = annotateproccall(s, p, &isbuiltin) || res;
                        for (i=0; i != p->u.call.outs.len; i++) {
                                res = annotatevar(s,  &p->u.call.outs.list[i], p->line, d, 1) || res;
                        }
                        for (i=0; i != p->u.call.inouts.len; i++) {
                                res = annotatevar(s,  &p->u.call.inouts.list[i], p->line, d, 1) || res;
                        }
                        for (i=0; i != p->u.call.ins.len; i++) {
                                res = annotatepp(s,  p->u.call.ins.list[i], isbuiltin, d)
                                      || res;
                        }
                 }
                 break;
        case GETS:
                res = annotatevar(s, &p->u.gets.lhs, p->line, d, 1) || res;
                res = annotatepp(s, p->u.gets.rhs, 0, d) || res;
                break;
        case SKIP:
        case ABORT:
        case FAIL:
                break;
        default:
                abort();
        }
        return (res);
}


static int annotatepp(scope *s, node *p, int allows_str, struct annodata *d)
{
        int res = 0;
        switch (p->type)
        {
        case BINOP:
                  res = annotatepp(s, p->u.binop.left, 0, d) || res;
                  res = annotatepp(s, p->u.binop.right, 0, d) || res;
                  break;
        case UNOP:
                  res = annotatepp(s, p->u.unop.arg, 0, d) || res;
                  break;
        case INTCONSTANT:
                  break;
        case STRCONSTANT:
                if (! allows_str) {
                        limerr("Illegal string constant", "", p->line);
                        res = 1;
                 }
                 break;
        case VARUSE:
                res = annotatevar(s, &p->u.varuse.var, p->line, d, 0);
                break;
        case CALL:
                {
                        int i; 
                        int isbuiltin;
                        res = annotateproccall(s, p, &isbuiltin) || res;
                        for (i=0; i != p->u.call.outs.len; i++) {
                                res = annotatevar(s,  &p->u.call.outs.list[i], p->line, d, 1) || res;
                        }
                        for (i=0; i != p->u.call.inouts.len; i++) {
                                res = annotatevar(s,  &p->u.call.inouts.list[i], p->line, d, 1) || res;
                        }
                        for (i=0; i != p->u.call.ins.len; i++) {
                                res = annotatepp(s,  p->u.call.ins.list[i], isbuiltin, d) 
                                      || res;
                        }
                 }
                 break;
         default: 
                 abort();
         }
         return (res);
}

static void printvar(var v)
{
        if (v.name != (atom*)0) {
                fprintf(stderr, "(VAR %s %d)", v.name->pname, v.index);
        } else {
                fprintf(stderr, "(VAR RESULT %d)", v.index);
        }
}

static void printgetslist(getslist g)
{
        int i;
        fprintf(stderr, "(");
        for (i=0; i != g.len; i++) {
                fprintf(stderr, "(:= ");
                printvar(g.list[i].lhs);
                printtree(g.list[i].rhs);
                fprintf(stderr, ")");
        }
        fprintf(stderr, ")");
}

static void printproccall(proccall p)
{
        fprintf(stderr, "(PROCCALL %s %lx %lx)", p.name->pname, 
                (long) p.pdecl, (long) p.exec);
}

static void printvarlist(varlist v)
{
        int i;
        fprintf(stderr, "(");
        for (i=0; i != v.len; i++) {
                printvar(v.list[i]);
        }
        fprintf(stderr, ")");
}

static void printexprlist(exprlist e)
{
        int i;
        fprintf(stderr, "(");
        for (i=0; i != e.len; i++) {
                printtree(e.list[i]);
        }
        fprintf(stderr, ")");
}

static void printstrconst(struct strconst s)
{
        int i;
        fprintf(stderr, "(");
        for (i=0; i != s.vallen; i++) {
                fprintf(stderr, "%x ", s.val[i]);
        }
        fprintf(stderr, ")");
}

static printprocdecl(struct procdecl *p)
{
        fprintf(stderr, " %s %d %lx", p->name->pname, p->framesize,
                (long) p);
        printvarlist(p->outs);
        printvarlist(p->inouts);
        printvarlist(p->ins);
        printtree(p->body);
}


void printtree(node *p)
{
        if ((node *)0 == p) {
                fprintf(stderr, " NULL ");
                return;
        }
        fprintf(stderr, "(");
        fprintf(stderr, "L%dM%x%x%x%xC%dV%dI%x ", 
            p->line, p->mark.pure, p->mark.safe, p->mark.stable,
            p->mark.total,
            p->mark.checkinput, p->mark.inputvar, p->mark.inputmask);
        switch (p->type) 
        {
        case ';':
               fprintf(stderr, "SEMI");
               printtree(p->u.semi.left);
               printtree(p->u.semi.right);
               break;
        case ARROW:
               fprintf(stderr, "ARROW");
               printtree(p->u.arrow.left);
               printtree(p->u.arrow.right);
               break;
        case '|':
               fprintf(stderr, "BAR");
               printtree(p->u.bar.left);
               printtree(p->u.bar.right);
               break;
        case DO:
               fprintf(stderr, "DO");
               printtree(p->u.doloop.cmd);
               break;
        case EVAL:
               fprintf(stderr, "EVAL");
               printtree(p->u.eval.expr);
               break;
        case TIL:
               fprintf(stderr, "TIL");
               printtree(p->u.til.left);
               printtree(p->u.til.right);
               break;
        case VAR:
               fprintf(stderr, "VAR");
               printgetslist(p->u.var.locals);
               printtree(p->u.var.cmd);
               break;
        case CALL:
                fprintf(stderr, "CALL");
                printproccall(p->u.call.p);
                printvarlist(p->u.call.outs);
                printvarlist(p->u.call.inouts);
                printexprlist(p->u.call.ins);
                break;
        case GETS:
                fprintf(stderr, "GETS");
                printvar(p->u.gets.lhs);
                printtree(p->u.gets.rhs);
                break;
        case BINOP:
                fprintf(stderr, "BINOP%d", p->u.binop.op);
                printtree(p->u.binop.left);
                printtree(p->u.binop.right);
                break;
        case UNOP:
                fprintf(stderr, "UNOP%d", p->u.unop.op);
                printtree(p->u.unop.arg);
                break;
        case INTCONSTANT:
                fprintf(stderr, "INTCONST %d", p->u.constant.val.intval);
                break;
        case STRCONSTANT:
                fprintf(stderr, "STRCONST");
                printstrconst(p->u.strconst);
                break;
        case VARUSE:
                fprintf(stderr, "VARUSE");
                printvar(p->u.varuse.var);
                break;
        case VARDECL:
                fprintf(stderr, "VARDECL");
                printgetslist(p->u.decl.u.globals);
                if ((node *)0 != p->u.decl.link) {
                        printtree(p->u.decl.link);
                }
                break;
        case PROCDECL:
                fprintf(stderr, "PROCDECL");
                printprocdecl(&p->u.decl.u.p);
                if ((node *)0 != p->u.decl.link) {
                        printtree(p->u.decl.link);
                }
                break;
        case SKIP:
                fprintf(stderr, "SKIP");
                break;
        case ABORT:
                fprintf(stderr, "ABORT");
                break;
        case FAIL:
                fprintf(stderr, "FAIL");
                break;
        default:
                abort();
        }
        fprintf(stderr, ")");
}

static void freegetslist(getslist g)
{
        free((char *) g.list);
}

void freevarlist(varlist v)
{
        if (v.list != (var*) 0) {
	        free((char *) v.list);
	}
}

static void freeexprlist(exprlist e)
{
        int i;
        for (i=0; i != e.len; i++) {
                freetree(e.list[i]);
        }
        if (e.list != (node**) 0) {
                free((char *) e.list);
        }
}

static void freestrconst(struct strconst s)
{
        free(s.val);
}

static freeprocdecl(struct procdecl p)
{
        freevarlist(p.outs);
        freevarlist(p.inouts);
        freevarlist(p.ins);
        freetree(p.body);
}

void freetree(node *p)
{
        switch (p->type) 
        {
        case ';':
               freetree(p->u.semi.left);
               freetree(p->u.semi.right);
               break;
        case ARROW:
               freetree(p->u.arrow.left);
               freetree(p->u.arrow.right);
               break;
        case '|':
               freetree(p->u.bar.left);
               freetree(p->u.bar.right);
               break;
        case DO:
               freetree(p->u.doloop.cmd);
               break;
        case EVAL:
               freetree(p->u.eval.expr);
               break;
        case TIL:
               freetree(p->u.til.left);
               freetree(p->u.til.right);
               break;
        case VAR:
               freegetslist(p->u.var.locals);
               freetree(p->u.var.cmd);
               break;
        case CALL:
                freevarlist(p->u.call.outs);
                freevarlist(p->u.call.inouts);
                freeexprlist(p->u.call.ins);
                break;
        case GETS:
                freetree(p->u.gets.rhs);
                break;
        case BINOP:
                freetree(p->u.binop.left);
                freetree(p->u.binop.right);
                break;
        case UNOP:
                freetree(p->u.unop.arg);
                break;
        case INTCONSTANT:
                break;
        case STRCONSTANT:
                freestrconst(p->u.strconst);
                break;
        case VARUSE:
                break;
        case VARDECL:
                freegetslist(p->u.decl.u.globals);
                if ((node *)0 != p->u.decl.link) {
                        freetree(p->u.decl.link);
                }
                break;
        case PROCDECL:
                freeprocdecl(p->u.decl.u.p);
                if ((node *)0 != p->u.decl.link) {
                        freetree(p->u.decl.link);
                }
                break;
        case SKIP:
        case ABORT:
        case FAIL:
                break;
        default:
                abort();
        }
        free((char *) p);
}

void mark(node *prog)
{
       int done = 0;
       node* p;
       while (! done) {
               p = prog;
               done = 1;
               while (p != (node *)0) {
                       if (p->type == VARDECL) {
                               /* skip */
                       } else if (p->type == PROCDECL) {
                               procdecl *pd = & p->u.decl.u.p;
                               if (!p->mark.stable) {
                                       struct nodemark temp;
                                       temp = pd->body->mark;
                                       markp(pd->body);
                                       done = done && 
                                         (temp.total == pd->body->mark.total) &&
                                         (temp.safe == pd->body->mark.safe) &&
                                         (temp.pure == pd->body->mark.pure) &&
                                         (temp.inputvar ==
                                          pd->body->mark.inputvar) &&
                                         IMASK_EQ (temp.inputmask,
                                          pd->body->mark.inputmask);
                                       p->mark = pd->body->mark;
                               }
                       } else {
                               abort();
                       }
                       p = p->u.decl.link;
              }
       }
       p = prog;
       while (p != (node *)0) {
               if (p->type == VARDECL) {
                       int i;
                       getslist *g = &p->u.decl.u.globals;
                       for (i=0; i != g->len; i++) {
                               markp(g->list[i].rhs);
                       }
               } 
               p = p->u.decl.link;
       }
}

static struct nodemark markp(node *p) 
{
	struct nodemark *r, a, b;
	r = &p->mark;
	if (r->stable) {
		return (*r);
	}
	switch (p->type)
	{
	case ';':
		a = markp(p->u.semi.left);
		b = markp(p->u.semi.right);
		r->total = a.total & b.total;
		r->stable = a.stable & b.stable;
		r->pure = a.pure & b.pure;
		r->safe = (a.safe & b.total) | (a.pure & b.safe);
		r->inputmask = a.inputmask;
		r->inputvar = a.inputvar;
		if (a.pure & PURE_I) {
			IMASK_AND (r->inputmask, b.inputmask);
			if (a.inputvar == IVAR_NONE &&
			    (a.pure & PURE_L) &&
			    (a.pure & PURE_G)) {
				r->inputvar = b.inputvar;
			}
		}
		break;
	case ARROW:
		a = markp(p->u.arrow.left);
		b = markp(p->u.arrow.right);
		r->total = 0;
		r->stable = a.stable & b.stable;
		r->pure = a.pure & b.pure;
		r->safe = a.pure & b.safe;
		r->inputmask = a.inputmask;
		r->inputvar = a.inputvar;
		if (a.pure & PURE_I) {
			IMASK_AND (r->inputmask, b.inputmask);
			if (a.inputvar == IVAR_NONE &&
			    (a.pure & PURE_L) &&
			    (a.pure & PURE_G)) {
				r->inputvar = b.inputvar;
			}
		}
		break;
	case '|':
		a = markp(p->u.bar.left);
		b = markp(p->u.bar.right);
		r->total = a.total | b.total;
		r->stable = a.stable & b.stable;
		r->pure = a.pure & (a.total | b.pure);
		r->safe = a.total | b.safe;
		r->inputmask = a.inputmask;
		IMASK_OR (r->inputmask, b.inputmask);
		if (a.inputvar == b.inputvar) {
			r->inputvar = a.inputvar;
		} else { 
			r->inputvar = IVAR_NONE;
		}
		break;
	case DO:
		a = markp(p->u.doloop.cmd);
		/* if the body of the DO loop is not safe, but is
		   stable, and it's got | at its root, then tack an
		   extra "| FAIL" onto the right hand side of the | at
		   the root.  This makes the body of the DO loop SAFE,
		   so it won't have to save state.  The state will be saved
		   by the new | that has been added, but with luck, the
		   state will be saved fewer times because the final |
		   will be executed only if left alternative of the
		   | at the root fails.  The DO would have to save the
		   state every time round the loop.
		 */
		if (a.stable && a.safe != SAFE &&
		    p->u.doloop.cmd->type == '|') {
			/* happens at most once per DO node */
			node *oldbar = p->u.doloop.cmd;
			node *newbar;
			oldbar->mark.stable = 0;
			while (oldbar->u.bar.right->type == '|') {
				oldbar = oldbar->u.bar.right;
				oldbar->mark.stable = 0;
			}
			newbar = newnode ('|', oldbar->line);
			newbar->u.bar.left = oldbar->u.bar.right;
			newbar->u.bar.right = newnode (FAIL, oldbar->line);
			oldbar->u.bar.right = newbar;
			a = markp(p->u.doloop.cmd);
		}
		r->total = TOTAL;
		r->stable = a.stable;
		r->pure = a.pure | a.total;
		r->safe = SAFE;
		r->inputvar = IVAR_NONE;
		IMASK_ALL (r->inputmask);
		break;
	case EVAL:
		a = markp(p->u.eval.expr);
		r->total = a.total;
		r->stable = a.stable;
		r->pure = a.pure;
		r->safe = a.safe;
		r->inputvar = a.inputvar;
		r->inputmask = a.inputmask;
		break;
	case TIL:
		a = markp(p->u.til.left);
		b = markp(p->u.til.right);
		r->total = a.total | b.total;
		r->stable = a.stable & b.stable;
		r->pure = a.pure & (a.total | b.pure);
		r->safe = a.total | b.safe;
		r->inputmask = a.inputmask;
		IMASK_OR (r->inputmask, b.inputmask);
		if (a.inputvar == b.inputvar) {
			r->inputvar = a.inputvar;
		} else { 
			r->inputvar = IVAR_NONE;
		}
		break;
	case GETS:
		a = markp(p->u.gets.rhs);
		r->total = a.total;
		r->stable = a.stable;
		if (p->u.gets.lhs.index >= 0) {
			r->pure = PURE_I | PURE_O | PURE_G;
		} else {
			r->pure = PURE_I | PURE_O | PURE_L;
		}
		r->safe = a.safe;
		r->inputvar = a.inputvar;
		r->inputmask = a.inputmask;
		break;
	case VARUSE:
	case INTCONSTANT:
	case STRCONSTANT:
	case SKIP:
	case ABORT:
		r->pure = PURE;
		r->safe = SAFE;
		r->total = TOTAL;
		r->stable = STABLE;
		r->inputvar = IVAR_NONE;
		IMASK_ALL (r->inputmask);
		break;
	case FAIL:
		r->pure = PURE;
		r->safe = SAFE;
		r->total = 0;
		r->stable = STABLE;
		r->inputvar = IVAR_NONE;
		IMASK_NONE (r->inputmask);
		break;
	case BINOP:
		a = markp(p->u.binop.left);
		b = markp(p->u.binop.right);
		r->total = a.total & b.total;
		r->stable = a.stable & b.stable;
		r->pure = a.pure & b.pure;
		r->safe = (a.safe & b.total) | (a.pure & b.safe);
		r->inputmask = a.inputmask;
		r->inputvar = a.inputvar;
		if (a.pure & PURE_I) {
			IMASK_AND (r->inputmask, b.inputmask);
			if (a.inputvar == IVAR_NONE &&
			    (a.pure & PURE_L) &&
			    (a.pure & PURE_G)) {
				r->inputvar = b.inputvar;
			}
		}
		break;
	case UNOP:
		a = markp(p->u.unop.arg);
		r->total = a.total;
		r->stable = a.stable;
		r->pure = a.pure;
		r->safe = a.safe;
		r->inputmask = a.inputmask;
		r->inputvar = a.inputvar;
		break;
	case VAR:
		{
			int i;
			a.total = TOTAL;
			a.pure = PURE;
			a.safe = SAFE;
			a.stable = STABLE;
			a.inputvar = IVAR_NONE;
			IMASK_ALL (a.inputmask);
			for (i=0; i != p->u.var.locals.len; i++) {
				b = markp(p->u.var.locals.list[i].rhs);
				if (a.pure & PURE_I) {
					IMASK_AND (a.inputmask, b.inputmask);
					if (a.inputvar == IVAR_NONE &&
					    (a.pure & PURE_L) &&
					    (a.pure & PURE_G)) {
						a.inputvar = b.inputvar;
					}
				}
				a.safe = (a.safe & b.total) | (a.pure & b.safe);
				a.total = a.total & b.total;
				a.stable = a.stable & b.stable;
				a.pure = a.pure & b.pure;
			}
			b = markp(p->u.var.cmd);
			r->safe = (a.safe & b.total) | (a.pure & b.safe);
			r->total = a.total & b.total;
			r->stable = a.stable & b.stable;
			r->pure = a.pure & b.pure;
			r->inputmask = a.inputmask;
			r->inputvar = a.inputvar;
			if (a.pure & PURE_I) {
				IMASK_AND (r->inputmask, b.inputmask);
				if (a.inputvar == IVAR_NONE &&
				    (a.pure & PURE_L) &&
				    (a.pure & PURE_G)) {
					r->inputvar = b.inputvar;
				}
			}
		}
		break;
	case CALL:
		{
			int i; 
			a.total = TOTAL;
			a.pure = PURE;
			a.safe = SAFE;
			a.stable = STABLE;
			a.inputvar = IVAR_NONE;
			IMASK_ALL (a.inputmask);
			for (i=0; i != p->u.call.ins.len; i++) {
				b = markp(p->u.call.ins.list[i]);
				if (a.pure & PURE_I) {
					IMASK_AND (a.inputmask, b.inputmask);
					if (a.inputvar == IVAR_NONE &&
					    (a.pure & PURE_L) &&
					    (a.pure & PURE_G)) {
						a.inputvar = b.inputvar;
					}
				}
				a.safe = (a.safe & b.total) | (a.pure & b.safe);
				a.total = a.total & b.total;
				a.stable = a.stable & b.stable;
				a.pure = a.pure & b.pure;
			}
			if (p->u.call.p.pdecl != (procdecl*)0) {
				b = p->u.call.p.pdecl->body->mark;
			} else {
				b = lookupscope(builtin(), 
					p->u.call.p.name)->mark;
				if (p->u.call.p.name == rdatm && 
				    p->u.call.ins.len == 0) {
					b.inputvar = IVAR_NONE;
				}
			}
			b.safe |= SAFE_L;  /* callee can't change our locals */
			b.pure |= PURE_L;
			r->safe = (a.safe & b.total) | (a.pure & b.safe);
			r->total = a.total & b.total;
			r->stable = a.stable & b.stable;
			r->pure = a.pure & b.pure;
			r->inputmask = a.inputmask;
			r->inputvar = a.inputvar;
			if (a.pure & PURE_I) {
				IMASK_AND (r->inputmask, b.inputmask);
			}
			if (a.inputvar == IVAR_NONE && (a.pure & PURE_I) && 
			    (a.pure & PURE_L) && (a.pure & PURE_G)) {
				if (b.inputvar < 0) {
					r->inputvar = b.inputvar;
				} else if (b.inputvar < p->u.call.outs.len) {
					/* out parameter (!) */
					r->inputvar = IVAR_NONE;
				} else if (b.inputvar < p->u.call.outs.len +
					   p->u.call.inouts.len) {
					/* inout parameter */
					r->inputvar = p->u.call.inouts.list[
					 b.inputvar - p->u.call.outs.len].index;
				} else if (b.inputvar < p->u.call.outs.len +
					   p->u.call.inouts.len +
					   p->u.call.ins.len) {
					/* in parameter */
					node *inp = p->u.call.ins.list[
					    b.inputvar - p->u.call.outs.len -
					    p->u.call.inouts.len];
					if (inp->type == VARUSE) {
						r->inputvar =
						 inp->u.varuse.var.index;
					} else if (inp->type == INTCONSTANT) {
						r->inputvar = IVAR_NONE;
						IMASK_NONE (r->inputmask);
						IMASK_ADD (r->inputmask,
						 inp->u.constant.val.intval);
					} else if (inp->type == STRCONSTANT &&
						inp->u.strconst.vallen > 0) {
						r->inputvar = IVAR_NONE;
						IMASK_NONE (r->inputmask);
						IMASK_ADD (r->inputmask, 
						 inp->u.strconst.val[0] & 0xff);
					} else {
						r->inputvar = IVAR_NONE;
					}
				} else {
					r->inputvar = IVAR_NONE;
				}
			}
			for (i=0; i != p->u.call.outs.len; i++) {
				if (p->u.call.outs.list[i].index >= 0) {
					r->pure &= ~PURE_L;
				} else {
					r->pure &= ~PURE_G;
				}
			}
			for (i=0; i != p->u.call.inouts.len; i++) {
				if (p->u.call.inouts.list[i].index >= 0) {
					r->pure &= ~PURE_L;
				} else {
					r->pure &= ~PURE_G;
				}
			}
		}
		break;
	default:
		abort();
	}
	r->checkinput = (r->inputvar != IVAR_NONE ||
		!IMASK_TESTALL (r->inputmask));
	return (*r);
}
