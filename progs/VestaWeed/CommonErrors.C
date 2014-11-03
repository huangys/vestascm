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

// Created on Wed Mar 12 20:58:35 PST 1997 by heydon

// Last modified on Mon Aug  9 18:35:17 EDT 2004 by ken@xorian.net
//      modified on Wed Jun 10 00:31:16 PDT 1998 by heydon

#include <errno.h>
#include <Basics.H>
#include <VestaSource.H>
#include "CommonErrors.H"

using std::ostream;
using std::endl;

ostream& operator<< (ostream &os, const InputError &err) throw ()
/* Print a representation of "err" to "os". */
{
    os << "  " << err.msg;
    if (!err.arg.Empty()) {
	os << ":" << endl;
        os << "  '" << err.arg << "'";
    }
    os << endl;
    return os;
}

ostream& operator<<(ostream &os, const SysError &err) throw ()
/* Print a representation of "err" to "os". */
{
    os << "System error in " << err.routine
       << " (errno = " << err.myerrno << ")" << endl;
    os << "  " << Basics::errno_Text(err.myerrno) << endl;
    return os;
}

ostream& operator<<(ostream &os, const ReposError &err) throw ()
/* Print a representation of "err" to "os". */
{
    os << "Repository error: " << VestaSource::errorCodeString(err.code)
      << " (code = " << err.code << ")" << endl;
    os << "  Failing operation: " << err.op << "" << endl;
    if (!err.arg.Empty()) {
	os << "  Argument: '" << err.arg << "'" << endl;
    }
    return os;
}
