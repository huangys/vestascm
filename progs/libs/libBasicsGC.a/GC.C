// Copyright (C) 2001, Compaq Computer Corporation

// This file is part of Vesta.

// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// Definitions of "new" and "delete" for use with the garbage collector.

#include <sys/types.h> // for "size_t"
#include <assert.h>
#include <errno.h>

#include <new> // std::bad_allox

// First include the auto-generated header of #defines for the gc
// library configuration.
#if !defined(__osf__)
#include <used_gc_flags.h>
#endif

// Now include gc.h
#include <gc.h>

#if defined(__osf__)
// The DECWest collector doesn't publicly declare this, but it's there.
extern "C" void GC_add_roots(void *low, void *high_plus_1);
#endif

#include "Basics.H"    // for Basics::gc_attribs

using std::cerr;
using std::endl;

// ----------------------------------------------------------------------
// There's some ugly code here to get the garbage collector
// initialized and to handle chicken-and-egg initialization problems.

// We use a static instance of BasicsGCInit (a special class defined
// below) to call GC_init.  This ensures that the garbage collector is
// initialized from the main thread, before we reach the entry point
// of the main function.

// In some cases, the C++ run-time library or other library
// initialization code may call our overloaded new operator so early
// that the garbage collector won't work right.  (Even calling GC_init
// itself can cause problems in some cases.)  To avoid this, we use
// malloc until the static instance of BasicsGCInit is constructed.
// We keep a linked list of the blocks allocated with malloc in
// pre_init_blocks (below).  After initializing the garbage collector
// we add these blocks to the root set (with calls to GC_add_roots).
// This protects any blocks allocated from the garbage collected heap
// that are referenced by these early malloced blocks.

// This may create some small memory leaks if any of these early
// malloced blocks are ever deleted.  However we expect most of them
// to be part of the initialization of static instances.  We could add
// code to the delete operator to test whether a block being deleted
// was one of these early malloced blocks and free it (after removing
// it from the root set) in that case, but that would add a
// significant time cost to the delete operator.
// ----------------------------------------------------------------------

#if defined(EARLY_GC_INIT_UNSAFE)
// Data structure used to keep a linked list of blocks allocated with
// malloc before the garbage collector is initialized.
struct pre_init_block
{
  struct pre_init_block *next;
  size_t size;
};
static struct pre_init_block *pre_init_blocks = 0;
#endif

// This class has a single purpose: determine the offset from the
// beginning of the heap block of the pointer given to the application
// for an array allocation.  Most C++ run-times store an element count
// at the beginning of the heap block for such allocations, so that
// they know how many times to call the destructor when the block is
// deleted.  We need to know this offset to tell the garbage collector
// to consider pointers offset from the beginning of the block to be
// valid references.
class ArrayOffsetCalc
{
private:
  // This struct is private because only the "value" static member
  // function belwo should ever construct an instance.
  struct _dummy
  {
    int i;

    _dummy() throw() : i(1) {}

    // This placement new variant returns the block starting position
    // to the caller.
    void *operator new[](size_t size, void **block_start)
    {
      void *result;
      result = malloc(size);
      *block_start = result;
      return result;
    }

    // We need this because of a requirement for a matching placement
    // delete in case an exception is thrown during construction of an
    // object.
    void operator delete[](void *p, void **block_start) { free(p); }

    // We need this for the actual deletion which occurs.
    void operator delete[](void *p) { free(p); }

    // The class must have a destructor.  In some situations, the
    // run-time is smart enough to not keep an array count for an
    // array of instances of a class with no destructor.
    ~_dummy() { i = -1; }
  };

  // In some C++ run-times (at least gcc 3.3 on PowerPC Linux),
  // allocating an array of this object will get a *different* block
  // offset.  (I'm really not clear why this is.  It may have
  // something to do with the size of the struct and alignment, or it
  // may have to do with the fact that it contains a member variable
  // with a destructor but has no explicitly declared destructor.)
  struct _dummy2
  {
    unsigned long long uniqueid[2];
    _dummy nameA;

