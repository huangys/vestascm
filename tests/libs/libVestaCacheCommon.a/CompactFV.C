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

// Created on Mon Mar 30 11:38:16 PST 1998 by heydon

#include <Basics.H>
#include <SRPC.H>
#include "FV.H"
#include "PrefixTbl.H"
#include "BitVector.H"
#include "PrefixTbl.H"
#include "CompactFV.H"

using std::ostream;
using std::istream;
using std::endl;

// The largest index that can be stored in "idx_sm".
static const Basics::uint32 small_max = ((1 << 16) - 1);

void CompactFV::List::StoreIndex(Basics::uint32 pos, Basics::uint32 val)
{
  // If the value is too large for 16-bit and we're using 16-bit
  // indexes, switch to 32-bit
  if((val & ~small_max) && (this->idx_sm != 0))
    {
      assert(this->idx == 0);
      this->idx = NEW_PTRFREE_ARRAY(Basics::uint32, this->num);
      // Note: we assume that any indexes above "pos" are not yet
      // valid and therefore we don't copy them.
      for(Basics::uint32 i = 0; i < pos; i++)
	{
	  this->idx[i] = this->idx_sm[i];
	}
      delete [] this->idx_sm;
      this->idx_sm = 0;
    }

  if(this->idx != 0)
    {
      assert(this->idx_sm == 0);
      this->idx[pos] = val;
    }
  else
    {
      assert(this->idx_sm != 0);
      this->idx_sm[pos] = val;
      // We already checked for overflow, so this should be impossible
      assert(this->idx_sm[pos] == val);
    }
}

Basics::uint32 CompactFV::List::GetIndex(Basics::uint32 pos) const throw ()
{
  // At least one of these must be non-NULL for us to get an index.
  assert((this->idx != 0) ||
	 (this->idx_sm != 0));
  
  if(this->idx != 0)
    {
      assert(this->idx_sm == 0);
      return this->idx[pos];
    }
  else
    {
      assert(this->idx_sm != 0);
      return this->idx_sm[pos];
    }
}

CompactFV::List::List(const FV::List &names) throw (PrefixTbl::Overflow)
  : num(names.len), tbl(names.len + 5),
    idx(0), idx_sm(0), types(0)
{
    // Check for overflow in num
    assert(((unsigned long) num) == names.len);
    if (names.len > 0) {
        // We always start out using small indexes.  We'll transition
        // to larger ones if needed as we add entries.  (We can't tell
        // how many entires the PrefixTbl will get from the number of
        // free variables, as there could be repeated paths.)
        this->idx_sm =  NEW_PTRFREE_ARRAY(Basics::uint16, names.len);

	this->types = NEW_PTRFREE_ARRAY(char, names.len);
	PrefixTbl::PutTbl
	  putTbl(/*sizeHint=*/ names.len << 1, /*useGC=*/ true);
	for (int i = 0; i < names.len; i++) {
	    const char *str = names.name[i].cchars();

	    // strip off name type code (a single character)
	    assert(str[0] != '\0' && str[1] == '/');
	    this->types[i] = str[0];

	    // add rest of path to table
	    this->StoreIndex(i, this->tbl.Put(str+2, /*INOUT*/ putTbl));
	}
    }
}

void CompactFV::List::ToFVList(/*INOUT*/ FV::ListApp &fvl) const throw ()
{
    const int BuffLen = 100;
    char buff[BuffLen + 2];

    char *str = buff;
    int strLen = BuffLen;
    str[1] = '/';
    for (int i = 0; i < this->num; i++) {
        while (! this->tbl.GetString(this->GetIndex(i), str+2, strLen)) {
	    // allocate a new, larger buffer and try again
	    strLen <<= 1;
	    str = NEW_PTRFREE_ARRAY(char, strLen + 2);
	    str[1] = '/';
	}
	str[0] = this->types[i];
	int ix = fvl.Append(FV::T(str)); assert(ix == i);
    }
}

void CompactFV::List::Write(ostream &ofs, bool old_format)
  const throw (FS::Failure)
{
    this->tbl.Write(ofs, old_format);
    if(old_format)
      {
	Basics::uint16 disk_num = this->num;
	// We don't mind asserting here because this functionality is
	// only used by test programs, not during normal operation.
	assert(((Basics::uint32) disk_num) == this->num);
	FS::Write(ofs, (char *)(&disk_num), sizeof(disk_num));
      }
    else
      {
	FS::Write(ofs, (char *)(&(this->num)), sizeof(this->num));
      }
    if (this->num > 0) {
	FS::Write(ofs, this->types, this->num * sizeof(this->types[0]));
	bool use_small = true;
	if(!old_format)
	  {
	    // Write 16-bit/32-bit choice
	    use_small = (this->idx_sm != 0);
	    char use_small_byte = use_small ? 1 : 0;
	    FS::Write(ofs, &use_small_byte, sizeof(char));
	  }

	if(use_small)
	  {
	    assert(this->idx_sm != 0);
	    FS::Write(ofs, (char *)(this->idx_sm), this->num * sizeof(this->idx_sm[0]));
	  }
	else
	  {
	    assert(this->idx != 0);
	    FS::Write(ofs, (char *)(this->idx), this->num * sizeof(this->idx[0]));
	  }
    }
}

