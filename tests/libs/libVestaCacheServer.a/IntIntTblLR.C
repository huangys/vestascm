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
#include <VestaLog.H>
#include <Recovery.H>
#include <BufStream.H>

#include "IntIntTblLR.H"

using std::istream;
using std::ostream;
using std::endl;
using Basics::OBufStream;

// the value of unused entries in the table
const IntIntTblLR::int_type IntIntTblLR::Unused = ~((IntIntTblLR::int_type) 0);
const IntIntTblLR::int_type_sm IntIntTblLR::Unused_sm = ~((IntIntTblLR::int_type_sm) 0);

static const Basics::uint32 small_max = (1 << 16) - 2;

IntIntTblLR::IntIntTblLR(Basics::uint32 sizeHint) throw ()
: numEntries(0), eltLen(sizeHint)
{
    this->elt = 0;
    if (sizeHint > 0) {
      // We start out with a 16-bit array regardless of the size.  We
      // could instead pick a 32-bit array if sizeHint is greater than
      // small_max.
      this->elt_sm = NEW_PTRFREE_ARRAY(int_type_sm, sizeHint);
      for (int_type i = 0; i < this->eltLen; i++) {
	this->elt_sm[i] = Unused_sm;
      }
    }
}

IntIntTblLR::IntIntTblLR(const IntIntTblLR *tbl) throw ()
: numEntries(tbl->numEntries), eltLen(tbl->eltLen)
{
    if (this->eltLen > 0) {
      Basics::uint32 numUsed = 0;
      if(tbl->elt != 0)
	{
	  assert(tbl->elt_sm == 0);
	  this->elt = NEW_PTRFREE_ARRAY(int_type, this->eltLen);
	  this->elt_sm = 0;
	  for (int_type i = 0; i < this->eltLen; i++) {
	    this->elt[i] = tbl->elt[i];
	    if (tbl->elt[i] != Unused) numUsed++;
	  }
	}
      else
	{
	  assert(tbl->elt_sm != 0);
	  this->elt_sm = NEW_PTRFREE_ARRAY(int_type_sm, this->eltLen);
	  this->elt = 0;
	  for (int_type i = 0; i < this->eltLen; i++) {
	    this->elt_sm[i] = tbl->elt_sm[i];
	    if (tbl->elt_sm[i] != Unused_sm) numUsed++;
	  }
	}
      assert(numUsed == this->numEntries);
    } else {
	assert(this->numEntries == 0);
	this->elt = 0;
	this->elt_sm = 0;
    }
}

static bool is_valid_num(Basics::int64 ln, bool check_unused = true)
{
  IntIntTblLR::int_type n = ln;
  if((ln < 0) || (((Basics::int64) n) != ln))
    return false;
  if(check_unused && (n == IntIntTblLR::Unused))
    return false;
  return true;
}

static bool is_small_num(Basics::int64 ln, bool check_unused = true)
{
  IntIntTblLR::int_type_sm n = ln;
  if((ln < 0) || (((Basics::int64) n) != ln))
    return false;
  if(check_unused && (n == IntIntTblLR::Unused_sm))
    return false;
  return true;
}

IntIntTblLR::int_type IntIntTblLR::CheckKey(Basics::int64 lk)
  throw(IntIntTblLR::InvalidKey)
{
  if(!is_valid_num(lk))
    throw InvalidKey(lk);
  int_type k = lk;
  return k;
}

IntIntTblLR::int_type IntIntTblLR::CheckValue(Basics::int64 lv)
  throw(IntIntTblLR::InvalidValue)
{
  if(!is_valid_num(lv))
    throw InvalidValue(lv);
  int_type v = lv;
  return v;
}

