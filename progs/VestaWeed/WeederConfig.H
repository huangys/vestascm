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

// Created on Tue Mar  4 11:44:32 PST 1997 by heydon

// Last modified on Tue Nov 13 13:09:23 EST 2001 by ken@xorian.net
//      modified on Sun Oct 26 15:55:40 PST 1997 by heydon

#ifndef _WEEDER_CONFIG_H
#define _WEEDER_CONFIG_H

// Section names in Vesta configuration file
extern const char *Config_WeederSection;

// Values from Vesta configuration file
extern const Text Config_WeederMDDir;  	  // [Weeder]/MetaDataDir
extern const Text Config_Weeded;   	  // [Weeder]/Weeded
extern const Text Config_MiscVars;  	  // [Weeder]/MiscVars
extern const Text Config_PendingGL;  	  // [Weeder]/PendingGL
extern const Text Config_WorkingGL;  	  // [Weeder]/WorkingGL
extern const int  Config_GLNodeBuffSize;  // [Weeder]/GLNodeBuffSize
extern const int  Config_DIBuffSize;      // [Weeder]/DIBuffSize
extern const Text WeedConfig_CacheHost;   // [Weeder]/CacheHost
extern const Text WeedConfig_CachePort;   // [Weeder]/CachePort
extern const Text WeedConfig_CacheMDRoot; // [Weeder]/CacheMDRoot
extern const Text WeedConfig_CacheMDDir;  // [Weeder]/CacheMDDir
extern const Text WeedConfig_CacheGLDir;  // [Weeder]/CacheGLDir

// Values formed from configuration values
/* Here, the "Config_WeederMDPath" is taken relative to the "Config_MDRoot"
   specified in <CacheConfig.H>. */
extern const Text Config_WeederMDPath; 	  // Config_MDRoot/Weeder_MDDir
extern const Text Config_WeededFile;	  //   WeederMDPath/Weeded
extern const Text Config_MiscVarsFile;    //   WeederMDPath/MiscVars
extern const Text Config_PendingGLFile;   //   WeederMDPath/PendingGL
extern const Text Config_WorkingGLFile;   //   WeederMDPath/WorkingGL

#endif // _WEEDER_CONFIG_H
