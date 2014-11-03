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
// Initial test of logging code
//

#include "VestaLog.H"
#include <stdlib.h>

using std::cin;
using std::cout;
using std::cerr;
using std::endl;
using std::fstream;

VestaLog VRLog;

int main() 
{
    int cknum, ckpkeep, logkeep, readonly, lock, bakckp;
    char typein[500];
    char junk;
    int done;

  initial:
    cout << "recover from checkpoint # (-1 for last): ";
    cin >> cknum;
    cin.get(junk);
    cout << "readonly (0/1)? ";
    cin >> readonly;
    cin.get(junk);
    cout << "lock (0/1)? ";
    cin >> lock;
    cin.get(junk);
    cout << "backup directory (default none): ";
    cin.get(typein, sizeof(typein));
    cin.get(junk);
    if (typein[0]) {
      cout << "backup checkpoint (0/1)? ";
      cin >> bakckp;
      cin.get(junk);
    } else {
      bakckp = 0;
    }
    try {
        VRLog.open(".", cknum, readonly, lock,
	  	   (typein[0] ? typein : NULL), bakckp);
    } catch (VestaLog::Error err) {
	cerr << "** error on open **: " << err.msg << endl;
	exit(1);
    }
    fstream *ckpt;
    try {
	ckpt = VRLog.openCheckpoint();
    } catch (VestaLog::Error err) {
	cerr << "** error on openCheckpoint **: " << err.msg << endl;
	exit(1);
    }
    if (ckpt == NULL) {
	cout << "No checkpoint file\n";
    } else {
	char c;
	cout << "-------checkpoint file-------\n";
	while (ckpt->get(c)) cout.put(c);
	ckpt->close();
	cout << "\n";
    }

    cout << "-------recovered log-------\n";
    for (;;) {
	char c;

	try {
	    VRLog.get(c);
	    cout.put(c);
	} catch (VestaLog::Error err) {
	    cerr << "** error on get **: " << err.msg << endl;
	    exit(1);
	} catch (VestaLog::Eof) {
	  try {
	    if (!VRLog.nextLog()) break;
	  } catch (VestaLog::Error err) {
	    cerr << "** error on nextLog **: " << err.msg << endl;
	    exit(1);
	  }
        }
    }
    cout << "\n-----------------------------\n";

    for (;;) {
	cout << "(l)og data, close and (r)estart, cl(o)se and quit, (p)rune, (c)heckpoint resume, (q)uit: ";
	cin.get(typein, sizeof(typein));
	cin.get(junk);
	switch (typein[0]) {
	  case 'l':
	    try {
		VRLog.loggingBegin();
	    } catch (VestaLog::Error err) {
		cerr << "** error on loggingBegin **: " << err.msg << endl;
		exit(1);
	    }
	    break;
	  case 'r':
	    VRLog.close();
	    goto initial;
	  case 'o':
	    VRLog.close();
	    exit(0);
	    break;
	  case 'p':
	    cout << "ckpkeep: ";
	    cin >> ckpkeep;
	    cin.get(junk);
	    cout << "logkeep (0=false, 1=true): ";
	    cin >> logkeep;
	    cin.get(junk);
	    VRLog.prune(ckpkeep, logkeep);
	    continue;
	  case 'c':
	    {
		fstream *nckpt;
		try {
		    nckpt = VRLog.checkpointResume();
		} catch (VestaLog::Error err) {
		    cerr << "** error on checkpointResume **: " <<
		      err.msg << endl;
		    exit(1);
		}
		if (nckpt == NULL) {
		    cout << "No checkpoint in progress" << endl;
		} else {
		    cout << "checkpoint data: ";
		    cin.get(typein, sizeof(typein));
		    cin.get(junk);
		    *nckpt << typein;
		    nckpt->close();
		}
	    }
	    continue;
	  case 'q':
	    exit(0);
	    break;
	  default:
	    continue;
	}
	break;
    }


  ready:
    for (;;) {
	cout << "(s)tart record, chec(k)point, checkpoint (e)nd, checkpoint (a)bort, (p)rune, cl(o)se and quit, (q)uit: ";
	cin.get(typein, sizeof(typein));
	cin.get(junk);
	switch (typein[0]) {
	  case 's':
	    VRLog.start();
	    break;
	  case 'k':
	    {
		fstream *nckpt;
		try {
		    nckpt = VRLog.checkpointBegin();
		} catch (VestaLog::Error err) {
		    cerr << "** error on checkpointBegin **: " <<
		      err.msg << endl;
		    exit(1);
		}
		cout << "checkpoint data: ";
		cin.get(typein, sizeof(typein));
		cin.get(junk);
		*nckpt << typein;
		nckpt->close();
	    }
	    continue;
	  case 'e':
	    try {
		VRLog.checkpointEnd();
	    } catch (VestaLog::Error err) {
		cerr << "** error on checkpointEnd **: " <<
		  err.msg << endl;
		exit(1);
	    }
	    continue;
	  case 'a':
	    try {
		VRLog.checkpointAbort();
	    } catch (VestaLog::Error err) {
		cerr << "** error on checkpointAbort **: " <<
		  err.msg << endl;
		exit(1);
	    }
	    continue;
	  case 'o':
	    VRLog.close();
	    exit(0);
	    break;
	  case 'p':
	    cout << "ckpkeep: ";
	    cin >> ckpkeep;
	    cin.get(junk);
	    cout << "logkeep (0=false, 1=true): ";
	    cin >> logkeep;
	    cin.get(junk);
	    VRLog.prune(ckpkeep, logkeep);
	    continue;
	  case 'q':
	    exit(0);
	    break;
	  default:
	    continue;
	}
	break;
    }

    for (;;) {
	cout << "(p)ut data, (c)ommit, (a)bort, cl(o)se and quit, (q)uit: ";
	cin.get(typein, sizeof(typein));
	cin.get(junk);
	switch (typein[0]) {
	  case 'p':
	    cout << "log data: ";
	    cin.get(typein, sizeof(typein));
	    cin.get(junk);
	    try {
		VRLog.put(typein);
	    } catch (VestaLog::Error err) {
		cerr << "** error on put **: " << err.msg << endl;
		exit(1);
	    }
	    break;
	  case 'c':
	    try {
		VRLog.commit();
	    } catch (VestaLog::Error err) {
		cerr << "** error on commit **: " << err.msg << endl;
		exit(1);
	    }
	    goto ready;
	  case 'a':
	    try {
		VRLog.abort();
	    } catch (VestaLog::Error err) {
		cerr << "** error on abort **: " << err.msg << endl;
		exit(1);
	    }
	    goto ready;
	  case 'o':
	    VRLog.close();
	    exit(0);
	    break;
	  case 'q':
	    exit(0);
	    break;
	}
    }

    return 0;
}
