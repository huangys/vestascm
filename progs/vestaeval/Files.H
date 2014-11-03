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

// File: Files.H 

#ifndef Files_H
#define Files_H

#include "EvalBasics.H"
#include "Location.H"
#include <VestaSource.H>

// If s does not have a suffix, append suff:
Text setSuffix(const Text& s, const Text& suff);

// Test whether path starts with slash or backslash:
bool IsAbsolutePath(const Text& path);

// Put path's final arc in name, the rest in prefix:
void SplitPath(const Text& path, Text& prefix, Text& name);

// Check for Delimiter; not platform-specific
bool IsDelimiter(const char c);

// Returns the main model name, given a possibly partial name or
// directory name.  Will append ".ves" if needed, or the given 
// default filename (e.g., ".main.ves") if needed.
Text MainModel(const Text& filename, const Text& defaultmain);

// Determine if a given path names a directory
bool IsDirectory(VestaSource *mRoot, Text& path, VestaSource*& vSource,
		 VestaSource::errorCode& vSourceErr);

// Open a file (either in the repository or in NFS), and record information
// to allow subsequent relative file names to be interpreted correctly.
bool OpenSource(VestaSource *mRoot,     // directory of the model
		const Text& fname,      // file name
                SrcLoc *loc,            // where this model is included
		std::fstream*& iFile,   // (out) resulting stream
		VestaSource*& newRoot,  // (out) directory of the model file
		ShortId& shortId,       // (out) shortid,
		VestaSource*& vSource); // (out) directory of the model file

// Replicate in missing objects. The function trys to find a real copy 
// of the object in other repositories which might be defined in 
// [UserInterface]DefaultHints and replicate it in. 
// Returns a pointer to a replicated object.
VestaSource *ReplicateMissing(VestaSource* root, const Text& name,
			      const Text& replSuffix);

#endif // Files_H
