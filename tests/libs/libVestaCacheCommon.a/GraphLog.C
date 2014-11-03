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

// Last modified on Fri Apr 22 17:27:58 EDT 2005 by ken@xorian.net  
//      modified on Sat Feb 12 12:02:14 PST 2000 by mann  
//      modified on Mon Nov 10 12:15:50 PST 1997 by heydon

#include <time.h>
#include <Basics.H>
#include <FS.H>
#include <VestaLog.H>
#include <FP.H>

#include "Model.H"
#include "CacheIndex.H"
#include "Derived.H"
#include "GraphLog.H"

using std::ostream;
using std::istream;

/* GraphLog::Entry -------------------------------------------------------- */

GraphLog::Entry *GraphLog::Entry::Recover(RecoveryReader &rd)
  throw (VestaLog::Error, VestaLog::Eof)
{
    GraphLog::Kind kind;
    rd.readAll((char *)(&kind), sizeof_assert(kind, 4));
    switch (kind) {
      case RootKind: return NEW_CONSTR(GraphLog::Root, (rd));
      case NodeKind: return NEW_CONSTR(GraphLog::Node, (rd));
      default:
	// This is a fatal error.
	{
	  Text msg = "Invalid graph log format.";

	  // This graph log could have been written by a machine of a
	  // different byte order.  We'll check that and sugest that
	  // in the error messgae if it looks likely.
	  GraphLog::Kind kind_swapped =
	    (GraphLog::Kind) Basics::swab32((Basics::uint32) kind);
	  if((kind_swapped == RootKind) ||
	     (kind_swapped == NodeKind))
	    {
	      msg += ("  (Maybe the graph log was written by a machine "
		      "of a different byte order?)");
	    }

	  throw VestaLog::Error(0, msg);
	}
    }
    return NULL; // not reached
}

GraphLog::Entry *GraphLog::Entry::Read(istream &ifs)
  throw (FS::Failure, FS::EndOfFile)
{
    GraphLog::Kind kind;
    FS::Read(ifs, (char *)(&kind), sizeof_assert(kind, 4));
    switch (kind) {
      case RootKind: return NEW_CONSTR(GraphLog::Root, (ifs));
      case NodeKind: return NEW_CONSTR(GraphLog::Node, (ifs));
      default:
	// This is a fatal error.
	{
	  Text msg = "Invalid graph log format.";

	  // This graph log could have been written by a machine of a
	  // different byte order.  We'll check that and sugest that
	  // in the error messgae if it looks likely.
	  GraphLog::Kind kind_swapped =
	    (GraphLog::Kind) Basics::swab32((Basics::uint32) kind);
	  if((kind_swapped == RootKind) ||
	     (kind_swapped == NodeKind))
	    {
	      msg += ("  (Maybe the graph log was written by a machine "
		      "of a different byte order?)");
	    }

	  throw VestaLog::Error(0, msg);
	}
    }
    return NULL; // not reached
}

void GraphLog::Entry::Debug(ostream &s) const throw ()
{
    s << "  ";
    switch (kind) {
      case NodeKind: s << "Node: "; break;
      case RootKind: s << "Root: "; break;
      default: assert(false);
    }
}

void GraphLog::Entry::DebugFull(ostream &s) const throw ()
{
    GraphLog::Entry::Debug(s);
    s << '\n';
}

/* GraphLog::Node --------------------------------------------------------- */

void GraphLog::Node::Init(CacheEntry::Index ci, FP::Tag *loc, Model::T model,
  CacheEntry::Indices *kids, Derived::Indices *refs) throw ()
{
    this->next = (Node *)NULL;
    this->ci = ci;
    this->loc = loc;
    this->model = model;
    this->kids = kids;
    this->refs = refs;
}

void GraphLog::Node::Log(VestaLog &log) const
  throw (VestaLog::Error)
{
    GraphLog::Entry::Log(log);
    log.write((char *)(&(this->ci)), sizeof_assert(this->ci, 4));
    this->loc->Log(log);
    Model::Log(this->model, log);
    this->kids->Log(log);
    this->refs->Log(log);
}

void GraphLog::Node::Recover(RecoveryReader &rd)
  throw (VestaLog::Error, VestaLog::Eof)
{
    rd.readAll((char *)(&(this->ci)), sizeof_assert(this->ci, 4));
    this->loc = NEW_CONSTR(FP::Tag, (rd));
    Model::Recover(/*OUT*/ this->model, rd);
    this->kids = NEW_CONSTR(CacheEntry::Indices, (rd));
    this->refs = NEW_CONSTR(Derived::Indices, (rd));
}

void GraphLog::Node::Write(ostream &ofs) const throw (FS::Failure)
{
    GraphLog::Entry::Write(ofs);
    FS::Write(ofs, (char *)(&(this->ci)), sizeof_assert(this->ci, 4));
    this->loc->Write(ofs);
    Model::Write(this->model, ofs);
    this->kids->Write(ofs);
    this->refs->Write(ofs);
}

