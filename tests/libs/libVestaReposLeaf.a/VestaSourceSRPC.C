// Copyright (C) 2004, Kenneth C. Schalk
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

// VestaSourceSRPC.C - shared code used by repository client APIs

// Last modified on Fri Apr 22 13:23:37 EDT 2005 by ken@xorian.net

#include "VestaSourceSRPC.H"
#include "VDirSurrogate.H"

static MultiSRPC* g_vssmulti;

// default repository
static Text g_default_host;
static Text g_default_intf;

using std::cerr;
using std::endl;

extern "C"
{
  void 
  VestaSourceSRPC_init_inner() throw()
  {
    try {
      g_default_intf =
	VestaConfig::get_Text("Repository", "VestaSourceSRPC_port");
      // Sanity check on the port
      if(g_default_intf.Empty())
	{
	  cerr << "Fatal: [Repository]VestaSourceSRPC_port is empty"
	       << endl;
	  abort();
	}
      g_default_host =
	VestaConfig::get_Text("Repository", "VestaSourceSRPC_host");
      // Sanity check on the host.
      if(g_default_host.Empty())
	{
	  cerr << "Fatal: [Repository]VestaSourceSRPC_host is empty"
	       << endl;
	  abort();
	}
      if(g_default_host.FindChar(':') != -1)
	{
	  cerr << "Fatal: [Repository]VestaSourceSRPC_host contains a colon"
	       << endl;
	  abort();
	}
      g_vssmulti = NEW(MultiSRPC);
      AccessControl::commonInit();
    } catch (VestaConfig::failure f) {
      cerr << "VestaConfig::failure " << f.msg << endl;
      // This is a fatal error
      abort();
    }
  }
}

static void VestaSourceSRPC_init() throw()
{
  static pthread_once_t vssonce = PTHREAD_ONCE_INIT;
  pthread_once(&vssonce, VestaSourceSRPC_init_inner);
}

Text VestaSourceSRPC::defaultHost() throw()
{
  VestaSourceSRPC_init();
  return g_default_host;
}
Text VestaSourceSRPC::defaultInterface() throw()
{
  VestaSourceSRPC_init();
  return g_default_intf;
}

MultiSRPC::ConnId VestaSourceSRPC::Start(SRPC *&srpc) throw (SRPC::failure)
{
  VestaSourceSRPC_init();
  return g_vssmulti->Start(VestaSourceSRPC::defaultHost(),
			   VestaSourceSRPC::defaultInterface(),
			   srpc);
}

MultiSRPC::ConnId VestaSourceSRPC::Start(SRPC *&srpc,
			const Text &host_interface)
  throw (SRPC::failure)
{
  VestaSourceSRPC_init();
  Text host, intf;
  int colon = host_interface.FindChar(':');
  if(colon == -1)
    {
      host = host_interface;
      intf = VestaSourceSRPC::defaultInterface();
    }
  else
    {
      host = host_interface.Sub(0, colon);
      intf = host_interface.Sub(colon+1);
    }
  if(host.Empty())
    host = VestaSourceSRPC::defaultHost();
  if(intf.Empty())
    intf = VestaSourceSRPC::defaultInterface();

  return g_vssmulti->Start(host, intf, srpc);
}

MultiSRPC::ConnId VestaSourceSRPC::Start(SRPC *&srpc,
					 Text hostname, Text interface)
  throw (SRPC::failure)
{
  VestaSourceSRPC_init();
  if(hostname.Empty())
    hostname = VestaSourceSRPC::defaultHost();
  if(interface.Empty())
    interface = VestaSourceSRPC::defaultInterface();

  return g_vssmulti->Start(hostname, interface, srpc);
}
  
void VestaSourceSRPC::End(MultiSRPC::ConnId id) throw()
{
  VestaSourceSRPC_init();
  g_vssmulti->End(id);
}

// Common code for sending an AccessControl::Identity
void VestaSourceSRPC::send_identity(SRPC* srpc, AccessControl::Identity who)
  throw(SRPC::failure)
{
  VestaSourceSRPC_init();
  if (who == NULL) {
    who = AccessControl::self();
  }
  who->send(srpc);
}
