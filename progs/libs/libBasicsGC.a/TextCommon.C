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

// Code shared by the "Text.C" and "TextGC.C" implementations. This file
// is #included by both those files.

// We need this macro to get vasprintf defined in some cases
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "Basics.H"
#include "Text.H"

#include <stdarg.h>

// The empty string to which the Text() constructor initializes new Text's
static const char* const EmptyStr = "";

static const char *StrCopy(const char *str) throw ()
// Returns a newly allocated copy of the null-terminated string "str".
{
    // Protect against NULL pointers and don't bother to copy empty
    // strings
    if((str == 0) || (*str == 0))
      {
	return EmptyStr;
      }
    int len = strlen(str) + 1;  // include +1 for null terminator
    char *res = NEW_PTRFREE_ARRAY(char, len);
    memcpy(res, str, len);
    return res;
}

static const char *StrCopy(const char *s, int len) throw ()
/* Returns a newly-allocated, null-terminated copy of the "len" chars pointed
   to by "s"; it is an unchecked run-time error for any of those bytes to
   be the null character. */
{
    // Protect against NULL pointers and don't bother to copy empty
    // strings
    if((s == 0) || (*s == 0))
      {
	return EmptyStr;
      }
    char *res = NEW_PTRFREE_ARRAY(char, len+1);
    memcpy(res, s, len);
    res[len] = '\0';
    return res;
}

// constructors
Text::Text() throw ()
{
    this->s = EmptyStr;
}

Text::Text(const char *str, void *copy) throw ()
{
    this->s = (copy == (void *)NULL) ? StrCopy(str) : str;
}

Text::Text(const char *bytes, int len) throw ()
{
    this->s = StrCopy(bytes, len);
}

Text Text::Sub(int start, int len) const throw ()
{
    int tLen = Length();
    Text res;
    if (start >= tLen || len == 0) {
	res.s = EmptyStr;
    } else {
	if ((unsigned int)start + (unsigned int)len > tLen) {
	    // In this case, we can include the null terminator in the copy
	    len = tLen - start + 1; // include +1 for null terminator
	    char *temp = NEW_PTRFREE_ARRAY(char, len);
	    memcpy(temp, this->s + start, len);
	    res.s = temp;
	} else {
	    // include +1 for null terminator
	    char *temp = NEW_PTRFREE_ARRAY(char, len+1);
	    memcpy(temp, this->s + start, len);
	    temp[len] = '\0';
	    res.s = temp;
	}
    }
    return res;
}

int Text::FindChar(char c, int start) const throw ()
{
    int tLen = Length();
    for (register int i = max(start, 0); i < tLen; i++) {
	if (this->s[i] == c) return i;
    }
    return -1;
}

int Text::FindCharR(char c, int start) const throw ()
{
    int tLen = Length();
    for (register int i = min(start, tLen - 1); i >= 0; i--) {
	if (this->s[i] == c) return i;
    }
    return -1;
}

int Text::FindText(const Text &substr, int start) const throw ()
{
    start = max(start, 0);
    if (start + substr.Length() > this->Length()) return -1;
    const char *match = strstr(this->s + start, substr.s);
    if (match != (char *)NULL) return (match - this->s);
    return -1;
}

const int
  N = sizeof(Word),		// bytes per Word
  ByteBits = 8,			// bits per byte
  WordBits = N * 8;		// bits per Word

Word RotateWord(Word w, int b) throw ()
// Returns the result of rotating "w" up by "b" bits (or down by -b
// bits if b is negative).
{
    b = (b % WordBits);

    // We know that on big-endian systems this function will only be
    // called with negative values and on little-endian systems it
    // will only be called with positive values, so we optimize away
    // the unused clause.
#if BYTE_ORDER == BIG_ENDIAN
    if (b < 0)
      {
	b = -b;
	Word hiMask = (((Word) -1) << b);
	Word loMask = (((Word) -1) ^ hiMask);
	w = ((w & loMask) << (WordBits - b)) | ((w & hiMask) >> b);
      }
#else
    // else
    if (b > 0)
      {
	Word hiMask = (((Word) -1) << (WordBits - b));
	Word loMask = (((Word) -1) ^ hiMask);
	w = ((w & loMask) << b) | ((w & hiMask) >> (WordBits - b));
      }
#endif

    return w;
}

const int
  Up1 = ByteBits,		// bits per byte
  LgUp1 = 3;			// log_2(Up1)

