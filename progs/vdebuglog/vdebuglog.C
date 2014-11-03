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

// Raw dump of VestaLog format

#include "VestaLog.H"
#include "VestaLogPrivate.H"

// copied from VestaLog.C
static unsigned int HashSeq(int seq)
{
    return ((unsigned int) seq + 12345u) * 715827881u;
}

int main(int argc, char* argv[])
{
    VLogBlock block;
    FILE* f = fopen(argv[1], "r");

    for (;;) {
	if (fread(block.data, 512, 1, f) != 1) exit(0);
	printf("%u %u %u ",
	       block.data->getSeq(), block.data->getLen(),
	       block.data->getVer());
	int i;
	for (i=0; i<sizeof(block.data->bytes); i++) {
	    printf("%02x", block.data->bytes[i]);
	}
	printf("\n");
    }
}
