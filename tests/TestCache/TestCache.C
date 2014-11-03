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

// Last modified on Fri Apr 22 22:31:33 EDT 2005 by ken@xorian.net  
//      modified on Sat Feb 12 17:33:35 PST 2000 by mann  
//      modified on Thu Feb 10 12:14:10 PST 2000 by heydon 
//      modified on Sat Jan 29 14:30:58 PST 2000 by heydon

// TestCache -- make calls on the cache server according to a file describing
//   function arguments and results.

// base libs
#include <time.h>
#include <Basics.H>
#include <Table.H>
#include <Generics.H>
#include <SRPC.H>

// vesta/fp
#include <FP.H>

// cache-common
#include <CacheArgs.H>
#include <CacheIntf.H>
#include <PKPrefix.H>
#include <Model.H>
#include <CacheIndex.H>
#include <FV.H>
#include <VestaVal.H>
#include <CompactFV.H>
#include <FV2.H>

// cache-debug
#include <Debug.H>

// cache-client
#include "CacheC.H"
#include "DebugC.H"
#include "WeederC.H"

using std::ios;
using std::istream;
using std::ifstream;
using std::cout;
using std::cin;
using std::cerr;
using std::endl;
using std::ws;

const char CommentChar = '%';
const char Newline = '\n';
const int  MaxWordLen = 80;
const char *CheckpointOnlyCmd = "CheckpointOnly";
const char *CheckpointCmd = "Checkpoint";
const char *PauseCmd = "Pause";
const char *RenewCmd = "RenewLeases";
const char *WeedCmd = "Weed";

static char buff[MaxWordLen];

typedef TextIntTbl NameMap;

void ErrMsg(char *msg, bool halt = true) throw ()
{
    cerr << "**" << endl << "** Error: " << msg << endl << "**" <<endl<<endl;
    cerr.flush();
    if (halt) exit(2);
}

bool SkipWhite(istream& ins, bool echoComments)
/* Returns true if there are pending commands to read; false if EOF is reached
   after skipping whitespace and comments. */
{
    char first;
    while ((first=ins.peek()) != EOF) {
	if (first == CommentChar) {
	    if (echoComments) {
		// echo this line to "cout"
		int c;
		do {
		    c = ins.get();
		    cout << (char)c;
		} while (c != EOF && c != Newline);
		cout.flush();
	    } else {
		// ignore the rest of the line
		ins.ignore(10000, Newline);
	    }
	} else if (first == Newline) {
	    // skip empty line
	    ins.ignore(1);
	} else {
	    return true;
	}
    }
    return false;
}

void Checkpoint(istream& ins, CacheC& client) throw (SRPC::failure)
{
    if (ins.get() != Newline)
	ErrMsg("unexpected chars after Checkpoint command");
    FP::Tag pkgVersion;      // leave empty
    Model::T model = 0;      // default value
    CacheEntry::Indices cis; // leave empty
    bool done = false;       // default value
    client.Checkpoint(pkgVersion, model, cis, done);
}

void FlushAll(DebugC& debug) throw (SRPC::failure)
{
    debug.FlushAll();
}

void Pause(istream& ins) throw ()
{
    if (ins.get() != Newline)
	ErrMsg("unexpected chars after Pause command");
    cerr << "Hit <Enter> to continue.";
    (void)cin.get();
    cout << endl;
}

void ReadCIs(istream& ins, /*OUT*/ CacheEntry::Indices& cis) throw ()
{
    int ix = 0, sz = 10;
    int *ra = NEW_PTRFREE_ARRAY(int, sz);

    // read CI's
    (void) ins.setf(ios::skipws);
    while (ins.peek() != Newline) {
	if (ix == sz) {
	    sz *= 2;
	    int *ra2 = NEW_PTRFREE_ARRAY(int, sz);
	    for (int i = 0; i < ix; i++) ra2[i] = ra[i];
	    delete ra;
	    ra = ra2;
	}
	ins >> ra[ix];
	ix++;
    }
    (void) ins.get(); // skip newline
    (void) ins.unsetf(ios::skipws);

    // copy to result
    cis.len = ix;
    cis.index = NEW_PTRFREE_ARRAY(CacheEntry::Index, ix);
    for (int i = 0; i < ix; i++) cis.index[i] = ra[i];
    delete ra;
}

void ReadBits(istream& ins, /*OUT*/ BitVector &cis) throw ()
{
    // read CI's
    cis.ResetAll();
    (void) ins.setf(ios::skipws);
    while (ins.peek() != Newline) {
	int ix;
	ins >> ix;
	if (cis.Set(ix)) {
	    ErrMsg("duplicate cache index");
	}
    }
    (void) ins.get(); // skip newline
    (void) ins.unsetf(ios::skipws);
}

