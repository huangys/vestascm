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

// perfDebugControl.C - definition of client call for controlling
// performance debugging features.

// Last modified on Mon Dec 13 12:42:23 EST 2004 by ken@xorian.net

#include "perfDebugControl.H"
#include "VestaSourceSRPC.H"

Basics::uint64 PerfDebug::Set(Basics::uint64 setting,
			      Text reposHost,
			      Text reposPort,
			      AccessControl::Identity who)
  throw (SRPC::failure)
{
  SRPC* srpc;
  MultiSRPC::ConnId id = VestaSourceSRPC::Start(srpc, reposHost, reposPort);

  srpc->start_call(VestaSourceSRPC::SetPerfDebug,
		   VestaSourceSRPC::version);
  VestaSourceSRPC::send_identity(srpc, who);
  srpc->send_int64(setting);
  srpc->send_end();

  Basics::uint64 result = srpc->recv_int64();
  srpc->recv_end();
  VestaSourceSRPC::End(id);

  return result;
}
