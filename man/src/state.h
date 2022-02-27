/* state.h */

/* The "current state" is the state of the globals, the input and output, 
   and the state of the top procedure frame.  A "state" s represents
   a call stack of frames together with a history s.history of previous 
   values of s.current. */

/* logically includes parsetree.h */

typedef struct state {
        limrd_t input;
        limwr_t output;
} state;

state *newstate(int nglobals, int nmutables, FILE *in, FILE *out);
/* Return a state with an empty call stack and history 
   whose input and output are in and out and which has
   room for nglobals global variables of which nmutable are
   mutable. */

void freestate(state *s);
/* Flush s's output stream, destroy s, and free any storage used on its 
   behalf. */

void save(state *s, int safe);
/* Saves s.current by pushing it on s.history.  "safe" is a
   bit vector indicating portions of "s.current" that are
   guaranteed will not change by the next restore, and which
   therefore don't need to be saved: see parsetree.h for
   the layout and interpretation of the bits. */

void discard(state *s, int safe);
/* Removes the top element of s.history. Requires that "safe" be
   equal to the corresponding parameter of the corresponding call
   to "save". */

void restore(state *s, int safe);
/* Sets s.current to the top element of
   s.history and removes that element. Requires that "safe" be
   equal to the corresponding parameter of the corresponding call
   to "save". */

void newframe(state *s, int nouts, varlist inouts, vallist ins,
        int framesize);
/* Push a new frame on the call stack with "nouts" out
   parameters initialized arbitrarily, with inouts.len inout parameters
   initialized to the current values of the variables in inouts.list
   and with ins.len in parameters initialized to the values in
   ins.list, and additional variables, initialized arbitrarily,
   to make a total of framesize. */

void oldframe(state *s, varlist outs, varlist inouts, int succeeded);
/* Remove the top frame from the call stack of s; if succeeded is non-zero,
   assign its out and inout values to the variables in the corresponding
   argument lists.
*/

void assign(state *s, var lhs, value rhs);
/* Change the value of lhs in s.current to be rhs. */

value lookup(state *s, var v);
/* Return the value of v in s.current. */

int lookupbyindex(state *s, int index);
/* Return the value of integer variable with given index in s.current. */

varlist outres(void);
/* Return a varlist containing a single variable reserved by
   the state interface for recording the results of
   functional procedure calls.  This var is global
   number zero. */
