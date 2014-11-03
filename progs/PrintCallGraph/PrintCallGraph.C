// Copyright (C) 2007, Vesta Free Software Project
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

#include <Table.H>
#include <IntKey.H>
#include <GraphLog.H>
#include <BitVector.H>
#include <CacheConfig.H>
#include <VestaLogSeq.H>
#include <GraphLog.H>
#include <SPKFile.H>
#include <SMultiPKFile.H>
#include <Units.H>
#include <OS.H>
#include <ReposUI.H>
#include <VestaConfig.H>
#include <IndentHelper.H>

using std::streampos;
using std::ostream;
using std::ifstream;
using std::cout;
using std::cerr;
using std::endl;
using Basics::IndentHelper;

// Program name, incorporated into error messages
Text program_name;

// Similar, but not identical, to the GLNode class used by the weeder.
class GL_Node
{
public:
  /* Default constructor */
  GL_Node() throw () { /* SKIP */ }

  /* Initialize this node to contain the relevant values of "node". */
  GL_Node(const GraphLog::Node &p_node, bool keep_dfiles = false) throw ()
    : ci(p_node.ci),
      pk(p_node.loc),
      kids(p_node.kids),
      dfiles(keep_dfiles ? p_node.refs : 0)
  { /* SKIP */ }

  GL_Node(CacheEntry::Index p_ci,
	  FP::Tag *p_pk,
	  CacheEntry::Indices *p_kids,
	  Derived::Indices *p_dfiles = 0) throw ()
    : ci(p_ci), pk(p_pk), kids(p_kids), dfiles(p_dfiles)
  { /*SKIP*/ }

  // fields
  CacheEntry::Index ci;      // this node's index
  FP::Tag *pk;               // PK for this cache entry
  CacheEntry::Indices *kids; // child entries
  Derived::Indices *dfiles;  // deriveds reachable from this entry
};

// Global table of graph log nodes we have saved
typedef Table<IntKey, GL_Node *>::Default GL_Node_Tbl;
GL_Node_Tbl g_gl_nodes;

// Global table of PK to source function text
typedef Table<FP::Tag, const Text *>::Default Source_Func_Tbl;
Source_Func_Tbl g_source_func_tbl;

// Function for getting the source funciton of a PK
const Text *PK2SourceFunc(const FP::Tag &pk) throw()
{
  const Text *result = 0;
  if(!g_source_func_tbl.Get(pk, result))
    {
      PKPrefix::T pfx(pk);
      try {
	ifstream ifs;
	SMultiPKFile::OpenRead(pfx, /*OUT*/ ifs);
	int version;
	try {
	  streampos origin =
	    SMultiPKFile::SeekToPKFile(ifs, pk, /*OUT*/ version);
	  SPKFileRep::Header *hdr = 0;
	  SPKFile pkfile(pk, ifs, origin, version, hdr, false, true);
	  result = pkfile.SourceFunc();

	  bool inTbl = g_source_func_tbl.Put(pk, result);
	  assert(!inTbl);
	} catch (...) { FS::Close(ifs); throw; }
	FS::Close(ifs);
      }
      catch (SMultiPKFileRep::NotFound)
	{
	  // Either the MultiPKFile doesn't exist on disk, or the
	  // PKFile doesn;t exist within the MultiPKFile.  In either
	  // case, we have nothing to do.
	}
      catch (FS::Failure)
	{
	  cerr << program_name
	       << ": WARNING: Failure reading MultiPKFile " << pfx
	       << " looking for PK " << pk << endl;
	}
      catch (FS::EndOfFile)
	{
	  // indicates a programming error or a corrupt file
	  cerr << program_name
	       << ": WARNING: Premature EOF in MultiPKFile " << pfx
	       << " looking for PK " << pk << endl;
	}
      catch(...)
	{
	  cerr << program_name
	       << ": WARNING: Unknown excpetion reading MultiPKFile " << pfx
	       << " looking for PK " << pk << endl;
	}
    }
  return result;
}

// Text to use for one level of indentation
static Text g_indent_text = "  ";

// Class for printing a call tree.  Includes code to remember which
// CIs have alreayd been printed and only print each a single time.
class EntryTreePrinter
{
protected:
  // Entries we have already printed to the screen
  BitVector cis_printed;

