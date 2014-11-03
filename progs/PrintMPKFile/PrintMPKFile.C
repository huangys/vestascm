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

// PrintMPKFile -- print the contents of a MultiPKFile

#include <Basics.H>
#include <FS.H>
#include <CacheArgs.H>
#include <CacheConfig.H>
#include <SMultiPKFileRep.H>
#include <PKPrefix.H>
#include <BufStream.H>

using std::ostream;
using std::ifstream;
using std::ofstream;
using std::cout;
using std::cerr;
using std::endl;
using std::hex;
using std::dec;
using Basics::OBufStream;

// This table type is used to store a mapping from common name index
// to fingerprint.  This is used for comparing the fingerprints of
// common names between CFP groups.
typedef Table<IntKey,FP::Tag>::Default CommonTagTbl;

static bool OpenFile(const Text fname, /*OUT*/ ifstream &ifs)
  throw (FS::Failure)
/* Open the file named "fname" for reading. If successful, set "ifs" to a
   stream on the file and return true. If the file does not exist, print an
   error message and return false. If any other error occurs, throw
   "FS::Failure".
*/
{
    try {
	FS::OpenReadOnly(fname, ifs);
    }
    catch (FS::DoesNotExist) {
	cerr << "Error: unable to open '" << fname << "' for reading." << endl;
	return false;
    }
    return true;
}

static void Banner(ostream &os, char c, int num) throw ()
{
    os << "// ";
    for (int i = 0; i < num; i++) os << c;
    os << endl;
}

static void PrintFile(ostream &os, const char *nm, bool verbose)
  throw (SMultiPKFileRep::BadMPKFile, FS::EndOfFile, FS::Failure)
/* Open and read the MultiPKFile named by the file "nm", and print out an
   ASCII representation of it to "os". If "nm" is a relative path, first
   prepend the pathname of the cache server's stable cache. Print out a
   verbose version if "verbose" is true. */
{
    // form the filename
    Text fname(nm);
    if (nm[0] != '/') {
	fname = Config_SCachePath + '/' + fname;
    }

    // open the file
    ifstream ifs;
    if (!OpenFile(fname, /*OUT*/ ifs)) return;

    try {
	// read it (including the PKFiles)
	SMultiPKFileRep::Header hdr(ifs);
	hdr.ReadEntries(ifs);
	hdr.ReadPKFiles(ifs);
	FS::Close(ifs);

	// print it
	Banner(os, '#', 74);
	os << "// <MultiPKFile> (" << fname << ")" << endl;
	Banner(os, '#', 74);
	hdr.Debug(os, verbose);
	for (int i = 0; i < hdr.num; i++) {
	    SMultiPKFileRep::HeaderEntry *he;
	    bool inTbl = hdr.pkTbl.Get(*(hdr.pkSeq[i]), he);
	    assert(inTbl);

	    // write PKFile info
	    if (he->pkfile != (SPKFile *)NULL) {
		he->pkfile->Debug(os, *(he->pkhdr), he->offset, verbose);
	    }
	}
	os.flush();
    } catch (...) {
	FS::Close(ifs);
	throw;
    }
}

// Quote HTML metacharacters ('<', '>', '"', '&') in the given text
// string.
Text HTMLQuote(const Text &original)
{
  Text result;
  int origLen = original.Length(), doneCount = 0;
  while(doneCount < origLen)
    {
      int ltPos = original.FindChar('<', doneCount);
      int gtPos = original.FindChar('>', doneCount);
      int quotPos = original.FindChar('"', doneCount);
      int ampPos = original.FindChar('&', doneCount);

      // Earliest character needing quoting is a less-than
      if((ltPos >= 0) &&
	 ((gtPos == -1) || (gtPos > ltPos)) &&
	 ((quotPos == -1) || (quotPos > ltPos)) &&
	 ((ampPos == -1) || (ampPos > ltPos)))
	{
	  // Copy everything up to the less-than
	  result += original.Sub(doneCount, ltPos - doneCount);
	  // Insert the quoted less-than.
	  result += "&lt;";
	  // Advance the doneCount;
	  doneCount = ltPos+1;
	}
      // Earliest character needing quoting is a greater-than
      else if((gtPos >= 0) &&
	      ((ltPos == -1) || (ltPos > gtPos)) &&
	      ((quotPos == -1) || (quotPos > gtPos)) &&
	      ((ampPos == -1) || (ampPos > gtPos)))
	{
	  // Copy everything up to the greater-than
	  result += original.Sub(doneCount, gtPos - doneCount);
	  // Insert the quoted greater-than.
	  result += "&gt;";
	  // Advance the doneCount;
	  doneCount = gtPos+1;
	}
      // Earliest character needing quoting is a double quote
      else if((quotPos >= 0) &&
	      ((ltPos == -1) || (ltPos > quotPos)) &&
	      ((gtPos == -1) || (gtPos > quotPos)) &&
	      ((ampPos == -1) || (ampPos > quotPos)))
	{
	  // Copy everything up to the double quote
	  result += original.Sub(doneCount, quotPos - doneCount);
	  // Insert the quoted double quote.
	  result += "&quot;";
	  // Advance the doneCount;
	  doneCount = quotPos+1;
	}
      // Earliest character needing quoting is an ampersand
      else if((ampPos >= 0) &&
	      ((ltPos == -1) || (ltPos > ampPos)) &&
	      ((gtPos == -1) || (gtPos > ampPos)) &&
	      ((quotPos == -1) || (quotPos > ampPos)))
	{
	  // Copy everything up to the ampersand
	  result += original.Sub(doneCount, ampPos - doneCount);
	  // Insert the quoted ampersand.
	  result += "&amp;";
	  // Advance the doneCount;
	  doneCount = ampPos+1;
	}
      // No characters needing quoting found: copy the remainder of
      // the string to the result.
      else
	{
	  assert((ltPos == -1) && (gtPos == -1) &&
		 (quotPos == -1) && (ampPos == -1));
	  result += original.Sub(doneCount);
	  doneCount = origLen;
	}
    }
  return result;
}

