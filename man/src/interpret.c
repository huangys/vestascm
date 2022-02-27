/* Executing a LIM program */

#include <stdio.h>
#include <stdlib.h>
#include "emalloc.h"
#include "atom.h"
#include "limrd.h"
#include "limwr.h"
#include "parsetree.h"
#include "state.h"
#include "limlex.h"
#include "y.tab.h"
#include "interpret.h"

#define MAXINS 10
   
int cp(node *p, state *s)
{
        int res = 1;
        int c;
        switch (p->type)
        {
        case SKIP:
               break;
        case FAIL:
               res = 0; 
               break;
        case ABORT:
              {
                      unsigned rdp = limrd_tell(s->input);
                      unsigned wrp = limwr_tell(s->output);
                      freestate(s); 
                      fprintf(stderr, 
                 "%s aborted at line %d, read %u chars, wrote %u chars\n",
                      limprogname, p->line, rdp, wrp);
                      exit(2);
               }
        case ';':
               res = cp(p->u.semi.left, s); 
               if (res) {
                       res = cp(p->u.semi.right, s);
               }
               break;
        case ARROW:
                {
                       value v;
                       res = cpp(p->u.arrow.left, s, &v); 
                       res = res && v.intval;
                       if (res) {
                               res = cp(p->u.arrow.right, s);
                       }
                }       
                break;
        case '|':
        	res = 0;
        	c = -2;
        	if (!p->u.bar.left->mark.checkinput ||
        	   (c = limrd_peek (s->input)) == -2 || 
        	   (IMASK_TEST (p->u.bar.left->mark.inputmask, c) &&
        	    (p->u.bar.left->mark.inputvar == IVAR_NONE ||
        	     (lookupbyindex (s, p->u.bar.left->mark.inputvar) & 0xff) == c))) {
			if (p->u.bar.left->mark.safe == SAFE) {
				res = cp(p->u.bar.left, s);
			} else {
				save(s, p->u.bar.left->mark.safe);
				res = cp(p->u.bar.left, s);
				if (res) { 
					discard(s, p->u.bar.left->mark.safe);
				} else {
					restore(s, p->u.bar.left->mark.safe);
				}
			}
		}
		if (!res) {
			if (!p->u.bar.right->mark.checkinput ||
			   (c == -2 && (c = limrd_peek (s->input)) == -2) ||  
			   (IMASK_TEST (p->u.bar.right->mark.inputmask, c) &&
			    (p->u.bar.right->mark.inputvar == IVAR_NONE ||
			     (lookupbyindex (s, p->u.bar.right->mark.inputvar) & 0xff) == c))) {
				res = cp(p->u.bar.right, s);
			}
		}
                break;
        case DO:
                if (p->u.doloop.cmd->mark.safe == SAFE) {
                        while (res) {
                                res = cp(p->u.doloop.cmd, s);
                        }
                } else {
                        while (res) {
                                save(s, p->u.doloop.cmd->mark.safe);
                                res = cp(p->u.doloop.cmd, s);
                                if (res) { 
                                        discard(s, p->u.doloop.cmd->mark.safe);
                                } else {
                                        restore(s, p->u.doloop.cmd->mark.safe);
                                }
                        }
                 }
                 res = 1;
                 break;
        case EVAL:
                {
                       value v;
                       res = cpp(p->u.eval.expr, s, &v); 
                }       
                break;
        case TIL:
                if (p->u.til.left->mark.safe == SAFE) {
                        while (res) {
				res = 0;
				if (!p->u.til.left->mark.checkinput ||
				   (c = limrd_peek (s->input)) == -2 || 
				   (IMASK_TEST (p->u.til.left->mark.inputmask, c) &&
				    (p->u.til.left->mark.inputvar == IVAR_NONE ||
				     (lookupbyindex (s, p->u.til.left->mark.inputvar) & 0xff) == c))) {
					res = cp(p->u.til.left, s);
				}
                                if (res) {
                                        break;
                                } else {
                                        res = cp(p->u.til.right, s);
                                }
                        }
                } else {
                        while (res) {
				res = 0;
				if (!p->u.til.left->mark.checkinput ||
				   (c = limrd_peek (s->input)) == -2 || 
				   (IMASK_TEST (p->u.til.left->mark.inputmask, c) &&
				    (p->u.til.left->mark.inputvar == IVAR_NONE ||
				     (lookupbyindex (s, p->u.til.left->mark.inputvar) & 0xff) == c))) {
					save(s, p->u.til.left->mark.safe);
					res = cp(p->u.til.left, s);
					if (res) { 
						discard(s, p->u.til.left->mark.safe);
						break;
					} else {
						restore(s, p->u.til.left->mark.safe);
						res = cp(p->u.til.right, s);
					}
                                } else {
					res = cp(p->u.til.right, s);
                                }
                        }
                 }
                 break;
        case VAR:
                {
                        value v;
                        int i = 0;
                        while (i != p->u.var.locals.len && res) {
                                res = cpp(p->u.var.locals.list[i].rhs, s, &v);
                                if (res) {
                                     assign(s, p->u.var.locals.list[i].lhs, v);
                                }
                                i++;
                        }
                        if (res) {        
                                res = cp(p->u.var.cmd, s);
                        }
                }
                break;
        case CALL:
                if (p->u.call.p.pdecl == (procdecl*)0) {
                        res = (* p->u.call.p.exec)(p, s);
                } else {
                        int i; 
                        value inval[MAXINS];
                        vallist ins;
                        ins.len = p->u.call.ins.len;
                        if (ins.len > MAXINS) {
                                ins.list = 
                                  (value *) emalloc(ins.len*sizeof(value));
                        } else {
                                ins.list = inval;
                        }
                        i = 0;
                        while (i != ins.len && res) {
                                res = cpp(p->u.call.ins.list[i],
                                          s, &ins.list[i]);
                                i++;
                        }
                        if (res) {
                                newframe(s, p->u.call.outs.len, 
                                         p->u.call.inouts, ins,
                                         p->u.call.p.pdecl->framesize);
                                res = cp(p->u.call.p.pdecl->body, s);
                                oldframe(s, p->u.call.outs,
                                	 p->u.call.inouts, res);
                        }
                        if (ins.len > MAXINS) {
                                free((char *) ins.list);
                        }
                }
                break;
         case GETS:
                 {
                         value rval;
                         res = cpp(p->u.gets.rhs, s, &rval);
                         if (res) {
                                 assign(s, p->u.gets.lhs, rval);
                         }
                  }
                  break;
         default: 
                 abort();
         }
         return (res);
}

