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

// Last modified on Thu Nov  8 12:36:38 EST 2001 by ken@xorian.net
//      modified on Tue Feb  1 14:29:57 PST 2000 by heydon

#include <Basics.H>
#include "ReadConfig.H"
#include "CacheConfig.H"

// Section names in Vesta configuration file
const char *Config_CacheSection = "CacheServer";

// Values from Vesta configuration file
const Text Config_Host(
  ReadConfig::TextVal(Config_CacheSection, "Host"));
const Text Config_Port(
  ReadConfig::TextVal(Config_CacheSection, "Port"));
static const Text Private_Dot(".");
static const Text Private_MDRoot(
  ReadConfig::TextVal(Config_CacheSection, "MetaDataRoot"));
const Text Config_MDRoot(
  Private_MDRoot.Empty() ? Private_Dot : Private_MDRoot);
const Text Config_MDDir(
  ReadConfig::TextVal(Config_CacheSection, "MetaDataDir"));
const Text Config_SVarsDir(
  ReadConfig::TextVal(Config_CacheSection, "StableVarsDir"));
const Text Config_Deleting(
  ReadConfig::TextVal(Config_CacheSection, "Deleting"));
const Text Config_HitFilter(
  ReadConfig::TextVal(Config_CacheSection, "HitFilter"));
const Text Config_MPKsToWeed(
  ReadConfig::TextVal(Config_CacheSection, "MPKsToWeed"));
const Text Config_SCacheDir(
  ReadConfig::TextVal(Config_CacheSection, "SCacheDir"));
const Text Config_WeededLogDir(
  ReadConfig::TextVal(Config_CacheSection, "WeededLogDir"));
const Text Config_CacheLogDir(
  ReadConfig::TextVal(Config_CacheSection, "CacheLogDir"));
const Text Config_EmptyPKLogDir(
  ReadConfig::TextVal(Config_CacheSection, "EmptyPKLogDir"));
const Text Config_GraphLogDir(
  ReadConfig::TextVal(Config_CacheSection, "GraphLogDir"));
const Text Config_CILogDir(
  ReadConfig::TextVal(Config_CacheSection, "CILogDir"));
const int  Config_LeaseTimeoutHrs =
  ReadConfig::IntVal(Config_CacheSection, "LeaseTimeoutHrs");

// optional values
int Config_LeaseTimeoutSpeedup(1);

// Values formed from configuration values
const Text Config_CacheMDPath(Config_MDRoot +'/'+ Config_MDDir);
const Text Config_SVarsPath(Config_CacheMDPath +'/'+ Config_SVarsDir);
const Text Config_DeletingFile(Config_SVarsPath +'/'+ Config_Deleting);
const Text Config_HitFilterFile(Config_SVarsPath +'/'+ Config_HitFilter);
const Text Config_MPKsToWeedFile(Config_SVarsPath +'/'+ Config_MPKsToWeed);
const Text Config_SCachePath(Config_CacheMDPath +'/'+ Config_SCacheDir);
const Text Config_WeededLogPath(Config_CacheMDPath +'/'+ Config_WeededLogDir);
const Text Config_CacheLogPath(Config_CacheMDPath +'/'+ Config_CacheLogDir);
const Text Config_EmptyPKLogPath(Config_CacheMDPath +'/'+Config_EmptyPKLogDir);
const Text Config_GraphLogPath(Config_CacheMDPath +'/'+ Config_GraphLogDir);
const Text Config_CILogPath(Config_CacheMDPath +'/'+ Config_CILogDir);
int Config_LeaseTimeoutSecs;

class CacheConfigInit {
  public:
    CacheConfigInit() throw ();
    // initialize the "CacheConfig" module
};
static CacheConfigInit cacheConfigInit;

CacheConfigInit::CacheConfigInit() throw ()
{
    (void) ReadConfig::OptIntVal(Config_CacheSection,
      "LeaseTimeoutSpeedup", /*OUT*/ Config_LeaseTimeoutSpeedup);
    const int SecsPerHour = 3600;
    Config_LeaseTimeoutSecs = max(1,
      (Config_LeaseTimeoutHrs * SecsPerHour) / Config_LeaseTimeoutSpeedup);
}