// Return an indentation string for a particular level of indentation.
static Text Indent(unsigned int level)
{
  // This holds indentation strings for different levels of
  // indentation
  static TextSeq l_IndentTexts;

  // Add indentation strings up to the level requested
  while(level >= l_IndentTexts.size())
    {
      unsigned int new_level = l_IndentTexts.size();
      // Note: each level of indentation is 2 spaces.
      unsigned int new_len = new_level*2;
      char *indet_str = NEW_PTRFREE_ARRAY(char, new_len+1);
      unsigned int i = 0;
      while(i < new_len)
	{
	  indet_str[i++] = ' ';
	}
      indet_str[new_len] = 0;
      l_IndentTexts.addhi(Text(indet_str, indet_str));
    }

  // Return the indentation string for the requested level.
  return l_IndentTexts.get(level);
}

extern "C"
{
  // Compare two paths of two free variables.  Used with qsort(3) to
  // sort lists of free variables by path to make them easier to read.
  static int comparePaths(const void *a, const void *b)
  {
    // Do pointer conversion to get at the Text data.
    Text *ta = *((Text **) a), *tb = *((Text **) b);

    // Check to see if one of the paths starts with "./" and the other
    // doesn't.  Sort dot paths after non-dot paths.
    bool a_dot = (((*ta)[2] == '.') && ((*ta)[3] == '/'));
    bool b_dot = (((*tb)[2] == '.') && ((*tb)[3] == '/'));
    if(a_dot && !b_dot)
      {
	return 1;
      }
    else if(b_dot && !a_dot)
      {
	return -1;
      }

    // If both or neither path starts with "./", compare them lexically.
    return strcmp(ta->cchars() + 2, tb->cchars() + 2);
  }
}

// Print an HTML table show a set of free variables.
static void HTMLFVTable(ostream &os,
			unsigned int indent_level,
			const FV::List *names,
			const BitVector *nameSet,
			bool columnHeaders = true)
{
  // Gather up the names into a separate array so that we can sort
  // them by path.
  unsigned int nameTotal = nameSet->Cardinality(), nameCount = 0;
  Text **nameArray = NEW_ARRAY(Text *, nameTotal);
  BVIter namesIter(*nameSet);
  unsigned int nameIndex;
  while(namesIter.Next(nameIndex))
    {
      nameArray[nameCount++] = &(names->name[nameIndex]);
    }
  assert(nameCount == nameTotal);

  // Sort the names by path.
  qsort(nameArray, nameTotal, sizeof(Text *), comparePaths);

  // Print the sorted names in a table
  os << Indent(indent_level++) << "<table border>" << endl;
  if(columnHeaders)
    {
      os << Indent(indent_level++) << "<tr>" << endl
	 << Indent(indent_level) << "<th>Type</th>" << endl
	 << Indent(indent_level) << "<th>Path</th>" << endl
	 << Indent(--indent_level) << "</tr>" << endl;
    }
  for(nameCount = 0; nameCount < nameTotal; nameCount++)
    {
      Text *name = nameArray[nameCount];
      char type = (*name)[0];
      Text quotedPath = HTMLQuote(name->Sub(2));
      os << Indent(indent_level++) << "<tr>" << endl
	 << Indent(indent_level) << "<td><tt>" << HTMLQuote(type) << "</tt>";
      // For those dependency types the evaluator uses, add a not
      // about the meaning of the dependency
      switch(type)
	{
	case 'N':
	  os << " (value)";
	  break;
	case '!':
	  os << " (existence)";
	  break;
	case 'T':
	  os << " (type)";
	  break;
	case 'L':
	  os << " (list length)";
	  break;
	case 'B':
	  os << " (binding names)";
	  break;
	case 'E':
	  os << " (expression)";
	  break;
	}
      os << "</td>" << endl
	 << Indent(indent_level) << "<td><tt>" << quotedPath << "</tt></td>"
	 << endl
	 << Indent(--indent_level) << "</tr>" << endl;;
    }
  os << Indent(--indent_level) << "</table>" << endl;
}

// Print the standard beginning of an HTML file
static unsigned int StartHTML(ostream &os, const Text &title)
{
  os << "<html>" << endl
     << "  <head>" << endl
     << "    <title>" << HTMLQuote(title) << "</title>" << endl
     << "  </head>" << endl
     << "  <body>" << endl;

  // Return the initial indent level
  return 2;
}

// Print the standard end of an HTML file
static void EndHTML(ostream &os)
{
  os << "  </body>" << endl
     << "</html>" << endl;
}

