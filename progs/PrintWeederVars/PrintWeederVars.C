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

// Created on Fri Apr  9 15:37:09 PDT 1999 by heydon
// Last modified on Sat Apr 30 16:40:04 EDT 2005 by ken@xorian.net
//      modified on Mon Apr 12 09:53:18 PDT 1999 by heydon

/* Syntax: PrintWeederVars

   This program prints the weeder's stable variables in a human-readable
   form.
*/

#include <SourceOrDerived.H>
#include <BitVector.H>
#include "WeederConfig.H"
#include "RootTbl.H"

using std::ifstream;
using std::cout;
using std::endl;
using std::hex;
using std::dec;

int main() {
    time_t startTime, keepTime;
    ShortId disShortId;
    BitVector *weeded;
    RootTbl *roots;

    // read variables
    ifstream ifs;
    try {
	try {
	    FS::OpenReadOnly(Config_WeededFile, /*OUT*/ ifs);
	    weeded = NEW_CONSTR(BitVector, (ifs));
	    FS::Close(ifs);
	}
	catch (FS::DoesNotExist) {
	  weeded = NEW(BitVector);
	}
    
	try {
	    FS::OpenReadOnly(Config_MiscVarsFile, /*OUT*/ ifs);
	    FS::Read(ifs, (char *)(&(startTime)), sizeof(startTime));
	    FS::Read(ifs, (char *)(&(keepTime)), sizeof(keepTime));
	    FS::Read(ifs, (char *)(&(disShortId)), sizeof(disShortId));
	    roots = NEW_CONSTR(RootTbl, (ifs));
	    FS::Close(ifs);
	}
	catch (FS::DoesNotExist) {
	    startTime = -1;
	    keepTime = -1;
            disShortId = 0;
            roots = NEW(RootTbl);
	}
    }
    catch (FS::EndOfFile) {
	// programming error
	assert(false);
    }

    // print variables
    cout << "*** Start Time ***" << endl;
    cout << "  startTime = ";
    if(startTime != -1)
      {
	// The result of ctime has a newline at the end, so we indent
	// the integer timestamp to line up with the text one.
	cout << ctime(&startTime)
	     << "              (" << startTime << ")";
      }
    else
      {
	cout << startTime;
      }
    cout << endl;
    cout << "  keepTime  = ";
    if(keepTime != -1)
      {
	cout << ctime(&keepTime)
	     << "              (" << keepTime << ")";
      }
    else
      {
	cout << keepTime;
      }
    cout << endl << endl;
    cout << "*** ShortId of DIs file ***" << endl;
    cout << "  disShortId = 0x" << hex << disShortId << dec << endl << endl;
    cout << "*** Table of GraphLog Roots to keep ***" << endl;
    cout << "  Size = " << roots->Size() << endl;
    if (roots->Size() > 0) {
        cout << "  Content = " << endl;
        roots->Print(cout, /*indent=*/ 4,
		     "[explicit]", "[fresh]");
    }
    cout << endl;
    cout << "*** Weeded set ***" << endl;
    int card = weeded->Cardinality();
    cout << "  Size = " << card << endl;
    if (card > 0) {
        cout << "  Content =" << endl;
        weeded->PrintAll(cout, /*indent=*/ 4, /*maxWidth=*/ 76);
        cout << endl;
    }
}
