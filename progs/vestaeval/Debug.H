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

// File: Debug.H

#ifndef Debug_H
#define Debug_H

#include "EvalBasics.H"
#include "ValExpr.H"
#include "Val.H"

bool CacheIt(Val v);
// Return TRUE iff v is cacheable.

void PrintVars(std::ostream& os, const Vars& fv);
// Print a list of variables.

void PrintTags(std::ostream& os, const FP::List& tags);
// Print a list of tags.

void PrintNames(std::ostream& os, const FV2::List& names);
// Print a list of names.

void PrintDpnd(std::ostream& os, Val v);
// Print the top-level dependency set of the value v.

void PrintAllDpnd(std::ostream& os, Val v);
// Print the complete dependency set of the value v.

Val ContextNames(const Context& c);
// Returns the list of names in the context c.

void PrintDpndSize(std::ostream& os, Val v);
// Print the sizes of the dps's nested in the value v.

void PrintDepsDiffs(const Text &source, const FP::Tag &pk, CacheEntry::Index cIndex, 
		    Val old_v, Val new_v, const Text &name = "result");
// Print the difference between the dependency information recorded in
// two values.  (Used for checking the effect of changes to the
// evaluator's code.  See the -dependency-check option.)

#endif // Debug_H