static Text ExpandURLPattern(const Text &pattern,
			     PKPrefix::T pfx,
			     FP::Tag *pk = 0,
			     FV::Epoch *namesEpoch = 0,
			     FP::Tag *cfp = 0,
			     CacheEntry::Index *ci = 0)
{
  Text result;

  // Text string used to identify which kind of URL pattern this is.
  // Determined by the presence of certain arguments.
  const char *pattern_kind = "MultiPKFile";
  if(ci != 0)
    {
      pattern_kind = "cache entry";
    }
  else if(cfp != 0)
    {
      pattern_kind = "common fingerprint group";
    }
  else if(pk != 0)
    {
      pattern_kind = "PKFile";
    }

  int patternLen = pattern.Length(), doneCount = 0;
  bool
    pfx_included = false,
    pk_included = false,
    cfp_included = false,
    index_included = false;
  while(doneCount < patternLen)
    {
      int pctPos = pattern.FindChar('%', doneCount);      

      if(pctPos >= 0)
	{
	  // Copy everything up to the percent sign
	  result += pattern.Sub(doneCount, pctPos - doneCount);

	  switch (pattern[pctPos+1])
	    {
	    case 'm':
	      // %m -> PKPrefix
	      result += pfx.Pathname(PKPrefix::Granularity(),
				     PKPrefix::Granularity());
	      pfx_included = true;
	      break;
	    case 'p':
	      // %p -> PK with _ between words
	      if(pk != 0)
		{
		  OBufStream pk_str;
		  pk->Print(pk_str, "_");
		  result += pk_str.str();
		  pk_included = true;
		}
	      else
		{
		  cerr << "Fatal error: %p ([p]rimary key) not valid in "
		       << pattern_kind << " URL pattern." << endl
		       << "\t" << pattern << endl;
		  exit(2);
		}
	      break;
	    case 'n':
	      // %n -> namesEpoch
	      if(namesEpoch != 0)
		{
		  OBufStream namesEpoch_str;
		  namesEpoch_str << *namesEpoch;
		  result += namesEpoch_str.str();
		  index_included = true;
		}
	      else
		{
		  cerr << "Fatal error: %n ([n]amesEpoch) not valid in "
		       << pattern_kind << " URL pattern." << endl
		       << "\t" << pattern << endl;
		  exit(2);
		}
	      break;
	    case 'c':
	      // %c -> CFP with _ between words
	      if(cfp != 0)
		{
		  OBufStream cfp_str;
		  cfp->Print(cfp_str, "_");
		  result += cfp_str.str();
		  cfp_included = true;
		}
	      else
		{
		  cerr << ("Fatal error: %c ([c]ommon fingerprint) not "
			   "valid in ")
		       << pattern_kind
		       << " URL pattern." << endl
		       << "\t" << pattern << endl;
		  exit(2);
		}
	      break;
	    case 'i':
	      // %i -> cache index
	      if(ci != 0)
		{
		  OBufStream ci_str;
		  ci_str << *ci;
		  result += ci_str.str();
		  index_included = true;
		}
	      else
		{
		  cerr << "Fatal error: %i (cache [i]ndex) not valid in "
		       << pattern_kind
		       << " URL pattern." << endl
		       << "\t" << pattern << endl;
		  exit(2);
		}
	      break;
	    case '%':
	      // %% -> %
	      result += "%";
	      break;
	    defrault:
	      // % followed by any other character will be left
	      // unmodified.
	      result += pattern.Sub(pctPos, 2);
	      break;
	    }

	  // Advance doneCount
	  doneCount = pctPos+2;
	}
      // No more escape sequences: copy the remainder of the pattern
      // to the result.
      else
	{
	  result += pattern.Sub(doneCount);
	  doneCount = patternLen;
	}
    }

  // Make sure we included enough information to be able to identify
  // whatever the URL is for.
  if(ci != 0)
    {
      // CI provided means this is a URL for a cache entry
      if(!pk_included && !pfx_included)
	{
	  cerr << "Fatal error: " << pattern_kind
	       << (" URL pattern contains neither %p ([p]rimary key) "
		   "nor %m (PK prefix identifying [M]ultiPKFile)!")
	       << endl
	       << "\t" << pattern << endl;
	  exit(2);
	}

      if(!index_included)
	{
	  cerr << "Fatal error: " << pattern_kind
	       << " URL pattern doesn't contain %i (cache [i]ndex)!"
	       << endl
	       << "\t" << pattern << endl;
	  exit(2);
	}
    }
  else if(cfp != 0)
    {
      // CFP provided means this is a URL for a CFP group
      if(!pk_included)
	{
	  cerr << "Fatal error: " << pattern_kind
	       << " URL pattern doesn't contain %p ([p]rimary key)!" << endl
	       << "\t" << pattern << endl;
	  exit(2);
	}
      
      if(!cfp_included)
	{
	  cerr << "Fatal error: " << pattern_kind
	       << " URL pattern doesn't contain %c ([c]ommon fingerprint)!"
	       << endl
	       << "\t" << pattern << endl;
	  exit(2);
	}
    }
  else if(pk != 0)
    {
      // PK included means this is a URL for a PKFile
      if(!pk_included)
	{
	  cerr << "Fatal error: " << pattern_kind
	       << " URL pattern doesn't contain %p ([p]rimary key)!" << endl
	       << "\t" << pattern << endl;
	  exit(2);
	}
    }
  else
    {
      // Otherwise, it must be for a MultiPKFile
      if(!pfx_included)
	{
	  cerr << "Fatal error: " << pattern_kind
	       << (" URL pattern doesn't contain "
		   "%m (PK prefix identifying [M]ultiPKFile)!") << endl
	       << "\t" << pattern << endl;
	  exit(2);
	}
    }

  return result;
}

// Generate an HTML filename for a specific MultiPKFile
static Text MPKFileHTMLFileName(PKPrefix::T pfx)
{
  OBufStream l_name;
  l_name << "mpkfile_"
	 << pfx.Pathname(PKPrefix::Granularity(), PKPrefix::Granularity())
	 << ".html";
  return Text(l_name.str());
}

static Text g_MPKFileURL_pattern;
static bool g_MPKFileURL_pattern_set = false;

static Text MPKFileURL(PKPrefix::T pfx)
{
  if(!g_MPKFileURL_pattern_set)
    {
      return MPKFileHTMLFileName(pfx);
    }

  return ExpandURLPattern(g_MPKFileURL_pattern, pfx);
}

// Generate an HTML filename for a specific PKFile
static Text PKFileHTMLFileName(FP::Tag pk)
{
  OBufStream l_name;
  l_name << "pkfile_";
  pk.Print(l_name, "_");
  l_name << ".html";
  return Text(l_name.str());
}

static Text g_PKFileURL_pattern;
static bool g_PKFileURL_pattern_set = false;

static Text PKFileURL(FP::Tag pk)
{
  if(!g_PKFileURL_pattern_set)
    {
      return PKFileHTMLFileName(pk);
    }

  return ExpandURLPattern(g_PKFileURL_pattern, PKPrefix::T(pk), &pk);
}

// Generate an HTML filename for a specific common fingerprint group
static Text CFPGroupHTMLFileName(FP::Tag pk, FP::Tag cfp)
{
  OBufStream l_name;
  l_name << "pkfile_";
  pk.Print(l_name, "_");
  l_name << "_cfp_";
  cfp.Print(l_name, "_");
  l_name << ".html";
  return Text(l_name.str());
}

