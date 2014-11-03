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

// Created on Tue Mar  4 11:57:33 PST 1997 by heydon

// Last modified on Tue Nov 13 13:09:23 EST 2001 by ken@xorian.net
//      modified on Sun Oct 26 15:55:59 PST 1997 by heydon

#include <Basics.H>
#include <ReadConfig.H>
#include <CacheConfig.H>

#include "WeederConfig.H"

// Section names in Vesta configuration file
const char *Config_WeederSection = "Weeder";

// Values from Vesta configuration file
const Text Config_WeederMDDir(
  ReadConfig::TextVal(Config_WeederSection, "MetaDataDir"));
const Text Config_Weeded(
  ReadConfig::TextVal(Config_WeederSection, "Weeded"));
const Text Config_MiscVars(
  ReadConfig::TextVal(Config_WeederSection, "MiscVars"));
const Text Config_PendingGL(
  ReadConfig::TextVal(Config_WeederSection, "PendingGL"));
const Text Config_WorkingGL(
  ReadConfig::TextVal(Config_WeederSection, "WorkingGL"));
const int  Config_GLNodeBuffSize =
  ReadConfig::IntVal(Config_WeederSection, "GLNodeBuffSize");
const int  Config_DIBuffSize =
  ReadConfig::IntVal(Config_WeederSection, "DIBuffSize");
const Text WeedConfig_CacheHost(
  ReadConfig::TextVal(Config_WeederSection, "CacheHost"));
const Text WeedConfig_CachePort(
  ReadConfig::TextVal(Config_WeederSection, "CachePort"));
const Text WeedConfig_CacheMDRoot(
  ReadConfig::TextVal(Config_WeederSection, "CacheMDRoot"));
const Text WeedConfig_CacheMDDir(
  ReadConfig::TextVal(Config_WeederSection, "CacheMDDir"));
const Text WeedConfig_CacheGLDir(
  ReadConfig::TextVal(Config_WeederSection, "CacheGLDir"));

// Values formed from configuration values
const Text Config_WeederMDPath(Config_MDRoot +'/'+ Config_WeederMDDir);
const Text Config_WeededFile(Config_WeederMDPath +'/'+ Config_Weeded);
const Text Config_MiscVarsFile(Config_WeederMDPath +'/'+Config_MiscVars);
const Text Config_PendingGLFile(Config_WeederMDPath +'/'+Config_PendingGL);
const Text Config_WorkingGLFile(Config_WeederMDPath +'/'+Config_WorkingGL);
