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

// Last modified on Fri Jul 16 17:21:26 EDT 2004 by ken@xorian.net  
//      modified on Sat Feb 12 17:37:19 PST 2000 by mann  
//      modified on Fri Oct 18 10:36:06 PDT 1996 by heydon

#include <Basics.H>
#include "FP.H"
#include "UniqueId.H"

using std::cout;
using std::endl;

const int NumIds = 1000;

main () 
{
    for (int i = 0; i < NumIds; i++) {
	cout << UniqueId() << endl;
    }
}
