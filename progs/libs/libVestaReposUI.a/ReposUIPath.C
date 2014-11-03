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

#include "ReposUIPath.H"

ReposUI::Path::Path(const Text &path_in, bool canonical) 
  throw(VestaConfig::failure, ReposUI::failure)
{
  Text canonical_path =
    canonical ? path_in : ReposUI::canonicalize(path_in);

  this->relative_path =
    ReposUI::stripRoot(canonical_path, this->which_root);

  if((this->which_root != ReposUI::VESTA) &&
     (this->which_root != ReposUI::VESTAWORK))
    {
      // This could happen if the caller told us this was a canonical
      // path but it wasn't
      throw ReposUI::failure(path_in +
			     " is not in the Vesta repository");
    }
}

static Text combine_path(const Text &root, const Text &relative)
{
  if(relative == "") return root;

  if((root.Length() < 1) ||
     (root[root.Length()-1] != '/'))
    {
      return (root + "/" + relative);
    }

  return (root + relative);
}

Text ReposUI::Path::canonical() const throw()
{
  assert((this->which_root == ReposUI::VESTA) ||
	 (this->which_root == ReposUI::VESTAWORK));

  Text root_path = ((this->which_root == ReposUI::VESTA)
		    ? "/vesta"
		    : "/vesta-work");

  return combine_path(root_path, this->relative_path);
}

Text ReposUI::Path::mounted() const throw(VestaConfig::failure)
{
  assert((this->which_root == ReposUI::VESTA) ||
	 (this->which_root == ReposUI::VESTAWORK));

  Text root_path =
    VestaConfig::get_Text("UserInterface",
			  (this->which_root == ReposUI::VESTA)
			  ? "AppendableRootName"
			  : "MutableRootName");

  return combine_path(root_path, this->relative_path);
}
