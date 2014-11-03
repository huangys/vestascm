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
#include <Generics.H>
#include <SRPC.H>
#include "TextIO.H"
#include "FV2.H"
#include "PrefixTbl.H"

#include <iomanip>

using std::ostream;
using std::istream;
using std::ios;
using std::setw;
using std::endl;

const Basics::uint32 PrefixTbl::endMarker = ~0;
const Basics::uint16 PrefixTbl::endMarker_sm = ~0;
const Basics::uint32 PrefixTbl::small_max = ((((Basics::uint32) 1) << 16) - 1);

// Expected number of arcs in an FV2::T
static const int ArcCountHint = 4;

PrefixTbl::PrefixTbl(Basics::uint32 szHint) throw ()
  : numArcs(0), maxArcs(0),
    indexArray(0), indexArray_sm(0), arcArray(0)
{
    if (szHint > 0) {
      if(szHint > PrefixTbl::small_max)
	{
	  // Clamp szHint to the 16-bit limit, for two reasons:

	  // 1. It's only a hint, and the higher the hint is the
	  // higher the probable difference between the hint and the
	  // actual size.

	  // 2. We're not allowed to use a 32-bit array until we
	  // actually cross that limit.
	  szHint = PrefixTbl::small_max;
	}
      maxArcs = szHint;

      this->indexArray_sm = NEW_PTRFREE_ARRAY(Basics::uint16, maxArcs);

      this->arcArray = NEW_ARRAY(Text, maxArcs);
    }
}

Basics::uint32 PrefixTbl::AddArc(const Text& arc) throw (PrefixTbl::Overflow)
{
    if(this->maxArcs == 0)
      {
	assert(this->numArcs == 0);
	assert(this->indexArray == 0);
	assert(this->indexArray_sm == 0);
	assert(this->arcArray == 0);

	// Arbitrary initial size, since nobody gave us a hint.
	this->maxArcs = 100;
	this->indexArray_sm = NEW_PTRFREE_ARRAY(Basics::uint16, this->maxArcs);
	this->arcArray = NEW_ARRAY(Text, this->maxArcs);
      }

    Basics::uint32 res = this->numArcs;
    assert(this->maxArcs >= this->numArcs);
    if (this->numArcs == this->maxArcs) {
      // We've run out of space and need to allocate more
      if(this->maxArcs < PrefixTbl::small_max)
	{
	  assert(this->indexArray == 0);
	  assert(this->indexArray_sm != 0);

	  // Double if possible, but increase by at least two.  If
	  // that overflows the maximum small size, go to the maximum
	  // small size.
	  Basics::uint16 new_max = this->maxArcs + max(this->maxArcs, 2);
	  if(new_max <= this->maxArcs) new_max = PrefixTbl::small_max;

	  this->maxArcs = new_max;
	  assert(this->maxArcs > this->numArcs);
	  Basics::uint16 *newIndexArray =
	    NEW_PTRFREE_ARRAY(Basics::uint16, this->maxArcs);
	  Text *newArcArray = NEW_ARRAY(Text, this->maxArcs);
	  for (Basics::uint32 i = 0; i < this->numArcs; i++) {
	    newIndexArray[i] = this->indexArray_sm[i];
	    newArcArray[i] = this->arcArray[i];
	  }
	  delete [] this->indexArray_sm;
	  delete [] this->arcArray;
	  this->indexArray_sm = newIndexArray;
	  this->arcArray = newArcArray;
	}
      else if (this->indexArray_sm != 0)
	{
	  // We need to switch from 16-bit to 32-bit.
	  assert(this->indexArray == 0);
	  assert(this->maxArcs == PrefixTbl::small_max);

	  // Allocate 1/4 again as many as we already have. (We
	  // clearly have a lot, so doubling is probably overkill.)
	  this->maxArcs += (this->maxArcs >> 2);
	  assert(this->maxArcs > this->numArcs);

	  Basics::uint32 *newIndexArray =
	    NEW_PTRFREE_ARRAY(Basics::uint32, this->maxArcs);
	  Text *newArcArray = NEW_ARRAY(Text, this->maxArcs);
	  for (Basics::uint32 i = 0; i < this->numArcs; i++) {
	    newIndexArray[i] = 
	      ((this->indexArray_sm[i] == PrefixTbl::endMarker_sm)
	       ? PrefixTbl::endMarker
	       : this->indexArray_sm[i]);
	    newArcArray[i] = this->arcArray[i];
	  }
	  delete [] this->indexArray_sm;
	  delete [] this->arcArray;
	  this->indexArray = newIndexArray;
	  this->indexArray_sm = 0;
	  this->arcArray = newArcArray;
	}
      else
	{
	  // We're already in 32-bit
	  assert(this->indexArray != 0);
	  assert(this->indexArray_sm == 0);
	  assert(this->maxArcs > PrefixTbl::small_max);

	  // By default, allocate 1/8 again as many as we already
	  // have.  If we're getting close to the 32-bit limit,
	  // increment by smaller amounts.
	  {
	    Basics::uint32 arc_incr = (this->maxArcs >> 3);
	    while((this->maxArcs <= this->numArcs) &&
		  (arc_incr > 0))
	      {
		this->maxArcs = this->numArcs + arc_incr;
		arc_incr >>= 1;
	      }
	  }
	  if(this->maxArcs <= this->numArcs)
	    {
	      // 32-bit overflow
	      throw PrefixTbl::Overflow(this->numArcs);
	    }

	  Basics::uint32 *newIndexArray =
	    NEW_PTRFREE_ARRAY(Basics::uint32, this->maxArcs);
	  Text *newArcArray = NEW_ARRAY(Text, this->maxArcs);
	  for (Basics::uint32 i = 0; i < this->numArcs; i++) {
	    newIndexArray[i] = this->indexArray[i];
	    newArcArray[i] = this->arcArray[i];
	  }
	  delete [] this->indexArray;
	  delete [] this->arcArray;
	  this->indexArray = newIndexArray;
	  this->arcArray = newArcArray;
	}
    }
    assert(this->numArcs < this->maxArcs);
    this->arcArray[this->numArcs++] = arc;
    return res;
}

