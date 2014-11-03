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

// RemoteModelSpace.H -- Sub-classes of ParseImports::ModelSpace for
// reading the imports of models in remote repositories

#include <ReposUI.H>
#include "RemoteModelSpace.H"

char
ParseImports::RemoteModelSpace::getC() 
  throw (ParseImports::Error, FS::EndOfFile, SRPC::failure)
{
    if (putback != -1) {
	char tmp = putback;
	putback = -1;
	return tmp;
    }
    while (bufidx >= inbuf) {
	if (goteof) {
	    throw FS::EndOfFile();
	}
	inbuf = RMS_BLOCKSIZE;
	VestaSource::errorCode err = file->read(buf, &inbuf, offset, who);
	if (err != VestaSource::ok) {
	    throw ParseImports::Error(ReposUI::errorCodeText(err));
	}
	bufidx = 0;
	offset += inbuf;
	goteof = (inbuf == 0);
    }
    return buf[bufidx++];
}

ParseImports::ModelSpace*
ParseImports::RemoteModelSpace::open(const Text &modelname) const
  throw (ParseImports::Error, FS::Failure, SRPC::failure)
{
  // The model name relative to the repostiory root.
  Text relname;
  try
  {
    relname = ReposUI::stripSpecificRoot(modelname.cchars(), ReposUI::VESTA);
  }
  catch(ReposUI::failure f)
  {
    throw ParseImports::Error(f.msg);
  }
  catch(VestaConfig::failure f)
  {
    throw ParseImports::Error(f.msg);
  }

  // The path this model would have in the locally-mounted repository
  Text localname =
    VestaConfig::get_Text("UserInterface", "AppendableRootName") +
    "/" + relname;

  // If it exists locally, we assume that the replication invariant
  // holds and thus that it is identical to the remote copy.
  if(FS::Exists(localname))
    {
      try
	{
	  ParseImports::ModelSpace* local =
	    NEW_CONSTR(OptimizedModelSpace, (localname, *this));

	  return local;
	}
      // In the even of any failure with trying to open it locally, we
      // fall back on reading it from the remote repository.
      catch(...)
	{
	}
    }

  RemoteModelSpace* ret = NEW_CONSTR(RemoteModelSpace, (root, who));

    VestaSource::errorCode err =
      root->lookupPathname(relname.cchars(), ret->file, who);
    if (err != VestaSource::ok) {
	throw ParseImports::Error(ReposUI::errorCodeText(err) + ", " +
				  relname);
    }
    if (ret->file->type != VestaSource::immutableFile &&
	ret->file->type != VestaSource::mutableFile) {
	delete ret->file;
	throw ParseImports::Error("Not a file, " + relname);
    }	
    return ret;
}

ParseImports::ModelSpace::type
ParseImports::RemoteModelSpace::getType(const Text &name) const 
  throw (SRPC::failure)
{
    Text relname;
    try 
    {
      relname = ReposUI::stripSpecificRoot(name.cchars(), ReposUI::VESTA);
    }
    catch(ReposUI::failure f)
    {
      throw ParseImports::Error(f.msg);
    }
    catch(VestaConfig::failure f)
    {
      throw ParseImports::Error(f.msg);
    }

    ParseImports::ModelSpace::type ret = ParseImports::ModelSpace::none;

    VestaSource* vs;
    VestaSource::errorCode err =
      root->lookupPathname(relname.cchars(), vs, who);
    if (err != VestaSource::ok) {
        return ret;
    }
    switch (vs->type) {
    case VestaSource::immutableFile:
    case VestaSource::mutableFile:
      ret = ParseImports::ModelSpace::file;
      break;
    case VestaSource::immutableDirectory:
    case VestaSource::appendableDirectory:
    case VestaSource::mutableDirectory:
      ret = ParseImports::ModelSpace::directory;
      break;
    default:
      break;
    }
    delete vs;
    return ret;
}

ParseImports::ModelSpace*
ParseImports::OptimizedModelSpace::open(const Text &modelname) const
    throw(ParseImports::Error, FS::Failure)
{
  // Always call the RemoteModelSpace for open, as it will do the right thing
  return remote_space.open(modelname);
}

ParseImports::ModelSpace::type
ParseImports::OptimizedModelSpace::getType(const Text &name) const
  throw ()
{
  // Try to get the type of this object using the local filesystem first.
  ParseImports::ModelSpace::type result = ParseImports::LocalModelSpace::getType(name);

  // If we can't find it here, ask the source repository
  if(result == ParseImports::ModelSpace::none)
    {
      result = remote_space.getType(name);
    }

  return result;
}

ParseImports::OptimizedModelSpace::OptimizedModelSpace
(const Text &modelname,
 const RemoteModelSpace &remote)
  throw(FS::Failure)
  : ParseImports::LocalModelSpace(modelname),
    remote_space(remote)
{
}
