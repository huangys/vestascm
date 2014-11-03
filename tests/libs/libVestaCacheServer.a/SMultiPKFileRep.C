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
#include <FP.H>

#include "SPKFileRep.H"      // for SPKFileRep::*Version
#include "SPKFile.H"
#include "SMultiPKFileRep.H"

using std::ostream;
using std::ifstream;
using std::endl;

// HeaderEntry routines -------------------------------------------------------

void SMultiPKFileRep::HeaderEntry::Read(ifstream &ifs)
  throw (FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ifs) == ``start of <Header>'' */
{
    FS::Read(ifs, (char *)(&(this->pk)), sizeof(this->pk));
    this->offsetLoc = FS::Posn(ifs);
    FS::Read(ifs, (char *)(&(this->offset)), sizeof(this->offset));
}

void SMultiPKFileRep::HeaderEntry::Write(ostream &ofs)
  throw (FS::Failure)
/* REQUIRES FS::Posn(ofs) == ``start of <Header>'' */
{
    FS::Write(ofs, (char *)(&(this->pk)), sizeof(this->pk));
    this->offsetLoc = FS::Posn(ofs);
    FS::Write(ofs, (char *)(&(this->offset)), sizeof(this->offset));
}

void SMultiPKFileRep::HeaderEntry::Debug(ostream &os, bool verbose) const
  throw ()
{
    if (!verbose) {
	os << "pk = " << pk << ", offset = " << offset << endl;
    } else {
	os << endl;
        os << "// <HeaderEntry>" << endl;
	os << "pk        = " << pk << endl;
	os << "offset    = " << offset << endl;
	os << "offsetLoc = " << offsetLoc << endl;
	os << "pkLen     = " << pkLen << endl;
    }
}

// Header routines ------------------------------------------------------------

bool SMultiPKFileRep::Header::AppendNewHeaderEntry(const FP::Tag &pk) throw ()
{
    bool res;
    HeaderEntry *he;
    if (!(res=this->pkTbl.Get(pk, /*OUT*/ he))) {
      he = NEW_CONSTR(HeaderEntry, (pk));
      bool inTbl = this->pkTbl.Put(pk, he); assert(!inTbl);
      if (this->num >= this->pksLen) {
	assert(this->num == this->pksLen);
	// allocate a larger "pks" array
	this->pksLen = max(this->pksLen + 2, 2 * this->pksLen);
	assert(this->pksLen > this->num);
	FPTagPtr *newPKs = NEW_ARRAY(FPTagPtr, this->pksLen);
	for (int i=0; i < this->num; i++) newPKs[i] = this->pkSeq[i];
	this->pkSeq = newPKs;
      }
      this->pkSeq[this->num++] = &(he->pk);
    }
    return res;
}

void SMultiPKFileRep::Header::RemoveHeaderEntry(const FP::Tag &pk) throw ()
{
    // find the corresponding PK in "pkSeq"
    int i;
    for (i = 0; i < num; i++) {
	if (*pkSeq[i] == pk) break;
    }
    assert(i < num);

    // shift remaining elements down 1 position
    for (/*SKIP*/; i < num-1; i++) {
	pkSeq[i] = pkSeq[i+1];
    }

    // decrement count and clear last element
    pkSeq[--num] = (FP::Tag *)NULL;
}

// initialize the SMultiPKFile magic number
static const Word MPKFileMagicNumber =
  FP::Tag("SMultiPKFileRep magic number - CAH").Hash();

void SMultiPKFileRep::Header::Read(ifstream &ifs)
  throw (SMultiPKFileRep::BadMPKFile, FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ifs) == ``start of <Header>'' */
{
    // read header info
    FS::Read(ifs, (char *)(&(this->version)), sizeof(this->version));
    if (this->version < SPKFileRep::FirstVersion ||
        this->version > SPKFileRep::LastVersion) {
	throw SMultiPKFileRep::BadMPKFile();
    }
    if (this->version >= SPKFileRep::MPKMagicNumVersion) {
	Word magicNumber;
	FS::Read(ifs, (char *)(&magicNumber), sizeof(magicNumber));
	if (magicNumber != MPKFileMagicNumber) {
	    throw SMultiPKFileRep::BadMPKFile();
	}
    }
    FS::Read(ifs, (char *)(&(this->num)), sizeof(this->num));
    this->lenOffset = FS::Posn(ifs);
    FS::Read(ifs, (char *)(&(this->totalLen)), sizeof(this->totalLen));
    FS::Read(ifs, (char *)(&(this->type)), sizeof(this->type));
}

void SMultiPKFileRep::Header::Write(ostream &ofs) throw (FS::Failure)
/* REQUIRES FS::Posn(ofs) == ``start of <Header>'' */
{
    // determine format type
    if (this->num < 8) {
	this->type = HT_List;
    } else {
	this->type = HT_SortedList;
    }

    // always write the most recent version
    this->version = SPKFileRep::LastVersion;

    // write header
    FS::Write(ofs, (char *)(&(this->version)), sizeof(this->version));
    if (this->version >= SPKFileRep::MPKMagicNumVersion) {
	FS::Write(ofs, (char *)(&MPKFileMagicNumber),
          sizeof(MPKFileMagicNumber));
    }
    FS::Write(ofs, (char *)(&(this->num)), sizeof(this->num));
    this->lenOffset = FS::Posn(ofs);
    // reading the following field is irrelevant
    FS::Write(ofs, (char *)(&(this->totalLen)), sizeof(this->totalLen));
    FS::Write(ofs, (char *)(&(this->type)), sizeof(this->type));
}

