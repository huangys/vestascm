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

// Last modified on Wed Apr 20 02:54:41 EDT 2005 by ken@xorian.net  
//      modified on Wed Apr  5 11:48:10 PDT 2000 by mann  
//      modified on Fri Feb  5 17:18:10 PST 1999 by heydon
//      modified on Tue Apr  9 11:54:22 PDT 1996 by levin

#include "Basics.H"
#include "RunToolClient.H"
#include <stdlib.h>

using std::cout;
using std::cerr;
using std::endl;

int main(int argc, char *argv[])
{
  RunTool::Host_info h;
  if (argc < 2) {
    cerr << "Usage: RunToolProbe host" << endl;
    return 2;
  }
  try {
    RunTool::get_info(argv[1], h);
    cout << "sysname     '" << h.sysname << "'" << endl
	 << "release     '" << h.release << "'" << endl
	 << "version     '" << h.version << "'" << endl
	 << "machine     '" << h.machine << "'" << endl
	 << "cpus        " << h.cpus << endl
	 << "cpuMHz      " << h.cpuMHz << endl
	 << "memKB       " << h.memKB << endl
	 << "max_tools   " << h.max_tools << endl
	 << "cur_tools   " << h.cur_tools << endl
	 << "load        " << h.load << endl
	 << "cur_pending " << h.cur_pending << endl
	 << "max_pending " << h.max_pending << endl;
  } catch (SRPC::failure f) {
    cout << "SRPC failure: "; cout << f.msg; cout << "\n";
    return 1;
  }
  return 0;
}