bool IntIntTblLR::Get(Basics::int64 lk, /*OUT*/ int_type& v) const throw ()
{
    if(!is_valid_num(lk)) return false;
    int_type k = lk;

    bool res = false;
    if (k < this->eltLen) {
      if(this->elt != 0) {
	assert(this->elt_sm == 0);
	int_type val = this->elt[k];
	res = (val != Unused);
	if (res) v = val;
      } else if (this->elt_sm != 0) {
	int_type val = this->elt_sm[k];
	res = (val != Unused_sm);
	if (res) v = val;
      }
    }
    return res;
}

template<class old_type, class new_type>
static void allocate_new_array(old_type *old_elt, Basics::uint32 old_len,
			       new_type *&new_elt, Basics::uint32 new_len)
{
  assert(new_len >= old_len);
  assert(sizeof(new_type) >= sizeof(old_type));

  const old_type old_Unused = ~((old_type) 0);
  const new_type new_Unused = ~((new_type) 0);

  new_elt = NEW_PTRFREE_ARRAY(new_type, new_len);
  Basics::uint32 i = 0;
  if(old_elt != 0)
    {
      for (/*SKIP*/; i < old_len; i++)
	{
	  if(old_elt[i] == old_Unused)
	    {
	      new_elt[i] = new_Unused;
	    }
	  else
	    {
	      new_elt[i] = old_elt[i];
	    }
	}
    }
  for (/*SKIP*/; i < new_len; i++)
    {
      new_elt[i] = new_Unused;
    }
}

bool IntIntTblLR::Put(Basics::int64 lk, Basics::int64 lv)
  throw (IntIntTblLR::InvalidKey, IntIntTblLR::InvalidValue)
{
    int_type k = CheckKey(lk);
    int_type v = CheckValue(lv);

    bool v_is_small = is_small_num(lv);

    if((this->elt_sm != 0) && !v_is_small)
      {
	// Convert existing small (16-bit) array to 32-bit, expanding
	// if needed as we copy
	int_type oldEltLen = this->eltLen;
	this->eltLen = max(k+1, oldEltLen);
	allocate_new_array(this->elt_sm, oldEltLen,
			   this->elt, this->eltLen);
	this->elt_sm = 0;
      }

    bool res;

    if (k < this->eltLen && this->elt_sm != 0) {
        assert(this->elt == 0);
	// fast path: simply test if the slot is in use
	res = (this->elt_sm[k] != Unused_sm);
	if (!res) this->numEntries++;
    } else if (k < this->eltLen && this->elt != 0) {
        assert(this->elt_sm == 0);
	// fast path: simply test if the slot is in use
	res = (this->elt[k] != Unused);
	if (!res) this->numEntries++;
    } else {
	// slow path: the array of elements must be extended or allocated
        int_type oldEltLen = this->eltLen;
	this->eltLen = max(k+1, min(oldEltLen<<1, oldEltLen+1000));
	if((this->elt != 0) || !v_is_small)
	  {
	    // Already in large (32-bit) mode or we need it because of
	    // the value size
	    assert(this->elt_sm == 0);
	    allocate_new_array(this->elt, oldEltLen,
			       this->elt, this->eltLen);
	  }
	else
	  {
	    // Use small mode by default
	    allocate_new_array(this->elt_sm, oldEltLen,
			       this->elt_sm, this->eltLen);
	  }
	this->numEntries++;
	res = false;
    }
    assert(k < this->eltLen);
    if(this->elt != 0)
      {
	assert(this->elt_sm == 0);
	this->elt[k] = v;
      }
    else
      {
	assert(this->elt_sm != 0);
	assert(v_is_small);
	this->elt_sm[k] = v;
      }
    return res;
}

bool IntIntTblLR::Delete(Basics::int64 lk, /*OUT*/ int_type& v) throw ()
{
    if(!is_valid_num(lk)) return false;
    int_type k = lk;

    bool res;
    if (k < this->eltLen && this->elt != 0
	&& this->elt[k] != Unused) {
	v = this->elt[k];
	this->elt[k] = Unused;
	assert(this->numEntries > 0);
	this->numEntries--;
	res = true;
    } else if (k < this->eltLen && this->elt_sm != 0 &&
	       this->elt_sm[k] != Unused_sm) {
	v = this->elt_sm[k];
	this->elt_sm[k] = Unused_sm;
	assert(this->numEntries > 0);
	this->numEntries--;
	res = true;
    } else {
	res = false;
    }
    return res;
}

