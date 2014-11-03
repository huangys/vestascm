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

// Created on Sun Aug 23 11:26:44 PDT 1998 by heydon
// Last modified on Thu Nov  8 12:36:38 EST 2001 by ken@xorian.net
//      modified on Sun Aug 23 11:47:20 PDT 1998 by heydon

#include <string.h>
#include <Basics.H>
#include "CacheArgs.H"

bool CacheArgs::StartsWith(const char *s1, const char *s2, int minMatch)
  throw ()
{
    int s1Len = strlen(s1), s2Len = strlen(s2);
    if (s1Len < minMatch || s1Len > s2Len) return false;
    return (strncmp(s1, s2, s1Len) == 0);
}
