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


/* TestCacheRandom -- make random calls on the Vesta-2 cache server */

#include <Basics.H>
#include <BufStream.H>
#include <SRPC.H>
#include <FP.H>
#include <VestaConfig.H>

// cache-common
#include <CacheArgs.H>
#include <CacheIntf.H>
#include <Model.H>
#include <CacheIndex.H>
#include <FV.H>
#include <CompactFV.H>
#include <VestaVal.H>
#include <NewVal.H>
#include <Debug.H>

#include "CacheC.H"

using std::ostream;
using std::cerr;
using std::endl;
using Basics::OBufStream;
using OS::cio;

// fatal programmer error
class FatalError {
  public: FatalError() { /*EMPTY*/ }
};

struct ClientArgs {
    int id;			// thread id
    int numReqs;		// number of requests; -1 -> infinity
    int numAddOps;		// number of AddEntry ops; -1 -> infinity
    int numPKs;                 // number of distinct PKs
    int minNames;               // min number of free vars / entry
    int maxNames;               // max number of free vars / entry
    int commonNames;            // number of common names per PK
    bool kids;			// generate non-empty "kids" to AddEntry?
    bool verbose;               // print call/return values?
};

static void ExitClient() throw ()
{
    cerr << "SYNTAX: TestCacheRandom ";
    cerr << "[-threads n] [-reqs numReqs] [-pks numPKs ] [-kids] [-quiet]";
    cerr << endl;
    exit(1);
}

static FV::Epoch ClientFreeVarsCall(CacheC *client,
  int id, const FP::Tag& pk, /*OUT*/ int &numNames,
  /*OUT*/ bool &isEmpty) throw (SRPC::failure)
{
    CompactFV::List names;
    FV::Epoch epoch;

    epoch = client->FreeVariables(pk, /*OUT*/ names, /*OUT*/ isEmpty);
    numNames = names.num;
    return epoch;
}

static CacheIntf::LookupRes
ClientLookupCall(CacheC *client, int id, const FP::Tag& pk,
  FV::Epoch epoch, int numNames) throw (SRPC::failure)
{
    // initialize arguments
    FP::List fps;
    if (numNames > 0) {
	NewVal::NewFPs(fps, numNames);
    }

    // make call
    CacheEntry::Index ci;
    VestaVal::T value;
    CacheIntf::LookupRes res;
    res = client->Lookup(pk, epoch, fps, /*OUT*/ ci, /*OUT*/ value);
    return res;
}

static void NewNames2(/*OUT*/ char* &types, /*OUT*/ FV2::List &names,
  int numCommon, unsigned int num) throw ()
{
    // pick random number of names if needed
    if (num < 1) num = Debug::MyRand(1, 5);

    // pick random types
    num = max(num, numCommon);
    types = NEW_PTRFREE_ARRAY(char, num+1);
    int i;
    for (i = 0; i < numCommon; i++) {
	types[i] = 'A';
    }
    for (/*SKIP*/; i < num; i++) {
	types[i] = 'A' + Debug::MyRand(0,25);
    }
    // terminate string to make debugging easier
    types[num] = '\0';

    // pick random names
    names.len = num;
    names.name = NEW_ARRAY(FV2::TPtr, num);
    Text prefix("name");
    for (i=0; i < num; i++) {
      names.name[i] = NEW(FV2::T);
      names.name[i]->addhi(prefix);
      OBufStream oss; 
      oss << i;
      names.name[i]->addhi(Text(oss.str()));
    }
}

static const Text SourceFunc("TestCacheRandom source function location");

static void ClientAddEntry(CacheC *client, int id, const FP::Tag& pk,
  int minNames, int maxNames, int commonNames, bool genKids)
  throw (SRPC::failure)
{
    int numNames;
    FV2::List names;
    char *types;
    FP::List fps;
    VestaVal::T value;
    Model::T model;
    CacheEntry::Indices kids;
    CacheEntry::Index ci;
    CacheIntf::AddEntryRes res;

    // initialize arguments
    numNames = Debug::MyRand(minNames, maxNames);
    NewNames2(/*OUT*/ types, /*OUT*/ names, commonNames, numNames);
    NewVal::NewFPs(/*OUT*/ fps, numNames);
    NewVal::NewValue(/*OUT*/ value);
    NewVal::NewModel(/*OUT*/ model);
    if (genKids) NewVal::NewCIs(/*OUT*/ kids);

    // make call
    res = client->AddEntry(pk, types, names, fps, value, model,
      kids, SourceFunc, /*OUT*/ ci);
}