void IntIntTblLR::Log(VestaLog& log) const
  throw (VestaLog::Error)
{
    Basics::uint32 reduced_eltLen = this->StoreEltLen();

    log.write((char *)(&(this->numEntries)), sizeof(this->numEntries));
    log.write((char *)(&(reduced_eltLen)), sizeof(reduced_eltLen));
    if (this->numEntries > 0) {
      // "write_pairs" determines whether we write a sequence of
      // key/value pairs or a whole array.
      //
      // "use_small" determines whether we use 16-bit integers or
      // 32-bit integers.
      bool write_pairs, use_small;
      char format_byte = this->StoreFormat(reduced_eltLen,
					   use_small, write_pairs);
      log.write(&format_byte, sizeof(format_byte));

      if(write_pairs)
	{
	  Basics::uint32 numWritten = 0;
	  for (int_type k = 0; k < reduced_eltLen; k++)
	    {
	      int_type v;
	      if (this->Get(k, /*OUT*/ v))
		{
		  assert(v != Unused);
		  if(use_small)
		    {
		      assert(is_small_num(v));
		      assert(is_small_num(k, false));

		      int_type_sm k_sm = k, v_sm = v;
		      log.write((char *)(&k_sm), sizeof(k_sm));
		      log.write((char *)(&v_sm), sizeof(v_sm));
		    }
		  else
		    {
		      log.write((char *)(&k), sizeof(k));
		      log.write((char *)(&v), sizeof(v));
		    }
		  numWritten++;
		}
	    }
	  assert(numWritten == this->numEntries);
	}
      else
	{
	  if(use_small)
	    {
	      assert(this->elt_sm != 0);
	      log.write((char *) this->elt_sm,
			sizeof(this->elt_sm[0]) * reduced_eltLen);
	    }
	  else
	    {
	      assert(this->elt != 0);
	      log.write((char *) this->elt,
			sizeof(this->elt[0]) * reduced_eltLen);
	    }
	}
    }
}

void IntIntTblLR::Recover(RecoveryReader &rd, bool old_format)
  throw (VestaLog::Error, VestaLog::Eof)