void SMultiPKFileRep::Header::ReadEntries(ifstream &ifs)
  throw (FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ifs) == ``start of <TypedHeader>'' */
{
    // read <HeaderEntry> records
    switch (this->type) {
      case HT_List:
      case HT_SortedList:
	if (this->version <= SPKFileRep::LargeFVsVersion) {
	    ReadListV1(ifs);
	} else {
	    // unsupported version
	    assert(false);
	}
	break;
      case HT_HashTable:
	/*** NYI ***/
	assert(false);
      default:
	// programmer error
	assert(false);
    }
}

void SMultiPKFileRep::Header::WriteEntries(ostream &ofs) throw (FS::Failure)
/* REQUIRES FS::Posn(ofs) == ``start of <TypedHeader>'' */
{
    switch (this->type) {
      case HT_List:
	WriteList(ofs);
	break;
      case HT_SortedList:
	WriteSortedList(ofs);
	break;
      case HT_HashTable:
	/*** NYI ***/
	assert(false);
      default:
	assert(false);
    }
}

void SMultiPKFileRep::Header::ReadListV1(ifstream &ifs)
  throw (FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ifs) == ``start of <SeqHeader>'' */
{
    HeaderEntry *prev = (HeaderEntry *)NULL;

    // allocate "pks" array
    this->pksLen = this->num;
    this->pkSeq = NEW_ARRAY(FPTagPtr, this->pksLen);

    // read <HeaderEntry> records and add them to "pkTbl"
    for (int i = 0; i < this->num; i++) {
      HeaderEntry *ent = NEW_CONSTR(HeaderEntry, (ifs));
	this->pkSeq[i] = &(ent->pk);
	if (prev != NULL) {
	    prev->pkLen = ent->offset - prev->offset;
	}
	bool inMap = this->pkTbl.Put(ent->pk, ent); assert(!inMap);
	prev = ent;
    }

    // set length of last <PKFile>
    if (prev != NULL) {
	prev->pkLen = this->totalLen - prev->offset;
    }
}

void SMultiPKFileRep::Header::WriteList(ostream &ofs) throw (FS::Failure)
/* REQUIRES FS::Posn(ofs) == ``start of <TypedHeader>'' */
{
    // write entries in order according to "pks" array
    for (int i = 0; i < this->num; i++) {
	HeaderEntry *ent;
	bool inTbl = this->pkTbl.Get(*(this->pkSeq[i]), /*OUT*/ ent);
	assert(inTbl);
	ent->Write(ofs);
    }
}

extern "C"
{
  static int SMultiPKFileRep_PKCompare(const void *p1, const void *p2) throw ()
  {
    FP::Tag *fpPtr1 = *((FP::Tag **)p1);
    FP::Tag *fpPtr2 = *((FP::Tag **)p2);
    return Compare(*fpPtr1, *fpPtr2);
  }
}

void SMultiPKFileRep::Header::WriteSortedList(ostream &ofs)
  throw (FS::Failure)
/* REQUIRES FS::Posn(ofs) == ``start of <TypedHeader>'' */
{
    // sort the list
    qsort((void *)(this->pkSeq), (size_t)(this->num),
      (size_t)sizeof(*(this->pkSeq)), SMultiPKFileRep_PKCompare);

    // write them as a simple list
    WriteList(ofs);
}

void SMultiPKFileRep::Header::ReadPKFiles(ifstream &ifs)
  throw (FS::EndOfFile, FS::Failure)
/* REQUIRES FS::Posn(ofs) == ``start of <PKFile>*'' */
{
    // read entries
    for (int i = 0; i < this->num; i++) {
	HeaderEntry *ent;
	bool inTbl = this->pkTbl.Get(*(this->pkSeq[i]), ent); assert(inTbl);
	ent->pkfile = NEW_CONSTR(SPKFile,
				 (ent->pk, ifs, ent->offset,
				  this->version, ent->pkhdr,
				  /*readEntries=*/ true));
    }
}

void SMultiPKFileRep::Header::BackPatch(ostream &ofs)
  throw (FS::Failure)
{
    // record "totalLen"
    totalLen = FS::Posn(ofs);

    // write "totalLen"
    FS::Seek(ofs, lenOffset);
    FS::Write(ofs, (char *)(&(this->totalLen)), sizeof(this->totalLen));

    // write <HeaderEntry> "offset"'s
    for (int i = 0; i < this->num; i++) {
	HeaderEntry *ent;
	bool inTbl = this->pkTbl.Get(*(this->pkSeq[i]), ent); assert(inTbl);
	FS::Seek(ofs, ent->offsetLoc);
	FS::Write(ofs, (char *)(&(ent->offset)), sizeof(ent->offset));
    }
}

static const char *HeaderTypeName[SMultiPKFileRep::HT_Last] =
  { "List", "SortedList", "HashTable" };

void SMultiPKFileRep::Header::Debug(ostream &os, bool verbose) const throw ()
{
    // write <Header> info
    if (!verbose) {
	os << endl << "num = " << num << endl;
    } else {
	os << endl << "// <Header>" << endl;
	os << "version   = " << version << endl;
	os << "num       = " << num << endl;
	os << "totalLen  = " << totalLen << endl;
	assert(/*HT_List <= type &&*/ type < HT_Last);
	os << "type      = " << HeaderTypeName[type] << endl;
	os << "lenOffset = " << lenOffset << endl;
    }

    // write header entries
    for (int i = 0; i < num; i++) {
	HeaderEntry *he;
	bool inTbl = pkTbl.Get(*(pkSeq[i]), he);
	assert(inTbl);
	he->Debug(os, verbose);
    }
}
