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


#if defined (__linux__)
#include <unistd.h>
#endif 
#include <stdio.h>
#include <sys/time.h>

#include <climits>
#include <Basics.H>
#include "Debug.H"

// fatal programmer error
class FatalError {
  public: FatalError() { /*EMPTY*/ }
};

// global variables
const int Debug_TimeStampLen = 28; // "hh:mm:ss.mmm MM/DD/YYYY -- "

Text Debug::Timestamp() throw ()
{
    struct timeval tv;
    struct timezone tz;
    struct tm *tm;
    if (gettimeofday(&tv, &tz) < 0) {
	throw FatalError();
    }
    time_t secs = tv.tv_sec;
    if((tm = localtime(&secs)) == NULL) {
	throw FatalError();
    }
    char Debug_TimeStampBuff[Debug_TimeStampLen];
    int count =
      sprintf(Debug_TimeStampBuff, "%02d:%02d:%02d.%03d %02d/%02d/%04d -- ",
	      tm->tm_hour, tm->tm_min, tm->tm_sec, tv.tv_usec/1000,
	      tm->tm_mon + 1, tm->tm_mday, tm->tm_year + 1900);
    assert(count < Debug_TimeStampLen);
    return Text(Debug_TimeStampBuff);
}

int Debug::MyRand(int lower, int upper) throw ()
{
    int res;
    res = lower + (rand() % (upper-lower+1));
    return res;
}

void Debug::BlockForever() throw ()
{
    Basics::mutex mu0;
    Basics::cond cond0;
    mu0.lock();
    cond0.wait(mu0);
}
