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

// Last modified on Thu Aug  5 10:04:00 EDT 2004 by ken@xorian.net  
//      modified on Sat Feb 12 17:30:55 PST 2000 by mann  
//      modified on Mon Sep 22 11:51:16 PDT 1997 by heydon

// A test for the "PKPrefix" interface

#include <Basics.H>
#include <FP.H>
#include "PKPrefix.H"

using std::cout;
using std::endl;

static const int PKBits = 8 * sizeof(Word);
static const int BitsPerArc = 4;
static const int LoArcSz = 3;
static const int HiArcSz = 5;

int main() 
{
    FP::Tag fp("Here is a random fingerprint");
    cout << "Original FP:" << endl;
    cout << "  " << fp << endl;

    for (int arcSz = LoArcSz; arcSz <= HiArcSz; arcSz++) {
	cout << "\nPrefixes (arc size = " << arcSz << " characters):\n";
	int arcBits = arcSz * BitsPerArc;
	for (int i = PKBits - arcBits; i > -arcBits; i -= arcBits) {
	    i = max(i, 0);
	    PKPrefix::T pfx(fp, i);
	    cout << "  " << pfx << " -> ";
	    cout << "\"" << pfx.Pathname(i, arcBits) << "\"" << endl;
	}
    }
    return 0;
}
