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

/* File: Err.H                                                 */

#ifndef Err_H
#define Err_H

#include <VestaSource.H>
#include <ThreadIO.H>

#include "ValExpr.H"
#include "Location.H"
#include "ModelState.H"

// Count an error:
void Error();

// Return error count:
int ErrorCount();

// Print and count an error, with location, if available:
void Error(std::ostream& err, const Text &msg, SrcLoc *loc = noLoc);

// Print a warning, with location, if available:
void Warning(std::ostream& err, const Text &msg, SrcLoc *loc = noLoc);

// Append msg as part of the current error message:
void ErrorDetail(std::ostream& err, const Text &msg);

// Append the text form of v as part of the current error message:
void ErrorVal(std::ostream& err, const Val v);

// Append the text form of e as part of the current error message:
void ErrorExpr(std::ostream& err, const Expr e);

// Append the text form of args as part of the current error message:
void ErrorArgs(std::ostream& err, const Vals &args);

// Report "impossible" state encountered in procedure procName.
// It is used to indicate implementation errors.
//
// Note that this function never returns as it terminates the process.
// It needs "err_helper" to go with "err" so that it can force
// buffered output to be flushed before teminating the program.
void InternalError(std::ostream& err, const OS::ThreadIOHelper &err_helper,
		   const Text &procName);

// Print the number of errors encounted since the last ErrInit().
// Return true iff there is an error.
bool ErrorSummary(std::ostream& out);

// Record the error of evaluating expression expr on the stack:
Val RecordErrorOnStack(Expr expr);

/* Return a string representation of the VestaSource errorCode "err"
   suitable for use in human-readable error messsages. This function
   returns a pointer to static storage. */
const Text VestaSourceErrorMsg(VestaSource::errorCode err) throw ();

#endif // Err_H
