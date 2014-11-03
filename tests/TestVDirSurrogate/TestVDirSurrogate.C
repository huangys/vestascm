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

//
// TestVDirSurrogate.C
// Last modified on Tue Apr 19 22:29:46 EDT 2005 by ken@xorian.net  
//      modified on Fri May 18 17:36:12 PDT 2001 by mann  
//      modified on Tue Apr 29 08:52:01 PDT 1997 by heydon
//
// Simple client to test the VDirSurrogate layer of the repository server
//

#include "VestaSource.H"
#include "VestaConfig.H"
#include "VDirSurrogate.H"
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <iomanip>
#include <stdio.h>

using std::cout;
using std::cin;
using std::cerr;
using std::endl;
using std::setw;
using std::setfill;
using std::hex;
using std::dec;

char *boolText[] = { "false", "true" };

bool
myListCallback(void* closure, VestaSource::typeTag type, Arc arc,
	       unsigned int index, Bit32 pseudoInode, ShortId filesid,
	       bool master)
{
    cout << setw(3) << index
      << " " << setw(1) << VestaSource::typeTagChar(type)
      << (master ? 'M' : ' ')
      << " " << setw(8) << hex << pseudoInode << dec
      << " " << setw(8) << hex << filesid << dec
      << " " << arc << endl; 
    return true;
}

bool
valueCb(void* cl, const char* value)
{
    cout << value << " ";
    return true;
}

bool
historyCb(void* cl, VestaSource::attribOp op, const char* name,
	  const char* value, time_t timestamp)
{
    cout << VestaSource::attribOpChar(op) << ", " << name << ", "
      << value << ", " << timestamp << endl;
    return true;
}

void
coutLongId(const LongId& longid)
{
    int i;
    int len = longid.length();
    for (i=0; i<len; i++) {
	cout << setw(2) << setfill('0') << hex
	  << (int) (longid.value.byte[i] & 0xff) << setfill(' ');
    }
    cout << dec;
}

void
cinLongId(LongId& longid)
{
    char junk;
    char buf[256];
    cin >> buf;
    cin.get(junk);
    int i;
    for (i=0; i<32; i++) {
	int byte;
	if (sscanf(&buf[i*2], "%2x", &byte) != 1) break;
	longid.value.byte[i] = byte;
	if (buf[i*2 + 1] == '\0') {
	    longid.value.byte[i] <<= 4;
	    i++;
	    break;
	}
    }
    for (; i<32; i++) {
	longid.value.byte[i] = 0;
    }
}

void
coutFPTag(FP::Tag& fptag)
{
    int i;
    unsigned char* fpbytes = (unsigned char*) fptag.Words();
    for (i=0; i<FP::ByteCnt; i++) {
	cout << setw(2) << setfill('0') << hex
	  << (int) (fpbytes[i] & 0xff) << setfill(' ');
    }
    cout << dec;
}

void
cinFPTag(FP::Tag& fptag)
{
    for (;;) {
	char junk;
	char buf[256];
	cin >> buf;
	cin.get(junk);
	int i;
	unsigned char* fpbytes = (unsigned char*) fptag.Words();
	for (i=0; i<FP::ByteCnt; i++) {
	    int byte;
	    if (sscanf(&buf[i*2], "%2x", &byte) != 1) {
		cout << "Invalid fptag value; try again" << endl;
		break;
	    }
	    fpbytes[i] = byte;
	}
	if (i==FP::ByteCnt) return;
    }
}

#if 0 //!!
void
default_identity(AccessControl::Identity who)
{
    who->aup_time = time(NULL);
    static char machname[MAX_MACHINE_NAME + 1];
    gethostname(machname, sizeof(machname));
    who->aup_machname = machname;
    who->aup_uid = geteuid();
    who->aup_gid = getegid();
    static int groups[NGRPS];
    who->aup_len = getgroups(NGRPS, (gid_t*) groups);
    who->aup_gids = groups;
}

