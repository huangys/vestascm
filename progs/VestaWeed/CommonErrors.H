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

// Created on Wed Mar 12 20:55:43 PST 1997 by heydon

// CommonErrors.H -- defines common error classes that can occur from
//   input, system, and repository operations

#ifndef _COMMON_ERRORS_H
#define _COMMON_ERRORS_H

#include <errno.h>
#include <Basics.H>
#include <VestaSource.H>

/* An "InputError" exception is thrown to indicate an error in the
   weeder instructions input file. */
class InputError {
  public:
    InputError(const char *error_msg, const char *arg = "") throw ()
      : msg(error_msg), arg(arg) { /* SKIP */ }
    friend std::ostream& operator<<(std::ostream &os, const InputError &err) throw ();
    Text arg; // must be public for manipulation by "ProcessPattern"
  private:
    const char *msg;
};

/* A "SysError" exception is thrown to indicate the failure of a
   system call. */
class SysError {
  public:
    SysError(const char *routine) throw ()
	: routine(routine) { this->myerrno = errno; }
    friend std::ostream& operator<<(std::ostream &os, const SysError &err) throw ();
  private:
    const char *routine; // for example, "stat(2)"
    int myerrno;
};

/* A "ReposError" exception is thrown to indicate the failure of
   an operation on the Vesta repository server. */
class ReposError {
  public:
    ReposError(VestaSource::errorCode errCode, const char *op,
      const Text &arg = "") throw ()
      : code(errCode), op(op), arg(arg) { /*SKIP*/ }
    friend std::ostream& operator<<(std::ostream &os, const ReposError &err) throw ();
  private:
    VestaSource::errorCode code; // repository error code
    const char *op;  // failing operation
    const Text arg;  // optional argument
};

#endif // _COMMON_ERRORS_H
