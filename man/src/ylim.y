%{
#include <stdio.h>
#include "atom.h"
#include "parsetree.h"
#include "limlex.h"
#include "limrd.h"
#include "limwr.h"
#include "state.h"
static node *newbinop(int op, node *left, node *right, int line);
static varlist addtovarlist (varlist l, var v);
static exprlist addtoexprlist (exprlist l, node *e);
static getslist addtogetslist (getslist l, assignment a);
%}
%union
{
	node    *node;          /* node in parse tree */
	varlist varlist;
	getslist getslist;
	assignment gets;
	exprlist exprlist;
	lexitem lexitem;
}

%token  	CALL BINOP UNOP
%token          VARUSE PROCDECL VARDECL 

%token <lexitem> ID DO OD IS IN END GETS PROC VAR CONST EVAL
%token <lexitem> INTCONSTANT SKIP ABORT FAIL TIL
%token <lexitem> STRCONSTANT '{' '}' '(' ')' ';' ','
%token <lexitem> ARROW '|' ':' '<' '>' GE LE '=' '#'
%token <lexitem> OR AND NOT '-' '+' '*' '/' '%'

%type     <node>  decl sdecl stmt expr call scall
%type     <varlist> varlist
%type     <varlist> varlist2
%type     <getslist> getslist
%type     <gets> gets
%type     <exprlist> inclist

%left   '|'
%left   ARROW
%left   ';'
%left   OR
%left   AND
%left   NOT
%left   '>' GE '<' LE '=' '#'
%left   '+' '-'
%left   '*' '/' '%'
%left   UNARYMINUS

%start prog
%%

prog	: /*empty*/                    
	| prog decl
	        {
	        	if (progtreetail == (node *) 0) {
	        		progtree = $2;
			} else {
				progtreetail->u.decl.link = $2;
			}
			progtreetail = $2;
			$2->u.decl.link = (node *) 0;
		}
	;


decl	: PROC varlist GETS sdecl IS stmt END ';'
                {$$=$4; 
                 $$->line = $1.line; 
                 $$->u.decl.u.p.outs = $2; 
                 $$->u.decl.u.p.body = $6;}
	| PROC sdecl IS stmt END ';'
                {$$=$2;
                 $$->line = $1.line; 
                 $$->u.decl.u.p.body = $4;}
	| VAR getslist ';'
	        {$$ = newnode(VARDECL, $1.line); $$->u.decl.u.globals = $2;}
	;

sdecl	: ID '(' ')'
                {$$ = newnode(PROCDECL, $1.line); 
                 $$->u.decl.u.p.name = $1.u.atom;}
	| '(' varlist ')' ':' ID '(' ')'
                {$$ = newnode(PROCDECL, $1.line); 
                 $$->u.decl.u.p.name = $5.u.atom;
                 $$->u.decl.u.p.inouts = $2;}
	| ID ':' ID '(' ')'
                {var v; 
                 v.name = $1.u.atom; 
                 $$=newnode(PROCDECL, $1.line);
                 $$->u.decl.u.p.name = $3.u.atom;
                 $$->u.decl.u.p.inouts.len=1;
                 $$->u.decl.u.p.inouts.list = (var*) emalloc(sizeof(var));
                 $$->u.decl.u.p.inouts.list[0] = v;}                 
	| '(' varlist ')' ':' ID '(' varlist ')'
                {$$ = newnode(PROCDECL, $1.line); 
                 $$->u.decl.u.p.name = $5.u.atom;
                 $$->u.decl.u.p.inouts = $2;
                 $$->u.decl.u.p.ins = $7;}
	| ID ':' ID '(' varlist ')'
                {var v; 
                 v.name = $1.u.atom; 
                 $$=newnode(PROCDECL, $1.line);
                 $$->u.decl.u.p.name = $3.u.atom;
                 $$->u.decl.u.p.inouts.len=1;
                 $$->u.decl.u.p.inouts.list = (var*) emalloc(sizeof(var));
                 $$->u.decl.u.p.inouts.list[0] = v;
                 $$->u.decl.u.p.ins = $5;}                 
	| ID '(' varlist ')'
                {$$=newnode(PROCDECL, $1.line);
                 $$->u.decl.u.p.name = $1.u.atom;
                 $$->u.decl.u.p.ins = $3;}                 
	;

