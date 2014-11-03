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

// Last modified on Tue Aug  3 16:36:21 EDT 2004 by ken@xorian.net
//      modified on Mon Jul 15 09:26:27 PDT 1996 by heydon

#ifndef _LOOKUP_STATS_H
#define _LOOKUP_STATS_H

#include <Basics.H>
#include <SRPC.H>
#include "Timer.H"

class LookupStats {
  public:
    // lookup data for a single lookup outcome
    class Datum {
      public:
	// data fields
	Basics::uint64 num;                 // number of this operation
	Timer::MicroSecs totalTime;        // total time for this operation
	Timer::MicroSecs minTime, maxTime; // minimum/maximum times seen

	// constructor
	Datum() throw ()
          : num(0), totalTime(0UL), minTime(0UL), maxTime(0UL)
          { /* SKIP */ }

	// update
	void Update(Timer::MicroSecs tm) throw ();
	/* Update this lookup datum to record a new event taking "tm"
	   microseconds. */

	void Update(const Datum &d) throw ();
        /* Aggregate the lookup data in "d" into this datum. */

	// send/receive
	void Send(SRPC &srpc) const throw (SRPC::failure);
	void Recv(SRPC &srpc) throw (SRPC::failure);

	// print
	void Print(std::ostream &os, const char *label, int labelWidth,
          Timer::MicroSecs total, int indent = 2) const throw ();
    };

    // the kinds of lookup outcomes
    enum Kind {NewHits, WarmHits, DiskHits, AllMisses, NumKinds, NoOutcome};

    // a record of various kinds of lookup outcomes
    class Rec {
      public:
	// data fields
	Datum datum[NumKinds];

	// send/receive
	void Send(SRPC &srpc) const throw (SRPC::failure);
	void Recv(SRPC &srpc) throw (SRPC::failure);

	// print
	friend std::ostream& operator << (std::ostream &os, const LookupStats::Rec &r)
          throw () { r.Print(os); return os; }
	void Print(std::ostream &os, int indent = 2) const throw ();
    };
};

#endif // _LOOKUP_STATS_H