static Text g_CFPGroupURL_pattern;
static bool g_CFPGroupURL_pattern_set = false;

static Text CFPGroupURL(FP::Tag pk, FV::Epoch namesEpoch, FP::Tag cfp)
{
  if(!g_CFPGroupURL_pattern_set)
    {
      return CFPGroupHTMLFileName(pk, cfp);
    }

  return ExpandURLPattern(g_CFPGroupURL_pattern,
			  PKPrefix::T(pk), &pk,
			  &namesEpoch, &cfp);
}

// Generate an HTML filename for a specific cache entry
static Text CacheEntryHTMLFileName(CacheEntry::Index ci)
{
  OBufStream l_name;
  l_name << "ci_" << ci << ".html";
  return Text(l_name.str());
}

static Text g_CacheEntryURL_pattern;
static bool g_CacheEntryURL_pattern_set = false;

static Text CacheEntryURL(FP::Tag pk, CacheEntry::Index ci)
{
  if(!g_CacheEntryURL_pattern_set)
    {
      return CacheEntryHTMLFileName(ci);
    }

  return ExpandURLPattern(g_CacheEntryURL_pattern,
			  PKPrefix::T(pk), &pk,
			  // Note: no namesEpoch or CFP
			  0, 0,
			  &ci);
}

// Combine a directory and a relative path below it, adding a path
// separator if neccessary.
static Text CombinePath(const Text &dir, const Text &tail)
{
  OBufStream l_result;
  l_result << dir;
  if(dir[dir.Length() - 1] != PathnameSep)
    {
      l_result << PathnameSep;
    }
  l_result << tail;
  return Text(l_result.str());
}

// Each level will create one of these to rpresent its state.  This
// primarily it easier to pass this state down to lower levels of the
// HTML generation for adding index entries at higher levels.
struct HTMLFileInfo
{
  // Title of this HTML file, used for 
  Text title;

  // Filename of this HTML file
  Text fname;

  // URL used when referencing this page
  Text url;

  // The stream used for writing this THML file
  ofstream out;

  // Current indent level in this HTML file
  unsigned int indent_level;

  // Get the current level of indentation
  inline Text indent()
  {
    return Indent(this->indent_level);
  }
  // Get the current level of indentation and then increase it
  inline Text indentOpen()
  {
    return Indent((this->indent_level)++);
  }
  // Decrease the level of indentation and then get the new level of
  // indentation
  inline Text indentClose()
  {
    return Indent(--(this->indent_level));
  }
};

