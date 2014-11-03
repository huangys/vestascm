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

#ifndef _REMOTE_MODEL_SPACE_H
#define _REMOTE_MODEL_SPACE_H

#include <VestaSource.H>
#include "ParseImports.H"

// Size of the model to read from the remote server
#define RMS_BLOCKSIZE 8192

namespace ParseImports {

  // Subclass to support ParseImports reading models directly from the
  // VestaSource interface.  This is needed to support the general case
  // where the source repository may not be local or may not be mounted
  // locally as /vesta.
  //
  class RemoteModelSpace : public ModelSpace {
  public:
    char getC() throw (Error, FS::EndOfFile, SRPC::failure);
    void ungetC(char c) throw () { assert(putback == -1); putback = c; };
    long tell() throw () { return offset - inbuf + bufidx; };
    RemoteModelSpace(VestaSource* root, AccessControl::Identity who) {
      this->root = root;
      this->who = who;
      this->file = NULL;
      this->putback = -1;
      this->bufidx = 0;
      this->inbuf = 0;
      this->offset = 0;
      this->goteof = false;
    };
    ModelSpace* open(const Text &modelname) const
      throw(Error, FS::Failure, SRPC::failure);
    ModelSpace::type getType(const Text &name)
      const throw (SRPC::failure);
    ~RemoteModelSpace() { if (file) delete file; };
  private:
    VestaSource* root;
    AccessControl::Identity who;
    VestaSource* file;
    int putback;
    int bufidx;
    int inbuf;
    unsigned long offset;
    bool goteof;
    char buf[RMS_BLOCKSIZE];
  };

  // Use the local filesystem for reading a model already replicated
  // locally, but fall back on calling the remote repository for
  // anything not replicated locally.

  class OptimizedModelSpace : public LocalModelSpace
  {
  private:
    const RemoteModelSpace &remote_space;
  public:
    ModelSpace* open(const Text &modelname) const
      throw(Error, FS::Failure);
    ModelSpace::type getType(const Text &name) const throw ();
    OptimizedModelSpace(const Text &modelname,
			const RemoteModelSpace &remote) throw(FS::Failure);
  };
}

#endif
