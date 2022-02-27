%{
#include <stdio.h>
#include <ctype.h>
#include "atom.h"
#include "parsetree.h"
#include "limlex.h"
#include "y.tab.h"
#include "emalloc.h"

static int xdebug = 0;

#define xreturn(x)	{if (xdebug) {fprintf (stderr, "x\n");} return (x);}

static int fixstringliteral (char *s);
static long limatol (char *s);

int nesting = 0;
%}

%Start comment normal first

%%
<normal>"{"		{ yylval.lexitem.line = inputline; xreturn ('{'); }
<normal>"}"		{ yylval.lexitem.line = inputline; xreturn ('}'); }
<normal>"("		{ yylval.lexitem.line = inputline; xreturn ('('); }
<normal>")"		{ yylval.lexitem.line = inputline; xreturn (')'); }
<normal>":="		{ yylval.lexitem.line = inputline; xreturn (GETS); }
<normal>";"		{ yylval.lexitem.line = inputline; xreturn (';'); }
<normal>","             { yylval.lexitem.line = inputline; xreturn (','); }
<normal>"->"            { yylval.lexitem.line = inputline; xreturn (ARROW); }
<normal>"|"		{ yylval.lexitem.line = inputline; xreturn ('|'); }
<normal>":"		{ yylval.lexitem.line = inputline; xreturn (':'); }
<normal>"<"		{ yylval.lexitem.line = inputline; xreturn ('<'); }
<normal>">"		{ yylval.lexitem.line = inputline; xreturn ('>'); }
<normal>">="		{ yylval.lexitem.line = inputline; xreturn (GE); }
<normal>"<="		{ yylval.lexitem.line = inputline; xreturn (LE); }
<normal>"="		{ yylval.lexitem.line = inputline; xreturn ('='); }
<normal>"#"		{ yylval.lexitem.line = inputline; xreturn ('#'); }
<normal>"OR"		{ yylval.lexitem.line = inputline; xreturn (OR); }
<normal>"AND"		{ yylval.lexitem.line = inputline; xreturn (AND); }
<normal>"NOT"		{ yylval.lexitem.line = inputline; xreturn (NOT); }
<normal>"-"		{ yylval.lexitem.line = inputline; xreturn ('-'); }
<normal>"+"		{ yylval.lexitem.line = inputline; xreturn ('+'); }
<normal>"*"		{ yylval.lexitem.line = inputline; xreturn ('*'); }
<normal>"DIV"		{ yylval.lexitem.line = inputline; xreturn ('/'); }
<normal>"MOD"           { yylval.lexitem.line = inputline; xreturn ('%'); }
<normal>"VAR"		{ yylval.lexitem.line = inputline; xreturn (VAR); }
<normal>"CONST"         { yylval.lexitem.line = inputline; xreturn (CONST); }
<normal>"DO"		{ yylval.lexitem.line = inputline; xreturn (DO); }
<normal>"OD"		{ yylval.lexitem.line = inputline; xreturn (OD); }
<normal>"IS"		{ yylval.lexitem.line = inputline; xreturn (IS); }
<normal>"IN"		{ yylval.lexitem.line = inputline; xreturn (IN); }
<normal>"END"		{ yylval.lexitem.line = inputline; xreturn (END); }
<normal>"PROC"		{ yylval.lexitem.line = inputline; xreturn (PROC); }
<normal>"SKIP"          { yylval.lexitem.line = inputline; xreturn (SKIP); }
<normal>"FAIL"          { yylval.lexitem.line = inputline; xreturn (FAIL); }
<normal>"ABORT"         { yylval.lexitem.line = inputline; xreturn (ABORT); }
<normal>"EVAL"          { yylval.lexitem.line = inputline; xreturn (EVAL); }
<normal>"TIL"           { yylval.lexitem.line = inputline; xreturn (TIL); }
<normal>[a-zA-Z_][a-zA-Z_0-9]*	{
			  yylval.lexitem.u.atom = fromname((char *) yytext);
			  xreturn (ID); }
