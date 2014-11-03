// Copyright (C) 2007, Scott Venier
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
// File: Timing.H

#ifndef Timing_H
#define Timing_H

#include <Basics.H>
#include <ThreadIO.H>
#include <sys/types.h>
#include <RegExp.H>
#include <Sequence.H>
#include <VestaConfig.H>
#include <Timers.H>

namespace TimingRecorder {
	// exception to throw
	struct failure {
		Text msg;
	};

	// list of supported levels
	enum PrintTimingLevels {
		PrintNoTimings = 0,
		PrintToolTimings,
		PrintFuncTimings,
		PrintDetailTimings,
		LAST
	};
	// print the list
	Text printLevels(void);
	// turn the text names into a level
	PrintTimingLevels parseTimingLevel(Text name) throw(failure);
	// the current level
	extern PrintTimingLevels level;
	// the depth of argument values to print in the timing report
	extern int report_argument_depth;
	// initialze a regexp for name matching
	void init_regexp(const Text &re_text) throw(failure);
	// parse are file of regular expressions
	void parse_re_file(const Text &fname) throw(failure);
	// does this name match one of the regexps?
	bool name_match(Text name);

	// call these to do the necessary locking for output
	std::ostream& start_out(void);
	void end_out(bool aborting = false);

	// make all future output go to this new stream
	void set_out(Text filename) throw(failure);
};

#endif // Timing_H
