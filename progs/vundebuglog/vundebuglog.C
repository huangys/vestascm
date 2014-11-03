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

// vundebuglog.C
//
// Blindly catenate the data fields from vdebuglog output into a
// plain file, ignoring the block numbers, lengths, and version
// numbers.  Obviously, you need to have sorted the blocks first for
// this to be useful.

// Raw dump of VestaLog format

#include "VestaLog.H"
#include "VestaLogPrivate.H"

int main(int argc, char* argv[])
{
    char xdata[2*DiskBlockSize];
    FILE* f = fopen(argv[1], "r");

    for (;;) {
	if (fscanf(f, "%*u %*u %*u %s", xdata) != 1) exit(0);
	char *p = xdata;
	for (int i = 0; i < 2*(DiskBlockSize - 6); i += 2) {
	    int byte;
	    sscanf(xdata + i, "%2x", &byte);
	    putchar(byte);
	}
    }
}
