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

/* File: ModelState.C                                          */
/* Last modified on Fri Aug  6 13:12:57 EDT 2004 by ken@xorian.net     */
/*      modified on Fri Jun 20 18:15:36 PDT 1997 by yuanyu     */
/*      modified on Tue Feb 27 15:33:52 PST 1996 by levin      */
/*      modified on Thu Jan 25 12:39:02 PST 1996 by horning    */

#include "ModelState.H"
#include "Lex.H"
#include <iostream>

using std::istream;

void ModelStateInit(istream *in, VestaSource *modelRoot) {
  lexIn      = in;
  lookaheads = 0;
  lineNumber = 1;
  charNumber = 0;
  mRoot      = modelRoot;
}