void PrefixTbl::StoreIndex(Basics::uint32 pos, Basics::uint32 val)
  throw()
{
  assert(pos < this->numArcs);
  assert(val != PrefixTbl::endMarker);
  if(this->indexArray_sm != 0)
    {
      assert(this->indexArray == 0);
      assert(this->numArcs <= PrefixTbl::small_max);
      this->indexArray_sm[pos] = val;
      assert(((Basics::uint32) this->indexArray_sm[pos]) == val);
    }
  else
    {
      assert(this->indexArray != 0);
      this->indexArray[pos] = val;
    }
}

void PrefixTbl::StoreEndMarker(Basics::uint32 pos)
  throw()
{
  assert(pos < this->numArcs);
  if(this->indexArray_sm != 0)
    {
      assert(this->indexArray == 0);
      assert(this->numArcs <= PrefixTbl::small_max);
      this->indexArray_sm[pos] = PrefixTbl::endMarker_sm;
    }
  else
    {
      assert(this->indexArray != 0);
      this->indexArray[pos] = PrefixTbl::endMarker;
    }
}

Basics::uint32 PrefixTbl::Put(const char *path,
			      PrefixTbl::PutTbl /*INOUT*/ &tbl)
  throw (PrefixTbl::Overflow)
{
    Basics::uint32 resIdx;
    if (!tbl.Get(Text(path, /*copy=*/ (void*)1), /*OUT*/ resIdx)) {
	// local buffer so allocation can be avoided in most cases
	const int BuffSz = 100;
	char buff[BuffSz + 1];

	// make a mutable copy of "path" in "str"
	int pathLen = strlen(path);
	char *str = (pathLen <= BuffSz)
	  ? buff
	  : (NEW_PTRFREE_ARRAY(char, pathLen + 1));
	memcpy(str, path, pathLen + 1);    // copy '\0' too

	char *curr = str + (pathLen - 1);
	while (curr >= str && *curr != '/') curr--;
	resIdx = this->AddArc(Text(curr + 1));
        bool inTbl = tbl.Put(Text(str), resIdx); assert(!inTbl);
	Basics::uint32 lastIdx = resIdx;
	while (curr >= str) {
	    assert(*curr == '/');
	    *curr = '\0';
	    Basics::uint32 currIdx;
	    if (tbl.Get(Text(str, /*copy=*/ (void*)1), /*OUT*/ currIdx)) {
	        this->StoreIndex(lastIdx, currIdx);
		return resIdx;
	    } else {
		while (--curr >= str && *curr != '/') /*SKIP*/;
		currIdx = this->AddArc(Text(curr + 1));
		inTbl = tbl.Put(Text(str), currIdx); assert(!inTbl);
	        this->StoreIndex(lastIdx, currIdx);
		lastIdx = currIdx;
	    }
	}
	this->StoreEndMarker(lastIdx);
    }
    return resIdx;
}