/* We must set any unused entries to "Unused". We could do this by setting all
   entries to "Unused" immediately after the array is allocated. But if the
   array is mostly full, that would mean writing most entries twice. Instead,
   we set entries to "Unused" between the used entries, and at the end of the
   loop. The correctness of this algorithm relies on the fact that the entries
   are written by the corresponding "Log" method in order of increasing key.
*/
{
    rd.readAll((char *)(&(this->numEntries)), sizeof(this->numEntries));
    rd.readAll((char *)(&(this->eltLen)), sizeof(this->eltLen));
    if (this->numEntries > 0) {
        bool use_small, read_pairs;
	if(old_format)
	  {
	    use_small = true;
	    read_pairs = true;
	  }
	else
	  {
	    char format_byte;
	    rd.readAll(&format_byte, sizeof(format_byte));
	    // This must match the format in the Log member function
	    if(!DecodeStoreFormat(format_byte, use_small, read_pairs))
	      {
		// Invalid format!
		Text emsg =
		  Text::printf("IntIntTblLR::Recover: "
			       "invalid format byte = %u",
			       (unsigned int) format_byte);
		throw VestaLog::Error(0, emsg);
	      }
	  }

	if(use_small)
	  {
	    this->elt_sm = NEW_PTRFREE_ARRAY(int_type_sm, this->eltLen);
	    this->elt = 0;
	  }
	else
	  {
	    this->elt = NEW_PTRFREE_ARRAY(int_type, this->eltLen);
	    this->elt_sm = 0;
	  }

	if(read_pairs)
	  {
	    int_type nextToWrite = 0;
	    // Invariant: All elements of "this->elt"/"this->elt_sm"
	    // in the half-open interval "[0, nextToWrite)" have been
	    // written.
	    for (int_type i = 0; i < this->numEntries; i++) {
	      int_type k, v;
	      if(use_small)
		{
		  int_type_sm k_sm, v_sm;
		  rd.readAll((char *)(&k_sm), sizeof(k_sm));
		  rd.readAll((char *)(&v_sm), sizeof(v_sm));

		  if(v_sm == Unused_sm)
		    {
		      OBufStream msg;
		      msg << ("IntIntTblLR::Recover: "
			      "reserved (unused) value recovered, k = ")
			  << k_sm << ", v = " << v_sm;
		      throw VestaLog::Error(0, msg.str());
		    }

		  k = k_sm;
		  v = v_sm;
		}
	      else
		{
		  rd.readAll((char *)(&k), sizeof(k));
		  rd.readAll((char *)(&v), sizeof(v));

		  if(v == Unused)
		    {
		      OBufStream msg;
		      msg << ("IntIntTblLR::Recover: "
			      "reserved (unused) value recovered, k = ")
			  << k << ", v = " << v;
		      throw VestaLog::Error(0, msg.str());
		    }
		}

	      if(k >= this->eltLen)
		{
		  OBufStream msg;
		  msg << "IntIntTblLR::Recover: key out of range, k = "
		      << k << ", eltLen = " << this->eltLen;
		  throw VestaLog::Error(0, msg.str());
		}

	      // make intervening entries unused
	      assert(k >= nextToWrite);
	      assert((this->elt != 0) || (this->elt_sm != 0));
	      while (nextToWrite < k) {
		if(this->elt != 0)
		  this->elt[nextToWrite++] = Unused;
		else
		  this->elt_sm[nextToWrite++] = Unused_sm;
	      }
	      if(this->elt != 0)
		this->elt[k] = v;
	      else
		{
		  if(!is_small_num(v))
		    {
		      OBufStream msg;
		      msg << ("IntIntTblLR::Recover: "
			      "large value recovered but using small storage"
			      ", k = ")
			  << k << ", v = " << v;
		      throw VestaLog::Error(0, msg.str());
		    }

		  this->elt_sm[k] = v;
		}
	    
	      nextToWrite++; // == k+1
	    }
	    // make any remaining entries unused
	    while (nextToWrite < this->eltLen) {
	      if(this->elt != 0)
		this->elt[nextToWrite++] = Unused;
	      else
		this->elt_sm[nextToWrite++] = Unused_sm;
	    }
	  }
	else
	  {
	    if(use_small)
	      {
		assert(this->elt_sm != 0);
		rd.readAll((char *) this->elt_sm,
			   sizeof(this->elt_sm[0]) * this->eltLen);
	      }
	    else
	      {
		assert(this->elt != 0);
		rd.readAll((char *) this->elt,
			   sizeof(this->elt[0]) * this->eltLen);
	      }
	  }

    } else {
      this->elt = 0;
      this->elt_sm = 0;
    }
}

Basics::uint32 IntIntTblLR::StoreEltLen() const throw()
{
  Basics::uint32 reduced_eltLen = this->eltLen;
  {
    int_type not_used;
    while((reduced_eltLen > this->numEntries) &&
	  !this->Get((reduced_eltLen-1), /*OUT*/ not_used))
      {
	reduced_eltLen--;
      }
  }
  assert(reduced_eltLen >= this->numEntries);

  return reduced_eltLen;
}

