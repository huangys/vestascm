/* node of the parse tree for lim */

typedef struct var {
        atom *name;
        int index;
        struct var *link;
} var;

/* The index field of a local variable is its index in
   the local frame; this will be non-negative.  The
   index field of a global variable is (-1 - its index
   in the list of all global variables of the program).
   The indexs of mutable global variables precede the
   indexes of immutable gloabl variables.
   The "link" field is local to the procedure annotate.
*/

typedef struct varlist {
        int len;
        var *list;
} varlist;

typedef struct exprlist {
        int len;
        struct node **list;
} exprlist;

typedef struct assignment {
        var lhs;
        struct node *rhs;
} assignment;

typedef struct getslist {
        int len;
        assignment *list;
} getslist;

typedef struct value {
        int intval;
} value;

typedef struct vallist {
        int len;
        value *list;
} vallist;

typedef struct procdecl {
        atom *name;
        varlist outs;
        varlist inouts;
        varlist ins;
        int framesize;
        struct node *body;
} procdecl;

typedef struct proccall {
        atom *name;
        procdecl *pdecl;
        int (*exec)(/* struct node *p, state *s */);
} proccall;

/* The "mark" field of a node contains the following bits:

    bit            means that the command:

    TOTAL        is total
    PURE_I       does not modify the input stream
    PURE_O       does not modify the output stream
    PURE_G       does not modify the globals
    PURE_L       does not modify the locals
    SAFE_I       does not modify the input stream if it fails
    SAFE_O       does not modify the output stream if it fails
    SAFE_G       does not modify the globals if it fails
    SAFE_L       does not modify the locals if it fails
    STABLE       the previous bits are correct

In addition, the mark field contains fields inputvar, inputmask,
and checkinput, with the following meanings:

    if inputvar is not IVAR_NONE, then the command will certainly fail
    unless the next character exists in the input and has the same
    value as variable with index inputvar.

    inputmask represents a set of characters.  If a character c is not
    in inputmask, then the command will certainly fail if the next 
    character in the input is c.  inputmask is represented by a 32 bit
    word.  Character c is in inputmask if ((1 << (c & 0x1f)) & inputmask).
    For the purposes of this mask, end-of-file counts as a character
    with code -1.

    checkinput is true if inputmask does not contain all characters, or if
    inputvar is not IVAR_NONE
*/

#define TOTAL   0x0f
#define PURE_I  0x01
#define PURE_O  0x02
#define PURE_G  0x04
#define PURE_L  0x08
#define SAFE_I  0x01
#define SAFE_O  0x02
#define SAFE_G  0x04
#define SAFE_L  0x08
#define STABLE  0x01
#define PURE    0x0f
#define SAFE    0x0f

#define IMASK_AND(x,y)	((x) &= (y))
#define IMASK_OR(x,y)	((x) |= (y))
#define IMASK_ADD(x,y)	((x) |= (1 << ((y) & 0x1f)))
#define IMASK_TEST(x,y)	((x) & (1 << ((y) & 0x1f)))
#define IMASK_TESTALL(x)	((x) == -1)
#define IMASK_ALL(x)	((x) = -1)
#define IMASK_NONE(x)	((x) = 0)
#define IMASK_EQ(x,y)	((x) == (y))

#define IVAR_NONE	(-1)

typedef struct node {
    int type;
    int line;
    struct nodemark {
            char stable;
            char safe;
            char pure;
            char total;
            char checkinput;
            int inputvar;
            int inputmask;
    } mark;
    union {
        struct {
            struct node *left;
            struct node *right;
        } semi;
        struct {
            struct node *left;
            struct node *right;
        } arrow;
        struct {
            struct node *left;
            struct node *right;
        } bar;
        struct {
            struct node *cmd;
        } doloop;
        struct {
            struct node *expr;
        } eval;
        struct {
            struct node *left;
            struct node *right;
        } til;
        struct {
                getslist locals;
                struct node *cmd;
        } var;
        struct {
            varlist outs;
            varlist inouts;
            exprlist ins;
            proccall p;
        } call;
        struct varuse {
                var var;
        } varuse;
        struct constant {
                value val;
        } constant;
        struct strconst {
                char *val;
                int vallen;
        } strconst;
        struct binop {
                int op;
                struct node *left;
                struct node *right;
        } binop;
        struct unop {
                int op;
                struct node *arg;
        } unop;
        struct assignment gets;
        struct decl {
                struct node *link;
                union{
                        procdecl p;
                        getslist globals;
                } u;
        } decl;
    } u;
} node;

void freetree(node *p);
/* destroy p and free the storage for its nodes. */

void freevarlist(varlist v);
/* destroy v and free the storage for its nodes. */

node *annotate(node *prog, int *nglobals, int *nmutants, int *mainframesize);
/* fill in the index fields of all variable-use
   nodes in prog, fill in the proc fields of all
   procedure calls in prog, fill in the outlist
   for functional procedure calls, fill in the
   exec method for builtin procedures, check
   that procedure calls have the right number
   of parameters and perform other syntax
   checks, count the number of global variables
   and set nglobals to this value, count the number
   of mutable global variabes and set nmutants
   to this value, and return
   the body of the procedure named "Main", or
   return NULL if errors were detected. 
   In the successful case, set mainframesize to
   the size for the frame for "Main". */

void mark(node *prog);
/* fill in the safety bits of all nodes in prog,
   which is a complete program. */

node *newnode(int t, int line);
/* allocate and return a new node of type t, initializing
   all internal pointer fields to null and integers to zero,
   and initializing its line number field to line. */

void printtree(node *p);
/* print a textual representation of p to stderr. */
