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

// Created on Thu Aug 21 22:47:07 PDT 1997 by heydon
// Last modified on Fri Nov  9 14:31:15 EST 2001 by ken@xorian.net
//      modified on Thu Aug 21 23:33:17 PDT 1997 by heydon

#include <Basics.H>
#include <FS.H>
#include <SourceOrDerived.H>

#include "HexReader.H"

HexReader::HexReader(char *path) throw (FS::DoesNotExist, FS::Failure)
: lineNum(1)
{
    FS::OpenReadOnly(Text(path), /*OUT*/ this->ifs);
}

HexReader::~HexReader() throw (FS::Failure)
{
    FS::Close(this->ifs);
}
 
ShortId HexReader::Next() throw (FS::Failure, HexReader::BadValue)
{
    // check for EOF
    if (FS::AtEOF(this->ifs)) return 0;

    // read next line of file
    const int BuffSz = 60;
    char buff[BuffSz];
    ifs.getline(buff, BuffSz);
    if (!ifs) {
	char lnbuff[10]; sprintf(lnbuff, "%d", this->lineNum);
	Text arg("line "); arg += lnbuff;
	throw FS::Failure("ifstream.getline", arg);
    }

    // convert it to a ShortId
    char *val = buff;
    if (strncmp(val, "0x", 2) == 0) val += 2;
    ShortId res;
    if (sscanf(val, "%x", /*OUT*/ &res) != 1) {
	throw HexReader::BadValue(this->lineNum, buff);
    }

    // success
    this->lineNum++;
    return res;
}

