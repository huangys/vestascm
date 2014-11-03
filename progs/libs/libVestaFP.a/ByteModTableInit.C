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

#include "ByteModTable.H"

// Define the storage for ByteModTable
Poly ByteModTable[8][256];

// Initialization of ByteModTable constants------------------------------------

static void TimesX(Poly& p) throw ()
  /* Set "p" to the polynomial "p" times "X" mod "POLY_IRRED". */
{
  short x127flag = (p.w[0] & POLY_X63_W);
  p.w[0] >>= 1;
  if (p.w[1] & POLY_X63_W) p.w[0] |= POLY_ONE_W;
  p.w[1] >>= 1;
  if (x127flag) PolyInc(p, POLY_IRRED);
}

// This is the fucntion which actually computes ByteModTable.
extern "C" void ByteModTableInit_inner()
{
  Poly PowerTable[256], p;

  // TimesX above assumes this, as does FP::Send and FP::Recv, as well
  // as other code in this package.
  assert(PolyVal::WordCnt == 2);

  // Compute "PowerTable"
  int i;
  p = POLY_ONE;
  for(i = 0; i < 256; TimesX(p),i++)
    {
      PowerTable[i] = p;
    }

  // Compute ByteModTable
  int j, k;
  const unsigned char X7 = 1;
  for(i = 0; i < 8; i++)
    {
      for(j = 0; j < 256; j++)
	{
	  p = POLY_ZERO;
	  for(k=0; k < 8; k++)
	    {
	      if(j & (X7 << k))
		{
		  PolyInc(p, PowerTable[191-(i*8)-k]);
		}
	    }

	  ByteModTable[i][j] = p;
	}
    }
}

// This function should be called by other code before accessing
// ByteModTable
void ByteModTableInit() throw()
{
  static pthread_once_t init_once = PTHREAD_ONCE_INIT;
  pthread_once(&init_once, ByteModTableInit_inner);
}

