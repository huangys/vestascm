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

#if !defined(_BASICS_SEM_H)
#define _BASICS_SEM_H

#include <semaphore.h>
#include <assert.h>

// This could be in Thread.H, but most programs don't use it

namespace Basics
{
  class sem
  {
  private:
    sem_t s;
  public:
    sem(unsigned int value = 0)
    {
      int res = sem_init(&s, 0, value);
      assert(res == 0);
    }

    ~sem()
    {
      int res = sem_destroy(&s);
      assert(res == 0);
    }

    void post()
    {
      int res = sem_post(&s);
      assert(res == 0);
    }

    void wait()
    {
      int res;
      do
	res = sem_wait(&s);
      while((res == -1) && (errno == EINTR));
      assert(res == 0);
    }

    bool trywait()
    {
      int res = sem_trywait(&s);
      return (res == 0);
    }

    int getvalue()
    {
      int val;
      int res = sem_getvalue(&s, &val);
      assert(res == 0);
      return val;
    }
  };
}

#endif