void GraphLog::Node::Read(istream &ifs)
  throw (FS::Failure, FS::EndOfFile)
{
    FS::Read(ifs, (char *)(&(this->ci)), sizeof_assert(this->ci, 4));
    this->loc = NEW_CONSTR(FP::Tag, (ifs));
    Model::Read(/*OUT*/ this->model, ifs);
    this->kids = NEW_CONSTR(CacheEntry::Indices, (ifs));
    this->refs = NEW_CONSTR(Derived::Indices, (ifs));
}

void GraphLog::Node::Debug(ostream &os) const throw ()
{
    GraphLog::Entry::Debug(os);
    os << "pk = " << *(this->loc) << ", ci = " << this->ci << ", ...\n";
}

void GraphLog::Node::DebugFull(ostream &os) const throw ()
{
    GraphLog::Entry::DebugFull(os);
    os << "    pk = " << *(this->loc) << '\n';
    os << "    ci = " << this->ci << '\n';
    os << "    model = " << this->model << '\n';
    os << "    kids = "; this->kids->Print(os, /*indent=*/ 6);
    os << "    refs = "; this->refs->Print(os, /*indent=*/ 6);
}

/* GraphLog::Root --------------------------------------------------------- */

void GraphLog::Root::Init(const FP::Tag &pkgVersion, Model::T model,
  CacheEntry::Indices *cis, bool done) throw ()
{
    this->pkgVersion = pkgVersion;
    this->model = model;
    (void) time(&(this->ts));
    this->cis = cis;
    this->done = done;
}

void GraphLog::Root::Log(VestaLog &log) const
  throw (VestaLog::Error)
{
    GraphLog::Entry::Log(log);
    this->pkgVersion.Log(log);
    Model::Log(this->model, log);

    // Log this->ts as a 32-bit integer, as the size of time_t can
    // vary between platforms.
    Basics::int32 l_ts = this->ts;
    log.write((char *)(&l_ts), sizeof_assert(l_ts, 4));

    this->cis->Log(log);

    // Log this->done as a one-byte value, as the size of the bool
    // type can vary between platforms/compilers.
    bool8 l_done = this->done ? 1 : 0;
    log.write((char *)(&l_done), sizeof_assert(l_done, 1));
}

void GraphLog::Root::Recover(RecoveryReader &rd)
  throw (VestaLog::Error, VestaLog::Eof)
{
    this->pkgVersion.Recover(rd);
    Model::Recover(/*OUT*/ this->model, rd);

    // Recover this->ts as a 32-bit integer
    Basics::int32 l_ts;
    rd.readAll((char *)(&l_ts), sizeof_assert(l_ts, 4));
    this->ts = l_ts;

    this->cis = NEW_CONSTR(CacheEntry::Indices, (rd));

    // Recover done is a one-byte value
    bool8 l_done;
    rd.readAll((char *)(&l_done), sizeof_assert(l_done, 1));
    this->done = (l_done != 0);
}

void GraphLog::Root::Write(ostream &ofs) const throw (FS::Failure)
{
    GraphLog::Entry::Write(ofs);
    this->pkgVersion.Write(ofs);
    Model::Write(this->model, ofs);

    // Write this->ts as a 32-bit integer
    Basics::int32 l_ts = this->ts;
    FS::Write(ofs, (char *)(&l_ts), sizeof_assert(l_ts, 4));

    this->cis->Write(ofs);

    // Write this->done as a one-byte value
    bool8 l_done = this->done ? 1 : 0;
    FS::Write(ofs, (char *)(&l_done), sizeof_assert(l_done, 1));
}

void GraphLog::Root::Read(istream &ifs)
  throw (FS::Failure, FS::EndOfFile)
{
    this->pkgVersion.Read(ifs);
    Model::Read(/*OUT*/ this->model, ifs);

    // Read this->ts as a 32-bit integer
    Basics::int32 l_ts;
    FS::Read(ifs, (char *)(&l_ts), sizeof_assert(l_ts, 4));
    this->ts = l_ts;

    this->cis = NEW_CONSTR(CacheEntry::Indices, (ifs));

    // Read this->done as a one-byte value
    bool8 l_done;
    FS::Read(ifs, (char *)(&l_done), sizeof_assert(l_done, 1));
    this->done = (l_done != 0);
}

void GraphLog::Root::Debug(ostream &os) const throw ()
{
    GraphLog::Entry::Debug(os);
    os << "pkgFP = " << this->pkgVersion;
    os << ", model = " << this->model;
    /*** Print more debugging info? ***/
    os << ", ...\n";
}

void GraphLog::Root::DebugFull(ostream &os) const throw ()
{
#ifdef CTIME_R_AVAIL
    // convert "this->ts" to ASCII
    const int BuffSz = 80;
    char timeBuff[BuffSz];
    int res = ctime_r(&(this->ts), timeBuff, BuffSz);
    assert(res == 0);
#else
    char *timeBuff = ctime(&(this->ts));
    assert(timeBuff != NULL);
#endif

    // print all fields
    GraphLog::Entry::DebugFull(os);
    os << "    pkgFP = " << this->pkgVersion << '\n';
    os << "    model = " << this->model << '\n';
    os << "    time = " << timeBuff; // note: "timeBuff" has a trailing '\n'
    os << "    done = " << BoolName[done] << '\n';
    os << "    cis = "; this->cis->Print(os, /*indent=*/ 6);
}