    void *operator new[](size_t size, void **block_start)
    {
      void *result;
      result = malloc(size);
      *block_start = result;
      return result;
    }
    void operator delete[](void *p, void **block_start) { free(p); }
    void operator delete[](void *p) { free(p); }
  };

public:
  // Call these functions to get the pointer offset into a heap block
  // for an array.

  static unsigned int value()
  {
    // Allocate an array and capture the block start.
    void *bs;
    _dummy *as = new (&bs) _dummy[1];
    delete [] as;
    // Return the byte offset of the array start from the block start
    return (((char *) as) - ((char *) bs));
  }

  static unsigned int value2()
  {
    void *bs;
    _dummy2 *as = new (&bs) _dummy2[1];
    delete [] as;
    return (((char *) as) - ((char *) bs));
  }
};

// ------ MYGETENV  get a value from ENV, optionally print message if found

static char *mygetenv (const char *env_name, bool vbose)
{
  char *val = getenv(env_name);
  if (vbose && val != NULL)
    cerr << "GC_ENV: Detected " << val << " in ENV variable named: " << env_name << endl;

  return val;
}

// ------ GETENV_UNAME  get an unsigned, base 10, numerical value from env

static int getenv_unum (const char *env_name, unsigned long *result, bool vbose)
{
  // Returns 0 if name was not in ENV
  // Returns 1 if name was converted successfully
  // Returns 2 if name was non-numeric
  // Returns 3 if name produced overflow error
  // Make sure to include <errno.h>

  char *end_cnvt, *env_val;
  unsigned long t_val;

  if ((env_val = mygetenv(env_name, vbose)) == NULL)
    return 0;    // Not found

  errno = 0;      // strtoul only sets errno on overflow
  t_val = strtoul(env_val,&end_cnvt,10);
  if (errno == ERANGE)
    return 3;    // overflow

  if (env_val == end_cnvt ||  // there ere no digits...
      *end_cnvt != 0)      // ...or there were left-over bytes...
    return 2;    // non-numeric stuff

  *result = t_val;
  return 1;
}

static bool BasicsGCInit_done = false;