int idiv(int n, int m)
{
        if (m < 0) {
                m = -m;
                n = -n;
        }
        if (n < 0) {
                return (-((-n + m - 1) / m));
        } else {
                return (n/m);
        }
}                        

int imod(int n, int m)
{
        return (n - m * idiv(n, m));
}
                        
int cpp(node *e, state *s, value *v)
{
        int res = 1;
        switch (e->type)
        {
        case BINOP:
                if (e->u.binop.op == AND) {
                        res = cpp(e->u.binop.left, s, v);
                        if (res && v->intval) {
                                res = cpp(e->u.binop.right, s, v);
                        }
                } else if (e->u.binop.op == OR) {
                        res = cpp(e->u.binop.left, s, v);
                        if (res && !v->intval) {
                                res = cpp(e->u.binop.right, s, v);
                        }
                } else {
                        value larg; 
                        value rarg;
                        res = cpp(e->u.binop.left, s, &larg) &&
                              cpp(e->u.binop.right, s, &rarg);
                        if (res) {
                            switch (e->u.binop.op)
                            {
                            case '+': v->intval = (larg.intval + rarg.intval); break;
                            case '-': v->intval = (larg.intval - rarg.intval); break;
                            case '*': v->intval = (larg.intval * rarg.intval); break;
                            case '/': if (0 == rarg.intval) {
                                        limerr("division by zero", "", e->line);
                                        abort();
                                    } else {
                                        v->intval = idiv(larg.intval, rarg.intval); 
                                        break;
                                    }
                            case '%': if (0 == rarg.intval) {
                                        limerr("mod by zero", "", e->line);
                                        abort();
                                    } else {
                                        v->intval = imod(larg.intval, rarg.intval); 
                                        break;
                                    }
                            case '<': v->intval = (larg.intval < rarg.intval); break;
                            case '>': v->intval = (larg.intval > rarg.intval); break;
                            case  LE: v->intval = (larg.intval <= rarg.intval); break;
                            case  GE: v->intval = (larg.intval >= rarg.intval); break;
                            case '#': v->intval = (larg.intval != rarg.intval); break;
                            case '=': v->intval = (larg.intval == rarg.intval); break;
                            default: abort();
                            }
                        }
                  }
                  break;
        case UNOP:
                {
                        value arg; 
                        res = cpp(e->u.unop.arg, s, &arg);
                        if (res) {
                            switch (e->u.unop.op)
                            {
                            case '-': v->intval = -arg.intval; break;
                            case NOT: v->intval = !arg.intval; break;
                            default: abort();
                            }
                        }
                  }
                  break;
        case INTCONSTANT:
                *v = e->u.constant.val;
                break;
        case VARUSE:
                *v = lookup(s, e->u.varuse.var);
                break;
        case CALL:
                if (e->u.call.p.pdecl==(procdecl*)0) {
                        if (1 != e->u.call.outs.len) {abort();}
                        res = (* e->u.call.p.exec)(e, s);
                        if (res) {
                                *v = lookup(s, e->u.call.outs.list[0]);
                        }
                } else {
                        int i; 
                        value inval[MAXINS];
                        vallist ins;
                        ins.len = e->u.call.ins.len;
                        if (ins.len > MAXINS) {
                                ins.list = 
                                  (value *) emalloc(ins.len*sizeof(value));
                        } else {
                                ins.list = inval;
                        }
                        i = 0;
                        while (i != ins.len && res) {
                                res = cpp(e->u.call.ins.list[i], s,
                                	  &ins.list[i]);
                                i++;
                        }
                        if (1 != e->u.call.outs.len) {abort();}
                        if (res) {
                                newframe(s, 1, e->u.call.inouts, ins,
                                    e->u.call.p.pdecl->framesize);
                                res = cp(e->u.call.p.pdecl->body, s);
                                oldframe(s, e->u.call.outs,
                                	 e->u.call.inouts, res);
                                *v = lookup(s, e->u.call.outs.list[0]);
                        }
                        if (ins.len > MAXINS) {
                                free((char *) ins.list);
                        }
                }
                break;
        default:
                abort();
             
        }
        return (res);
}
   
