/* limrd.h */

/* A limrd_t is an input stream of characters that supports
   certain operations that are important to LIM, in particular,
   short backwards seeks. */

#define limrd_getchar(t) (*(t)->getch)(t)
/* consume and return the next character in the stream t, or
   return -1 if t is at end of file. */

#define limrd_eof(t) (*(t)->eof)(t)
/* return whether the stream t is at end of file. */

#define limrd_tell(t) (*(t)->tell)(t)
/* return the number of characters that have been read from 
   the stream t */

#define limrd_seek(t, n) (*(t)->seek)((t), (n))
/* reset the pointer of the stream t to be as though it has read
   n characters and return 0, or return -1 on failure. */

#define limrd_close(t) (*(t)->close)(t)
/* destroy the stream t and free any resources allocated on its
   behalf. */

#define limrd_consume(t, s, slen) (*(t)->consume)((t), (s), (slen))
/* If the next slen characters of the stream t are those in
   the string s, read them and return 1, else return 0. */

#define limrd_at(t, s, slen) (*(t)->at)((t), (s), (slen))
/* If the next slen characters of the stream t are those in
   the string s, return 1, else return 0. */

#define limrd_maxread(t) (*(t)->maxread)(t)
/* Return the maximum number of characters that were ever read
   from t. */

#define limrd_peek(t) (*(t)->peek)(t)
/* return (but do not consume) the next character in the stream t, or
   return -1 if t is at end of file, or return -2 */

struct limrd_t {
        int (*getch) (/* struct limrd_t * */);
        int (*eof) (/* struct limrd_t * */);
        unsigned (*tell) (/* struct limrd_t * */);
        int (*seek) (/* struct limrd_t *, unsigned n */);
        void (*close) (/* struct limrd_t * */);
        int (*consume) (/* struct limrd_t *, char *s, int slen */);
        int (*at) (/* struct limrd_t *, char *s, int slen */);
        unsigned (*maxread) (/* struct limrd_t * */);
        int (*peek) (/* struct limrd_t * */);
}; 

typedef struct limrd_t *limrd_t;

