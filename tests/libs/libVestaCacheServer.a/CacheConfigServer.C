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

// Created on Tue Sep 16 10:37:52 PDT 1997 by heydon

#include <Basics.H>
#include <ReadConfig.H>
#include <CacheConfig.H>
#include "CacheConfigServer.H"

// required values
const int  Config_FreePauseDur =
  ReadConfig::IntVal(Config_CacheSection, "FreePauseDur");

// optional values
int Config_MaxRunning(32);
int Config_WeedPauseDur(0);
int Config_MaxCacheLogCnt(500);
int Config_MPKFileFlushNum(20);
int Config_FlushWorkerCnt(5);
int Config_FlushNewPeriodCnt(1);
int Config_PurgeWarmPeriodCnt(2);
int Config_EvictPeriodCnt(3);
bool Config_FreeAggressively(false);
bool Config_ReadImmutable(false);
bool Config_KeepNewOnFlush(false);
bool Config_KeepOldOnFlush(false);

class CacheConfigServerInit {
  public:
    CacheConfigServerInit() throw ();
    // initialize the "CacheConfigServer" module
};
static CacheConfigServerInit cacheConfigServerInit;

CacheConfigServerInit::CacheConfigServerInit() throw ()
{
    // initialize optional values
    (void) ReadConfig::OptIntVal(Config_CacheSection,
      "MaxRunning", /*OUT*/ Config_MaxRunning);
    (void) ReadConfig::OptIntVal(Config_CacheSection,
      "WeedPauseDur", /*OUT*/ Config_WeedPauseDur);
    (void) ReadConfig::OptIntVal(Config_CacheSection,
      "MaxCacheLogCnt", /*OUT*/ Config_MaxCacheLogCnt);
    (void) ReadConfig::OptIntVal(Config_CacheSection,
      "MPKFileFlushNum", /*OUT*/ Config_MPKFileFlushNum);
    (void) ReadConfig::OptIntVal(Config_CacheSection,
      "FlushWorkerCnt", /*OUT*/ Config_FlushWorkerCnt);

    if(!ReadConfig::OptIntVal(Config_CacheSection, "FlushNewPeriodCnt",
			      /*OUT*/ Config_FlushNewPeriodCnt))
      {
	// Use the old name (FreePeriodCnt) if FlushNewPeriodCnt isn't
	// set.
	(void) ReadConfig::OptIntVal(Config_CacheSection, "FreePeriodCnt",
				     /*OUT*/ Config_FlushNewPeriodCnt);
      }

    if(!ReadConfig::OptIntVal(Config_CacheSection, "PurgeWarmPeriodCnt",
			      /*OUT*/ Config_PurgeWarmPeriodCnt))
      {
	// Default PurgeWarmPeriodCnt to FlushNewPeriodCnt+1
	Config_PurgeWarmPeriodCnt = Config_FlushNewPeriodCnt + 1;
      }

    if(!ReadConfig::OptIntVal(Config_CacheSection, "EvictPeriodCnt",
			      /*OUT*/ Config_EvictPeriodCnt))
      {
	// Default EvictPeriodCnt to PurgeWarmPeriodCnt+1
	Config_EvictPeriodCnt = Config_PurgeWarmPeriodCnt + 1;
      }

    (void) ReadConfig::OptBoolVal(Config_CacheSection,
      "FreeAggressively", /*OUT*/ Config_FreeAggressively);
    (void) ReadConfig::OptBoolVal(Config_CacheSection,
      "ReadImmutable", /*OUT*/ Config_ReadImmutable);
    (void) ReadConfig::OptBoolVal(Config_CacheSection,
      "KeepNewOnFlush", /*OUT*/ Config_KeepNewOnFlush);
    (void) ReadConfig::OptBoolVal(Config_CacheSection,
      "KeepOldOnFlush", /*OUT*/ Config_KeepOldOnFlush);
}
