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

// Last modified on Thu Aug  5 00:38:05 EDT 2004 by ken@xorian.net

// StatLocation.H -- A class representing a location within the cache
// about which statistics are recorded.  Used for recording the
// min/max locations as well as (optionally) "troublesome" locations.

#ifndef _STAT_LOCATION_H
#define _STAT_LOCATION_H

#include <FP.H>
#include <FV.H>
#include <CacheIndex.H>

namespace Stat
{
  // A Location within the cache.
  class Location
  {
  private:
    // Path of MPKFile or directory above it.
    Text path;

    // Informaiton about the PKFile this was in.
    FP::Tag pk;
    Text sourceFunc;
    bool has_pk;

    // Informaiton about the CFP group this was in.
    FP::Tag cfp;
    FV::Epoch namesEpoch;
    bool has_cfp;

    // Informaiton about the cache entry this was in.
    CacheEntry::Index ci;
    bool has_ci;

  public:
    Location(const Text &p_path)
      : path(p_path),
	has_pk(false), has_cfp(false), has_ci(false)
    { }
    Location(const Text &p_path,
	     const FP::Tag &p_pk, const Text &p_sourceFunc)
      : path(p_path),
	pk(p_pk), sourceFunc(p_sourceFunc), has_pk(true),
	has_cfp(false), has_ci(false)
    { }
    Location(const Text &p_path,
	     const FP::Tag &p_pk, const Text &p_sourceFunc,
	     FV::Epoch p_namesEpoch, const FP::Tag &p_cfp)
      : path(p_path),
	pk(p_pk), sourceFunc(p_sourceFunc), has_pk(true),
	cfp(p_cfp), namesEpoch(p_namesEpoch), has_cfp(true),
	has_ci(false)
    { }
    Location(const Text &p_path,
	     const FP::Tag &p_pk, const Text &p_sourceFunc,
	     FV::Epoch p_namesEpoch, const FP::Tag &p_cfp,
	     CacheEntry::Index p_ci)
      : path(p_path),
	pk(p_pk), sourceFunc(p_sourceFunc), has_pk(true),
	cfp(p_cfp), namesEpoch(p_namesEpoch), has_cfp(true),
	ci(p_ci), has_ci(true)
    { }
    Location(const Location &p_other)
      : path(p_other.path),
	pk(p_other.pk), sourceFunc(p_other.sourceFunc), has_pk(p_other.has_pk),
	cfp(p_other.cfp), namesEpoch(p_other.namesEpoch), has_cfp(p_other.has_cfp),
	ci(p_other.ci), has_ci(p_other.has_ci)
    { }

    Location add_pk(const FP::Tag &p_pk, const Text *p_sourceFunc) const
    {
      return Location(path, p_pk, *p_sourceFunc);
    }

    Location add_cfp(FV::Epoch p_namesEpoch, const FP::Tag &p_cfp) const
    {
      return Location(path, pk, sourceFunc, p_namesEpoch, p_cfp);
    }

    Location add_ci(CacheEntry::Index p_ci) const
    {
      return Location(path, pk, sourceFunc, namesEpoch, cfp, p_ci);
    }

    const Text &get_sourceFunc() const { return sourceFunc; }

    void print(std::ostream &out, int indent = 0) const;
  };
}

std::ostream &operator<<(std::ostream &, const Stat::Location &);

#endif // _STAT_LOCATION_H
