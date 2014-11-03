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

// PrintFVDiff -- print the differences between the free variables
// (aka secondary dependencies) of two cache entries

#include <Basics.H>
#include <FS.H>
#include <FP.H>
#include <CacheIndex.H>
#include <PKPrefix.H>
#include <CacheConfig.H>
#include <SMultiPKFileRep.H>
#include <VestaConfig.H>

using std::cout;
using std::cerr;
using std::endl;
using std::ifstream;

Text program_name;

Text MPKFileName(const FP::Tag &pk) throw ()
// Get the filename of the MultiPKFile that would contain the given
// primary key.
{
    PKPrefix::T pfx(pk);
    int granBits = PKPrefix::Granularity();
    int ArcBits = 8; // at most 256 elements per directory
    char buff[10];
    int printLen = sprintf(buff, "gran-%02d/", granBits);
    assert(printLen == 8);
    Text res(Config_SCachePath + "/" + buff + pfx.Pathname(granBits, ArcBits));
    return res;
}

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
      cerr << program_name
	   << ": Fatal error: unable to open '" << fname << "' for reading." << endl;
      return false;
    }
    return true;
}

bool hasName(CE::T *entry, unsigned int name_i,
	     /*OUT*/ unsigned int &fpIndex)
{
  assert(entry != 0);
  if(entry->IMap() != 0)
    {
      IntIntTblLR::int_type temp;
      if(entry->IMap()->Get(name_i, /*OUT*/ temp))
	{
	  fpIndex = temp;
	  return true;
	}
    }
  else if(name_i < entry->FPs()->len)
    {
      fpIndex = name_i;
      return true;
    }
  return false;
}

void usage()
{
  cerr << "Usage: " << program_name
       << " [-v|--verbose] <primary-key-in-hex> <cache-index-1> <cache-index-2>"
       << endl << endl
       << "Example: " << program_name
       << " '2b1f8372382297bc 7b16a71ef275c5a5' 16267 29341"
       << endl << endl;
}

bool parse_ci(const char *arg,
	      /*OUT*/ CacheEntry::Index &ci)
{
  assert(arg != 0);

  char *end;
  errno = 0;
  unsigned long int value = strtoul(arg, &end, 0);
  ci = value;
  if(*end != 0)
    {
      cerr << program_name
	   << ": Couldn't parse \"" << arg << "\" as a cache index ("
	   << strlen(end) << " leftover characters)" << endl;
      return false;
    }
  else if((errno == ERANGE) || (((unsigned long int) ci) != value))
    {
      cerr << program_name
	   << ": Couldn't parse \"" << arg
	   << "\" as a cache index (value too large)" << endl;
      return false;
    }
  return true;
}

bool parse_pk(const char *arg,
	      /*OUT*/ FP::Tag &pk)
{
  assert(arg != 0);

  // First check for any characters that are neither hex digits nor
  // whitespace
  const char *cur = arg;
  while(*cur)
    {
      if(!isxdigit(*cur) && !isspace(*cur))
	{
	  cerr << program_name
	       << ": Couldn't parse \"" << arg
	       << "\" as a primary key (contains non-whitespace, non-hex characters)"
	       << endl;
	  return false;
	}
      cur++;
    }

  // Now use sscanf to extract the values and make sure there aren't
  // any leftover characters
  int n_chars = 0;
  int n_matched =
    sscanf(arg,
	   "%016" FORMAT_LENGTH_INT_64 "x%016" FORMAT_LENGTH_INT_64 "x%n",
	   &(pk.Words()[0]), &(pk.Words()[1]), &n_chars);

  if((n_matched < 2) || (n_chars < strlen(arg)))
    {
      cerr << program_name
	   << ": Couldn't parse \"" << arg << "\" as a primary key ("
	   << (strlen(arg) - n_chars) << " leftover characters, "
	   << n_matched << " hex words read)" << endl;
      return false;
    }

  return true;
}

