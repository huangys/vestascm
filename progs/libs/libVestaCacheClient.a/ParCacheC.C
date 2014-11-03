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

// Last modified on Mon May 23 22:09:36 EDT 2005 by ken@xorian.net
//      modified on Tue Jan 21 13:45:00 PST 1997 by heydon

// system includes
#include <unistd.h>
#include <sys/param.h>

// basics
#include <Basics.H>

// cache-common
#include <CacheConfig.H>

// local includes
#include "ParCacheC.H"

Basics::mutex mu; // protects serverHost
static Text *serverHost = (Text *)NULL;

Text *ParCacheC::Locate() throw ()
{
    Text *res;
    mu.lock();
    res = (serverHost == (Text *)NULL)
      ? NEW_CONSTR(Text, (Config_Host))
      : serverHost;
    mu.unlock();
    return res;
}

void ParCacheC::SetServerHost(char *name) throw ()
{
    mu.lock();
    if (name == (char *)NULL) {
	serverHost = (Text *)NULL;
    } else {
      serverHost = NEW_CONSTR(Text, (name));
    }
    mu.unlock();
}