Word Text::Hash() const throw ()
  // This function is based on the implementation in the file
  // "UnsafeHash.m3" that is part of the SRC Modula-3 distribution.
  // That file has a more complete description of the method.

  // Conceptually, this can be thought of as a faster version of this:

  //    Word res = 0;
  //    unsinged int N = sizeof(Word);
  //    char *resc = ((char *) res);
  //    for(unsigned int i = 0; i < strlen(this->s); i++)
  //    {
  //      res_c[i % N] = res_c[i % N] ^ this->s[i];
  //    }
  //    res += strlen(this->s);
  //    return res;

  // Although the result value of the code below will not be identical
  // (as it's affected by the byte-order of the host processor).

  // (A web search for "UnsafeHash.m3" should turn up the original.  I
  // considered simply reproducing the original description/proof
  // here, but that would undoubtedly cause license/copyright
  // issues. --KCS)
{
    const PointerInt modNmask = (Word)N - 1UL;
    Word temp = 0UL;
    const char *p = this->s;
    Word m = (Word)strlen(p);
    const char *endp = p + m;

#if defined(VALGRIND_SUPPORT)
    // Lower performance but completely safe, even as far as paranoid
    // run-time checkers like valgrind are concerned.
    char *temp_c = ((char *) &temp);
    for(unsigned int i = 0; i < m; i++)
      {
        temp_c[i % sizeof(temp)] ^= this->s[i];
      }
    return m + temp;
#else
    // process at most N-1 initial chars if "p" is not word-aligned
    PointerInt jpre = (PointerInt)p & modNmask, jpost;
    if ((jpre != 0) && (p != endp))
      {
        // jpost is the number of junk bytes in the first word after
        // the non-junk bytes.  (This is only non-zero for very short
        // strings.)
	jpost = max(0, N - jpre - m);

	// Get the Word-aligned memory that contains the start of the
	// string (and jpre preceeding junk bytes).
	temp = *((Word *)(p-jpre));

	// Shift temp to remove preceeding junk bytes and following
	// junk bytes (to handle the case of very short strings where
	// jpost > 0).
#if BYTE_ORDER == BIG_ENDIAN
	temp <<= (jpre << LgUp1);
	temp >>= ((jpost + jpre) << LgUp1);
#else
	temp >>= (jpre << LgUp1);
	temp <<= ((jpost + jpre) << LgUp1);
#endif

	// Skip forward to the next word-aligned chunk of the string
	// or the end of the string, whichever comes first.
	p += (N - jpre - jpost);
      }

    // process middle words
    while (p + N <= endp) {
	temp ^= *((Word *)p);
	p += N;
    }
    
    // process at most N-1 trailing chars
    if (p != endp)
      {
	// w is the last Word-aligned chunk of the string, including
	// some trailing junk characters.
	Word w = *((Word *)p);

	// jpost is the number of trailing junk characters in this
	// Word but not in the string.  jpostUp1 is the number of bits
	// in jpost bytes.
	jpost = N - (endp - p);
	Word jpostUp1 = (jpost << LgUp1);

	// Shift w to remove bytes past the end of the string and
	// thereby avoid including random garbage in the hash value.
	// Rotate temp by the same amount in the same direction as the
	// shift to align it with w.
#if BYTE_ORDER == BIG_ENDIAN
	w >>= jpostUp1;
	temp = RotateWord(temp, -jpostUp1);
#else
	w <<= jpostUp1;
	temp = RotateWord(temp, jpostUp1);
#endif

	// Combine w (now free of junk bytes) into the working hash
	// value.
	temp ^= w;
    }

    // Finally, we rotate the hash value by the same number of bytes
    // as the string length and add the string length to the result
    // (to ensure that strings which are an even number of identical
    // repitions of Word-sized sequences don't all get the same hash
    // value).
#if BYTE_ORDER == BIG_ENDIAN
    return m + RotateWord(temp, -(m << LgUp1));
#else
    return m + RotateWord(temp, (m << LgUp1));
#endif

#endif // SAFE_HASH
}

Text Text::WordWrap(const Text &prefix, unsigned int columns)
  const throw ()
{
  Text result = prefix;
  unsigned int line_len = result.Length();
  const char *read_p = s;

  // Until we run out of original...
  while(*read_p)
    {
      // Find the beginning and end of the next word.
      const char *next_word_start = read_p, *next_word_end;
      while(*next_word_start && isspace(*next_word_start))
	next_word_start++;
      next_word_end = next_word_start;
      while(*next_word_end && !isspace(*next_word_end))
	next_word_end++;

      // See if adding this word to the current like would put us over
      // the line length limit.
      if((line_len + (next_word_end - read_p)) > columns)
	{
	  // Replace the whitespace before this word with a newline,
	  // and update the line length counter.
	  unsigned int next_word_len = (next_word_end - next_word_start);
	  result += (Text("\n") + prefix +
		     Text(next_word_start, next_word_len));
	  line_len = prefix.Length() + next_word_len;
	}
      // If this would still be within the line length limit, just
      // copy them over.
      else
	{
	  unsigned int added_len = (next_word_end - read_p);
	  result += Text(read_p, added_len);
	  line_len += added_len;
	}

      // Regardless of what we did, move past the next word.
      read_p = next_word_end;
    }

  // Return the result.
  return result;
}