int main(int argc, char *argv[])
{
  program_name = argv[0];
  if(argc < 4)
    {
      cerr << program_name
	   << ": Must supply primary key and two cache indecies"
	   << endl << endl; 
      usage();
      return 1;
    }

  CacheEntry::Index ci_a, ci_b;
  FP::Tag pk;
  bool verbose = false, have_pk = false, have_ci_a = false, have_ci_b = false;

  for(int argi = 1; argi < argc; argi++)
    {
      if((strcmp(argv[argi], "-v") == 0) ||
	 (strcmp(argv[argi], "--verbose") == 0))
	{
	  verbose = true;
	}
      else if(!have_pk)
	{
	  if(!parse_pk(argv[argi], pk))
	    {
	      return 1;
	    }
	  have_pk = true;
	}
      else if(!have_ci_a)
	{
	  if(!parse_ci(argv[argi], ci_a))
	    {
	      return 1;
	    }
	  have_ci_a = true;
	}
      else if(!have_ci_b)
	{
	  if(!parse_ci(argv[argi], ci_b))
	    {
	      return 1;
	    }
	  have_ci_b = true;
	}
      else
	{
	  cerr << program_name
	       << ": Extra command-line argument '" << argv[argi] << "'"
	       << endl << endl; 
	  usage();
	  return 1;
	}
    }

  if(ci_a == ci_b)
    {
      cerr << program_name
	   << ": Must specify two different CIs" << endl;
      return 1;
    }

  Text mpk_fname = MPKFileName(pk);

  try
    {
      // open the file
      ifstream ifs;
      if (!OpenFile(mpk_fname, /*OUT*/ ifs))
	{
	  return 2;
	}

	// read it (including the PKFiles)
	SMultiPKFileRep::Header hdr(ifs);
	hdr.ReadEntries(ifs);
	hdr.ReadPKFiles(ifs);
	FS::Close(ifs);

	SMultiPKFileRep::HeaderEntry *he = 0;
	bool inTbl = hdr.pkTbl.Get(pk, he);
	if(!inTbl || (he == 0) || (he->pkfile == 0))
	  {
	    cerr << program_name
		 << ": Primary key " << pk << " not present in "
		 << mpk_fname << endl;
	    return 3;
	  }

	CE::T *entry_a = 0, *entry_b = 0;
	assert(he->pkhdr != 0);
	for(int cfp_i = 0; cfp_i < he->pkhdr->num; cfp_i++)
	  {
	    // Get the first cache entry in CFP group cfp_i
	    CE::List *list;
	    bool inTbl =
	      he->pkfile->OldEntries()->Get(he->pkhdr->entry[cfp_i].cfp,
					    /*OUT*/ list);
	    assert(inTbl);

	    while(list != 0)
	      {
		if(list->Head()->CI() == ci_a)
		  {
		    assert(entry_a == 0);
		    entry_a = list->Head();
		  }
		else if(list->Head()->CI() == ci_b)
		  {
		    assert(entry_b == 0);
		    entry_b = list->Head();
		  }
		list = list->Tail();
	      }
	  }
	if(entry_a == 0)
	  {
	    cerr << program_name
		 << ": Couldn't find entry for CI " << ci_a << endl;
	    return 3;
	  }
	if(entry_b == 0)
	  {
	    cerr << program_name
		 << ": Couldn't find entry for CI " << ci_b << endl;
	    return 3;
	  }

	if(verbose)
	  {
	    const Text *sourceFunc = he->pkfile->SourceFunc();
	    if(sourceFunc != 0)
	      cout << "sourceFunc = " << *sourceFunc << endl;
	    cout << "------------------------------" << endl
		 << "~ : differs" << endl
		 << "< : only in " << ci_a << endl
		 << "> : only in " << ci_b << endl
		 << "------------------------------" << endl;
	  }

	for(unsigned int name_i = 0;
	    name_i < he->pkfile->AllNames()->len;
	    name_i++)
	  {
	    unsigned int fpIndex_a, fpIndex_b;
	    bool in_a = hasName(entry_a, name_i, fpIndex_a),
	      in_b = hasName(entry_b, name_i, fpIndex_b);

	    if(in_a && in_b)
	      {
		assert(fpIndex_a < entry_a->FPs()->len);
		assert(fpIndex_b < entry_b->FPs()->len);
		FP::Tag fp_a = entry_a->FPs()->fp[fpIndex_a];
		FP::Tag fp_b = entry_b->FPs()->fp[fpIndex_b];
		if(fp_a != fp_b)
		  {
		    if(verbose)
		      cout << "~ ";
		    cout << he->pkfile->AllNames()->name[name_i]
			 << endl;
		  }
	      }
	    else if(verbose && in_a && !in_b)
	      {
		cout << "< " << he->pkfile->AllNames()->name[name_i] << endl;
	      }
	    else if(verbose && !in_a && in_b)
	      {
		cout << "> " << he->pkfile->AllNames()->name[name_i] << endl;
	      }
	  }
    }
    catch (SMultiPKFileRep::BadMPKFile)
      {
	cerr << program_name
	     << ": Fatal error: '" << mpk_fname
	     << "' is an invalid or corrupt MultiPKFile" << endl;
	return 2;
      }
    catch (FS::EndOfFile)
      {
	cerr << program_name
	     << ": Fatal error: unexpected end-of-file" << endl;
	return 2;
      }
    catch (const FS::Failure &f)
      {
	cerr << program_name << ": " << f;
	return 2;
      }
    catch (VestaConfig::failure f)
      {
	cerr << program_name << ": " << f.msg << endl;
	return 1;
      }
    catch (...)
      {
	cerr << program_name << ": Unknown exception!" << endl;
	return 4;
      }

    return 0;
}
