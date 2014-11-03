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

#include <Basics.H>
#include <FS.H>

#include "SPKFileRep.H"

using std::ios;
using std::ostream;
using std::streampos;
using std::ifstream;
using std::endl;

// <CFPHeaderEntry> routines --------------------------------------------------

void SPKFileRep::HeaderEntry::Read(ifstream &ifs, streampos origin)
  throw (FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ifs) == ``start of <CFPHeaderEntry>'' */
{
    FS::Read(ifs, (char *)(&(this->cfp)), sizeof(this->cfp));
    this->offsetLoc = FS::Posn(ifs) - origin;
    FS::Read(ifs, (char *)(&(this->offset)), sizeof(this->offset));
}

void SPKFileRep::HeaderEntry::Write(ostream &ofs, streampos origin)
  throw (FS::Failure)
/* REQUIRES FS::Posn(ofs) == ``start of <CFPHeaderEntry>'' */
{
    FS::Write(ofs, (char *)(&(this->cfp)), sizeof(this->cfp));
    this->offsetLoc = FS::Posn(ofs) - origin;
    FS::Write(ofs, (char *)(&(this->offset)), sizeof(this->offset));
}

void SPKFileRep::HeaderEntry::Debug(ostream &os, bool verbose) const throw ()
{
    if (verbose) {
	os << endl << "// <CFPHeaderEntry>" << endl;
	os << "cfp       = " << this->cfp << endl;
	os << "offset    = " << this->offset << endl;
	os << "offsetLoc = " << this->offsetLoc << endl;
    } else {
	os << "cfp = " << this->cfp
           << ", offset = " << this->offset << endl;
    }
}

// <CFPHeader> routines -------------------------------------------------------

void SPKFileRep::Header::Skip(ifstream &ifs, streampos origin, int version)
  throw (FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ifs) == ``start of <PKFile>'' */
{
    SPKFileRep::Header hdr(ifs, origin, version, /*readEntries=*/ false);
    switch (hdr.type) {
      case HT_List:
      case HT_SortedList:
	if (version <= SPKFileRep::LargeFVsVersion) {
	    SkipListV1(ifs, hdr.num);
	} else {
	    // unsupported version
	    assert(false);
	}
	break;
      case HT_HashTable:
      case HT_PerfectHashTable:
	/*** NYI ***/
	assert(false);
      default:
	// unrecognized type
	assert(false);
    }
}

void SPKFileRep::Header::SkipListV1(ifstream &ifs, int num)
  throw (FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ifs) == ``start of <CFPSeqHeader>'' */
{
    FS::Seek(ifs, num * HeaderEntry::Size(), ios::cur);
}

void SPKFileRep::Header::Read(ifstream &ifs, streampos origin, int version,
  bool readEntries) throw (FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ifs) == ``start of <CFPHeader>'' */
{
    // read immediate disk data
    FS::Read(ifs, (char *)(&(this->num)), sizeof(this->num));
    FS::Read(ifs, (char *)(&(this->type)), sizeof(this->type));

    if (readEntries) {
      // fill in "entry" based on "type"
      this->entry = NEW_ARRAY(HeaderEntry, this->num);
      switch (this->type) {
      case HT_List:
      case HT_SortedList:
	if (version <= SPKFileRep::LargeFVsVersion) {
	  ReadListV1(ifs, origin);
	} else {
	  // unsupported version
	  assert(false);
	}
	break;
      case HT_HashTable:
      case HT_PerfectHashTable:
	/*** NYI ***/
	assert(false);
      default:
	assert(false);
      }
    }
}

void SPKFileRep::Header::Write(ostream &ofs, streampos origin)
  throw (FS::Failure)
/* REQUIRES FS::Posn(ofs) == ``start of <CFPHeader>'' */
{
    // determine format type
    if (this->num < 8) {
	this->type = HT_List;
    } else {
	this->type = HT_SortedList;
    }

    // write immediate disk data
    FS::Write(ofs, (char *)(&(this->num)), sizeof(this->num));
    FS::Write(ofs, (char *)(&(this->type)), sizeof(this->type));

    // write "entry" data
    switch (this->type) {
      case HT_List:
	WriteList(ofs, origin);
	break;
      case HT_SortedList:
	WriteSortedList(ofs, origin);
	break;
      case HT_HashTable:
      case HT_PerfectHashTable:
	/*** NYI ***/
	assert(false);
      default:
	assert(false);
    }
}

void SPKFileRep::Header::ReadListV1(ifstream &ifs, streampos origin)
  throw (FS::EndOfFile, FS::Failure)
{
    // read <CFPHeaderEntry> records
    for (int i = 0; i < this->num; i++) {
	this->entry[i].Read(ifs, origin);
    }
}

void SPKFileRep::Header::WriteList(ostream &ofs, streampos origin)
  throw (FS::Failure)
/* REQUIRES FS::Posn(ofs) == ``start of <CFPHeader>'' */
{
    // write <CFPHeaderEntry> records
    for (int i = 0; i < this->num; i++) {
	this->entry[i].Write(ofs, origin);
    }
}

extern "C"
{
  static int SPKFileRep_HECompare(const void *p1, const void *p2) throw ()
  {
    const SPKFileRep::HeaderEntry *he1 = (SPKFileRep::HeaderEntry *)p1;
    const SPKFileRep::HeaderEntry *he2 = (SPKFileRep::HeaderEntry *)p2;
    return Compare(he1->cfp, he2->cfp);
  }
}

void SPKFileRep::Header::WriteSortedList(ostream &ofs, streampos origin)
  throw (FS::Failure)
{
    // sort the entries using qsort(3)
    qsort((void *)(this->entry), (size_t)(this->num),
      (size_t)sizeof(*(this->entry)), SPKFileRep_HECompare);

    // write them as a simple list
    WriteList(ofs, origin);
}

void SPKFileRep::Header::BackPatch(ostream &ofs, streampos origin) const
  throw (FS::Failure)
{
    for (int i = 0; i < this->num; i++) {
	FS::Seek(ofs, origin + (streampos) this->entry[i].offsetLoc);
	FS::Write(ofs, (char *)(&(this->entry[i].offset)),
          sizeof(this->entry[i].offset));
    }
}

const char *HeaderTypeName[] = {
  "List", "SortedList", "HashTable", "PerfectHashTable" };

void SPKFileRep::Header::Debug(ostream &os, bool verbose) const throw ()
{
    if (verbose) {
	os << endl << "// <CFPHeader>" << endl;
	os << "num       = " << this->num << endl;
	assert(/*HT_List <= this->type &&*/ this->type < HT_Last);
	os << "type      = " << HeaderTypeName[this->type] << endl;
    } else {
	os << "num = " << this->num << endl;
    }
}