stmt	: stmt ';' stmt                
                {$$ = newnode(';', $2.line); 
                 $$->u.semi.left = $1; 
                 $$->u.semi.right = $3;}
        | expr ARROW stmt
                {$$ = newnode(ARROW, $2.line); 
                 $$->u.arrow.left = $1; 
                 $$->u.arrow.right = $3;
                }
	| stmt '|' stmt
                {$$ = newnode('|', $2.line); 
                 $$->u.bar.left = $1; 
                 $$->u.bar.right = $3;}
	| DO stmt OD
                {$$ = newnode(DO, $1.line); 
                 $$->u.doloop.cmd = $2;}
	| VAR getslist IN stmt END
                {$$ = newnode(VAR, $1.line); 
                 $$->u.var.locals= $2;
                 $$->u.var.cmd = $4;}
        | SKIP
                {$$ = newnode(SKIP, $1.line);}
        | FAIL
                {$$ = newnode(FAIL, $1.line);}
        | EVAL expr
                {$$ = newnode(EVAL, $1.line);
                 $$->u.eval.expr = $2;}
        | ABORT
                {$$ = newnode(ABORT, $1.line);}
        | TIL stmt DO stmt END
                {$$ = newnode(TIL, $1.line);
                 $$->u.til.left = $2;
                 $$->u.til.right = $4;}
	| varlist GETS expr
	        {if ($3->type == CALL) {
	                freevarlist($3->u.call.outs);
	                $3->u.call.outs = $1;
	                $$ = $3;
	         } else if ($1.len != 1) { 
	                 char buf[100];
	                 sprintf(buf,
				 "unexpected comma in assignment at line %d",
	                   $2.line);
	                 yyerror(buf); 
	         } else {
	                 $$ = newnode(GETS, $2.line);
	                 $$->u.gets.lhs = $1.list[0];
	                 $$->u.gets.rhs = $3;
	                 free((char*)$1.list);
	         }
	        }
	| call 
	        {$$=$1;}
	| '{' stmt '}'
	        {$$=$2;}
	;

getslist: gets
            {$$.len = 1; 
             $$.list = (assignment*) emalloc(sizeof(assignment)); 
             $$.list[0] = $1;} 
	| getslist ',' gets
            {$$ = addtogetslist($1, $3);} 
	;

gets:    ID GETS expr
        {$$.lhs.name = $1.u.atom; $$.rhs = $3;}
        ;

varlist : ID
                {var v; 
                 v.name = $1.u.atom; 
                 $$.len=1;
                 $$.list = (var*) emalloc(sizeof(var));
                 $$.list[0] = v;}                 
	| varlist ',' ID
                {var v; 
                 v.name = $3.u.atom; 
                 $$ = addtovarlist($1, v);}
	;

varlist2: ID ',' ID
                {var v; 
                 v.name = $1.u.atom; 
                 $$.len=1;
                 $$.list = (var*) emalloc(sizeof(var));
                 $$.list[0] = v;
                 v.name = $3.u.atom; 
                 $$ = addtovarlist($$, v);}                 
	| varlist2 ',' ID
                {var v; 
                 v.name = $3.u.atom; 
                 $$ = addtovarlist($1, v);}
	;

inclist	: expr
                {$$.len=1; 
                 $$.list=(node**) emalloc(sizeof(node*));
                 $$.list[0] = $1;}
	| inclist ',' expr
                {$$ = addtoexprlist($1, $3);}
	;

call	: scall
                {$$=$1;}
	| '(' varlist2 ')' ':' scall
                {$$=$5;
                 $$->u.call.inouts=$2;}
        | ID ':' scall 
                {var v; 
                 v.name = $1.u.atom; 
                 $$=$3;
                 $$->u.call.inouts.len=1;
                 $$->u.call.inouts.list = (var*) emalloc(sizeof(var));
                 $$->u.call.inouts.list[0] = v;}                 
	;