void CompactFV::List::Read(istream &ifs, bool old_format)
  throw (FS::EndOfFile, FS::Failure)
{
    this->tbl.Read(ifs, old_format);
    bool use_small = true;
    if(old_format)
      {
	Basics::uint16 disk_num;
	FS::Read(ifs, (char *)(&disk_num), sizeof(disk_num));
	this->num = disk_num;
	use_small = true;
      }
    else
      {
	FS::Read(ifs, (char *)(&(this->num)), sizeof(this->num));
      }
    if (this->num > 0) {
	this->types = NEW_PTRFREE_ARRAY(char, this->num);
	FS::Read(ifs, this->types, this->num * sizeof(this->types[0]));
	if(!old_format)
	  {
	    // Read 16-bit/32-bit choice
	    char use_small_byte;
	    FS::Read(ifs, &use_small_byte, sizeof(char));
	    use_small = (use_small_byte != 0);
	  }

	if(use_small)
	  {
	    this->idx_sm = NEW_PTRFREE_ARRAY(Basics::uint16, this->num);
	    this->idx = 0;
	    FS::Read(ifs, (char *)(this->idx_sm), this->num * sizeof(this->idx_sm[0]));
	  }
	else
	  {
	    this->idx = NEW_PTRFREE_ARRAY(Basics::uint32, this->num);
	    this->idx_sm = 0;
	    FS::Read(ifs, (char *)(this->idx), this->num * sizeof(this->idx[0]));
	  }
    } else {
	this->types = 0;
	this->idx = 0;
	this->idx_sm = 0;
    }
}

void CompactFV::List::Send(SRPC &srpc, bool old_protocol) const throw (SRPC::failure)
{
    this->tbl.Send(srpc, old_protocol);
    // Note that we call this even if this->num is zero and
    // this->types is a NULL pointer.  It's used to implicitly
    // transmit this->num.
    srpc.send_bytes((const char *)(this->types), this->num);
    if(this->num > 0)
      {
	if(!old_protocol)
	  {
	    // Indicate to the client whether we'll be sending small
	    // indexes
	    srpc.send_bool(this->idx_sm != 0);
	  }
	else
	  {
	    // We should only be in 32-bit mode if the this->tbl is,
	    // and in that case it will have already sent a failure.
	    // So really we shouldn't need to check whether
	    // "this->idx" is non-NULL, but it's a cheap test.

	    // We do need to check whether "this->num" fits in a
	    // 16-bit integer though, as we know old clients will
	    // expect it to fit in one.
	    
	    Basics::uint16 receiver_num = this->num;
	    if((this->idx != 0) ||
	       (((Basics::uint32) receiver_num) != this->num))
	      {
		srpc.send_failure(SRPC::not_implemented,
				  "CompactFV::List too large for old protocol; "
				  "upgrade client software");
	      }
	  }
      }

    if((this->num > 0) && (this->idx != 0))
      {
	assert(!old_protocol);
	srpc.send_int32_array((Basics::int32 *) this->idx, this->num);
      }
    else if(((this->idx_sm != 0) && (this->num > 0)) || old_protocol)
      {
	// Note that we call this even when this->num is zero and
	// this->idx_sm is a NULL pointer for the old protocol.
	srpc.send_int16_array((Basics::int16 *) this->idx_sm, this->num);
      }
}

void CompactFV::List::Recv(SRPC &srpc, bool old_protocol) throw (SRPC::failure)
/* Note: The wire protocol of this method must agree with that of
   "FV::List::Send" in "FV.C". */
{
    this->tbl.Recv(srpc, old_protocol);
    int len = 0, len2 = 0;
    this->types = srpc.recv_bytes(/*OUT*/ len);
    this->num = len;
    if(this->num > 0)
      {
	bool use_small = true;
	if(!old_protocol)
	  {
	    use_small = srpc.recv_bool();
	  }

	if(use_small)
	  {
	    this->idx_sm = (Basics::uint16 *) srpc.recv_int16_array(/*OUT*/ len2);
	    this->idx = 0;
	  }
	else
	  {
	    this->idx = (Basics::uint32 *) srpc.recv_int32_array(/*OUT*/ len2);
	    this->idx_sm = 0;
	  }
      }
    else if(old_protocol)
      {
	// The old protocol sends an empty array even when the length
	// is zero.
	this->idx_sm = (Basics::uint16 *) srpc.recv_int16_array(/*OUT*/ len2);
	this->idx = 0;
      }
    if(len != len2)
      {
	// This should never happen, unless of course the client
	// is broken or malicious
	Text msg =
	  Text::printf("CompactFV::List has inconsistent length: "
		       "%d types, %d indexes", len, len2);
	srpc.send_failure(SRPC::protocol_violation, msg);
      }
}

inline void Indent(ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << " ";
}

void CompactFV::List::Print(ostream &os, int indent) const throw ()
{
    for (int i = 0; i < this->num; i++) {
	Indent(os, indent);
	FV2::T *nm = this->tbl.Get(this->GetIndex(i));
	os << this->types[i] << '/' << *nm << endl;
    }
}

bool CompactFV::List::operator==(const CompactFV::List &other) const throw()
{
  if(this->num != other.num)
    return false;
  if(memcmp(this->types, other.types, this->num) != 0)
    return false;
  if(this->tbl != other.tbl)
    return false;
  for(Basics::uint32 i = 0; i < this->num; i++)
    {
      if(this->GetIndex(i) != other.GetIndex(i))
	return false;
    }
  return true;
}
