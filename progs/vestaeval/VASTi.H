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

// File: VASTi.H                                              

#ifndef VASTi_H
#define VASTi_H

#include "EvalBasics.H"
#include <SRPC.H>

// Parse and evaluate a model:
// "model" is the name of the top-level model to be evaluated.
bool StartEval(const Text& model);

// Print error stack if asked:
void PrintErrorStack(std::ostream& vout);

#endif // VASTi_H