scall	: ID '(' ')'
                {$$ = newnode(CALL, $1.line);
                 $$->u.call.p.name = $1.u.atom;}
	| ID '(' inclist ')'
                {$$ = newnode(CALL, $1.line);
                 $$->u.call.p.name = $1.u.atom;
                 $$->u.call.ins = $3;}
	;

expr	: call
                {$$=$1; $$->u.call.outs=outres();}
	| ID
                {$$=newnode(VARUSE, $1.line); 
                 $$->u.varuse.var.name=$1.u.atom;}
	| INTCONSTANT
	        {$$=newnode(INTCONSTANT, $1.line);
	         $$->u.constant.val.intval = $1.u.integer;}
	| STRCONSTANT
	        {$$=newnode(STRCONSTANT, $1.line);
	         $$->u.strconst.val = $1.u.string.s;
	         $$->u.strconst.vallen = $1.u.string.len; }
	| expr '+' expr
	        {$$=newbinop('+', $1, $3, $2.line);}
	| expr '-' expr
	        {$$=newbinop('-', $1, $3, $2.line);}
	| expr '*' expr
	        {$$=newbinop('*', $1, $3, $2.line);}
	| expr '/' expr
	        {$$=newbinop('/', $1, $3, $2.line);}
	| expr '%' expr
	        {$$=newbinop('%', $1, $3, $2.line);}
	| expr '<' expr
	        {$$=newbinop('<', $1, $3, $2.line);}
	| expr '>' expr
	        {$$=newbinop('>', $1, $3, $2.line);}
	| expr LE expr
	        {$$=newbinop(LE, $1, $3, $2.line);}
	| expr GE expr
	        {$$=newbinop(GE, $1, $3, $2.line);}
	| expr '#' expr
	        {$$=newbinop('#', $1, $3, $2.line);}
	| expr '=' expr
	        {$$=newbinop('=', $1, $3, $2.line);}
	| expr AND expr
	        {$$=newbinop(AND, $1, $3, $2.line);}
	| expr OR expr
	        {$$=newbinop(OR, $1, $3, $2.line);}
	| NOT expr
	        {$$=newnode(UNOP, $1.line); 
	         $$->u.unop.op = NOT;
	         $$->u.unop.arg = $2;}
	| '-' expr %prec UNARYMINUS
	        {$$=newnode(UNOP, $1.line); 
	         $$->u.unop.op = '-';
	         $$->u.unop.arg = $2;}
	| '(' expr ')'
	        {$$=$2;}
	;
%%
int inputline = 1;

yyerror (char *s)
{
	limerr (s, "", inputline);
	exit (2);
}

int powerof4(int n) 
{
        if (n <= 0) {
                abort();
        } else {
                while ((n % 4) == 0) {
                        n = n/4;
                }
                return (n == 1);
        }
}

static node *newbinop(int op, node *left, node *right, int line) 
{
        node *res = newnode(BINOP, line);
        res->u.binop.op = op;
        res->u.binop.left = left; 
        res->u.binop.right = right; 
        return (res);
}

static varlist addtovarlist (varlist l, var v)
{
        if (powerof4(l.len)) {
                l.list = (var*) erealloc((char *) l.list, 4 * l.len * sizeof(var));
        }
        l.list[l.len]=v;
        l.len++;
        return (l);
}

static exprlist addtoexprlist (exprlist l, node *e)
{
        if (powerof4(l.len)) {
                l.list = (node**) erealloc((char *) l.list, 4*l.len * sizeof(node*));
        }
        l.list[l.len]=e;
        l.len++;
        return (l);
}

static getslist addtogetslist (getslist l, assignment a)
{
        if (powerof4(l.len)) {
                l.list = (assignment*) 
                    erealloc((char *) l.list, 4 * l.len * sizeof(assignment));
        }
        l.list[l.len]=a;
        l.len++;
        return (l);
}

