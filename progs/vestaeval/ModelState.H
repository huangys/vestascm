// Copyright (C) 2001, Compaq Computer Corporation
// 
// This file is part of Vesta.
// 
// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

/* File: ModelState.H                                                 */
/* Last Modified On Fri Aug  6 11:25:08 EDT 2004 by ken@xorian.net */
/*      Modified On Fri Jun 20 18:15:16 PDT 1997 by yuanyu     */
/*      Modified On Tue Feb 27 15:19:44 PST 1996 by levin      */
/*      Modified On Thu Jan 25 12:38:17 PST 1996 by horning    */

#ifndef ModelState_H
#define ModelState_H

#include "EvalBasics.H"
#include "ValExpr.H"
#include "VestaSource.H"

// Globals associated with processing a model.
extern std::istream *lexIn;              // input for lexer
extern char lookAhead1, lookAhead2; // character lookaheads
extern int lookaheads;              // if there are character lookaheads
extern VestaSource *mRoot;          // directory in which model resides
extern VestaSource *rRoot;          // root directory of repository (const)

void ModelStateInit(std::istream *in, VestaSource *modelRoot);

#endif // ModelState_H


