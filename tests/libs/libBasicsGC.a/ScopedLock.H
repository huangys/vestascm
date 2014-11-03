// Copyright (C) 2006, Kenneth C. Schalk
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

#if !defined(_BASICS_SCOPEDLOCK_H)
#define _BASICS_SCOPEDLOCK_H

#include <Basics.H>

// This could be in Thread.H, but most programs don't use it

namespace Basics
{
  class ScopedLock
  {
  private:
    bool locked;
    Basics::mutex &mu;
  public:
    void lock()
    {
      mu.lock();
      locked = true;
    }
    void unlock()
    {
      mu.unlock();
      locked = false;
    }
    bool is_locked()
    {
      return locked;
    }
    ScopedLock(Basics::mutex &m, bool lock_now = true)
      : mu(m), locked(false)
    {
      if(lock_now) lock();
    }

    ~ScopedLock()
    {
      if(locked) unlock();
    }
  };
}

#endif
