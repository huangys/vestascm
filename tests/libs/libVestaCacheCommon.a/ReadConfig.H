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

// Last modified on Thu Nov  8 12:36:38 EST 2001 by ken@xorian.net
//      modified on Fri Aug  8 14:41:51 PDT 1997 by heydon

// Wrapper functions for VestaConfig.H

#ifndef _READ_CONFIG_H
#define _READ_CONFIG_H

#include <Basics.H>

/* The routines in this interface are wrapper functions for the "get" method
   of a "VestaConfig" object. If there is an error accessing the configuration
   file, or if the requested section/name does not exist in the configuration
   file, they report a fatal error and cause the process to exit. */

class ReadConfig {
  public:
    static Text Location() throw ();
    /* Return the result of "VestaConfig::get_location()". Report a
       fatal error and exit if that function throws an exception. */

    static Text TextVal(const Text& sect, const Text& nm) throw ();
    /* Return the value in the Vesta configuration file associated with
       the name "nm" in the section "sect". */

    static int IntVal(const Text& sect, const Text& nm) throw ();
    /* Read the value in the Vesta configuration file associated with the name
       "nm" in the section "sect". If it is a string representation of an
       integer, return its value; otherwise, report a fatal error and
       exit. */

    static bool OptIntVal(const Text& sect, const Text& nm, int& val) throw ();
    /* Read the value in the Vesta configuration file associated with the
       name "nm" in the section "sect". If no such entry exists, return
       "false". Otherwise, return "true" and set "val" to the entry's value
       converted to an integer. If the entry's value cannot be converted to an
       integer, report a fatal error and exit. */

    static bool OptBoolVal(const Text& sect, const Text& nm, bool& val)
      throw ();
    /* Like "OptIntVal", only read a boolean value. */
};

#endif // _READ_CONFIG_H
