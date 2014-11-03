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

// VPKFileChkPt.H -- records certain aspects of the state of a VPKFile

#ifndef _VPK_FILE_CHK_PT
#define _VPK_FILE_CHK_PT

#include <Basics.H>
#include <FP.H>
#include <Table.H>
#include <PKEpoch.H>
#include <FV.H>

#include "CacheEntry.H"

/* A "VPKFileChkPt" object is used by "SMultiPKFile::Rewrite" to checkpoint
   the state of a VPKFile. Then, while the SPKFile is being rewritten, new
   entries may be added to the VPKFile. After the SPKFile has been written,
   but before it has been committed on disk, each relevant VPKFile is updated
   from its checkpoint by removing the entries in the checkpoint. See the
   "VToSCache" procedure and the "NewVF" function in "SVCache.spec".

   New "VPKFileChkPt" objects are created by the "VPKFile::CheckPoint" method.

   To checkpoint a VPKFile without having to make a complete copy of it, we
   rely on certain properties of how VPKFile's are augmented:

   To checkpoint the scalar "pkEpoch" and "namesEpoch" values, we simply copy
   their values.

   Since the "allNames" list is only appended to when new entries are added to
   the VPKFile, we checkpoint it by recording its current length and a pointer
   to the current "allNames" value.

   Since the "newUncommon" list of cache entries is only prepended to, we
   checkpoint it by making copies of its cache entries in "newUncommon" and
   recording its current value in "newUncommonHead". Copying the cache entries
   is necessary because the "SMultiPKFile::Rewrite" method can change the
   contents of the entries. Similarly, we checkpoint the "newCommon" table by
   making copies of its entries in "newCommon" and saving copies of the heads
   of each list in the "newCommonHeads" table as well. */

class VPKFileChkPt {
  public:
    // constructor
    VPKFileChkPt()
    : pkEpoch(0), namesEpoch(0), allNamesLen(0), allNames(NULL),
      newUncommon(NULL), newUncommonHead(NULL), hasNewEntries(false)
    { /*SKIP*/ }

    // dictionary type
    typedef Table<FP::Tag,CE::List*>::Default CFPEntryMap;
    typedef Table<FP::Tag,CE::List*>::Iterator CFPEntryIter;

    // data fields
    const Text *sourceFunc;           // copy of "sourceFunc"
    PKFile::Epoch pkEpoch;            // copy of "pkEpoch"
    FV::Epoch namesEpoch;             // copy of "namesEpoch"
    unsigned int allNamesLen;         // copy of "allNames.len"
    const FV::ListApp *allNames;      // ptr to "allNames"
    const CE::List *newUncommon;      // checkpoint of "newUncommon"
    CFPEntryMap newCommon;            // table of checkpoints of "newCommon"
    const CE::List *newUncommonHead;  // ptr to original "newUncommon" list
    CFPEntryMap newCommonHeads;       // table of pointers to original lists
    bool hasNewEntries;

    /* "hasNewEntries" is true iff "newUncommonHead != NULL" or if
       "newCommonHeads.Size() > 0". That is, it represents if there
       are any new cache entries in the checkpoint. */
};

#endif // _VPK_FILE_CHK_PT
