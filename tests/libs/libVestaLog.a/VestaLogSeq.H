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

// Created on Fri Mar  7 13:32:11 PST 1997 by heydon


// VestaLogSeq.H -- a wrapper for a VestaLog that provides a uniform
//   interface for accessing the log's checkpoint and subsequent logs
//   as a sequence of RecoveryReader's.

#ifndef _VESTA_LOG_SEQ_H
#define _VESTA_LOG_SEQ_H

#include <Basics.H>
#include <FS.H>
#include "VestaLog.H"
#include "Recovery.H"

/* The methods of a VestaLogSeq are unmonitored. */

class VestaLogSeq {
  public:
    VestaLogSeq(char *dir) throw ()
      : dir(dir), opened(false) { /* SKIP */ }
    /* Create a new VestaLogSeq on the log represented in the filesystem by
       directory "dir". */

    void Open(int startVer = -1, bool readonly = false)
      throw (VestaLog::Error);
    /* Open the VestaLogSeq starting at the checkpoint named by
       "ver". A value of "-1" for "ver" means to recover from the
       latest checkpoint.  If ver != -1, the specified checkpoint must
       exist.  If "readonly" is "true", fewer access permissions are
       required. */

    RecoveryReader* Next(int endVer = -1) throw (VestaLog::Error, FS::Failure);
    /* By default, return a new RecoveryReader on the initial checkpoint or
       the next log file in the sequence, or NULL if there are no more log
       files. If "endVer" is non-negative, return NULL if the next log in the
       sequence has a version number at least "endVer" (that is, only return
       non-NULL if the next log version is strictly less than "endVer"). The 
       returned RecoveryReader should be read until end-of-file before "Next"
       is called again. After "Next" returns "NULL", call the "Close" method
       below. */

    void Close() throw ();
    /* Close the VestaLogSeq. The log sequence may be re-opened by a
       subsequent call to "Open". */

    int CurLogVersion() throw(VestaLog::Error)
    /* Return the log/ckp version currently being read. */
    {
      return this->vlog.logVersion();
    }

  private:
    VestaLog vlog;        // the underlying VestaLog object
    char *dir;            // name of directory that contains the log
    bool opened;          // is the VestaLog open?
    bool readChkpt;       // has the checkpoint been opened?
    std::fstream *chkptFS;     // fstream on checkpoint file
    RecoveryReader *rr;   // recovery reader on log files; NULL for first one
};

#endif // _VESTA_LOG_SEQ_H
