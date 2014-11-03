// Copyright (C) 2005, Vesta Free Software Project
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

#include <ReposUI.H>
#include <Text.H>
#include <VestaSource.H>
#include <iostream>

using std::cout;
using std::endl;
int main(int argc, char **argv)
{
	Text path(argv[1]);
	VestaSource *vs = ReposUI::filenameToVS(path);
	Text ov = ReposUI::prevVersion(vs);
	cout << path << " is based on " << ov << endl;
	delete vs;
	return 0;
}
