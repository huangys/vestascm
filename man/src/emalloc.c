#include <strings.h>
#include <stdlib.h>

char *emalloc (int n)
{
	char *res = (char *) malloc ((unsigned) n);

	if (res == 0) {
		perror ("malloc");
		exit (2);
	}
	return (res);
}

char *erealloc (char *p, int n)
{
	p = (char *) realloc (p, (unsigned) n);

	if (p == 0) {
		perror ("realloc");
		exit (2);
	}
	return (p);
}

char *stralloc (char *p)
{
	int len = strlen (p);
	char *res;

	res = emalloc (len+1);
	(void) strcpy (res, p);
	return (res);
}
