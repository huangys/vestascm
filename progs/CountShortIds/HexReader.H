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

// Created on Thu Aug 21 22:45:34 PDT 1997 by heydon
// Last modified on Mon Aug  9 18:03:02 EDT 2004 by ken@xorian.net
//      modified on Thu Aug 21 23:35:29 PDT 1997 by heydon

// HexReader.H -- class for reading files containing ShortIds, one per line

#ifndef _HEX_READER_H
#define _HEX_READER_H

#include <Basics.H>
#include <FS.H>
#include <SourceOrDerived.H>

class HexReader {
  public:
    // exception class
    class BadValue {
      public:
	int lineNum;
	Text str;
	BadValue(int lineNum, char *str) throw ()
	    : lineNum(lineNum), str(str) { /*SKIP*/ }
    };

    HexReader(char *path) throw (FS::DoesNotExist, FS::Failure);
    /* Open a new HexReader on the file named by "path". */

    ~HexReader() throw (FS::Failure);
    /* Close this HexReader. */

    ShortId Next() throw (FS::Failure, BadValue);
    /* Read and return the next hex value in the file. A return value of 0
       indicates end-of-file. Throw "BadValue" if the line is not a hex
       value. */
  private:
    std::ifstream ifs;
    int lineNum;
};

#endif // _HEX_READER_H
