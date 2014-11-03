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

// Last modified on Fri Nov  9 14:11:52 EST 2001 by ken@xorian.net
//      modified on Fri Dec 12 09:50:09 PST 1997 by heydon

// Poly.H -- polynomials for use by the fingerprint package

#ifndef _POLY_H
#define _POLY_H

#include <Basics.H>

class PolyVal {
  public:
    enum { ByteCnt = 16 };		       // tag bytes
    enum { WordCnt = ByteCnt / sizeof(Word) }; // tag words
};

typedef struct {
    Word w[PolyVal::WordCnt];
} Poly, RawFP;

/* The irreducible polynomial used as a modulus is 
   1 + x^3 + x^5 + x^6 + x^7 + x^8 + x^9 + x^10 + x^17 + x^19 + x^21 +
   x^22 + x^23 + x^25 + x^26 + x^27 + x^30 + x^31 + x^32 + x^33 + x^35 +
   x^37 + x^38 + x^40 + x^41 + x^42 + x^43 + x^46 + x^47 + x^48 + x^50 +
   x^51 + x^52 + x^55 + x^56 + x^58 + x^59 + x^60 + x^61 + x^66 + x^68 +
   x^70 + x^71 + x^73 + x^75 + x^76 + x^79 + x^85 + x^86 + x^87 + x^91 +
   x^92 + x^95 + x^96 + x^99 + x^102 + x^103 + x^105 + x^106 + x^107 +
   x^108 + x^110 + x^114 + x^117 + x^119 + x^120 + x^121 + x^125 + x^126
   + x^127 + x^128

   POLY_IRRED is its representation.
*/

const Word
  POLY_X63_W = 0x1,
  POLY_ONE_W = CONST_INT_64(0x8000000000000000);

const Poly
  POLY_ZERO = {0L, 0L},
  POLY_ONE = {0L, POLY_ONE_W}, 
  POLY_IRRED = {CONST_INT_64(0x2b590719937a25c7),
		CONST_INT_64(0x97e05773d6f3b9bc)}; 

inline void PolyInc(/*INOUT*/ Poly& p, const Poly& q) throw ()
// Equivalent to "p = p + q"
{
    p.w[0] ^= q.w[0];
    p.w[1] ^= q.w[1];
}

#endif // _POLY_H