char IntIntTblLR::StoreFormat(Basics::uint32 reduced_eltLen,
			      /*OUT*/ bool &use_small,
			      /*OUT*/ bool &write_pairs)
  const throw()
{
  // Does this table have <50% occupancy?  If so, we're better off
  // storing key/value pairs.
  write_pairs = (this->numEntries < (reduced_eltLen / 2));

  // There are 4 on-disk formats:
  //
  // 1 : 16-bit integer array (high occupancy)
  //
  // 2 : 16-bit integer key/value pairs (low occupancy)
  //
  // 3 : 32-bit integer array (high occupancy)
  //
  // 4 : 32-bit integer  key/value pairs (low occupancy)

  char format_byte;
  if(write_pairs)
    {
      // We can use small format integers as long as both the
      // values and the keys will fit.
      use_small = ((this->elt == 0) &&
		   (is_small_num(reduced_eltLen, false)));

      format_byte = use_small ? 2 : 4;
    }
  else
    {
      // We use small format as long as we don't have a large
      // array.
      use_small = (this->elt == 0);

      format_byte = use_small ? 1 : 3;
    }

  return format_byte;
}

bool IntIntTblLR::DecodeStoreFormat(char format_byte,
				    /*OUT*/ bool &use_small,
				    /*OUT*/ bool &read_pairs)
  throw()
{
  // See the description of the formats above in
  // IntIntTblLR::StoreFormat
  switch(format_byte)
    {
    case 1:
      use_small = true;
      read_pairs = false;
      return true;
    case 2:
      use_small = true;
      read_pairs = true;
      return true;
    case 3:
      use_small = false;
      read_pairs = false;
      return true;
    case 4:
      use_small = false;
      read_pairs = true;
      return true;
    }
  return false;
}

void IntIntTblLR::Write(ostream &ofs, bool old_format) const
  throw (FS::Failure)
{
    Basics::uint32 reduced_eltLen = this->StoreEltLen();

    FS::Write(ofs, (char *)(&(this->numEntries)), sizeof(this->numEntries));
    FS::Write(ofs, (char *)(&(reduced_eltLen)), sizeof(reduced_eltLen));
    if (this->numEntries > 0) {
      // "write_pairs" determines whether we write a sequence of
      // key/value pairs or a whole array.
      //
      // "use_small" determines whether we use 16-bit integers or
      // 32-bit integers.
      bool write_pairs, use_small;

      if(old_format)
	{
	  // The caller is required to check that the table will fit
	  // in the old format before calling this function.  The
	  // "CanWriteOld" member function is provided to test this.
	  // An assertion failure may result otherwise.
	  write_pairs = true;
	  use_small = true;
	}
      else
	{
	  char format_byte = this->StoreFormat(reduced_eltLen,
					       use_small, write_pairs);
	  FS::Write(ofs, &format_byte, sizeof(format_byte));
	}

      if(write_pairs)
	{
	  Basics::uint32 numWritten = 0;
	  for (int_type k = 0; k < reduced_eltLen; k++)
	    {
	      int_type v;
	      if (this->Get(k, /*OUT*/ v))
		{
		  assert(v != Unused);
		  if(use_small)
		    {
		      assert(is_small_num(v));
		      assert(is_small_num(k, false));

		      int_type_sm k_sm = k, v_sm = v;
		      FS::Write(ofs, (char *)(&k_sm), sizeof(k_sm));
		      FS::Write(ofs, (char *)(&v_sm), sizeof(v_sm));
		    }
		  else
		    {
		      FS::Write(ofs, (char *)(&k), sizeof(k));
		      FS::Write(ofs, (char *)(&v), sizeof(v));
		    }
		  numWritten++;
		}
	    }
	  assert(numWritten == this->numEntries);
	}
      else
	{
	  if(use_small)
	    {
	      assert(this->elt_sm != 0);
	      FS::Write(ofs, (char *) this->elt_sm,
			sizeof(this->elt_sm[0]) * reduced_eltLen);
	    }
	  else
	    {
	      assert(this->elt != 0);
	      FS::Write(ofs, (char *) this->elt,
			sizeof(this->elt[0]) * reduced_eltLen);
	    }
	}
    }
}