void
print_identity(AccessControl::Identity who)
{
   cout << "Identity: ";
   cout << "[ time=0x" << hex << who->aup_time << dec
      << ", machname=" << who->aup_machname << "," << endl
      << " uid=" << who->aup_uid << ", gid=" << who->aup_gid
      << ", gids=";
    int i;
    for (i=0; i<who->aup_len; i++) {
	cout << who->aup_gids[i] << " ";
    }
   cout << "]" << endl;
}
#endif

int
readSrcN(const int& nextSrcN)
{
    for (;;) {
	int res;
	char typein[300];
	char junk;
	cout << "src#: ";
	cin.get(typein, sizeof(typein));
	cin.get(junk);
	res = atoi(typein);
	if (res >= nextSrcN) {
	    cout << "Out of range; max src# is " << nextSrcN - 1 << endl;
	} else {
	    return res;
	}
    }
}

Text
vsToFilename(VestaSource* vs)
{
    Text ret = "";
    LongId longid_par;
    unsigned int index;
    VestaSource* vs_cur = vs;
    VestaSource* vs_par;
    VestaSource* vs_junk;
    VestaSource::errorCode err;
    char arcbuf[MAX_ARC_LEN+1];
    const Text& host = vs->host();
    const Text& port = vs->port();
    for (;;) {
	longid_par = vs_cur->longid.getParent(&index);
	if (longid_par == NullLongId) {
	  Text prefix = "invalid longid/";
	  if (vs_cur->longid.value.byte[0] == 0) {
	    if (vs_cur->longid == RootLongId) {
	      prefix = "/vesta/";
	    } else if (vs_cur->longid == MutableRootLongId) {
	      prefix = "/vesta-work/";
	    } else if (vs_cur->longid == VolatileRootLongId) {
	      prefix = "/vesta-work/.volatile/";
	    } else if (vs_cur->longid == DirShortIdRootLongId) {
	      prefix = "dir sid longid/";
	    } else if (vs_cur->longid == FileShortIdRootLongId) {
	      prefix = "file sid longid/";
	    }
	  }
	  ret = prefix + ret;
	  break;
	}
	vs_par = VDirSurrogate::LongIdLookup(longid_par, host, port);
	if (vs_par == NULL) {
	  return "longid lookup failed";
	}
	err = vs_par->lookupIndex(index, vs_junk, arcbuf);
	if (err != VestaSource::ok) {
	  return Text("longid lookup failed: ") +
	    VestaSource::errorCodeString(err);
	}
	delete vs_junk;
	ret = Text(arcbuf) + PathnameSep + ret;
	if (vs_cur != vs) delete vs_cur;
	vs_cur = vs_par;
    }
 done:
    // Strip bogus final "/" from return value
    ret = ret.Sub(0, ret.Length() - 1);
    if (vs_cur != vs) delete vs_cur;
    return ret;		  
}

