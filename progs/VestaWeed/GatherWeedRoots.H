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

// Created on Thu Mar 13 09:32:49 PST 1997 by heydon

// Last modified on Tue Nov 13 13:09:23 EST 2001 by ken@xorian.net
//      modified on Wed Apr 14 11:41:11 PDT 1999 by heydon

#ifndef _GATHER_WEED_ROOTS_H
#define _GATHER_WEED_ROOTS_H

#include <Basics.H>
#include <SRPC.H>
#include <FS.H>
#include "CommonErrors.H"
#include "RootTbl.H"

class GatherWeedRoots {
  public:
    static void P(char *file, bool echoModels, /*OUT*/ RootTbl *rootTbl)
      throw (SRPC::failure, FS::Failure, FS::DoesNotExist,
             InputError, SysError, ReposError);
    /* Read the weeder instructions in the file named "file", scanning the
       repository for each model file that matches one of its patterns.
       Add each of the matched roots to "rootTbl". If "echoFiles" is true,
       print a line for each model file matched by a pattern in the
       instruction file. */
};

#endif // _GATHER_WEED_ROOTS_H
