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

// SMultiPKFileRep.H -- classes for reading/writing MultiPKFile header records 

#ifndef _SMULTI_PK_FILE_REP_H
#define _SMULTI_PK_FILE_REP_H

#include <Basics.H>
#include <FS.H>
#include <Table.H>
#include <FP.H>
#include <BitVector.H>

#include "SPKFileRep.H"
#include "IntIntTblLR.H"
#include "SPKFile.H"

class SMultiPKFileRep {
public:
  // type for offsets/lengths within a file
  typedef unsigned short UShort;
  typedef unsigned int UInt;

  // Exceptions
  class NotFound {};
  class BadMPKFile {}; // the file is not a valid MultiPKFile

  // <HeaderEntry> ----------------------------------------------------------

  class HeaderEntry {
  public:
    // constructors
    HeaderEntry() throw ()
      : pk(), offset(0), offsetLoc(0), pkLen(0), pkfile(NULL), pkhdr(NULL),
	pkfileModified(false), becameEmpty(false)
    { /*SKIP*/ }
    HeaderEntry(const FP::Tag &pk) throw ()
      : pk(pk), offset(0), offsetLoc(0), pkLen(0), pkfile(NULL),pkhdr(NULL),
	pkfileModified(false), becameEmpty(false)
    { /*SKIP*/ }
    HeaderEntry(std::ifstream &ifs) throw (FS::EndOfFile, FS::Failure)
      : pkLen(0), pkfile(NULL),
	pkfileModified(false), becameEmpty(false)
    { Read(ifs); }
    /* REQUIRES FS::Posn(ifs) == ``start of <HeaderEntry>'' */

    // read/write
    void Read(std::ifstream &ifs) throw (FS::EndOfFile, FS::Failure);
    /* REQUIRES FS::Posn(ifs) == ``start of <HeaderEntry>'' */

    void Write(std::ostream &ofs) throw (FS::Failure);
    /* REQUIRES FS::Posn(ofs) == ``start of <HeaderEntry>'' */

    // other methods
    static int Size() { return sizeof(FP::Tag) + sizeof(UInt); }
    /* Return the size of a "HeaderEntry" on disk. */

    bool SPKFileExists() const throw () { return offset != 0UL; }
    /* Return "true" iff this "HeaderEntry" was created for a real SPKFile
       in the MultiPKFile using the "HeaderEntry(ifstream)" constructor
       or the "Read(ifstream)" method. */

    void Debug(std::ostream &os, bool verbose = false) const throw ();
    /* By default, write a one-line elided representation of this
       <HeaderEntry> to "os". If "verbose" is true, write a complete,
       multi-line representation. */

    // disk data
    FP::Tag pk;
    UInt offset;		   // start of this PKFile w/in the MultiPKFile

    // auxiliary data -- not written to disk
    UInt offsetLoc;            // location of "offset" on disk
    UInt pkLen;		   // length of this PKFile on disk

    // auxiliary SPKFile data
    SPKFile *pkfile;           // the corresponding PKFile...
    SPKFileRep::Header *pkhdr; // and its <CFPHeader> (for back-patching)

    /* The auxiliary SPKFile fields are not changed as part of the "Read"
       and "Write" methods. */

    // pkfile->Update results
    bool pkfileModified;	   // was PKFile changed by "pkfile->Update"?
    BitVector *exCommonNames;  // ex-common names from updating
    BitVector *exUncommonNames;// ex-uncommon names from updating
    BitVector *packMask;       // packing bit vector for "allNames" indices
    IntIntTblLR *reMap;        // remapping of "allNames" indices
    bool becameEmpty;          // were all entries deleted?

    /* The "exCommonNames", "exUncommonNames", "packMask",
       "reMap", and "becameEmpty" fields are temporary values used
       to store the result of the "pkfile->Update" "OUT"
       variables. They are only valid if "pkfileModified" is
       true. */
  };

  // <Header> ---------------------------------------------------------------

  // dictionary types for Header
  typedef Table<FP::Tag,HeaderEntry*>::Default HdrEntryMap;
  typedef Table<FP::Tag,HeaderEntry*>::Iterator HdrEntryIter;

  // "Header.type" values
  enum HeaderType { HT_List, HT_SortedList, HT_HashTable, HT_Last };

  class Header {
  public:
    // constructors
    Header() throw ()
      : version(0), num(0), totalLen(0), type(HT_Last),
	lenOffset(0), pksLen(0) { /*SKIP*/ }
    Header(std::ifstream &ifs) throw (BadMPKFile, FS::EndOfFile, FS::Failure)
    { Read(ifs); }
    /* REQUIRES FS::Posn(ifs) == ``start of <Header>'' */

    // methods
    bool AppendNewHeaderEntry(const FP::Tag &pk) throw ();
    /* If there is no header entry in "pkTbl" under key "pk", create a new
       one for "pk", add it to the table, append it to the "pkSeq" array,
       and return "false". Otherwise, do nothing and return "true". */ 

