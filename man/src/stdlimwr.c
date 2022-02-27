/* stdlimwr.c */

#include <stdio.h>
#include "limwr.h"
#include "emalloc.h"

#define BUFLEN (1024*32)
#define WRITELEN (1024*8)
#define MIN(a,b)    ((a) < (b)? (a) : (b))

typedef struct t_rep {
        struct limwr_t pub;
        char *buf;
        unsigned st;
        unsigned cur;
        FILE *fp;
} *t_rep;

static void stdlimputs(t_rep self, char *s, int slen);
static void stdlimclose(t_rep self);
static unsigned stdlimtell(t_rep self);
static int stdlimseek(t_rep self, unsigned n);
static void flushbuf(t_rep self);


/* Q1: The characters in buf starting at buf[(st % BUFLEN)] and
   continuing circularly to buf[cur % BUFLEN] agree with the characters 
   of the target starting at st. 

   Q2: The number of characters that have been written by puts
   equals cur (modulo 2^wordsize).

   Q3: The target of the writer is the file fp, and the
   number of characters that have been written to fp is
   st (modulo 2^wordsize). */

limwr_t wrfromfile(FILE *f)
{
        t_rep res = (t_rep) emalloc(sizeof(struct t_rep));
        res->buf = emalloc(BUFLEN);
        res->st = 0;
        res->cur = 0;
        /* Q1 && Q2 */
        res->fp = f;
        /* Q3 */
        res->pub.puts = 
            (int (*) (struct limwr_t *self, char *s, int slen)) stdlimputs;
        res->pub.tell = (unsigned (*) (struct limwr_t *self)) stdlimtell;
        res->pub.seek = 
            (int (*) (struct limwr_t *self, unsigned n)) stdlimseek;
        res->pub.close = (void (*) (struct limwr_t *self)) stdlimclose;
        return (&res->pub);
}

static void stdlimputs(t_rep self, char *s, int slen)
{
        int done = 0;
        while (1) {
                int n = MIN(slen-done, BUFLEN - (self->cur - self->st));
                int i;
                for (i = 0; i != n; i++) {
                        self->buf[(self->cur+i) % BUFLEN] = s[done+i];
                }
                done += n;
                self->cur += n;
                if (done == slen) {
                        return;
                }
                flushbuf(self);
        }
}

static void flushbuf(t_rep self)
/* Flush at least one character of self to its file.
   Requires that there be at least one character to be flushed.
*/
{
        int bufpos = self->st % BUFLEN;
        int m = MIN(WRITELEN, MIN(BUFLEN-bufpos, self->cur - self->st));
        m = fwrite(&self->buf[bufpos], sizeof(char), m, self->fp);
        if (m == 0) {
                abort();
        } else {
                self->st += m;
        }
}

static void stdlimclose(t_rep self)
{
        while (self->cur != self->st) {
                flushbuf(self);
        }
        free(self->buf);
        (void) fclose(self->fp);
        free((char *) self);
}

static unsigned stdlimtell(t_rep self) {
        return (self->cur);
}

static int stdlimseek(t_rep self, unsigned n) {
        if ((int) (n - self->cur) > 0 ) {
                abort();
        } else if ((self->cur - self->st) >= (self->cur - n)) {
                self->cur = n;
                return (0);
        } else {
                return (-1);
        }
}

