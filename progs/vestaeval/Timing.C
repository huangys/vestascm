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
// File: Timing.C

#include "Timing.H"
#include <Basics.H>
#include <RegExp.H>
#include <Sequence.H>
#include <Generics.H>
#include <VestaConfig.H>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <FS.H>

using std::ostream;
using std::ofstream;
using std::ios;
using OS::cio;

// Timing level names for comand-line switch
static Text timing_level_names[TimingRecorder::LAST] =
	{"none", "tool", "func", "detail"};

// Selected level of timing recording
TimingRecorder::PrintTimingLevels TimingRecorder::level =
	TimingRecorder::PrintNoTimings;

// depth of arguments to print in the report
int TimingRecorder::report_argument_depth = 2;

// stream to get the very spammy output
static OS::ThreadIO *timing_out = 0;

// all the regexps to match this function against
static Sequence<Basics::RegExp *> *timing_re_seq = 0;

Text TimingRecorder::printLevels(void) {
	Text ret;
	for(int i = PrintNoTimings; i < LAST - 1; i++) {
		ret += timing_level_names[i];
		ret += ", ";
	}
	ret += timing_level_names[LAST - 1];
	return ret;
}

TimingRecorder::PrintTimingLevels TimingRecorder::parseTimingLevel(Text name)
	throw(TimingRecorder::failure){
	for(PrintTimingLevels i = PrintNoTimings; i < LAST; i = PrintTimingLevels(i+1)) {
		if(name == timing_level_names[i]) {
			return i;
		}
	}
	TimingRecorder::failure f;
	f.msg = "unrecognized timing level " + name + "\n";
	f.msg += "expecting one of " + printLevels();
	throw f;
}

ostream& TimingRecorder::start_out(void) {
	if(timing_out != 0) {
		return timing_out->start_out();
	} else {
		return cio().start_out();
	}
}

void TimingRecorder::end_out(bool aborting) {
	if(timing_out != 0) {
		timing_out->end_out(aborting);
	} else {
		cio().end_out(aborting);
	}
}

void TimingRecorder::set_out(Text filename) throw(failure) {
	if(timing_out != 0) {
		TimingRecorder::failure f;
		f.msg = "Can't call TimingRecorder::set_out() after it's already been set";
		throw f;
	}
	ofstream *out = NEW(ofstream);
	out->open(filename.cchars(), ios::out);
	if (out->fail()) {
		TimingRecorder::failure f;
		f.msg = "unable to open timing data file: "
			+ Basics::errno_Text(errno);
		throw f;
	}
	timing_out = NEW_CONSTR(OS::ThreadIO, (out, NULL, NULL));
}

bool TimingRecorder::name_match(Text name) {
	if(timing_re_seq == 0)
		// empty list means match everything
		return true;
	for(int i = 0; i < timing_re_seq->size(); i++) {
		Basics::RegExp *re = timing_re_seq->get(i);
		if(re->match(name))
			return true;
	}
	return false;
}

void TimingRecorder::init_regexp(const Text &re_text)
throw(TimingRecorder::failure) {
	if(timing_re_seq == 0) {
		timing_re_seq = NEW(Sequence<Basics::RegExp *>);
	}
	Basics::RegExp *re;
	try {
		re = NEW_CONSTR(Basics::RegExp,
				(re_text, (REG_EXTENDED | REG_NOSUB)));
	} catch(Basics::RegExp::ParseError e) {
		TimingRecorder::failure f;
		f.msg = "Unable to compile regexp: \"" + re_text +
			"\": " + e.msg;
		throw f;
	}
	timing_re_seq->addhi(re);
}

Text getline(std::ifstream &ifs) {
	Text line("");
	char buff[100];
	while(!ifs.eof()) {
		(void) ifs.getline(buff, sizeof(buff));
		line = line + buff;
		if(!ifs.fail() || ifs.eof()) break;
		ifs.clear();
	}
	return line;
}

void TimingRecorder::parse_re_file(const Text &fname)
throw(TimingRecorder::failure) {
	try {
		std::ifstream ifs;
		FS::OpenReadOnly(fname, /*OUT*/ifs);
		while(!ifs.eof()) {
			Text line = getline(ifs);
			if(line == "") continue;
			TimingRecorder::init_regexp(line);
		}
		FS::Close(ifs);
	} catch (FS::DoesNotExist e) {
		TimingRecorder::failure f;
		f.msg = fname + ": file not found.";
		throw f;
	} catch (FS::Failure e) {
		TimingRecorder::failure f;
		f.msg = fname + ": " + Basics::errno_Text(e.get_errno());
		throw f;
	}
}
