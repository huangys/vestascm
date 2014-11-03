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

// File: ApplyCache.H
// Last modified on Fri Nov  9 14:02:43 EST 2001 by ken@xorian.net 
//      modified on Wed Apr 12 16:32:33 PDT 2000 by yuanyu 
//      modified on Thu Feb 29 16:11:54 PST 1996 by levin  
//      modified on Fri Jan 26 16:56:30 PST 1996 by horning

#ifndef ApplyCache_H
#define ApplyCache_H

#include "ValExpr.H"
#include "Location.H"

// Apply a closure to arguments in the context c:
Val ApplyFunction(ClosureVC *clos, ApplyEC *ae, const Context& c);
Val ApplicationFromCache(ClosureVC *clos, const Context& argsCon, SrcLoc *loc);

// Apply a model to arguments in the context c:
Val ApplyModel(ModelVC *model, ApplyEC *ae, const Context& c);
Val ModelFromCache(ModelVC *model, const Context& argsCon, SrcLoc *loc);

// Apply the RunTool in the context c:
Val ApplyRunTool(ArgList args, const Context& c);

void EvalTagsInit();
void StartRenewLeaseThread();

#endif  // ApplyCache_H
