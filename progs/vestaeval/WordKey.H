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

// File: IndexKey.H
// Last Modified On Sat Feb 12 17:47:47 PST 2000 by mann   
//      Modified On Tue Mar 17 18:50:47 PST 1998 by yuanyu 
//      Modified On Fri Mar  1 12:49:46 PST 1996 by levin

#ifndef WordKey_H
#define WordKey_H

#include <Basics.H>

class WordKey {
  /* Used to instantiate Table with Word as key. */
public:
  WordKey() { };
  WordKey(Word w) { word = w; };
  Word Hash() const throw() { return word; };
  friend int operator==(WordKey k1, WordKey k2) 
    { return k1.word == k2.word; };
protected:
  Word word;
};

#endif // WordKey_H
