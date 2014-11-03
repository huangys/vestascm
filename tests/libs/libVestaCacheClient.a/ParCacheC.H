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

// Last modified on Thu Nov  8 12:40:11 EST 2001 by ken@xorian.net
//      modified on Tue Jan 21 13:43:15 PST 1997 by heydon

// ParCacheC -- the client module for interfacing with a partitioned
//              Vesta-2 cache server

#ifndef _PAR_CACHE_C_H
#define _PAR_CACHE_C_H

#include <Basics.H>

/* For now, we only support connections to a single server. The "ParCache"
   spec describes how to distribute PK's across multiple servers. This
   interface is just a thin veneer for such a facility. The interface will
   likely change quite substantially if partitioned cache servers are ever
   implemented. */

class ParCacheC {
  public:
    static Text *Locate() throw ();
    /* Return the name of the host on which the server is running. */

    static void SetServerHost(char *name = NULL) throw ();
    /* This method sets the name returned by subsequent calls of "Locate".
       By default, the server host name is NULL, which means that the server
       host is the value associated with the "[CacheServer]/Host" variable
       in the Vesta configuration file. */
};

#endif // _PAR_CACHE_C_H