int initialize(node *p, state *s)
{
        int i;
        int res=1;
        value v;
        while (res && p != (node*)0) {
                if (p->type == VARDECL) {
                        i = 0;
                        while (res && i != p->u.decl.u.globals.len) {
                                res = cpp(p->u.decl.u.globals.list[i].rhs, s, &v);
                                if (res) {
                                     assign(s, p->u.decl.u.globals.list[i].lhs, v);
                                }
                                i++;
                        }
                } else if (p->type != PROCDECL) {
                        abort();
                }
                p = p->u.decl.link;
        }
        return (res);
}

int exec_rd(struct node *p, state *s)
{
        value c;
        c.intval = limrd_getchar(s->input);
        if (c.intval == -1) {
                return (0);
        } else {
                assign(s, p->u.call.outs.list[0], c);
                return (1);
        }
}

int exec_rdx(struct node *p, state *s)
{
       node *arg = p->u.call.ins.list[0];
       if (STRCONSTANT == arg->type) {
               return(limrd_consume(s->input, arg->u.strconst.val,
                                    arg->u.strconst.vallen));
       } else {
               value v;
               if (cpp(arg, s, &v)) {
                       char c = v.intval;
                       return (limrd_consume(s->input, &c, 1));
               } else {
                       return (0);
               }
       } 
}

int exec_at(struct node *p, state *s)
{
       node *arg = p->u.call.ins.list[0];
       if (STRCONSTANT == arg->type) {
               return(limrd_at(s->input, arg->u.strconst.val,
                                    arg->u.strconst.vallen));
       } else {
               value v;
               if (cpp(arg, s, &v)) {
                       char c = v.intval;
                       return (limrd_at(s->input, &c, 1));
               } else {
                       return (0);
               }
       } 
}

int exec_wr(struct node *p, state *s)
{
       node *arg = p->u.call.ins.list[0];
       if (STRCONSTANT == arg->type) {
               limwr_puts(s->output, arg->u.strconst.val,
                                    arg->u.strconst.vallen);
               return (1);
       } else {
               value v;
               if (cpp(arg, s, &v)) {
                       char c = v.intval;
                       limwr_puts(s->output, &c, 1);
                       return (1);
               } else {
                       return (0);
               }
       } 
}

int exec_err(struct node *p, state *s)
{
       node *arg = p->u.call.ins.list[0];
       if (STRCONSTANT == arg->type) {
               fwrite(arg->u.strconst.val, 1, arg->u.strconst.vallen,
                      stderr);
               fflush(stderr); 
               return (1);
       } else {
               value v;
               if (cpp(arg, s, &v)) {
                       putc(v.intval, stderr);
                       fflush(stderr);
                       return (1);
               } else {
                       return (0);
               }
       } 
}


/*ARGSUSED*/
int exec_eof(struct node *p, state *s)
{
        return (limrd_eof(s->input));
}
