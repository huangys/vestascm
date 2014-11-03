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
#include <FP.H>
#include <PKEpoch.H>
#include <FV.H>
#include <TextIO.H>

#include "CacheEntry.H"
#include "CacheLog.H"

using std::ostream;
using std::endl;

void CacheLog::Entry::Init(const Text& sourceFunc, FP::Tag *pk,
			   PKFile::Epoch pkEpoch,
			   CacheEntry::Index ci, VestaVal::T *value,
			   Model::T model, CacheEntry::Indices *kids,
			   FV::List *names, FP::List *fps)
  throw ()
{
    this->sourceFunc = sourceFunc;
    this->pk = pk;
    this->pkEpoch = pkEpoch;

    this->ci = ci;
    this->value = value;
    this->model = model;
    this->kids = kids;
    this->names = names;
    this->fps = fps;
    assert(names->len == fps->len);

    this->next = (Entry *)NULL;
}

void CacheLog::Entry::Log(VestaLog &log) const
  throw (VestaLog::Error)
{
  // First we store a format version marker
  {
    // This is a little convoluted, but ensures that we won't mistake
    // an old cache log entry that lacks an embedded format version
    // for a new one with an embedded format version.

    // First we store a 16-bit integer equal to 3.  This would be the
    // length of the sourceFunc in the old format.  Next we store a
    // single null byte.  The bytes making up the sourceFunc string
    // will never contain null bytes, so this would be impossible in
    // the old format.  Finally, we store a format version in another
    // 16-bit integer.
    Basics::uint16 old_sourceFunc_len = 3;
    log.write((char *)&old_sourceFunc_len, sizeof(old_sourceFunc_len));
    char null_byte = 0;
    log.write(&null_byte, 1);
    Basics::uint16 logged_version = CacheLog::CurrentVersion;
    log.write((char *)(&logged_version), sizeof(logged_version));
  }
  
    TextIO::Log(log, this->sourceFunc);
    this->pk->Log(log);
    log.write((char *)(&(this->pkEpoch)), sizeof(this->pkEpoch));

    log.write((char *)(&(this->ci)), sizeof(this->ci));
    this->value->Log(log);
    this->kids->Log(log);
    Model::Log(this->model, log);

    this->names->Log(log);
    this->fps->Log(log);
}

void CacheLog::Entry::Recover(RecoveryReader &rd)
  throw (VestaLog::Error, VestaLog::Eof)
{
  // Start by reading in the embedded log entry format version.  (See
  // also the comments above in the "Log" member function.)
  int version;
  // First is a 16-bit integer which will be exactly 3 for an entry
  // with an embedded format version, or will be the length of the
  // sourceFunc text for an old log entry.
  Basics::uint16 old_sourceFunc_len;
  rd.readAll((char *)(&old_sourceFunc_len), sizeof(old_sourceFunc_len));
  if(old_sourceFunc_len == 3)
    {
      // Next is a single null byte.
      char null_byte;
      rd.readAll(&null_byte, sizeof(null_byte));
      if(null_byte == 0)
	{
	  // Next is the format version of the log entry.
	  Basics::uint16 logged_version;
	  rd.readAll((char *)(&logged_version), sizeof(logged_version));
	  version = logged_version;

	  if(version < CacheLog::LargeFVsVersion)
	    {
	      // If the stored format version is below the first
	      // version to store an embedded format version, we have
	      // a problem.
	      Text msg =
		Text::printf("Invalid CacheLog format "
			     "(stored version %d is too low)",
			     version);
	      throw VestaLog::Error(0, msg);
	    }
	}
      else
	{
	  // This must be an old-style cache log entry with a
	  // sourceFunc of length 3.
	  version = CacheLog::SourceFuncVersion;
	  char buff[4];
	  buff[0] = null_byte;
	  rd.readAll(&(buff[1]), 2);
	  buff[4] = 0;
	  this->sourceFunc = buff;
	}
    }
  else
    {
      // This must be an old-style cache log entry.
      version = CacheLog::SourceFuncVersion;
      char *buff = NEW_PTRFREE_ARRAY(char, old_sourceFunc_len+1);
      rd.readAll(buff, old_sourceFunc_len);
      buff[old_sourceFunc_len] = 0;
      this->sourceFunc = buff;
      delete [] buff;
    }

  // Make sure we have a valid format version
  if((version > CacheLog::CurrentVersion) ||
     (version < CacheLog::MinSupportedVersion))
    {
      Text msg = Text::printf("Unsupported CacheLog format version %d",
			      version);
      throw VestaLog::Error(0, msg);
    }

  if(version > CacheLog::SourceFuncVersion)
    {
      // We still need to read the sourceFunc
      TextIO::Recover(rd, /*OUT*/ this->sourceFunc);
    }

  pk = NEW_CONSTR(FP::Tag, (rd));
  rd.readAll((char *)(&(this->pkEpoch)), sizeof(this->pkEpoch));

  rd.readAll((char *)(&(this->ci)), sizeof(this->ci));
  this->value =
    NEW_CONSTR(VestaVal::T, (rd,
			     // Support the old format
			     (version < CacheLog::LargeFVsVersion)));
  this->kids = NEW_CONSTR(CacheEntry::Indices, (rd));
  Model::Recover(/*OUT*/ this->model, rd);

  this->names = NEW_CONSTR(FV::List, (rd));
  this->fps = NEW_CONSTR(FP::List, (rd));
}

void CacheLog::Entry::Write(ostream &ofs) const throw (FS::Failure)
{
  // First we store a format version marker.  (See description above
  // in the "Log" member function.)
  {
    // (See description above in CacheLog::Entry::Log.)
    Basics::uint16 old_sourceFunc_len = 3;
    FS::Write(ofs, (char *)&old_sourceFunc_len, sizeof(old_sourceFunc_len));
    char null_byte = 0;
    FS::Write(ofs, &null_byte, 1);
    Basics::uint16 logged_version = CacheLog::CurrentVersion;
    FS::Write(ofs, (char *)(&logged_version), sizeof(logged_version));
  }
    TextIO::Write(ofs, this->sourceFunc);
    this->pk->Write(ofs);
    FS::Write(ofs, (char *)(&(this->pkEpoch)), sizeof(this->pkEpoch));

    FS::Write(ofs, (char *)(&this->ci), sizeof(this->ci));
    this->value->Write(ofs);
    this->kids->Write(ofs);
    Model::Write(this->model, ofs);

    this->names->Write(ofs);
    this->fps->Write(ofs);
}

static void Indent(ostream &os, int indent) throw ()
{
    for (int i = 0; i < indent; i++) os << ' ';
}

void CacheLog::Entry::Debug(ostream &s, int indent) const throw ()
{
    Indent(s, indent);
    s << "pk = " << *pk << ", pkEpoch = " << pkEpoch
      << ", ci = " << ci << ", ..." << endl;
}

void CacheLog::Entry::DebugFull(ostream &s) const throw ()
{
    s << "  sourceFunc = " << this->sourceFunc << endl;
    s << "  pk         = " << *pk << endl;
    s << "  pkEpoch    = " << pkEpoch << endl;

    s << "  ci         = " << ci << endl;
    s << "  value      =" << endl; this->value->Print(s, /*indent=*/ 4);
    s << "  kids       = "; this->kids->Print(s, /*indent=*/ 4);
    s << "  model      = " << this->model << endl;

    s << "  names      =" << endl; this->names->Print(s, /*indent=*/ 4);
    s << "  fps        =" << endl; this->fps->Print(s, /*indent=*/ 4);
}
