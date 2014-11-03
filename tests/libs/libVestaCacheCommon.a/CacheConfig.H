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
//      modified on Tue Feb  1 14:25:34 PST 2000 by heydon

#ifndef _CACHE_CONFIG_H
#define _CACHE_CONFIG_H

#include <Basics.H>

// Section names in Vesta configuration file
extern const char *Config_CacheSection;

// Required configuration values
extern const Text Config_Host;        	  // [CacheServer]/Host
extern const Text Config_Port;        	  // [CacheServer]/Port
extern const Text Config_MDRoot;      	  // [CacheServer]/MetatDataRoot
extern const Text Config_MDDir;       	  // [CacheServer]/MetaDataDir
extern const Text Config_SVarsDir;    	  // [CacheServer]/StableVarsDir
extern const Text Config_Deleting;    	  // [CacheServer]/Deleting
extern const Text Config_HitFilter;   	  // [CacheServer]/HitFilter
extern const Text Config_MPKsToWeed;  	  // [CacheServer]/MPKsToWeed
extern const Text Config_SCacheDir;   	  // [CacheServer]/SCacheDir
extern const Text Config_WeededLogDir;	  // [CacheServer]/WeededLogDir
extern const Text Config_CacheLogDir; 	  // [CacheServer]/CacheLogDir
extern const Text Config_EmptyPKLogDir;   // [CacheServer]/EmptyPKLogDir
extern const Text Config_GraphLogDir; 	  // [CacheServer]/GraphLogDir
extern const Text Config_CILogDir;    	  // [CacheServer]/CILogDir
extern const int  Config_LeaseTimeoutHrs; // [CacheServer]/LeaseTimeoutHrs

// optional values
extern int Config_LeaseTimeoutSpeedup;    // [CacheServer]/LeaseTimeoutSpeedup

// Note: the value "Config_MDRoot" is set to "." if [CacheServer]/MetaDataRoot
// is bound to the empty string in the configuration file.

// Values formed from configuration values
extern const Text Config_CacheMDPath;  	  // MDRoot/MDDir
extern const Text Config_SVarsPath;    	  //   CacheMDPath/SVarsDir
extern const Text Config_DeletingFile; 	  //     SVarsPath/Deleting
extern const Text Config_HitFilterFile;	  //     SVarsPath/HitFilter
extern const Text Config_MPKsToWeedFile;  //     SVarsPath/MPKsToWeed
extern const Text Config_SCachePath;   	  //   CacheMDPath/SCacheDir
extern const Text Config_WeededLogPath;	  //   CacheMDPath/WeededLogDir
extern const Text Config_CacheLogPath; 	  //   CacheMDPath/CacheLogDir
extern const Text Config_EmptyPKLogPath;  //   CacheMDPath/EmptyPKLogDir
extern const Text Config_GraphLogPath; 	  //   CacheMDPath/GraphLogDir
extern const Text Config_CILogPath;    	  //   CacheMDPath/CILogDir

// (LeaseTimeoutHrs * 3600) / LeaseTimeoutSpeedup
extern int Config_LeaseTimeoutSecs;

#endif // _CACHE_CONFIG_H