Basics::uint32 PrefixTbl::Put(const FV2::T& ts,
			      PrefixTbl::PutTbl /*INOUT*/ &tbl)
  throw (PrefixTbl::Overflow)
{
    if(ts.size() == 0) return PrefixTbl::endMarker;
    Basics::uint32 arcIdx = ts.size() - 1;

    Basics::uint32 resIdx;
    char *str = ts.ToText().chars();
    int strPos = (int) strlen(str);
    if (!tbl.Get(Text(str, /*copy=*/ (void*)1), /*OUT*/ resIdx)) {
	resIdx = this->AddArc(ts.get(arcIdx));
        bool inTbl = tbl.Put(Text(str), resIdx); assert(!inTbl);
	Basics::uint32 lastIdx = resIdx;
	while (arcIdx-- > 0) {
	    while (str[--strPos] != '/') /*SKIP*/;
	    str[strPos] = '\0';
	    Basics::uint32 currIdx;
	    if (tbl.Get(Text(str, /*copy=*/ (void*)1), /*OUT*/ currIdx)) {
	        this->StoreIndex(lastIdx, currIdx);
		return resIdx;
	    } else {
		currIdx = this->AddArc(ts.get(arcIdx));
		inTbl = tbl.Put(Text(str), currIdx); assert(!inTbl);
	        this->StoreIndex(lastIdx, currIdx);
		lastIdx = currIdx;
	    }
	}
	this->StoreEndMarker(lastIdx);
    }
    return resIdx;
}

FV2::T *PrefixTbl::Get(Basics::uint32 idx) const throw ()
{
    if(idx >= this->numArcs)
      {
	// Invalid index
	return 0;
      }
    FV2::T *path = NEW_CONSTR(FV2::T, (ArcCountHint));
    register Basics::uint32 index = idx;
    while (index != PrefixTbl::endMarker) {
	assert(0 <= index  && index < this->numArcs);
	path->addlo(this->arcArray[index]);
	index = this->PrefixIndex(index);
    }
    return path;
}

bool PrefixTbl::GetString(Basics::uint32 idx, char *buff, int buff_len)
  const throw ()
/* This method constructs the string "backwards", starting at the end of the
   buffer and prepending arcs (just as the "Get" method above uses "addlo" to 
   prepend each arc). */
{
    // The caller must give us a valid index.  
    assert(idx < this->numArcs);

    // first, build list of arc indices
    Sequence<Basics::uint32, true> indexList(/*sizeHint = */ 20);
    register Basics::uint32 index = idx;
    while (index != PrefixTbl::endMarker) {
      indexList.addlo(index);
      index = this->PrefixIndex(index);
    }

    // second, write arcs into "buff"
    register char *curr = buff;
    buff_len--; // subtract one for required null terminator
    while (indexList.size() > 0) {
        index = indexList.remlo();
	assert(index < this->numArcs);
	const Text &txt = this->arcArray[index];
	int arcLen = txt.Length();
	if (arcLen > buff_len) return false; // not enough space
	(void) memcpy(curr, txt.cchars(), arcLen);
	curr += arcLen;
	*curr++ = '/';
	buff_len -= (arcLen + 1);
    }

    // replace final '/' by null and return
    *(curr - 1) = '\0';
    return true;
}

Basics::uint64 PrefixTbl::MemorySize() const throw ()
{
    register Basics::uint64 res = sizeof(PrefixTbl);
    if(this->indexArray_sm != 0)
      {
	res += this->maxArcs * sizeof(this->indexArray_sm[0]);
      }
    else if(this->indexArray != 0)
      {
	res += this->maxArcs * sizeof(this->indexArray[0]);
      }
    if(this->arcArray != 0)
      {
	res += this->maxArcs * sizeof(this->arcArray[0]);
      }
    for (Basics::uint32 i = 0; i < this->numArcs; i++) {
	res += this->arcArray[i].Length() + 1;
    }
    return res;
}