  // Indent for printing call trees
  IndentHelper indent;

  // Return true and set "reason" to explanatory text if this node
  // shouldn't be printed.  Default implementation keeps from printing
  // the same entry multiple times.
  virtual bool SuppressNode(const GL_Node *node,
			    /*OUT*/ Text &reason)
  {
    if(cis_printed.Read(node->ci))
      {
	reason = "[printed above]";
	return true;
      }
    return false;
  }

public:
  EntryTreePrinter()
    : indent(0,
	     // Use the indentation text from the global
	     g_indent_text)
  {
  }

  void PrintEntry(const GL_Node *node)
  {
    assert(node != 0);
    cout << indent << "ci = " << node->ci << endl;
    Text suppress_reason;
    if(SuppressNode(node, suppress_reason))
      {
	cout << indent << suppress_reason << endl
	     << endl;
	return;
      }
    assert(node->pk != 0);
    cout << indent << "pk = " << *node->pk << endl;
    const Text *sourceFunc = PK2SourceFunc(*node->pk);
    if(sourceFunc != 0)
      {
	cout << indent << "sourceFunc = " << *sourceFunc << endl;
      }
    if(node->dfiles != 0)
      {
	cout << indent << "refs = ";
	node->dfiles->Print(cout,
			    (((unsigned int) indent)+1)*g_indent_text.Length());
      }
    cout << endl;
    cis_printed.Set(node->ci);
    if(node->kids != 0)
      {
	++indent;
	PrintCIList(node->kids);
	--indent;
      }
  }

  void PrintCI(CacheEntry::Index ci)
  {
    GL_Node *kid = 0;
    bool inTbl = g_gl_nodes.Get(ci, kid);
    if(inTbl)
      {
	assert(kid != 0);
	PrintEntry(kid);
      }
    else
      {
	cout << indent << "ci = " << ci << endl
	     << indent << "MISSING FROM GRAPH LOG!" << endl
	     << endl;
      }
  }

  void PrintCIList(const CacheEntry::Indices *cis)
  {
    if(cis == 0) return;
    for(unsigned int i = 0; i < cis->len; i++)
      {
	PrintCI(cis->index[i]);
      }
  }
};

// Return a timestamp formatter according to [UserInterface]TimeFormat
Text formatted_time(time_t when)
{
  Text timefmt = VestaConfig::get_Text("UserInterface", "TimeFormat");
  char timebuf[512];
  strftime(timebuf, sizeof(timebuf), timefmt.cchars(), localtime(&when));
  return Text(timebuf);
}

// Abstract base class for a graph log search
class Searcher
{
public:
  // "Process" is called for each entry in the graph log after it has
  // been read.  Nodes are copied into "g_gl_nodes" before "Process"
  // is called.  If "Process" returns false, the graph log will
  // continue to be read.  If it returns true, reading the graph log
  // will stop.
  virtual bool Process(const GraphLog::Node *node) = 0;
  virtual bool Process(const GraphLog::Root *root) = 0;

  // "Done" is called once ater the entire graph log has been read.
  virtual void Done() = 0;
};

// Search for a particular CI and print the call tree below it
class CI_Searcher : public Searcher
{
private:
  // The CI we're looking for
  CacheEntry::Index ci;

  EntryTreePrinter tree_printer;

public:
  CI_Searcher(CacheEntry::Index ci)
    : ci(ci)
  { }

  virtual bool Process(const GraphLog::Node *node)
  {
    assert(node != 0);
    if(node->ci == this->ci)
      {
	GL_Node *node_p = 0;
	bool inTbl = g_gl_nodes.Get(node->ci, node_p);
	assert(inTbl);
	assert(node_p != 0);

	tree_printer.PrintEntry(node_p);

	// We found the CI we were looking for, we can stop now.
	return true;
      }
    // Not our CI, continue
    return false;
  }
  virtual bool Process(const GraphLog::Root *root)
  {
    // Continue on any graph log root
    return false;
  }

  virtual void Done()
  {
  }
};