// Generate an HTML file representation of a cache entry
static void PrintCacheEntryHTML(const Text &out_dir, bool print_fnames,
				// MultiPKFile HTML state
				HTMLFileInfo &mpk_html,
				// PKFile HTML state
				HTMLFileInfo &pkfile_html,
				// CFP group HTML state
				HTMLFileInfo &cfp_html,
				// The header entry for our PKFile
				SMultiPKFileRep::HeaderEntry *he,
				// The cache entry to be printed
				CE::T *entry,
				// Set of uncommon names that are
				// common to all entries in the same
				// CFP group as this entry
				BitVector &uncommonCommon,
				bool uncommonCommon_empty)
  throw (FS::EndOfFile, FS::Failure)
{
  HTMLFileInfo ce_html;

  // Determine the HTML filename and open it
  ce_html.fname = CacheEntryHTMLFileName(entry->CI());
  FS::OpenForWriting(CombinePath(out_dir, ce_html.fname), ce_html.out);
  if(print_fnames)
    cout << "Writing " << ce_html.fname << endl;

  // Set the URL for this page
  ce_html.url = CacheEntryURL(he->pk, entry->CI());

  // Set the title for the document
  {
    OBufStream l_title;
    l_title << "Cache entry, index = " << entry->CI();
    ce_html.title = l_title.str();
  }

  // Print the HTML opener
  ce_html.indent_level = StartHTML(ce_html.out, ce_html.title);

  // Add an index entry for this cache entry up in the MultiPKFile,
  // PKFile, and CFP group HTML
  mpk_html.out
    << mpk_html.indent()
    << "<li><a href=\"" << ce_html.url << "\">" << ce_html.title
    << "</a></li>" << endl;
  pkfile_html.out
    << pkfile_html.indent()
    << "<li><a href=\"" << ce_html.url << "\">" << ce_html.title
    << "</a></li>" << endl;
  cfp_html.out
    << cfp_html.indent()
    << "<li><a href=\"" << ce_html.url << "\">" << ce_html.title
    << "</a></li>" << endl;

  ce_html.out
    // Hyperlinks up
    << ce_html.indent() << "Up: <a href=\"" << mpk_html.url
    << "\">" << mpk_html.title << "</a><br>" << endl
    << ce_html.indent() << "Up: <a href=\"" << pkfile_html.url
    << "\">" << pkfile_html.title << "</a><br>" << endl
    << ce_html.indent() << "Up: <a href=\"" << cfp_html.url
    << "\">" << cfp_html.title << "</a><br>" << endl
    << ce_html.indent() << "<hr>" << endl
    // Repeat the title in a section header
    << ce_html.indent() << "<h1>" << ce_html.title << "</h1>" << endl;

  // If this is the only entry in this CFP group, just print its
  // uncommon FVs.
  if(uncommonCommon_empty)
    {
      if(entry->UncommonNames()->IsEmpty())
	{
	  ce_html.out
	    << ce_html.indent() << "<p>No uncommon secondary dependencies.</p>"
	    << endl;
	}
      else
	{
	  ce_html.out
	    << ce_html.indent() << "<p>Uncommon secondary dependencies ("
	    << entry->UncommonNames()->Cardinality()
	    << " total):</p>"
	    << endl;
	  HTMLFVTable(ce_html.out, ce_html.indent_level,
		      he->pkfile->AllNames(), entry->UncommonNames());
	}
    }
  else
    {
      // Find and print the set of uncommon names that only this entry
      // uses.
      BitVector uncommonUnique(entry->UncommonNames());
      uncommonUnique -= uncommonCommon;

      if(uncommonUnique.IsEmpty())
	{
	  ce_html.out
	    << ce_html.indent()
	    << "<p>There are no uncommon secondary "
	    << "dependencies used by this entry but not "
	    << "others in its CFP group.</p>" << endl;
	}
      else
	{
	  ce_html.out
	    << ce_html.indent()
	    << "<p>Uncommon secondary dependencies used "
	    << "by this entry but not others in its CFP "
	    << "group ("
	    << uncommonUnique.Cardinality()
	    << " total):</p>" << endl;
	  HTMLFVTable(ce_html.out, ce_html.indent_level,
		      he->pkfile->AllNames(), &uncommonUnique);
	}
    }

  // Child cache entries
  if(entry->Kids()->len == 0)
    {
      ce_html.out
	<< ce_html.indent() << "<p>No child cache entries.</p>" << endl;
    }
  else
    {
      ce_html.out
	<< ce_html.indent() << "<p>Child cache entries ("
	<< entry->Kids()->len
	<< " total):</p>" << endl
	<< ce_html.indentOpen()
	<< "<ul>" << endl;
      for(int kid_i = 0; kid_i < entry->Kids()->len; kid_i++)
	{
	  CacheEntry::Index kid = entry->Kids()->index[kid_i];
	  ce_html.out
	    << ce_html.indent()
	    << "<li>" << kid << "</li>" << endl;
	}
      ce_html.out << ce_html.indentClose() << "</ul>" << endl;
    }
  
  // Derived indecies
  const VestaVal::T *value = entry->Value();
  if(value->dis.len == 0)
    {
      ce_html.out
	<< ce_html.indent() << "<p>No result files.</p>" << endl;
    }
  else
    {
      ce_html.out
	<< ce_html.indent() << "<p>Result files (" << value->dis.len
	<< " total):</p>" << endl
	<< ce_html.indentOpen() << "<ul>" << endl << hex;
      for(int di_i = 0; di_i < value->dis.len; di_i++)
	{
	  ce_html.out
	    << ce_html.indent() << "<li>";
	  ShortId di = value->dis.index[di_i];
	  bool anchored = false;
	  if(!SourceOrDerived::dirShortId(di))
	    {
	      char *di_path =
		SourceOrDerived::shortIdToName(di, false);
	      ce_html.out << "<a href=\"file://" << di_path
			  << "\">";
	      anchored = true;
	    }
	  ce_html.out << "0x" << di;
	  if(anchored)
	    {
	      ce_html.out << "</a>";
	    }
	  ce_html.out << "</li>" << endl;
	}
      ce_html.out << dec << ce_html.indentClose() << "</ul>" << endl;
    }

  // Value size
  ce_html.out
    << ce_html.indent() << "<p>Stored value size: " << value->len
    << " bytes.</p>" << endl;

  // Value PrefixTbl size
  ce_html.out
    << ce_html.indent() << "<p>Value PrefixTbl size: "
    << value->prefixTbl.NumArcs() << " entries.";
  // If the size of this PrexiTbl seems overly
  // large, include a message about its maximum
  // possible size.
  if(value->prefixTbl.NumArcs() > (PrefixTbl::endMarker / 2))
    {
      ce_html.out
	<< ce_html.indent() << "  (Note: the maximum possible size is "
	<< PrefixTbl::endMarker
	<< (" entries.  The size of the PrefixTbl is "
	    "affected by the size of the entry's result value and the "
	    "number of distinct dependency paths recorded in the "
	    "entry's result value.)");
    }
  ce_html.out << "</p>" << endl;

  // Finish the HTML, flush and close the file.
  EndHTML(ce_html.out);
  ce_html.out.flush();
  ce_html.out.close();
}

