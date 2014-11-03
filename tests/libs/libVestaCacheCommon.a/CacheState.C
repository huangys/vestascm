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

// Created on Sat May 31 15:01:29 PDT 1997 by heydon
// Last modified on Sun Jun  5 18:43:25 EDT 2005 by ken@xorian.net
//      modified on Sun Aug 22 15:17:49 PDT 1999 by heydon

#include <Basics.H>

#include <time.h>
// necessary to make up for broken header file
extern "C" char* _Pctime_r(const time_t *timer, char *buffer);

#include <SRPC.H>
#include "CacheState.H"

using std::ostream;
using std::endl;

inline void Indent(ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << " ";
}

void CacheId::Print(ostream &os, int k) const throw ()
{
    Indent(os, k); os << "Hostname:      " << this->host << endl;
    Indent(os, k); os << "Port number:   " << this->port << endl;
    Indent(os, k); os << "Stable cache:  " << this->stableDir << endl;
    Indent(os, k); os << "Cache version: " << this->cacheVersion << endl;
    Indent(os, k); os << "Intf version:  " << this->intfVersion << endl;
    char buffer[64];
    Indent(os, k); os << "Up since:      ";
    os << ctime_r(&(this->startTime), buffer);
    Indent(os, k); os << "Current time:  ";
    time_t now = time((time_t *)NULL);
    assert(now >= 0);
    os << ctime_r(&now, buffer);
}

void CacheId::Send(SRPC &srpc) const throw (SRPC::failure)
{
    srpc.send_Text(this->host);
    srpc.send_Text(this->port);
    srpc.send_Text(this->stableDir);
    srpc.send_Text(this->cacheVersion);
    srpc.send_int(this->intfVersion);
    srpc.send_int((int)(this->startTime));
}

void CacheId::Recv(SRPC &srpc) throw (SRPC::failure)
{
    srpc.recv_Text(/*OUT*/ this->host);
    srpc.recv_Text(/*OUT*/ this->port);
    srpc.recv_Text(/*OUT*/ this->stableDir);
    srpc.recv_Text(/*OUT*/ this->cacheVersion);
    this->intfVersion = srpc.recv_int();
    this->startTime = (time_t)(srpc.recv_int());
}

bool operator==(const MethodCnts &c1, const MethodCnts &c2) throw ()
{
    return ((c1.freeVarsCnt == c2.freeVarsCnt)
            && (c1.lookupCnt == c2.lookupCnt)
            && (c1.addEntryCnt == c2.addEntryCnt));
}

EntryState& EntryState::operator+=(const EntryState &s) throw ()
{
    this->newEntryCnt += s.newEntryCnt;
    this->oldEntryCnt += s.oldEntryCnt;
    this->newPklSize += s.newPklSize;
    this->oldPklSize += s.oldPklSize;
    return *this;
}

void CacheState::Print(ostream &os, int k) const throw ()
{
    Indent(os, k); os <<"Image size:           "<< this->virtualSize << endl;
    Indent(os, k); os <<"Resident size:        "<< this->physicalSize<< endl;
    Indent(os, k); os <<"FreeVars calls:       "<< this->cnt.freeVarsCnt<<endl;
    Indent(os, k); os <<"Lookup calls:         "<< this->cnt.lookupCnt << endl;
    Indent(os, k); os <<"AddEntry calls:       "<< this->cnt.addEntryCnt<<endl;
    Indent(os, k); os <<"Num VMultiPKFiles:    "<< this->vmpkCnt << endl;
    Indent(os, k); os <<"Num VPKFiles:         "<< this->vpkCnt << endl;
    Indent(os, k); os <<"Num total entries:    "<< this->entryCnt << endl;
    Indent(os, k); os <<"Num new entries:      "<< this->s.newEntryCnt << endl;
    Indent(os, k); os <<"Num old entries:      "<< this->s.oldEntryCnt << endl;
    Indent(os, k); os <<"New pickle size:      "<< this->s.newPklSize << endl;
    Indent(os, k); os <<"Old pickle size:      "<< this->s.oldPklSize << endl;
    Indent(os, k); os <<"HitFilter size:       "<< this->hitFilterCnt << endl;
    Indent(os, k); os <<"Num entries to del:   "<< this->delEntryCnt << endl;
    Indent(os, k); os <<"Num MPKFiles to weed: "<< this->delEntryCnt << endl;
}

void CacheState::Send(SRPC &srpc) const throw (SRPC::failure)
{
    srpc.send_int(this->virtualSize);
    srpc.send_int(this->physicalSize);
    srpc.send_int(this->cnt.freeVarsCnt);
    srpc.send_int(this->cnt.lookupCnt);
    srpc.send_int(this->cnt.addEntryCnt);
    srpc.send_int(this->vmpkCnt);
    srpc.send_int(this->vpkCnt);
    srpc.send_int(this->entryCnt);
    srpc.send_int(this->s.newEntryCnt);
    srpc.send_int(this->s.oldEntryCnt);
    srpc.send_int(this->s.newPklSize);
    srpc.send_int(this->s.oldPklSize);
    srpc.send_int(this->hitFilterCnt);
    srpc.send_int(this->delEntryCnt);
    srpc.send_int(this->mpkWeedCnt);
}

void CacheState::Recv(SRPC &srpc) throw (SRPC::failure)
{
    this->virtualSize = srpc.recv_int();
    this->physicalSize = srpc.recv_int();
    this->cnt.freeVarsCnt = srpc.recv_int();
    this->cnt.lookupCnt = srpc.recv_int();
    this->cnt.addEntryCnt = srpc.recv_int();
    this->vmpkCnt = srpc.recv_int();
    this->vpkCnt = srpc.recv_int();
    this->entryCnt = srpc.recv_int();
    this->s.newEntryCnt = srpc.recv_int();
    this->s.oldEntryCnt = srpc.recv_int();
    this->s.newPklSize = srpc.recv_int();
    this->s.oldPklSize = srpc.recv_int();
    this->hitFilterCnt = srpc.recv_int();
    this->delEntryCnt = srpc.recv_int();
    this->mpkWeedCnt = srpc.recv_int();
}