// Search for graph log roots for a given model.  If one for a
// completed evaluation can be found, print its call tree.  Otherwise,
// print call trees for however much partially completed work was done
// on the most recent evaluation.
class Model_Searcher : public Searcher
{
private:
  // The fingerprint of the immutable directory containing the model
  // and the shortid of the model.  We need these to match against
  // graph log roots.
  FP::Tag model_dir_fp;
  ShortId model_sid;

  Text model_path;

  // "last_root" is the last matching root we've seen.
  // "last_done_root" is the last matching root we've seen with
  // done=true.
  const GraphLog::Root *last_root;
  const GraphLog::Root *last_done_root;

public:
  Model_Searcher(const Text &path)
    throw(ReposUI::failure)
    : last_root(0), last_done_root(0), model_path(path)
  {
    VestaSource *model_vs = ReposUI::filenameToVS(path);
    VestaSource *model_dir_vs = model_vs->getParent();

    assert(model_vs != 0);
    assert(model_dir_vs != 0);

    model_dir_fp = model_dir_vs->fptag;
    model_sid = model_vs->shortId();

    delete model_vs;
    delete model_dir_vs;
  }

  virtual bool Process(const GraphLog::Node *node)
  {
    // Continue on any graph log node (we're looking for roots)
    return false;
  }

  virtual bool Process(const GraphLog::Root *root)
  {
    assert(root != 0);
    if((root->pkgVersion == model_dir_fp) &&
       (root->model == model_sid))
      {
	assert(root->cis != 0);

	last_root = root;

	if(root->done)
	  {
	    last_done_root = root;
	  }
      }

    // Always continue.  (We want to find the best match that's latest
    // in the graph log.)
    return false;
  }

  virtual void Done()
  {
    const GraphLog::Root *root_to_print = last_done_root;
    if((root_to_print == 0) && (last_root != 0))
      {
	root_to_print = last_root;
      }

    if(root_to_print == 0)
      {
	cout << "No matching build found" << endl << endl;
	return;
      }

    assert(root_to_print->cis != 0);

    cout << model_path << endl
	 << "Evaluated at: " << formatted_time(root_to_print->ts) << endl
	 << "Build was " << (root_to_print->done? "completed" : "incomplete")
	 << endl
	 << root_to_print->cis->len << " call tree(s) follow" << endl
	 << endl;

    EntryTreePrinter tree_printer;

    tree_printer.PrintCIList(root_to_print->cis);
  }
};

// Specialization of the call tree printer which will suppress
// printing of any entries which don't refer to the derived file we're
// looking for.
class DerivedRefTreePrinter : public EntryTreePrinter
{
protected:
  // The message we'll give when a node doesn't refer to the derived
  // file we're interested in.
  Text no_ref_msg;

  // The set of CIs which do refer to the derived file we're
  // interested in.
  const BitVector &refer_cis;

  virtual bool SuppressNode(const GL_Node *node,
			    /*OUT*/ Text &reason)
  {
    if(!refer_cis.Read(node->ci))
      {
	reason = no_ref_msg;
	return true;
      }
    return EntryTreePrinter::SuppressNode(node, reason);
  }

public:
  DerivedRefTreePrinter(ShortId derived_sid,
			const BitVector &refer_cis)
    : refer_cis(refer_cis)
  {
    char buf[30];
    sprintf(buf, "[No reference to derived 0x%08x]", derived_sid);
    no_ref_msg = buf;
  }
};

// Search for cache entries that refer to a particular derived file.
// Print call tres that refer to the derived file, suppressing the
// printing of calls that don't refer to the derived file.
class Derived_Searcher : public Searcher
{
private:
  ShortId derived_sid;

  bool derivedMatch(Derived::Indices *refs)
  {
    if(refs == 0) return false;

    for(unsigned int i = 0; i < refs->len; i++)
      {
	if(refs->index[i] == this->derived_sid)
	  return true;
      }

    return false;
  }

  // "refer_cis" will be used to collect every cache index which
  // refers to the derived file of interest.

  // "seed_cis" will be used to collect the highest level of the call
  // tree which reference the derived file.
  BitVector refer_cis, seed_cis;

public:
  Derived_Searcher(ShortId dsid)
    : derived_sid(dsid)
  { }

