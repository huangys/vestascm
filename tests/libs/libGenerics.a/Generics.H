// Copyright (C) 2001, Compaq Computer Corporation

// This file is part of Vesta.

// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// Last modified on Mon Apr 18 16:23:53 EDT 2005 by irina.furman@intel.com 
//      modified on Wed Jun  2 18:17:20 EDT 2004 by ken@xorian.net        
//      modified on Mon May  3 16:04:45 PDT 1999 by heydon
//      modified on Tue Dec  5 10:53:35 PST 1995 by levin

//  ****************
//  *  Generics.H  *
//  ****************

// Common generic types

#ifndef _Generics
#define _Generics

#include <Basics.H>
#include "Sequence.H"
#include "Table.H"
#include "IntKey.H"
#include "Atom.H"

// Sequences
typedef Sequence<int, true> IntSeq;
typedef Sequence<Text> TextSeq;
typedef Sequence<Atom> AtomSeq;

// Tables (for each table, there is a table and an interator type)
typedef Table<Text,int>::Default TextIntTbl;
typedef Table<Text,int>::Iterator TextIntIter;
typedef Table<Text,Text>::Default TextTextTbl;
typedef Table<Text,Text>::Iterator TextTextIter;
typedef Table<Text,const char*>::Default TextStringTbl;
typedef Table<Text,const char*>::Iterator TextStringIter;
typedef Table<IntKey,int>::Default IntIntTbl;
typedef Table<IntKey,int>::Iterator IntIntIter;

#endif  /* _Generics  */
