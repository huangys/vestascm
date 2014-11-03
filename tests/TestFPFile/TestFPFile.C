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

// Done on Fri Aug  1 11:13:49 PDT 1997 by heydon
// Last modified on Fri Jul 16 17:11:00 EDT 2004 by ken@xorian.net  
//      modified on Wed Sep  6 18:35:29 PDT 2000 by mann  
//      modified on Fri Aug  1 15:37:53 PDT 1997 by heydon

/* Syntax: TestFPFile filename

   Open the file named "filename", fingerprint its contents, close the
   file, and print the resulting fingerprint.
*/

#include <Basics.H>
#include <FS.H>
#include <FP.H>

using std::cerr;
using std::cout;
using std::endl;
using std::ifstream;

void Syntax(char *msg) throw ()
{
    cerr << "Error: " << msg << endl;
    cerr << "Syntax: TestFPFile filename" << endl;
    exit(1);
}

void DoWork(const Text &fname, /*INOUT*/ FP::Tag &fp) throw (FS::Failure)
{
    ifstream ifs;
    FS::OpenReadOnly(fname, /*OUT*/ ifs);
    FP::FileContents(ifs, /*INOUT*/ fp);
    FS::Close(ifs);
}

int main(int argc, char *argv[])
{
    if (argc != 2) Syntax("incorrect number of arguments");
    Text filename(argv[1]);
    FP::Tag fp("");
    try {
	DoWork(filename, /*INOUT*/ fp);
	cout << fp << endl;
    }
    catch (const FS::Failure &f) {
	cerr << f;
    }
    return 0;
}
