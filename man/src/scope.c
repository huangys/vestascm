#include <stdlib.h>
#include "atom.h"
#include "parsetree.h"
#include "scope.h"
#include "emalloc.h"

scope *newscope() {
        scope *res;
        res = (scope*) emalloc(sizeof(scope));
        res->len = 50;
        res->stack = (entity*) emalloc(res->len * sizeof(entity));
        res->sp = 0;
        return (res);
}

void freescope(scope *s) {
        free((char *) s->stack);
        free((char *) s);
}

entity *lookupscope(scope *s, atom *a) {
        int i = s->sp-1;
        while (i != -1 && a != s->stack[i].name) { i--;} 
        if (i==-1) {
                return ((entity*)0);
        } else {
                return (&s->stack[i]);
        }
}

void savescope(scope *s) {
        entity e;
        e.type = MARK;
        e.name = (atom*)0;
        bindscope(s, &e);
}

void restorescope(scope *s) 
{
        do {s->sp--;} while (s->sp != -1 && s->stack[s->sp].type != MARK);
        if (s->sp==-1) {
                abort();
        }
}

void bindscope(scope *s,entity *e) 
{
        if (s->len==s->sp) {
                s->len = s->len * 2;
                s->stack = (entity*) erealloc((char*)s->stack, 
                                         s->len * sizeof(entity));
        }
        s->stack[s->sp] = *e;
        s->sp++;
}
