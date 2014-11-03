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

// Last modified on Thu Nov  8 12:36:38 EST 2001 by ken@xorian.net
//      modified on Wed Sep  2 12:47:56 PDT 1998 by heydon

#include "CacheIntf.H"

// Names of debugging levels
static char *CacheIntf_DebugNamesArray[] = {
  "None", "StatusMsgs", "LeaseExp", "LogRecover", "LogFlush",
  "MPKFileFlush", "LogFlushEntries", "WeederOps", "AddEntryOp",
  "OtherOps", "WeederScans", "All"
};

char *CacheIntf::DebugName(int i) throw ()
{
    if (i < 0 || i > CacheIntf::All) assert(false);
    return (CacheIntf_DebugNamesArray[i]);
}

const char *const CacheIntf::LookupResName(CacheIntf::LookupRes res)
  throw ()
{
    static const char *const LookupResNameArray[] =
	{ "Hit", "Miss", "FV Mismatch", "Bad Lookup Args" };
    return LookupResNameArray[res];
}

const char *const CacheIntf::AddEntryResName(CacheIntf::AddEntryRes res)
  throw ()
{
    static const char *const AddEntryResNameArray[] =
	{ "Entry Added", "No Lease", "Bad AddEntry Args" };
    return AddEntryResNameArray[res];
}
