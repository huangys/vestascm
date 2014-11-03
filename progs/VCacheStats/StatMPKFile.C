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

// Last modified on Fri Apr 29 00:27:48 EDT 2005 by ken@xorian.net         
//      modified on Mon Jul 15 17:14:55 EDT 2002 by kcschalk@shr.intel.com 
//      modified on Sat Feb 12 13:10:04 PST 2000 by mann  
//      modified on Wed Oct 21 20:24:26 PDT 1998 by heydon

#include <Basics.H>
#include <FS.H>
#include "SMultiPKFileRep.H"
#include "StatError.H"
#include "StatNames.H"
#include "StatDirEntry.H"
#include "StatPKFile.H"
#include "StatMPKFile.H"

using std::ifstream;
using std::cout;
using std::endl;

int MPKFileObj::Search(int verbose, /*INOUT*/ Stat::Collection &stats)
  throw (StatError::BadMPKFile,
	 StatError::EndOfFile, FS::Failure, FS::DoesNotExist)
{
    if (verbose >= 2) {
	cout << "  " << this->fname << endl;
    }

    // read the MPKFile
    ifstream ifs;
    FS::OpenReadOnly(this->fname, /*OUT*/ ifs);
    try {
	try {
	  this->hdr = NEW_CONSTR(SMultiPKFileRep::Header, (ifs));
	  this->hdr->ReadEntries(ifs);
	  this->hdr->ReadPKFiles(ifs);
	}
	catch (SMultiPKFileRep::BadMPKFile) {
	    throw (StatError::BadMPKFile(this->fname));
	}
	catch (FS::EndOfFile) {
	    throw (StatError::EndOfFile(this->fname));
	}
    }
    catch (...) {
	FS::Close(ifs);
	throw;
    }
    FS::Close(ifs);

    // iterate over children
    int cnt;
    {
      PKFileObj *pkf = (PKFileObj *) 0;
      MPKFileIter it(this);
      for (cnt = 0; it.Next(/*OUT*/ pkf); cnt++) {
	(void) pkf->Search(verbose, /*INOUT*/ stats);
      }
      pkf = (PKFileObj *) 0; // drop on floor for GC
    }

    // update "stats.entryStats" field for this MultiPKFile
    stats.entryStats[Stat::MPKFileSize].AddVal(this->hdr->totalLen, this->loc);

    // update fan-out for this level
    int thisLevel = 3;
    StatCount *sc;
    if (stats.fanout.size() <= thisLevel) {
	assert(stats.fanout.size() == thisLevel);
	sc = NEW(StatCount);
	stats.fanout.addhi(sc);
    } else {
	sc = stats.fanout.get(thisLevel);
    }
    sc->AddVal(cnt, this->loc);
    return thisLevel;
}

bool MPKFileIter::Next(/*OUT*/ PKFileObj* &pkf) throw ()
{
    const SMultiPKFileRep::Header *hdr = this->mpkf->hdr;
    bool res = (this->pkIndex < hdr->num);
    if (res) {
	SMultiPKFileRep::HeaderEntry *he;
	FP::Tag *fpPtr = hdr->pkSeq[this->pkIndex++];
	bool inTbl = hdr->pkTbl.Get(*fpPtr, /*OUT*/ he);
	assert(inTbl);
	pkf = NEW_CONSTR(PKFileObj, (*(mpkf->loc), he));
    }
    return res;
}
