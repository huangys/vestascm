/* scope.h */

/* A scope is a mapping from atoms to entities, where
   an entity is either a local index, a global index, or a global
   procedure.   */

/* This interface logically includes parsetree. */

#define LOCAL 0
#define GLOBAL 1
#define PROCEDURE 2
#define BUILTIN 3
#define MARK 4

typedef struct entity {
    int type;
    int index; 
    procdecl *pdecl;
    atom *name;
    struct nodemark mark;
    int (*exec)(/* struct node *p, state *s */);
    struct {
            int outs;
            int inouts;
            int ins;
    } sig;
} entity; 

typedef struct scope {
        entity *stack;
        int len; /* number of entities allocated */
        int sp;  /* number of entities used */
} scope;

/* The name field of an entity is relevant for all types: it
   is the name bound to the entity in the scoppe.  In addition:
   If type = LOCAL or GLOBAL, index is the index of the var in
       the local or global frame.
   If type = PROCEDURE, pdecl is the declaration of the procedure.
   If type = BUILTIN, nodemark is the mark for the builtin (see 
   parsetree.h), exec is its exec method, and sig is its signature
   (the number of its parameters of each type).
 */

scope *newscope();
/* return a scope in which no atoms are bound. */

entity *lookupscope(scope *s, atom *a);
/* return the entity to which a is bound in s, or null if
   a is unbound in s. */

void bindscope(scope *s, entity *e);
/* modify s to bind e.name to e. */

void savescope(scope *s);
/* save the state of s. */

void restorescope(scope *s);
/* restore s to its last saved state. */

void freescope(scope *s);
/* destroy s and free its storage. */

