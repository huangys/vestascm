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

// Last modified on Thu Nov  8 12:36:38 EST 2001 by ken@xorian.net  
//      modified on Fri Jul 27 19:04:05 PDT 2001 by mann  
//      modified on Mon Aug 31 14:12:42 PDT 1998 by heydon

// NewVal.H -- debugging interface for constructing new cache-related values

#ifndef _NEW_VAL_H
#define _NEW_VAL_H

#include <Basics.H>
#include <FP.H>
#include "BitVector.H"
#include "FV.H"
#include "Model.H"
#include "CacheIndex.H"
#include "Derived.H"
#include "VestaVal.H"

class NewVal {
  public:
    static void NewNames(/*OUT*/ FV::List& names, unsigned int num = 0)
      throw ();
    static void NewFP(/*OUT*/ FP::Tag& fp, int vals = 3) throw ();
    static void NewFPs(/*OUT*/ FP::List& fps, unsigned int num = 0,
		       int vals = 3) throw ();
    static void NewModel(/*OUT*/ Model::T &model) throw ();
    static void NewCI(/*OUT*/ CacheEntry::Index& index) throw ();
    static void NewCIs(/*OUT*/ CacheEntry::Indices& kids, unsigned int num = 0)
      throw ();
    static void NewDI(/*OUT*/ Derived::Index& index) throw ();
    static void NewDIs(/*OUT*/ Derived::Indices& kids, unsigned int num = 0)
      throw ();
    static void NewValue(/*OUT*/ VestaVal::T& value) throw ();
    static void NewBV(/*OUT*/ BitVector &bv) throw ();
    static void NewBool(/*OUT*/ bool &b) throw ();
};

#endif // _NEW_VAL_H
