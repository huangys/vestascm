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

// 
// Mastership.H
// Last modified on Tue Jul 20 17:27:28 EDT 2004 by ken@xorian.net
//      modified on Mon Mar 20 18:49:11 PST 2000 by mann
//
// Implements RequestMastership and grantMastership, etc.
//

#include "UniqueId.H"
#include "FP.H"
#include "VestaSource.H"

// Module initialization
// Call before log is recovered.
void MastershipInit1() throw ();
// Call after log is recovered.
void MastershipInit2() throw ();


VestaSource::errorCode AcquireMastership(const char* pathname,
    const char* srcHost, const char* srcPort, char pathnameSep,
    AccessControl::Identity dwho, AccessControl::Identity swho) throw ();

void QueueRecoverMastership(const char* pathname, char pathnameSep,
				   const char* requestid) throw ();

void MastershipCheckpoint(std::fstream& ckpt) throw ();

extern Text myHostPort, myMasterHint;
