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


// Read the graph log and print its complete contents

#include <Basics.H>
#include <VestaLog.H>
#include <Recovery.H>
#include <VestaLogSeq.H>
#include <FP.H>
#include <CacheArgs.H>
#include <CacheConfig.H>
#include <GraphLog.H>

#include <Table.H>
#include <ShortIdKey.H>
#include <Units.H>
#include <Sequence.H>
#include <algorithm>

using std::cout;
using std::cerr;
using std::endl;

using std::sort;

// An instance of this class will compare keys by their associated
// values in a Table
template<class K>
class CompareByValue
{
private:
  const typename Table<K, Basics::uint64>::Default &values;
public:
  CompareByValue(typename Table<K, Basics::uint64>::Default &vs)
    : values(vs)
  { }

  bool operator()(const K &a, const K &b)
  {
    Basics::uint64 va, vb;
    values.Get(a, va);
    values.Get(b, vb);
    // Note: reverse order so that the sorted order is from highest to
    // lowest
    return vb < va;
  }
};

// This function will extract the keys from a table and put them in a
// sequence sorted by their value.

template<class K>
Sequence<K, true> sorted_keys(typename Table<K, Basics::uint64>::Default &values)
{
  Sequence<K, true> result;

  // Iterate over the table and extract all the keys
  typename Table<K, Basics::uint64>::Iterator it(&values);
  K k;
  Basics::uint64 v;
  while(it.Next(k, v))
    {
      result.addhi(k);
    }

  // Sort the keys in descending order by their associated value
  CompareByValue<K> comparator(values);
  sort(result.begin(), result.end(), comparator);

  return result;
}

// Table types used to accumulate the size in the graph log of nodes
// by PK and model shortid.

typedef Table<FP::Tag, Basics::uint64>::Default SizeByFpTable;
typedef Table<ShortIdKey, Basics::uint64>::Default SizeByModelTable;

// Sequence types for the sorted lists of PKs and model shortids.

typedef Sequence<FP::Tag, true> Fp_List;
typedef Sequence<ShortIdKey, true> Model_List;

void ReadGraphLog(RecoveryReader &rd,
		  bool verbose, bool brief, bool stats,
		  /*INOUT*/ int &num, /*INOUT*/ int &rootCnt, /*INOUT*/ int &nodeCnt,
		  /*INOUT*/ SizeByFpTable &size_by_fp, /*INOUT*/ SizeByModelTable &size_by_model)
  throw (VestaLog::Error, VestaLog::Eof)
{
    while (!rd.eof()) {
	GraphLog::Entry *entry = GraphLog::Entry::Recover(rd);
	if (entry->kind == GraphLog::RootKind) {
	    rootCnt++;
	} else if (entry->kind == GraphLog::NodeKind) {
	    nodeCnt++;

	    if(stats)
	      {
		GraphLog::Node *node = (GraphLog::Node *) entry;

		unsigned long bytes_this_node = node->Size();
		   
		Basics::uint64 byte_total = 0;
		(void) size_by_fp.Get(*(node->loc), byte_total);
		byte_total += bytes_this_node;
		(void) size_by_fp.Put(*(node->loc), byte_total);

		// Skip the null shortid
		if(node->model != NullShortId)
		  {
		    // This must be a file
		    assert(!SourceOrDerived::dirShortId(node->model));

		    byte_total = 0;
		    (void) size_by_model.Get(node->model, byte_total);
		    byte_total += bytes_this_node;
		    (void) size_by_model.Put(node->model, byte_total);
		  }

	      }
	} else {
	    cerr << "Fatal error: unrecognized graph log node:" << endl;
	    cerr << "  kind = " << entry->kind << endl;
	    cerr << "Exiting..." << endl;
	    exit(1);
	}
	if (verbose) {
	    cout << "*** Entry " << num++ << " ***" << endl;
	    entry->DebugFull(cout);
	    cout << endl;
	} else if(!brief) {
	    entry->Debug(cout);
	}
	cout.flush();
	delete entry;
    }
}

void SyntaxError(Text msg, char *arg = (char *)NULL) throw ()
{
    cerr << "Error: " << msg;
    if (arg != (char *)NULL) cerr << " `" << arg << "'";
    cerr << endl;
    cerr << "Syntax: PrintGraphLog [ -verbose | -brief ] [ -stats ]" << endl;
    exit(1);
}

