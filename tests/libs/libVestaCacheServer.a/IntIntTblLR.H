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

/* IntIntTblLR.H -- Like a generic "IntIntTbl", defines a table that maps
   non-negative shorts to short, but these tables also have methods to
   write/read them from logs and streams. */

#ifndef _INT_INT_TBL_LR_H
#define _INT_INT_TBL_LR_H

#include <Basics.H>
#include <FS.H>
#include <VestaLog.H>
#include <Recovery.H>

class IntIntTblLR {
  public:
    typedef Basics::uint32 int_type;

    // the value of unused entries in the table
    static const int_type Unused;

    // Used for small storage when we can.
    typedef Basics::uint16 int_type_sm;
    static const int_type_sm Unused_sm;

    // Exception types thrown when keys/values which are invalid are
    // given
    struct InvalidKey
    {
      Basics::int64 key;
      InvalidKey(Basics::int64 k) : key(k) { }
    };
    struct InvalidValue
    {
      Basics::int64 val;
      InvalidValue(Basics::int64 v) : val(v) { }
    };

    // Check whether a key/value is valid (even without an instance)
    static int_type CheckKey(Basics::int64 lk) throw(InvalidKey);
    static int_type CheckValue(Basics::int64 lv) throw(InvalidValue);

    // constructors
    IntIntTblLR(Basics::uint32 sizeHint = 0) throw ();
    IntIntTblLR(const IntIntTblLR *tbl) throw ();
    IntIntTblLR(RecoveryReader &rd, bool old_format = false)
      throw (VestaLog::Error, VestaLog::Eof)
      { Recover(rd, old_format); }
    IntIntTblLR(std::istream &ifs, bool old_format = false)
      throw (FS::EndOfFile, FS::Failure)
      { Read(ifs, old_format); }
    ~IntIntTblLR() throw ()
	{
	  // Help the garbage collector
	  this->elt = 0;
	  this->elt_sm = 0;
	}

    // table operations
    bool Get(Basics::int64 lk, /*OUT*/ int_type& v) const throw ();
    /* If the key "lk" exists in the table, then set "v" to the
       associated value and return "true". Otherwise, leave "v"
       unchanged and return "false".

       If "lk" won't fit in int_type, "false" is returned. */

    bool Put(Basics::int64 lk, Basics::int64 lv) throw (InvalidKey, InvalidValue);
    /* Set the value associated with key "lk" in the table to
       "lv". Return "true" iff there was already a value associated
       with "lk" in the table before the method was invoked.

       If "lk" won't fit in int_type, InvalidKey is thrown.  If "lv"
       won't fit in int_type, InvalidValue is thrown. */

    bool Delete(Basics::int64 lk, /*OUT*/ int_type& v) throw ();
    /* If the key "lk" exists in the table, set "v" to its associated
       value, remove the association from the table, and return "true".
       Otherwise, leave "v" unchanged and return "false".

       If "lk" won't fit in int_type, "false" is returned. */

    Basics::uint32 Size() const throw () { return this->numEntries; }
    /* Return the number of <k,v> associations in the table. */

    Basics::uint32 ArraySize() const throw () { return this->eltLen; }
    /* Return the size of the underlying array. */

    // log/recover
    void Log(VestaLog& log) const throw (VestaLog::Error);
    /* Append a representation of this table to "log". Requires "log" to
       be in the "logging" state. */
    void Recover(RecoveryReader &rd, bool old_format = false)
      throw (VestaLog::Error, VestaLog::Eof);
    /* Recover the representation of this table from "rd". */

    // write/read
    void Write(std::ostream &ofs, bool old_format = false)
      const throw (FS::Failure);
    /* Write this table to "ofs". */
    void Read(std::istream &ifs, bool old_format = false)
      throw (FS::EndOfFile, FS::Failure);
    /* Read this table from "ifs". */

    bool CanWriteOld() const throw ();
    /* Can we write this table in the old format? */

    // print
    void Print(std::ostream &os, int indent) const throw ();

    // Comparison (primarily for testing)
    bool operator==(const IntIntTblLR &other) const throw ();
    bool operator!=(const IntIntTblLR &other) const throw ()
    {
      return !(*this == other);
    }

  private:
    Basics::uint32 numEntries;  // number of elements in "elt"
    Basics::uint32 eltLen;      // size of "elt" array
    int_type *elt;    // array of elements; may be NULL if "eltLen == 0"
    int_type_sm *elt_sm;

    // Find a lower value for the number of elements in the entry
    // array by searching down from the end for the highest non-empty
    // element.  This makes it possible to write in a more compact
    // format if "this->eltLen" has a lot of extra space.
    Basics::uint32 StoreEltLen() const throw();

    // Determine the format to store in.  Returns one of four values
    // for a format byte:

    // 1 : 16-bit integer array (high occupancy)

    // 2 : 16-bit integer key/value pairs (low occupancy)

    // 3 : 32-bit integer array (high occupancy)

    // 4 : 32-bit integer  key/value pairs (low occupancy)

    // "reduced_eltLen" is the value stored for eltLen, computed with
    // StoreEltLen() above.

    // "write_pairs" determines whether we write a sequence of
    // key/value pairs or a whole array.

    // "use_small" determines whether we use 16-bit integers or 32-bit
    // integers.
    char StoreFormat(Basics::uint32 reduced_eltLen,
		     /*OUT*/ bool &use_small, /*OUT*/ bool &write_pairs)
      const throw();

    // Decode a format byte read from disk.
    static bool DecodeStoreFormat(char format_byte,
				  /*OUT*/ bool &use_small,
				  /*OUT*/ bool &read_pairs)
      throw();

    friend class IntIntTblIter;
};

class IntIntTblIter {
  public:
    typedef IntIntTblLR::int_type int_type;

    IntIntTblIter(const IntIntTblLR* tbl) throw ()
	: tbl(tbl) { Reset(); }
    /* Return a new iterator on the "Default" table "tbl". While the
       iterator is in use, the client is only allowed to modify the table
       by invoking the "Put" and "Delete" methods with "resize = false".
       Once the iteration is complete, it is recommended that the "Resize"
       method be called to adjust the size of the table if necessary. */

    void Reset() throw () { this->k = 0; this->done = false; }
    /* Resets the iterator so that it will iterate over all the elements
       in its associated table. This can be used to iterate over the
       elements of the same table multiple times. */

    bool Next(/*OUT*/ int_type& k, /*OUT*/ int_type& v) throw ();
    /* If there is an association in this iterator's table that has not
       already been enumerated, select one, set "k" to its key, set "v" to
       its value, and return "true". Otherwise, leave "k" and "v"
       unchanged, and return "false". */
  private:
    const IntIntTblLR* tbl;  // corresponding table
    int_type k;                 // next key to consider
    bool done;               // true if Next() has returned "false"

    // hide copy constructor
    IntIntTblIter(const IntIntTblIter&);
};

#endif // _INT_INT_TBL_LR_H