// Generate an HTML file representation of a common fingerprint group.
// Also, call PrintCacheEntryHTML to generate HTML files for the cache
// entries in this CFP group.
static void PrintCFPGroupHTML(const Text &out_dir, bool print_fnames,
			      // MultiPKFile HTML state
			      HTMLFileInfo &mpk_html,
			      // PKFile HTML state
			      HTMLFileInfo &pkfile_html,
			      // The MultPKFile header entry for our PKFile
			      SMultiPKFileRep::HeaderEntry *he,
			      // Our index within the PKFile
			      unsigned int cfp_index,
			      // The fingerprints of the common names
			      // for all CFP groups in this PKFile
			      CommonTagTbl *commonTags)
  throw (FS::EndOfFile, FS::Failure)
{
  HTMLFileInfo cfp_html;

  FP::Tag cfp = he->pkhdr->entry[cfp_index].cfp;

  // Determine the HTML filename and open it
  cfp_html.fname = CFPGroupHTMLFileName(he->pk, cfp);
  FS::OpenForWriting(CombinePath(out_dir, cfp_html.fname), cfp_html.out);
  if(print_fnames)
    cout << "Writing " << cfp_html.fname << endl;

  // Set the URL for this page
  cfp_html.url = CFPGroupURL(he->pk, he->pkfile->NamesEpoch(), cfp);

  // Set the title for the document
  {
    OBufStream l_title;
    l_title << "Common fingerprint Group " << cfp_index << ": " << cfp;
    cfp_html.title = l_title.str();
  }

  // Print the HTML opener
  cfp_html.indent_level = StartHTML(cfp_html.out, cfp_html.title);

  // Add an index entry for this CFP group up in the MultiPKFile and
  // PKFile HTML
  mpk_html.out
    << mpk_html.indent()
    << "<li><a href=\"" << cfp_html.url << "\">" << cfp_html.title
    << "</a></li>" << endl;
  pkfile_html.out
    << pkfile_html.indent()
    << "<li><a href=\"" << cfp_html.url << "\">" << cfp_html.title
    << "</a></li>" << endl;

  cfp_html.out
    // Hyperlink up the MultiPKFile
    << cfp_html.indent() << "Up: <a href=\"" << mpk_html.url
    << "\">" << mpk_html.title << "</a><br>" << endl
    // Hyperlink up the PKFile
    << cfp_html.indent() << "Up: <a href=\"" << pkfile_html.url
    << "\">" << pkfile_html.title << "</a><br>" << endl
    << cfp_html.indent() << "<hr>" << endl
    // Repeat the title in a section header
    << cfp_html.indent() << "<h1>" << cfp_html.title << "</h1>" << endl;

  // Determine and display which common names have different
  // fingerprintsin other CFP groups
  if(he->pkhdr->num > 1)
    {
      cfp_html.out
	<< cfp_html.indent()
	<< "<p>Common secondary dependencies with different "
	<< "fingerprints (values) in other CFP groups:</p>" << endl
	<< cfp_html.indentOpen() << "<table border>" << endl
	<< cfp_html.indentOpen() << "<tr>" << endl
	<< cfp_html.indent() << "<th>CFP Group</th>" << endl
	<< cfp_html.indent() << "<th>Differing dependencies</th>" << endl
	<< cfp_html.indentClose() << "</tr>" << endl;

      // Compare the fingerprints of the commonNames for this CFP with
      // that of each other CFP group, printing a table which shows
      // which names have different values.
      for(int cfp_j = 0; cfp_j < he->pkhdr->num; cfp_j++)
	{
	  if(cfp_index == cfp_j)
	    continue;
	  cfp_html.out
	    << cfp_html.indentOpen() << "<tr>" << endl
	    << cfp_html.indentOpen() << "<td>" << endl
	    << cfp_html.indent() << cfp_j << "<br>" << endl
	    << cfp_html.indent() << he->pkhdr->entry[cfp_j].cfp << endl
	    << cfp_html.indentClose() << "</td>" << endl
	    << cfp_html.indentOpen() << "<td>" << endl;

	  // Loop over the common names and find the set of differing
	  // commonNames.
	  BitVector commonDiff;
	  BVIter commonIter(*(he->pkfile->CommonNames()));
	  unsigned int commonIndex;
	  while(commonIter.Next(commonIndex))
	    {
	      // Get the fingerprint of this name in this CFP group
	      // and the other CFP group.
	      FP::Tag myFP, theirFP;
	      bool inTbl = commonTags[cfp_index].Get(commonIndex,
						     /*OUT*/myFP);
	      assert(inTbl);
	      inTbl = commonTags[cfp_j].Get(commonIndex,
					    /*OUT*/theirFP);
	      assert(inTbl);

	      // Remember if they're different
	      if(myFP != theirFP)
		{
		  commonDiff.Set(commonIndex);
		}
	    }

	  // Print a table showing the differing dependencies
	  cfp_html.out
	    << cfp_html.indent() << commonDiff.Cardinality()
	    << " total:" << endl;
	  HTMLFVTable(cfp_html.out, cfp_html.indent_level,
		      he->pkfile->AllNames(), &commonDiff);

	  // Close this table cell and row
	  cfp_html.out << cfp_html.indentClose() << "</td>" << endl
		       << cfp_html.indentClose() << "</tr>" << endl;
	}
      // Close the table
      cfp_html.out << cfp_html.indentClose() << "</table>" << endl;

    }
  
  // Get the list of cache entries in this CFP group
  CE::List *list;
  bool inTbl = he->pkfile->OldEntries()->Get(cfp, /*OUT*/ list);
  assert(inTbl);
  unsigned int entry_count = 1;

  // Used to find the set on uncommon names common to all entries
  // in this CFP group (if there's more than one entry).
  BitVector uncommonCommon;
  bool uncommonCommon_empty = true;

  // If there's more than one entry in this CFP group...
  if(list->Tail() != 0)
    {
      // Loop over the entries in this CFP group
      CE::List *temp_list = list;
      while(temp_list != 0)
	{
	  CE::T *entry = temp_list->Head();
	  assert(entry != 0);

	  // uncommonCommon becomes the intersection of the
	  // uncommonNames of all entries in thie CFP group
	  if(uncommonCommon_empty)
	    {
	      uncommonCommon = *(entry->UncommonNames());
	      uncommonCommon_empty = false;
	    }
	  else
	    {
	      uncommonCommon &= *(entry->UncommonNames());
	    }

	  temp_list = temp_list->Tail();

	  // Count entries while we do this
	  entry_count++;
	}

      // If there were any uncommon names common across all
      // entries in thie CFP group, add a table showing them.
      unsigned int uncommonCommon_count = uncommonCommon.Cardinality();
      if(uncommonCommon_count > 0)
	{
	  cfp_html.out
	    << cfp_html.indent()
	    << "<p>Uncommon secondary dependencies used by all "
	    << "entries in this CFP group (" << uncommonCommon_count
	    << " total):</p>" << endl;
	  HTMLFVTable(cfp_html.out, cfp_html.indent_level,
		      he->pkfile->AllNames(), &uncommonCommon);
	}
      else
	{
	  cfp_html.out
	    << cfp_html.indent()
	    << "<p>There are no uncommon secondary dependencies common to all "
	    << "entries in this CFP group:</p>" << endl;
	  uncommonCommon_empty = true;
	}
    }

  // Start index list for cache entries in our HTML file and the
  // MultiPKFile and PKFile HTML files.
  cfp_html.out
    << cfp_html.indent() << "<p>Cache entries (" << entry_count
    << " total):</p>" << endl
    << cfp_html.indentOpen() << "<ul>" << endl;
  pkfile_html.out 
    << pkfile_html.indent() << "Cache entries (" << entry_count
    << " total):" << endl
    << pkfile_html.indentOpen() << "<ul>" << endl;
  mpk_html.out
    << mpk_html.indent() << "Cache entries (" << entry_count
    << " total):" << endl
    << mpk_html.indentOpen() << "<ul>" << endl;

  // Now print each entry
  while(list != 0)
    {
      CE::T *entry = list->Head();
      assert(entry != 0);

      PrintCacheEntryHTML(out_dir, print_fnames,
			  mpk_html, pkfile_html, cfp_html,
			  he, entry, uncommonCommon, uncommonCommon_empty);

      list = list->Tail();
    }

  // Close index lists for cache entries
  cfp_html.out << cfp_html.indentClose() << "</ul>" << endl;
  pkfile_html.out << pkfile_html.indentClose() << "</ul>" << endl;
  mpk_html.out << mpk_html.indentClose() << "</ul>" << endl;

  // Finish the HTML, flush and close the file.
  EndHTML(cfp_html.out);
  cfp_html.out.flush();
  cfp_html.out.close();
}

