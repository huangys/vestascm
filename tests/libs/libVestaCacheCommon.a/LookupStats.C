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

// Last modified on Tue Aug  3 17:56:48 EDT 2004 by ken@xorian.net  
//      modified on Sat Feb 12 11:52:55 PST 2000 by mann  
//      modified on Mon Nov 10 12:41:27 PST 1997 by heydon

#include <Basics.H>
#include <SRPC.H>
#include "Timer.H"
#include "LookupStats.H"

#include <iomanip>

using std::ostream;
using std::ios;
using std::setw;
using std::setprecision;
using std::resetiosflags;
using std::setiosflags;

inline void Indent(ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << " ";
}

// LookupStats::Datum ---------------------------------------------------------

void LookupStats::Datum::Update(Timer::MicroSecs tm) throw ()
{
    if (this->num == 0) {
	this->minTime = this->maxTime = tm;
    } else {
	this->minTime = min(this->minTime, tm);
	this->maxTime = max(this->maxTime, tm);
    }
    this->num++;
    this->totalTime += tm;
}

void LookupStats::Datum::Update(const LookupStats::Datum &d) throw ()
{
    if (d.num > 0) {
	if (this->num == 0) {
	    this->minTime = d.minTime;
	    this->maxTime = d.maxTime;
	} else {
	    this->minTime = min(this->minTime, d.minTime);
	    this->maxTime = max(this->maxTime, d.maxTime);
	}
	this->num += d.num;
	this->totalTime += d.totalTime;
    } else {
	assert(d.totalTime == 0UL);
    }
}

void LookupStats::Datum::Send(SRPC &srpc) const throw (SRPC::failure)
{
  srpc.send_int64(this->num);
  srpc.send_int64(this->totalTime);
  srpc.send_int64(this->minTime);
  srpc.send_int64(this->maxTime);
}

void LookupStats::Datum::Recv(SRPC &srpc) throw (SRPC::failure)
{
  this->num = srpc.recv_int64();
  this->totalTime = srpc.recv_int64();
  this->minTime = srpc.recv_int64();
  this->maxTime = srpc.recv_int64();
}

void LookupStats::Datum::Print(ostream &os, const char *label,
  int labelWidth, Timer::MicroSecs total, int indent) const throw ()
{
    Indent(os, indent);
    os << resetiosflags(ios::right) << setiosflags(ios::left)
      << setw(labelWidth) << label;
    os << setw(0) << " = ";
    os << resetiosflags(ios::left) << setiosflags(ios::right)
      << setw(6) << this->num;
    if (total != 0UL) {
	float numF = (float)(this->num);
        os << setw(0) << " (";
	os << setprecision(0) << setw(3)
	  << setiosflags(ios::fixed) << resetiosflags(ios::showpoint)
	  << (100.0 * numF / (float)total);
        os << setw(0) << "%)";
	if (numF != 0.0) {
	    os << ", avg = "
              << setprecision(2) << setiosflags(ios::showpoint)
              << setw(5) << (((float)(this->totalTime) / 1000.0) / numF)
              << setw(0) << " ms";
	    os << ", min/max = "
              << setw(5) << ((float)(this->minTime) / 1000.0) << '/'
              << setw(5) << ((float)(this->maxTime) / 1000.0)
              << setw(0) << " ms";
	}
    }
    os << '\n';
}

// LookupStats::Rec -----------------------------------------------------------

void LookupStats::Rec::Send(SRPC &srpc) const throw (SRPC::failure)
{
    for (int i = 0; i < LookupStats::NumKinds; i++) {
	this->datum[i].Send(srpc);
    }
}

void LookupStats::Rec::Recv(SRPC &srpc) throw (SRPC::failure)
{
    for (int i = 0; i < LookupStats::NumKinds; i++) {
	this->datum[i].Recv(srpc);
    }
}

static const char *OutcomeNames[] = {
  "new hits", "warm hits", "disk hits", "all misses" };

void LookupStats::Rec::Print(ostream &os, int indent) const throw ()
{
    // compute max label width
    const char *lookupsLabel = "all lookups";
    int mw = strlen(lookupsLabel);
    int i;
    for (i = 0; i < LookupStats::NumKinds; i++) {
	mw = max(mw, strlen(OutcomeNames[i]));
    }

    // aggregate lookup statistics
    LookupStats::Datum allLookups;
    for (i = 0; i < LookupStats::NumKinds; i++) {
	allLookups.Update(this->datum[i]);
    }

    // print outcome stats
    allLookups.Print(os, "all lookups", mw, allLookups.num, indent);
    for (i = 0; i < LookupStats::NumKinds; i++) {
	this->datum[i].Print(os, OutcomeNames[i], mw, allLookups.num, indent);
    }
}