bool IntIntTblLR::CanWriteOld() const throw ()
{
  if(this->elt != 0)
    {
      // We need to check the entire array for either keys or values
      // that are too large
      for (int_type k = 0; k < this->eltLen; k++)
	{
	  int_type v = this->elt[k];
	  if(v == Unused) continue;

	  if(!is_small_num(k, false))
	    // Key can't be stored in old format
	    return false;
	  if(!is_small_num(v))
	    // Value can't be stored in old format
	    return false;
	}
    }
  else if(this->eltLen > Unused_sm)
    {
      // The values are implicitly all in range, but we need to check
      // for any entries with keys that are too large
      int_type k = 1;
      k <<= 16;
      for (; k < this->eltLen; k++)
	{
	  if(this->elt_sm[k] != Unused_sm)
	    // Key can't be stored in old format
	    return false;
	}
    }

  return true;
}

void IntIntTblLR::Read(istream &ifs, bool old_format)
  throw (FS::EndOfFile, FS::Failure)
/* We must set any unused entries to "Unused". We could do this by setting all
   entries to "Unused" immediately after the array is allocated. But if the
   array is mostly full, that would mean writing most entries twice. Instead,
   we set entries to "Unused" between the used entries, and at the end of the
   loop. The correctness of this algorithm relies on the fact that the entries
   are written by the corresponding "Write" method in order of increasing key.
*/
{
    FS::Read(ifs, (char *)(&(this->numEntries)), sizeof(this->numEntries));
    FS::Read(ifs, (char *)(&(this->eltLen)), sizeof(this->eltLen));
    if(this->numEntries > this->eltLen)
      {
	// Invalid format!
	Text emsg =
	  Text::printf("invalid format: numEntries = %u > eltLen = %u",
		       (unsigned int) this->numEntries,
		       (unsigned int) this->eltLen);
	throw FS::Failure("IntIntTblLR::Read", emsg, 0);
      }
    if (this->numEntries > 0) {
        bool use_small, read_pairs;
	if(old_format)
	  {
	    use_small = true;
	    read_pairs = true;
	  }
	else
	  {
	    char format_byte;
	    FS::Read(ifs, &format_byte, sizeof(format_byte));
	    // This must match the format in the Write member function
	    if(!DecodeStoreFormat(format_byte, use_small, read_pairs))
	      {
		// Invalid format!
		Text emsg =
		  Text::printf("invalid format byte = %u",
			       (unsigned int) format_byte);
		throw FS::Failure("IntIntTblLR::Read", emsg, 0);
	      }
	  }
	if(use_small)
	  {
	    this->elt_sm = NEW_PTRFREE_ARRAY(int_type_sm, this->eltLen);
	    this->elt = 0;
	  }
	else
	  {
	    this->elt = NEW_PTRFREE_ARRAY(int_type, this->eltLen);
	    this->elt_sm = 0;
	  }
	if(read_pairs)
	  {
	    int_type nextToWrite = 0;
	    // Invariant: All elements of "this->elt"/"this->elt_sm"
	    // in the half-open interval "[0, nextToWrite)" have been
	    // written.
	    for (int_type i = 0; i < this->numEntries; i++) {
	      int_type k, v;
	      if(use_small)
		{
		  int_type_sm k_sm, v_sm;
		  FS::Read(ifs, (char *)(&k_sm), sizeof(k_sm));
		  FS::Read(ifs, (char *)(&v_sm), sizeof(v_sm));

		  if(v_sm == Unused_sm)
		    {
		      OBufStream msg;
		      msg << "reserved (unused) value read, k = "
			  << k_sm << ", v = " << v_sm;
		      throw FS::Failure("IntIntTblLR::Read", msg.str(), 0);
		    }

		  k = k_sm;
		  v = v_sm;
		}
	      else
		{
		  FS::Read(ifs, (char *)(&k), sizeof(k));
		  FS::Read(ifs, (char *)(&v), sizeof(v));

		  if(v == Unused)
		    {
		      OBufStream msg;
		      msg << "reserved (unused) value read, k = "
			  << k << ", v = " << v;
		      throw FS::Failure("IntIntTblLR::Read", msg.str(), 0);
		    }
		}

	      if(k >= this->eltLen)
		{
		  OBufStream msg;
		  msg << "key out of range, k = "
		      << k << ", eltLen = " << this->eltLen;
		  throw FS::Failure("IntIntTblLR::Read", msg.str(), 0);
		}

	      // make intervening entries unused
	      assert(k >= nextToWrite);
	      assert((this->elt != 0) || (this->elt_sm != 0));
	      while (nextToWrite < k) {
		if(this->elt != 0)
		  this->elt[nextToWrite++] = Unused;
		else
		  this->elt_sm[nextToWrite++] = Unused_sm;
	      }

	      if(this->elt != 0)
		this->elt[k] = v;
	      else
		{
		  if(!is_small_num(v))
		    {
		      OBufStream msg;
		      msg
			<< "large value read but using small storage, k = "
			<< k << ", v = " << v;
		      throw FS::Failure("IntIntTblLR::Read", msg.str(), 0);
		    }

		  this->elt_sm[k] = v;
		}

	      nextToWrite++; // == k+1
	    }

	    // make any remaining entries unused
	    while (nextToWrite < this->eltLen) {
	      if(this->elt != 0)
		this->elt[nextToWrite++] = Unused;
	      else
		this->elt_sm[nextToWrite++] = Unused_sm;
	    }
	  }
	else
	  {
	    if(use_small)
	      {
		assert(this->elt_sm != 0);
		FS::Read(ifs, (char *) this->elt_sm,
			 sizeof(this->elt_sm[0]) * this->eltLen);
	      }
	    else
	      {
		assert(this->elt != 0);
		FS::Read(ifs, (char *) this->elt,
			 sizeof(this->elt[0]) * this->eltLen);
	      }
	  }
    } else {
      this->elt = 0;
      this->elt_sm = 0;
    }
}