int main(int argc, char *argv[]) 
{
  bool verbose = false, stats = false, brief = false;
  Basics::uint64 size_cutoff = (100 * 1024);
    int arg;
    for (arg = 1; arg < argc; arg++) {
	if (*argv[arg] == '-') {
	    if (CacheArgs::StartsWith(argv[arg], "-verbose")) {
		verbose = true;
		brief = false;
		continue;
	    }
	    if (CacheArgs::StartsWith(argv[arg], "-stats")) {
		stats = true;
		continue;
	    }
	    if (CacheArgs::StartsWith(argv[arg], "-brief")) {
		brief = true;
		verbose = false;
		continue;
	    }
	    if (CacheArgs::StartsWith(argv[arg], "-size-cutoff")) {
	        if(++arg >= argc) SyntaxError("-size-cutoff requires an argument");
		try
		  {
		    size_cutoff = Basics::ParseUnitVal(argv[arg]);
		  }
		catch(Basics::ParseUnitValFailure e)
		  {
		    SyntaxError(Text("Couldn't parse stats cutoff: ") + e.emsg,
				argv[arg]);
		  }
		continue;
	    }
	}
	SyntaxError("unrecognized argument", argv[arg]);
    }
    if (!verbose && !brief) {
	cout << "Graph log entries:" << endl;
    }

    try {
	// open "graphLog"
	VestaLogSeq graphLogSeq(Config_GraphLogPath.chars());
	graphLogSeq.Open(/*ver=*/ -1, /*readonly=*/ true);
	RecoveryReader *rd;
	
	// read "graphLog"
	int num = 0, rootCnt = 0, nodeCnt = 0;
	SizeByFpTable size_by_fp;
	SizeByModelTable size_by_model;

	while ((rd = graphLogSeq.Next()) != (RecoveryReader *)NULL) {
	  ReadGraphLog(*rd,
		       verbose, brief, stats,
		       /*INOUT*/ num, /*INOUT*/ rootCnt, /*INOUT*/ nodeCnt,
		       /*INOUT*/ size_by_fp, /*INOUT*/ size_by_model);
	}

	// print totals
	cout << "*** Totals ***" << endl;
	cout << "  roots = " << rootCnt << endl;
	cout << "  nodes = " << nodeCnt << endl;

	if(stats)
	  {
	    cout << "*** Stats ***" << endl;

	    cout << "  --- By PK ---" << endl;

	    Fp_List sorted_fps = sorted_keys<FP::Tag>(size_by_fp);

	    while(sorted_fps.size() > 0)
	      {
		FP::Tag pk = sorted_fps.remlo();
		Basics::uint64 v;
		size_by_fp.Get(pk, v);

		if(v < size_cutoff)
		  {
		    cout << "  (Skipping " << (sorted_fps.size() + 1)
			 << " PKs with less than "
			 << Basics::FormatUnitVal(size_cutoff) << ")" << endl;
		    break;
		  }

		cout << "    " << pk << " = "
		     << Basics::FormatUnitVal(v) << " bytes" << endl;
	      }

	    cout << "  --- By Model ---" << endl;

	    Model_List sorted_models = sorted_keys<ShortIdKey>(size_by_model);

	    while(sorted_models.size() > 0)
	      {
		ShortId sid = sorted_models.remlo().sid;

		// Skip the null shortid
		if(sid == 0) continue;

		Basics::uint64 v;
		size_by_model.Get(sid, v);

		if(v < size_cutoff)
		  {
		    cout << "  (Skipping " << (sorted_models.size() + 1)
			 << " Models with less than "
			 << Basics::FormatUnitVal(size_cutoff) << ")" << endl;
		    break;
		  }

		char *path = SourceOrDerived::shortIdToName(sid, false);
		cout << "    " << path << " = "
		     << Basics::FormatUnitVal(v) << " bytes" << endl;
		delete [] path;
	      }

	  }
    }
    catch (VestaLog::Error &err) {
	cerr << "VestaLog fatal error -- failed reading graph log:" << endl;
        cerr << "  " << err.msg << endl;
        cerr << "Exiting..." << endl;
	exit(1);
    }
    catch (VestaLog::Eof) {
	cerr << "VestaLog fatal error: ";
        cerr << "unexpected EOF while reading graph log; exiting..." << endl;
	exit(1);
    }
    return 0;
}
