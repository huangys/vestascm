/* Execute LIM commands and evaluate LIM expressions. */

int cp(node *p, state *s);
/* res = cp(p, s) executes the command p in the state s.current
   and returns 1, or determines that p fails in s.current and returns
   0 after arbitrarily changing s.current. Requires that p be 
   a command. */

int cpp(node *e, state *s, value *v);
/* Set v to the value of e in state s and return 1, or
   return 0 (with arbitrary side effects) if e fails in s. */

int initialize(node *p, state *s);
/* Initialize the global variables of p, which is a
   list of proc and global variable declarations. These
   initializations are performed in the order in 
   which they appear in p. Return 1 if all 
   initializations succeed, and 0 if any of them fail.  */

int exec_rd(struct node *p, state *s);
/* The exec method for the built-in Rd(). */

int exec_rdx(struct node *p, state *s);
/* The exec method for the built-in Rd(x). */

int exec_wr(struct node *p, state *s);
/* The exec method for the built-in Wr. */

int exec_err(struct node *p, state *s);
/* The exec method for the built-in Err. */

int exec_eof(struct node *p, state *s);
/* The exec method for the built-in Eof. */

int exec_at(struct node *p, state *s);
/* The exec method for the built-in At. */