// Shared code for PadLeft and PadRight: computes the number of copies
// needed (including possibly a partial copy) and allocates the
// storage for the new string.

static void PaddingCopies(unsigned int baseLen,
			  unsigned int finalLen,
			  unsigned int padLen,
			  /*OUT*/ unsigned int &copies,
			  /*OUT*/ unsigned int &partial,
			  /*OUT*/ char *&dest)
{
  assert(padLen > 0);
  unsigned int needed = (finalLen > baseLen) ? (finalLen - baseLen) : 0;
  copies = needed/padLen;
  partial = needed - (copies*padLen);
  unsigned int totalLen = baseLen + (copies*padLen) + partial;
  assert(totalLen >= finalLen);
  dest = NEW_PTRFREE_ARRAY(char, totalLen+1);
  *dest = 0;
}

// Shared code for PadLeft and PadRight: copies the padding string
// multiple times (including possibly a final partial copy).

static void CopyPadding(unsigned int copies, unsigned int partial,
			char *dest, const Text &padding)
{
  while(copies > 0)
    {
      strcat(dest, padding.cchars());
      copies--;
    }
  if(partial)
    {
      strncat(dest, padding.cchars(), partial);
    }
}

Text Text::PadLeft(unsigned int toLen, const Text &padding)
  const throw()
{

  unsigned int copies, partial;
  char *dest;
  PaddingCopies(this->Length(), toLen, padding.Length(),
		copies, partial, dest);

  // Padding on the left
  CopyPadding(copies, partial, dest, padding);
  // Original on the right
  strcat(dest, this->s);

  Text res;
  res.s = dest;
  return res;
}

Text Text::PadRight(unsigned int toLen, const Text &padding)
  const throw()
{
  unsigned int copies, partial;
  char *dest;
  PaddingCopies(this->Length(), toLen, padding.Length(),
		copies, partial, dest);

  // Oritinal on the left
  strcat(dest, this->s);
  // Padding on the right
  CopyPadding(copies, partial, dest, padding);

  Text res;
  res.s = dest;
  return res;
}

#if !defined(HAVE_VSNPRINTF) && !defined(HAVE_VASPRINTF)
// If neither vasprintf(3) nor vsnprintf(3) are available, then we use
// vfprintf(3) to /dev/null to figure out the size.

static FILE *g_dev_null = 0;

extern "C" void Text_printf_dev_null_init()
{
  assert(g_dev_null == 0);
  g_dev_null = fopen("/dev/null", "w");
  assert(g_dev_null != 0);
}

static pthread_once_t open_once = PTHREAD_ONCE_INIT;
#endif

Text Text::printf(char *fmt, ...) throw()
{
  va_list args;
#if defined(HAVE_VASPRINTF)
  // Format the string into a heap block allocated by vasprintf(3)
  char *malloced_result = 0;
  va_start(args, fmt);
  int length = vasprintf(&malloced_result, fmt, args);
  va_end(args);

  // Unfortunately, we may be using a different memory allocator, so
  // we need to copy that result to a heap block allocated correctly.
  const char *result_str = StrCopy(malloced_result, length);
  free(malloced_result);

  Text result;
  result.s = result_str;
  return result;
#elif defined(HAVE_VSNPRINTF)
  // First use vsnprintf(3) with a small fixed buffer to get the
  // length of the formatted string.
  char tmp_buf[10];
  va_start(args, fmt);
  int length = vsnprintf(tmp_buf, sizeof(tmp_buf), fmt, args);
  va_end(args);

  // Now allocate a block of the correct size and use vsnprintf again.
  char *result_str = NEW_PTRFREE_ARRAY(char, length+1);
  va_start(args, fmt);
  int length2 = vsnprintf(result_str, length+1, fmt, args);
  va_end(args);
  assert(length2 == length);

  Text result;
  result.s = result_str;
  return result;
#else
  // Make sure /dev/null has been opened
  int once_result = pthread_once(&open_once, Text_printf_dev_null_init);
  assert(once_result == 0);
  assert(g_dev_null != 0);

  // Use vfprintf to /dev/null to get the length of the formatted
  // string
  static Basics::mutex dev_null_mu;
  va_start(args, fmt);
  dev_null_mu.lock();
  int length = vfprintf(g_dev_null, fmt, args);
  dev_null_mu.unlock();
  va_end(args);

  // Now allocate a block of the correct size and use vsprintf.
  char *result_str = NEW_PTRFREE_ARRAY(char, length+1);
  va_start(args, fmt);
  int length2 = vsprintf(result_str, fmt, args);
  va_end(args);
  assert(length2 == length);

  Text result;
  result.s = result_str;
  return result;
#endif
}