void ReadPfxs(istream &ins, /*OUT*/ PKPrefixTbl &pfxs) throw ()
{
    // read prefixes into sequence
    (void) ins.setf(ios::skipws);
    while (ins.peek() != Newline) {
	ins >> buff;
	FP::Tag pk(buff);
	PKPrefix::T pfx(pk);
	(void)pfxs.Put(pfx, (char)true);
    }
    (void) ins.get(); // skip newline
    (void) ins.unsetf(ios::skipws);
}

void RenewLeases(istream& ins, CacheC& client) throw (SRPC::failure)
{
    if (ins.get() != Newline)
	ErrMsg("unexpected chars after RenewLeases command");
    CacheEntry::Indices cis;
    ReadCIs(ins, /*OUT*/ cis);
    bool res = client.RenewLeases(cis);
}

void StartMark(WeederC& client) throw (SRPC::failure)
{
    int newLogVer;
    client.StartMark(/*OUT*/ newLogVer);
}

void SetHitFilter(WeederC& client, const BitVector &cis) throw (SRPC::failure)
{
    client.SetHitFilter(cis);
}

void ResumeLeaseExp(WeederC& client) throw (SRPC::failure)
{
    client.ResumeLeaseExp();
}

void EndMark(WeederC& client, const BitVector &cis,
  const PKPrefixTbl &pfxs) throw (SRPC::failure)
{
    int chkptVer = client.EndMark(cis, pfxs);
}

void Weed(istream &ins, WeederC &client) throw (SRPC::failure)
{
    if (ins.get() != Newline)
	ErrMsg("unexpected chars after Weed command");

    BitVector cis;
    PKPrefixTbl pfxs;
    ReadBits(ins, /*OUT*/ cis);
    ReadPfxs(ins, /*OUT*/ pfxs);

    StartMark(client);
    SetHitFilter(client, cis);
    ResumeLeaseExp(client);
    EndMark(client, cis, pfxs);
}

FV::Epoch FreeVariables(CacheC& client, const FP::Tag& pk,
  /*OUT*/ CompactFV::List& names) throw (SRPC::failure)
{
    bool isEmpty; // unused
    return client.FreeVariables(pk, /*OUT*/ names, /*OUT*/ isEmpty);
}

CacheIntf::LookupRes Lookup(CacheC& client, const FP::Tag& pk,
  FV::Epoch epoch, const FP::List& fps) throw (SRPC::failure)
{
    CacheEntry::Index ci; // unused
    VestaVal::T cacheVal; // unused
    return client.Lookup(pk, epoch, fps,
     /*OUT*/ ci, /*OUT*/ cacheVal);
}

static const Text SourceFunc("TestCache source function location");

CacheIntf::AddEntryRes AddEntry(CacheC& client, const FP::Tag& pk,
  const char* types, const FV2::ListApp& allNames, const FP::List& fps,
  const VestaVal::T& value) throw (SRPC::failure)
{
    // prepare args
    Model::T model = 0;       // default value
    CacheEntry::Indices kids; // empty kids list

    // make call
    CacheEntry::Index ci;
    return client.AddEntry(pk, types, allNames, fps,
      value, model, kids, SourceFunc, /*OUT*/ ci);
}

