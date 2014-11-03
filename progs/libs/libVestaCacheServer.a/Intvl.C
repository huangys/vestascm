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

// Last modified on Thu Aug  5 00:02:42 EDT 2004 by ken@xorian.net
//      modified on Fri Feb 27 18:44:51 PST 1998 by heydon

#include <Basics.H>
#include <VestaLog.H>
#include <Recovery.H>

#include "CacheEntry.H"
#include "Intvl.H"

using std::ostream;
using std::endl;

void Intvl::T::Log(VestaLog &log) const throw (VestaLog::Error)
{
    log.write((char *)(&(this->op)), sizeof(this->op));
    log.write((char *)(&(this->lo)), sizeof(this->lo));
    log.write((char *)(&(this->hi)), sizeof(this->hi));
}

void Intvl::T::Recover(RecoveryReader &rd)
  throw (VestaLog::Error, VestaLog::Eof)
{
    rd.readAll((char *)(&(this->op)), sizeof(this->op));
    rd.readAll((char *)(&(this->lo)), sizeof(this->lo));
    rd.readAll((char *)(&(this->hi)), sizeof(this->hi));
}

void Intvl::T::Debug(ostream &s) const throw ()
{
    s << "  interval = " << ((op == Intvl::Add) ? "+" : "-")
      << " [" << lo << ", " << hi << "]" << endl;
}
