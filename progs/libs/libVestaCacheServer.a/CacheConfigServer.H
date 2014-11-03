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

// Created on Tue Sep 16 10:37:18 PDT 1997 by heydon

#ifndef _CACHE_CONFIG_SERVER_H
#define _CACHE_CONFIG_SERVER_H

// This file defines configuration variables required only by the cache
// server.

#include <Basics.H>

// Required configuration values
extern const int  Config_FreePauseDur;    // [CacheServer]/FreePauseDur

// optional values
extern int  Config_MaxRunning;  	  // [CacheServer]/MaxRunning
extern int  Config_WeedPauseDur;    	  // [CacheServer]/WeedPauseDur
extern int  Config_MaxCacheLogCnt;  	  // [CacheServer]/MaxCacheLogCnt
extern int  Config_MPKFileFlushNum; 	  // [CacheServer]/MPKFileFlushNum
extern int  Config_FlushWorkerCnt;        // [CacheServer]/FlushWorkerCnt

// Note: FlushChangedPeriodCnt used to be called FreePeriodCnt, and
// tat name is still allowed in the config file if
// [CacheServer]/FlushNewPeriodCnt isn't set.

extern int  Config_FlushNewPeriodCnt;     // [CacheServer]/FlushNewPeriodCnt
extern int  Config_PurgeWarmPeriodCnt;    // [CacheServer]/PurgeWarmPeriodCnt
extern int  Config_EvictPeriodCnt;        // [CacheServer]/EvictPeriodCnt

extern bool Config_FreeAggressively;      // [CacheServer]/FreeAggressively
extern bool Config_ReadImmutable;         // [CacheServer]/ReadImmutable
extern bool Config_KeepNewOnFlush;        // [CacheServer]/KeepNewOnFlush
extern bool Config_KeepOldOnFlush;        // [CacheServer]/KeepOldOnFlush

#endif // _CACHE_CONFIG_SERVER_H
