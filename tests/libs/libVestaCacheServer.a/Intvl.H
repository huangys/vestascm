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

// Last modified on Thu Aug  5 00:02:06 EDT 2004 by ken@xorian.net
//      modified on Sun Aug 30 22:56:44 PDT 1998 by heydon

/* Records an interval of cache indices that should be added or removed from
   the set of cache indices currently in use. */

#ifndef _INTVL_H
#define _INTVL_H

#include <Basics.H>
#include <VestaLog.H>
#include <Recovery.H>
#include "CacheEntry.H"

// the closed interval "[lo, hi]"
namespace Intvl
{
  // type for indicating
  enum Op { Remove, Add };

  class T {
  public:
    Intvl::Op op;
    CacheEntry::Index lo, hi;
    T() throw () { /*SKIP*/ }
    T(RecoveryReader &rd) throw (VestaLog::Error, VestaLog::Eof)
    { Recover(rd); }

    // log/recover
    void Log(VestaLog &log) const throw (VestaLog::Error);
    void Recover(RecoveryReader &rd)
      throw (VestaLog::Error, VestaLog::Eof);

    // print
    void Debug(std::ostream &s) const throw ();

  private:
    // hide copy constructor
    T(const T &);
  };

  class List {
  public:
    Intvl::List *next;
    Intvl::T i;
    List() throw () { /*SKIP*/ }
  private:
    // hide copy constructor
    List(const List &);
  };
}

#endif // _INTVL_H
