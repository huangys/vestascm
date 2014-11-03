// Copyright (C) 2007, Vesta Free Software Project
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

#include "ReposUI.H"

namespace ReposUI
{
  class Path
  {
  private:
    // Which root is this path in?
    ReposUI::RootLoc which_root;

    // Relative path within the root.
    Text relative_path;
  public:
    // Initialize from a path.  If "canonical" is true, then "path_in"
    // is already canonical and need not be put through
    // ReposUI::canonicalize.  If the object doesn't seem to be in the
    // repository, this function will throw ReposUI::failure.
    Path(const Text &path_in, bool canonical = false)
      throw(VestaConfig::failure, ReposUI::failure);

    // Return the relative portion of the path
    const Text &relative() const throw()
    { return relative_path; }
    // Return the root specifier
    const ReposUI::RootLoc root() const throw()
    { return which_root; }

    // Return the canonical version of the path
    Text canonical() const throw();
    // Return the mounted version of the path (accessible through the
    // filesystem)
    Text mounted() const throw(VestaConfig::failure);
  };
}
