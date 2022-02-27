#include "atom.h"
#include "emalloc.h"

#define LGTBLSIZE 9
#define TBLSIZE (1<<LGTBLSIZE)
#define WORDSIZE (8 * sizeof(unsigned))

typedef struct atomrep {
        atom atom;
        struct atomrep *link;
} atomrep;

static unsigned multiplier = 2619930051;

static atomrep *bucket[TBLSIZE];

atom *fromname(char *name)
{
      unsigned h = 0;
      int i = 0;
      atomrep *res;
      while (name[i] != '\0') {
          h = (h<<2) + name[i];
          i ++;
      }
      h = (multiplier * h) >> (WORDSIZE-LGTBLSIZE);
      res = bucket[h];
      while (res != (atomrep*)0 && strcmp(res->atom.pname, name) != 0) {
              res = res->link;
      }
      if (res == (atomrep*)0) {
              res = (atomrep*) emalloc(sizeof(atomrep));
              res->atom.pname = stralloc(name);
              res->link = bucket[h];
              bucket[h] = res;
      }
      return (&res->atom);
}      
       