// Generate an HTML file representation of a PKFile.  Also, call
// PrintCFPGroupHTML to generate HTML files for the common fingerprint
// groups in this PKFile.
static void PrintPKFileHTML(const Text &out_dir, bool print_fnames,
			    // MultiPKFile HTML state
			    HTMLFileInfo &mpk_html,
			    // Our index within the MultiPKFile
			    unsigned int pkfile_index,
			    // The MPKFile header entry for this PKFile
			    SMultiPKFileRep::HeaderEntry *he)
  throw (FS::EndOfFile, FS::Failure)
{
  HTMLFileInfo pkfile_html;

  // Determine the HTML filename and open it
  pkfile_html.fname = PKFileHTMLFileName(he->pk);
  FS::OpenForWriting(CombinePath(out_dir, pkfile_html.fname), pkfile_html.out);
  if(print_fnames)
    cout << "Writing " << pkfile_html.fname << endl;

  // Set the URL for this page
  pkfile_html.url = PKFileURL(he->pk);

  // Set the title for the document
  {
    OBufStream l_title;
    l_title << "PKFile " << pkfile_index << ": " << he->pk;
    pkfile_html.title = l_title.str();
  }

  // Print the HTML opener
  pkfile_html.indent_level = StartHTML(pkfile_html.out, pkfile_html.title);

  // Put together an HTML bit for this PKFile's sourceFunc, even in
  // the case where it's not set.
  Text sourceFunc = ((he->pkfile->SourceFunc() == 0)
		     ? Text("<i>UNKNOWN</i>")
		     : (Text("<tt>")+
			HTMLQuote(*(he->pkfile->SourceFunc()))+
			"</tt>"));

  // Add an index entry for this PKFile up in the MultiPKFile HTML
  mpk_html.out
    << mpk_html.indent()
    << "<li><a href=\"" << pkfile_html.url << "\">" << pkfile_html.title
    << "</a><br>" << endl;
  mpk_html.out
    << mpk_html.indent() << "Function: " << sourceFunc << "</li>" << endl;

  pkfile_html.out
    // Hyperlink up the MultiPKFile
    << pkfile_html.indent() << "Up: <a href=\"" << mpk_html.url
    << "\">" << mpk_html.title << "</a>" << endl
    << pkfile_html.indent() << "<hr>" << endl
    // Repeat the title in a section header
    << pkfile_html.indent() << "<h1>" << pkfile_html.title << "</h1>" << endl
    // Source function for this PKFile
    << pkfile_html.indent() << "<p>Function:</p>" << endl
    << pkfile_html.indent() << "<blockquote>" << sourceFunc
    << "</blockquote>" << endl
    // Epochs
    << pkfile_html.indent()
    << "<p>PKFile epoch (incremented each time the PKFile is re-written): "
    << he->pkfile->PKEpoch() << "</p>" << endl
    << pkfile_html.indent()
    << ("<p>Secondary dependency names epoch (incremented each time "
	"secondary dependencies are added or deleted): ")
    << he->pkfile->NamesEpoch() << "</p>" << endl
    // Introduction of common secondary dependencies
    << pkfile_html.indent() << "<p>Common secondary dependencies ("
    << he->pkfile->CommonNames()->Cardinality()
    << " total):</p>" << endl;
  HTMLFVTable(pkfile_html.out, pkfile_html.indent_level,
	      he->pkfile->AllNames(), he->pkfile->CommonNames());

  // Loop over CFP groups, gathering the fingerprints of the common
  // names from each one.  This will be used to show for each CFP
  // group which common names differ from other groups.
  CommonTagTbl *commonTags = NEW_ARRAY(CommonTagTbl, he->pkhdr->num);
  for(int cfp_i = 0; cfp_i < he->pkhdr->num; cfp_i++)
    {
      // Get the first cache entry in CFP group cfp_i
      CE::List *list;
      bool inTbl =
	he->pkfile->OldEntries()->Get(he->pkhdr->entry[cfp_i].cfp,
				      /*OUT*/ list);
      assert(inTbl);
      assert(list != 0);
      CE::T *entry = list->Head();
      assert(entry != 0);

      // Loop over the common names
      BVIter commonIter(*(he->pkfile->CommonNames()));
      unsigned int commonIndex;
      while(commonIter.Next(commonIndex))
	{
	  // Find the index of this name in this entry's FP list.
	  int fpsIndex = commonIndex;
	  if(entry->IMap() != 0)
	    {
	      IntIntTblLR::int_type temp;
	      inTbl = entry->IMap()->Get(commonIndex, /*OUT*/ temp);
	      assert(inTbl);
	      fpsIndex = temp;
	    }

	  // Remember the tag of this common name in this CFP group.
	  inTbl = commonTags[cfp_i].Put(commonIndex,
					entry->FPs()->fp[fpsIndex]);
	  assert(!inTbl);
	}
    }

  // Start index list for CFP groups in our HTML file and the
  // MultiPKFile HTML file.
  pkfile_html.out
    << pkfile_html.indent() << "<p>Common fingerprint groups ("
    << he->pkhdr->num << " total):</p>" << endl
    << pkfile_html.indentOpen() << "<ul>" << endl;
  mpk_html.out
    << mpk_html.indent() << "Common fingerprint groups ("
    << he->pkhdr->num << " total):" << endl
    << mpk_html.indentOpen() << "<ul>" << endl;

  // Loop over the CFP groups
  for(int cfp_i = 0; cfp_i < he->pkhdr->num; cfp_i++)
    {
      // Generate the HTML file for the CFP group.
      PrintCFPGroupHTML(out_dir, print_fnames,
			mpk_html, pkfile_html,
			he, cfp_i, commonTags);
    }

  // Close index lists for CFP groups
  pkfile_html.out << pkfile_html.indentClose() << "</ul>" << endl;
  mpk_html.out << mpk_html.indentClose() << "</ul>" << endl;

  // Finish the HTML, flush and close the file.
  EndHTML(pkfile_html.out);
  FS::Close(pkfile_html.out);
}

