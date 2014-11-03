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

// Last modified on Wed Dec  8 01:33:30 EST 2004 by ken@xorian.net

#ifndef _FPSHORTID_H
#define _FPSHORTID_H

#include "Basics.H"
#include "FP.H"
#include "ShortIdBlock.H"

//
// The methods are unmonitored
//
class FPShortIdTable {
  public:
    typedef FP::Tag (*GetFP)(Bit8* ptr);
    typedef ShortId (*GetSid)(Bit8* ptr);

    FPShortIdTable(GetFP getfp, GetSid getsid,
		   Bit32 sizeHint = 0, double resizeF = 2.0,
		   double resizeT = 0.7);
    ~FPShortIdTable();
    ShortId Get(const FP::Tag& fptag);
    bool Set(Bit8 *ptr, bool resize = true);
    bool Resize(Bit32 size);
    Bit32 Size();
    Bit32 Entries();
    void Clear();  // empty the table but do not resize

  private:
    Bit32 numEntries;      // current number of entries in table
    Bit32 tableSize;
    double resizeTrigger;
    double resizeFactor;
    
    Bit32 *table;   
    GetFP getfp;
    GetSid getsid;
};

// These procedures are used to create two instances of the above
// object and manipulate them.  They are monitored by a private mutex.
// Better coding style might put this layer in a separate .H file.
//

// Initialization.  Call once from RepositoryMain.
void InitFPShortId();

// Clear both tables; used in preparation for rebuilding them after a
// checkpoint or source weed.
void DeleteAllFPShortId();

// Enter a file fingerprint in the table, starting with a pointer to
// the file's directory entry.  The pointer is retained, which is OK
// for entries in immutable or appendable directories. Entries in
// immutable directories are safe because they do not change.  Entries
// in appendable directories are safe because they can change only by
// having their entry type set to "deleted" or "outdated", in which
// case we ignore them on lookup.
void SetFPFileShortId(Bit8 *entry);

// Enter a directory fingerprint in the table, starting with a
// pointer to the representation.  This pointer is retained, which is
// OK because only an immutable directory can be passed in.
void SetFPDirShortId(Bit8 *rep);

// Get either a file or directory.  The fptag is looked up in both
// tables.  This is a bit inefficient, and could be avoided since the
// ultimate caller knows (in today's applications, at least) whether
// it is expecting a file or a directory.  However, for historical
// reasons the existing VestaSource interface doesn't supply this
// information, and it's a bit of a pain to change it and the client.
ShortId GetFPShortId(const FP::Tag &fptag);

#endif //_FPSHORTID_H