Basics::uint64 PrefixTbl::Skip(istream &ifs, bool old_format)
  throw (FS::EndOfFile, FS::Failure)
{
    Basics::uint64 res = 0;
    Basics::uint32 numArcs;
    Basics::uint32 idx_size;
    if(old_format)
      {
	Basics::uint16 disk_numArcs;
	FS::Read(ifs, (char *)(&(disk_numArcs)), sizeof(disk_numArcs));
	numArcs = disk_numArcs;
	res += sizeof(disk_numArcs);
	idx_size = sizeof(Basics::uint16);
      }
    else
      {
	FS::Read(ifs, (char *)(&numArcs), sizeof(numArcs));
	res += sizeof(numArcs);
	// The size of the encoded index depends on the number of entries
	idx_size = ((numArcs > PrefixTbl::small_max)
		    ? sizeof(Basics::uint32)
		    : sizeof(Basics::uint16));
      }
    for (Basics::uint32 i = 0; i < numArcs; i++) {
      FS::Seek(ifs, idx_size, ios::cur);  // skip the index
      res += idx_size;
      res += TextIO::Skip(ifs);
    }
    return res;
}

void PrefixTbl::Write(ostream &ofs, bool old_format)
  const throw (FS::Failure)
{
  // First write the length
  if(old_format) {
    // This format only works if we have less than 2^16 entries
    assert(this->numArcs <= PrefixTbl::small_max);
    assert((this->numArcs == 0) || (this->indexArray_sm != 0));
    assert(this->indexArray == 0);

    Basics::uint16 disk_numArcs = this->numArcs;
    assert(this->numArcs == ((Basics::uint32) disk_numArcs));

    FS::Write(ofs, (char *)(&(disk_numArcs)), sizeof(disk_numArcs));
  } else {
    FS::Write(ofs, (char *)(&(this->numArcs)), sizeof(this->numArcs));
    // If we're using 32-bit indexes, we must have more than 2^16.
    // Anyone reading this from disk will decide how to read each
    // encoded integer based on numArcs.
    assert((this->indexArray == 0) ||
	   (this->numArcs > PrefixTbl::small_max));
  }

  // Now write the entries
  for (Basics::uint32 i = 0; i < this->numArcs; i++) {
    if(this->indexArray == 0)
      {
	assert(this->indexArray_sm != 0);
	Basics::uint16 *idx = &(this->indexArray_sm[i]);
	assert((*idx == PrefixTbl::endMarker_sm) ||
	       (*idx < this->numArcs));
	FS::Write(ofs, (char *)(idx), sizeof(*idx));
      }
    else
      {
	Basics::uint32 *idx = &(this->indexArray[i]);
	assert((*idx == PrefixTbl::endMarker) ||
	       (*idx < this->numArcs));
	FS::Write(ofs, (char *)(idx), sizeof(*idx));
      }
    TextIO::Write(ofs, this->arcArray[i]);
  }
}

void PrefixTbl::Read(istream &ifs, bool old_format)
  throw (FS::EndOfFile, FS::Failure)
{
  // First read the length and allocate the arrays
  if(old_format) {
    Basics::uint16 disk_numArcs;
    FS::Read(ifs, (char *)(&(disk_numArcs)), sizeof(disk_numArcs));
    this->numArcs = disk_numArcs;
  } else {
    FS::Read(ifs, (char *)(&(this->numArcs)), sizeof(this->numArcs));
  }
  this->maxArcs = this->numArcs;
  if (this->numArcs > 0) {
    if(this->numArcs > PrefixTbl::small_max)
      {
	this->indexArray = NEW_PTRFREE_ARRAY(Basics::uint32, this->numArcs);
	this->indexArray_sm = 0;
      }
    else
      {
	this->indexArray_sm = NEW_PTRFREE_ARRAY(Basics::uint16, this->numArcs);
	this->indexArray = 0;
      }
    this->arcArray = NEW_ARRAY(Text, this->numArcs);
  } else {
    this->indexArray = 0;
    this->indexArray_sm = 0;
    this->arcArray = 0;
  }

  // Now read the entries
  for (Basics::uint32 i = 0; i < this->numArcs; i++) {
    if(this->indexArray == 0)
      {
	assert(this->indexArray_sm != 0);
	Basics::uint16 *idx = &(this->indexArray_sm[i]);
	FS::Read(ifs, (char *)(idx), sizeof(*idx));
	if((*idx >= this->numArcs) &&
	   (*idx != PrefixTbl::endMarker_sm))
	  {
	    Text emsg =
	      Text::printf("invalid index %u",
			   (unsigned int) *idx);
	    throw FS::Failure("PrefixTbl::Read", emsg, 0);
	  }
      }
    else
      {
	Basics::uint32 *idx = &(this->indexArray[i]);
	FS::Read(ifs, (char *)(idx), sizeof(*idx));
	if((*idx >= this->numArcs) &&
	   (*idx != PrefixTbl::endMarker))
	  {
	    Text emsg =
	      Text::printf("invalid index %u",
			   (unsigned int) *idx);
	    throw FS::Failure("PrefixTbl::Read", emsg, 0);
	  }
      }
    TextIO::Read(ifs, /*OUT*/ this->arcArray[i]);
  }
}

