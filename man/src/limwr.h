/* limwr.h */

/* A limwr_t is an output stream of characters that supports
   certain operations that are important to LIM, in particular,
   short backwards seeks. */

#define limwr_puts(t, s, slen) (*(t)->puts)((t), (s), (slen))
/* Output the character sequence s of length slen to the stream t. */

#define limwr_tell(t) (*(t)->tell)(t)
/* return the number of characters that have been written to 
   the stream t */

#define limwr_seek(t, n) (*(t)->seek)((t), (n))
/* reset the pointer of the stream t to be as though 
   n characters have been written and return 0, or return -1 
   on failure. */

#define limwr_close(t) (*(t)->close)(t)
/* Flush the stream t, then destroy it and free any resources allocated on its
   behalf. */

typedef struct limwr_t {
        int (*puts) (/* struct limwr_t *self, char *s, int slen */);
        unsigned (*tell) (/* struct limwr_t *self */);
        int (*seek) (/* struct limwr_t *self, unsigned n */);
        void (*close) (/* struct limwr_t *self */);
} *limwr_t;