void *
test_thread(void *arg)
{
    VestaSource* src[100];
    int nextSrcN;
    AccessControl::Identity who = NULL;  //!!

    // Test
    cout << "RepositoryRoot is source #0; MutableRoot is source #1" << endl; 
    src[0] = VestaSource::repositoryRoot();
    src[1] = VestaSource::mutableRoot();
    nextSrcN = 2;
    for (;;) {
	char typein[300];
	char arc[300];
	char arc2[300];
	char arc3[300];
	char arc4[300];
	char arc5[300];
	char junk;
	long ltmp;
	int srcN, newSrcN, srcN2, srcN3, srcN4, i, master, tmp, chk;
	unsigned int index;
	VestaSource::errorCode err;
	ShortId sid;
	LongId longid, longid2;
	char* charstar;
	VestaSource* vs;
	Bit64 handle;
	time_t timestamp;
	VestaSource::attribOp op;
	FP::Tag fptag;
	Text rhost, rport;

	cout << "command (? for help): ";

	cin.get(typein, sizeof(typein));
	cin.get(junk);
	switch (typein[0]) {
	  case '?':
	    cout << 
"res(Y)nc, (p)rint, (s)hortId, lookup (a)rc/(P)ath, (i)nsert, (d)elete, (l)ist\n"
"makeM(u)table, longid app(e)nd/(g)etParent/loo(k)up/fromSh(o)rtId/(v)alid,\n"
"(h)asAttribs, i(n)Attribs, getAttrib(1), getAttrib(2), lis(t)Attribs,\n"
"getAttribHistor(y), writeAttri(b), get(N)FSInfo, make(f)ilesImmutable,\n"
"create(V)olatile, (D)eleteVolatile, repli(c)ate, repli(C)ateAttribs\n"
"identity (S)etNew/(R)esetToDefault, set(m)aster,\n"
"(r)ead, (w)rite, e(x)ecutable, setE(X)ecutable, si(z)e, setSi(Z)e,\n"
"(3)timestamp, (4)setTimestamp, get(B)ase, fpToS(H)ortId,\n"
"(M)easureDirectory, c(O)llapseBase, read(W)hole,\n"
"(q)uit\n";
	    break;

	  case 'Y':
	    srcN = readSrcN(nextSrcN);
	    src[srcN]->resync();
	    break;

	  case 'p':
	    srcN = readSrcN(nextSrcN);
	    cout << "rep " << hex << (PointerInt) src[srcN]->rep << dec
	      << "\nattribs " << hex <<
		(PointerInt) src[srcN]->attribs << dec
		    << "\ntype " << VestaSource::typeTagString(src[srcN]->type)
		      << "\nlongid ";
	    coutLongId(src[srcN]->longid);
	    cout << "\ntimestamp " << src[srcN]->timestamp()
	      << "\npseudoInode " << hex << src[srcN]->pseudoInode << dec
		<< "\nmaster " << (int) src[srcN]->master
	          << "\nfingerprint ";
	    coutFPTag(src[srcN]->fptag);
// not implemented in VDirSurrogate:
//	    cout << "\nowner " << src[srcN]->ac.owner
//		   << ", group " << src[srcN]->ac.group
//		     << ", mode " << oct << (int)src[srcN]->ac.mode << dec;
	    cout << endl;
	    cout << vsToFilename(src[srcN]) << endl;
	    break;

	  case 's':
	    srcN = readSrcN(nextSrcN);
	    cout << hex << src[srcN]->shortId() << dec << endl;
	    break;

	  case 'a':
	    cout << "in ";
	    srcN = readSrcN(nextSrcN);
	    cout << "arc: ";
	    cin.get(arc, sizeof(arc));
	    cin.get(junk);
	    err = src[srcN]->lookup(arc, src[nextSrcN], who);
	    if (err == VestaSource::ok) {
		newSrcN = nextSrcN++;
		cout << "src# = " << newSrcN << endl;
	    } else {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    }
	    break;

	  case 'P':
	    cout << "in ";
	    srcN = readSrcN(nextSrcN);
	    cout << "pathname: ";
	    cin.get(arc, sizeof(arc));
	    cin.get(junk);
	    err = src[srcN]->lookupPathname(arc, src[nextSrcN], who);
	    if (err == VestaSource::ok) {
		newSrcN = nextSrcN++;
		cout << "src# = " << newSrcN << endl;
	    } else {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    }
	    break;

	  case 'i':
	    cout << "in ";
	    srcN = readSrcN(nextSrcN);
	    cout << "arc: ";
	    cin.get(arc, sizeof(arc));
	    cin.get(junk);
	    cout << "master (0 or 1): ";
	    cin >> master;
	    cin.get(junk);
	    cout << "dupeCheck (0=dontR 1=rDiff): ";
	    cin >> chk;
	    cin.get(junk);
	    cout << "(f)ile, m(u)File, (i)mDir, (a)pDir, (m)uDir, (g)host, (s)tub: ";
	    cin.get(typein, sizeof(typein));
	    cin.get(junk);
	    switch (typein[0]) {
	      case 'f':
	      case 'u':
		cout << "shortId: ";
		cin >> hex >> sid >> dec;
		cin.get(junk);
		if (typein[0] == 'f') {
		    err = src[srcN]->
		      insertFile(arc, sid, (bool) master, who,
				 (VestaSource::dupeCheck) chk,
				 &src[nextSrcN]);
		} else {
		    err = src[srcN]->
		      insertMutableFile(arc, sid, (bool) master, who,
					(VestaSource::dupeCheck) chk,
					&src[nextSrcN]);
		}
		if (err == VestaSource::ok) {
		    cout << "new src# = " << nextSrcN++;
		    cout << endl;
		} else {
		    cout << "error " << VestaSource::errorCodeString(err) << endl;
		}
		break;

	      case 'i':
		cout << "mutable directory (-1 for NULL) ";
		newSrcN = readSrcN(nextSrcN);
		err = src[srcN]->insertImmutableDirectory(arc,
			(newSrcN == -1 ? NULL : src[newSrcN]), (bool) master, 
			who, (VestaSource::dupeCheck) chk, &src[nextSrcN]);
		if (err == VestaSource::ok) {
		    cout << "new src# = " << nextSrcN++;
		    cout << endl;
		} else {
		    cout << "error " << VestaSource::errorCodeString(err) << endl;
		}
		break;

	      case 'a':
		err = src[srcN]->insertAppendableDirectory(
                        arc, (bool) master, who,
			(VestaSource::dupeCheck) chk, &src[nextSrcN]);
		if (err == VestaSource::ok) {
		    cout << "new src# = " << nextSrcN++;
		    cout << endl;
		} else {
		    cout << "error " << VestaSource::errorCodeString(err) << endl;
		}
		break;

	      case 'm':
		cout << "immutable directory (-1 for NULL) ";
		newSrcN = readSrcN(nextSrcN);
		err = src[srcN]->insertMutableDirectory(
			arc, newSrcN == -1 ? NULL : src[newSrcN], 
                        (bool) master, who,
			(VestaSource::dupeCheck) chk, &src[nextSrcN]);
		if (err == VestaSource::ok) {
		    cout << "new src# = " << nextSrcN++;
		    cout << endl;
		} else {
		    cout << "error " << VestaSource::errorCodeString(err)
		      << endl;
		}
		break;

	      case 'g':
		err =
		  src[srcN]->insertGhost(arc, (bool) master, who,
					 (VestaSource::dupeCheck) chk,
					 &src[nextSrcN]);
		if (err == VestaSource::ok) {
		    cout << "new src# = " << nextSrcN++;
		    cout << endl;
		} else {
		    cout << "error " << VestaSource::errorCodeString(err)
		      << endl;
		}
		break;

	      case 's':
		err =
		  src[srcN]->insertStub(arc, (bool) master, who,
					(VestaSource::dupeCheck) chk,
					&src[nextSrcN]);
		if (err == VestaSource::ok) {
		    cout << "new src# = " << nextSrcN++;
		    cout << endl;
		} else {
		    cout << "error " << VestaSource::errorCodeString(err)
		      << endl;
		}
		break;

	      default:
		break;
	    }
	    break;

	  case 'd':
	    cout << "from";
	    srcN = readSrcN(nextSrcN);
	    cout << "arc: ";
	    cin.get(arc, sizeof(arc));
	    cin.get(junk);
	    cout << "existCheck (0 or 1): ";
	    cin >> chk;
	    cin.get(junk);
	    err = src[srcN]->reallyDelete(arc, who, (bool) chk);
	    if (err == VestaSource::ok) {
		cout << "ok" << endl;
	    } else {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    }
	    break;

	  case 'l':
	    srcN = readSrcN(nextSrcN);
	    cout << "(d)eltaOnly, (a)ll entries: ";
	    cin.get(typein, sizeof(typein));
	    cin.get(junk);
	    err = src[srcN]->list(0, myListCallback, NULL, who,
				  typein[0] == 'd');
	    if (err != VestaSource::ok) {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    }
	    break;

	  case 'B':
		srcN = readSrcN(nextSrcN);
		err = src[srcN]->getBase(src[nextSrcN], who);
		if (err != VestaSource::ok) {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    } else {
		newSrcN = nextSrcN++;
		cout << "src# = " << newSrcN << endl;
	    }
	    break;

	  case 'u':
	    srcN = readSrcN(nextSrcN);
	    if (src[srcN]->type == VestaSource::immutableFile ||
		src[srcN]->type == VestaSource::mutableFile) {
		cout << "shortId: ";
		cin >> hex >> sid >> dec;
		cin.get(junk);
	    }
	    err = src[srcN]->makeMutable(src[nextSrcN], sid,
					 (Basics::uint64) -1, who);
	    if (err != VestaSource::ok) {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    } else {
		newSrcN = nextSrcN++;
		cout << "src# = " << newSrcN << endl;
	    }
	    break;

	  case 'e':
	    cout << "longid: ";
	    cinLongId(longid);
	    cout << "index: ";
	    cin >> index;
	    cin.get(junk);
	    cout << "child = ";
	    coutLongId(longid.append(index));
	    cout << endl;
	    break;

	  case 'g':
	    cout << "longid: ";
	    cinLongId(longid);
	    cout << "parent = ";
	    coutLongId(longid.getParent(&index));
	    cout << endl;
	    cout << "index = " << index << endl;
	    break;

	  case 'k':
	    cout << "longid: ";
	    cinLongId(longid);
	    cout << "host: ";
	    cin.get(arc, sizeof(arc));
	    cin.get(junk);
	    rhost = arc;
	    cout << "port: ";
	    cin.get(arc, sizeof(arc));
	    cin.get(junk);
	    rport = arc;
	    try {
		src[nextSrcN] =
		  VDirSurrogate::LongIdLookup(longid, rhost, rport);
		if (src[nextSrcN] != NULL) {
		    newSrcN = nextSrcN++;
		    cout << "src# = " << newSrcN << endl;
		} else {
		    cout << "error " << endl;
		}
	    } catch (SRPC::failure f) {
		cout << "error " << f.msg << " (" << f.r << ")" << endl;
	    }
	    break;

	  case 'v':
	    cout << "longid: ";
	    cinLongId(longid);
	    cout << "host: ";
	    cin.get(arc, sizeof(arc));
	    cin.get(junk);
	    rhost = arc;
	    cout << "port: ";
	    cin.get(arc, sizeof(arc));
	    cin.get(junk);
	    rport = arc;
	    try {
	      tmp = VDirSurrogate::LongIdValid(longid, rhost, rport);
	      cout << (tmp ? "true" : "false") << endl;
	    } catch (SRPC::failure f) {
		cout << "error " << f.msg << " (" << f.r << ")" << endl;
	    }
	    break;

	  case 'H':
		cout << "fptag: ";
		cinFPTag(fptag);
		cout << "host: ";
		cin.get(arc, sizeof(arc));
		cin.get(junk);
		rhost = arc;
		cout << "port: ";
		cin.get(arc, sizeof(arc));
		cin.get(junk);
		rport = arc;
		try {
		sid = VDirSurrogate::fpToShortId(fptag, rhost, rport);
		cout << "sid = " << setw(8) << hex << sid << dec << endl;
	    } catch (SRPC::failure f) {
		cout << "error " << f.msg << " (" << f.r << ")" << endl;
	    }
	    break;

	  case 'o':
	    cout << "shortId: ";
	    cin >> hex >> sid >> dec;
	    cin.get(junk);
	    cout << "fptag: ";
	    cinFPTag(fptag);
	    longid = LongId::fromShortId(sid, &fptag);
	    src[nextSrcN] = longid.lookup();
	    if (src[nextSrcN] != NULL) {
		newSrcN = nextSrcN++;
		cout << "src# = " << newSrcN << endl;
	    } else {
		cout << "error " << endl;
	    }
	    break;

	  case 'h':
	    srcN = readSrcN(nextSrcN);
	    cout << boolText[(int) src[srcN]->hasAttribs()] << endl;;
	    break;
	    
	  case 'n':
	    srcN = readSrcN(nextSrcN);
	    cout << "name: ";
	    cin.get(arc, sizeof(arc));
	    cin.get(junk);
	    cout << "value: ";
	    cin.get(arc2, sizeof(arc2));
	    cin.get(junk);
	    cout << boolText[(int) src[srcN]->inAttribs(arc, arc2)] << endl;
	    break;

	  case '1':
	    srcN = readSrcN(nextSrcN);
	    cout << "name: ";
	    cin.get(arc, sizeof(arc));
	    cin.get(junk);
	    charstar = src[srcN]->getAttrib(arc);
	    if (charstar != NULL) {
		cout << charstar << endl;
	    }
	    break;

	  case '2':
	    srcN = readSrcN(nextSrcN);
	    cout << "name: ";
	    cin.get(arc, sizeof(arc));
	    cin.get(junk);
	    src[srcN]->getAttrib(arc, valueCb, NULL);
	    cout << endl;
	    break;

	  case 't':
	    srcN = readSrcN(nextSrcN);
	    src[srcN]->listAttribs(valueCb, NULL);
	    cout << endl;
	    break;

	  case 'y':
	    srcN = readSrcN(nextSrcN);
	    src[srcN]->getAttribHistory(historyCb, NULL);
	    break;

	  case 'b':
	    srcN = readSrcN(nextSrcN);
	    cout << "(s)et, (c)lear, (a)dd, (r)emove: ";
	    cin.get(typein, sizeof(typein));
	    cin.get(junk);
	    switch (typein[0]) {
	      case 's':
		op = VestaSource::opSet;
		break;
	      case 'c':
		op = VestaSource::opClear;
		break;
	      case 'a':
		op = VestaSource::opAdd;
		break;
	      case 'r':
		op = VestaSource::opRemove;
		break;
	      default:
		continue;
	    }
	    cout << "name: ";
	    cin.get(arc, sizeof(arc));
	    cin.get(junk);
	    if (op == VestaSource::opClear) {
		arc2[0] = '\000';
	    } else {
		cout << "value: ";
		cin.get(arc2, sizeof(arc2));
		cin.get(junk);
	    }
	    cout << "timestamp (0=now): ";
	    cin >> timestamp;
	    cin.get(junk);
	    src[srcN]->writeAttrib(op, arc, arc2, who, timestamp);
	    break;

	  case 'V':
	    cout << "hostname: ";
	    cin.get(arc, sizeof(arc));
	    cin.get(junk);
	    cout << "port: ";
	    cin.get(arc2, sizeof(arc2));
	    cin.get(junk);
	    cout << "handle (hex): ";
	    cin >> hex >> handle >> dec;
	    cin.get(junk);
	    cout << "readOnlyExisting (0/1): ";
	    cin >> ltmp;
	    cin.get(junk);
	    err = VDirSurrogate::createVolatileDirectory
	      (arc, arc2, handle, src[nextSrcN], (bool)ltmp);
	    if (err == VestaSource::ok) {
		newSrcN = nextSrcN++;
		cout << "src# = " << newSrcN << endl;
	    } else {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    }
	    break;

	  case 'D':
	    srcN = readSrcN(nextSrcN);
	    err = VDirSurrogate::deleteVolatileDirectory(src[srcN]);
	    if (err != VestaSource::ok) {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    }
	    break;

	  case 'c':
	    cout << "pathname: ";
	    cin.get(typein, sizeof(typein));
	    cin.get(junk);
	    cout << "asStub (0/1): ";
	    cin >> ltmp;
	    cin.get(junk);
	    cout << "asGhost (0/1): ";
	    cin >> tmp;
	    cin.get(junk);
	    cout << "dstHost: ";
	    cin.get(arc, sizeof(arc));
	    cin.get(junk);
	    cout << "dstPort: ";
	    cin.get(arc2, sizeof(arc2));
	    cin.get(junk);
	    cout << "srcHost: ";
	    cin.get(arc3, sizeof(arc3));
	    cin.get(junk);
	    cout << "srcPort: ";
	    cin.get(arc4, sizeof(arc4));
	    cin.get(junk);
	    err = VDirSurrogate::replicate(typein, (bool)ltmp, (bool)tmp,
					   arc, arc2, arc3, arc4,
					   PathnameSep, who, who);
	    if (err != VestaSource::ok) {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    }
	    break;

	  case 'C':
	    cout << "pathname: ";
	    cin.get(typein, sizeof(typein));
	    cin.get(junk);
	    cout << "includeAccess (0/1): ";
	    cin >> ltmp;
	    cin.get(junk);
	    cout << "dstHost: ";
	    cin.get(arc, sizeof(arc));
	    cin.get(junk);
	    cout << "dstPort: ";
	    cin.get(arc2, sizeof(arc2));
	    cin.get(junk);
	    cout << "srcHost: ";
	    cin.get(arc3, sizeof(arc3));
	    cin.get(junk);
	    cout << "srcPort: ";
	    cin.get(arc4, sizeof(arc4));
	    cin.get(junk);
	    err = VDirSurrogate::replicateAttribs(typein, (bool)ltmp,
						  arc, arc2, arc3, arc4,
						  PathnameSep, who, who);
	    if (err != VestaSource::ok) {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    }
	    break;

	  case 'N':
	    VDirSurrogate::getNFSInfo(charstar, longid, longid2);
	    cout << "socket " << charstar << endl << "root ";
	    coutLongId(longid);
	    cout << endl << "muRoot ";
	    coutLongId(longid2);
	    cout << endl;
	    delete charstar;
	    break;

          case 'S':
#if 0 //!!
	    who->aup_time = time(NULL);
	    cout << "machine name: ";
	    cin.get(who->aup_machname, MAX_MACHINE_NAME);
	    cin.get(junk);
	    cout << "uid: ";
	    cin >> who->aup_uid;
	    cin.get(junk);
	    cout << "gid: ";
	    cin >> who->aup_gid;
	    cin.get(junk);
	    cout << "# of gids: ";
	    cin >> who->aup_len;
	    cin.get(junk);
	    for (i=0; i<who->aup_len; i++) {
		cin >> who->aup_gids[i];
	    }
	    cin.get(junk);
	    print_identity(who);
#else
	    cout << "not reimplemented yet" << endl;
#endif
	    break;

          case 'R':
#if 0 //!!
	    default_identity(who);
	    print_identity(who);
#else
	    who = NULL;
#endif
	    break;

	  case 'f':
	    srcN = readSrcN(nextSrcN);
	    cout << "threshold: ";
	    cin >> ltmp;
	    cin.get(junk);
	    err = src[srcN]->makeFilesImmutable(ltmp, who);
	    if (err != VestaSource::ok) {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    }
	    break;

	  case 'm':
	    srcN = readSrcN(nextSrcN);
	    cout << "master (0 or 1): ";
	    cin >> master;
	    cin.get(junk);
	    err = src[srcN]->setMaster(master, who);
	    if (err != VestaSource::ok) {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    }
	    break;

	  case 'r':
	    srcN = readSrcN(nextSrcN);
	    cout << "nbytes: ";
	    cin >> i;
	    cout << "offset: ";
	    cin >> ltmp;
	    cin.get(junk);
	    charstar = (char *) malloc(i);
	    err = src[srcN]->read(charstar, &i, ltmp);
	    if (err != VestaSource::ok) {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    } else {
		cout << "read " << i << " bytes\n";
		cout << "============================================\n";
		cout.write(charstar, i);
		cout << "============================================\n";
	    }
	    free(charstar);
	    break;

	  case 'w':
	    srcN = readSrcN(nextSrcN);
	    cout << "nbytes: ";
	    cin >> i;
	    cout << "offset: ";
	    cin >> ltmp;
	    cin.get(junk);
	    charstar = (char *) malloc(i);
	    cout << "data: ";
	    cin.get(charstar, i);
	    cin.get(junk);
	    err = src[srcN]->write(charstar, &i, ltmp);
	    if (err != VestaSource::ok) {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    } else {
		cout << "wrote " << i << " bytes\n";
	    }
	    free(charstar);
	    break;

	  case 'x':
	    srcN = readSrcN(nextSrcN);
	    cout << boolText[(int) src[srcN]->executable()] << endl;
	    break;

	  case 'X':
	    srcN = readSrcN(nextSrcN);
	    cout << "executable (0 or 1): ";
	    cin >> tmp;
	    cin.get(junk);
	    err = src[srcN]->setExecutable(tmp);
	    if (err != VestaSource::ok) {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    }
	    break;

	  case 'z':
	    srcN = readSrcN(nextSrcN);
	    cout << "size " << src[srcN]->size() << endl;
	    break;

	  case 'Z':
	    srcN = readSrcN(nextSrcN);
	    cout << "size: ";
	    cin >> ltmp;
	    cin.get(junk);
	    err = src[srcN]->setSize(ltmp);
	    if (err != VestaSource::ok) {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    }
	    break;

	  case '3':
	    srcN = readSrcN(nextSrcN);
	    cout << "timestamp " << src[srcN]->timestamp() <<endl;
	    break;

	  case '4':
	    srcN = readSrcN(nextSrcN);
	    cout << "timestamp (decimal): ";
	    cin >> tmp;
	    cin.get(junk);
	    err = src[srcN]->setTimestamp((time_t)tmp);
	    if (err != VestaSource::ok) {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    }
	    break;

	  case 'M':
	    srcN = readSrcN(nextSrcN);
	    {
	      VestaSource::directoryStats stats;
	      err = src[srcN]->measureDirectory(stats);
	      if (err != VestaSource::ok)
		{
		  cout << "error " << VestaSource::errorCodeString(err) << endl;
		}
	      else
		{
		  cout
		    << "base chain length = " << stats.baseChainLength << endl
		    << "used entry count  = " << stats.usedEntryCount << endl
		    << "total entry count = " << stats.totalEntryCount << endl
		    << "used entry size   = " << stats.usedEntrySize << endl
		    << "total entry size  = " << stats.totalEntrySize << endl;
		}
	    }
	    cout << endl;
	    break;

	  case 'O':
	    srcN = readSrcN(nextSrcN);
	    err = src[srcN]->collapseBase();
	    if (err != VestaSource::ok) {
		cout << "error " << VestaSource::errorCodeString(err) << endl;
	    }
	    break;

	  case 'W':
	    srcN = readSrcN(nextSrcN);
	    // Read the entire file, sending it to standard output.
	    src[srcN]->readWhole(cout);
	    break;

	  case 'q':
	    exit(0);

	  default:
	    break;
	}
    }

    //return NULL; // not reached
}

int
main(int argc, char *argv[])
{
    int c;

    // Run the interactive test thread
    try {
	(void) test_thread((void *)NULL);
    } catch (VestaConfig::failure f) {
	cerr << f.msg << endl;
	exit(2);
    } catch (SRPC::failure f) {
	cerr << f.msg << " (" << f.r << ")" << endl;
	exit(3);
    }
    return 0;
}
