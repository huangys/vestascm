/* stdlimrd.c */

#include <stdio.h>
#include "limrd.h"
#include "emalloc.h"

#define BUFLEN (1024*32)
#define READLEN (1024*8)
#define MIN(a,b)    ((a) < (b)? (a) : (b))

typedef struct t_rep {
        struct limrd_t pub;
        char *buf;
        unsigned st;
        unsigned len;
        unsigned cur;
        unsigned maxread;
        FILE *fp;
} *t_rep;

static int stdlimgetchar(t_rep self);
static int stdlimconsume(t_rep self, char *s, int slen);
static int stdlimat(t_rep self, char *s, int slen);
static int stdlimeof(t_rep self);
static int filbuf(t_rep self);
static void stdlimclose(t_rep self);
static unsigned stdlimtell(t_rep self);
static unsigned stdlimmaxread(t_rep self);
static int stdlimpeek(t_rep self);
static int stdlimseek(t_rep self, unsigned n);

/* Q1: The len characters in buf starting at buf[(st % BUFLEN)] and
   continuing circularly agree with the len characters of the source
   starting at st. 

   Q2: The number of characters that have been read by getchar
   or consume equals cur (modulo 2^wordsize), and maxread is
   the maximum value of cur over the lifetime of the reader.

   Q3: The source of the reader is the source of fp, and the
   number of characters that have been read from fp is
   st+len (modulo 2^wordsize). */

limrd_t rdfromfile(FILE *f)
{
        t_rep res = (t_rep) emalloc(sizeof(struct t_rep));
        res->buf = emalloc(BUFLEN);
        res->st = 0;
        res->len = 0;
        /* Q1 */
        res->cur = 0;
        res->maxread = 0;
        /* Q2 */
        res->fp = f;
        /* Q3 */
        res->pub.getch = (int (*) (struct limrd_t *self)) stdlimgetchar;
        res->pub.eof = (int (*) (struct limrd_t *self)) stdlimeof;
        res->pub.tell = (unsigned (*) (struct limrd_t *self)) stdlimtell;
        res->pub.maxread = 
            (unsigned (*) (struct limrd_t *self)) stdlimmaxread;
        res->pub.seek = 
            (int (*) (struct limrd_t *self, unsigned n)) stdlimseek;
        res->pub.close = (void (*) (struct limrd_t *self)) stdlimclose;
        res->pub.consume = 
            (int (*) (struct limrd_t *self, char *s, int slen)) stdlimconsume;
        res->pub.at = 
            (int (*) (struct limrd_t *self, char *s, int slen)) stdlimat;
        res->pub.peek = (int (*) (struct limrd_t *self)) stdlimpeek;
        return (&res->pub);
}

static int stdlimgetchar(t_rep self)
{
        int res;
        if (self->cur == self->st+self->len) {
                if (filbuf(self) == -1) {
                        return (-1);
                }       
        }
        res = ((unsigned char *) self->buf)[self->cur % BUFLEN];
        self->cur++;
        if (self->cur > self->maxread) {
                self->maxread = self->cur;
        }
        return (res);
}

static int stdlimpeek(t_rep self)
{
        int res;
        if (self->cur == self->st+self->len) {
        	return (-2);
        }
        res = ((unsigned char *) self->buf)[self->cur % BUFLEN];
        return (res);
}

static int stdlimconsume(t_rep self, char *s, int slen) {
        int i;
        while (self->st + self->len - self->cur < slen) {
                if (filbuf(self) != 0) { 
                        return (0);
                }
        }
        i = 0;
        while (i != slen && s[i] == self->buf[(self->cur+i)%BUFLEN]) {
                i++;
        }
        if (i == slen) {
                self->cur += slen;
                if (self->cur > self->maxread) {
                        self->maxread = self->cur;
                }
                return (1);
        } else {
                return (0);
        }
}

static int stdlimat(t_rep self, char *s, int slen) {
        int i;
        while (self->st + self->len - self->cur < slen) {
                if (filbuf(self) != 0) { 
                        return (0);
                }
        }
        i = 0;
        while (i != slen && s[i] == self->buf[(self->cur+i)%BUFLEN]) {
                i++;
        }
        if (i == slen) {
                return (1);
        } else {
                return (0);
        }
}

static int stdlimeof(t_rep self) {
        if (self->cur == self->st+self->len) {
                (void) filbuf(self);
        }
        return (self->cur == self->st+self->len);
}

static int filbuf(t_rep self)
/* advance self.st+self.len by at least 1 and advance st if
   necessary and return 0, or return -1 if st+len is the
   number of characters in the source.  Requires that
   either cur-st>=READLEN OR len+READLEN<=BUFLEN.
   */
{
        int bufpos = (self->st+self->len) % BUFLEN;
        int numread = 0;
        if (!feof (self->fp)) {
		numread = fread(&self->buf[bufpos], sizeof(char), 
		    MIN(READLEN, BUFLEN-bufpos), self->fp);
	}
        if (numread==0) {
                if (ferror(self->fp)) {
                        abort();
                }
                return (-1);
        } else {
                self->len = self->len + numread;
                if (self->len > BUFLEN) {
                        self->st = self->st + (self->len-BUFLEN);
                        self->len = BUFLEN;
                }
                return (0);
        }
}

static void stdlimclose(t_rep self)
{
        free(self->buf);
        (void) fclose(self->fp);
        free((char *) self);
}

static unsigned stdlimtell(t_rep self) 
{
        return (self->cur);
}

static unsigned stdlimmaxread(t_rep self) 
{
        return (self->maxread);
}

static int stdlimseek(t_rep self, unsigned n) 
{
        if ((int) (n - self->cur) > 0 ) {
                abort();
        } else if ((self->cur - self->st) >= (self->cur - n)) {
                self->cur = n;
                if (self->cur > self->maxread) {
                        self->maxread = self->cur;
                }
                return (0);
        } else {
                return (-1);
        }
}