extern "C" void BasicsGCInit_inner()
{
    GC_init();

    // In the case where the garbage collector will not consider any
    // pointer to the interior of a block to be a valid reference, we
    // need to tell it a couple offsets which *should* be considered
    // valid references.

#if !defined(ALL_INTERIOR_POINTERS) && defined(GC_REGISTER_DISPLACEMENT)
    // Needed for code which uses the LSB of a pointer as a boolean
    // flag (at least SharedTable from basics/generics does this).
    GC_REGISTER_DISPLACEMENT(1);

    // Needed for allocations of arrays of objects.  First add two
    // dynamically determined offsets.
    GC_REGISTER_DISPLACEMENT(ArrayOffsetCalc::value());
    GC_REGISTER_DISPLACEMENT(ArrayOffsetCalc::value2());

    // Some C++ run-time environments use more than one different
    // block offset for arrays under subtly different conditions.
    // Unfortunately, the dynamic detection may not cover all
    // possibilities, so also add a couple fixed offsets.
    GC_REGISTER_DISPLACEMENT(4);
    GC_REGISTER_DISPLACEMENT(8);
#endif

#if !defined(ALL_INTERIOR_POINTERS) && defined(__GNUC__) && (__GNUC__ >= 3)
    // Needed to force the gcc 3.[234] default allocator class to use
    // our new operator.  (See: bits/stl_alloc.h in 3.[23],
    // ext/pool_allocator.h and ext/mt_allocator.h in 3.4.)  If we
    // don't do this, it manages its own free lists using interior
    // pointers within heap blocks, which will not work correctly with
    // ALL_INTERIOR_POINTERS disabled.

# if !defined(__GNUC_MINOR__) || (__GNUC_MINOR__ < 4)
    // gcc 3.[23] use this environment variable
    putenv("GLIBCPP_FORCE_NEW=1");
# endif
# if !defined(__GNUC_MINOR__) || (__GNUC_MINOR__ >= 4)
    // gcc 3.4 switched to this environment variable
    putenv("GLIBCXX_FORCE_NEW=1");
# endif

#endif

#if !defined(__osf__)
    // ----- Allow ENV to alter some GC behaviors

    bool vbose = false;
    int tstat;

    if (mygetenv("GC_ENV_VERBOSE", 1) != NULL)
       vbose = true;

    // how many GC retries before giving up after heap expand fails
    if ((tstat = getenv_unum("GC_MAX_RETRIES", &GC_max_retries, vbose)) > 1)
       cerr << "WARNING: Non-numeric or huge value in GC_MAX_RETRIES was ignored. value= " << getenv("GC_MAX_RETRIES") << endl;

    if (GC_max_retries > 10)
       GC_max_retries = 10;    // probably a mistake, should we announce the action ??

    if (GC_max_retries < 1)
       GC_max_retries = 1;    // local default (factory default = 0)

    if (vbose && tstat)
	cerr << "GC_ENV: GC_max_retires finally set to: " << GC_max_retries << endl;

    // free space divisor
    if ((tstat = getenv_unum("GC_FREE_SPACE_DIVISOR", &GC_free_space_divisor, vbose)) > 1)
	cerr << "WARNING: Non-numeric or huge value in GC_FREE_SPACE_DIVISOR was ignored. value= " << getenv("GC_FREE_SPACE_DIVISOR") << endl;

    if (GC_free_space_divisor < 3)
       GC_free_space_divisor = 3;    // as low as we could want to go

    if (GC_free_space_divisor > 100)
       GC_free_space_divisor = 100;    // probably a mistake, should we announce the action ??

    if (vbose && tstat)
	cerr << "GC_ENV: GC_free_space_divisor finally set to: " << GC_free_space_divisor << endl;

    if (mygetenv("GC_DONT_EXPAND", vbose) != NULL)
       // Don's expand heap unless forced, or explicitly asked
       GC_dont_expand = 1;    // Assume default is 0

    // --------------------------------
#endif

    BasicsGCInit_done = true;
#if defined(EARLY_GC_INIT_UNSAFE)
    // Add any blocks allocated with malloc before initialization to
    // the root set.
    struct pre_init_block *cur = pre_init_blocks;
    while(cur)
      {
	char *real_block = ((char *) (cur+1));
	GC_add_roots(real_block, real_block+cur->size);
	cur = cur->next;
      }
#endif
}

void BasicsGCInit()
{
  static pthread_once_t init_once = PTHREAD_ONCE_INIT;
  pthread_once(&init_once, BasicsGCInit_inner);
}

// Initialize the garbage collector with a static instance of this
// class.  This ensures that we're ready to go before main starts.
class BasicsGCInitializer {
public:
  BasicsGCInitializer() throw ()
  {
    BasicsGCInit();
  }
};
static BasicsGCInitializer gcInit; // cause GC to be initialized from main thread

#if defined(EARLY_GC_INIT_UNSAFE)
// early_alloc - Perform an allocation before the garbage collector
// has been initialized.

static void *early_alloc(size_t size)
{
  // Allocate a block with malloc and keep a record of it so we
  // can add it to the GC root set later.
  struct pre_init_block *result_block =
    (struct pre_init_block *) ::malloc(sizeof(struct pre_init_block) +
				       size);
  result_block->next = pre_init_blocks;
  result_block->size = size;
  pre_init_blocks = result_block;
  return ((void *) (result_block+1));
}
#endif

// ptrfree_alloc - Allocate a block which the caller promises will
// contain no pointers.

