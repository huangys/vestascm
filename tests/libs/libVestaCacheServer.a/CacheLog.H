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


#ifndef _CACHE_LOG_H
#define _CACHE_LOG_H

#include <Basics.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <FP.H>
#include <PKEpoch.H>
#include <FV.H>

#include "CacheEntry.H"

/* The "cacheLog" is a linked list of "CacheLog::Entry" structures. Each
   structure contains:

     "pk":         a primary key
     "pkEpoch":    the epoch of the entry's PKFile at the time it was
                   added
     "sourceFunc": the source location annotation from the client
     "ci":         the cache index of the entry
     "value":      the stored result value
     "model":      model (ShortId) for the entry
     "kids":       a list of the child cache entry indecies
     "names":      the free variables (on which the functions result
                   depends)
     "fps":        the fingerprints corresponding to the free
                   variables

   Conceptually, this is the arguments from the client's AddEntry
   call, plus the pkEpoch and ci.
*/

namespace CacheLog
{
    // Once initialized, all data fields (except the "next" field) of a
    // "CacheLog::Entry" are read-only 
    class Entry {
      public:
        // Constructors
        Entry() throw ()
          : pk((FP::Tag *)NULL), value(NULL), kids(NULL),
	    names(NULL), fps(NULL),
            next((Entry *)NULL) { /*SKIP*/ }
      	Entry(const Text& sourceFunc, FP::Tag *pk, PKFile::Epoch pkEpoch,
	      CacheEntry::Index ci, VestaVal::T *value,
	      Model::T model, CacheEntry::Indices *kids,
	      FV::List *names, FP::List *fps) throw ()
          : sourceFunc(sourceFunc), pk(pk), pkEpoch(pkEpoch),
	    ci(ci), value(value), model(model), kids(kids),
	    names(names), fps(fps),
	    next((Entry *)NULL)
            { /*SKIP*/ }
      	Entry(RecoveryReader &rd) throw (VestaLog::Error, VestaLog::Eof)
        /* REQUIRES Sup(LL) = CacheS.cacheLogMu */
      	  { Recover(rd); }

      	void Init(const Text& sourceFunc, FP::Tag *pk, PKFile::Epoch pkEpoch,
		  CacheEntry::Index ci, VestaVal::T *value,
		  Model::T model, CacheEntry::Indices *kids,
		  FV::List *names, FP::List *fps) throw ();
      	/* Initialize this cache log entry to the tuple "(sourceFunc,
           pk, pkEpoch, ci, value, model, kids, names, fps)". */

        // Destructors
      	~Entry() throw () { this->Reset(); }
        void Reset() throw ()
          {
	    this->pk = NULL; this->value = NULL; this->kids = NULL;
	    this->names = NULL; this->fps = NULL;
	  }
      	/* Drop memory reachable from this cache entry. */

      	void Log(VestaLog &log) const throw (VestaLog::Error);
        /* REQUIRES Sup(LL) = CacheS.cacheLogMu */
      	/* Append this cache entry to "log"; requires "log" to be
    	   in the "logging" state. */
    
      	void Recover(RecoveryReader &rd)
          throw (VestaLog::Error, VestaLog::Eof);
        /* REQUIRES Sup(LL) = CacheS.cacheLogMu */
      	/* Recover a previously-logged CacheLogEntry from "rd". */

      void Write(std::ostream &ofs) const throw (FS::Failure);
        /* Write this cache log entry to "ofs". */

      void Debug(std::ostream &s, int indent = 2) const throw ();
        /* Write a one-line summary of this entry to "s". The output is
           indented by "indent" spaces, and ends with a newline. */

      void DebugFull(std::ostream &s) const throw ();
        /* Write full debugging information for this entry to "s" consisting
           of serveral lines. The output is indented; it is not followed by a
           blank line. */

        // All data fields of a CacheLog::Entry are read-only after init.
        /* The "pk", "values", "kids", "names", and "fps" fields are
           pointers to structures that also reside in the cache. */
      	Entry *next;
        Text sourceFunc;           // source location of function definition
      	FP::Tag *pk;		   // primary key
        PKFile::Epoch pkEpoch;     // current PKFile epoch

	CacheEntry::Index ci;      // cache index
	VestaVal::T *value;        // value stored in this cache entry
	CacheEntry::Indices *kids; // child cache indecies
	Model::T model;            // model (ShortId) for this entry

        FV::List *names;           // free variables of this entry...
        FP::List *fps;	           // ...and associated fingerprints

      private:
        // hide copy constructor from clients
        Entry(const Entry &);

    };

  // Different versions of the formatthat cache log entries are
  // stored in.
  enum
    {
      // The version that added the sourceFunc field.  (This isn't
      // actually the first version, but it's the oldest one we
      // can support.)
      SourceFuncVersion = 1,
      LargeFVsVersion = 2
    };
  enum
    {
      MinSupportedVersion = SourceFuncVersion,
      CurrentVersion = LargeFVsVersion
    };
}

#endif // _CACHE_LOG_H
