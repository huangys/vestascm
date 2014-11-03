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

// Last modified on Sun May 22 21:31:07 EDT 2005 by ken@xorian.net
//      modified on Wed Dec 13 11:19:23 PST 2000 by yuanyu
//      modified on Fri Mar 28 09:19:00 PST 1997 by heydon

#include "Basics.H"

using std::cout;
using std::endl;

const int Arg1 = 4;
const int Res1 = 5;
const int PauseSecs = 1;
const int MainPause = 4;
const int SubPause = 2;

Basics::mutex mu;

void* Thread1(void *arg)
{
    mu.lock();
    cout << "  Starting Thread1...\n";

    int val = *(int *)arg;
    assert(val == Arg1);
    cout << "  Got arg " << val << " okay.\n";
    delete (int *)arg;

    cout << "  Producing result " << Res1 << ".\n";
    int *res = NEW(int);
    *res = Res1;

    cout << "  Done Thread1.\n";
    mu.unlock();
    return res;
}

void *Thread2(void *arg)
{
    mu.lock();
    cout << "  Starting Thread2...\n";
    (cout << "  Pausing " << PauseSecs << " second(s)...\n").flush();
    Basics::thread::pause(PauseSecs);
    (cout << "  Done Thread2.\n").flush();
    mu.unlock();
    return (void *)NULL;
}

void *Thread3(void *arg)
{
    mu.lock();
    cout << "  Starting Thread2...\n";
    (cout << "  Pausing " << SubPause << " second(s)...\n").flush();
    mu.unlock();
    Basics::thread::pause(SubPause);
    mu.lock();
    (cout << "  Done Thread2.\n").flush();
    mu.unlock();
    return (void *)NULL;
}

int main(void)
{
    mu.lock();
    cout << "Starting main thread...\n\n";

    cout << "Forking Thread1(" << Arg1 << ")...\n";
    Basics::thread th;
    int *arg1 = NEW(int); *arg1 = Arg1;
    th.fork(Thread1, arg1);
    cout << "Joining Thread1...\n";
    mu.unlock();
    int *res = (int *)(th.join());
    assert(*res == Res1);
    mu.lock();
    cout << "Got result " << *res << " okay.\n\n";
    delete res;

    cout << "Forking Thread2...\n";
    th.fork(Thread2, (void *)NULL);
    cout << "Joining Thread2...\n";
    mu.unlock();
    res = (int *)(th.join());
    assert(res == (void *)NULL);
    mu.lock();
    cout << "Joined okay.\n\n";

    cout << "Forking and detaching Thread3...\n";
    mu.unlock();
    th.fork_and_detach(Thread3, (void *)NULL);
    mu.lock();
    (cout << "Main thread pausing " << MainPause << " second(s)...\n").flush();
    mu.unlock();
    Basics::thread::pause(MainPause);
    mu.lock();
    (cout << "Paused okay.\n\n").flush();

    cout << "Done main thread." << endl;
    mu.unlock();

    return 0;
}
