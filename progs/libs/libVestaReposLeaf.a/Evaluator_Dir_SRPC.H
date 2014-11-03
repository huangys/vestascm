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

// Last modified on Mon Mar 24 10:44:49 EST 2003 by ken@xorian.net   
//      modified on Fri Jul 14 18:50:32 PDT 2000 by mann   
//      modified on Wed Sep 17 16:20:15 PDT 1997 by yuanyu 
//      modified on Fri May 26 15:34:57 PDT 1995 by horning
//      modified on Fri May 19 16:33:47 PDT 1995 by levin

// This file describes the SRPC interface for evaluator directories.
// An evaluator directory is a special form of immutable directory
// used by the repository as the "base" for the mutable directory that
// is provided for use as the file system root for an encapsulated tool.
// An evaluator directory corresponds to a Vesta binding, and the
// operations in this interface enable the repository implementation to
// perform directory-like operations on the binding.  Since the
// directory is immutable, only operations that interrogate its state
// are defined.

#ifndef Evaluator_Dir_SRPC_H
#define Evaluator_Dir_SRPC_H

// The interface includes the following procedures.  The enumeration
// gives the value to be passed as the SRPC 'procId' parameter, and
// the comments indicate arguments and return values.  The marshalled
// types (i.e., those understood by SRPC) are parenthesized.

enum { EVALUATOR_DIR_SRPC_VERSION = 3 };

const int DIR_HANDLE_BYTES = 8;

enum ed_entry_type {
  ed_none = 0,
  ed_directory = 1,
  ed_file = 2,
  ed_device = 3
};

enum ed_procs {

  ed_lookup = 0,
  // Arguments:
  //   dir      (bytes)    8-byte directory handle
  //   name     (chars)    name to be looked up in dir
  // Results:
  //   type     (int)      ed_entry_type.  If ed_none, name was not
  //                       found in dir.  Otherwise, additional results
  //                       follow:
  //   index    (int)      index of entry in dir 
  //   subdir   (bytes)    8-byte handle (present iff type == ed_directory)
  //   file     (int)      shortId of file (present iff type == ed_file)
  //   fptag    (bytes)    FP::Tag (present iff type == ed_file)
  //   rdev     (int)      device number (present iff type == ed_device)

  ed_lookup_index = 1,
  // Arguments:
  //   dir      (bytes)    8-byte directory handle
  //   index    (int)      index of entry in dir 
  // Results:
  //   type     (int)      ed_entry_type.  If ed_none, name was not
  //                       found in dir.  Otherwise, additional results
  //                       follow:
  //   name     (chars)    name in specified entry
  //   subdir   (bytes)    8-byte handle (present iff type == ed_directory)
  //   file     (int)      shortId of file (present iff type == ed_file)
  //   fptag    (bytes)    FP::Tag (present iff type == ed_file)

  ed_oldlist = 2,
  // Same as ed_list, but the "file" result is omitted.

  ed_list = 3
  // Arguments:
  //   dir      (bytes)    8-byte directory handle
  //   index    (int)      index at which to begin directory listing
  //   limit    (int)      maximum amount of listing to return, see below
  //   overhead (int)      used in limit calculation, see below
  // Results:
  //   returns a general sequence (in the SRPC sense), using send_seq_*
  //   (No length information is passed with send_seq_start.)  Each
  //   sequence element is a triple:
  //     name   (chars)    directory entry name
  //     type   (int)      ed_entry_type
  //     file   (int)      shortId of file (NullShortId if type != ed_file)
  //   The first element of the sequence will be the directory entry
  //   whose index is 'index'.  (The first entry in a directory has
  //   index 0.)  Successive elements of the sequence correspond to
  //   successively numbered members of the directory.  The sequence
  //   terminates when either:
  //     (a) extending the sequence by one would cause the cumulative
  //         cost of the sequence to exceed 'limit', where the cost
  //         of an element of the sequence is strlen(name)+overhead, or
  //     (b) the end of the directory is reached, in which case a
  //         special "terminating" entry is appended to the sequence
  //         whose 'name' is empty and whose 'type' is ed_none.  This
  //         entry is not included in the cost calculation.
  //   Notice that an empty sequence can arise only as a result of
  //   unfortunate settings for 'limit' and/or 'overhead'.
  //   It is an error to pass a negative number for 'index'.  Passing
  //   a value greater than the index of the last member in the directory
  //   is handled as in (b), above.

  };


#endif  /* Evaluator_Dir_SRPC_H */
