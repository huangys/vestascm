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

//
// VLogHelp.H
// Last modified on Tue Jul 20 17:27:00 EDT 2004 by ken@xorian.net
//      modified on Mon Jul 20 14:36:50 PDT 1998 by mann
//
// Helpful functions for logging
//

std::ostream& operator<<(std::ostream& s, const LongId& longid);
std::ostream& PutQuotedString(std::ostream& s, const char* string);
std::ostream& PutFPTag(std::ostream& s, const FP::Tag& fptag);
void LogPutQuotedString(VestaLog& log, const char* string);
void GetFPTag(RecoveryReader* rr, char& c, FP::Tag& fptag);