void *MainClientProc(void *ptr) throw ()
/* This is the procedure invoked for each thread forked by "main". "ptr" is
   actually a pointer to a "ClientArgs" structure, which encodes the thread
   id, the number of requests this thread should make on the cache, and
   whether or not it should generate a non-empty "kids" list on calls to the
   cache's AddEntry method. */
{
    ClientArgs *args = (ClientArgs *)ptr;
    FP::Tag pk;
    FV::Epoch epoch;
    int numNames;
    CacheIntf::LookupRes res;

    cio().start_out() << Debug::Timestamp() 
		      << "Starting client thread " << args->id << endl << endl;
    cio().end_out();

    try {
      CacheC client(/*debug=*/ args->verbose
		    ? CacheIntf::All
		    : CacheIntf::None);

      int addOps = 0;
      for (int i = 0; 
	   (args->numReqs < 0 || i < args->numReqs) &&
	   (args->numAddOps < 0 || addOps < args->numAddOps);
	   i++) {
	if (args->verbose) {
	  cio().start_out() << Debug::Timestamp() 
			    << "Client thread " << args->id 
			    << " **** Round " << i << " ****" << endl << endl;
	  cio().end_out();
	}
	NewVal::NewFP(/*OUT*/ pk, /*vals=*/ args->numPKs);
	do {
	  bool isEmpty;
	  epoch = ClientFreeVarsCall(&client, args->id, pk,
				     /*OUT*/ numNames, /*OUT*/ isEmpty);
	  if (isEmpty) {
	    res = CacheIntf::Miss;
	  } else {
	    res = ClientLookupCall(&client, args->id,
				   pk, epoch, numNames);
	  }
	} while (res == CacheIntf::FVMismatch);
	if (res == CacheIntf::Miss) {
	  ClientAddEntry(&client, args->id, pk,
			 args->minNames, args->maxNames, args->commonNames,
			 args->kids);
	  addOps++;
	}
      }
    } catch (SRPC::failure f) {
      cio().start_err() << "SRPC Failure: " << f.msg
			<< " (code " << f.r << ")" << endl << endl;
      cio().end_err();
    }

    cio().start_out() << Debug::Timestamp() 
		      << "Exiting client thread " << args->id << endl << endl;
    cio().end_out();
    return (void *)NULL;
}

int main(int argc, char *argv[]) 
{
    int numThreads = 1, numReqs = -1, numAddOps = -1, numPKs = 10;
    int minNames = 1, maxNames = 5, commonNames = 1;
    bool verbose = true;
    bool kids = false;
    int arg;

    // parse arguments
    if (argc < 1 || argc > 17) {
	cerr << "Error: Incorrect number of arguments" << endl;
	ExitClient();
    }
    for (arg = 1; arg < argc; arg++) {
	if (CacheArgs::StartsWith(argv[arg], "-threads")) {
	    if (sscanf(argv[++arg], "%d", &numThreads) < 1 || numThreads < 1) {
		cerr <<"Error: -threads argument not a positive integer"<<endl;
		ExitClient();
	    }
	} else if (CacheArgs::StartsWith(argv[arg], "-reqs")) {
	    if (sscanf(argv[++arg], "%d", &numReqs) < 1) {
		cerr << "Error: -reqs argument must be an integer" << endl;
		ExitClient();
	    }
	} else if (CacheArgs::StartsWith(argv[arg], "-addops")) {
	    if (sscanf(argv[++arg], "%d", &numAddOps) < 1) {
		cerr << "Error: -addops argument must be an integer" << endl;
		ExitClient();
	    }
	} else if (CacheArgs::StartsWith(argv[arg], "-commonNames")) {
	    if (sscanf(argv[++arg], "%d", &commonNames) < 1) {
		cerr << "Error: -commonNames argument must be an integer"
                     << endl;
		ExitClient();
	    }
	} else if (CacheArgs::StartsWith(argv[arg], "-minNames", 3)) {
	    if (sscanf(argv[++arg], "%d", &minNames) < 1) {
		cerr << "Error: -minNames argument must be an integer" << endl;
		ExitClient();
	    }
	    if (maxNames < minNames) maxNames = minNames;
	} else if (CacheArgs::StartsWith(argv[arg], "-maxNames", 3)) {
	    if (sscanf(argv[++arg], "%d", &maxNames) < 1) {
		cerr << "Error: -maxNames argument must be an integer" << endl;
		ExitClient();
	    }
	    if (maxNames < minNames) minNames = maxNames;
	} else if (CacheArgs::StartsWith(argv[arg], "-pks")) {
	    if (sscanf(argv[++arg], "%d", &numPKs) < 1) {
		cerr << "Error: -numPKs argument must be an integer" << endl;
		ExitClient();
	    }
	} else if (CacheArgs::StartsWith(argv[arg], "-seed")) {
	    int seed;
	    if (sscanf(argv[++arg], "%d", &seed) < 1) {
		cerr << "Error: -seed argument must be an integer" << endl;
		ExitClient();
	    }
	    srand((unsigned int) seed);
	} else if (CacheArgs::StartsWith(argv[arg], "-kids")) {
	    kids = true;
	} else if (CacheArgs::StartsWith(argv[arg], "-quiet")) {
	    verbose = false;
	} else {
	    cerr << "Error: argument " << arg << " is bad" << endl;
	    ExitClient();
	}
    }
    
    // start client
    cio().start_out() << "Starting client:" << endl
		      << "  Config: " << VestaConfig::get_location() 
		      << endl << endl;
    cio().end_out();

    // fork threads
    cio().start_out() << Debug::Timestamp()  << "Forking " << numThreads 
		      << " thread(s)" << endl << endl;
    cio().end_out();
    Basics::thread *th = NEW_PTRFREE_ARRAY(Basics::thread, numThreads);
    int i;
    for (i = 0; i < numThreads; i++) {
      ClientArgs *args = NEW(ClientArgs);
      args->id = i + 1;
      args->numReqs = numReqs;
      args->numAddOps = numAddOps;
      args->numPKs = numPKs;
      args->minNames = minNames;
      args->maxNames = maxNames;
      args->commonNames = commonNames;
      args->kids = kids;
      args->verbose = verbose;
      th[i].fork(MainClientProc, (void *)args);
    }

    // join with forked threads
    cio().start_out() << Debug::Timestamp()
		      << "Main client thread now waiting for threads to complete..." 
		      << endl << endl;
    cio().end_out();
    for (i = 0; i < numThreads; i++) {
	(void) th[i].join();
    }
}