static void *ptrfree_alloc(size_t size,
			   const char *file, unsigned int line)
{
  void *result;

#if defined(EARLY_GC_INIT_UNSAFE)
  // If the garbage collector hasn't been initialized yet, we won't
  // use it.  (In some situations, things blow up when trying to use
  // the collector very early during process initialization.)
  if(!BasicsGCInit_done)
    return early_alloc(size);
#else
  BasicsGCInit();
#endif

  // If the caller promoises that this block will not contain any
  // pointers to other heap blocks, use the pointer-free (aka
  // atomic) variant.
#if defined(GC_MALLOC_ATOMIC)
# if defined(GC_DEBUG)
  // If requested, use the gc's debugging allocation function
  result = GC_debug_malloc_atomic_ignore_off_page(size, file, line);
# else
    // With the Boehm collector, we use the ignore_off_page variant to
    // tell the GC library that we will always keep a pointer to
    // somewhere near the beginning of the block.  For arrays, the
    // pointer that the calling code gets isusually a few bytes into
    // the block, because the C++ runtime records the number of
    // elements in the array at the beginning of the block.

    result = GC_malloc_atomic_ignore_off_page(size);
# endif
#elif defined(__digital__)
  result = GC_malloc_ptrfree(size);
#else
# error Which garbage collector are we using?
#endif

  // If we run out of memory, die with assert.  Technically, we should
  // throw std::bad_alloc, but that normally causes death by
  // unexpected exception, so we just do it here, with a more useful
  // message.
  if(result == 0)
    {
      cerr << "Out of memory trying to allocate " << size << " bytes at "
	   << file << ":" << line << endl;
      abort();
    }
  return result;
}

// normal_alloc - Perform an allocation for a block which may contain
// pointers.

static void *normal_alloc(size_t size,
			  const char *file, unsigned int line)
{
  void *result;

#if defined(EARLY_GC_INIT_UNSAFE)
  // If the garbage collector hasn't been initialized yet, we won't
  // use it.  (In some situations, things blow up when trying to use
  // the collector very early during process initialization.)
  if(!BasicsGCInit_done)
    return early_alloc(size);
#else
  BasicsGCInit();
#endif

#if defined(GC_MALLOC_ATOMIC)
# if defined(GC_DEBUG)
  // If requested, use the gc's debugging allocation function
  result = GC_debug_malloc_ignore_off_page(size, file, line);
# else
    result = GC_malloc_ignore_off_page(size);
# endif
#elif defined(__digital__)
  result = GC_malloc(size);
#else
# error Which garbage collector are we using?
#endif

  // If we run out of memory, die with assert.  Technically, we should
  // throw std::bad_alloc, but that normally causes death by
  // unexpected exception, so we just do it here, with a more useful
  // message.
  if(result == 0)
    {
      cerr << "Out of memory trying to allocate " << size << " bytes at "
	   << file << ":" << line << endl;
      abort();
    }
  return result;
}

Basics::gc_no_pointers_t Basics::gc_no_pointers;

void *operator new(size_t size, Basics::gc_no_pointers_t unused,
		   const char *file, unsigned int line)
{
  return ptrfree_alloc(size, file, line);
}

void *operator new[](size_t size, Basics::gc_no_pointers_t unused,
		     const char *file, unsigned int line)
{
  return ptrfree_alloc(size, file, line);
}

void* operator new(size_t size,
		   const char *file, unsigned int line)
{
  return normal_alloc(size, file, line);
}

void* operator new[](size_t size,
		     const char *file, unsigned int line)
{
  return normal_alloc(size, file, line);
}

// Override the normal new operator to perform an allocation without
// any GC attributes (i.e. not pointer-free).

void* operator new(size_t size) throw(std::bad_alloc)
{
  return normal_alloc(size, "<UNKNOWN>", 0);
}

void* operator new[](size_t size) throw(std::bad_alloc)
{
  return normal_alloc(size, "<UNKNOWN>", 0);
}

// De-allocation functions do nothing.

void operator delete(void *p) { /*SKIP*/ }
void operator delete[](void *p) { /*SKIP*/ }