  virtual bool Process(const GraphLog::Node *node)
  {
    assert(node != 0);
    if(derivedMatch(node->refs))
      {
	// This node refers to the derived file and should be treated
	// as a seed.
	refer_cis.Set(node->ci);
	seed_cis.Set(node->ci);

	// The children of this node are not seeds
	assert(node->kids != 0);
	for(unsigned int i = 0; i < node->kids->len; i++)
	  {
	    seed_cis.Reset(node->kids->index[i]);
	  }
      }
    // Always continue.  (We want to find every reference in the graph
    // log.)
    return false;
  }

  virtual bool Process(const GraphLog::Root *root)
  {
    // Always continue.
    return false;
  }

  virtual void Done()
  {
    char buf[10];
    sprintf(buf, "%08x", derived_sid);

    cout << "Found " << seed_cis.Cardinality()
	 << " call trees referring to derived file 0x"
	 << buf << endl << endl;

    DerivedRefTreePrinter tree_printer(derived_sid, refer_cis);

    BVIter it(seed_cis);
    CacheEntry::Index ci;
    while(it.Next(ci))
      {
	tree_printer.PrintCI(ci);
      }
  }
};

void usage()
{
  cerr << "Usage: " << program_name << endl
       << "    [{ --model | -m } path-to-model-file]" << endl
       << "        Search for the call tree of a top-level model" << endl
       << "    [{ --ci | -i } decimal-cache-index]" << endl
       << "        Search for the call tree of a cache index" << endl
       << "    [{ --derived-file | -d } hex-derived-shortid]" << endl
       << "        Search for cache entries protecting a derived file" << endl
       << "    [{ --mem | -M }]" << endl
       << "        Before program exit, print memory usage" << endl
       << endl;
}

