// Copyright (c) 2000, Compaq Computer Corporation
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

// File: ThreadData.C

#include "ThreadData.H"
#include <pthread.h>

using Basics::OBufStream;

Basics::mutex frontierMu;
Basics::mutex callStackMu;

static pthread_key_t threadDataKey;

Basics::mutex ThreadData::mu;
ThreadData* ThreadData::head;

extern "C"
{
  static void
  ThreadDataDelete(void* vdata)
  {
    ThreadData* data = (ThreadData*) vdata;
    ThreadData::mu.lock();
    if (data->prev) {
      data->prev->next = data->next;
    } else {
      data->head = data->next;
    }
    if (data->next) {
      data->next->prev = data->prev;
    }
    if(recordCallStack) {
      assert(data->callStack != NULL);
      delete data->callStack;
      data->callStack = NULL; // help out the garbage collector
    } else {
      assert(data->callStack == NULL);
    }
    delete data;
    ThreadData::mu.unlock();
  }
}

// Create thread-specific data for the current thread
ThreadData*
ThreadDataCreate(int id, CacheEntry::IndicesApp *threadCIs)
{
  ThreadData* data = (ThreadData*) pthread_getspecific(threadDataKey);
  if (data) ThreadDataDelete(data);
  data = NEW(ThreadData);
  data->id = id;
  data->funcCallDepth = -1;
  assert(threadCIs != 0);
  data->orphanCIs = threadCIs;
  data->traceRes = NULL;
  if (recordCallStack) {
    data->callStack = NEW(Exprs);
  } else {
    data->callStack = NULL;
  }
  data->parent = NULL;
  data->parentCallStackSize = 0;
  pthread_setspecific(threadDataKey, (void*) data);
  ThreadData::mu.lock();
  data->prev = NULL;
  if (data->head) data->head->prev = data;
  data->next = data->head;
  data->head = data;
  ThreadData::mu.unlock();
  return data;
}

// Get thread-specific data for the current thread
ThreadData*
ThreadDataGet()
{
  return (ThreadData*) pthread_getspecific(threadDataKey);
}

// Initialize module.  Must call from main thread.
extern "C" void
ThreadDataInit_inner()
{
  pthread_key_create(&threadDataKey, ThreadDataDelete);
  ThreadData* thdata = ThreadDataCreate(0, NEW(CacheEntry::IndicesApp));
  if (recordTrace) {
    thdata->traceRes = NEW(OBufStream);
  }
}

void ThreadDataInit()
{
  static pthread_once_t init_once = PTHREAD_ONCE_INIT;
  pthread_once(&init_once, ThreadDataInit_inner);
}
