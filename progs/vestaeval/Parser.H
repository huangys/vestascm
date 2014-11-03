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

/* File: Parser.H                                              */
/* Last Modified On Fri Aug  6 11:59:57 EDT 2004 by ken@xorian.net */
/*      Modified On Fri Jan 23 23:36:33 PST 1998 by yuanyu     */
/*      Modified On Fri Jan 26 18:23:25 PST 1996 by horning    */
/*      Modified On Fri Oct  8 13:47:06 PDT 1993 by hanna      */

#ifndef Parser_H
#define Parser_H

#include "EvalBasics.H"
#include "Expr.H"
#include <VestaSource.H>
#include <iostream>
#include <fstream>

// Parse a Vesta 2 SDL model:
Expr Parse(std::istream *in,        // istream for the model
	   const Text& name,        // model file name
	   ShortId shortId,         // short id of model name
	   VestaSource *modelRoot); // directory of the model file

#endif // Parser_H
