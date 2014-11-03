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

// Last modified on Tue Mar 22 14:16:37 EST 2005 by ken@xorian.net
//      modified on Thu Feb  2 09:23:22 PST 1995 by levin

#ifndef _chars_seq_private
#define _chars_seq_private


class chars_seq_impl {
  friend class SRPC;
  friend class SRPC_impl;
public:
  // Because of type-system breaches needed for storage management, the
  // actual data is not defined as part of the class, but rather as a
  // separately allocated piece of storage of type "rep".
  enum stg_type { grow, full, manual };
  struct header {
    stg_type storage;
    int len;            // sequence length
    char *bodies;       // start of string bodies
    char *limit;        // one past last byte in buffer (i.e., end of bodies)
  };
  struct rep {
    header h;
    char *base;         // sequence[0], ...
  };

  static rep *allocate_buffer(int len_hint = 10, int bytes = 0);

  static void expand(rep *&p, int len_hint, int min_body_add);

};

#endif  /* _chars_seq_private */