void PrefixTbl::Log(VestaLog &log, bool old_format)
  const throw (VestaLog::Error)
{
    // First write the length
    if(old_format) {
      // This format only works if we have less than 2^16 entries
      assert(this->numArcs <= PrefixTbl::small_max);
      assert((this->numArcs == 0) || (this->indexArray_sm != 0));
      assert(this->indexArray == 0);

      Basics::uint16 disk_numArcs = this->numArcs;
      assert(this->numArcs == ((Basics::uint32) disk_numArcs));

      log.write((char *)(&disk_numArcs), sizeof(disk_numArcs));
    } else {
      log.write((char *)(&(this->numArcs)), sizeof(this->numArcs));
      // If we're using 32-bit indexes, we must have more than 2^16.
      // Anyone reading this from disk will decide how to read each
      // encoded integer based on numArcs.
      assert((this->indexArray == 0) ||
	     (this->numArcs > PrefixTbl::small_max));
    }

    // Now write the entries
    for (Basics::uint32 i = 0; i < this->numArcs; i++) {
      if(this->indexArray == 0)
	{
	  assert(this->indexArray_sm != 0);
	  Basics::uint16 *idx = &(this->indexArray_sm[i]);
	  assert((*idx == PrefixTbl::endMarker_sm) ||
		 (*idx < this->numArcs));
	  log.write((char *)(idx), sizeof(*idx));
	}
      else
	{
	  Basics::uint32 *idx = &(this->indexArray[i]);
	  assert((*idx == PrefixTbl::endMarker) ||
		 (*idx < this->numArcs));
	  log.write((char *)(idx), sizeof(*idx));
	}
      TextIO::Log(log, this->arcArray[i]);
    }
}

void PrefixTbl::Recover(RecoveryReader &rd, bool old_format)
  throw (VestaLog::Error, VestaLog::Eof)
{
    if(old_format) {
      Basics::uint16 disk_numArcs;
      rd.readAll((char *)(&disk_numArcs), sizeof(disk_numArcs));
      this->numArcs = disk_numArcs;
    } else {
      rd.readAll((char *)(&(this->numArcs)), sizeof(this->numArcs));
    }
    this->maxArcs = this->numArcs;
    if (this->numArcs > 0) {
      if(this->numArcs > PrefixTbl::small_max)
	{
	  this->indexArray = NEW_PTRFREE_ARRAY(Basics::uint32, this->numArcs);
	  this->indexArray_sm = 0;
	}
      else
	{
	  this->indexArray_sm = NEW_PTRFREE_ARRAY(Basics::uint16, this->numArcs);
	  this->indexArray = 0;
	}
      this->arcArray = NEW_ARRAY(Text, this->numArcs);
    } else {
      this->indexArray = 0;
      this->indexArray_sm = 0;
      this->arcArray = 0;
    }

    for (Basics::uint32 i = 0; i < this->numArcs; i++) {
      if(this->indexArray == 0)
	{
	  assert(this->indexArray_sm != 0);
	  Basics::uint16 *idx = &(this->indexArray_sm[i]);
	  rd.readAll((char *)(idx), sizeof(*idx));
	  if((*idx >= this->numArcs) &&
	     (*idx != PrefixTbl::endMarker_sm))
	    {
	      Text emsg =
		Text::printf("PrefixTbl::Recover: invalid index %u",
			     (unsigned int) *idx);
	      throw VestaLog::Error(0, emsg);
	    }
	}
      else
	{
	  Basics::uint32 *idx = &(this->indexArray[i]);
	  rd.readAll((char *)(idx), sizeof(*idx));
	  if((*idx >= this->numArcs) &&
	     (*idx != PrefixTbl::endMarker))
	    {
	      Text emsg =
		Text::printf("PrefixTbl::Recover: invalid index %u",
			     (unsigned int) *idx);
	      throw VestaLog::Error(0, emsg);
	    }
	}
      TextIO::Recover(rd, /*OUT*/ this->arcArray[i]);
    }
}

