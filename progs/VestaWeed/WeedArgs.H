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

// Created on Thu Mar 13 08:44:27 PST 1997 by heydon

// Last modified on Tue Nov 13 13:09:23 EST 2001 by ken@xorian.net  
//      modified on Tue Jun 27 15:31:20 PDT 2000 by mann  
//      modified on Fri Sep 11 11:19:47 PDT 1998 by heydon

// WeedArgs.H -- defines a structure for representing arguments to VestaWeed

#ifndef _WEED_ARGS_H
#define _WEED_ARGS_H

#include <time.h>
#include <Basics.H>
#include <CacheIntf.H>

class WeedArgs {
  public:
    enum DeletionStatus { NoDeletions, QueryDeletions, DoDeletions };

    char *instrFile;             // file containing weeder instructions;
    CacheIntf::DebugLevel debug; // debugging message level
    DeletionStatus delStatus;    // perform deletions?
    bool globInstrs;  	       	 // print the (expanded) weeder instructions?
    bool printRoots;  	       	 // print the status of each graphLog root?
    time_t keepSecs;             // keep anything younger than "keepSecs" secs
    bool noNew;                  // don't start a new weed, just resume old one
};

#endif // _WEED_ARGS_H