// Generate an HTML file representation of a MultiPKFile.  Also, call
// PrintPKFileHTML to generate HTML files for the PKFiles in this
// MultiPKFile.
static void PrintFileHTML(const Text &out_dir, bool print_fnames,
			  const char *nm)
  throw (SMultiPKFileRep::BadMPKFile, FS::EndOfFile, FS::Failure)
{
    // form the filename
    Text fname(nm);
    if (nm[0] != '/') {
	fname = Config_SCachePath + '/' + fname;
    }

    // open the MultiPKFile
    ifstream ifs;
    if (!OpenFile(fname, /*OUT*/ ifs)) return;

    try {
	// read it (including the PKFiles)
	SMultiPKFileRep::Header hdr(ifs);
	hdr.ReadEntries(ifs);
	hdr.ReadPKFiles(ifs);
	FS::Close(ifs);

	HTMLFileInfo mpk_html;

	// Determine the HTML filename and open it
	assert(hdr.num > 0);
	PKPrefix::T pfx(*(hdr.pkSeq[0]));
	mpk_html.fname = MPKFileHTMLFileName(pfx);
	FS::OpenForWriting(CombinePath(out_dir, mpk_html.fname), mpk_html.out);
	if(print_fnames)
	  cout << "Writing " << mpk_html.fname << endl;

	// Set the URL for this page
	mpk_html.url = MPKFileURL(pfx);

	// Set the title for the document
	{
	  OBufStream l_title;
	  l_title << "MultiPKFile " << HTMLQuote(fname);
	  mpk_html.title = l_title.str();
	}

	// Print the HTML opener
	mpk_html.indent_level = StartHTML(mpk_html.out, mpk_html.title);

	mpk_html.out
	  // Repeat the title in a section header
	  << mpk_html.indent() << "<h1>" << mpk_html.title << "</h1>" << endl
	  << mpk_html.indent() << "<p>PKFiles (" << hdr.num
	  << " total):</p>" << endl
	  << mpk_html.indentOpen() << "<ul>" << endl;

	// Loop over the PKFiles, generating HTML for each one
	for (int i = 0; i < hdr.num; i++) {
	    SMultiPKFileRep::HeaderEntry *he;
	    bool inTbl = hdr.pkTbl.Get(*(hdr.pkSeq[i]), he);
	    assert(inTbl);

	    // Note: this will add entries to the index in out HTML
	    // file as well.
	    PrintPKFileHTML(out_dir, print_fnames, mpk_html, i, he);
	}

	// Finish the index.
	mpk_html.out
	  << mpk_html.indentClose() << "</ul>" << endl;

	// Finish the HTML, flush and close the file.
	EndHTML(mpk_html.out);
	FS::Close(mpk_html.out);
    } catch (...) {
	FS::Close(ifs);
	throw;
    }
}

static void ExitProgram(char *msg) throw ()
{
    cerr << "Fatal error: " << msg << endl;
    cerr << "Syntax: PrintMPKFile" << endl
	 << "    [-verbose]" << endl
	 << "    [-html [directory]]" << endl
	 << "    [-url-patterns mpkfile pkfile cfp-group entry]" << endl
	 << "    file ..." << endl;
    exit(1);
}

int main(int argc, char *argv[])
{
    bool verbose = false;
    bool html = false;
    const char *html_dir = ".";

    // process command-line switch(es)
    int arg = 1;
    for (/*SKIP*/; arg < argc && *argv[arg] == '-'; arg++) {
	if (CacheArgs::StartsWith(argv[arg], "-verbose")) {
	    verbose = true;
	} else if (CacheArgs::StartsWith(argv[arg], "-html")) {
	    html = true;
	    arg++;
	    if((arg < argc) && FS::IsDirectory(argv[arg]))
	      {
		html_dir = argv[arg];
	      }
	    else
	      {
		arg--;
	      }
	} else if (CacheArgs::StartsWith(argv[arg], "-url-patterns")) {
	  if((arg + 4) > argc)
	    {
	      ExitProgram("-url-patterns needs 4 pattern arguments: "
			  "MultiPKFile, PKFile, common fingerprint group, "
			  "cache entry");
	    }
	  g_MPKFileURL_pattern = argv[++arg];
	  g_MPKFileURL_pattern_set = true;
	  g_PKFileURL_pattern = argv[++arg];
	  g_PKFileURL_pattern_set = true;
	  g_CFPGroupURL_pattern = argv[++arg];
	  g_CFPGroupURL_pattern_set = true;
	  g_CacheEntryURL_pattern = argv[++arg];
	  g_CacheEntryURL_pattern_set = true;
	} else{
	    ExitProgram("unrecognized command-line option");
	}
    }

    // process file arguments
    if (argc - arg < 1) {
	ExitProgram("no file arguments specified");
    }
    bool firstFile = true; // determines whether to print a separating newline
    for (/*SKIP*/; arg < argc; arg++) {
	try {
		if (firstFile) firstFile = false; else cout << endl;
	    if(html)
	      {
		PrintFileHTML(html_dir, verbose, argv[arg]);
	      }
	    else
	      {
		PrintFile(cout, argv[arg], verbose);
	      }
	}
	catch (SMultiPKFileRep::BadMPKFile) {
	    cerr << "Error: '" << argv[arg]
                 << "' is a bad MultiPKFile" << endl;
	    return 1;
	}
        catch (FS::EndOfFile) {
            cerr << "Fatal error: unexpected end-of-file" << endl;
	    return 1;
	}
	catch (const FS::Failure &f) {
	    cerr << f;
	    return 1;
	}
    }
    return 0;
}
