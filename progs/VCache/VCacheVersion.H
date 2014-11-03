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

// Created on Wed Jul 23 12:29:31 PDT 1997 by heydon

// VCacheVersion.H -- defines the version identifier of the Vesta cache

#ifndef _VCACHE_VERSION_H
#define _VCACHE_VERSION_H

#include <Basics.H>

// Note: this string gets generated in a separate source file by
// progs.ves in the same directory.
extern const char *Version;

const Text VCacheVersion(Version);

#endif // _VCACHE_VERSION_H