    void RemoveHeaderEntry(const FP::Tag &pk) throw ();
    /* Remove "pk" from this header's "pkSeq" array, but not from its
       "pkTbl". It is a checked run-time error if "pk" does not occur in
       "pkSeq". This only modifies the locations of the entries in 
       the "pkSeq" array array above the one that is deleted. */

    // reading/writing
    void Read(std::ifstream &ifs) throw (BadMPKFile, FS::EndOfFile,FS::Failure);
    /* REQUIRES FS::Posn(ifs) == ``start of <Header>'' */
    /* Read the "Header" disk data from "ifs", i.e., the fields,
       "version", "num", "totalLen", and "type". The value of the
       auxilliary fields "lenOffset" and "pksLen" are also set. */ 

    void ReadEntries(std::ifstream &ifs) throw (FS::EndOfFile, FS::Failure);
    /* REQUIRES FS::Posn(ifs) == ``start of <TypedHeader>'' */
    /* Initialize the auxiliary fields "pkTbl" and "pkSeq" from "ifs" to
       contain a representation of this MultiPKFile's <HeaderEntry>
       records. This sets the "offsetLoc" and "pkLen" auxiliary fields
       of the "HeaderEntry" objects stored in "pkTbl", but not their
       "pkfile" or "pkhdr" fields. */ 

    void ReadPKFiles(std::ifstream &ifs) throw (FS::EndOfFile, FS::Failure);
    /* REQUIRES FS::Posn(ofs) == ``start of <PKFile>*'' */
    /* Read the <PKFile> records into the "pkfile" and "pkhdr" fields of
       the "HeaderEntry" records in "pkTbl" in the order specified by the
       "pks" array. */

    void Write(std::ostream &ofs) throw (FS::Failure);
    /* REQUIRES FS::Posn(ofs) == ``start of <Header>'' */
    /* Write the "Header" disk data to "ifs", i.e., the fields,
       "version", "num", "totalLen", and "type". The value of "lenOffset"
       is set to the location where the "totalLen" field was written. */ 

    void WriteEntries(std::ostream &ofs) throw (FS::Failure);
    /* REQUIRES FS::Posn(ofs) == ``start of <TypedHeader>'' */
    /* Write this MultiPKFile's <HeaderEntry> records to "ofs" according
       to the "pkTbl" and "pkSeq" fields. This may have the side-effect of
       permuting the PK's in the "pkSeq" array. It may also change the
       values of the "version and "type" fields. */

    void BackPatch(std::ostream &ofs) throw (FS::Failure);
    /* Back-patch the offsets and length values in this <Header> by
       seeking to their locations and writing their values to "ofs". The
       initial position of "ofs" is used as the value for the "totalLen"
       field. */

    // other methods
    int Size()
    { return sizeof(this->version) + sizeof(this->num)
	+ sizeof(this->totalLen) + sizeof(this->type); }

    void Debug(std::ostream &os, bool verbose = false) const throw ();
    /* Write an ASCII representation of this <Header> and its
       <HeaderEntry> records to "os". This does not write the PKFile's
       themselves. If "verbose" is false, write a concise representation.
       Otherwise, write the complete values of the structures. */

    // pointer to a FP::Tag
    typedef FP::Tag *FPTagPtr;

    // disk data
    UShort version;    // version number of MultiPKFile file format
    UShort num;        // number of PKFile's within this MultiPKFile
    UInt totalLen;     // must be back-patched
    UShort type;       // header entry format type (list, hashtable, etc)

    // auxiliary data -- not written to disk
    UInt lenOffset;    // location of "totalLen" field
    UShort pksLen;     // size of "pkSeq" array; "pksLen >= num"
    FPTagPtr *pkSeq;   // an array of the PK's in "Domain(pkTbl)"
    HdrEntryMap pkTbl; // map of this MPKFile's PKFiles: PK -> HeaderEntry

    /* The "pkSeq" array records the *order* in which the <HeaderEntry>
       records (and hence the corresponding <PKFile> records) are written
       in the MultiPKFile. "pksLen" records the allocated size of the
       "pkSeq" array. */

  private:
    void ReadListV1(std::ifstream &ifs) throw (FS::EndOfFile, FS::Failure);
    /* REQUIRES FS::Posn(ifs) == ``start of <SeqHeader>'' */
    /* Read a list of "HeaderEntry"'s into this "Header"'s "pkTbl". */

    void WriteList(std::ostream &ofs) throw (FS::Failure);
    /* REQUIRES FS::Posn(ofs) == ``start of <TypedHeader>'' */
    /* Write the entries in this "Header"'s "pkTbl" to "ofs" as an
       unordered list. */

    void WriteSortedList(std::ostream &ofs) throw (FS::Failure);
    /* REQUIRES FS::Posn(ofs) == ``start of <TypedHeader>'' */
    /* Write the entries in this "Header"'s "pkTbl" to "ofs" is
       sorted order by PK. */
  };
};

#endif // _SMULTI_PK_FILE_REP_H
