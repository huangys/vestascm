// Copyright (C) 2001, Compaq Computer Corporation

// This file is part of Vesta.

// Vesta is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of
// the License, or (at your option) any later version.

// Vesta is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA

#include <ctype.h>
#include "Units.H"

// Base 10 international system of units prefixes.  See:

// http://en.wikipedia.org/wiki/SI_prefix

#define DEC_ONE_K CONST_UINT_64(1000)
#define DEC_ONE_M CONST_UINT_64(1000000)
#define DEC_ONE_G CONST_UINT_64(1000000000)
#define DEC_ONE_T CONST_UINT_64(1000000000000)
#define DEC_ONE_P CONST_UINT_64(1000000000000000)
#define DEC_ONE_E CONST_UINT_64(1000000000000000000)

// Binary brefixes for different magnitudes.  See:

// http://en.wikipedia.org/wiki/Binary_prefix

#define BIN_ONE_K (CONST_UINT_64(1)<<10)
#define BIN_ONE_M (CONST_UINT_64(1)<<20)
#define BIN_ONE_G (CONST_UINT_64(1)<<30)
#define BIN_ONE_T (CONST_UINT_64(1)<<40)
#define BIN_ONE_P (CONST_UINT_64(1)<<50)
#define BIN_ONE_E (CONST_UINT_64(1)<<60)

Text Basics::FormatUnitVal(Basics::uint64 val, bool decimal) throw ()
{
  const Basics::uint64 OneK = decimal ? DEC_ONE_K : BIN_ONE_K;
  const Basics::uint64 OneM = decimal ? DEC_ONE_M : BIN_ONE_M;
  const Basics::uint64 OneG = decimal ? DEC_ONE_G : BIN_ONE_G;
  const Basics::uint64 OneT = decimal ? DEC_ONE_T : BIN_ONE_T;
  const Basics::uint64 OneP = decimal ? DEC_ONE_P : BIN_ONE_P;
  const Basics::uint64 OneE = decimal ? DEC_ONE_E : BIN_ONE_E;

    char buff[20];
    if (val < OneK) {
	sprintf(buff, "%"FORMAT_LENGTH_INT_64"u", val);
    } else {
      // Lower case k for decimal, as K means Kelin in SI.
      char suffix = decimal ? 'k' : 'K';
      if (val >= OneE)
	{
	  val /= OneP;
	  suffix = 'E';
	}
      else if (val >= OneP)
	{
	  val /= OneT;
	  suffix = 'P';
	}
      else if (val >= OneT)
	{
	  val /= OneG;
	  suffix = 'T';
	}
      else if (val >= OneG)
	{
	  val /= OneM;
	  suffix = 'G';
	}
      else if (val >= OneM)
	{
	  val /= OneK;
	  suffix = 'M';
	}

      float ratio = ((float)val) / ((float)OneK);
      if (ratio < 9.95) { // assures won't round up to 10.0
	sprintf(buff, "%.1f%c", ratio, suffix);
      } else {
	val = (val + (OneK / 2)) / OneK; // round
	sprintf(buff, "%"FORMAT_LENGTH_INT_64"u%c", val, suffix);
      }
    }
    return Text(buff);
}

Basics::uint64 Basics::ParseUnitVal(const char *val, bool decimal)
  throw (Basics::ParseUnitValFailure)
{
  const Basics::uint64 OneK = decimal ? DEC_ONE_K : BIN_ONE_K;
  const Basics::uint64 OneM = decimal ? DEC_ONE_M : BIN_ONE_M;
  const Basics::uint64 OneG = decimal ? DEC_ONE_G : BIN_ONE_G;
  const Basics::uint64 OneT = decimal ? DEC_ONE_T : BIN_ONE_T;
  const Basics::uint64 OneP = decimal ? DEC_ONE_P : BIN_ONE_P;
  const Basics::uint64 OneE = decimal ? DEC_ONE_E : BIN_ONE_E;

  // If the value doesn't start with a digit, that's an error.
  if(!isdigit(*val))
    {
      ParseUnitValFailure err;
      err.val = val;
      err.emsg = (Text("Value '") + val + "' doesn't start with a number");
      throw err;
    }

  char *end;

  unsigned long long result = strtoul(val, &end, 0);
  switch(*end)
    {
    case 'k':
    case 'K':
      result *= OneK; end++;
      break;
    case 'm':
    case 'M':
      result *= OneM; end++;
      break;
    case 'g':
    case 'G':
      result *= OneG; end++;
      break;
    case 't':
    case 'T':
      result *= OneT; end++;
      break;
    case 'p':
    case 'P':
      result *= OneP; end++;
      break;
    case 'e':
    case 'E':
      result *= OneE; end++;
      break;
    case 0:
      break;
    default:
      // If there's some other character after the number, that's an
      // error.
      {
	ParseUnitValFailure err;
	err.val = val;
	err.emsg = ("Unknown unit multiplier '" + Text(*end) +
		    "' in value '" + val + "'");
	throw err;
      }
    }
  
  // If there's something after the unit multiplier, that's an error.
  if(*end != 0)
    {
      ParseUnitValFailure err;
      err.val = val;
      err.emsg = ("Extra characters '" + Text(end) +
		  "' after unit multiplier in value '" + val + "'");
      throw err;
    }
  return result;
}