int main(int p_argc, const char **p_argv)
{
  program_name = p_argv[0];

  // These variables control what we will do
  Searcher *search = 0;
  bool mem_stats = false;
  bool print_dis = false;

  // Command-line argument processing
  for(unsigned int l_arg = 1; l_arg < p_argc; l_arg++)
    {
      if((strcmp(p_argv[l_arg], "--ci") == 0) ||
	 (strcmp(p_argv[l_arg], "-i") == 0))
	{
	  l_arg++;
	  if(l_arg >= p_argc)
	    {
	      cerr << program_name
		   << ": Argument (cache index) required with "
		   << p_argv[l_arg-1] << endl;
	      usage();
	      exit(1);
	    }
	  const char *ci_start = p_argv[l_arg];
	  char *ci_end = 0;
	  unsigned long int ci = strtoul(ci_start, &ci_end, 10);
	  if((ci_end == ci_start) || (*ci_end != 0))
	    {
	      cerr << program_name
		   << ": Invalid cache index (argument to " << p_argv[l_arg-1]
		   << "): \"" << p_argv[l_arg] << "\"" << endl;
	      usage();
	      exit(1);
	    }
	  search = NEW_CONSTR(CI_Searcher, (ci));
	}
      else if((strcmp(p_argv[l_arg], "--model") == 0) ||
	      (strcmp(p_argv[l_arg], "-m") == 0))
	{
	  l_arg++;
	  if(l_arg >= p_argc)
	    {
	      cerr << program_name
		   << ": Argument (model path) required with "
		   << p_argv[l_arg-1] << endl;
	      usage();
	      exit(1);
	    }

	  try
	    {
	      search = NEW_CONSTR(Model_Searcher, (p_argv[l_arg]));
	    }
	  catch(ReposUI::failure f)
	    {
	      cerr << program_name
		   << ": Error getting model to search for: "
		   << f.msg << endl;
	      usage();
	      exit(1);
	    }
	}
      else if((strcmp(p_argv[l_arg], "--derived-file") == 0) ||
	      (strcmp(p_argv[l_arg], "-d") == 0))
	{
	  l_arg++;
	  if(l_arg >= p_argc)
	    {
	      cerr << program_name
		   << ": Argument (derived file shortid) required with "
		   << p_argv[l_arg-1] << endl;
	      usage();
	      exit(1);
	    }
	  const char *sid_start = p_argv[l_arg];
	  char *sid_end = 0;
	  unsigned long int sid = strtoul(sid_start, &sid_end, 16);
	  if((sid_end == sid_start) || (*sid_end != 0))
	    {
	      cerr << program_name
		   << ": Invalid derived file shortid (argument to "
		   << p_argv[l_arg-1] << "): \""
		   << p_argv[l_arg] << "\"" << endl;
	      usage();
	      exit(1);
	    }
	  search = NEW_CONSTR(Derived_Searcher, (sid));
	}
      else if((strcmp(p_argv[l_arg], "--mem") == 0) ||
	      (strcmp(p_argv[l_arg], "-M") == 0))
	{
	  mem_stats = true;
	}
      else if((strcmp(p_argv[l_arg], "--indent") == 0) ||
	      (strcmp(p_argv[l_arg], "-I") == 0))
	{
	  l_arg++;
	  if(l_arg >= p_argc)
	    {
	      cerr << program_name
		   << ": Argument (indentation text) required with "
		   << p_argv[l_arg-1] << endl;
	      usage();
	      exit(1);
	    }
	  g_indent_text = p_argv[l_arg];
	}
      else if((strcmp(p_argv[l_arg], "--print-deriveds") == 0) ||
	      (strcmp(p_argv[l_arg], "-D") == 0))
	{
	  print_dis = true;
	}
      else if((strcmp(p_argv[l_arg], "--help") == 0) ||
	      (strcmp(p_argv[l_arg], "-h") == 0))
	{
	  usage();
	  exit(0);
	}
      else
	{
	  cerr << program_name
	       << ": Unrecognized command-line argument: "
	       << p_argv[l_arg] << endl;
	  usage();
	  exit(1);
	}
    }

  if(search == 0)
    {
      cerr << program_name
	   << ": No search requested" << endl;
      usage();
      exit(1);
    }

  // This is where we actually read and search the graph log
  try
    {
      bool done = false;

      // Open the graph log.
      VestaLogSeq l_graph_log_seq(Config_GraphLogPath.chars());
      l_graph_log_seq.Open(/*version=*/ -1, /*readonly=*/ true);

      // Read the whole graph log
      RecoveryReader *l_rd;
      while (!done && ((l_rd = l_graph_log_seq.Next()) != NULL))
	{
	  while (!done && !l_rd->eof())
	    {
	      GraphLog::Entry *l_entry = GraphLog::Entry::Recover(*l_rd);
	      assert(l_entry != 0);

	      if(l_entry->kind == GraphLog::NodeKind)
		{
		  // Record this node in our table
		  GL_Node *l_node = NEW_CONSTR(GL_Node,
					       (*(GraphLog::Node *) l_entry, print_dis));
		  
		  bool inTbl = g_gl_nodes.Put(IntKey(l_node->ci), l_node);
		  if(inTbl)
		    {
		      // Note a duplicate CI but continue
		      cerr << program_name
			   << ": WARNING: duplicate CI in graph log (corrupted cache?)"
			   << endl;
		    }

		  // Process this node
		  done = search->Process((GraphLog::Node *) l_entry);
		}
	      else if(l_entry->kind == GraphLog::RootKind)
		{
		  // Process this root
		  done = search->Process((GraphLog::Root *) l_entry);
		}
	    }
	}

      // Do post-search processing
      search->Done();

      l_graph_log_seq.Close();
    }
  catch (VestaLog::Error &err)
    {
      cerr << program_name
	   << ": VestaLog fatal error -- failed reading graph log:" << endl
	   << "  " << err.msg << endl
	   << "Exiting..." << endl;
      exit(2);
    }
  catch (VestaLog::Eof)
    {
      cerr << program_name
	   << "VestaLog fatal error: "
	   << "unexpected EOF while reading graph log; exiting..." << endl;
      exit(2);
    }
  catch (VestaConfig::failure f)
    {
      cerr << program_name << ": " << f.msg << endl;
      exit(3);
    }
  catch (SRPC::failure f)
    {
      cerr << program_name
	   << ": SRPC failure; " << f.msg << " (" << f.r << ")" << endl;
      exit(4);
    }

  if(mem_stats)
    {
      unsigned long total, resident;
      OS::GetProcessSize(total, resident);
      cout << "---------- Memory usage at exit ----------" << endl
	   << "Total    : " << Basics::FormatUnitVal(total) << endl
	   << "Resident : " << Basics::FormatUnitVal(resident) << endl;
    }

  return 0;
}
