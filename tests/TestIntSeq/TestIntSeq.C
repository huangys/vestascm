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

// Last modified on Sun Jan  2 18:26:01 EST 2005 by ken@xorian.net
//      modified on Mon May  3 16:04:41 PDT 1999 by heydon
//      modified on Sat Feb 17 11:29:26 PST 1996 by mcjones

/* Based on /proj/m3/pkg/sequence/tests/src/Test.m3 as
   Last modified on Tue Nov  1 09:10:02 PST 1994 by kalsow */

#include <limits.h>
#include <stdlib.h>
#include <Basics.H>
#include "Sequence.H"

#include <algorithm> // STL algorithms package

// int -> int sequence
typedef Sequence<int> T;

using std::cout;
using std::endl;

bool Eq(const T& s, const T& t)
{
    const int n = s.size();
    if( n != t.size() ) return false;
    for( int i=0; i<n; i++ )
      if( s.get(i) != t.get(i) ) return false;
    return true;
}

T Iota(int n)
{
    T res(n);
    for( int i=0; i<n; i++ )
      res.addhi(i);
    return res;
}

T Rev(const T& s)
{
    const int n = s.size();
    T res(n);
    for( int i=0; i<n; i++ )
      res.addlo(s.get(i));
    return res;
}

T Sort(const T &s)
{
  T res(s);
  std::sort(res.begin(), res.end());
  return res;
}

T Rev_Iter(T &s)
{
  T res(s.size());
  for(T::Iterator it = s.begin(); it != s.end(); ++it)
    res.addlo(*it);
  return res;
}

int main() {

    typedef Sequence<int> T;

    T empty(0);
    int i;

    T iota10 = Iota(10);
    T revIota10 = Rev(iota10);
    T revRevIota10 = Rev(revIota10);
    T iota10a = Iota(10);
    assert ( Eq( revRevIota10, iota10a) );
    /* assert ( Eq(Rev(Rev(Iota(10))), Iota(10)) ); */
    assert (! Eq(Iota(5), Iota(0)));
    assert (! Eq(Iota(5), Rev(Iota(5))));

    T s1 = Iota(5);
    for( i=4; 0<=i; i-- ) assert( s1.remhi() == i );
    assert( Eq( s1, empty ) );

    T s2 = Iota(100);
    assert( s2.size() == 100 );
    for( i=0; i<100; i++ ) assert( s2.remlo() == i );
    assert( Eq( s2, empty ) );

    assert( Iota(12).gethi() == 11 );

    assert( Iota(12).getlo() == 0 );

    T s3 = Iota(10);
    for( i=0; i<10; i++ ) s3.put(i, i);
    assert( Eq( s3, Iota(10) ) );

    int *arNone = 0;

    T s4(arNone, 0);
    s4.addhi(0); s4.addhi(1); s4.addhi(2);
    assert( Eq( Iota(3), s4 ) );

    int arIota5[] = {0, 1, 2, 3, 4};
    T sIota5(arIota5, 5);
    assert( Eq( Iota(5), sIota5 ) );

    T s5 = empty.cat(empty);
    s5.addhi(0); s5.addhi(1); s5.addhi(2);
    assert( Eq( Iota(3), s5 ) );


    assert( Eq( Iota(5), Iota(5).cat( Iota(0) ) ) );
    assert( Eq( Iota(5), Iota(0).cat( Iota(5) ) ) );

    T s6 = Iota(5).cat( Rev(Iota(5)) );
    for( i=0; i<5; i++ )
    {
        assert( s6.get(i) == i );
        assert( s6.get(i + 5) == 4 - i );
    }

    int ar34[] = {3, 4};
    T s34(ar34, 2);
    assert( Eq( Iota(5).sub(3), s34 ) );

    assert( Eq( Iota(5).sub(0), Iota(5) ) );
    assert( Eq( Iota(5).sub(5, 5), Iota(0) ) );
    assert( Eq( Iota(5).sub(0, 4), Iota(4) ) );
    assert( Eq( Rev(Iota(5)).sub(1, 4), Rev(Iota(4)) ) );

    T s7(5);
    s7.addhi(0);
    s7.addhi(0);
    (void)s7.remlo();
    (void)s7.remlo();
    for( i=0; i<5; i++) s7.addhi(i);
    assert( Eq( s7, Iota(5) ) );

    // Try simple use of Sequence::Iterator
    assert( Eq( Rev_Iter(s7), Rev(s7)));

    // Try sorting a Sequence
    assert( Eq(Sort(Rev(Iota(20))), Iota(20)));

 /*   
    s7.~T();
    s34.~T();
    s6.~T();
    s5.~T();
    sIota5.~T();
    s4.~T();
    s3.~T();
    s2.~T();
    s1.~T();
    iota10a.~T();
    revRevIota10.~T();
    revIota10.~T();
    iota10.~T();
    empty.~T();
    
    abort();
*/

    cout << "All tests passed!" << endl;
    return 0;
}
