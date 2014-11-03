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

// Created on Fri Nov  7 09:50:08 PST 1997 by heydon
// Last modified on Tue Aug  3 16:27:14 EDT 2004 by ken@xorian.net
//      modified on Mon Nov 10 12:41:27 PST 1997 by heydon

// Defines the type "Model::T", which is a "ShortId".

#ifndef _MODEL_H
#define _MODEL_H

#include <Basics.H>
#include <FS.H>
#include <SRPC.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <SourceOrDerived.H>

class Model {
  public:
    // Model type
    typedef ShortId T;

    // log/recover
    static void Log(T model, VestaLog &log) throw (VestaLog::Error)
	{ log.write((char *)(&model), sizeof(model)); }
    static void Recover(/*OUT*/ T &model, RecoveryReader &rd)
	throw (VestaLog::Error, VestaLog::Eof)
        { rd.readAll((char *)(&model), sizeof(model)); }

    // write/read
    static void Write(T model, std::ostream &ofs) throw (FS::Failure)
	{ FS::Write(ofs, (char *)(&model), sizeof(model)); }
    static void Read(/*OUT*/ T &model, std::istream &ifs)
	throw (FS::EndOfFile, FS::Failure)
	{ FS::Read(ifs, (char *)(&model), sizeof(model)); }

    // send/receive
    static void Send(T model, SRPC &srpc) throw (SRPC::failure)
	{ srpc.send_int(model); }
    static T Recv(SRPC &srpc) throw (SRPC::failure)
        { return (T)(srpc.recv_int()); }
};

#endif // _MODEL_H
