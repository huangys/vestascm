
/* atom.h */

/* An atom* is unique representative of a char*. */

typedef struct atom {
        char *pname; /*readonly*/
} atom;

atom *fromname(char *name);
/* return the atom for "name", creating it if necessary. */

