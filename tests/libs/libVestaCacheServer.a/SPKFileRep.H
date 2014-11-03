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

// SPKFileRep.H -- classes for reading/writing PKFile header records

#ifndef _SPK_FILE_REP_H
#define _SPK_FILE_REP_H

#include <Basics.H>
#include <FS.H>
#include <FP.H>
#include <fstream>

class SPKFileRep {
  public:
    // Current version number of disk data. This is the version number for
    // MultiPKFiles and for the PKFiles within them. All positive versions
    // less than the current one are also supported.
    //
    // Version history:
    //  1 -- original version
    //  2 -- added magic number to SMultiPKFileRep::Header; no change to
    //       SMultiPKFile header entry format or PKFile format on disk
    //  3 -- added sourceFunc field to PKFiles
    //  4 -- changed the representations of certain data structures to
    //       allow more free variables (eliminating limits imposed by
    //       the use of 16-bit integers)
    enum {
        OriginalVersion = 1,
	MPKMagicNumVersion = 2,
        SourceFuncVersion = 3,
	LargeFVsVersion = 4
    };
    enum {
	FirstVersion = OriginalVersion,
	LastVersion = LargeFVsVersion
    };

    // type for ints of various sizes
    typedef unsigned short UShort;
    typedef unsigned int UInt;

    // <CFPHeaderEntry> -------------------------------------------------------

    class HeaderEntry {
      public:
	// constructors
	HeaderEntry() { /*SKIP*/ }
	HeaderEntry(std::ifstream &ifs, std::streampos origin)
	    throw (FS::EndOfFile, FS::Failure)
	    { Read(ifs, origin); }
	/* REQUIRES FS::Posn(ifs) == ``start of <CFPHeaderEntry>'' */

	// reading/writing
	void Read(std::ifstream &ifs, std::streampos origin)
	  throw (FS::EndOfFile, FS::Failure);
	/* REQUIRES FS::Posn(ifs) == ``start of <CFPHeaderEntry>'' */

	void Write(std::ostream &ofs, std::streampos origin) throw (FS::Failure);
	/* REQUIRES FS::Posn(ofs) == ``start of <CFPHeaderEntry>'' */

	void Debug(std::ostream &os, bool verbose = false) const throw ();
	/* Write an ASCII representation of this "HeaderEntry" to "os". By
	   default, a one-line elided representation is written. If "verbose"
	   is true, a complete multi-line representation is written. */

	// size
	static int Size() throw() { return sizeof(FP::Tag) + sizeof(UInt); }

	// disk data
        FP::Tag cfp;       // common fingerprint
	UInt offset;	   // start of corresponding <PKEntries> (*)

	// auxiliary data -- not written to disk
	UInt offsetLoc;    // location of "offset" on disk (*)

	// (*) These offsets are interpreted relative to the start of
        //     the corresponding <PKFile>.
    }; // <CFPHeaderEntry>

    // <CFPHeader> ------------------------------------------------------------

    // "Header.type" values
    enum HeaderType { HT_List, HT_SortedList, HT_HashTable,
      HT_PerfectHashTable, HT_Last };

    class Header {
      public:
	// constructors
	Header() : num(0), type(HT_Last), entry(NULL) { /*SKIP*/ }
	Header(std::ifstream &ifs, std::streampos origin, int version,
            bool readEntries = true) throw (FS::EndOfFile, FS::Failure)
	    : entry(NULL) { Read(ifs, origin, version, readEntries); }
	/* REQUIRES FS::Posn(ifs) == ``start of <CFPHeader>'' */

    	// skip
    	static void Skip(std::ifstream &ifs, std::streampos origin, int version)
    	   throw (FS::EndOfFile, FS::Failure);
    	/* REQUIRES FS::Posn(ifs) == ``start of <CFPHeader>'' */
    	/* Move the current position of "ifs" to the end of the <CFPHeader>. */

	// reading/writing
	void Read(std::ifstream &ifs, std::streampos origin, int version,
          bool readEntries = true) throw (FS::EndOfFile, FS::Failure);
	/* REQUIRES FS::Posn(ifs) == ``start of <CFPHeader>'' */
	/* Initializes the "Header" from the stream "ifs", computing internal
	   offsets relative to "origin". "version" is the version number found
	   in the MultiPKFile <Header>. If "readEntries" is true, this also
           allocates the "entry" array and reads the <CFPHeaderEntry> records
	   into it. Otherwise, it leaves "ifs" positioned at the start of the
	   <CFPTypedHeader>. */

	void Write(std::ostream &ofs, std::streampos origin) throw (FS::Failure);
	/* REQUIRES FS::Posn(ofs) == ``start of <CFPHeader>'' */
	/* Write the complete "Header" to "ofs" using "origin" as the origin
	   position against which offsets in the <CFPHeader> are computed.
           This includes writing the <CFPHeaderEntry> records represented
           by the "entry" array. This method may have the side-effect of
	   permuting the "HeaderEntry"'s in the "entry" array. */

        void BackPatch(std::ostream &ofs, std::streampos origin) const
          throw (FS::Failure);
        /* Back-patch the "offset"s in this PKFile's <HeaderEntry> records
           by seeking to their locations relative to "origin" and writing
           their values to "ofs". */ 

	void Debug(std::ostream &os, bool verbose = false) const throw ();
	/* Write an ASCII represenation of this "Header" to "os". By default,
	   a one-line elided representation is written. If "verbose" is true,
	   a multi-line complete represenation is written. In any event, this
	   does not write the information in the "entry" array; use the
	   "HeaderEntry::Debug" method for that. */

	// disk data
	UShort num;    	    // number of <CFPHeaderEntry>'s
	UShort type;   	    // format in which HeaderEntry's are stored

	// auxiliary data
	HeaderEntry *entry; // array of corresponding <CFPHeaderEntry>'s

        /* The "entry" field is a dynamically allocated list of this
           PKFile's <CFPHeaderEntry> records, in order; it has length
           "num". */

      private:
        static void SkipListV1(std::ifstream &ifs, int num)
          throw (FS::EndOfFile, FS::Failure);
	/* REQUIRES FS::Posn(ifs) == ``start of <CFPSeqHeader>'' */

	void ReadListV1(std::ifstream &ifs, std::streampos origin)
	  throw (FS::EndOfFile, FS::Failure);
	/* REQUIRES FS::Posn(ifs) == ``start of <CFPHeader>'' */

	void WriteList(std::ostream &ofs, std::streampos origin)
          throw (FS::Failure);
	/* REQUIRES FS::Posn(ofs) == ``start of <CFPHeader>'' */

	void WriteSortedList(std::ostream &ofs, std::streampos origin)
          throw (FS::Failure);
	/* REQUIRES FS::Posn(ofs) == ``start of <CFPHeader>'' */
    }; // <CFPHeader>
};

#endif // _SPK_FILE_REP_H
