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

// Last modified on Tue Aug  3 17:01:04 EDT 2004 by ken@xorian.net  
//      modified on Sat Feb 12 12:02:04 PST 2000 by mann  
//      modified on Fri Aug  8 14:46:02 PDT 1997 by heydon

#include <stdio.h>
#include <Basics.H>
#include <VestaConfig.H>
#include "ReadConfig.H"

using std::cerr;
using std::endl;

void DieWithError(const Text &msg) throw () {
  cerr << "Fatal error: " << msg << endl;
  exit(1);
}

Text ReadConfig::Location() throw ()
{
    try {
	return VestaConfig::get_location();
    } catch (VestaConfig::failure f) {
	DieWithError("failed to locate Vesta-2 configuration file");
    }
    return ""; // not reached
}

Text ReadConfig::TextVal(const Text& sect, const Text& nm) throw ()
{
    Text res;
    try {
	res = VestaConfig::get_Text(sect, nm);
    } catch (VestaConfig::failure f) {
	DieWithError(f.msg);
    }
    return res;
}

int ReadConfig::IntVal(const Text& sect, const Text& nm) throw ()
{
    int res;
    try {
	res = VestaConfig::get_int(sect, nm);
    } catch (VestaConfig::failure f) {
	DieWithError(f.msg);
    }
    return res;
}

bool ReadConfig::OptIntVal(const Text& sect, const Text& nm, int& val) throw ()
{
    bool res;
    try {
	Text dummy;
	if ((res = VestaConfig::get(sect, nm, dummy))) {
	    val = VestaConfig::get_int(sect, nm);
	}
    } catch (VestaConfig::failure f) {
	DieWithError(f.msg);
    }
    return res;
}

bool ReadConfig::OptBoolVal(const Text& sect, const Text& nm, bool& val)
  throw ()
{
    bool res;
    try {
	Text dummy;
	if ((res = VestaConfig::get(sect, nm, dummy))) {
	    int int_val = VestaConfig::get_int(sect, nm);
	    val = (int_val != 0) ? true : false;
	}
    } catch (VestaConfig::failure f) {
	DieWithError(f.msg);
    }
    return res;
}