void PrefixTbl::Send(SRPC &srpc, bool old_protocol)
  const throw (SRPC::failure)
{
    srpc.send_int((int)(this->numArcs));
    if (this->numArcs > 0) {
      if(this->numArcs <= PrefixTbl::small_max)
	{
	  assert(this->indexArray_sm != 0);
	  srpc.send_int16_array((Basics::int16 *) this->indexArray_sm,
				this->numArcs);
	}
      else
	{
	  if(old_protocol)
	    {
	      srpc.send_failure(SRPC::not_implemented,
				"PrefixTbl too large for old protocol; "
				"upgrade client software");
	    }
	  assert(this->indexArray != 0);
	  srpc.send_int32_array((Basics::int32 *) this->indexArray,
				this->numArcs);
	}
      for (Basics::uint32 i = 0; i < this->numArcs; i++) {
	srpc.send_Text(this->arcArray[i]);
      }
    }
}

void PrefixTbl::Recv(SRPC &srpc, bool old_protocol)
  throw (SRPC::failure)
{
    this->numArcs = (Basics::uint32) srpc.recv_int();
    this->maxArcs = this->numArcs;
    if (this->numArcs > 0) {
      int dummyLen = 0;
      if(this->numArcs > PrefixTbl::small_max)
	{
	  if(old_protocol)
	    {
	      srpc.send_failure(SRPC::not_implemented,
				"PrefixTbl too large for old protocol; "
				"upgrade client software");
	    }
	  this->indexArray =
	    (Basics::uint32 *) srpc.recv_int32_array(/*OUT*/ dummyLen);
	  this->indexArray_sm = 0;
	}
      else
	{
	  this->indexArray_sm =
	    (Basics::uint16 *) srpc.recv_int16_array(/*OUT*/ dummyLen);
	  this->indexArray = 0;
	}
      if(dummyLen != this->numArcs)
	{
	  // This should never happen, unless of course the client
	  // is broken or malicious
	  Text msg =
	    Text::printf("PrefixTbl has inconsistent length: "
			 "%d name arcs, %d indexes", this->numArcs, dummyLen);
	  srpc.send_failure(SRPC::protocol_violation, msg);
	}
      this->arcArray = NEW_ARRAY(Text, this->numArcs);
      for (Basics::uint32 i = 0; i < this->numArcs; i++) {
	srpc.recv_Text(/*OUT*/ this->arcArray[i]);
      }
    } else {
      this->indexArray = 0;
      this->indexArray_sm = 0;
      this->arcArray = 0;
    }
}

inline void Indent(ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << " ";
}

void PrefixTbl::Print(ostream &os, int indent) const throw ()
{
    Indent(os, indent); os << "Index  Prefix  Arc" << endl;
    for (Basics::uint32 i = 0; i < this->numArcs; i++) {
	Indent(os, indent);
	os << setw(4) << i;
	os << setw(7);
	Basics::uint32 prefix = this->PrefixIndex(i);
	if(prefix == PrefixTbl::endMarker)
	  {
	    os << "<END>";
	  }
	else
	  {
	    os << prefix;
	  }
	os << "    ";
        os << this->arcArray[i] << endl;
    }
}

bool PrefixTbl::operator==(const PrefixTbl &other) const throw()
{
  if(this->numArcs != other.numArcs) return false;

  for(Basics::uint32 i = 0; i < this->numArcs; i++)
    {
      if(this->PrefixIndex(i) != other.PrefixIndex(i))
	return false;
      if(this->arcArray[i] != other.arcArray[i])
	return false;
    }
  // No mismatches, so they're equal
  return true;
}
