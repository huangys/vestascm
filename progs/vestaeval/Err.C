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

/* File: Err.C                                                 */

#include <VestaSource.H>
#include <ReposUI.H>
#include "Err.H"
#include "EvalBasics.H"
#include "ValExpr.H"
#include "Val.H"
#include "Expr.H"
#include "Location.H"
#include "ThreadData.H"

using std::ostream;
using std::endl;
using std::flush;
using OS::cio;
int errorCount = 0;

void Error() {
  errorCount++;
}

int ErrorCount() {
  return errorCount;
}

void Error(ostream& err, const Text &msg, SrcLoc *loc) {
  errorCount++;
  if (loc != NULL && !loc->IsNone()) {
    err << *loc << ":" << endl;
  }
  // Try to add a line break if "msg" contains "Current token class"
  int i = msg.FindText("Current token class");
  if (i < 0) {
    err << ThreadLabel() << "Error: " << msg;
  } else {
    err << ThreadLabel() << "Parse error: " << msg.Sub(0, i) << endl << msg.Sub(i);
  }
  err << flush;
}

void Warning(std::ostream& err, const Text &msg, SrcLoc *loc)
{
  if (loc != NULL && !loc->IsNone()) {
    err << *loc << ":" << endl;
  }
  err << ThreadLabel() << "Warning: " << msg << endl;
}

void ErrorVal(ostream& err, const Val v) 
{
  v->PrintD(err);
  err << flush;
}

void ErrorExpr(ostream& err, const Expr e) 
{
  e->PrintD(err);
  err << flush;
}

void ErrorDetail(ostream& err, const Text &msg) 
{
  err << msg << flush;
}

void InternalError(ostream& err, const OS::ThreadIOHelper &err_helper,
		   const Text &procName)
{
  errorCount++;
  err << endl << "Vesta evaluator: Internal error in procedure "
      << procName << endl;
  err_helper.end(true);
  abort();
}

void ErrorArgs(ostream& err, const Vals &args) 
{
  Vals work = args;
  
  err << "(";
  while (!work.Null()) {
    work.Pop()->PrintD(err);
    if (!work.Null())
      err << ", ";
  }
  err << ")";
  return;
}

bool ErrorSummary(ostream& out) 
{
  if (errorCount == 0) {
    //out << "\nNo errors were reported.\n";
    return false;
  }
  if (errorCount == 1)
    out << endl << "One error was reported." << endl;
  else
    out << endl << errorCount << " errors were reported." << endl;
  return true;
}

Val RecordErrorOnStack(Expr expr) {
  if (recordCallStack) {
    callStackMu.lock();          
    ThreadDataGet()->callStack->addlo(expr);
    callStackMu.unlock();              
  }
  Val result = NEW(ErrorVC);
  if (recordCallStack) {
    callStackMu.lock();              
    ThreadDataGet()->callStack->remlo();
    callStackMu.unlock();              
  }
  return result;
}

const Text VestaSourceErrorMsg(VestaSource::errorCode err) throw ()
{
  return ReposUI::errorCodeText(err);
}