inline void Indent(ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << " ";
}

void IntIntTblLR::Print(ostream &os, int indent) const throw ()
{
    const int NumPerLine = 7;
    int cnt = 0;
    os << "{ ";
    for (int_type k = 0; cnt < this->numEntries; k++) {
	int_type v;
	if (this->Get(k, /*OUT*/ v)) {
	    if (cnt > 0) os << ", ";
	    if (cnt % NumPerLine == 0) {
		os << endl; Indent(os, indent);
	    }
	    os << k << "->" << v;
	    cnt++;
	}
    }
    os << " }" << endl;
}

bool IntIntTblLR::operator==(const IntIntTblLR &other) const throw ()
{
  if(this->numEntries != other.numEntries)
    return false;

  int_type k = 0;
  for(; k < this->eltLen; k++)
    {
      int_type v;
      if(this->Get(k, /*OUT*/ v))
	{
	  int_type v2;
	  if(!other.Get(k, /*OUT*/ v2))
	    return false;
	  if(v != v2)
	    return false;
	}
      else if(other.Get(k, /*OUT*/ v))
	{
	  // Entry in other table but not our table
	  return false;
	}
    }
  // This loop shouldn't do anything, but other.eltLen might be longer
  // than this->eltLen
  for(;k < other.eltLen; k++)
    {
      int_type v;
      if(other.Get(k, /*OUT*/ v))
	{
	  // Entry in other table but not our table
	  return false;
	}
    }
  return true;
}

bool IntIntTblIter::Next(/*OUT*/ int_type& k, /*OUT*/ int_type& v) throw ()
{
    bool res;
    if(this->done) return false;
    while (this->k < this->tbl->eltLen &&
	   !tbl->Get(this->k, /*OUT*/ v)) {
	(this->k)++;
    }
    if (this->k < this->tbl->eltLen) {
	k = this->k;
	(this->k)++;
	res = true;
    } else {
	this->done = true;
	res = false;
    }
    return res;
}