void CallCache(istream& ins, CacheC& client, const FP::Tag& pk)
  throw (SRPC::failure)
{
    if (ins.get() != Newline)
	ErrMsg("chars after primary key text");
    FV2::ListApp allNames;
    NameMap nameMap;

    // read free variables
    (void) ins.setf(ios::skipws);
    while (ins.peek() != Newline) {
	ins >> buff;
	FV2::T *nm = NEW_CONSTR(FV2::T, (buff));
	// prepend type if none was supplied
	if (nm->getlo().Length() != 1) {
	    nm->addlo(Text("N"));
	}
	int i = allNames.Append(nm);
	(void) nameMap.Put(nm->ToText(), i);
    }
    (void) ins.get(); // skip newline

    // allocate types
    char *types = NEW_PTRFREE_ARRAY(char, allNames.len);
    int i;
    for (i = 0; i < allNames.len; i++) {
	FV2::T *name = allNames.name[i];
	assert(name->size() > 1 && name->getlo().Length() == 1);
	// strip type from name
	Text firstArc(name->remlo());
	types[i] = firstArc[0];
    }

    // read fingerprints
    FP::List fps(allNames.len);
    for (i = 0; ins.peek() != Newline; i++) {
	ins >> buff;
	if (i > fps.len) {
	    // expand array if necessary
	    int newLen = fps.len * 2;
	    FP::Tag *newFP = NEW_PTRFREE_ARRAY(FP::Tag, newLen);
	    for (int j = 0; j > fps.len; j++) {
		newFP[j] = fps.fp[j];
	    }
	    fps.len = newLen;
	    fps.fp = newFP;
	}
	fps.fp[i] = FP::Tag(buff);
    }
    fps.len = i; // set final length
    (void) ins.get(); // skip newline

    // read value
    ins >> buff;
    VestaVal::T value(buff);
    (void) ins.unsetf(ios::skipws);

    CacheIntf::LookupRes res;
    while (true) {
	// make "FreeVariables" call
	CompactFV::List names;
	FV::Epoch epoch = FreeVariables(client, pk, /*OUT*/ names);

	// fill in new "fps" list for call to "Lookup"
	FP::List fps2(names.num);
	for (int i = 0; i < names.num; i++) {
	    FV2::T *fv2 = names.tbl.Get(names.idx[i]);
	    fv2->addlo(Text(names.types[i]));
	    int j;
	    if (nameMap.Get(fv2->ToText(), /*OUT*/ j)) {
		// we have supplied an FP for this one
		fps2.fp[i] = fps.fp[j];
	    } else {
		// use default FP
		fps2.fp[i] = FP::Tag("");
	    }
	}

	// make "Lookup" call
	res = Lookup(client, pk, epoch, fps2);
	if (res != CacheIntf::FVMismatch) break;
    }
    if (res == CacheIntf::BadLookupArgs)
	ErrMsg("bad arguments to \"Lookup\"");

    // make "AddEntry" call (if necessary)
    if (res == CacheIntf::Miss) {
	CacheIntf::AddEntryRes res =
          AddEntry(client, pk, types, allNames, fps, value);
	switch (res) {
	  case CacheIntf::NoLease:
	    ErrMsg("missing lease in \"AddEntry\"", /*halt=*/ false);
	  case CacheIntf::BadAddEntryArgs:
	    ErrMsg("bad arguments to \"AddEntry\"", /*halt=*/ false);
	}
    }
}

void RunTest(istream& ins, bool echoComments) throw (SRPC::failure)
{
    CacheC cacheC(/*debug=*/ CacheIntf::All);
    DebugC debugC(/*debug=*/ CacheIntf::All);
    WeederC weederC(/*debug=*/ CacheIntf::All);
    while (SkipWhite(ins, echoComments)) {
	// read first word
	ins >> ws >> buff;
	assert(strlen(buff) >= 1);

	if (strcmp(buff, CheckpointOnlyCmd) == 0) {
	    Checkpoint(ins, cacheC);
	} else if (strcmp(buff, CheckpointCmd) == 0) {
	    Checkpoint(ins, cacheC);
	    FlushAll(debugC);
	} else if (strcmp(buff, PauseCmd) == 0) {
	    Pause(ins);
	} else if (strcmp(buff, RenewCmd) == 0) {
	    RenewLeases(ins, cacheC);
	} else if (strcmp(buff, WeedCmd) == 0) {
	    Weed(ins, weederC);
	} else {
	    FP::Tag pk(buff);
	    CallCache(ins, cacheC, pk);
	}
    }
}

void Exit(char *msg) throw ()
{
    cerr << "Error: " << msg << endl;
    cerr << "Syntax: TestCache [ -comments ] [ file ]" << endl;
    cerr.flush();
    exit(1);
}

int main(int argc, char *argv[]) 
{
    bool echoComments = false;
    istream *input = &(cin);

    // process command-line
    for (int arg = 1; arg < argc; arg++) {
	if (*(argv[arg]) == '-') {
	    if (CacheArgs::StartsWith(argv[arg], "-comments")) {
		echoComments = true;
	    } else {
		Exit("unrecognized command-line switch");
	    }
	} else if (arg + 1 == argc) {
	  if(!FS::IsFile(argv[arg]))
	    {
	      Text errMsg = "\"";
	      errMsg += argv[arg];
	      errMsg += "\" doesn't exist or isn't a file";
	      Exit(errMsg.chars());
	    }
	    // open file
	  input = NEW_CONSTR(ifstream, (argv[arg]));
	  if (input->fail()) {
	    Text errMsg("unable to open file \"");
	    errMsg += argv[arg]; errMsg += "\" for reading";
	    ErrMsg(errMsg.chars());
	  }
	} else {
	    Exit("too many arguments");
	}
    }

    // run test
    try {
	RunTest(*input, echoComments);
    }
    catch (SRPC::failure f) {
	cerr << "SRPC failure: " << f.msg << endl;
	exit(2);
    }
    return 0;
}
