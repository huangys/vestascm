extern int inputline;
extern char *limprogname;		/* name of file being compiled/executed */

typedef struct string {
        int len;
        char *s;
} string;

typedef struct lexitem {
        int line;
        union {
                atom *atom;
                int integer;
                string string;
        } u;
} lexitem;

extern node *progtree;
/* This variable should be initialized to nil before the parser is called.
   After the parser has returned, it contains the head of a linked list of 
   top-level declarations, linked through the .link field */

extern node *progtreetail;
/* This variable should be initialized to nil before the parser is called.
   After the parser has returned, it contains a pointer to the last 
   top-level declaration in the "progtree" list, or nil if the list
   is empty. */

extern int debug;
/* debug flags:
	bit 0	allow Err() to be optimized away (see parsetree.c)
	bit 1	print parsetree before annotation (see lim.c)
	bit 2	print parsetree after annotation (see lim.c)
	bit 3	print parsetree after marking (see lim.c)
 */

void limerr (char *msg0, char *msg1, int line);
/* Print an error message, giving the lim programme file name, the message in 
   msg0 & msg1, and, if line is non-zero, the line number. */