<normal>[0-9]+		{ yylval.lexitem.u.integer = limatol ((char *) yytext); 
                          yylval.lexitem.line = inputline;
                          xreturn (INTCONSTANT); }
<normal>\"([^\n\"\\]|\\.)*\"    {
			  char *res = stralloc((char *) &yytext[1]);
			  res[strlen(res)-1] = '\0';
			  yylval.lexitem.u.string.len = fixstringliteral(res);
			  yylval.lexitem.u.string.s = res;
			  yylval.lexitem.line = inputline;
			  xreturn(STRCONSTANT); }
<normal>\'([^\n\'\\]|\\.)*\'    {
			  char *res = stralloc((char *) &yytext[1]);
			  res[strlen(res)-1] = '\0';
			  if (1 !=  fixstringliteral(res)) {
				 limerr("illegal char literal", "", inputline);
				 exit (2);
			  }
			  yylval.lexitem.u.integer = res[0] & 0xff;
			  free(res);
			  yylval.lexitem.line = inputline;
			  xreturn(INTCONSTANT); }
<normal>"(*"		{ nesting = 1; BEGIN comment; }
<normal>[ \t\r]*	{}
<normal,comment>\n	{ inputline++; }
<comment>"(*"		{ nesting++; }
<comment>"*)"		{ nesting--; if (nesting==0) { BEGIN normal;}  }
<comment>.		{}
<normal>\"              { limerr("newline in string constant", "", inputline);
			  exit (2); }
<normal>\'              { limerr("newline in character constant",
			   	"", inputline);
			  exit (2); }
<normal>.|\n            { limerr("illegal character", "", inputline);
			  exit (2); }
<first>"#"[^\n]*\n      { inputline++; BEGIN normal; }
.|\n			{ unput (yytext[0]);
			  if (yytext[0] == '#') {
			  	BEGIN first;
			  } else {
			  	BEGIN normal;
			  }
			}

%%

int digitval(char digit)
{
        int ch = tolower(digit);
        static char s[] = "0123456789abcdef";
        int res = 0;
        while ('\0' != s[res] && ch != s[res]) { 
                res++;
        }
        return (res);
}

static int fixstringliteral (char *s)
{
        int from = 0;
        int to = 0;
	while (s[from] != '\0') {
		if (s[from] == '\\') {
			from++;
			if (s[from] == 'x' || 
			    (isdigit (s[from]) && s[from] != '8' && s[from] != '9')) {
				int x = 0;
				int lim = 3 + from;
				int base = 8;
				if (s[from] =='x') {
					from++;
					lim = 2 + from;
					base = 16;
				}
				while (from != lim && digitval(s[from]) < base) {
					x = base * x + digitval(s[from]);
					from++;
				}
				s[to] = x;
				to++;
			} else {
				switch (s[from])
				{
				case 't': 
					s[to] = '\t'; break;
				case 'b': 
					s[to] = '\b'; break;
				case 'v': 
					s[to] = '\v'; break;
				case 's': 
					s[to] = ' '; break;
				case 'r': 
					s[to] = '\r'; break;
				case 'e': 
					s[to] = '\033'; break;
				case 'n': 
					s[to] = '\n'; break;
				case 'f': 
					s[to] = '\f'; break;
				case '\'': 
					s[to] = '\''; break;
				case '"': 
					s[to] = '"'; break;
				case '\\': 
					s[to] = '\\'; break;
				default:  
					limerr("illegal escape in char or string literal",
						"", inputline);
					exit (2);
				}
				to++;
				from++;
			}
		} else {
			s[to] = s[from];
			to++;
			from++;
		}
	}
	return (to);
}

static long limatol (char *s)
{
	long res = 0;
	while (isdigit (*s)) {
		long res2 = res + res;
		long res4 = res2 + res2;
		long res8 = res4 + res4;
		long res10 = res8 + res2;
		res = res10 + (*s - '0');
		if (res2 < 0 || res4 < 0 || res8 < 0 || res10 < 0 || res < 0) {
			limerr("number too large", "", inputline);
			exit (2);
		}
		s++;
	}
	return (res);
}
